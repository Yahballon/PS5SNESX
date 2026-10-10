// (AI-assisted) Persistent library index; game contents are never stored here.
#pragma once
#include "games.h"
#include <vector>
namespace snes {
std::string CacheFile(const std::string& root,const std::vector<std::string>& directories);
bool ReadLibrary(const std::string& file,const std::vector<std::string>& directories,std::vector<Game>& games);
bool WriteLibrary(const std::string& file,const std::vector<Game>& games);
}
