#!/usr/bin/env python3
"""Generate small, visually distinct TGA files for AROS MultiView QA."""
from pathlib import Path
import sys

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/samples')
out.mkdir(parents=True, exist_ok=True)
width = height = 64
colors = ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255))

def head(kind, depth, descriptor, palette=False):
    data = bytearray(18)
    data[1] = int(palette)
    data[2] = kind
    if palette:
        data[5] = 4
        data[7] = 24
    data[12:14] = width.to_bytes(2, 'little')
    data[14:16] = height.to_bytes(2, 'little')
    data[16] = depth
    data[17] = descriptor
    return data

def quadrant(x, y):
    return colors[(y >= 32) * 2 + (x >= 32)]

def bgr(rgb):
    r, g, b = rgb
    return bytes((b, g, r))

# TGA's default origin is at bottom left.
raw = head(2, 24, 0)
for y in reversed(range(height)):
    for x in range(width):
        raw += bgr(quadrant(x, y))
(out / 'quadrants-24-bottom.tga').write_bytes(raw)

# Top-right origin, RLE, one packet per half-row.
rle = head(10, 32, 0x38)
for y in range(height):
    for x in (48, 16):
        red, green, blue = quadrant(x, y)
        rle += bytes((0x9f, blue, green, red, 255))
(out / 'quadrants-32-rle-right.tga').write_bytes(rle)

alpha = head(2, 32, 0x28)
for y in range(height):
    for x in range(width):
        red, green, blue = quadrant(x, y)
        alpha += bytes((blue, green, red, x * 4))
(out / 'quadrants-32-alpha.tga').write_bytes(alpha)

indexed = head(1, 8, 0x20, palette=True)
for color in colors:
    indexed += bgr(color)
for y in range(height):
    for x in range(width):
        indexed.append((y >= 32) * 2 + (x >= 32))
(out / 'quadrants-8-palette.tga').write_bytes(indexed)

(out / 'truncated.tga').write_bytes(raw[:21])
