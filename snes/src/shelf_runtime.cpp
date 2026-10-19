// (AI-assisted) SNES core/video/audio integration, preserving the original shelf renderer.
#include "shelf_runtime.h"
#include "games.h"
#include "fe_settings.h"
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <time.h>
extern "C" {int sceAudioOutInit();int sceAudioOutOpen(int,int,int,unsigned,unsigned,unsigned);
int sceAudioOutOutput(int,const void*);int sceAudioOutClose(int);}
namespace snes {
class ShelfAudio {
    std::array<int16_t,48000> ring{};size_t read_at=0,write_at=0;
    int handle=-1;std::mutex mutex;std::atomic<bool> quit{false};std::thread thread;
public:
    bool Open() {sceAudioOutInit();handle=sceAudioOutOpen(255,0,0,256,48000,1);if(handle<0)return false;
        thread=std::thread([this]{alignas(64) int16_t block[512];while(!quit){std::memset(block,0,sizeof(block));
            {std::lock_guard<std::mutex> lock(mutex);for(unsigned i=0;i<512&&read_at!=write_at;++i){block[i]=ring[read_at];read_at=(read_at+1)%ring.size();}}
            if(sceAudioOutOutput(handle,block)<0){timespec pause{0,5000000};nanosleep(&pause,nullptr);}}});return true;}
    void Push(const std::vector<int16_t>& samples) {std::lock_guard<std::mutex> lock(mutex);
        for(size_t i=0;i+1<samples.size();i+=2){size_t a=(write_at+1)%ring.size(),b=(a+1)%ring.size();
            if(a==read_at||b==read_at)break;ring[write_at]=samples[i];ring[a]=samples[i+1];write_at=b;}}
    ~ShelfAudio(){quit=true;if(thread.joinable())thread.join();if(handle>=0)sceAudioOutClose(handle);}
};
ShelfRuntime::ShelfRuntime(const std::string& directory):core(directory+"/saves"),root(directory){}
ShelfRuntime::~ShelfRuntime()=default;
bool ShelfRuntime::Start(const std::string& path,fe::Renderer& renderer) {
    error.clear();std::vector<uint8_t> rom;if(!ReadRom(path,rom,error))return false;
    std::map<std::string,std::string> options;
    auto read=[&](const std::string& file){std::string text;fe::settings::ReadFile(file,text);
        for(const auto& item:fe::settings::ReadState(text).kv)options[item.first]=item.second;};
    read(root+"/gs.ini");std::string file=path.substr(path.find_last_of('/')+1);file.resize(file.find_last_of('.'));
    read(root+"/settings/"+file+".ini");
    auto get=[&](const std::string& key,const std::string& fallback){auto at=options.find(key);return at==options.end()?fallback:at->second;};
    core.SetOption("bsnes_region",get("SNES/Region","auto"));
    core.SetOption("bsnes_violate_accuracy","enabled");
    core.SetOption("bsnes_chip_hle",get("SNES/ChipMode","HLE"));
    core.SetOption("bsnes_gamma_ramp",get("SNES/Gamma","disabled"));
    aspect=get("SNES/Aspect","16:9");bindings.Load(options);sound=get("SNES/Sound","true")=="true";
    if(!core.Load(path,std::move(rom))){error=core.error;return false;}
    resampler.Reset(core.sample_rate);
    if(sound){audio=std::make_unique<ShelfAudio>();if(!audio->Open()){error="SNES audio port failed";audio.reset();}}
    menu_atlas=renderer.Atlas();active=true;previous=0;accumulated=0;last_save=0;
    return true;
}
void ShelfRuntime::Stop(fe::Renderer& renderer) {
    if(!core.Unload())error=core.error;
    audio.reset();renderer.SetAtlas(menu_atlas);
    if(game_texture)renderer.DestroyTexture(game_texture);game_texture=nullptr;active=false;
}
bool ShelfRuntime::Build(fe::Renderer& renderer,fe::FrameDesc& frame,uint32_t raw,double now) {
    const auto input=bindings.Update(raw,now);
    if(input.menu){Stop(renderer);return false;}
    if(input.save||input.load) {
        const bool okay=input.save?core.SaveState():core.LoadState();
        if(!okay)error=core.error;
        std::printf("[snesxy] state %s: %s\n",input.save?"save":"load",okay?"OK":core.error.c_str());
    }
    core.buttons[0]=input.joypad;
    if(previous==0){previous=now;last_save=now;accumulated=1/core.fps;}
    accumulated+=std::clamp(now-previous,0.0,0.1);previous=now;
    bool changed=false;unsigned runs=0;
    while(accumulated>=1/core.fps&&runs++<6){core.Run();if(audio)audio->Push(resampler.Process(core.audio));accumulated-=1/core.fps;changed=true;}
    if(now-last_save>=5){if(!core.SaveBattery())error=core.error;last_save=now;}
    // (AI-assisted) Deferred texture retirement uses the existing renderer's fenced frame slots.
    // Continuous menu/game transitions still need proper testing on PS5 hardware.
    if(changed&&!core.pixels.empty()) {
        auto* next=renderer.CreateTexture(core.width,core.height,VK_FORMAT_R8G8B8A8_UNORM,core.pixels.data());
        if(!next){error="SNES frame upload failed";Stop(renderer);return false;}
        renderer.SetAtlas(next);if(game_texture)renderer.DestroyTexture(game_texture);game_texture=next;
    }
    frame=fe::FrameDesc{};frame.fade=1;
    frame.bg.glow_pos[2]=frame.bg.glow_pos[3]=1;
    frame.bg.floor_glow[2]=frame.bg.floor_glow[3]=1;
    frame.bg.misc[0]=float(renderer.width())/renderer.height();frame.bg.misc[1]=0.5f;
    float w=renderer.width(),h=renderer.height();float width=w,height=h;
    if(aspect=="4:3"){width=std::min(w,h*4.0f/3.0f);height=width*3.0f/4.0f;}
    if(aspect=="Integer"){float scale=std::max(1.0f,std::floor(std::min(w/core.width,h/core.height)));width=core.width*scale;height=core.height*scale;}
    const float x=(w-width)/2,y=(h-height)/2;
    auto vertex=[](float x,float y,float u,float v){return fe::UiVertex{x,y,u,v,0xffffffff,0,0,0,2};};
    frame.ui={vertex(x,y,0,0),vertex(x+width,y,1,0),vertex(x,y+height,0,1),
              vertex(x,y+height,0,1),vertex(x+width,y,1,0),vertex(x+width,y+height,1,1)};
    return true;
}
}
