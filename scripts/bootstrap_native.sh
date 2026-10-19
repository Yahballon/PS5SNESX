#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
base="$root/build/deps"
mkdir -p "$base"
cd "$base"
test -d PS5_Vulkan/.git || git clone -q https://github.com/mihawk-99/PS5_Vulkan.git
test -d PS5_PayloadSDK/.git || git clone -q https://github.com/mihawk-99/PS5_PayloadSDK.git
export PS5_PAYLOAD_SDK_FORK="$base/PS5_PayloadSDK"
vulkan="$base/PS5_Vulkan"
cd "$vulkan"
bash tools/setup-native-dependencies.sh
# PS5_Vulkan 'make' builds an unrelated demo app, NOT its driver archives.
# The prior build stopped on undefined symbol ps5_fp_ieee from that demo.
bash tools/rebuild-libc.sh
test -s runtime/libc.prx
# Fetch the PS5_Vulkan-documented OpenGL SDK v0.3.0 sources at a fixed commit.
# The v0.2.0 shader compiler was incompatible with PS5_Vulkan's
# fragment-input patch. The 0.3.0 sources contain the revised anchor.
# The published old SDK binary archive carries a layout without the
# third_party source tree, so it cannot feed PS5_Vulkan's shader compiler.
sdk_checkout="$base/ps5-opengl-sdk-0.3.0"
if [[ ! -d "$sdk_checkout/.git" ]]; then
  git clone -q --depth=1 --branch v0.3.0 \
    https://github.com/blackbearreloaded/ps5-opengl.git "$sdk_checkout"
fi
test "$(git -C "$sdk_checkout" rev-parse HEAD)" = 6cb291abea32281571c49705735046425cf000fd
# Pre-fetch the large Mesa archive with resumable HTTP retries. Upstream's
# Python urlretrieve() has no retry policy and may fail on connection resets.
read -r mesa_version mesa_url mesa_sha < <(python3 - "$sdk_checkout/dependencies.json" <<'PY'
import json,sys
x=json.load(open(sys.argv[1]))["mesa"]
print(x["version"],x["url"],x["sha256"])
PY
)
mesa_source="$sdk_checkout/third_party/mesa-$mesa_version.tar.xz"
mkdir -p "$(dirname "$mesa_source")"
if [[ ! -s "$mesa_source" ]] || ! printf '%s  %s\n' "$mesa_sha" "$mesa_source" | sha256sum -c --status; then
  rm -f "$mesa_source"
  urls=("$mesa_url" "https://archive.mesa3d.org/mesa-$mesa_version.tar.xz"
         "https://www.mesa3d.org/archive/mesa-$mesa_version.tar.xz")
  success=0
  for source in "${urls[@]}"; do
    echo "Trying pinned Mesa source: $source"
    if curl -fL --retry 6 --retry-all-errors --retry-delay 5 --connect-timeout 35 \
           --max-time 420 "$source" -o "$mesa_source.download" &&
       printf '%s  %s\n' "$mesa_sha" "$mesa_source.download" | sha256sum -c --status; then
      mv "$mesa_source.download" "$mesa_source"
      success=1
      break
    fi
    rm -f "$mesa_source.download"
  done
  [[ $success == 1 ]] || { echo "Pinned Mesa archive unavailable or hash mismatch" >&2; exit 1; }
fi
(cd "$sdk_checkout" && python3 tools/fetch-sources.py)
# A clean SDK checkout lacks Mesa-generated headers and C sources (notably
# util/format/u_format_gen.h). The upstream PS5_Vulkan assembler reproduces
# all the generator commands in the correct order; its output is checked
# against the exact SDK-pinned compiler tree before materializing the files.
bash "$vulkan/tooling/sdk/assemble-psbc-fork.sh" \
  "$sdk_checkout" "$sdk_checkout/third_party/opengnm-psbc"
test -s "$sdk_checkout/third_party/opengnm-psbc/src/util/format/u_format_gen.h"
test -s "$sdk_checkout/third_party/opengnm-psbc/src/util/format/u_format_pack.h"
export PS5_OPENGL_SDK="$sdk_checkout"
# The Vulkan helper needs an independent cache, but the archive has identical pin.
mkdir -p "$vulkan/.deps/native/mesa"
cp -f "$mesa_source" "$vulkan/.deps/native/mesa/mesa-$mesa_version.tar.xz"
test -d "$PS5_OPENGL_SDK/third_party/opengnm-psbc"
test -f "$PS5_OPENGL_SDK/tools/fetch-sources.py"
echo "PS5 shader compiler SDK source checkout: $PS5_OPENGL_SDK"
bash tools/fetch-mesa.sh
bash tools/build-psbc-ps5.sh
bash tools/build-vulkan-runtime.sh
# build-driver.sh signs two Vulkan modules using PS5_Vulkan's own native
# host-side ELF/FSELF converter. Unlike "make" (which builds a demo), build
# only this prerequisite using upstream's exact tools/build.sh compiler recipe.
native="$vulkan/tooling/native"
zlib_root="$vulkan/.deps/native/zlib/root"
zlib_archive=$(find "$zlib_root" -type f -name libz.a -print -quit)
test -s "$zlib_archive" || { echo "Missing pinned host zlib.a" >&2; exit 1; }
test -f "$native/native_app_builder.cpp"
mkdir -p "$vulkan/build/host"
host_tool="$vulkan/build/host/ps5-native-tool"
cxx=$(command -v clang++-18 || command -v clang++)
"$cxx" -std=c++20 -O2 -Wall -Wextra -Werror \
  -I "$zlib_root/usr/include" \
  "$native/native_app_builder.cpp" "$native/self_container.cpp" \
  "$native/elf_object.cpp" "$native/sce_module_writer.cpp" \
  "$zlib_archive" -o "$host_tool"
test -x "$host_tool" || { echo "Host PS5 SELF converter not built" >&2; exit 1; }
bash tools/build-driver.sh
for archive in \
  build/driver/ps5/libps5vk.ps5.a \
  .deps/native/vulkan-runtime/lib/libvk_runtime.ps5.a \
  build/driver/ps5/libpsbc_driver.ps5.a \
  .deps/native/psbc/lib/libpsbc_support.ps5.a; do
  test -s "$archive" || { echo "Missing native Vulkan archive $archive" >&2; exit 1; }
done
test -x .deps/native/ps5-payload-sdk/bin/prospero-clang
echo "PS5 Vulkan graphics archives ready for MesenCE title link"
