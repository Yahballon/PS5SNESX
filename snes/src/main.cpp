// (AI-assisted) PS5SNES native app. SPDX-License-Identifier: GPL-3.0-or-later
#include "core_session.h"
#include "games.h"
#include "demo_renderer.hpp"
#include "fe_nfs.h"
#include "fe_file_io.h"
#include "elevation.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <thread>
#include <time.h>
#include <unistd.h>

extern "C" {
int scePadInit();int scePadOpen(int,int,int,const void*);int scePadReadState(int,void*);
int sceUserServiceInitialize(const void*);int sceUserServiceGetInitialUser(int*);
int sceAudioOutInit();int sceAudioOutOpen(int,int,int,unsigned,unsigned,unsigned);
int sceAudioOutOutput(int,const void*);int sceAudioOutClose(int);
}
namespace {
constexpr uint32_t up=0x10,down=0x40,cross=0x4000,square=0x8000,triangle=0x1000;
constexpr uint32_t touch=0x40100000,l1=0x400,r1=0x800,l3r3=6;
double Now(){timespec ts{};clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec+ts.tv_nsec*1e-9;}
struct Pad {uint32_t buttons;uint8_t lx,ly,rx,ry,l2,r2,p0,p1;uint8_t rest[256];};
class Audio {
public:
    bool Open() {
        sceAudioOutInit();handle=sceAudioOutOpen(255,0,0,256,48000,1);
        std::printf("[nes] audio port=%#x\n",handle);if(handle<0)return false;
        thread=std::thread([this]{
            alignas(64) int16_t block[512];
            while(!quit) {
                std::memset(block,0,sizeof(block));
                {std::lock_guard<std::mutex> lock(mutex);
                    if(!paused)for(unsigned i=0;i<512&&read_at!=write_at;++i){block[i]=ring[read_at];read_at=(read_at+1)%ring.size();}}
                if(sceAudioOutOutput(handle,block)<0){timespec pause{0,5000000};nanosleep(&pause,nullptr);}
            }
        });return true;
    }
    void Push(const std::vector<int16_t>& samples) {
        std::lock_guard<std::mutex> lock(mutex);
        for(size_t i=0;i+1<samples.size();i+=2) {
            size_t first=(write_at+1)%ring.size(),second=(first+1)%ring.size();
            if(first==read_at||second==read_at)break;
            ring[write_at]=samples[i];ring[first]=samples[i+1];write_at=second;
        }
    }
    void Pause(bool value) {std::lock_guard<std::mutex> lock(mutex);paused=value;read_at=write_at=0;}
    ~Audio(){quit=true;if(thread.joinable())thread.join();if(handle>=0)sceAudioOutClose(handle);}
private:
    std::array<int16_t,48000> ring{};size_t read_at=0,write_at=0;
    bool paused=true;int handle=-1;std::mutex mutex;std::atomic<bool> quit{false};std::thread thread;
};
snes::Session* session=nullptr;Audio audio;
std::vector<snes::Game> games;std::vector<std::string> dirs;
std::string data_root,status,playing_title;
int pad=-1,user=255;uint32_t previous=0;size_t selection=0;
double last_tick=0,accumulated=0,next_repeat=0,last_save=0,pad_retry=0;
bool in_game=false,return_latch=false,state_latch=false;
std::string Upper(std::string value){for(char& c:value){if(c>='a'&&c<='z')c-=32;if(static_cast<unsigned char>(c)>127)c=' ';}return value;}
void Refresh() {
    dirs={data_root+"/games","/app0/games"};
    for(int i=0;i<8;++i) {
        const auto root="/mnt/usb"+std::to_string(i);
        dirs.push_back(root+"/homebrew/PS5SNES/games");dirs.push_back(root+"/homebrew/PS5SNES/roms");
        dirs.push_back(root+"/PS5SNES/games");dirs.push_back(root+"/SNES");
    }
    auto remote=fe::nfs::ConfiguredDirectories(data_root+"/nfs.txt");
    dirs.insert(dirs.end(),remote.begin(),remote.end());
    games=snes::Scan(dirs);selection=std::min(selection,games.empty()?size_t(0):games.size()-1);
    status=games.empty()?"NO GAMES - COPY .SNES OR .ZIP TO THE GAMES FOLDER":std::to_string(games.size())+" SNES GAMES FOUND";
    std::printf("[nes] %s\n",status.c_str());std::fflush(stdout);
}
uint32_t ReadPad() {
    const double now=Now();
    if(pad<0&&now>=pad_retry){pad=scePadOpen(user,0,0,nullptr);pad_retry=now+2;}
    Pad state{};state.lx=state.ly=state.rx=state.ry=128;
    if(pad<0||scePadReadState(pad,&state)!=0)return 0;
    auto buttons=state.buttons;
    if(state.lx<64)buttons|=0x80;else if(state.lx>192)buttons|=0x20;
    if(state.ly<64)buttons|=up;else if(state.ly>192)buttons|=down;
    return buttons;
}
void StartGame() {
    if(games.empty())return;
    std::vector<uint8_t> bytes;
    if(!snes::ReadRom(games[selection].path,bytes,status))return;
    if(!session->Load(games[selection].path,std::move(bytes))){status=session->error;return;}
    playing_title=games[selection].title;status=session->error;in_game=true;
    last_tick=Now();last_save=last_tick;accumulated=1/session->fps;
    audio.Pause(false);
}
void Draw(ps5::demo::Canvas& canvas) noexcept {
    using ps5::demo::Color;
    const double now=Now();const uint32_t held=ReadPad(),pressed=held&~previous;previous=held;
    const bool return_combo=(held&l1)&&(held&r1)&&(held&touch);
    if(in_game) {
        if(return_combo&&!return_latch) {
            audio.Pause(true);
            bool saved=session->Unload();in_game=false;
            status=saved?"GAME CLOSED - SAVED":session->error;
        } else {
            const bool combo=(held&l3r3)==l3r3;
            if(combo&&!state_latch&&(held&(up|down))) {
                bool okay=(held&up)?session->SaveState():session->LoadState();
                if(!okay)std::printf("[nes] %s\n",session->error.c_str());
                state_latch=true;
            }
            if(!combo)state_latch=false;
            session->buttons[0]=return_combo||combo?0:snes::MapButtons(held);
            accumulated+=std::clamp(now-last_tick,0.0,0.1);last_tick=now;
            unsigned runs=0;
            while(accumulated>=1/session->fps&&runs++<6) {
                session->Run();audio.Push(session->audio);accumulated-=1/session->fps;
            }
            if(now-last_save>5){if(!session->SaveBattery())std::printf("[nes] save: %s\n",session->error.c_str());last_save=now;}
        }
    } else {
        const bool repeat=(held&(up|down))&&now>=next_repeat;
        if((pressed&(up|down))||repeat) {
            if(!games.empty()){if(held&down)selection=(selection+1)%games.size();else selection=(selection+games.size()-1)%games.size();}
            next_repeat=now+((pressed&(up|down))?0.35:0.1);
        }
        if(pressed&square)Refresh();
        if(pressed&cross)StartGame();
    }
    return_latch=return_combo;
    if(in_game) {
        canvas.clear(Color::background);
        canvas.text(64,30,"PS5SNES V1 MODDED BY YAHBALLON",3,Color::cyan);
        // (AI-assisted) Integer scaling keeps the SNES pixels sharp; needs proper testing on PS5.
        if(!session->pixels.empty())canvas.blit(session->pixels.data(),session->width,session->height,4);
        canvas.text(64,1030,"L1 R1 TOUCHPAD: MENU    OPTIONS: START    TOUCHPAD: SELECT",3,Color::white);
        return;
    }
    canvas.clear(Color::background);
    canvas.text(100,70,"PS5SNES",12,Color::cyan);
    canvas.text(100,190,"V1 MODDED BY YAHBALLON",4,Color::white);
    const size_t first=(selection/12)*12;
    for(size_t row=0;row<12&&first+row<games.size();++row) {
        const size_t index=first+row;const unsigned y=290+row*48;
        if(index==selection)canvas.rectangle(90,y-8,1740,42,Color::panel);
        auto label=Upper(games[index].title);if(label.size()>68)label.resize(68);
        canvas.text(110,y,label,4,index==selection?Color::yellow:Color::white);
    }
    auto message=Upper(status);if(message.size()>90)message.resize(90);
    canvas.text(100,895,message,3,Color::yellow);
    canvas.text(100,950,"CROSS: PLAY    UP DOWN: SELECT    SQUARE: REFRESH",4,Color::white);
    canvas.text(100,1010,"SNES A: CROSS    SNES B: CIRCLE    SAVES: AUTOMATIC",3,Color::cyan);
    std::fflush(stdout);
}
}
int main() {
    const auto access=elevation::request(elevation::Capability::filesystem);
    data_root="/data/PS5SNES";
    if(mkdir(data_root.c_str(),0777)!=0&&errno!=EEXIST)data_root="/download0/PS5SNES";
    mkdir(data_root.c_str(),0777);mkdir((data_root+"/games").c_str(),0777);
    mkdir((data_root+"/saves").c_str(),0777);mkdir((data_root+"/logs").c_str(),0777);
    int fd=open((data_root+"/logs/boot.log").c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_APPEND,0666);
    if(fd>=0){dup2(fd,fileno(stdout));dup2(fd,fileno(stderr));close(fd);setvbuf(stdout,nullptr,_IONBF,0);}
    std::printf("[nes] PS5SNES v1 modded by YAHBALLON (AI-assisted), filesystem=%u path=%s\n",unsigned(access),data_root.c_str());
    sceUserServiceInitialize(nullptr);sceUserServiceGetInitialUser(&user);scePadInit();
    pad=scePadOpen(user,0,0,nullptr);bool sound=audio.Open();
    snes::Session core(data_root+"/saves");session=&core;Refresh();
    if(!sound)status="AUDIO PORT FAILED - SEE BOOT.LOG";
    if(access!=elevation::Status::ok)status="FILESYSTEM ACCESS FAILED - SEE BOOT.LOG";
    ps5::demo::run_frames(Draw,"PS5SNES v1 modded by YAHBALLON");
}
