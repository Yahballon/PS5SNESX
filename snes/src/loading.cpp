// SNESXY HYPERCUBE: original animated 16-bit boot, not NESYX Pixel Reactor.
// Pure native Vulkan UI primitives: no video files, network or audio dependencies.
#include "loading.h"
#include "snesxy_build_version.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace snes {
namespace {
constexpr float pi=3.14159265359f;
constexpr uint32_t c(uint8_t r,uint8_t g,uint8_t b,uint8_t a=255) {
    return uint32_t(r)|(uint32_t(g)<<8)|(uint32_t(b)<<16)|(uint32_t(a)<<24);
}
struct P { float x,y; };
struct V { float x,y,z; };
// Camera projects an independently spinning wireframe cube to the 2D PS5 UI.
P project(V v,float angle,float tilt,float cx,float cy,float scale) {
    const float a=std::cos(angle),b=std::sin(angle);
    const float x=v.x*a-v.z*b,z=v.x*b+v.z*a;
    const float u=std::cos(tilt),w=std::sin(tilt);
    const float y=v.y*u-z*w,depth=v.y*w+z*u;
    const float perspective=3.1f/(3.1f-depth*0.23f);
    return {cx+x*scale*perspective,cy+y*scale*perspective};
}
}
void BuildLoadingFrame(fe::FrameDesc& frame,const fe::Fonts& font,unsigned width,unsigned height,
                       const ScanProgress& progress,double elapsed,bool done) {
    frame=fe::FrameDesc{};
    if(!width||!height)return;
    const float W=float(width),H=float(height),k=H/2160.f,t=float(elapsed);
    const float dark[4]={0.009f,0.008f,0.035f,1.f},middle[4]={0.035f,0.013f,0.071f,1.f};
    const float bottom[4]={0.004f,0.009f,0.027f,1.f};
    std::copy(dark,dark+4,frame.bg.top);
    std::copy(middle,middle+4,frame.bg.mid);
    std::copy(bottom,bottom+4,frame.bg.bottom);
    frame.bg.glow_color[0]=0.49f;frame.bg.glow_color[1]=0.17f;frame.bg.glow_color[2]=0.98f;frame.bg.glow_color[3]=0.40f;
    frame.bg.glow_pos[0]=0.50f;frame.bg.glow_pos[1]=0.42f;frame.bg.glow_pos[2]=0.75f;frame.bg.glow_pos[3]=0.49f;
    frame.bg.floor_glow[0]=0.20f;frame.bg.floor_glow[1]=0.40f;frame.bg.floor_glow[2]=0.92f;frame.bg.floor_glow[3]=0.075f;
    frame.bg.misc[0]=W/H;frame.bg.misc[1]=0.8f;frame.bg.misc[2]=0.55f;frame.bg.misc[3]=t;
    frame.time=t;
    auto& ui=frame.ui;
    ui.reserve(3200);
    const uint32_t white=c(248,246,255),muted=c(186,177,225),violet=c(163,111,255);
    // Four pastel beams nod to the original 16-bit SNES button palette.
    const std::array<uint32_t,4> spectrum={c(114,189,255),c(176,128,255),c(255,112,193),c(119,244,203)};
    auto rect=[&](float x,float y,float w,float h,float r,uint32_t color){
        if(w>0&&h>0)fe::Fonts::AddRoundedRect(ui,x,y,w,h,r,color);
    };
    auto title=[&](const std::string& text,float x,float y,float size,uint32_t color,float weight,fe::Fonts::Align align=fe::Fonts::Left){
        font.AddText(ui,text.c_str(),x,y,size*k,color,weight,align);
    };
    // Cinematic scanlines drift opposite to the prism rotation.
    for(int i=0;i<16;++i){
        const float y=H*(0.095f+float(i)/19.f)+std::sin(t*0.65f+float(i))*9*k;
        rect(0,y,W,1.5f*k,0,c(75,79,185,24));
    }
    // Four corner rails keep the accent free of a distracting rectangular dock.
    const float rail=120*k;
    rect(90*k,108*k,rail,7*k,2*k,spectrum[0]);
    rect(90*k,108*k,7*k,rail,2*k,spectrum[1]);
    rect(W-90*k-rail,108*k,rail,7*k,2*k,spectrum[2]);
    rect(W-97*k,108*k,7*k,rail,2*k,spectrum[3]);
    title("SNESXY",136*k,200*k,96,white,0.72f);
    title(std::string("by YAHBALLON  /  v")+snesxy_build::kVersion,140*k,258*k,34,muted,0.18f);
    title(done?"LIBRARY READY":"16-BIT BOOT SEQUENCE",W-110*k,186*k,37,
        done?spectrum[3]:spectrum[1],0.35f,fe::Fonts::Right);

    const float cx=W*0.5f,cy=H*0.432f;
    // Hypercube consists of two separate 3D cubes connected at every corner.
    // Eight vertices / twelve edges on each one, plus eight connecting edges.
    std::array<P,8> outer,inner;
    for(int i=0;i<8;++i) {
        const V v={(i&1)?1.f:-1.f,(i&2)?1.f:-1.f,(i&4)?1.f:-1.f};
        outer[i]=project(v,t*0.37f,0.43f+0.25f*std::sin(t*0.26f),cx,cy,304*k);
        const V q={v.x*0.58f,v.y*0.58f,v.z*0.58f};
        inner[i]=project(q,-t*0.29f,0.64f+0.20f*std::cos(t*0.38f),cx,cy,304*k);
    }
    const auto segment=[&](P a,P b,uint32_t color,int dots,float diameter){
        for(int j=0;j<=dots;++j){
            const float v=float(j)/float(dots);
            const float pulse=0.4f+0.6f*std::sin(t*2.8f-v*8.f);
            const float d=diameter*(0.8f+0.2f*pulse);
            rect(a.x+(b.x-a.x)*v-d*0.5f,a.y+(b.y-a.y)*v-d*0.5f,d,d,d*0.20f,color);
        }
    };
    for(int i=0;i<8;++i){
        for(int bit=0;bit<3;++bit){
            int target=i^(1<<bit);
            if(target<=i)continue;
            segment(outer[i],outer[target],spectrum[bit],11,9*k);
            segment(inner[i],inner[target],spectrum[(bit+1)%4],9,7*k);
        }
        segment(outer[i],inner[i],c(165,132,246,145),8,5*k);
        // Pulsing corner nodes highlight the cube's silhouette.
        const float glow=0.5f+0.5f*std::sin(t*3.2f+float(i)*1.3f);
        const float d=(13.f+8.f*glow)*k;
        rect(outer[i].x-d/2,outer[i].y-d/2,d,d,4*k,spectrum[i%4]);
    }
    // Four floating SNES face-button satellite signals circle the hypercube.
    const float orbitRadius=480*k;
    for(int i=0;i<4;++i){
        const float theta=0.5f*pi*i-t*0.19f;
        const float x=cx+std::cos(theta)*orbitRadius,y=cy+std::sin(theta)*orbitRadius*0.49f;
        const float size=(29.f+8.f*std::sin(t*2+float(i)))*k;
        rect(x-size/2,y-size/2,size,size,size/2,spectrum[i]);
    }
    // Subtle floating pixels, distinct from NESYX starfield/reaction core.
    for(int i=0;i<30;++i){
        const float seed=float((i*83+17)%127)/127.f;
        const float x=W*(0.13f+0.75f*std::fmod(seed+t*0.008f,1.f));
        const float y=H*(0.19f+0.60f*std::fmod(float((i*37)%101)/101.f-t*0.018f+1.f,1.f));
        rect(x,y,4*k,4*k,0,spectrum[i%4]);
    }
    // Native real-time ROM scan counters. Unknown NFS folder totals are
    // displayed as a moving pulse, never an invented percentage.
    const float infoY=H*0.715f;
    title(done?"CARTRIDGES ONLINE":"SYNCING YOUR CARTRIDGES",cx,infoY/k,73,white,0.55f,fe::Fonts::Center);
    const std::string tally=std::to_string(progress.games)+" GAMES FOUND  /  "+
                           std::to_string(progress.entries)+" ENTRIES READ";
    title(tally,cx,infoY/k+65,35,muted,0.18f,fe::Fonts::Center);
    const float barW=std::min(W-230*k,1610*k),bx=cx-barW/2,by=H*0.807f;
    rect(bx,by,barW,18*k,9*k,c(47,39,91));
    const bool measured=progress.sources_total>0;
    const float ratio=done?1.f:(measured?
        std::clamp(float(progress.sources_done)/float(progress.sources_total),0.f,1.f):0.f);
    if(done||measured) {
        if(ratio>0)rect(bx,by,barW*ratio,18*k,9*k,spectrum[1]);
        if(ratio>0.01f&&!done)rect(bx+barW*ratio-15*k,by-4*k,30*k,26*k,6*k,spectrum[2]);
    }else {
        const float move=0.5f+0.5f*std::sin(t*2.4f);
        rect(bx+move*(barW-250*k),by,250*k,18*k,9*k,spectrum[0]);
    }
    std::string dir=progress.directory.empty()?"Searching game locations...":progress.directory;
    if(dir.size()>92)dir="..."+dir.substr(dir.size()-89);
    title(dir,cx,H*0.858f/k,32,muted,0.14f,fe::Fonts::Center);
    title("FOLDERS CHECKED  "+std::to_string(progress.sources_done)+" / "+std::to_string(progress.sources_total),
          cx,H*0.910f/k,35,done?spectrum[3]:violet,0.22f,fe::Fonts::Center);
    title("SNESXY  /  16-BIT HYPERCUBE",W-116*k,H*0.967f/k,28,c(123,137,190),0.14f,fe::Fonts::Right);
}
} // namespace snes
