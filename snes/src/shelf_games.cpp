// (AI-assisted) SNES image adapter for the original PS5SX2 shelf API.
#include "fe_games.h"
#include "fe_settings.h"
#include "games.h"
#include "fe_nfs.h"
#include "library_cache.h"
#include "scan_progress.h"
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
namespace fe {
bool IsDiscImageName(const char* name) {std::string s=name?name:"";auto at=s.rfind('.');if(at==s.npos)return false;
    s=settings::Lower(s.substr(at));return s==".sfc"||s==".smc"||s==".swc"||s==".fig"||s==".zip";}
void MakeTitle(const std::string& stem,std::string& title,std::string& region,std::string& extra) {
    title=stem;region.clear();extra.clear();auto at=stem.find(" (");if(at!=stem.npos)title=stem.substr(0,at);
    if(stem.find("(USA)")!=stem.npos)region="USA";
    else if(stem.find("(Europe)")!=stem.npos)region="Europe";
    else if(stem.find("(Japan)")!=stem.npos)region="Japan";
    else if(stem.find("(World)")!=stem.npos)region="World";
}
std::string ReadSerial(const std::string& path) {
    uint32_t hash=2166136261u;for(unsigned char c:path){hash^=c;hash*=16777619u;}
    char text[20];std::snprintf(text,sizeof(text),"SNES-%08X",hash);return text;
}
void SortGames(std::vector<GameInfo>& games) {std::sort(games.begin(),games.end(),[](const auto& a,const auto& b){
    const auto ta=settings::Lower(a.title),tb=settings::Lower(b.title);return ta==tb?a.file<b.file:ta<tb;});}
std::vector<GameInfo> ScanGames(const std::vector<std::string>& directories) {
    static std::mutex scan_mutex;std::lock_guard<std::mutex> lock(scan_mutex);
    auto dirs=directories;std::string root;
    for(const auto& directory:directories)if(directory.ends_with("/PS5SNES")){
        root=directory;auto remote=fe::nfs::ConfiguredDirectories(directory+"/nfs.txt");dirs.insert(dirs.end(),remote.begin(),remote.end());}
    const std::string cache=root.empty()?"":snes::CacheFile(root,dirs),force=root+"/cache/rescan-library";
    struct stat info{};bool forced=!root.empty()&&stat(force.c_str(),&info)==0;
    std::vector<snes::Game> listed;
    const bool cached=!cache.empty()&&!forced&&snes::ReadLibrary(cache,dirs,listed);
    if(cached){
        // (AI-assisted) USB attachment and local file changes must not be hidden by the NFS cache.
        std::vector<std::string> local_dirs;for(const auto& dir:dirs)if(!fe::nfs::IsUrl(dir.c_str()))local_dirs.push_back(dir);
        auto local=snes::Scan(local_dirs);const size_t local_count=local.size();
        for(auto& game:listed)if(fe::nfs::IsUrl(game.path.c_str()))local.push_back(std::move(game));
        listed=std::move(local);
        std::printf("[library] fresh local/USB: %zu games; cached NFS: %zu games\n",local_count,listed.size()-local_count);
        if(!cache.empty())snes::WriteLibrary(cache,listed);
        snes::ReportCached(listed.size());
    }
    else {
        listed=snes::Scan(dirs);
        if(!cache.empty()&&!snes::ScanFailed()){
            if(snes::WriteLibrary(cache,listed))unlink(force.c_str());
        }
        else if(!cache.empty()&&snes::ScanFailed()){
            std::vector<snes::Game> previous;
            if(snes::ReadLibrary(cache,dirs,previous)){for(auto& game:previous)if(fe::nfs::IsUrl(game.path.c_str())&&std::none_of(listed.begin(),listed.end(),[&](const auto& found){return found.path==game.path;}))listed.push_back(std::move(game));snes::ReportCached(listed.size());}
        }
    }
    std::vector<GameInfo> games;for(const auto& game:listed) {
        GameInfo info;info.path=game.path;info.file=game.path.substr(game.path.find_last_of('/')+1);
        info.stem=info.file.substr(0,info.file.find_last_of('.'));MakeTitle(info.stem,info.title,info.region,info.extra);
        info.bytes=game.bytes;info.serial=ReadSerial(info.path);games.push_back(std::move(info));
    }SortGames(games);return games;
}
void SetSerialCacheFile(const std::string&){}void SetGameDbFile(const std::string&){}
bool ApplyGameDbTitle(GameInfo&){return false;}
std::string DescribeImage(const std::string&){return "SNES ROM (FCEUmm)";}
void ReadBadges(GameInfo& game,const std::string&,const std::string&,const std::string&) {
    game.badges={"SNES"};if(game.file.size()>4&&settings::Lower(game.file.substr(game.file.size()-4))==".zip")game.badges.push_back("ZIP");
}
}
