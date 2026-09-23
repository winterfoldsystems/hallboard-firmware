#!/usr/bin/env python3
"""icons.py

What this is for
-----------------
Rasterises the Meteocons SVGs vendored into firmware/icons/meteocons/ (see that directory's
README for source, licence and the HallBoard-name-to-file mapping) into firmware/hb_icons.h: one
LVGL A8 (alpha-only) image per icon per size the board draws, 28 px for the weather face's three
cards and 24 px for the hourly strip.

The Meteocons line set draws each icon in several stroke colours (amber suns, blue raindrops,
grey clouds), and HallBoard never wants those colours: every icon is recoloured on screen by a
design token (T_CHALK on a card, T_CHALK70 in the hourly strip), the same way the night palette
recolours everything else. So this script first flattens every real fill/stroke colour in the SVG
to solid black (flatten() below) -- `fill="none"` is left alone -- and rasterises that. The result
is an RGBA image whose alpha channel is the shape's true pixel coverage, independent of what
colour the original icon happened to be: exactly the A8 mask LVGL wants, and exactly what "flatten
to one alpha channel, luminance-independent" means (this is not a greyscale/luminance conversion,
which would have made amber and pale grey strokes read as different opacities).

Every Meteocons icon carries a lot of empty air inside its own 64x64 viewBox (room for the
animated wobble/sway/drift these are built for), which is exactly what a still 24-28 px board icon
cannot spare: drawn at the viewBox's own scale, the ink itself is a small blob in the middle of a
mostly-transparent square. So before scaling to a target size, ink_bbox() below measures where the
flattened icon's ink actually falls (in SVG viewBox units, off a single fixed-resolution reference
render, the same for every target size an icon is drawn at), square_crop() grows that box to a
square around its own centre, and rasterize() renders exactly that square, scaled so the ink fills
the target size less MARGIN_PX on every side, then pastes it into the middle of a fully transparent
canvas of the target size. Both stages are pure functions of the flattened SVG text and a fixed
reference resolution, so the result is as reproducible as the unmodified-viewBox render this
replaces.

Rendering is supersampled (SUPERSAMPLE x each target size, box-resampled down with Pillow's
Lanczos filter) since a 64-viewBox icon's 2-3 px strokes get thin fast at 24 px, and a like-for-
like render at the final size aliases badly.

Needs a local SVG rasteriser: cairosvg (pure Python wheel, pulls in a system Cairo, which is
already on this machine via Homebrew's `cairo`) and Pillow. Both live in firmware/sim/.venv/,
gitignored, not a build dependency of the device or the sim binary -- see the sim README.

How to run
----------
    firmware/sim/.venv/bin/python3 firmware/sim/tools/icons.py

This writes firmware/hb_icons.h. It is a generated file: never edit it by hand; add or rename an
icon in ICONS below (and its SVG in firmware/icons/meteocons/) and run this again.
"""

import io
import re
import sys
from pathlib import Path

try:
    import cairosvg
except ImportError:
    print(
        "error: needs cairosvg and Pillow. From firmware/sim/:\n"
        "    python3 -m venv .venv && .venv/bin/pip install cairosvg Pillow\n"
        "then run this script with .venv/bin/python3. See the sim README's icon section.",
        file=sys.stderr,
    )
    sys.exit(1)
from PIL import Image

HERE = Path(__file__).resolve().parent
FIRMWARE = HERE.parent.parent
ICON_DIR = FIRMWARE / "icons" / "meteocons"
OUT = FIRMWARE / "hb_icons.h"

# The board only ever draws icons at these two sizes: 28 px on a weather card, 24 px in the
# hourly strip.
SIZES = (28, 24)
SUPERSAMPLE = 4
# A fixed resolution to measure each icon's ink at, independent of either target size, so both
# sizes crop to the same square. 8x the 64-unit viewBox is more than enough precision.
REF_PX = 512
# Left around the ink on every side at the target size, once the crop is scaled down to it.
MARGIN_PX = 1

