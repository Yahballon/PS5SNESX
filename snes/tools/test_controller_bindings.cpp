// (AI-assisted) Exercise real controller mapping, shortcut edges and original settings persistence.
#include "controller_bindings.h"
#include "fe_options.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <cstdio>
int main(int argc,char** argv) {
    assert(argc==2);snes::ControllerBindings bindings;bindings.Load({});bindings.Update(0,0);
    assert(bindings.Update(0x4000,0.1).joypad==256);
    assert(bindings.Update(0x2000,0.2).joypad==1);
    assert(bindings.Update(8,0.3).joypad==8);
    assert(bindings.Update(1,0.4).joypad==4);
    auto save=bindings.Update(6|0x10,0.5);assert(save.save&&!save.load&&save.joypad==0);
    assert(!bindings.Update(6|0x10,0.6).save);
    bindings.Update(6,0.7);assert(bindings.Update(6|0x40,0.8).load);
    assert(!bindings.Update(6|0x40,0.9).load);
    bindings.Update(0,1);assert(bindings.Update(0xc00|0x100000,1.1).menu);
    bindings.Load({{"SNES/ButtonA","Square"},{"SNES/ButtonB","None"},{"SNES/SaveButton1","Circle"},{"SNES/SaveButton2","L1"},{"SNES/SaveButton3","R1"}});
    bindings.Update(0,0);assert(bindings.Update(0x8000,0.1).joypad==256);
    assert(bindings.Update(0x2000,0.2).joypad==0);
    assert(!bindings.Update(0x2400,0.3).save);
    save=bindings.Update(0x2c00,0.4);assert(save.save&&save.joypad==0);
    assert(!bindings.Update(0x2c00,0.5).save);
    bindings.Load({{"SNES/SaveButton1","Cross"},{"SNES/SaveButton2","None"},{"SNES/SaveButton3","None"}});
    assert(!bindings.Update(0x4000,0).save); // Cross held while selecting a game cannot immediately save.
    bindings.Update(0,0.1);assert(bindings.Update(0x4000,0.2).save);
    bindings.Load({{"SNES/MenuButton1","None"},{"SNES/MenuButton2","None"},{"SNES/MenuButton3","None"}});
    bindings.Update(0,0);assert(!bindings.Update(0xc00|0x100000,1).menu);
    assert(!bindings.Update(0xc00|0x100000,2.9).menu);
    assert(bindings.Update(0xc00|0x100000,3.1).menu);
    bindings.Load({{"SNES/SwapAB","true"}});bindings.Update(0,0);assert(bindings.Update(0x4000,1).joypad==1);
    bindings.Load({{"SNES/ButtonA","invalid-value"}});bindings.Update(0,0);assert(bindings.Update(0x4000,1).joypad==256);
    std::string root=argv[1];std::filesystem::create_directories(root+"/settings");
    std::ofstream(root+"/gs.ini")<<"SNES/ButtonA=Square\n";
    std::ofstream(root+"/settings/Demo.ini")<<"SNES/ButtonA=Triangle\n";
    fe::OptionsPaths paths;paths.gs_ini=root+"/gs.ini";paths.settings_dir=root+"/settings";
    fe::GameInfo game;game.title="Demo";game.stem="Demo";
    fe::OptionsSheet sheet;sheet.Open(paths,&game);sheet.SetTab(fe::kTabControls);
    bool seen_a=false,seen_save=false,seen_load=false,seen_menu=false;
    for(const auto& row:sheet.rows()) {
        if(!row.def)continue;
        if(row.def->key=="SNES/ButtonA") {
            seen_a=true;assert(sheet.Value(row)=="Triangle");assert(sheet.Reset(row));
            // Reset reloads rows, so find the row anew after leaving this loop.
            break;
        }
    }
    sheet.Open(paths,&game);sheet.SetTab(fe::kTabControls);
    for(const auto& row:sheet.rows())if(row.def) {
        if(row.def->key=="SNES/ButtonA")assert(sheet.Value(row)=="Square"&&sheet.Source(row)==fe::OptionsSheet::From::Global);
        seen_save|=row.def->key=="SNES/SaveButton1";seen_load|=row.def->key=="SNES/LoadButton1";seen_menu|=row.def->key=="SNES/MenuButton3";
    }
    assert(seen_a&&seen_save&&seen_load&&seen_menu);
    sheet.Open(paths,nullptr);sheet.SetTab(fe::kTabControls);
    for(const auto& row:sheet.rows())if(row.def&&row.def->key=="SNES/ButtonA"){assert(sheet.Step(row,1));break;}
    sheet.Open(paths,nullptr);sheet.SetTab(fe::kTabControls);
    for(const auto& row:sheet.rows())if(row.def&&row.def->key=="SNES/ButtonA")assert(sheet.Value(row)=="Triangle");
    std::string text;assert(fe::settings::ReadFile(root+"/gs.ini",text));
    std::map<std::string,std::string> settings;for(const auto& kv:fe::settings::ReadState(text).kv)settings[kv.first]=kv.second;
    bindings.Load(settings);bindings.Update(0,0);assert(bindings.Update(0x1000,1).joypad==256);
    puts("Controller bindings passed: defaults, custom buttons, disabled buttons, chords, edge triggers, launch release, emergency return, legacy mapping and per-game/global persistence (AI-assisted)");
}
