// (AI-assisted) Read-only NFSv3 ISO streaming; needs proper testing on the console.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fe_nfs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <cerrno>
#include <mutex>
#include <sstream>
#ifndef _WIN32
#include "fe_file_io.h"
#endif

#ifdef ORBIS_NFS
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <time.h>
#include <nfsc/libnfs.h>
#endif

namespace fe::nfs
{
    thread_local bool fast_listing = false;
    void SetFastListing(bool enabled) { fast_listing = enabled; }
    // (AI-assisted) Progress belongs to the caller's thread, not concurrent web scans.
    thread_local std::function<void(const std::string&, bool)> list_progress;
    void SetListProgress(std::function<void(const std::string&, bool)> callback) { list_progress = std::move(callback); }

    // (AI-assisted) Bound the common offline IPv4 NAS path before RPC/export retries.
    bool ServerReachable(const std::string& url) {
#ifdef ORBIS_NFS
        const auto end=url.find('/',6);const auto host=url.substr(6,end==url.npos?url.npos:end-6);
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(2049);
        if(inet_pton(AF_INET,host.c_str(),&address.sin_addr)!=1)return true;
        const int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return true;
        const int flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);return true;}
        bool ready=connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0;
        if(!ready&&errno==EINPROGRESS){pollfd event{fd,POLLOUT,0};if(poll(&event,1,750)>0){int error=0;socklen_t size=sizeof(error);ready=getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)==0&&error==0;}}
        close(fd);return ready;
#else
        (void)url;return true;
#endif
    }

	bool ReadConfiguration(const std::string& path, std::string& text, std::string& error)
	{
#ifndef _WIN32
		return fileio::Read(path, text, error, true);
#else
		text.clear();
		std::ifstream file(path);
		if (file)
			text.assign(std::istreambuf_iterator<char>(file), {});
		error.clear();
		return true;
#endif
	}

	bool SaveConfiguration(const std::string& path, const std::string& url, std::string& error)
	{
#ifndef _WIN32
		static std::mutex save_mutex;
		std::lock_guard<std::mutex> lock(save_mutex);
		std::string old, updated;
		if (!ReadConfiguration(path, old, error) || !UpdatePrimaryDirectory(old, url, updated, error))
			return false;
		const bool ok = fileio::WriteAtomic(path, updated, error);
		if (!ok)
			std::printf("[nfs] (AI-assisted) %s\n", error.c_str());
		return ok;
#else
		error = "Configuration saving requires a POSIX build";
		return false;
#endif
	}

	bool UpdatePrimaryDirectory(const std::string& text, const std::string& url, std::string& output, std::string& error)
	{
		const size_t slash = url.find('/', 6);
		if (!url.empty() && (!IsUrl(url.c_str()) || slash == std::string::npos || slash == 6 || url.size() > 1024 ||
								url.find_first_of("\r\n\t") != std::string::npos || url.find('\0') != std::string::npos ||
								url.substr(6, slash - 6).find_first_of(" @:?#") != std::string::npos || url.find('?') != std::string::npos))
		{
			error = "Use nfs://server/export/folder (no port or query options).";
			return false;
		}
		output.clear();
		bool replaced = false;
		for (size_t begin = 0; begin < text.size();)
		{
			const size_t end = text.find('\n', begin);
			const size_t next = end == std::string::npos ? text.size() : end + 1;
			const std::string line = text.substr(begin, next - begin);
			const size_t first = line.find_first_not_of(" \t\r");
			if (!replaced && first != std::string::npos && IsUrl(line.c_str() + first))
			{
				if (!url.empty())
					output += url + "\n";
				replaced = true;
			}
			else
				output += line;
			begin = next;
		}
		if (!replaced && !url.empty())
		{
			if (!output.empty() && output.back() != '\n')
				output += '\n';
			output += url + "\n";
		}
		error.clear();
		return true;
	}

	bool IsUrl(const char* path)
	{
		return path && std::strncmp(path, "nfs://", 6) == 0;
	}

	std::vector<std::string> ConfiguredDirectories(const std::string& config)
	{
		std::vector<std::string> dirs;
		std::string text, error;
		if (!ReadConfiguration(config, text, error))
		{
			std::printf("[nfs] %s\n", error.c_str());
			return dirs;
		}
		std::printf("[nfs] config %s: %zu bytes\n", config.c_str(), text.size());
		std::istringstream file(text);
		std::string line;
		while (dirs.size() < 4 && std::getline(file, line))
		{
			const size_t first = line.find_first_not_of(" \t\r");
			if (first == std::string::npos || line[first] == '#')
				continue;
			line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
			const size_t root_slash = line.find('/', 6);
			while (root_slash != std::string::npos && line.size() > root_slash + 1 && line.back() == '/')
				line.pop_back();
			if (!IsUrl(line.c_str()) || root_slash == std::string::npos || root_slash == 6 ||
				line.size() > 4096 || line.find('\0') != std::string::npos)
			{
				std::printf("[nfs] invalid directory in %s\n", config.c_str());
				continue;
			}
			if (std::find(dirs.begin(), dirs.end(), line) == dirs.end())
			{
				dirs.push_back(line);
				std::printf("[nfs] configured share: %s\n", line.c_str());
			}
		}
		std::printf("[nfs] configured shares: %zu (changes require app restart)\n", dirs.size());
		return dirs;
	}

