// (AI-assisted) Read-only NFS image access, independent of kernel mounts.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <cstdint>
#include <string>
#include <vector>

struct nfs_context;
struct nfsfh;

namespace fe::nfs
{
	bool IsUrl(const char* path);
    bool ServerReachable(const std::string& url);
	bool ReadConfiguration(const std::string& path, std::string& text, std::string& error);
	bool SaveConfiguration(const std::string& path, const std::string& url, std::string& error);
	// (AI-assisted) Validate and replace only the first URL, preserving other shares and comments.
	bool UpdatePrimaryDirectory(const std::string& text, const std::string& url, std::string& output, std::string& error);

	struct Entry
	{
		std::string name;
		uint64_t size;
		bool directory;
	};

	// URLs are nfs://server/export[/folder], without URL query options.
	void SetFastListing(bool enabled);
	void SetListProgress(std::function<void(const std::string&, bool)> callback);
	bool List(const std::string& url, std::vector<Entry>& entries, std::string& error);
	bool Stat(const std::string& url, uint64_t& size, std::string& error);
	std::vector<std::string> ConfiguredDirectories(const std::string& config);

	class File
	{
	public:
		File() = default;
		~File();
		File(const File&) = delete;
		File& operator=(const File&) = delete;
		bool Open(const std::string& url, std::string& error);
		bool ReadAt(uint64_t offset, void* buffer, size_t length);
		void Close();
		uint64_t Size() const { return m_size; }

	private:
#ifdef ORBIS_NFS
		nfs_context* m_context = nullptr;
		nfsfh* m_handle = nullptr;
#endif
		uint64_t m_size = 0;
	};
} // namespace fe::nfs
