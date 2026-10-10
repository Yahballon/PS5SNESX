// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "FlatFileReader.h"

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Error.h"

#include <cerrno>
#include <cstring>

static constexpr size_t CHUNK_SIZE = 128 * 1024;

FlatFileReader::FlatFileReader() = default;

FlatFileReader::~FlatFileReader()
{
	pxAssert(!m_file);
#ifdef ORBIS_NFS
	pxAssert(!m_nfs);
#endif
}

bool FlatFileReader::Open2(std::string filename, Error* error)
{
	m_filename = std::move(filename);
#ifdef ORBIS_NFS
	if (fe::nfs::IsUrl(m_filename.c_str()))
	{
		m_nfs = std::make_unique<fe::nfs::File>();
		std::string message;
		if (!m_nfs->Open(m_filename, message))
		{
			Error::SetString(error, message);
			m_nfs.reset();
			return false;
		}
		m_file_size = m_nfs->Size();
		return true;
	}
#endif
	if (!(m_file = FileSystem::OpenCFile(m_filename.c_str(), "rb", error)))
		return false;

	const s64 filesize = FileSystem::FSize64(m_file);
	if (filesize <= 0)
	{
		Error::SetStringView(error, "Failed to determine file size.");
		Close2();
		return false;
	}

	m_file_size = static_cast<u64>(filesize);
	return true;
}

bool FlatFileReader::Precache2(ProgressCallback* progress, Error* error)
{
#ifdef ORBIS_NFS
	if (m_nfs)
	{
		Error::SetStringView(error, "Precaching NFS images is unsupported; disable disc precaching.");
		return false;
	}
#endif
	if (!m_file || !CheckAvailableMemoryForPrecaching(m_file_size, error))
		return false;

	m_file_cache = std::make_unique_for_overwrite<u8[]>(m_file_size);
	if (FileSystem::FSeek64(m_file, 0, SEEK_SET) != 0 ||
		FileSystem::ReadFileWithProgress(
			m_file, m_file_cache.get(), m_file_size, progress, error) != m_file_size)
	{
		m_file_cache.reset();
		return false;
	}

	std::fclose(m_file);
	m_file = nullptr;
	return true;
}

ThreadedFileReader::Chunk FlatFileReader::ChunkForOffset(u64 offset)
{
	ThreadedFileReader::Chunk chunk = {};
	if (offset >= m_file_size)
	{
		chunk.chunkID = -1;
	}
	else
	{
		chunk.chunkID = offset / CHUNK_SIZE;
		chunk.length = static_cast<u32>(std::min<u64>(m_file_size - offset, CHUNK_SIZE));
		chunk.offset = static_cast<u64>(chunk.chunkID) * CHUNK_SIZE;
	}

	return chunk;
}

int FlatFileReader::ReadChunk(void* dst, s64 blockID)
{
	if (blockID < 0)
		return -1;

	const u64 file_offset = static_cast<u64>(blockID) * CHUNK_SIZE;
#ifdef ORBIS_NFS
	if (m_nfs)
	{
		if (file_offset >= m_file_size)
			return -1;
		const size_t read_size = static_cast<size_t>(std::min<u64>(m_file_size - file_offset, CHUNK_SIZE));
		return m_nfs->ReadAt(file_offset, dst, read_size) ? static_cast<int>(read_size) : -1;
	}
#endif
	if (m_file_cache)
	{
		if (file_offset >= m_file_size)
			return -1;

		const u64 read_size = std::min<u64>(m_file_size - file_offset, CHUNK_SIZE);
		std::memcpy(dst, &m_file_cache[file_offset], read_size);
		return static_cast<int>(read_size);
	}

	if (FileSystem::FSeek64(m_file, file_offset, SEEK_SET) != 0)
		return -1;

	const u32 read_size = static_cast<u32>(std::min<u64>(m_file_size - file_offset, CHUNK_SIZE));

	return (std::fread(dst, read_size, 1, m_file) == 1) ? static_cast<int>(read_size) : 0;
}

void FlatFileReader::Close2()
{
#ifdef ORBIS_NFS
	m_nfs.reset();
	if (!m_file)
		m_file_size = 0;
#endif
	if (!m_file)
		return;

	std::fclose(m_file);
	m_file = nullptr;
	m_file_size = 0;
}

u32 FlatFileReader::GetBlockCount() const
{
	return static_cast<u32>(m_file_size / m_blocksize);
}