#ifdef ORBIS_NFS
	namespace
	{
		// One context per open file or listing: no context is shared between the shelf,
		// web server and CDVD worker. Only read operations are exposed.
		nfs_context* Mount(const std::string& url, std::string& error, std::string& directory)
		{
			const size_t slash = url.find('/', 6);
			if (!IsUrl(url.c_str()) || slash == std::string::npos || slash == 6 || url.find('\0') != std::string::npos)
			{
				error = "Expected nfs://server/export/folder";
				return nullptr;
			}
			const std::string server = url.substr(6, slash - 6);
			const std::string requested = url.substr(slash);
			std::string candidate = requested;
			// (AI-assisted) Servers may export a parent but refuse MOUNT on its subdirectories.
			// Mount an accessible ancestor, then access only the requested relative directory.
			for (unsigned attempt = 0; attempt < 8; attempt++)
			{
				std::printf("[nfs] connecting NFSv3 server=%s export=%s requested=%s timeout=1000ms\n", server.c_str(), candidate.c_str(), requested.c_str());
				nfs_context* context = nfs_init_context();
				if (!context)
				{
					error = "Could not allocate NFS context";
					return nullptr;
				}
				nfs_set_timeout(context, 1000);
                nfs_set_readdir_max_buffer_size(context, 65536, 262144); // (AI-assisted) Batch directory names; needs proper testing across NFS servers.
				nfs_set_autoreconnect(context, 0);
				const int version_result = nfs_set_version(context, 3);
				const int result = version_result == 0 ? nfs_mount(context, server.c_str(), candidate.c_str()) : version_result;
				if (result == 0)
				{
                    const int socket=nfs_get_fd(context),one=1;
                    if(socket>=0) {
                        const int configured=setsockopt(socket,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));
                        std::printf("[nfs] TCP_NODELAY=%d errno=%d\n",configured,configured?errno:0);
                    }
					directory = candidate == requested ? "/" : candidate == "/" ? requested :
					                                                              requested.substr(candidate.size());
					std::printf("[nfs] mount OK: export=%s relative_directory=%s\n", candidate.c_str(), directory.c_str());
					error.clear();
					return context;
				}
				error = nfs_get_error(context);
				std::printf("[nfs] mount FAILED export=%s rc=%d errno=%d: %s\n", candidate.c_str(), result, errno, error.c_str());
				nfs_destroy_context(context);
				const bool path_error = result == -EACCES || result == -ENOENT || result == -ENOTDIR ||
				                        error.find("MNT3ERR_ACCES") != std::string::npos || error.find("MNT3ERR_NOENT") != std::string::npos;
				if (version_result != 0 || !path_error || candidate == "/")
					break;
				const size_t parent = candidate.rfind('/');
				candidate = parent == 0 ? "/" : candidate.substr(0, parent);
			}
			return nullptr;
		}

		bool SplitFile(const std::string& url, std::string& directory, std::string& name, std::string& error)
		{
			const size_t slash = url.rfind('/');
			if (!IsUrl(url.c_str()) || slash <= 5 || slash == url.size() - 1)
			{
				error = "Expected an NFS image URL";
				return false;
			}
			directory = url.substr(0, slash);
			// An image directly in the server's root export still needs a trailing slash.
			if (directory.find('/', 6) == std::string::npos)
				directory += '/';
			name = url.substr(slash);
			return true;
		}
	} // namespace

	bool List(const std::string& url, std::vector<Entry>& entries, std::string& error)
	{
		entries.clear();
        auto milliseconds=[] {timespec value{};clock_gettime(CLOCK_MONOTONIC,&value);return value.tv_sec*1000.0+value.tv_nsec/1e6;};
        const double start=milliseconds();
		std::string directory_path;
		nfs_context* context = Mount(url, error, directory_path);
		if (!context)
			return false;
		const double mounted=milliseconds();
		nfsdir* directory = nullptr;
		const bool ok = nfs_opendir(context, directory_path.c_str(), &directory) == 0;
		const double opened=milliseconds();
        size_t names=0, probes=0;
		if (ok)
		{
			while (const nfsdirent* entry = nfs_readdir(context, directory))
			{
				if (!entry->name || entry->name[0] == '.' || std::strchr(entry->name, '/'))
					continue;
                if (list_progress) list_progress(entry->name, true);
				uint32_t type = entry->type;
				uint64_t size = entry->size;
                ++names;
                if (type == 0 && fast_listing) {
                    std::string name=entry->name;
                    for(char& c:name)if(c>='A'&&c<='Z')c+=32;
                    const auto dot=name.find_last_of('.');
                    const std::string extension=dot==name.npos?"":name.substr(dot);
                    // ROM/archive contents and actual size are validated when the user starts a game.
                    if(extension==".zip"||extension==".sfc"||extension==".smc"||extension==".swc"||extension==".fig")type=1;
                }
				// (AI-assisted) READDIR without attributes can report type 0; query metadata instead of losing those games.
				if (type == 0)
				{
                    ++probes;
					nfs_stat_64 st = {};
					const std::string path = (directory_path == "/" ? directory_path : directory_path + "/") + entry->name;
					const int result = nfs_lstat64(context, path.c_str(), &st);
					if (result == 0)
					{
						type = (st.nfs_mode & 0170000) == 0100000 ? 1 : (st.nfs_mode & 0170000) == 0040000 ? 2 :
						                                                                                     0;
						size = st.nfs_size;
					}
					else
						std::printf("[nfs] metadata FAILED %s rc=%d: %s\n", path.c_str(), result, nfs_get_error(context));
				}
				if (!fast_listing || names <= 3) std::printf("[nfs] entry '%s' type=%u resolved_type=%u bytes=%llu\n", entry->name, entry->type, type,
					static_cast<unsigned long long>(size));
				if (type == 1 || type == 2)
					entries.push_back({entry->name, size, type == 2});
			}
			nfs_closedir(context, directory);
		}
		else
		{
			error = nfs_get_error(context);
			std::printf("[nfs] directory listing FAILED %s: %s\n", url.c_str(), error.c_str());
		}
		std::printf("[nfs] listing %s: status=%s usable entries=%zu\n", url.c_str(), ok ? "OK" : "FAILED", entries.size());
		std::printf("[nfs] scan summary: %zu names, %zu metadata probes, fast=%d, mount=%.0fms directory=%.0fms entries=%.0fms\n",names,probes,fast_listing,mounted-start,opened-mounted,milliseconds()-opened);
		nfs_destroy_context(context);
		return ok;
	}

	bool Stat(const std::string& url, uint64_t& size, std::string& error)
	{
		std::string directory, name;
		if (!SplitFile(url, directory, name, error))
			return false;
		std::string directory_path;
		nfs_context* context = Mount(directory, error, directory_path);
		if (!context)
			return false;
		nfs_stat_64 st = {};
		const bool ok = nfs_stat64(context, ((directory_path == "/" ? std::string() : directory_path) + name).c_str(), &st) == 0 && (st.nfs_mode & 0170000) == 0100000;
		if (ok)
			size = st.nfs_size;
		else
			error = "NFS image is unavailable or is not a regular file";
		nfs_destroy_context(context);
		return ok;
	}

	bool File::Open(const std::string& url, std::string& error)
	{
		Close();
		std::string directory, name;
		if (!SplitFile(url, directory, name, error))
			return false;
		std::string directory_path;
		m_context = Mount(directory, error, directory_path);
		if (!m_context)
			return false;
		nfs_stat_64 st = {};
		if (nfs_open(m_context, ((directory_path == "/" ? std::string() : directory_path) + name).c_str(), O_RDONLY, &m_handle) != 0 ||
			nfs_fstat64(m_context, m_handle, &st) != 0 || (st.nfs_mode & 0170000) != 0100000 || st.nfs_size == 0)
		{
			error = "Cannot open NFS image: " + std::string(nfs_get_error(m_context));
			Close();
			return false;
		}
		m_size = st.nfs_size;
		return true;
	}

	bool File::ReadAt(uint64_t offset, void* buffer, size_t length)
	{
		if (!m_handle || offset > m_size || length > m_size - offset)
			return false;
		auto* out = static_cast<unsigned char*>(buffer);
		while (length)
		{
			const size_t count = std::min<size_t>(length, 128 * 1024);
			const int n = nfs_pread(m_context, m_handle, offset, count, out);
			if (n <= 0 || static_cast<size_t>(n) > count)
				return false;
			out += n;
			offset += n;
			length -= n;
		}
		return true;
	}

	void File::Close()
	{
		if (m_handle)
			nfs_close(m_context, m_handle);
		if (m_context)
			nfs_destroy_context(m_context);
		m_handle = nullptr;
		m_context = nullptr;
		m_size = 0;
	}
#else
	bool List(const std::string&, std::vector<Entry>& entries, std::string& error)
	{
		entries.clear();
		error = "This build has no NFS support (build with LIBNFS_PREFIX)";
		return false;
	}
	bool Stat(const std::string&, uint64_t&, std::string& error)
	{
		error = "This build has no NFS support";
		return false;
	}
	bool File::Open(const std::string&, std::string& error)
	{
		error = "This build has no NFS support";
		return false;
	}
	bool File::ReadAt(uint64_t, void*, size_t) { return false; }
	void File::Close() { m_size = 0; }
#endif

	File::~File() { Close(); }
} // namespace fe::nfs
