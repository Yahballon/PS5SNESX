// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "CDVD/ThreadedFileReader.h"

#include <cstdio>
#ifdef ORBIS_NFS
#include "ps5/frontend/fe_nfs.h"
#endif

class FlatFileReader final : public ThreadedFileReader
{
	DeclareNoncopyableObject(FlatFileReader);

	std::FILE* m_file = nullptr;
#ifdef ORBIS_NFS
	std::unique_ptr<fe::nfs::File> m_nfs;
#endif
	std::unique_ptr<u8[]> m_file_cache;
	u64 m_file_size = 0;

public:
	FlatFileReader();
	~FlatFileReader() override;

	bool Open2(std::string filename, Error* error) override;

	bool Precache2(ProgressCallback* progress, Error* error) override;

	Chunk ChunkForOffset(u64 offset) override;
	int ReadChunk(void* dst, s64 blockID) override;

	void Close2() override;

	u32 GetBlockCount() const override;
};
