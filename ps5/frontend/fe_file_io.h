// (AI-assisted) Descriptor I/O for settings in the public-SDK console build.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cerrno>
#include <cstdio>
#include <string>
#include <mutex>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fe::fileio
{
	inline bool Read(const std::string& path, std::string& text, std::string& error, bool allow_missing = false)
	{
		text.clear();
		const int fd = open(path.c_str(), O_RDONLY);
		if (fd < 0)
		{
			if (allow_missing && errno == ENOENT)
			{
				error.clear();
				return true;
			}
			error = "Cannot read " + path + " (errno " + std::to_string(errno) + ")";
			return false;
		}
		char buffer[4096];
		for (;;)
		{
			const ssize_t n = read(fd, buffer, sizeof(buffer));
			if (n < 0 && errno == EINTR)
				continue;
			if (n < 0)
			{
				const int code = errno;
				close(fd);
				errno = code;
				error = "Cannot read " + path + " (errno " + std::to_string(code) + ")";
				return false;
			}
			if (!n)
				break;
			text.append(buffer, static_cast<size_t>(n));
		}
		close(fd);
		error.clear();
		return true;
	}

	inline bool WriteAtomic(const std::string& path, const std::string& text, std::string& error)
	{
		static std::mutex writer_mutex;
		std::lock_guard<std::mutex> lock(writer_mutex);
		const std::string temporary = path + ".tmp";
		const size_t slash = path.rfind('/');
		if (slash != std::string::npos && slash > 0)
		{
			const std::string parent = path.substr(0, slash);
			if (mkdir(parent.c_str(), 0777) != 0 && errno != EEXIST)
			{
				error = "Cannot create " + parent + " (errno " + std::to_string(errno) + ")";
				return false;
			}
		}
		const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd < 0)
		{
			error = "Cannot open " + temporary + " (errno " + std::to_string(errno) + ")";
			return false;
		}
		int code = 0;
		size_t offset = 0;
		while (offset < text.size())
		{
			const ssize_t n = write(fd, text.data() + offset, text.size() - offset);
			if (n < 0 && errno == EINTR)
				continue;
			if (n <= 0)
			{
				code = n < 0 ? errno : EIO;
				break;
			}
			offset += static_cast<size_t>(n);
		}
		const char* operation = "write";
		if (close(fd) != 0 && !code)
		{
			code = errno;
			operation = "close";
		}
		if (!code && rename(temporary.c_str(), path.c_str()) != 0)
		{
			code = errno;
			operation = "replace";
		}
		if (code)
		{
			unlink(temporary.c_str());
			errno = code;
			error = "Cannot " + std::string(operation) + " " + path + " (errno " + std::to_string(code) + ")";
			return false;
		}
		error.clear();
		return true;
	}
} // namespace fe::fileio
