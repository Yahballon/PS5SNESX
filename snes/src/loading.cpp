// (AI-assisted) Loading feedback in the original PS5SX2 UI style.
#include "loading.h"
#include <algorithm>
#include <cmath>
namespace snes {
void BuildLoadingFrame(fe::FrameDesc& frame,const fe::Fonts& fonts,unsigned width,unsigned height,
                       const ScanProgress& progress,double elapsed,bool done) {
    frame=fe::FrameDesc{};const float W=width,H=height,k=H/2160.0f;
    frame.bg.glow_color[0]=0.62f;frame.bg.glow_color[1]=0.50f;frame.bg.glow_color[2]=0.86f;frame.bg.glow_color[3]=0.5f;
    frame.bg.glow_pos[0]=0.5f;frame.bg.glow_pos[1]=0.48f;frame.bg.glow_pos[2]=0.58f;frame.bg.glow_pos[3]=0.4f;
    frame.bg.floor_glow[0]=0.5f;frame.bg.floor_glow[1]=0.8f;frame.bg.floor_glow[2]=0.3f;frame.bg.floor_glow[3]=0.035f;
    const float top[4]={0.035f,0.035f,0.048f,1},mid[4]={0.075f,0.068f,0.105f,1},bottom[4]={0.012f,0.012f,0.018f,1};
    std::copy(top,top+4,frame.bg.top);std::copy(mid,mid+4,frame.bg.mid);std::copy(bottom,bottom+4,frame.bg.bottom);
    frame.bg.misc[0]=W/H;frame.bg.misc[1]=0.8f;frame.bg.misc[2]=0.55f;frame.bg.misc[3]=elapsed;
    auto& ui=frame.ui;constexpr uint32_t white=0xffffffff,accent=0xfff5adc2,faint=0xffc7c7c7;
    float x=112*k;x+=fonts.AddText(ui,"PS5",x,150*k,60*k,white,0.8f);
    fonts.AddText(ui,"SNES",x+2*k,150*k,60*k,accent,0.8f);
    fonts.AddText(ui,"v2 modded by YAHBALLON",114*k,214*k,32*k,faint,0.1f);
    fonts.AddText(ui,done?"Games ready":"Loading games",W/2,H*0.43f,92*k,white,0.3f,fe::Fonts::Center);
    std::string counts=std::to_string(progress.games)+" games found  |  "+std::to_string(progress.entries)+" entries read";
    fonts.AddText(ui,counts.c_str(),W/2,H*0.51f,46*k,faint,0.1f,fe::Fonts::Center);
    const float bar_width=std::min(W-224*k,1800*k),left=(W-bar_width)/2,bar_y=H*0.56f;
    fe::Fonts::AddRoundedRect(ui,left,bar_y,bar_width,24*k,12*k,0xff383838);
    // NFS does not disclose the total file count up front: animate activity instead of inventing a percentage.
    const float segment=done?bar_width:bar_width*0.20f;
    const float offset=done?0:float((std::sin(elapsed*2.5)+1)*0.5)*(bar_width-segment);
    fe::Fonts::AddRoundedRect(ui,left+offset,bar_y,segment,24*k,12*k,accent);
    std::string location=progress.directory.empty()?"Preparing game scan...":progress.directory;
    if(location.size()>100)location="..."+location.substr(location.size()-97);
    fonts.AddText(ui,location.c_str(),W/2,H*0.65f,36*k,faint,0.1f,fe::Fonts::Center);
    std::string sources="Folders checked: "+std::to_string(progress.sources_done)+" / "+std::to_string(progress.sources_total);
    fonts.AddText(ui,sources.c_str(),W/2,H*0.73f,34*k,faint,0.1f,fe::Fonts::Center);
}
}
