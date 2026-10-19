<div align="center">

# SNESXY

**16-Bit Memories. Next-Gen Homebrew.**

**SNESXY v1.0.8 · by YAHBALLON · PPSA99205**

[![Native PS5 Build](https://github.com/Yahballon/PS5SNESX/actions/workflows/build-snesxy.yml/badge.svg)](https://github.com/Yahballon/PS5SNESX/actions/workflows/build-snesxy.yml)
[![Releases](https://img.shields.io/github/v/release/Yahballon/PS5SNESX?style=flat-square)](https://github.com/Yahballon/PS5SNESX/releases)

</div>

SNESXY is a native Super Nintendo emulator for jailbroken PS5 consoles, based on the **bsnes-mercury balanced** core. Its modern 3D CoverFlow frontend takes inspiration from our NESYX work, while using its own SNES-themed palettes and an original Hypercube startup animation. The repository stays named PS5SNESX to preserve existing links.

## What's new in v1.0.8

- **Native SNES box proportions:** Landscape SNES covers render on **wide glass cases**; rarer portrait artwork stays in taller cases. Every cover preserves its image ratio and full image without cropping, and reflections match the case size. Cached artwork is reused automatically—no cover-cache reset.

- **Adaptive 3D glass CoverFlow:** landscape and portrait box art automatically use matching case shapes, uncropped textures, reflections, smooth movement and cover caching.
- **Five SNESXY themes:** Royal Indigo (violet/pink), Sunset Arcade (orange/coral), Emerald Circuit (mint green), Glacier Glass (ice blue), and Midnight Gold (black/gold). Change them in **Settings → Appearance → Theme**; saved for the entire library.
- **16-Bit Hypercube:** animated rotating wireframe cubes with colorful satellites, scanlines, and accurate NFS/ROM scanning counters; wholly separate from NESYX's Pixel Reactor.
- **Additional SNES cover provider:** [Libretro Named Boxarts](https://thumbnails.libretro.com/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/Named_Boxarts/) alongside the original SNES collection and Libretro GitHub mirror.
- **16:9 Fullscreen** by default for new installations; 4:3 and Integer pixels remain available.
- **Close confirmation:** in the game library, press **○** to open the prompt, **×** to exit, or **○** to cancel.

## Install

Download **`PPSA99205.zip`** from [Releases](https://github.com/Yahballon/PS5SNESX/releases). Extract the complete `PPSA99205` directory and install it using a supported PS5 homebrew launcher (commonly `/data/homebrew/PPSA99205/`). Do not omit `eboot.bin`, `sce_sys/`, `sce_module/` or the packaged helper files.

> **Update safely:** Existing games, battery saves, save states, configurations, cover art and logs are intentionally kept in `/data/PS5SNES/`. **SNESXY does not rename that data directory or change the Title ID `PPSA99205`.**

Place your own legally acquired `.sfc`, `.smc`, `.swc` or `.fig` games in `/data/PS5SNES/games/`; compatible single-ROM ZIP files are also supported. USB and configured NFSv3 directories can be scanned. For NFS, use **Settings → Network** and restart/rescan the library.

## Controls

| Action | DualSense |
| --- | --- |
| Browse SNES games | D-pad / left stick |
| Launch selected game | × |
| Open Settings | □ |
| Close app prompt (library only) | ○ |
| Confirm exit / Cancel | × / ○ |
| SNES A / B / X / Y | ○ / × / △ / □ in-game |
| SNES L / R | L1 / R1 |
| Start / Select | Options / Touchpad click or Create |
| Save / Load state | L3 + R3 + D-pad Up / Down |
| Return from SNES game | L1 + R1 + Touchpad click |
| Refresh selected cover | R2 |

## Builds and source

[**Native Build & Release workflow**](.github/workflows/build-snesxy.yml) runs the SNESXY regression tests, provisions the pinned open-source PS5 toolchain, builds the native executable and creates `PPSA99205.zip`. Main-branch builds publish a release tagged `snesxy-v1.0.8`. The version is recorded in `app.json` and `snes/src/snesxy_build_version.h`.

```text
snes/src/                  SNES core integration, runtime and Hypercube loader
snes/third_party/          bsnes-mercury balanced source
snes/tools/                Native build and host tests
ps5/frontend/              SNESXY CoverFlow, settings, SNES cover worker
ps5/porpoise_original/     Licensed Vulkan/Porpoise UI components
ps5/frontend/assets/       Embedded icons and menu files
scripts/                   Pinned PS5 toolchain bootstrap
.github/workflows/         Automated native build and release
```

For manual builds, see [BUILDING.md](BUILDING.md). A prepared PS5 SDK/Vulkan build environment is required; building on ordinary Windows without the appropriate console toolchain does not produce a usable PS5 package.

## Credits and legal

**SNESXY by YAHBALLON.** Built using the open-source [bsnes-mercury](https://github.com/libretro/bsnes-mercury) core, PS5 homebrew frameworks and the appropriately licensed Porpoise frontend. Other upstream contributors and licenses are credited in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), [COPYING.GPLv3](COPYING.GPLv3) and the bundled dependency directories.

No commercial SNES games, BIOS files or other copyrighted ROMs are provided. This community project is not affiliated with Sony or Nintendo.
