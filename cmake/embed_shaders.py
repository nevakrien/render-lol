"""Embed SPIR-V so the executable needs no runtime shader paths (also on Android)."""
import pathlib
import struct
import sys

output = pathlib.Path(sys.argv[1])
output.parent.mkdir(parents=True, exist_ok=True)
lines = ["#pragma once", "#include <cstdint>", "namespace shaders {"]
for filename in sys.argv[2:]:
    path = pathlib.Path(filename)
    data = path.read_bytes()
    words = struct.unpack(f"<{len(data) // 4}I", data)
    name = path.name.removesuffix(".spv").replace(".", "_")
    lines.append(f"inline constexpr uint32_t {name}[] = {{")
    lines.append(",".join(hex(word) for word in words))
    lines.append("};")
lines.append("}")
output.write_text("\n".join(lines) + "\n")