# HallBoard icon name (docs/screen-document.md's `i`, plus the two card-3 icons hb_ui.h picks
# itself) to the Meteocons file it draws. See firmware/icons/meteocons/README.md for why each one
# was picked, in particular `pour`, which has no dedicated heavy-rain icon to reach for.
ICONS = [
    ("sun", "clear-day"),
    ("night", "clear-night"),
    ("partly", "partly-cloudy-day"),
    ("cloud", "overcast"),
    ("rain", "rain"),
    ("pour", "raindrops"),
    ("snow", "snow"),
    ("fog", "fog"),
    ("storm", "thunderstorms"),
    ("wind", "wind"),
    ("raindrop", "raindrop"),
    ("umbrella", "umbrella"),
]

COLOR_ATTR_RE = re.compile(r'(fill|stroke)="#[0-9a-fA-F]{3,6}"')
# `wind.svg` draws its two lines dashed and animates stroke-dashoffset to sweep the dashes along,
# so a static render at the SVG's own dashoffset (0) shows only the fraction of each line the
# dash pattern happens to paint there, which reads as a broken scribble rather than a wind icon.
# Dropping the attribute is what every other renderer does once it ignores the animation too: the
# line comes back solid, which is what a still 32/24 px icon wants regardless.
DASH_ATTR_RE = re.compile(r'\s*stroke-dasharray="[^"]*"')


def flatten(svg_text):
    """Every real fill/stroke colour becomes solid black; `fill="none"` (there is no `stroke="none"`
    in this set) is left alone. Rasterising that gives an RGBA image whose alpha channel is the
    shape's coverage, not a reading of the original colour. Also drops stroke-dasharray (see
    DASH_ATTR_RE): a static render has no business with an animated dash phase."""
    text = COLOR_ATTR_RE.sub(lambda m: f'{m.group(1)}="#000000"', svg_text)
    return DASH_ATTR_RE.sub("", text)


VIEWBOX_RE = re.compile(r'viewBox="[^"]*"')


def ink_bbox(flat_svg_text):
    """Where a flattened icon's ink actually falls, in the SVG's own 0-64 viewBox units, measured
    off one fixed-resolution (REF_PX) render so both target sizes crop to the same box. Alpha-only
    coverage, the same read rasterize() ends up wanting: a pixel is "ink" if it has any coverage at
    all, which is what getbbox() on the alpha channel gives for free. An icon with literally no ink
    (should not happen for anything in ICONS) falls back to the whole viewBox rather than crashing."""
    png_bytes = cairosvg.svg2png(bytestring=flat_svg_text.encode("utf-8"), output_width=REF_PX, output_height=REF_PX)
    alpha = Image.open(io.BytesIO(png_bytes)).convert("RGBA").split()[-1]
    box = alpha.getbbox()
    if box is None:
        return 0.0, 0.0, 64.0, 64.0
    scale = 64.0 / REF_PX
    x0, y0, x1, y1 = box
    return x0 * scale, y0 * scale, x1 * scale, y1 * scale


def square_crop(bbox):
    """Grows an SVG-unit bounding box to a square around its own centre: (left, top, side)."""
    x0, y0, x1, y1 = bbox
    side = max(x1 - x0, y1 - y0)
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    return cx - side / 2.0, cy - side / 2.0, side


def with_viewbox(svg_text, left, top, side):
    return VIEWBOX_RE.sub(f'viewBox="{left:.4f} {top:.4f} {side:.4f} {side:.4f}"', svg_text, count=1)


