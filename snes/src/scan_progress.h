// (AI-assisted) SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <functional>
#include <string>
namespace snes {
struct ScanProgress {
    size_t games=0, entries=0, sources_done=0, sources_total=0;
    std::string directory;
};
// Thread-local observer: web-server scans never overwrite the boot-screen progress.
bool ScanFailed();
void ReportCached(size_t count);
void SetScanProgress(std::function<void(const ScanProgress&)> callback);
}
