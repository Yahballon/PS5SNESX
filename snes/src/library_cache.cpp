// (AI-assisted) Bounded, checked library cache with atomic replacement.
#include "library_cache.h"
#include "fe_file_io.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <zlib.h>
namespace snes {
std::string CacheFile(const std::string& root,const std::vector<std::string>& directories) {
    uint64_t hash=14695981039346656037ull;
    for(const auto& directory:directories){for(unsigned char c:directory){hash^=c;hash*=1099511628211ull;}hash^=0;hash*=1099511628211ull;}
    char text[32];std::snprintf(text,sizeof(text),"%016llx",static_cast<unsigned long long>(hash));
    return root+"/cache/library-"+text+".bin";
}
static void Put(std::string& data,uint64_t value,unsigned bytes){for(unsigned i=0;i<bytes;++i)data.push_back(char(value>>(i*8)));}
bool ReadLibrary(const std::string& file,const std::vector<std::string>& directories,std::vector<Game>& games) {
    struct stat info{};if(stat(file.c_str(),&info)!=0||info.st_size<16||info.st_size>(40<<20))return false;
    std::string data,error;if(!fe::fileio::Read(file,data,error)||data.size()<16||data.compare(0,8,"PS5SNES4\0",8)!=0)return false;
    size_t at=8;bool valid=true;
    auto get=[&](unsigned size){uint64_t value=0;if(at>data.size()||size>data.size()-at){valid=false;return value;}
        for(unsigned i=0;i<size;++i)value|=uint64_t(static_cast<unsigned char>(data[at++]))<<(8*i);return value;};
    uint32_t expected=0;for(unsigned i=0;i<4;++i)expected|=uint32_t(static_cast<unsigned char>(data[data.size()-4+i]))<<(i*8);
    if(crc32(0,reinterpret_cast<const Bytef*>(data.data()),data.size()-4)!=expected)return false;
    const uint64_t count=get(4);if(count>8192)return false;
    std::vector<Game> result;
    for(size_t i=0;i<count;++i) {
        const size_t length=get(4);const uint64_t bytes=get(8);
        if(!valid||!length||length>4096||at>data.size()-4||length>data.size()-4-at)return false;
        std::string path=data.substr(at,length);at+=length;
        if(path.find('\0')!=path.npos||path.find("/../")!=path.npos)return false;
        bool allowed=false;for(auto dir:directories){while(dir.size()>1&&dir.back()=='/')dir.pop_back();
            if(path.starts_with(dir+"/")){allowed=true;break;}}
        if(!allowed)return false;
        const auto slash=path.find_last_of('/'),dot=path.find_last_of('.');
        if(dot==path.npos||dot<=slash)return false;
        auto extension=path.substr(dot);for(char& c:extension)if(c>='A'&&c<='Z')c+=32;
        if(extension!=".zip"&&extension!=".sfc"&&extension!=".smc"&&extension!=".swc"&&extension!=".fig")return false;
        result.push_back({path,path.substr(slash+1,dot-slash-1),bytes});
    }
    if(at!=data.size()-4)return false;games=std::move(result);return true;
}
bool WriteLibrary(const std::string& file,const std::vector<Game>& games) {
    if(games.size()>8192)return false;
    std::string data("PS5SNES4\0",8);Put(data,games.size(),4);
    for(const auto& game:games){if(game.path.empty()||game.path.size()>4096)return false;
        Put(data,game.path.size(),4);Put(data,game.bytes,8);data+=game.path;}
    Put(data,crc32(0,reinterpret_cast<const Bytef*>(data.data()),data.size()),4);
    std::string error;const bool okay=fe::fileio::WriteAtomic(file,data,error);
    if(!okay)std::printf("[library] cache write failed: %s\n",error.c_str());return okay;
}
}
