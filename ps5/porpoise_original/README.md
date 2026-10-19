# Original Project Porpoise renderer port for PS5NESX

**Experimental feature branch, NOT yet a verified PS5 release.**

Goal: Render the NES game library using the **original** Project Porpoise
Gfx Vulkan implementation (including its original SPIR-V shader bytecode),
rather than imitating it with the legacy black plastic NES cover mesh.

Original sources from
[elripalda/Porpoise-Dolphin-Emulator-for-PS5](https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5),
commit `2ed1ab5a06995aa198f7bccd4da0f90c3fc29d39`.
Source files are imported **byte-for-byte**, retaining the original
copyright headers and GPL-3.0-or-later licence. Their Git blob hashes
are verified during CI. Author: Ruben (Project Porpoise).

The NES bridge `../frontend/fe_exact_porpoise.{h,cpp}` is a new adapter;
unlike the upstream UI source, it must translate the NES game library to
the upstream renderer. It uses Porpoise's exact 1920x1080 coverflow
coordinates, front-cover glass block, reflection and cover artwork
geometry, including its source values and animations. The remaining NES
game details and Sony controller-specific settings are intentionally
PS5NESX's, not Dolphin emulator functions.

The build downloads the original Porpoise Nunito fonts and icons at a
**pinned revision**, verifies original Git blob hashes and bundles the UI
assets so the renderer can read them at `/app0/porpoise-ui/`.
The native Gfx renderer uses the precompiled original shader in
`src/ui_frag.inc`; it does not use our old NES black-plastic shader.
Included fonts and icons retain the original licences.

Fallback: If the original renderer fails to initialize, PS5NESX uses
the existing v1.0.15 Vulkan menu. The emulator's NES gameplay renderer
remains unchanged even when the original UI initializes.

Must test on an actual PS5 before release: renderer init, Vulkan driver
compatibility, frame latency, font and icon loading, original cover
perspective, reflection, input sounds, launching and exiting the game.
This is *not yet* a guarantee of pixel-perfect parity.

## v1.0.17 PS5 console screenshot regression

The v1.0.16 PS5 screenshot still showed the old black PS5NES case menu,
meaning the original renderer likely failed initialization and the legacy
fallback silently ran. The v1.0.16 build pipeline only verified a signed
package; it could not verify the actual PS5 output.

Changes:
- Embed the original pinned Porpoise Nunito typefaces and menu icons in
  eboot.bin and extract them to the writable /data/PS5NES/porpoise-ui folder.
  The runtime no longer assumes the package is at /app0.
- Keep the original Porpoise Gfx renderer, blue-glass box geometry and SPIR-V,
  but add explicit error logging around Vulkan and font initialization.
- Add its library top navigation bar, original 1920x1080 spatial positions,
  rendered controller icons and a bottom glass "Recently Browsed" dock.
  The latter uses actually selected games, not fabricated play history.
- If renderer initialization still fails, display a visible warning instead
  of making the old black cases look like a successful port.
- NES core, HD graphics, saved games, ROM paths and all v1.0.16 releases stay
  available; native signed build alone does not confirm working PS5 output.

Copyright remains with the original Porpoise authors for their borrowed code.
The adapter has intentionally different game-specific content and interaction.
A successful physical PS5 test is still needed to claim visual parity.

## v1.0.18 — Experimental PS5 Vulkan texture UV correction

An actual PS5 screenshot of v1.0.17 shows proper untextured glass, 3D
frames and grid floor, but the loaded NES cover textures, original Nunito
SDF atlas, and button textures appear as narrow *vertical stripes*.
Because all sampled textures are affected yet procedural shapes remain
correct, this points to texture coordinate interpolation or image sampling,
not the cover files or the missing optional Japanese font.

The original Porpoise fragment shader is now rebuilt from its GLSL during
the native GitHub Actions build. K_IMAGE, K_TEXT, K_COVER and K_REFLECTION
reconstruct their UV coordinates using the **v_local** varying (also used
by the visibly correct procedural glass) and flat v_p2 bounds. Original
projection, glass effects, room grid and UI geometry are preserved. The
shader fix is experimental and its effectiveness on the console still
must be confirmed. A successful native build is not proof.

An optional upstream NotoSansJP-Porpoise.ttf is also pinned and embedded,
to suppress the unrelated missing-CJK-font warning. The build verifies its
Git object checksum.

If stripes persist on v1.0.18, next debug step is GPU texture upload
row-pitch and VkImage sampling/readback. The ROM and HD-Pack core are
unchanged; saves, cover cache and app settings remain in /data/PS5NES.
