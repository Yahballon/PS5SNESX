#include "cover_urls.h"
#include "fe_covers.h"
#include <cassert>
#include <fstream>
#include <cstdio>
int main(int argc,char**argv){assert(argc==2);fe::GameInfo game;game.title="Super Mario World";game.stem="Super Mario World (Europe) (Rev 1) [!]";game.region="Europe";auto mario=snes::CoverUrls(game);assert(!mario.empty()&&mario[0].find("Super%20Mario%20World%20%28USA%29.png")!=mario[0].npos);puts(mario[0].c_str());game.title="Donkey Kong Country";game.stem="Donkey Kong Country (USA) (Rev 2)";game.region="USA";auto donkey=snes::CoverUrls(game);assert(donkey[0].find("Donkey%20Kong%20Country%20%28USA%29.png")!=donkey[0].npos);puts(donkey[0].c_str());assert(snes::CoverKey("Super Mario World (U) [b1]")=="supermarioworld");game.title="Super Mario World 2 - Yoshi's Island";game.stem=game.title;auto sequel=snes::CoverUrls(game);assert(sequel[0]!=mario[0]);
 std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};fe::CoverImage image;assert(fe::CoverService::Decode(bytes,1024,image)&&image.width>0&&image.height>0);puts("Title/region/revision matching, sequel separation and actual repository PNG decode passed (AI-assisted)");}
