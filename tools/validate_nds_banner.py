#!/usr/bin/env python3
"""Check the built application banner, including its unchanged transparent icon."""
import argparse
import hashlib
from pathlib import Path
import struct


TITLE = "GBARunner3 Custom\nA GBA Hypervisor for DS\nGimoXagros"
ICON_SHA256 = "c73783e612cfcd5c94a4dff788ac966bfa3eeaae32e8ebb034ef08a059d3f33d"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def crc16(data):
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def validate(nds_path, icon_path):
    nds = nds_path.read_bytes()
    require(len(nds) >= 0x160, "Truncated NDS header")
    offset = struct.unpack_from("<I", nds, 0x68)[0]
    require(offset >= 0x160 and offset + 0x840 <= len(nds), "Invalid banner extent")
    banner = nds[offset:offset + 0x840]
    require(struct.unpack_from("<H", banner)[0] == 1, "Expected six-language DS banner")
    require(crc16(banner[0x20:]) == struct.unpack_from("<H", banner, 2)[0],
            "Banner CRC mismatch")
    for language in range(6):
        field = banner[0x240 + language * 0x100:0x340 + language * 0x100]
        expected = TITLE.encode("utf-16le").ljust(0x100, b"\0")
        require(field == expected, f"Unexpected title or padding in language {language}")

    bmp = icon_path.read_bytes()
    require(hashlib.sha256(bmp).hexdigest() == ICON_SHA256, "Original icon BMP changed")
    require(bmp[:2] == b"BM" and struct.unpack_from("<IiiHH", bmp, 14) == (40, 32, 32, 1, 4),
            "Expected 32x32 4-bit BMP")
    require(struct.unpack_from("<I", bmp, 30)[0] == 0, "Compressed BMP is unsupported")
    pixel_offset = struct.unpack_from("<I", bmp, 10)[0]
    pixels = []
    for y in range(32):
        row = bmp[pixel_offset + (31 - y) * 16:pixel_offset + (32 - y) * 16]
        pixels.append([value for byte in row for value in (byte >> 4, byte & 15)])
    require(all(len(row) == 32 for row in pixels), "Truncated BMP pixels")
    for y in range(32):
        for x in range(32):
            index = ((y // 8) * 4 + x // 8) * 64 + (y % 8) * 8 + x % 8
            byte = banner[0x20 + index // 2]
            actual = (byte >> (4 * (index % 2))) & 15
            require(actual == pixels[y][x], f"Icon pixel changed at {x},{y}")
    for index in range(16):
        blue, green, red, _ = bmp[54 + index * 4:58 + index * 4]
        expected = (red >> 3) | ((green >> 3) << 5) | ((blue >> 3) << 10)
        actual = struct.unpack_from("<H", banner, 0x220 + index * 2)[0]
        require(actual == expected, f"Icon palette entry {index} changed")
    # DS banner palette index 0 is transparent; preserve the source's background.
    border = pixels[0] + pixels[-1] + [row[0] for row in pixels] + [row[-1] for row in pixels]
    require(all(index == 0 for index in border), "Icon border is not transparent")
    transparent = sum(index == 0 for row in pixels for index in row)
    print(f"PASS: six exact English titles; CRC; unchanged icon/palette; {transparent} transparent pixels")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nds", type=Path)
    parser.add_argument("--icon", type=Path,
                        default=Path(__file__).resolve().parents[1] / "code/bootstrap/icon.bmp")
    args = parser.parse_args()
    try:
        validate(args.nds, args.icon)
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, f"FAIL: {error}\n")
