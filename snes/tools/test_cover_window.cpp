// (AI-assisted) Use actual font rasterization and CoverService with a 907-game library.
#include "fe_covers.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <set>
#include <thread>
static std::vector<uint8_t> Read(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv) {
    assert(argc==2);std::string root=argv[1];
    auto text=Read(root+"/bin/resources/fonts/Roboto-Regular.ttf"),icons=Read(root+"/bin/resources/fonts/promptfont.otf");
    fe::Fonts fonts;assert(fonts.Init(text.data(),text.size(),icons.data(),icons.size()));
    std::vector<fe::GameInfo> games(907);
    for(int i=0;i<907;++i){games[i].title="SNES test "+std::to_string(i);games[i].stem=games[i].title;games[i].file=games[i].stem+".nes";games[i].path="/missing/"+games[i].file;}
    fe::CoverConfig config;config.window_radius=12;config.allow_download=false;
    fe::CoverService covers;covers.Start(games,&fonts,config,{});
    auto collect=[&](int selected) {
        std::set<int> placeholders;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(std::chrono::steady_clock::now()<deadline) {
            fe::CoverImage image;
            while(covers.Poll(image))if(image.kind==fe::CoverImage::Placeholder&&std::abs(image.game-selected)<=12)placeholders.insert(image.game);
            const size_t expected=selected==0?13:25;
            if(placeholders.size()==expected)return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        assert(false&&"nearby artwork did not arrive");
    };
    collect(0);covers.SetSelected(450);collect(450);covers.SetSelected(0);collect(0);
    // Stop even while the bounded queue is full and the worker awaits a consumer.
    covers.SetSelected(800);std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(covers.Stop(2000));
    puts("907-game cover streaming: selected window, distant jumps, revisits and full-queue shutdown passed (AI-assisted)");
}
