#!/usr/bin/env python3
"""Compile the Porpoise PS5 texture UV workaround to native SPIR-V.

The intact upstream vertex shader and Porpoise's glass/grid logic remain
unchanged. Only image/font/cover sampling uses the stable v_local varying
plus per-quad flat UV bounds. This avoids a PS5 driver interpolation issue
that renders texture atlases as vertical stripes.
"""
from pathlib import Path
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = root / "shaders/ui.frag"
target = root / "src/ui_frag.inc"
code = source.read_text()
assert code.count("texture(tex, mix(v_p2.xy, v_p2.zw, v_local))") == 3
assert "texture(tex, v_uv)" not in code
with tempfile.TemporaryDirectory() as d:
    output = Path(d) / "ui.frag.spv"
    subprocess.run([
        "glslangValidator", "-V", "--target-env", "vulkan1.1",
        "-S", "frag", "-o", str(output), str(source)
    ], check=True)
    data = output.read_bytes()
    assert len(data) % 4 == 0 and data[:4] == b"\x03\x02\x23\x07"
    words = struct.unpack("<%dI" % (len(data)//4), data)
    lines = [
        "// Generated from original Porpoise shader with PS5NESX UV fix",
        "#pragma once",
        "const uint32_t porpoise_ui_frag[] = {",
    ]
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join(f"0x{word:08x}" for word in words[i:i+8]) + ",")
    target.write_text("\n".join(lines + ["};", ""]))
    print(f"Porpoise UV-fixed shader: {len(words)} SPIR-V words => {target}")
