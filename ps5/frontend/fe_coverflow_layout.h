// Original Wii-era-style 3D carousel for the native PS5 Vulkan shelf.
// Independent implementation; no Wii platform source or assets are copied.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fe_math.h"
#include <algorithm>
#include <cmath>

namespace fe
{
// All case dimensions remain tied to the existing PS5 3D case mesh.
constexpr float kClassicCaseHalfHeight = 0.95f;
constexpr float kClassicCenterZ = 1.05f;

struct ClassicCoverPose
{
    Mat4 model;
    float brightness = 1.0f;
    float scale = 1.0f;
};

// Fractional selection offset makes spring scrolling continuous.
inline ClassicCoverPose ClassicCoverflowPose(float offset, float time_seconds)
{
    const float distance = std::fabs(offset);
    const float sign = offset < 0.0f ? -1.0f : 1.0f;
    const float side = Smoothstep(0.0f, 1.0f, std::min(distance, 1.0f));
    const float tail = std::max(distance - 1.0f, 0.0f);
    // Oversized front-facing selection; angled side cases recede and shrink.
    const float outer_scale = std::max(0.62f, 0.96f - 0.072f * tail);
    const float scale = Mix(1.28f, outer_scale, side);
    const float x = sign * Mix(0.0f, 1.67f + 0.70f * tail, side);
    const float z = Mix(kClassicCenterZ, -0.52f - 0.26f * tail, side);
    const float centre_sway = (1.0f - side) * 0.023f * std::sin(time_seconds * 0.7f);
    const float yaw = Mix(centre_sway, -sign * Radians(51.0f), side);
    // Scaling a case raises its middle while its lower edge stays on the floor.
    const float hover = (1.0f - side) * (0.014f + 0.007f * (1.0f + std::sin(time_seconds * 1.2f)));
    const float y = (scale - 1.0f) * kClassicCaseHalfHeight + hover;

    ClassicCoverPose out;
    out.model = Mat4::Translate(x, y, z) * Mat4::RotateY(yaw) * Mat4::Scale(scale, scale, scale);
    out.scale = scale;
    out.brightness = std::max(0.56f, Mix(1.0f, 0.84f, side) - 0.040f * tail);
    return out;
}

/* Porpoise-inspired shallow-angle cover flow for PS5NESX.
 * Adapted to the native Vulkan case mesh from elripalda/Porpoise
 * src/ui_app_views.cpp (Ruben, 2026), GPL-3.0-or-later. We keep our
 * centre case enlarged to fit the NES shelf but use the shallow
 * yaw, 1st/2nd neighbour spacing and receding scale profile. */
inline ClassicCoverPose PorpoiseCoverflowPose(float offset, float time_seconds)
{
    const float a = std::fabs(offset);
    const float sign = offset < 0.0f ? -1.0f : 1.0f;
    const float outward = std::max(0.0f, a - 1.0f);
    const float x = sign * (1.65f * std::min(a, 1.0f) + 1.23f * outward);
    const float scale = a <= 1.0f
        ? 1.28f - 0.18f * a
        : std::max(0.62f, 1.10f - 0.125f * outward);
    const float yaw = sign * (0.22f * std::min(a, 1.0f));
    const float center_sway = 0.045f * std::sin(time_seconds * 0.9f)
        * (1.0f - Smoothstep(0.0f, 0.7f, std::min(a, 0.7f)));
    const float z = kClassicCenterZ - 0.54f * std::min(a, 1.0f) - 0.18f * outward;
    const float hover = (1.0f - Smoothstep(0.0f, 1.0f, std::min(a, 1.0f)))
        * 0.017f * (1.0f + std::sin(time_seconds * 0.9f));
    const float y = (scale - 1.0f) * kClassicCaseHalfHeight + hover;
    ClassicCoverPose out;
    out.model = Mat4::Translate(x, y, z)
        * Mat4::RotateY(yaw + center_sway)
        * Mat4::Scale(scale, scale, scale);
    out.scale = scale;
    out.brightness = std::max(0.65f, 1.0f - 0.10f * std::min(a, 1.0f) - 0.035f * outward);
    return out;
}

} // namespace fe
