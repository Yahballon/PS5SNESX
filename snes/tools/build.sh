#!/usr/bin/env bash
# (AI-assisted) Build the separate PS5SNES app using existing, public PS5 dependencies.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
: "${PS5SNES_DEPS:?Set PS5SNES_DEPS to your prepared public PS5 dependency directory}"
export PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:-$PS5SNES_DEPS/PS5_Vulkan/.deps/native/ps5-payload-sdk}
export LIBNFS_PREFIX=${LIBNFS_PREFIX:-$PS5SNES_DEPS/libnfs-install}
export LIBZ_PREFIX=${LIBZ_PREFIX:-$PS5SNES_DEPS/filesystem-helper/.deps/pacbrew/v0.40.2/sysroot/user/homebrew}
python3 "$here/build_shelf.py"
