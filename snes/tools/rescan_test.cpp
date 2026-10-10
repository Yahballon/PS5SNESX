// (AI-assisted) Verify that the original settings sheet exposes and triggers a manual rescan.
#include "fe_options.h"
#include <cassert>
#include <cstdio>
int main() {
    fe::OptionsPaths paths;
    paths.gs_ini="/tmp/PS5SNES-no-existing-ini";
    paths.nfs_url=[] { return std::string("nfs://example/SNES"); };
    paths.save_nfs_url=[](const std::string&,std::string&){return true;};
    bool requested=false;
    paths.rescan_library=[&](std::string&){requested=true;return true;};
    fe::OptionsSheet sheet;sheet.Open(paths,nullptr);
    bool found=false;
    for(const auto& row:sheet.rows())if(row.kind==fe::OptionsSheet::Kind::RescanLibrary) {
        found=true;assert(sheet.Value(row)=="Restart required");assert(sheet.Activate(row,0));
    }
    assert(found&&requested&&sheet.status().find("Restart")!=std::string::npos);
    puts("Original settings-sheet rescan action passed (AI-assisted)");
}
