#!/usr/bin/env python3
"""night_palette.py

What this is for
-----------------
firmware/hb_tokens.h defines the DAY palette as sRGB hex values, one per design
token. The night palette keeps each colour's OKLCH lightness (L) and hue (h)
but halves its chroma (C), per the design's "hues desaturate by half at
night" rule. This script parses the DAY block, does the sRGB -> OKLCH ->
halve chroma -> sRGB round trip for every token, and prints a NIGHT block in
the same layout and comment style as DAY.

How to run
----------
    python3 firmware/sim/tools/night_palette.py
    python3 firmware/sim/tools/night_palette.py --table

The output is not wired into the build. A person reads the printed NIGHT
block (and, with --table, the markdown table of the per-token OKLCH values)
and commits it into hb_tokens.h by hand, so the firmware never depends on
this script or on Python being available at build time.

Everything below is Python 3 standard library only: no third-party colour
or math packages.
"""

import argparse
import math
import re
import sys
from pathlib import Path

# The OKLab matrices (Bjoern Ottosson). M1 takes linear sRGB to an LMS-like
# space; M2 takes the cube root of that (LMS') to OKLab. The inverses are
# computed numerically below, not hard-coded, so nothing here depends on
# someone else's rounding.
M1 = [
    [0.4122214708, 0.5363325363, 0.0514459929],
    [0.2119034982, 0.6806995451, 0.1073969566],
    [0.0883024619, 0.2817188376, 0.6299787005],
]

M2 = [
    [0.2104542553, 0.7936177850, -0.0040720468],
    [1.9779984951, -2.4285922050, 0.4505937099],
    [0.0259040371, 0.7827717662, -0.8086757660],
]


def invert3x3(m):
    """Numeric inverse of a 3x3 matrix via Gauss-Jordan elimination."""
    n = 3
    aug = [list(m[i]) + [1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]

    for col in range(n):
        pivot_row = max(range(col, n), key=lambda r: abs(aug[r][col]))
        if abs(aug[pivot_row][col]) < 1e-15:
            raise ValueError("matrix is singular")
        aug[col], aug[pivot_row] = aug[pivot_row], aug[col]

        pivot = aug[col][col]
        aug[col] = [x / pivot for x in aug[col]]

        for r in range(n):
            if r == col:
                continue
            factor = aug[r][col]
            if factor != 0.0:
                aug[r] = [aug[r][j] - factor * aug[col][j] for j in range(2 * n)]

    return [row[n:] for row in aug]


M1_INV = invert3x3(M1)
M2_INV = invert3x3(M2)


def mat_vec(m, v):
    return [sum(m[r][c] * v[c] for c in range(3)) for r in range(3)]


def cbrt(x):
    """Sign-preserving cube root (Python's ** chokes on negative bases)."""
    return math.copysign(abs(x) ** (1.0 / 3.0), x) if x != 0.0 else 0.0


def clamp01(x):
    return max(0.0, min(1.0, x))


def srgb_to_linear(c):
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


def linear_to_srgb(c):
    if c <= 0.0031308:
        return c * 12.92
    return 1.055 * (c ** (1.0 / 2.4)) - 0.055


def hex_to_rgb01(hexval):
    r = (hexval >> 16) & 0xFF
    g = (hexval >> 8) & 0xFF
    b = hexval & 0xFF
    return r / 255.0, g / 255.0, b / 255.0


def rgb01_to_hex(r, g, b):
    r8 = round(clamp01(r) * 255.0)
    g8 = round(clamp01(g) * 255.0)
    b8 = round(clamp01(b) * 255.0)
    return (r8 << 16) | (g8 << 8) | b8


def srgb_hex_to_oklch(hexval):
    r, g, b = hex_to_rgb01(hexval)
    rl, gl, bl = srgb_to_linear(r), srgb_to_linear(g), srgb_to_linear(b)

    lms = mat_vec(M1, [rl, gl, bl])
    lms_prime = [cbrt(x) for x in lms]
    lab = mat_vec(M2, lms_prime)

    L, a, b_ = lab
    c = math.hypot(a, b_)
    h = math.degrees(math.atan2(b_, a))
    if h < 0.0:
        h += 360.0
    return L, c, h


def oklch_to_srgb_hex(L, c, h):
    hr = math.radians(h)
    a = c * math.cos(hr)
    b_ = c * math.sin(hr)

    lms_prime = mat_vec(M2_INV, [L, a, b_])
    lms = [x ** 3 for x in lms_prime]
    rl, gl, bl = mat_vec(M1_INV, lms)

    r = linear_to_srgb(rl)
    g = linear_to_srgb(gl)
    b = linear_to_srgb(bl)
    return rgb01_to_hex(r, g, b)


DAY_BLOCK_RE = re.compile(r"Palette\s+DAY\s*=\s*\{\{(.*?)\}\}\s*;", re.S)
ENTRY_RE = re.compile(r"0x([0-9A-Fa-f]{6})\s*,\s*//\s*(T_\w+)")


def parse_day_palette(tokens_path):
    text = tokens_path.read_text()
    block_match = DAY_BLOCK_RE.search(text)
    if not block_match:
        raise ValueError(f"could not find the DAY palette block in {tokens_path}")

    entries = ENTRY_RE.findall(block_match.group(1))
    if not entries:
        raise ValueError(f"found the DAY block but no 0xRRGGBB, // T_NAME lines in it")

    return [(name, int(hex_str, 16)) for hex_str, name in entries]


def main():
    parser = argparse.ArgumentParser(description="Derive the NIGHT palette from DAY (half chroma, same L and h).")
    parser.add_argument(
        "--tokens",
        type=Path,
        default=Path(__file__).resolve().parent.parent.parent / "hb_tokens.h",
        help="path to hb_tokens.h (default: ../../hb_tokens.h relative to this script)",
    )
    parser.add_argument(
        "--table",
        action="store_true",
        help="also print a markdown table of name, day hex, night hex, and OKLCH L/C/h",
    )
    args = parser.parse_args()

    day = parse_day_palette(args.tokens)

    rows = []
    for name, day_hex in day:
        L, C, h = srgb_hex_to_oklch(day_hex)
        night_hex = oklch_to_srgb_hex(L, C / 2.0, h)
        rows.append((name, day_hex, night_hex, L, C, h))

    print("inline constexpr Palette NIGHT = {{")
    for name, _day_hex, night_hex, _L, _C, _h in rows:
        print(f"    0x{night_hex:06X},  // {name}")
    print("}};")

    if args.table:
        print()
        print("| Name | Day hex | Night hex | L | C | h |")
        print("| --- | --- | --- | --- | --- | --- |")
        for name, day_hex, night_hex, L, C, h in rows:
            print(f"| {name} | #{day_hex:06X} | #{night_hex:06X} | {L:.3f} | {C:.3f} | {h:.1f} |")

    return 0


if __name__ == "__main__":
    sys.exit(main())
