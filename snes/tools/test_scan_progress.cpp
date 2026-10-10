// (AI-assisted) Exercise the actual scanner with a deliberately slow 240-ROM NFS listing.
#include "games.h"
#include "loading.h"
#include "fe_nfs.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
namespace fe::nfs {
thread_local std::function<void(const std::string&,bool)> callback;
void SetListProgress(std::function<void(const std::string&,bool)> value){callback=std::move(value);}
bool IsUrl(const char* path){return std::string(path).starts_with("nfs://");}
bool List(const std::string&,std::vector<Entry>& entries,std::string&) {
    for(int i=0;i<240;++i){std::string name="SNES test "+std::to_string(i)+".nes";
        if(callback)callback(name,true);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));entries.push_back({name,16400,false});}
    if(callback)callback("readme.txt",true);entries.push_back({"readme.txt",10,false});return true;
}
File::~File(){}bool File::Open(const std::string&,std::string&){return false;}
bool File::ReadAt(uint64_t,void*,size_t){return false;}void File::Close(){}
}
int main() {
    snes::LoadingState state;std::atomic<bool> finished{false};size_t final_games=0;
    std::thread worker([&]{snes::SetScanProgress([&](const auto& progress){state.Publish(progress);});
        auto games=snes::Scan({"nfs://example/SNES","nfs://example/SNES"});final_games=games.size();
        snes::SetScanProgress({});{std::lock_guard<std::mutex> lock(state.mutex);state.done=true;}finished=true;});
    size_t samples=0,previous_entries=0;bool saw_intermediate=false;
    while(!finished){bool done=false;auto progress=state.Snapshot(done);
        assert(progress.entries>=previous_entries);previous_entries=progress.entries;
        saw_intermediate|=progress.games>0&&progress.games<240;++samples;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    worker.join();bool done=false;auto progress=state.Snapshot(done);
    assert(done&&final_games==240&&progress.games==240&&progress.entries==241);
    assert(progress.sources_total==1&&progress.sources_done==1&&saw_intermediate&&samples>20);
    // A web scan on a different thread must not modify boot-screen progress.
    std::thread web([]{snes::Scan({"nfs://example/SNES"});});web.join();
    auto after=state.Snapshot(done);assert(after.entries==progress.entries&&after.games==progress.games);
    std::printf("Slow 240-ROM NFS scan: live counts, responsive snapshots, root deduplication and thread isolation passed (%zu updates) (AI-assisted)\n",samples);
}