def rasterize(svg_path, size):
    flat = flatten(svg_path.read_text())
    left, top, side = square_crop(ink_bbox(flat))
    cropped = with_viewbox(flat, left, top, side)
    # The ink fills everything but MARGIN_PX on each side; the crop above is exactly the ink's own
    # square, so scaling it straight to that inner size is what makes it fill that inner size.
    content_px = size - 2 * MARGIN_PX
    hi = content_px * SUPERSAMPLE
    png_bytes = cairosvg.svg2png(bytestring=cropped.encode("utf-8"), output_width=hi, output_height=hi)
    content = Image.open(io.BytesIO(png_bytes)).convert("RGBA").resize((content_px, content_px), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(content, (MARGIN_PX, MARGIN_PX), content)
    return canvas.split()[-1].tobytes()  # the alpha channel, row-major, one byte per pixel


def c_name(icon_name, size):
    return f"ICON_{icon_name}_{size}"


def render_array(name, data):
    lines = [f"inline constexpr uint8_t {name}_data[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(str(b) for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def render_header():
    return [
        "// Generated by firmware/sim/tools/icons.py. Do not edit by hand; the source SVGs are",
        "// firmware/icons/meteocons/ (see that directory's README for licence and provenance).",
        "// Regenerate with:",
        "//     firmware/sim/.venv/bin/python3 firmware/sim/tools/icons.py",
        "//",
        "// Every icon is an LVGL A8 (alpha-only) image, one entry per icon per size (28 px for a",
        "// weather card, 24 px for the hourly strip), cropped to its own ink and rescaled so the ink",
        "// fills the size: the Meteocons line icons are drawn in several stroke colours and a lot of",
        "// empty viewBox air, which icons.py flattens to a single coverage mask and crops away, so the",
        "// device recolours every one with a design token through the ordinary lv_image style",
        "// mechanism (g_img in hb_tokens.h), and the night palette repaints them for free, the same as",
        "// text and fills.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <cstring>",
        "",
        "#include <lvgl.h>",
        "",
        "namespace hb {",
        "",
    ]


def main():
    entries = []  # (icon_name, size, data)
    for icon_name, stem in ICONS:
        svg_path = ICON_DIR / f"{stem}.svg"
        if not svg_path.exists():
            print(f"error: {svg_path} is missing", file=sys.stderr)
            return 1
        for size in SIZES:
            entries.append((icon_name, size, rasterize(svg_path, size)))

    lines = render_header()
    for icon_name, size, data in entries:
        name = c_name(icon_name, size)
        lines.append(render_array(name, data))
        lines.append(f"inline constexpr lv_image_header_t {name}_hdr = {{")
        lines.append(f"    LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_A8, 0, {size}, {size}, {size}, 0,")
        lines.append("};")
        lines.append(f"inline constexpr lv_image_dsc_t {name} = {{")
        lines.append(f"    {name}_hdr, sizeof({name}_data), {name}_data, nullptr, nullptr,")
        lines.append("};")
        lines.append("")

    size_list = " or ".join(str(s) for s in SIZES)
    lines.append("// `name` (one of docs/screen-document.md's weather icon names, or the two card-3 names")
    lines.append(f"// hb_ui.h picks itself, \"raindrop\" and \"umbrella\") and `size` ({size_list}) to the")
    lines.append("// descriptor to draw; nullptr when hb draws nothing under that name, or at that size.")
    lines.append("inline const lv_image_dsc_t *icon(const char *name, int size) {")
    lines.append("  if (name == nullptr || name[0] == '\\0') return nullptr;")
    first = True
    for icon_name, _stem in ICONS:
        kw = "if" if first else "else if"
        lines.append(f'  {kw} (strcmp(name, "{icon_name}") == 0) {{')
        for size in SIZES:
            lines.append(f"    if (size == {size}) return &{c_name(icon_name, size)};")
        lines.append("    return nullptr;")
        lines.append("  }")
        first = False
    lines.append("  return nullptr;")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace hb")
    lines.append("")

    OUT.write_text("\n".join(lines))
    total = sum(len(d) for _, _, d in entries)
    print(f"wrote {OUT}: {len(entries)} icon/size pairs, {total} bytes of mask data")
    return 0


if __name__ == "__main__":
    sys.exit(main())
