// (AI-assisted) Exercise the actual tiled renderer, integer scaling and a failed flip.
#include "demo_renderer.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
static void* mapped;
static int waits=0;
static std::array<int,4> flips{};
static int flip_count=0;
static size_t Offset(unsigned x,unsigned y) {
    const uint32_t offset=((y<<4)&0x70)^((y<<5)&0xf00)^((y<<9)&0x1000)^((y<<8)&0x4000)^
                          ((x<<2)&0xc)^((x<<5)&0x380)^((x<<4)&0x400)^((x<<6)&0x800)^((x<<9)&0xa000);
    return size_t((y>>7)*15+(x>>7))*65536+offset;
}
extern "C" {
bool ps5ObserveOwnedAllocation(const void* p) noexcept {return p!=nullptr;}
size_t sceKernelGetDirectMemorySize(){return 64u<<20;}
int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t,size_t,int,int64_t* p){*p=0;return 0;}
int sceKernelMapDirectMemory(void** p,size_t length,int,int,int64_t,size_t){mapped=std::calloc(1,length);*p=mapped;return mapped?0:-1;}
int sceKernelSendNotificationRequest(uint32_t,void*,size_t,int){return 0;}
int sceKernelUsleep(uint32_t){return 0;}
int sceSystemServiceHideSplashScreen(){return 0;}
int sceVideoOutOpen(int,int,int,const void*){return 1;}
int sceVideoOutSetFlipRate(int,int){return 0;}
void sceVideoOutSetBufferAttribute2(void*,uint64_t,uint32_t,uint32_t w,uint32_t h,uint64_t,uint32_t,uint64_t){assert(w==1920&&h==1080);}
int sceVideoOutRegisterBuffers2(int,int,int,void*,int count,void*,int,void*){assert(count==2);return 0;}
int sceVideoOutSubmitFlip(int,int index,uint32_t,int64_t){assert(flip_count<4);flips[flip_count++]=index;return flip_count==2?-1:0;}
int sceVideoOutWaitVblank(int) {
    if(++waits<4)return 0;
    assert(flip_count==4);assert(flips[0]==0&&flips[1]==1&&flips[2]==1&&flips[3]==0);
    for(unsigned buffer=0;buffer<2;++buffer) {
        auto* p=static_cast<uint8_t*>(mapped)+buffer*0x1000000;
        auto pixel=[&](unsigned x,unsigned y){return *reinterpret_cast<uint32_t*>(p+Offset(x,y));};
        assert(pixel(0,0)==0xff190d0a);
        assert(pixel(956,536)==0xff0000ff);assert(pixel(959,539)==0xff0000ff);
        assert(pixel(960,536)==0xff00ff00);assert(pixel(956,540)==0xffff0000);
    }
    std::puts("Actual VideoOut renderer: tiled color, 4x scaling and failed-flip back-buffer safety passed (AI-assisted)");
    std::fflush(stdout);std::_Exit(0);
}
}
int main(){ps5::demo::run_frames([](ps5::demo::Canvas& c) noexcept {
    c.clear(ps5::demo::Color::background);
    const uint32_t pixels[]={0xff0000ff,0xff00ff00,0xffff0000,0xffffffff};c.blit(pixels,2,2,4);
},"test");}
