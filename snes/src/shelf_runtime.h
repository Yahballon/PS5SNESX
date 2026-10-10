// (AI-assisted) SNES playback hosted by the original PS5SX2 Vulkan shelf.
#pragma once
#include "core_session.h"
#include "controller_bindings.h"
#include "resampler.h"
#include "fe_renderer.h"
#include <memory>
namespace snes {
class ShelfAudio;
class ShelfRuntime {
public:
    explicit ShelfRuntime(const std::string& directory);
    ~ShelfRuntime();
    bool Start(const std::string& path,fe::Renderer& renderer);
    bool Build(fe::Renderer& renderer,fe::FrameDesc& frame,uint32_t buttons,double now);
    void Stop(fe::Renderer& renderer);
    bool Active()const{return active;}
    bool UseLeftStickDpad()const{return bindings.left_stick_dpad;}
    std::string error;
private:
    Session core;
    ControllerBindings bindings;
    Resampler resampler;
    std::unique_ptr<ShelfAudio> audio;
    std::string root;
    fe::Texture* menu_atlas=nullptr;
    fe::Texture* game_texture=nullptr;
    bool active=false,sound=true;
    std::string aspect="4:3";
    double previous=0,accumulated=0,last_save=0;
};
}
