// (AI-assisted) Original PS5SX2 frontend startup, with a SNES playback backend.
#include "fe_ps5.h"
#include "elevation.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" int sceNetInit();
int main() {
    sceNetInit();
    const auto access=elevation::request(elevation::Capability::filesystem);
    std::string root="/data/PS5SNES";
    if(mkdir(root.c_str(),0777)!=0&&errno!=EEXIST)root="/download0/PS5SNES";
    mkdir(root.c_str(),0777);
    for(const char* dir:{"games","saves","settings","covers","cache","logs"})mkdir((root+"/"+dir).c_str(),0777);
    mkdir((root+"/cache/ps5vk-shader-cache").c_str(),0777);
    setenv("PS5VK_SHADER_CACHE_DIR",(root+"/cache/ps5vk-shader-cache").c_str(),1);
    int fd=open((root+"/logs/boot.log").c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_APPEND,0666);
    if(fd>=0){dup2(fd,fileno(stdout));dup2(fd,fileno(stderr));close(fd);setvbuf(stdout,nullptr,_IOLBF,4096);}
    std::printf("[boot] PS5SNES v6 by YAHBALLON (AI-assisted), original PS5SX2 shelf, filesystem=%u\n",unsigned(access));
    OrbisFrontendPaths paths;paths.top_dir=root;paths.games_dir=root+"/games";paths.settings_dir=root+"/settings";
    paths.gs_ini=root+"/gs.ini";paths.covers_dir=root+"/covers";paths.cache_dir=root+"/cache";
    paths.logs_dir=root+"/logs";paths.settings_log=root+"/logs/settings.log";paths.serial_cache=root+"/cache/serials.txt";
    paths.usb_list=root+"/cache/usb-games.txt";paths.allow_download=true;paths.sound=true;
    paths.report_header="PS5SNES v6 by YAHBALLON (AI-assisted)";
    for(int i=0;i<8;++i){auto usb="/mnt/usb"+std::to_string(i);
        for(const char* folder:{"/homebrew/PS5SNES/games","/homebrew/PS5SNES/roms","/PS5SNES/games","/PS5SNES/roms","/homebrew/PPSA99205/games","/homebrew/PPSA99205/roms","/SNES"})paths.usb_dirs.push_back(usb+folder);}
    for(const char* local:{"/data/PS5SNES/games","/data/PS5SNES/roms","/data/SNES","/data/homebrew/PPSA99205/games","/data/homebrew/PPSA99205/roms"})paths.usb_dirs.push_back(local);
    orbis_frontend_set_language(root+"/lang");
    orbis_web_start(paths,"v6 by YAHBALLON");
    bool ran=false;orbis_frontend_run(paths,"v6 by YAHBALLON",&ran);
    for(;;)sleep(1);
}
