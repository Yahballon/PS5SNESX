// (AI-assisted) Shared controller schema used by runtime, settings sheet and tests.
#include "controller_bindings.h"
#include <algorithm>
namespace snes {
static bool Held(const Source& source,uint32_t raw) {
    return source.mask && (source.all?(raw&source.mask)==source.mask:(raw&source.mask)!=0);
}
static Source Parse(const std::string& value,const std::string& fallback) {
    for(const auto& source:sources)if(value==source.name)return source;
    for(const auto& source:sources)if(fallback==source.name)return source;
    return sources[0];
}
void ControllerBindings::Load(const std::map<std::string,std::string>& settings) {
    auto get=[&](const std::string& key,const std::string& fallback){auto at=settings.find(key);return at==settings.end()?fallback:at->second;};

    for(size_t i=0;i<mapping.size();++i){
        const char* fallback=buttons[i].fallback;

        mapping[i]=Parse(get(buttons[i].key,fallback),fallback);
    }
    for(size_t i=0;i<chords.size();++i)for(size_t j=0;j<3;++j) {
        const std::string key="SNES/"+std::string(shortcuts[i].name)+"Button"+std::to_string(j+1);
        chords[i][j]=Parse(get(key,shortcuts[i].fallback[j]),shortcuts[i].fallback[j]);
    }
    left_stick_dpad=get("SNES/LeftStickDpad","true")!="false";
    initialized=false;previous={};emergency_since=-1;
}
ControllerFrame ControllerBindings::Update(uint32_t raw,double now) {
    ControllerFrame out;std::array<bool,3> held{};uint32_t consumed=0;
    for(size_t i=0;i<chords.size();++i) {
        bool any=false,complete=true;
        for(const auto& source:chords[i])if(source.mask){any=true;complete&=Held(source,raw);}
        held[i]=any&&complete;
        if(held[i])for(const auto& source:chords[i])consumed|=source.mask;
    }
    // A binding held during launch only counts after it has been released and pressed again.
    if(initialized){out.save=held[0]&&!previous[0];out.load=!out.save&&held[1]&&!previous[1];out.menu=held[2]&&!previous[2];}
    previous=held;initialized=true;
    // Keep an exit available even if the configured menu shortcut is disabled.
    const bool emergency=(raw&0xc00)==0xc00&&(raw&0x40100000);
    if(emergency){if(emergency_since<0)emergency_since=now;if(now-emergency_since>=2)out.menu=true;}
    else emergency_since=-1;
    raw&=~consumed;
    for(size_t i=0;i<mapping.size();++i)if(Held(mapping[i],raw))out.joypad|=uint16_t(1u<<buttons[i].nes_id);
    return out;
}
}
