// Native SNES library adapter for the unmodified Porpoise Gfx renderer.
// Project Porpoise: copyright (C) 2026 Ruben, GPL-3.0-or-later.
// https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5
// This adapter contains no Dolphin emulation code.
#pragma once
#include "fe_covers.h"
#include "../porpoise_original/src/ui_gfx.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace fe {
class ExactPorpoiseShelf {
public:
    bool Init(const porpoise::ui::GfxInit& config, const std::vector<GameInfo>& games);
    bool Ready() const { return m_ready; }
    void Cover(const CoverImage& image);
    void Select(int index, float scroll, double elapsed, const std::string& clock);
    void SetCoverProgress(const CoverProgress& state) { m_cover_progress = state; }
    void SetTheme(const std::string& theme);
    const std::string& Theme() const { return m_theme; }
    void Prepare(unsigned slot, float w, float h);
    void Upload(VkCommandBuffer cmd) { if(m_ready) m_gfx.record_uploads(cmd); }
    void Record(VkCommandBuffer cmd) { if(m_ready) m_gfx.record(cmd); }
    void Shutdown();
private:
    void DrawTile(int i, float cx, float cy, float w, float h,
                  float yaw, float alpha, bool focused, bool reflection);
    porpoise::ui::Gfx m_gfx;
    std::vector<std::string> m_names;
    std::vector<porpoise::ui::Texture*> m_art;
    int m_selected = 0;
    float m_scroll = 0;
    double m_elapsed = 0;
    std::string m_clock;
    CoverProgress m_cover_progress;
    std::string m_theme = "royal-indigo";
    porpoise::ui::Texture* m_fallback = nullptr; // reusable offline placeholder, contained without stretching
    bool m_ready = false;
};
} // namespace fe
