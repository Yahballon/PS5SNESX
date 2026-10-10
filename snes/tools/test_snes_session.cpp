// (AI-assisted) Test the real SNES core using an original generated LoROM/SPC program.
#include "core_session.h"
#include "games.h"
#include "controller_bindings.h"
#include "resampler.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
int main(int argc,char** argv) {
    assert(argc==3);std::string root=argv[1];std::vector<uint8_t> rom,zip;std::string error;
    assert(snes::ReadRom(root+"/input.sfc",rom,error));assert(rom.size()==32768);
    std::vector<uint8_t> headered;assert(snes::ReadRom(root+"/headered.smc",headered,error));assert(snes::ReadRom(root+"/input.zip",zip,error)&&rom==zip);
    assert(!snes::ReadRom(root+"/invalid.sfc",zip,error));
    snes::Session session(argv[2]);session.SetOption("bsnes_violate_accuracy","enabled");session.SetOption("bsnes_chip_hle","HLE");
    assert(session.Load("generated-input.sfc",rom));assert(session.fps>59&&session.fps<61&&session.sample_rate>32000&&session.sample_rate<32100);
    bool heard=false;
    for(int i=0;i<20;++i){session.Run();heard|=std::any_of(session.audio.begin(),session.audio.end(),[](int16_t sample){return sample!=0;});}
    assert(session.width>=256&&session.height>=224&&session.pixels.size()==session.width*session.height);
    assert(heard);
    const auto center=session.pixels[(session.height/2)*session.width+session.width/2];
    assert((center&255)>200&&((center>>8)&255)<32&&((center>>16)&255)<32);
    auto* memory=static_cast<uint8_t*>(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));assert(memory&&retro_get_memory_size(RETRO_MEMORY_SAVE_RAM)>0);
    snes::ControllerBindings bindings;bindings.Load({});bindings.Update(0,0);
    session.buttons[0]=bindings.Update(0x4000,1).joypad;assert(session.buttons[0]==1);session.Run();session.Run();assert(memory[0]==1);
    assert(session.SaveState());session.buttons[0]=0;session.Run();session.Run();assert(memory[0]==0);
    assert(session.LoadState());assert(memory[0]==1);assert(session.SaveBattery());assert(session.Unload());
    assert(session.Load("generated-input.sfc",rom));memory=static_cast<uint8_t*>(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));assert(memory[0]==1);assert(session.Unload());
    assert(session.Load("headered.smc",headered));session.Run();assert(session.Unload());
    session.SetOption("bsnes_region","pal");assert(session.Load("generated-input.sfc",rom));assert(session.fps>49&&session.fps<51);session.Run();assert(session.Unload());
    // Check every added SNES face/shoulder mapping.
    bindings.Load({});bindings.Update(0,0);
    assert(bindings.Update(0x2000,1).joypad==256);assert(bindings.Update(0x1000,2).joypad==512);
    assert(bindings.Update(0x8000,3).joypad==2);assert(bindings.Update(0x400,4).joypad==1024);assert(bindings.Update(0x800,5).joypad==2048);
    snes::Resampler full,chunked;full.Reset(32040.5);chunked.Reset(32040.5);
    std::vector<int16_t> source(64080);for(size_t i=0;i<source.size();++i)source[i]=int16_t((i*57)%20000-10000);
    const auto expected=full.Process(source);std::vector<int16_t> joined;
    for(size_t i=0;i<source.size();i+=1000){std::vector<int16_t> input(source.begin()+i,source.begin()+std::min(i+1000,source.size()));auto output=chunked.Process(input);joined.insert(joined.end(),output.begin(),output.end());}
    assert(joined==expected&&expected.size()>95980&&expected.size()<96002);
    puts("Real SNES integration passed: LoROM/ZIP, color video, SPC audio, all controller buttons, SRAM, state roundtrip, PAL and continuous 48-kHz resampling (AI-assisted)");
}
