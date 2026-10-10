# PS5SX2: the PS5 layer

PS5SX2 runs PCSX2 on a jailbroken PS5. Its renderer is PCSX2's Vulkan backend on a modified version of ps5vk, the Vulkan driver from [Mihawk-99](https://github.com/mihawk-99)'s [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), tweaked specifically for PS5SX2 and linked into the app. It is developed on a PS5 Pro with firmware 11.40. The console runs the PS5SX2 Helper payload, which jailbreaks the app and mounts /data into its sandbox.

## Layout

- **The repository root** is PCSX2: upstream commit 450a39a, imported without its history, with the port's changes as commits on top.
- **`ps5/coreorbis/`**:
  - the app's entry point, `main-boot.cpp`;
  - the PS5 shims PCSX2 runs on, in `orbis-shims/` and `include-orbis/`;
  - the build, `Makefile.vk` and `link-vk.sh`.
- **`ps5/frontend/`**: the game shelf and the settings web page. `fe_shaders.inc` is generated from `shaders/` by `build_shaders.sh`, which needs glslangValidator.
- **`ps5/proto/native/`**: the host tool that converts and signs the eboot, plus the app's CRT and linker script. They come from BlackBearReloaded's ps5-native-app-boilerplate (GPL-3.0-or-later).
- **`ps5/tools/`**:
  - `build-mesa-util.sh`, from Mihawk-99's PS5_vkQuake;
  - `prepare_console_elf.py`.
- **`ps5/sce_sys/param.json`**: the app's parameters (title ID PPSA99203). The icon, the backgrounds and the music ship with the releases.

## Dependencies

- **The driver:** Swordpdf/PS5HB_Vulkan at commit 5da7401, built with its own scripts (`tools/setup-native-dependencies.sh`, then `tools/build-driver.sh`). `PS5_VULKAN_DIR` names the checkout.
- **`PS5SX2_DEPS`:** a folder holding:
  - `sdk285/`: the ps5-payload-dev SDK v0.42, with the changes to `target/lib` described below;
  - `ryml/`: rapidyaml at 0cd25a1, with its c4core submodule;
  - `glslang/`: glslang at 275822a. `glslang/build-ps5/` holds its static libraries, built with the SDK's CMake toolchain: Release, with `ENABLE_OPT`, `ENABLE_HLSL`, the binaries and the tests off;
  - `shaderc/`: shaderc v2026.2, used for its headers only.
- **Host tools:** LLVM 18 (`clang-18`, `clang++-18`, `ld.lld-18`, `llvm-objcopy-18`, `llvm-nm-18`), zlib's development files and Python 3.

Each path can also be set on its own: `RYML`, `PS5_PAYLOAD_SDK`, `GLSLANG`, `GLSLANG_BUILD` and `SHADERC_INCLUDE`.

**Not written down yet:** the SDK's `target/lib` differs from the stock v0.42 release.
- `crt1.o`, `libc++abi.a` and `libunwind.a` are the port's rebuilt copies.
- It adds the AGC import stubs `libSceAgc` and `libSceAgcDriver`, as `.so` and `.prx`.

The steps to recreate that folder from the stock SDK are still to be written down.

## Build

```sh
export PS5SX2_DEPS=/path/to/deps
export PS5_VULKAN_DIR=/path/to/PS5HB_Vulkan
cd ps5/coreorbis
make -f Makefile.vk -j"$(nproc)"
ORBIS_BUILD_TAG=my-build bash link-vk.sh
```

- The first link also builds the host tool, into `ps5/proto/native/build/host/`.
- The signed eboot is `ps5/build-vk/app/eboot.bin`.
- On the console, the app folder `/data/homebrew/PPSA99203/` holds:
  - `eboot.bin`;
  - `sce_sys/`;
  - `sce_module/libc.prx`, which the driver repository generates with `make libc` (see its `runtime/README.md`).

## Experimental direct NFS game loading (AI-assisted)

