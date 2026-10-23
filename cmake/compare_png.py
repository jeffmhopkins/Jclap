"""Compares two 8-bit RGB/RGBA PNGs with a tolerance: compare_png.py actual.png expected.png

Fails if the sizes differ, or if more than MAX_FRACTION of the pixels differ by more than
THRESHOLD in any channel (small differences come from GPU drivers and font rasterizers). No
dependencies beyond the standard library, so CI needs nothing extra.
"""

import struct
import sys
import zlib

THRESHOLD = 48
MAX_FRACTION = 0.005


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, header = 8, b"", None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    width, height, depth, color, _, _, interlace = header
    if depth != 8 or color not in (2, 6) or interlace:
        raise ValueError(f"{path}: only 8-bit RGB/RGBA non-interlaced PNGs are supported")
    bpp = 3 if color == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * bpp
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(height):
        kind = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                line[i] = (line[i] + a) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + b) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        rows.append(bytes(line))
        prev = line
    # Drop alpha so RGB and RGBA files compare.
    pixels = []
    for line in rows:
        pixels.extend(line[i:i + 3] for i in range(0, stride, bpp))
    return width, height, pixels


def main():
    actual, expected = sys.argv[1], sys.argv[2]
    w1, h1, p1 = read_png(actual)
    w2, h2, p2 = read_png(expected)
    if (w1, h1) != (w2, h2):
        print(f"size {w1}x{h1}, expected {w2}x{h2}")
        return 1
    bad = sum(1 for a, b in zip(p1, p2) if max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])) > THRESHOLD)
    fraction = bad / len(p1)
    print(f"{bad} of {len(p1)} pixels differ ({fraction:.3%}, limit {MAX_FRACTION:.1%})")
    if fraction > MAX_FRACTION:
        print(f"If the change is intended, copy {actual} over {expected}.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
