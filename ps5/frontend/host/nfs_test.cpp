// (AI-assisted) Backend tests without a live NFS server or console SDK.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fe_nfs.h"
#include "fe_url_keyboard.h"
#ifndef _WIN32
#include "fe_file_io.h"
#endif
#include <nfsc/libnfs.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <fcntl.h>

static int contexts, handles, directories, directory_index, reads;
static bool mount_failure, open_failure, stat_failure, read_failure, eof;
static uint64_t image_size = (uint64_t{1} << 33) + 2048;
static std::string server, export_path, file_path;
static std::string listing_path;
static bool parent_export_only;
#ifndef _WIN32
static int write_budget = -1;
static bool interrupt_write = false;
static size_t short_write = 0;
extern "C" ssize_t __real_write(int fd, const void* buffer, size_t length);
extern "C" ssize_t __wrap_write(int fd, const void* buffer, size_t length)
{
	if (interrupt_write)
	{
		interrupt_write = false;
		errno = EINTR;
		return -1;
	}
	if (write_budget == 0)
	{
		errno = EIO;
		return -1;
	}
	if (short_write)
		length = std::min(length, short_write);
	if (write_budget > 0)
		length = std::min(length, static_cast<size_t>(write_budget));
	const ssize_t n = __real_write(fd, buffer, length);
	if (write_budget > 0 && n > 0)
		write_budget -= static_cast<int>(n);
	return n;
}
#endif

nfs_context* nfs_init_context()
{
	contexts++;
	return new nfs_context;
}
void nfs_destroy_context(nfs_context* p)
{
	contexts--;
	delete p;
}
void nfs_set_readdir_max_buffer_size(nfs_context*,uint32_t dir,uint32_t max){assert(dir==65536&&max==262144);}
int nfs_get_fd(nfs_context*){return -1;}
void nfs_set_timeout(nfs_context*, int ms) { assert(ms == 5000); }
void nfs_set_autoreconnect(nfs_context*, int retries) { assert(retries == 0); }
int nfs_set_version(nfs_context*, int version)
{
	assert(version == 3);
	return 0;
}
int nfs_mount(nfs_context*, const char* s, const char* p)
{
	server = s;
	export_path = p;
	if (parent_export_only && export_path == "/export/PS2")
		return -EACCES;
	return mount_failure ? -1 : 0;
}
const char* nfs_get_error(nfs_context*) { return "simulated failure"; }
int nfs_opendir(nfs_context*, const char* path, nfsdir** out)
{
	listing_path = path;
	directory_index = 0;
	directories++;
	*out = new nfsdir;
	return 0;
}
static bool zip_listing=false;
nfsdirent* nfs_readdir(nfs_context*, nfsdir*)
{
	static char dot[] = ".", iso[] = "Game (USA).iso", folder[] = "DVD", link[] = "link.iso", unknown[] = "Unknown.ISO";
	static nfsdirent entries[] = {{dot, 2, 0}, {iso, 1, 8192}, {folder, 2, 0}, {link, 5, 0}, {unknown, 0, 0}};
	entries[4].name=zip_listing?const_cast<char*>("Unknown.ZIP"):unknown;
	return directory_index < 5 ? &entries[directory_index++] : nullptr;
}
void nfs_closedir(nfs_context*, nfsdir* p)
{
	directories--;
	delete p;
}
int nfs_stat64(nfs_context*, const char*, nfs_stat_64* out)
{
	*out = {image_size, 0100444};
	return stat_failure ? -1 : 0;
}
int nfs_open(nfs_context*, const char* name, int flags, nfsfh** out)
{
	assert(flags == O_RDONLY);
	file_path = name;
	if (open_failure)
		return -1;
	handles++;
	*out = new nfsfh;
	return 0;
}
int nfs_close(nfs_context*, nfsfh* p)
{
	handles--;
	delete p;
	return 0;
}
int nfs_fstat64(nfs_context* c, nfsfh*, nfs_stat_64* out) { return nfs_stat64(c, "", out); }
int nfs_lstat64(nfs_context* c, const char* path, nfs_stat_64* out) { return nfs_stat64(c, path, out); }
int nfs_pread(nfs_context*, nfsfh*, uint64_t offset, uint64_t count, void* out)
{
	reads++;
	if (read_failure)
		return -1;
	if (eof)
		return 0;
	assert(count <= 128 * 1024);
	const int n = static_cast<int>(std::min<uint64_t>(count, 997)); // short reads
	for (int i = 0; i < n; i++)
		static_cast<unsigned char*>(out)[i] = (offset + i) & 255;
	return n;
}

