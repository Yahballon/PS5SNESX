# PS5SNES v6 by YAHBALLON (AI-assisted)

Includes the local/USB library cache fix, bounded offline-NAS detection, corrected
empty-library hints, non-modded menu branding and R2 selected-cover refresh.
The new 723-file SNES cover index uses normalized title/region matching and
libretro fallback. Super Mario World and Donkey Kong Country repository URLs
returned HTTP 200; actual PNG decoding and revision/title matching passed.
Selected-only refresh and cached image retention on download failure passed.
Native compilation, memory metadata and signed executable integrity passed.
Console testing of the latest cover update remains required.


## Source release preparation — 2026-10-10 (AI-assisted)

Installation and build instructions now match v6. GitHub reporting templates
are specific to PS5SNES. Obsolete NES entry points and duplicate README stubs
were removed; unused FCEUmm sources are excluded from the source ZIP.
The shared PCSX2/PS5SX2 scaffold and upstream notices are retained.

The host integration test passed again in Linux/WSL with Clang 18: generated
LoROM and ZIP loading, copier headers, video, SPC audio, controller input,
battery saves, state roundtrip, PAL timing and continuous 48-kHz resampling.
The source ZIP has a regenerated per-file SHA-256 manifest and archive checksum.

The console executable was not rebuilt during this source cleanup. Earlier
native-build and cover-test results above describe prior work. New console
builds and the latest cover behavior still require PS5 hardware validation.
