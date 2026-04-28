#!/usr/bin/env python3
"""Resize a PNG and emit it as an RGB565 C header for TFT_eSPI.pushImage().

Usage: png_to_rgb565.py <input.png> <output.h> <w> <h> <symbol>
"""
import sys
from PIL import Image

def main():
    if len(sys.argv) != 6:
        print(__doc__); sys.exit(1)
    src, out, w, h, sym = sys.argv[1:]
    w = int(w); h = int(h)
    im = Image.open(src).convert("RGB")
    im.thumbnail((w, h), Image.LANCZOS)
    # pad to exact size with black
    canvas = Image.new("RGB", (w, h), (0, 0, 0))
    ox = (w - im.size[0]) // 2
    oy = (h - im.size[1]) // 2
    canvas.paste(im, (ox, oy))

    data = []
    for y in range(h):
        for x in range(w):
            r, g, b = canvas.getpixel((x, y))
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data.append(v)

    with open(out, "w") as f:
        f.write(f"// Auto-generated from {src} by png_to_rgb565.py. Do not edit.\n")
        f.write("#pragma once\n\n#include <stdint.h>\n\n")
        f.write(f"constexpr int {sym}_W = {w};\n")
        f.write(f"constexpr int {sym}_H = {h};\n")
        f.write(f"constexpr uint16_t {sym}[{w*h}] PROGMEM = {{\n")
        for i in range(0, len(data), 12):
            chunk = data[i:i+12]
            f.write("    " + ", ".join(f"0x{v:04X}" for v in chunk) + ",\n")
        f.write("};\n")
    print(f"wrote {out}: {w}x{h} = {w*h*2} bytes")

if __name__ == "__main__":
    main()