int main(int argc, char** argv)
{
	assert(argc == 2);
	std::string error;
#ifndef _WIN32
	const std::string persistence_path = std::string(argv[1]) + "-parent/nfs.txt";
	short_write = 2;
	interrupt_write = true;
	assert(fe::nfs::SaveConfiguration(persistence_path, "nfs://host/games", error));
	short_write = 0;
	assert(fe::nfs::ConfiguredDirectories(persistence_path) == std::vector<std::string>{"nfs://host/games"});
	std::string persisted;
	assert(fe::nfs::ReadConfiguration(persistence_path, persisted, error));
	assert(persisted == "nfs://host/games\n");
	write_budget = 3;
	assert(!fe::nfs::SaveConfiguration(persistence_path, "nfs://broken/games", error));
	assert(error.find("write") != std::string::npos);
	write_budget = -1;
	assert(fe::nfs::ReadConfiguration(persistence_path, persisted, error) && persisted == "nfs://host/games\n");
	assert(!fe::nfs::SaveConfiguration(persistence_path, "bad-url", error));
	assert(fe::nfs::ReadConfiguration(persistence_path, persisted, error) && persisted == "nfs://host/games\n");
	assert(fe::nfs::SaveConfiguration(persistence_path, "nfs://new/games", error));
	assert(fe::nfs::ReadConfiguration(persistence_path, persisted, error) && persisted == "nfs://new/games\n");
	assert(fe::nfs::SaveConfiguration(persistence_path, "", error));
	assert(fe::nfs::ConfiguredDirectories(persistence_path).empty());
	const std::string settings_path = std::string(argv[1]) + "-settings/gs.ini";
	assert(fe::fileio::WriteAtomic(settings_path, "upscale_multiplier=2\n", error));
	assert(fe::fileio::Read(settings_path, persisted, error) && persisted == "upscale_multiplier=2\n");
	assert(!fe::fileio::WriteAtomic(settings_path + "/invalid", "bad", error));
	assert(error.find("errno") != std::string::npos);
	assert(fe::fileio::Read(settings_path, persisted, error) && persisted == "upscale_multiplier=2\n");
#endif
	std::string edited;
	const std::string original = "# Keep this comment\r\nnfs://old/export\r\n# Second share\nnfs://backup/games\n";
	assert(fe::nfs::UpdatePrimaryDirectory(original, "nfs://new/PS2 Games", edited, error));
	assert(edited == "# Keep this comment\r\nnfs://new/PS2 Games\n# Second share\nnfs://backup/games\n");
	assert(fe::nfs::UpdatePrimaryDirectory(original, "", edited, error));
	assert(edited == "# Keep this comment\r\n# Second share\nnfs://backup/games\n");
	assert(fe::nfs::UpdatePrimaryDirectory("# Existing comment", "nfs://host/", edited, error));
	assert(edited == "# Existing comment\nnfs://host/\n");
	for (const std::string invalid : {"192.168.1.2/export", "nfs://host", "nfs:///export", "nfs://host:2049/export", "nfs://host/export?version=4", "nfs://host/export\nother"})
	{
		edited = original;
		assert(!fe::nfs::UpdatePrimaryDirectory(original, invalid, edited, error));
		assert(edited == original && !error.empty());
	}
	fe::UrlKeyboard keyboard;
	keyboard.Open("nfs://");
	assert(keyboard.Activate() == fe::UrlKeyboard::Action::Changed && keyboard.text() == "nfs://1");
	keyboard.Delete();
	assert(keyboard.text() == "nfs://");
	keyboard.Open("nfs://host/\xc3\xa4");
	keyboard.Delete();
	assert(keyboard.text() == "nfs://host/");
	for (int i = 0; i < 7; i++)
		keyboard.Move(0, 1);
	keyboard.Move(1, 0); // Clear
	assert(keyboard.Activate() == fe::UrlKeyboard::Action::Changed && keyboard.text().empty());
	keyboard.Move(1, 0); // Save
	assert(keyboard.Activate() == fe::UrlKeyboard::Action::Save);
	keyboard.Move(1, 0); // Cancel
	assert(keyboard.Activate() == fe::UrlKeyboard::Action::Cancel);
	for (int i = 0; i < 100; i++)
	{
		keyboard.Move(1, 0);
		keyboard.Move(0, i % 2 ? -1 : 1);
		assert(keyboard.selected() >= 0 && keyboard.selected() < fe::UrlKeyboard::Keys);
	}
	keyboard.Open(std::string(1024, 'a'));
	keyboard.Activate();
	assert(keyboard.text().size() == 1024);
	std::vector<fe::nfs::Entry> entries;
	assert(fe::nfs::List("nfs://192.168.1.2/export/PS2", entries, error));
	assert(server == "192.168.1.2" && export_path == "/export/PS2");
	assert(entries.size() == 3 && entries[0].name == "Game (USA).iso" && entries[1].directory);
	assert(entries[2].name == "Unknown.ISO" && !entries[2].directory && entries[2].size == image_size);
	assert(contexts == 0 && directories == 0);
    zip_listing=true;stat_failure=true;fe::nfs::SetFastListing(true);
    assert(fe::nfs::List("nfs://host/export/PS2",entries,error));
    assert(entries.size()==3&&entries[2].name=="Unknown.ZIP"&&entries[2].size==0);
    fe::nfs::SetFastListing(false);zip_listing=false;stat_failure=false;
	parent_export_only = true;
	assert(fe::nfs::List("nfs://host/export/PS2", entries, error));
	assert(export_path == "/export" && listing_path == "/PS2");
	{
		fe::nfs::File nested;
		assert(nested.Open("nfs://host/export/PS2/Game.iso", error));
		assert(export_path == "/export" && file_path == "/PS2/Game.iso");
	}
	assert(contexts == 0 && handles == 0);
	parent_export_only = false;
	assert(!fe::nfs::List("nfs://host", entries, error));
	mount_failure = true;
	assert(!fe::nfs::List("nfs://host/export", entries, error));
	assert(contexts == 0);
	mount_failure = false;
	uint64_t size = 0;
	assert(fe::nfs::Stat("nfs://host/export/Game.iso", size, error) && size == image_size);
	assert(contexts == 0);
	{
		fe::nfs::File file;
		assert(file.Open("nfs://host/export/Game (USA).iso", error));
		assert(file_path == "/Game (USA).iso" && file.Size() == image_size);
		const uint64_t offset = (uint64_t{1} << 32) + 19;
		std::vector<unsigned char> bytes(256 * 1024);
		assert(file.ReadAt(offset, bytes.data(), bytes.size()));
		for (size_t i = 0; i < bytes.size(); i++)
			assert(bytes[i] == ((offset + i) & 255));
		const int before = reads;
		assert(!file.ReadAt(image_size - 1, bytes.data(), 2) && reads == before);
		eof = true;
		assert(!file.ReadAt(0, bytes.data(), 10));
		eof = false;
		read_failure = true;
		assert(!file.ReadAt(0, bytes.data(), 10));
		read_failure = false;
		assert(file.Open("nfs://host/Root.iso", error));
		assert(export_path == "/" && contexts == 1 && handles == 1);
		stat_failure = true;
		assert(!file.Open("nfs://host/export/Game.iso", error));
		assert(contexts == 0 && handles == 0);
		stat_failure = false;
		open_failure = true;
		assert(!file.Open("nfs://host/export/Game.iso", error));
		assert(contexts == 0 && handles == 0);
		open_failure = false;
		assert(file.Open("nfs://host/export/Game.iso", error));
	}
	assert(contexts == 0 && handles == 0);
	{
		std::ofstream config(argv[1]);
		config << "# comment\n\ninvalid\nnfs://host\n nfs://host/export///\r\n"
				  "nfs://host/export\nnfs://host/\n";
	}
	const auto dirs = fe::nfs::ConfiguredDirectories(argv[1]);
	assert(dirs.size() == 2 && dirs[0] == "nfs://host/export" && dirs[1] == "nfs://host/");
	std::puts("NFS backend tests passed (AI-assisted)");
}