The shelf automatically downloads missing covers for NFS, USB and local games
after filesystem access is ready, using each disc's serial and the existing
xlenore/ps2-covers source. Covers already found locally take priority. Downloads
are stored in the local cover cache using descriptor-based atomic writes and
are reused on subsequent launches. The existing `nocoverdl` flag still disables
downloads. The cover endpoint returned HTTP 200 for the two tested game serials;
the PS5 build/link/signing passed, while actual console downloads need testing.
The cover diagnostics build discards the pre-elevation HTTPS context before
starting the shelf worker. A failed initialization/request can be reinitialized,
and a cover gets at most one transport retry. Network/TLS, HTTP, decode, cache
write and worker-start failures remain visible in the shelf status and logs.
Skipped candidates log serial, download flags and recent-404 suppression. The
actual download-method tests cover retry success, exhausted retry, invalid image
and failed cache persistence; the repository URL format is unchanged.
The exact HTTPS stage/code is forwarded to the shelf and saved separately to
`/data/PCSX2/logs/cover-download.log`. Boot logging creates `logs/` explicitly
and redirects stdout/stderr to one descriptor opened with append semantics.
It retries after filesystem access even when the early pipe logger failed.
Tests cover real file creation, both output streams, retry without an early
pipe, avoiding repeat truncation and propagation of the transport error text.

The public-driver test build also has a PS5-only ordinary descriptor-set path
when `VK_KHR_push_descriptor` is absent. It creates non-push layouts and uses
immutable sets from command-buffer-lifetime pools, released after completion.
TFX partial texture changes refresh all bindings in each snapshot. Desktop
requirements and drivers offering push descriptors retain the existing path.
The real descriptor-builder retarget/snapshot tests and PS5 compilation/linking
passed; rendering with this fallback still requires console verification.

