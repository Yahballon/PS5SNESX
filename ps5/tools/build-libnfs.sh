#!/usr/bin/env bash
# (AI-assisted) Build upstream libnfs 5.0.3 with a supplied PS5 SDK CMake toolchain.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
source_dir=${1:?usage: build-libnfs.sh LIBNFS_SOURCE OUTPUT_DIR PS5_TOOLCHAIN_FILE}
output_dir=${2:?provide an out-of-tree output directory}
toolchain=${3:?provide the PS5 SDK CMake toolchain file}
source_dir=$(cd -- "$source_dir" && pwd)
mkdir -p "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd)
[[ "$output_dir" != "$source_dir" ]] || { echo "Use an out-of-tree output directory" >&2; exit 1; }
[[ -f "$toolchain" ]] || { echo "Toolchain file not found: $toolchain" >&2; exit 1; }
toolchain=$(cd -- "$(dirname -- "$toolchain")" && pwd)/$(basename -- "$toolchain")
cmake -S "$source_dir" -B "$output_dir/build" \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_C_FLAGS=-DHAVE_SYS_UIO_H=1 \
  -DCMAKE_INSTALL_PREFIX="$output_dir/install" \
  -DBUILD_SHARED_LIBS=OFF -DENABLE_TESTS=OFF -DENABLE_UTILS=OFF \
  -DENABLE_EXAMPLES=OFF -DENABLE_DOCUMENTATION=OFF
cmake --build "$output_dir/build" --parallel
cmake --install "$output_dir/build"
echo "LIBNFS_PREFIX=$output_dir/install"
