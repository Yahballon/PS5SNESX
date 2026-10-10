// (AI-assisted) Test 1000-ROM library caching with the actual scanner and original shelf adapter.
#include "library_cache.h"
#include "fe_games.h"
#include "fe_nfs.h"
#include "fe_file_io.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
static int requests=0,count=1000;static bool failure=false;
namespace fe::nfs {
bool probe_offline=false;bool ServerReachable(const std::string&){return !probe_offline;}
thread_local std::function<void(const std::string&,bool)> progress;
thread_local bool fast=false;
void SetListProgress(std::function<void(const std::string&,bool)> cb){progress=std::move(cb);}
void SetFastListing(bool value){fast=value;}
bool IsUrl(const char* path){return std::string(path).starts_with("nfs://");}
std::vector<std::string> ConfiguredDirectories(const std::string& file){std::ifstream input(file);std::string line;std::getline(input,line);return line.empty()?std::vector<std::string>{}:std::vector<std::string>{line};}
bool List(const std::string& url,std::vector<Entry>& entries,std::string& error) {
    ++requests;assert(fast);
    if(failure){error="offline test";return false;}
    for(int i=0;i<count;++i){auto name="Test "+std::to_string(i)+" (World).zip";
        if(progress)progress(name,true);entries.push_back({name,10000,false});}
    return true;
}
File::~File(){}bool File::Open(const std::string&,std::string&){assert(false);return false;}
bool File::ReadAt(uint64_t,void*,size_t){assert(false);return false;}void File::Close(){}
}
int main(int argc,char** argv) {
    assert(argc==2);std::string root=std::string(argv[1])+"/PS5SNES";std::filesystem::create_directories(root+"/cache");
    std::ofstream(root+"/nfs.txt")<<"nfs://example/SNES";
    std::string usb=std::string(argv[1])+"/usb/homebrew/PS5SNES/games";
    std::vector<std::string> dirs={root,usb};
    auto cold=fe::ScanGames(dirs);assert(cold.size()==1000&&requests==1);
    failure=true;auto warm=fe::ScanGames(dirs);assert(warm.size()==1000&&requests==1); // No NFS call even when offline.
    std::filesystem::create_directories(root+"/games");std::filesystem::create_directories(usb+"/nested");
    std::ofstream(root+"/games/Local.sfc")<<"ROM";std::ofstream(usb+"/nested/USB.smc")<<"ROM";
    assert(fe::ScanGames(dirs).size()==1002&&requests==1);
    std::filesystem::remove(root+"/games/Local.sfc");assert(fe::ScanGames(dirs).size()==1001&&requests==1);
    std::filesystem::remove(usb+"/nested/USB.smc");assert(fe::ScanGames(dirs).size()==1000&&requests==1);
    failure=false;count=1001;std::string error;
    assert(fe::fileio::WriteAtomic(root+"/cache/rescan-library","1",error));
    auto refresh=fe::ScanGames(dirs);assert(refresh.size()==1001&&requests==2);
    assert(!std::filesystem::exists(root+"/cache/rescan-library"));
    assert(fe::ScanGames(dirs).size()==1001&&requests==2);
    // A failed refresh preserves the existing cache and leaves the retry marker.
    failure=true;assert(fe::fileio::WriteAtomic(root+"/cache/rescan-library","1",error));
    assert(fe::ScanGames(dirs).size()==1001&&requests==3);
    assert(std::filesystem::exists(root+"/cache/rescan-library"));
    std::filesystem::remove(root+"/cache/rescan-library");
    std::ofstream(root+"/nfs.txt")<<"nfs://example/OTHER";failure=false;
    assert(fe::ScanGames(dirs).size()==1001&&requests==4); // Different share has a different key.
    fe::nfs::probe_offline=true;
    std::ofstream(root+"/games/Offline.sfc")<<"ROM";
    assert(fe::fileio::WriteAtomic(root+"/cache/rescan-library","1",error));
    assert(fe::ScanGames(dirs).size()==1002&&requests==4);
    std::string new_root=std::string(argv[1])+"/uncached/PS5SNES";
    std::filesystem::create_directories(new_root+"/games");std::ofstream(new_root+"/nfs.txt")<<"nfs://example/SNES";
    std::ofstream(new_root+"/games/Local.sfc")<<"ROM";
    assert(fe::ScanGames({new_root}).size()==1&&requests==4);
    std::vector<uint8_t> offline_rom;assert(!snes::ReadRom("nfs://example/SNES/offline.sfc",offline_rom,error));assert(offline_rom.empty()&&error.find("offline")!=error.npos);
    std::vector<snes::Game> games;
    for(int i=0;i<1000;++i)games.push_back({"nfs://example/SNES/Test "+std::to_string(i)+".zip","Test",10000});
    auto file=root+"/cache/test.bin";assert(snes::WriteLibrary(file,games));
    std::vector<snes::Game> loaded;assert(snes::ReadLibrary(file,{"nfs://example/SNES"},loaded)&&loaded.size()==1000);
    assert(!snes::ReadLibrary(file,{"nfs://example/OTHER"},loaded));
    std::fstream corrupt(file,std::ios::binary|std::ios::in|std::ios::out);corrupt.seekp(40);corrupt.put('!');corrupt.close();
    assert(!snes::ReadLibrary(file,{"nfs://example/SNES"},loaded));
    std::puts("1000-game cache passed: zero NFS calls on warm start, refresh, share changes, offline fallback and corruption rejection (AI-assisted)");
}