The optional NFS backend connects directly from PS5SX2 using
[libnfs 5.0.3](https://github.com/sahlberg/libnfs/tree/libnfs-5.0.3).
It does not require Itemzflow or a kernel/FUSE mount. The NFS-enabled app has
passed its backend tests and was compiled, linked and signed using the public
payload SDK v0.42 with mihawk-99's SDK fork at 95c08f27386fc698f6bbe21dde3030140a41d10b
and PS5_Vulkan at f3cbf875f89978deae9f855b9be2610cec2818ae. This public build uses
different SDK/driver inputs from the official PS5SX2 release and has not been
tested on a console yet.

Build libnfs **for the PS5**, using the same SDK as the app, rather than a host
library. From the repository root, with an upstream libnfs-5.0.3 source checkout:

```sh
bash ps5/tools/build-libnfs.sh /path/to/libnfs /path/to/nfs-output /path/to/ps5-toolchain.cmake
export LIBNFS_PREFIX=/path/to/nfs-output/install
cd ps5/coreorbis
make -f Makefile.vk -j"$(nproc)"
ORBIS_BUILD_TAG=my-nfs-build bash link-vk.sh
```

Keep `LIBNFS_PREFIX` set for both compilation and linking. NFS builds use
`obj-vk-nfs/` so they do not reuse objects from builds without NFS. Without this
variable, the normal build has no additional dependency and logs an explanation
if an NFS source is configured.

In an NFS-enabled build, press **Square** on the shelf, then **R1** for
**All games**, and select **Network → NFS URL** on the Settings tab. Press
**Cross** to open the controller keyboard. Use the D-pad to choose characters,
Cross to enter them, Triangle to delete, and Circle to cancel. Select **Save**
to store the URL, or **Clear** then **Save** to remove that share. Cancelling
does not change the saved URL. Restart PS5SX2 to refresh the network games.
Saving edits the first configured share and keeps additional shares and comments.

Alternatively, create `/data/PCSX2/nfs.txt`, one directory per line:

```text
# Replace this example with your server and its exported game directory.
nfs://192.168.1.10/exports/ps2
```

Restart PS5SX2 after changing this file. Up to four directories are supported.
The NFS debug build logs the configured URL, each mount attempt, listing errors,
entry metadata and why each file was accepted or skipped.
NFS discs skip local `host:` root resolution during VM initialization. A remote
URI must not be sent through local working-directory or symlink syscalls; the
CDVD reader remains responsible for remote disc access. The actual HLE function
was tested for both remote bypass and the preserved local-disc path. Console
crash resolution still requires verification.
Servers returning
entries without a type are queried with lstat. If a requested subdirectory
cannot itself be mounted, the client can mount an accessible ancestor export
and traverse only the requested relative directory; a fresh context is used
for each bounded attempt. Directory listing and ISO opening share this mapping.
The running version is displayed at the upper-left wordmark; the large testing
overlay is disabled in the debug package.
The filesystem-helper test build includes an exact-title one-shot Lapy helper
for `PPSA99203`. After existing HEN initialization, the app verifies `/data`
with a PID-specific write/read/remove probe. If access is missing it requests
the cooperative resident service, then uses the bundled `lapy.elf` through
the already-running local elfldr on port 9021 when no resident claimed the
request. Copy the complete test title folder, including the helper and manifest.
Failed elevation is logged and shown as a notification; a root UID alone does
not count as verified filesystem access. This integration was explicitly
approved by the user and its host tests passed; console behavior is unverified.
The shelf and web game list scan each directory and one level of subfolders;
local/USB copies take priority when filenames match. The server must permit the
PS5 to mount and read the configured directory and any scanned subdirectory.
Use the server's export path, not its SMB share name. This backend uses NFSv3
with libnfs's default user/group identity; URL query options are not supported.

Only **ISO images** stream from NFS in this version; CHD, CSO and ZSO remain
local/USB formats. The BIOS, saves, settings and covers remain on the console
or their existing local paths. Disable disc precaching for NFS games. Network
read errors fail the read rather than returning fabricated sectors; an offline
source is logged and local games can still be listed. RPC operations have a
five-second timeout with automatic reconnect disabled, although a whole scan
can involve many operations. Wired networking is preferable for streaming.

Backend verification: `python ps5/frontend/host/test-nfs.py` tests short reads,
reads above 4 GiB, bounds, connection/open/stat/read failures, cleanup, directory
filtering and configuration parsing using a libnfs API double.
The same tests cover controller keyboard navigation, UTF-8 deletion, input
length limits, URL validation and preserving other shares/comments when editing.
POSIX persistence tests cover first save with a missing parent folder, replacement,
clearing, readback, interrupted/short writes and preserving the previous file after
an injected write failure. Console settings and NFS configuration use descriptor
I/O instead of C-library text streams in the public-SDK build; save failures report
the failing operation and errno.
The SDK dependency builds and app link/signing passed. Console playback and
real-server compatibility still need verification.

## Licence and credits

PS5SX2 is GPL-3.0-or-later, like PCSX2; the licence text is `COPYING.GPLv3` at the root.

Third-party parts keep their licences:
- optional libnfs 5.0.3: LGPL-2.1-or-later (retain its upstream licence and sources with NFS-enabled distributions);
- the stb headers: public domain or MIT;
- Project Nayuki's QR Code generator: MIT;
- the Font Awesome Free brands font and icons: SIL OFL 1.1 for the font, CC BY 4.0 for the icons (`frontend/assets/SOURCES.txt`, `frontend/assets/fonts/FontAwesome-LICENSE.txt`).

The Discord and X logos are their owners' trademarks, used only to link to the author's accounts.

Thanks to:
- the PCSX2 Dev Team;
- [Mihawk-99](https://github.com/mihawk-99), for the Vulkan driver PS5SX2 runs on, in a version tweaked for it ([PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)), and the RADV port ([PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa));
- BlackBearReloaded (ps5-native-app-boilerplate);
- John Törnblom (the ps5-payload-dev SDK);
- xlenore/ps2-covers, the covers the app downloads at first run (not included here).
