#include "fe_covers.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
static std::vector<uint8_t> Read(const std::string&path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
int main(int argc,char**argv){assert(argc==3);std::string project=argv[1],cache=argv[2];auto text=Read(project+"/bin/resources/fonts/Roboto-Regular.ttf"),icons=Read(project+"/bin/resources/fonts/promptfont.otf"),image=Read(project+"/snes/sce_sys/icon0.png");fe::Fonts fonts;assert(fonts.Init(text.data(),text.size(),icons.data(),icons.size()));
 std::vector<fe::GameInfo> games(2);for(int i=0;i<2;++i){games[i].title="Test "+std::to_string(i);games[i].stem=games[i].title;games[i].serial="SNES-0000000"+std::to_string(i);games[i].file="Test.sfc";games[i].path="/missing/"+std::to_string(i)+"/Test.sfc";}
 fe::CoverConfig cfg;cfg.window_radius=1;cfg.cache_dir=cache;cfg.url_template="https://test.invalid/${serial}";std::atomic<int> calls0=0,calls1=0;std::atomic<bool> fail=false;
 fe::CoverService covers;covers.Start(games,&fonts,cfg,[&](const std::string&url,std::vector<uint8_t>&out){if(url.ends_with("0"))++calls0;else ++calls1;out=image;return fail?-1:200;});
 auto await_cover=[&](int target){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){fe::CoverImage result;while(covers.Poll(result))if(result.kind==fe::CoverImage::Cover&&result.game==target)return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}return false;};
 assert(await_cover(1));assert(calls0==1&&calls1==1);assert(!covers.Refresh(-1));assert(!covers.Refresh(2));assert(covers.Refresh(0));assert(await_cover(0));assert(calls0==2&&calls1==1);
 auto old=Read(cache+"/"+games[0].serial+".jpg");assert(!old.empty());fail=true;assert(covers.Refresh(0));auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(calls0<3&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(5));assert(calls0>=3&&calls0<=4&&calls1==1);assert(covers.Stop(5000));assert(Read(cache+"/"+games[0].serial+".jpg")==old);
 puts("Selected-only refresh, invalid index rejection and cached-image preservation on download failure passed (AI-assisted)");}
