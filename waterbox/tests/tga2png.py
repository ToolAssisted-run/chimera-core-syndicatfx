#!/usr/bin/env python3
"""Converts the harness's 32-bit top-down BGRA .tga to .png (no PIL needed).

usage: tga2png.py <in.tga> <out.png> [scale]
"""
import struct
import sys
import zlib


def main():
    src, dst = sys.argv[1], sys.argv[2]
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    data = open(src, "rb").read()
    w, h = struct.unpack("<HH", data[12:16])
    px = data[18:18 + w * h * 4]
    rows = []
    for y in range(h):
        line = bytearray()
        for x in range(w):
            b, g, r, _ = px[(y * w + x) * 4:(y * w + x) * 4 + 4]
            line += bytes((r, g, b)) * scale
        rows.extend([b"\0" + bytes(line)] * scale)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    open(dst, "wb").write(png)


if __name__ == "__main__":
    main()
