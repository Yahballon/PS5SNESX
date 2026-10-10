// (AI-assisted) Scan progress shared between a background scanner and the original shelf.
#pragma once
#include "fe_renderer.h"
#include "fe_games.h"
#include "scan_progress.h"
#include <functional>
#include <mutex>
#include <string>
namespace snes {
struct LoadingState {
    std::mutex mutex;
    ScanProgress progress;
    bool done=false;
    std::vector<fe::GameInfo> games;
    void Publish(const ScanProgress& value) {std::lock_guard<std::mutex> lock(mutex);progress=value;}
    ScanProgress Snapshot(bool& complete) {std::lock_guard<std::mutex> lock(mutex);complete=done;return progress;}
};
void BuildLoadingFrame(fe::FrameDesc& frame,const fe::Fonts& fonts,unsigned width,unsigned height,
                       const ScanProgress& progress,double elapsed,bool done=false);
}
