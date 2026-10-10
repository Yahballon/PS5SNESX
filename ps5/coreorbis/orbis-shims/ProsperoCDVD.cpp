// Orbis CDVD stubs: .gz images need PCSX2's indexed-zlib reader (GzippedFileReader), which isn't
// built, so that backend refuses. Raw .iso/.bin works fully via the real InputIsoFile
// (kept in build); .chd since vk-285-108 (the real ChdFileReader over libchdr, with zlib and
// zstd vendored in ps5/third_party); .cso and .zso since vk-285-113 (the real CsoFileReader over
// the vendored inflate and lz4). Physical-disc reader (Linux ioctl) likewise unavailable.
#include "CDVD/GzippedFileReader.h"
#include "CDVD/CDVDdiscReader.h"
#include "common/Error.h"

// (disc globals live in CDVDdiscReader.cpp already.)

IOCtlSrc::IOCtlSrc(std::string filename)
  : m_filename(std::move(filename))
{
}
IOCtlSrc::~IOCtlSrc() = default;
bool IOCtlSrc::Reopen(Error* error)
{
  Error::SetString(error, "Physical discs unsupported on Orbis");
  return false;
}
u32 IOCtlSrc::GetSectorCount() const
{
  return 0;
}
const std::vector<toc_entry>& IOCtlSrc::ReadTOC() const
{
  static const std::vector<toc_entry> empty;
  return empty;
}
bool IOCtlSrc::ReadSectors2048(u32 sector, u32 count, u8* buffer) const
{
  (void)sector;
  (void)count;
  (void)buffer;
  return false;
}
bool IOCtlSrc::ReadSectors2352(u32 sector, u32 count, u8* buffer) const
{
  (void)sector;
  (void)count;
  (void)buffer;
  return false;
}
bool IOCtlSrc::ReadTrackSubQ(cdvdSubQ* subq) const
{
  (void)subq;
  return false;
}
u32 IOCtlSrc::GetLayerBreakAddress() const
{
  return 0;
}
s32 IOCtlSrc::GetMediaType() const
{
  return 0;
}
bool IOCtlSrc::DiscReady()
{
  return false;
}
void GetValidDrive(std::string& drive)
{
  drive.clear();
}

static bool Unsupported(Error* error, const char* what)
{
  Error::SetString(error, what);
  return false;
}

GzippedFileReader::GzippedFileReader() = default;
GzippedFileReader::~GzippedFileReader() = default;
bool GzippedFileReader::Open2(std::string filename, Error* error)
{
  (void)filename;
  return Unsupported(error, "GZIP images not supported on Orbis (use .cso, .zso or .chd)");
}
ThreadedFileReader::Chunk GzippedFileReader::ChunkForOffset(u64 offset)
{
  (void)offset;
  return ThreadedFileReader::Chunk{-1, 0, 0};
}
int GzippedFileReader::ReadChunk(void* dst, s64 chunkID)
{
  (void)dst;
  (void)chunkID;
  return -1;
}
void GzippedFileReader::Close2()
{
}
u32 GzippedFileReader::GetBlockCount() const
{
  return 0;
}
