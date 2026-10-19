// Port adapter for the ORIGINAL Project Porpoise Gfx renderer, shaders,
// perspective, cover-flow arrangement and liquid-glass card primitives.
// Copyright (C) 2026 Ruben (Project Porpoise), GPL-3.0-or-later.
// Adaptation for SNES game selection: YAHBALLON, GPL-3.0-or-later.
#include "fe_exact_porpoise.h"
#include "fe_snesxy_cover_aspect.h"
#include "snesxy_build_version.h"
#include "../porpoise_original/src/ui_app_common.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
namespace fe {
using namespace porpoise::ui;
using namespace porpoise::ui::look;
using PorpoiseTexture = porpoise::ui::Texture;
extern "C" const uint8_t fe_snesxy_default[], fe_snesxy_default_end[];

bool ExactPorpoiseShelf::Init(const GfxInit& config, const std::vector<GameInfo>& games)
{
    m_names.clear();
    for (const GameInfo& g : games) m_names.push_back(g.title);
    m_art.assign(games.size(), nullptr);
    std::printf("[Porpoise Exact] texture_uv=flat_quad_rect+v_local (v1.0.18)\n");
    m_ready = m_gfx.init(config);
    // A single fallback texture is shared by every game without real artwork.
    m_fallback = nullptr;
    if (m_ready) {
        CoverImage placeholder;
        const std::vector<uint8_t> bytes(fe_snesxy_default, fe_snesxy_default_end);
        // The uploaded box art is already portrait (1024 x 1536): preserve
        // every source pixel and reuse one full-resolution GPU texture.
        if (CoverService::Decode(bytes, 768, placeholder))
            m_fallback = m_gfx.texture_rgba(placeholder.rgba.data(), placeholder.width, placeholder.height);
        std::printf("[Porpoise Exact] built-in NO COVER: %s\n", m_fallback ? "ready" : "unavailable");
    }
    std::printf("[Porpoise Exact] original UI Gfx init=%d, games=%zu\n",
                m_ready ? 1 : 0, games.size());
    return m_ready;
}
void ExactPorpoiseShelf::Shutdown()
{
    if (!m_ready) return;
    for (PorpoiseTexture* t : m_art) if (t) m_gfx.free_texture(t);
    m_art.clear();
    if (m_fallback) m_gfx.free_texture(m_fallback);
    m_fallback = nullptr;
    m_gfx.shutdown();
    m_ready = false;
}
void ExactPorpoiseShelf::Cover(const CoverImage& image)
{
    if (!m_ready || image.kind != CoverImage::Cover || image.game < 0 ||
        static_cast<size_t>(image.game) >= m_art.size() ||
        image.width <= 0 || image.height <= 0 ||
        image.rgba.size() != static_cast<size_t>(image.width) * image.height * 4)
        return;
    PorpoiseTexture*& art = m_art[static_cast<size_t>(image.game)];
    PorpoiseTexture* replacement = m_gfx.texture_rgba(image.rgba.data(), image.width, image.height);
    if (replacement) {
        if (art) m_gfx.free_texture(art);
        art = replacement;
    }
}
void ExactPorpoiseShelf::Select(int index, float scroll, double elapsed, const std::string& clock)
{
    m_selected = index;
    m_scroll = scroll;
    m_elapsed = elapsed;
    m_clock = clock;
}

// Porpoise 3D case drawing adapted to the natural aspect ratio of each
// SNES cover image (North American landscape and occasional portrait art).
void ExactPorpoiseShelf::DrawTile(int i, float cx, float cy, float w, float h,
                                  float yaw, float alpha, bool focused, bool reflection)
{
    Gfx& g = m_gfx;
    const float m = 13.0f; /* glass frame margin */
    const float glow = focused ? 20.0f : 0.0f;
    const float inner_r = kR * 0.6f;
    PorpoiseTexture* real = i >= 0 && static_cast<size_t>(i) < m_art.size() ? m_art[static_cast<size_t>(i)] : nullptr;
    PorpoiseTexture* cover = real ? real : m_fallback; // prefer downloaded or local boxart
    const float texRatio = SNESXYImageRatio(cover ? cover->width : 0,
                                           cover ? cover->height : 0);
    // Missing artwork uses a wide SNES case, but our square app emblem is
    // letterboxed inside it rather than stretched out of proportion.
    const SNESXYCoverSize inset = SNESXYFitInside(texRatio, w, h);

    if (reflection)
    {
        const float base = cy + h * 0.5f + m;
        const Corner r[4] = {project(cx, base, -w * 0.5f, 0, yaw),
                             project(cx, base, w * 0.5f, 0, yaw),
                             project(cx, base, w * 0.5f, h * 0.85f, yaw),
                             project(cx, base, -w * 0.5f, h * 0.85f, yaw)};
        if (cover) {
            // Preserve the entire box illustration in its reflection too.
            // A square fallback graphic sits centered inside its wide frame.
            const float uv[4] = {0, 0, 1, 1};
            if (!real) {
                g.quad3d(nullptr, r, w, h * 0.85f,
                         with_alpha(rgba(0x1E174B),0.22f*alpha),inner_r,true,true);
                const float imageW=inset.width;
                const float imageH=inset.height*0.85f;
                const Corner small[4]={
                    project(cx,base,-imageW*0.5f,0,yaw),
                    project(cx,base, imageW*0.5f,0,yaw),
                    project(cx,base, imageW*0.5f,imageH,yaw),
                    project(cx,base,-imageW*0.5f,imageH,yaw)
                };
                g.quad3d(cover,small,imageW,imageH,
                         with_alpha(rgba(0xB8C8FF),0.30f*alpha),
                         inner_r,true,true,uv);
            } else {
                g.quad3d(cover, r, w, h*0.85f,
                         with_alpha(rgba(0xB8C8FF),0.30f*alpha),
                         inner_r,true,true,uv);
            }
        } else {
            g.quad3d(nullptr, r, w, h * 0.85f,
                     with_alpha(rgba(0x2A4FD0), 0.25f * alpha), inner_r, true, true);
        }
        return;
    }
    Glass face;
    face.tint = kGlassBody;
    face.rim = focused ? kIcy : kEdge;
    face.fade = alpha;
    face.radius = kR;
    face.rim_w = focused ? 3.2f : 2.2f;
    face.glow = glow;
    face.phase = cx / 1920.0f;
    const float depth = 34.0f * (w / kTileW);
    glass_block(g, cx, cy, w + m * 2, h + m * 2, depth, yaw, 0, glow * 3, face);
    const Corner c[4] = {project(cx, cy, -w * 0.5f, -h * 0.5f, yaw),
                         project(cx, cy, w * 0.5f, -h * 0.5f, yaw),
                         project(cx, cy, w * 0.5f, h * 0.5f, yaw),
                         project(cx, cy, -w * 0.5f, h * 0.5f, yaw)};
    if (cover)
    {
        // No source-UV cropping: every pixel of the original horizontal OR
        // vertical SNES cover must be visible on its native-aspect case.
        const float uv[4] = {0,0,1,1};
        if (real) {
            g.quad3d(cover,c,w,h,with_alpha(kWhite,alpha),inner_r,
                     false,false,uv);
        } else {
            g.quad3d(nullptr,c,w,h,with_alpha(rgba(0x111B41),alpha),
                     inner_r,false,false);
            const Corner contained[4]={
                project(cx,cy,-inset.width*0.5f,-inset.height*0.5f,yaw),
                project(cx,cy, inset.width*0.5f,-inset.height*0.5f,yaw),
                project(cx,cy, inset.width*0.5f, inset.height*0.5f,yaw),
                project(cx,cy,-inset.width*0.5f, inset.height*0.5f,yaw)
            };
            g.quad3d(cover,contained,inset.width,inset.height,
                     with_alpha(kWhite,alpha),inner_r,false,false,uv);
        }
    } else {
        g.quad3d(nullptr, c, w, h, with_alpha(rgba(0x16328F), alpha),
                 inner_r, false, false);
    }
    Glass gloss;
    gloss.face = 2;
    gloss.radius = inner_r;
    gloss.rim_w = 0;
    gloss.tint = kWhite;
    gloss.rim = kClear;
    gloss.fade = alpha;
    gloss.phase = cx / 1920.0f;
    g.glass(c, w, h, 0, gloss);
}
void ExactPorpoiseShelf::SetTheme(const std::string& requested)
{
    std::string next=requested;
    if(next!="royal-indigo" && next!="sunset-arcade" && next!="emerald-circuit" &&
       next!="glacier-glass" && next!="midnight-gold") next="royal-indigo";
    if(next==m_theme)return;
    m_theme=next;
    std::printf("[Porpoise Exact] theme=%s\n",m_theme.c_str());
}

void ExactPorpoiseShelf::Prepare(unsigned slot, float w, float h)
{
    if (!m_ready) return;
    Gfx& g = m_gfx;
    // Source: Porpoise make_porpoise(), ui_app_common.hpp and
    // ui_app.cpp (the top bar, library and dock visual values).
    // Original Project Porpoise look parameters:
    // source porpoise_original/src/ui_theme.cpp (make_* methods).
    // We deliberately preserve its existing Vulkan shader/backgrounds.
    Look look;
    look.panel_style = 1;
    float radius=24.0f;
    // Five SNESXY-exclusive palettes: each with a distinct light field.
    if(m_theme=="royal-indigo") {
        look.background=3;look.dark=0.17f;
        look.light0={0.34f,0.15f,0.99f,1.f};look.light1={0.90f,0.29f,0.89f,1.f};
        look.hue=0.76f;look.hue_spread=0.23f;radius=24.f;
    } else if(m_theme=="sunset-arcade") {
        look.background=10;look.dark=0.19f;
        look.light0={1.0f,0.22f,0.19f,1.f};look.light1={0.99f,0.77f,0.25f,1.f};
        look.hue=0.06f;look.hue_spread=0.33f;radius=18.f;
    } else if(m_theme=="emerald-circuit") {
        look.background=7;look.dark=0.13f;
        look.light0={0.13f,0.83f,0.46f,1.f};look.light1={0.26f,0.97f,0.77f,1.f};
        look.hue=0.38f;look.hue_spread=0.18f;radius=20.f;
    } else if(m_theme=="glacier-glass") {
        look.background=1;look.dark=0.08f;look.fill_alpha=1.4f;
        look.light0={0.43f,0.78f,0.98f,1.f};look.light1={0.86f,0.95f,1.0f,1.f};
        look.hue=0.55f;look.hue_spread=0.11f;radius=27.f;
    } else if(m_theme=="midnight-gold") {
        look.background=1;look.dark=0.025f;look.fill_alpha=1.5f;
        look.light0={0.99f,0.67f,0.15f,1.f};look.light1={0.93f,0.83f,0.47f,1.f};
        look.hue=0.12f;look.hue_spread=0.10f;radius=22.f;
    }
    kR=radius;
    g.begin(slot, w, h, static_cast<float>(m_elapsed), 0, false);
    g.set_look(look);
    g.background();

    // SNESXY branding with the build number visible immediately below it.
    // No Library / Save States / Settings tabs or L1/R1 glyphs.
    const auto themeAccent=m_theme=="sunset-arcade"?rgba(0xFFB86A):
       m_theme=="emerald-circuit"?rgba(0x63FFC1):
       m_theme=="glacier-glass"?rgba(0xB2E9FF):
       m_theme=="midnight-gold"?rgba(0xF8D58A):rgba(0xC7B8FF);
    if (g.brand_mask()) g.image(g.brand_mask(), 48, kBarCy-32, 64, 64, themeAccent);
    g.text_mid(Font::SemiBold, 30, 132, kBarCy-13, themeAccent, Align::Left,
               "SNESXY", 7.0f);
    char build_credit[96];
    std::snprintf(build_credit, sizeof(build_credit), "by YAHBALLON  |  v%s", snesxy_build::kVersion);
    g.text_mid(Font::SemiBold, 17, 132, kBarCy+22, kLavender, Align::Left, build_credit);
    if(!m_clock.empty())
        g.text_mid(Font::SemiBold,27,1866,kBarCy,kWhite,Align::Right,m_clock);

    // Compact, non-blocking progress HUD. The data comes from the cover
    // worker's mutex-protected snapshot; rendering never touches networking.
    const CoverProgress& progress = m_cover_progress;
    // The status box is transient: it disappears automatically when the
    // final missing ROM has been checked (or when the worker stops because
    // the network is unavailable). A manual R2 refresh starts a new cycle.
    if(progress.games > 0 && progress.planned > 0 &&
       !progress.done && !progress.offline)
    {
        constexpr float px=1414,py=149,pw=452,ph=100;
        g.panel(px,py,pw,ph,rgba(0x061339,0.78f),0.84f,19,
                rgba(0x42AFFF,0.78f),1.3f,0,0.15f);
        std::string state;
        if(progress.transferring)
        {
            state = "Fetching: " + progress.current;
            if(state.size()>42) state=state.substr(0,39)+"...";
        }
        else state="Searching for missing game covers";
        g.text(Font::SemiBold,19,px+19,py+14,kWhite,Align::Left,state);
        char detail[100];
        std::snprintf(detail,sizeof(detail),"%d / %d checked   |   %d saved   |   %d unavailable",
            progress.attempted,progress.planned,progress.downloaded,progress.failed);
        g.text(Font::SemiBold,16,px+19,py+45,kLavender,Align::Left,detail);
        const float barX=px+19,barY=py+80,barW=pw-38;
        g.panel(barX,barY,barW,6,rgba(0x25426B,0.78f),0.85f,3);
        const float fraction=std::clamp(progress.planned > 0 ?
            float(progress.attempted)/float(progress.planned) : 0.0f,0.0f,1.0f);
        if(fraction>0)
            g.panel(barX,barY,std::max(3.0f,barW*fraction),6,rgba(0x60D7FF),0.95f,3);
        if(progress.transferring && fraction<1)
        {
            // Moving pulse means the current request is still active even if
            // its download progress has no reported byte count.
            const float remaining=barW*(1.0f-fraction);
            const float pulse=std::fmod(float(m_elapsed)*0.45f,1.0f);
            g.panel(barX+barW*fraction+pulse*std::max(1.0f,remaining-18),
                    barY,18,6,rgba(0xBCEEFF),0.9f,3);
        }
    }

    const int shown=static_cast<int>(m_names.size());
    g.text(Font::Bold,54,90,132,kWhite,Align::Left,"Your Games");
    char counter[48];
    std::snprintf(counter,sizeof counter,"%d games",shown);
    g.text(Font::SemiBold,32,92,200,kLavender,Align::Left,counter);

    // Original drawing order, offsets, widths, scales and yaw.
    std::vector<int> order;
    for(int i=0;i<shown;++i)
        if(std::fabs(float(i)-m_scroll)<4.2f) order.push_back(i);
    std::sort(order.begin(),order.end(),[&](int a,int b){
        return std::fabs(float(a)-m_scroll)>std::fabs(float(b)-m_scroll);
    });
    for(int pass=0;pass<2;++pass)
        for(int i:order)
        {
            const float k=float(i)-m_scroll;
            const float a=std::fabs(k),s=k<0?-1.0f:1.0f;
            float x,scale,yaw;
            if(a<=1.0f)
            {
                // Wider SNES boxes need more breathing room than NES cases.
                x=kCx+s*a*545.0f;
                scale=1.0f-0.12f*a;
                yaw=s*a*0.18f;
            }
            else
            {
                x=kCx+s*(545.0f+(a-1.0f)*390.0f);
                scale=std::max(0.50f,0.88f-0.10f*(a-1.0f));
                yaw=s*0.18f;
            }
            const float alpha=std::clamp(1.0f-(a-3.0f),0.0f,1.0f);
            const bool focused=i==m_selected && a<0.5f;
            const float sway=std::sin(float(m_elapsed)*0.9f)*0.06f*
                (1.0f-std::min(a*2.0f,1.0f));
            const PorpoiseTexture* texture =
                static_cast<size_t>(i)<m_art.size() ? m_art[static_cast<size_t>(i)] : nullptr;
            const float ratio = texture
                ? SNESXYImageRatio(texture->width,texture->height)
                : kSNESXYDefaultBoxRatio;
            // Sizes are calculated per ROM, not per catalogue. Landscape
            // covers get wide cases while rarer portrait covers stay tall.
            const SNESXYCoverSize dimensions =
                SNESXYCaseSize(ratio,kTileH,525.0f,scale);
            DrawTile(i,x,kCy,dimensions.width,dimensions.height,
                     yaw+sway,alpha,focused,pass==0);
        }
    if(m_selected>0) g.glyph(Glyph::Arrow,34,kCy,46,rgba(0x58B8FF),-kPi*0.5f);
    if(m_selected+1<shown) g.glyph(Glyph::Arrow,1886,kCy,46,rgba(0x58B8FF),kPi*0.5f);
    if(m_selected>=0 && m_selected<shown)
    {
        g.text(Font::Bold,46,kCx,748,kWhite,Align::Center,m_names[m_selected]);
        // Game title only; no redundant system subtitle.
    }

    // No Recently Browsed dock: keep the coverflow and footer uncluttered.

    // Same prompt alignment and icon glyph renderer as Porpoise.
    constexpr float hintY=1010;
    struct Prompt {Glyph glyph; const char* text;};
    const Prompt left[]={{Glyph::DPad,"Browse"},{Glyph::Cross,"Play"}};
    const Prompt right[]={{Glyph::Square,"Settings"}};
    float lx=58;
    for(const auto& p:left)
    {
        g.glyph(p.glyph,lx+22,hintY,44,kWhite);
        lx+=52+g.text_mid(Font::SemiBold,24,lx+52,hintY,kWhite,Align::Left,p.text)+40;
    }
    float rx=1862;
    for(int i=0;i>=0;--i)
    {
        const float tw=g.measure(Font::SemiBold,24,right[i].text);
        g.text_mid(Font::SemiBold,24,rx,hintY,kWhite,Align::Right,right[i].text);
        g.glyph(right[i].glyph,rx-tw-26,hintY,44,kWhite);
        rx-=tw+98;
    }
    char index_text[32];
    std::snprintf(index_text,sizeof index_text,"%02d / %02d",m_selected+1,shown);
    g.text_mid(Font::SemiBold,24,960,hintY,kLavender,Align::Center,index_text,2);
}
} // namespace fe
