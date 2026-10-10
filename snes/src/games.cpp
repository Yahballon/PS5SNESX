// (AI-assisted) Local/USB/NFS ROM loading, including bounded ZIP decoding in memory.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "games.h"
#include "scan_progress.h"
#include "fe_nfs.h"
#include <libretro.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

namespace snes {
static thread_local std::function<void(const ScanProgress&)> scan_callback;
static thread_local ScanProgress scan_progress;
static thread_local bool scan_failed=false;
bool ScanFailed(){return scan_failed;}
static thread_local std::set<std::string> visited;
void SetScanProgress(std::function<void(const ScanProgress&)> callback) { scan_callback=std::move(callback); }
static void Publish() { if(scan_callback)scan_callback(scan_progress); }
void ReportCached(size_t count){scan_progress={};scan_progress.games=count;scan_progress.entries=count;scan_progress.directory="Cached library - rescan in All games settings";Publish();}

static std::string Lower(std::string s) { for (char& c:s) if(c>='A'&&c<='Z')c+=32;return s; }
static bool RomName(const std::string& name) {
    auto p=name.rfind('.'); if(p==std::string::npos)return false;
    auto ext=Lower(name.substr(p));return ext==".sfc"||ext==".smc"||ext==".swc"||ext==".fig";
}
static bool GameName(const std::string& name) {
    return RomName(name)||(name.size()>4 && Lower(name.substr(name.size()-4))==".zip");
}
static void Add(std::vector<Game>& games,const std::string& path,const std::string& name,uint64_t bytes=0) {
    auto dot=name.rfind('.');games.push_back({path,name.substr(0,dot),bytes});
}
static void Walk(const std::string& path,unsigned depth,std::vector<Game>& games) {
    if(depth>5||games.size()>=8192||!visited.insert(path).second)return;
    scan_progress.directory=path;scan_progress.games=games.size();Publish();
    if(fe::nfs::IsUrl(path.c_str())) {
        if(depth==0&&!fe::nfs::ServerReachable(path)){scan_failed=true;std::printf("[nfs] offline server skipped: %s (750ms probe)\n",path.c_str());return;}
        std::vector<fe::nfs::Entry> entries;std::string error;
        size_t candidates=0;
        fe::nfs::SetListProgress([&](const std::string& name,bool) {
            ++scan_progress.entries;if(GameName(name))++candidates;
            scan_progress.directory=path;scan_progress.games=games.size()+candidates;Publish();
        });
        fe::nfs::SetFastListing(true);
        const bool listed=fe::nfs::List(path,entries,error);
        fe::nfs::SetFastListing(false);
        fe::nfs::SetListProgress({});
        if(!listed){scan_failed=true;std::printf("[nes] scan: %s\n",error.c_str());return;}
        for(const auto& entry:entries) {
            if(entry.name.empty()||entry.name[0]=='.'||games.size()>=8192)continue;
            auto child=path+"/"+entry.name;
            if(entry.directory)Walk(child,depth+1,games);
            else if(GameName(entry.name))Add(games,child,entry.name,entry.size);
        }
        return;
    }
    DIR* dir=opendir(path.c_str());if(!dir){if(errno!=ENOENT)std::printf("[library] cannot open %s: errno=%d (%s)\n",path.c_str(),errno,std::strerror(errno));return;}
    std::printf("[library] scanning local directory: %s\n",path.c_str());
    while(auto* entry=readdir(dir)) {
        if(entry->d_name[0]=='.'||games.size()>=8192)continue;
        ++scan_progress.entries;
        std::string child=path+"/"+entry->d_name;struct stat info{};
        if(lstat(child.c_str(),&info)!=0)continue;
        if(S_ISDIR(info.st_mode))Walk(child,depth+1,games);
        else if(S_ISREG(info.st_mode)&&GameName(entry->d_name))Add(games,child,entry->d_name,info.st_size);
        scan_progress.games=games.size();Publish();
    }
    closedir(dir);
}
std::vector<Game> Scan(const std::vector<std::string>& directories) {
    std::vector<Game> games;std::set<std::string> seen;
    scan_progress={};scan_failed=false;visited.clear();
    for(const auto& dir:directories)seen.insert(dir);
    scan_progress.sources_total=seen.size();seen.clear();Publish();
    for(const auto& dir:directories)if(seen.insert(dir).second) {
        Walk(dir,0,games);++scan_progress.sources_done;scan_progress.games=games.size();Publish();
    }
    std::sort(games.begin(),games.end(),[](const Game& a,const Game& b){return Lower(a.title)<Lower(b.title);});
    seen.clear();games.erase(std::remove_if(games.begin(),games.end(),[&](const Game& g){return !seen.insert(g.path).second;}),games.end());
    scan_progress.games=games.size();Publish();return games;
}
static uint16_t U16(const uint8_t* p){return p[0]|(p[1]<<8);}
static uint32_t U32(const uint8_t* p){return U16(p)|(uint32_t(U16(p+2))<<16);}
static bool Unzip(const std::vector<uint8_t>& zip,std::vector<uint8_t>& rom,std::string& error) {
    error="Invalid or unsupported ZIP";
    if(zip.size()<22)return false;
    size_t end=zip.size()-22;
    const size_t min=zip.size()>65557?zip.size()-65557:0;
    while(U32(zip.data()+end)!=0x06054b50 || end+22+U16(zip.data()+end+20)!=zip.size()) {
        if(end==min)return false;--end;
    }
    const auto* e=zip.data()+end;
    if(U16(e+4)||U16(e+6)||U16(e+8)!=U16(e+10))return false;
    size_t at=U32(e+16);const size_t central_size=U32(e+12);
    if(at>end||central_size>end-at)return false;
    const size_t central_end=at+central_size;
    size_t found=0;unsigned matches=0;
    for(unsigned i=0;i<U16(e+10);++i) {
        if(at>central_end||central_end-at<46||U32(zip.data()+at)!=0x02014b50)return false;
        const auto* c=zip.data()+at;
        const size_t name=U16(c+28),extra=U16(c+30),comment=U16(c+32);
        const size_t size=46+name+extra+comment;
        if(size>central_end-at)return false;
        if(RomName(std::string(reinterpret_cast<const char*>(c+46),name))) {found=at;++matches;}
        at+=size;
    }
    if(matches!=1){error=matches?"ZIP contains multiple ROMs - use one ROM per ZIP":"ZIP contains no SNES ROM";return false;}
    const auto* c=zip.data()+found;
    const size_t compressed=U32(c+20),length=U32(c+24),local=U32(c+42);
    const auto method=U16(c+10);
    if(U16(c+8)&1){error="Encrypted ZIP is unsupported";return false;}
    if(length<16||length>(32u<<20)||compressed>zip.size()||local>zip.size()||zip.size()-local<30)return false;
    const auto* l=zip.data()+local;
    if(U32(l)!=0x04034b50||U16(l+8)!=method||(U16(l+6)&1))return false;
    const size_t payload=local+30+U16(l+26)+U16(l+28);
    if(payload>zip.size()||compressed>zip.size()-payload)return false;
    rom.resize(length);
    if(method==0) {if(compressed!=length)return false;std::memcpy(rom.data(),zip.data()+payload,length);}
    else if(method==8) {
        z_stream stream{};stream.next_in=const_cast<Bytef*>(zip.data()+payload);stream.avail_in=compressed;
        stream.next_out=rom.data();stream.avail_out=length;
        if(inflateInit2(&stream,-MAX_WBITS)!=Z_OK)return false;
        int status=inflate(&stream,Z_FINISH);inflateEnd(&stream);
        if(status!=Z_STREAM_END||stream.total_out!=length||stream.total_in!=compressed)return false;
    } else {error="Unsupported ZIP compression";return false;}
    if(crc32(0,rom.data(),rom.size())!=U32(c+16)){error="ZIP checksum failed";return false;}
    return true;
}
bool ReadRom(const std::string& path,std::vector<uint8_t>& rom,std::string& error) {
    error.clear();rom.clear();std::vector<uint8_t> bytes;
    constexpr size_t limit=64u<<20;
    if(fe::nfs::IsUrl(path.c_str())) {
        if(!fe::nfs::ServerReachable(path)){error="NFS server offline; use a local game or reconnect the NAS";return false;}
        fe::nfs::File file;
        if(!file.Open(path,error))return false;
        if(!file.Size()||file.Size()>limit){error="ROM/archive exceeds 64 MiB";return false;}
        bytes.resize(file.Size());
        for(size_t at=0;at<bytes.size();at+=65536)
            if(!file.ReadAt(at,bytes.data()+at,std::min<size_t>(65536,bytes.size()-at))){error="NFS read failed";return false;}
    } else {
        const int fd=open(path.c_str(),O_RDONLY);if(fd<0){error="Cannot open ROM: errno "+std::to_string(errno);return false;}
        struct stat st{};
        if(fstat(fd,&st)!=0||st.st_size<16||uint64_t(st.st_size)>limit){close(fd);error="Invalid ROM/archive size";return false;}
        bytes.resize(st.st_size);size_t at=0;
        while(at<bytes.size()) {
            auto n=read(fd,bytes.data()+at,bytes.size()-at);
            if(n<0&&errno==EINTR)continue;
            if(n<=0){close(fd);error="Incomplete ROM read";return false;}at+=n;
        }
        close(fd);
    }
    if(path.size()>4&&Lower(path.substr(path.size()-4))==".zip") {
        if(!Unzip(bytes,rom,error)){rom.clear();return false;}
    } else rom=std::move(bytes);
    // (AI-assisted) Validate normal SNES cartridge headers before invoking the core.
    bool valid=false;const size_t copier=rom.size()%32768==512?512:0;
    for(size_t header:{size_t(0x7fc0),size_t(0xffc0),size_t(0x40ffc0)}) {
        if(rom.size()<copier+header+64)continue;
        const auto* h=rom.data()+copier+header;const unsigned reset=U16(h+60),mapper=h[21]&0xef;
        if(reset>=0x8000&&reset!=0xffff&&h[23]<=16&&h[25]<=13&&
            (mapper==0x20||mapper==0x21||mapper==0x22||mapper==0x25)){valid=true;break;}
    }
    if(!valid||rom.size()>(32u<<20)){error="Not a supported SNES cartridge image";rom.clear();return false;}
    error.clear();return true;
}
uint16_t MapButtons(uint32_t raw) {
    uint16_t mapped=0;
    const uint32_t masks[]={0x4000,0x8000,0x40100001,8,0x10,0x40,0x80,0x20,0x2000,0x1000,0x400,0x800};
    for(unsigned i=0;i<12;++i)if(raw&masks[i])mapped|=1u<<i;
    return mapped;
}
}
