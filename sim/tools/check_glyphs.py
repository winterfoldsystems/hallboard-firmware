#!/usr/bin/env python3
"""Check that every character the firmware can draw has a glyph to draw it with.

A missing glyph is not a build error: LVGL draws a hollow box and the board ships with it. This
walks the other way round, from the strings in the firmware to the fonts, and fails when a code
point is in neither the built-in Montserrat bitmaps nor one of the custom `font:` entries in
ui.yaml.

Sources of text: string literals in hb_ui.h and hb_copy.h, and the quoted strings and `text:`
values in hallboard.yaml and ui.yaml. Sources of glyphs: the `glyphs:` lists in ui.yaml's `font:`
block, plus the built-in Montserrat coverage, which is read from the LVGL checkout under
firmware/sim/lvgl when it is there and falls back to the set LVGL has shipped for years.

Standard library only, so CI can run it straight after a checkout.

    python3 firmware/sim/tools/check_glyphs.py     # exit 1 on a missing glyph
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.dirname(HERE)
FIRMWARE = os.path.dirname(SIM)

# What LVGL's own lv_font_montserrat_*.c say they were generated from. Used when the LVGL
# checkout is not there to read the real thing out of.
FALLBACK_BUILTIN = set(range(0x20, 0x80)) | {0xB0, 0x2022} | {
    61441, 61448, 61451, 61452, 61453, 61457, 61459, 61461, 61465, 61468, 61473, 61478, 61479,
    61480, 61502, 61507, 61512, 61515, 61516, 61517, 61521, 61522, 61523, 61524, 61543, 61544,
    61550, 61552, 61553, 61556, 61559, 61560, 61561, 61563, 61587, 61589, 61636, 61637, 61639,
    61641, 61664, 61671, 61674, 61683, 61724, 61732, 61787, 61931, 62016, 62017, 62018, 62019,
    62020, 62087, 62099, 62189, 62212, 62810, 63426, 63650,
}

PRINTABLE_ASCII = set(range(0x20, 0x7F))


def read(path):
    with open(path, "rb") as f:
        return f.read()


# ---------------------------------------------------------------- escapes
C_ESCAPES = {b"n": b"\n", b"t": b"\t", b"r": b"\r", b"0": b"\0", b"\\": b"\\",
             b'"': b'"', b"'": b"'", b"a": b"\a", b"b": b"\b", b"f": b"\f", b"v": b"\v"}


def decode_c_bytes(raw):
    """Turn the bytes between two quotes in C or YAML source into the bytes they stand for."""
    out = bytearray()
    i = 0
    while i < len(raw):
        c = raw[i:i + 1]
        if c != b"\\":
            out += c
            i += 1
            continue
        nxt = raw[i + 1:i + 2]
        if nxt == b"x":
            m = re.match(rb"[0-9a-fA-F]{1,2}", raw[i + 2:])
            if m:
                out.append(int(m.group(0), 16))
                i += 2 + len(m.group(0))
                continue
        if nxt in (b"u", b"U"):
            width = 4 if nxt == b"u" else 8
            m = re.match(rb"[0-9a-fA-F]{%d}" % width, raw[i + 2:])
            if m:
                out += chr(int(m.group(0), 16)).encode("utf-8")
                i += 2 + width
                continue
        if nxt in C_ESCAPES:
            out += C_ESCAPES[nxt]
            i += 2
            continue
        out += nxt
        i += 2
    return bytes(out)


def code_points(raw, where, problems):
    """Code points in a decoded byte string, complaining about anything that is not UTF-8."""
    try:
        return {ord(ch) for ch in raw.decode("utf-8")}
    except UnicodeDecodeError:
        problems.append("%s: a string literal is not valid UTF-8" % where)
        return {ord(ch) for ch in raw.decode("utf-8", "ignore")}


# ---------------------------------------------------------------- the fonts
def strip_c_comments(src):
    src = re.sub(rb"/\*.*?\*/", b" ", src, flags=re.S)
    return re.sub(rb"//[^\n]*", b" ", src)


def strip_yaml_comment(item):
    """Drop a trailing `#` comment, leaving one that is inside a quoted glyph list alone."""
    out = bytearray()
    in_quote = False
    i = 0
    while i < len(item):
        c = item[i:i + 1]
        if in_quote and c == b"\\":
            out += item[i:i + 2]
            i += 2
            continue
        if c == b'"':
            in_quote = not in_quote
        elif c == b"#" and not in_quote:
            break
        out += c
        i += 1
    return bytes(out).strip()


def ui_yaml_font_glyphs(path):
    """The glyph lists in ui.yaml's `font:` block, one set per font id.

    A hand parser rather than PyYAML, which is not in the standard library. It understands the
    two shapes the block uses: an inline `glyphs: ["..."]` and a block list, either of which may
    carry a `&name` anchor, plus `glyphs: *name` for a font that reuses an earlier list. An
    anchored set is shared by reference, so items added after the anchor line are in it too.
    """
    lines = read(path).split(b"\n")
    fonts = {}
    anchors = {}
    in_font = False
    font_id = None
    in_glyphs = False

    def add(quoted_in):
        for quoted in re.findall(rb'"((?:[^"\\]|\\.)*)"', quoted_in):
            fonts[font_id] |= code_points(decode_c_bytes(quoted), path, [])

    for line in lines:
        if re.match(rb"^font:\s*$", line):
            in_font = True
            continue
        if not in_font:
            continue
        # A new top-level key ends the block.
        if line.strip() and not line.startswith((b" ", b"\t", b"#")):
            break
        stripped = line.strip()
        m = re.match(rb"^-?\s*id:\s*(\S+)", stripped)
        if m:
            font_id = m.group(1).decode()
            fonts.setdefault(font_id, set())
            in_glyphs = False
            continue
        if stripped.startswith(b"- file:") or re.match(rb"^file:", stripped):
            in_glyphs = False
            continue
        m = re.match(rb"^glyphs:\s*(.*)$", stripped)
        if m:
            in_glyphs = True
            rest = strip_yaml_comment(m.group(1))
            anchor = re.match(rb"^&(\S+)\s*(.*)$", rest)
            if anchor:
                # The set object itself is the anchor, so the items below land in it as well.
                anchors[anchor.group(1).decode()] = fonts[font_id]
                rest = anchor.group(2).strip()
            alias = re.match(rb"^\*(\S+)$", rest)
            if alias:
                fonts[font_id] |= anchors.get(alias.group(1).decode(), set())
                in_glyphs = False
                continue
            if rest.startswith(b"["):                      # inline list on one line
                add(rest)
                in_glyphs = False
            continue
        if in_glyphs:
            if not stripped.startswith(b"-"):
                in_glyphs = False
                continue
            add(strip_yaml_comment(stripped[1:]))
    return fonts


def builtin_montserrat():
    """The built-in coverage, read from the ranges LVGL's font generator recorded in the file."""
    path = os.path.join(SIM, "lvgl", "src", "font", "lv_font_montserrat_16.c")
    if not os.path.exists(path):
        return FALLBACK_BUILTIN, "the recorded fallback set (no LVGL checkout)"
    head = read(path)[:4096].decode("utf-8", "ignore")
    out = set()
    for spec in re.findall(r"-r\s+([0-9a-fA-FxX,\-]+)", head):
        for part in spec.split(","):
            if not part:
                continue
            m = re.match(r"^(0[xX][0-9a-fA-F]+|\d+)-(0[xX][0-9a-fA-F]+|\d+)$", part)
            if m:
                out |= set(range(int(m.group(1), 0), int(m.group(2), 0) + 1))
            elif re.match(r"^(0[xX][0-9a-fA-F]+|\d+)$", part):
                out.add(int(part, 0))
    if not out:
        return FALLBACK_BUILTIN, "the recorded fallback set (unreadable font header)"
    return out, "lv_font_montserrat_16.c"


def lv_symbols():
    """LV_SYMBOL_* to its code points, for the symbol names the firmware uses by macro."""
    path = os.path.join(SIM, "lvgl", "src", "font", "lv_symbol_def.h")
    if not os.path.exists(path):
        return {}
    out = {}
    for name, value in re.findall(rb'#define\s+(LV_SYMBOL_[A-Z0-9_]+)\s+"((?:[^"\\]|\\.)*)"',
                                  read(path)):
        out[name.decode()] = code_points(decode_c_bytes(value), path, [])
    return out


# ---------------------------------------------------------------- the text
def literals_from_c(path, problems):
    src = strip_c_comments(read(path))
    used = set()
    for quoted in re.findall(rb'"((?:[^"\\\n]|\\.)*)"', src):
        used |= code_points(decode_c_bytes(quoted), path, problems)
    return used, set(m.decode() for m in re.findall(rb"LV_SYMBOL_[A-Z0-9_]+", src))


def literals_from_yaml(path, problems):
    src = read(path)
    used = set()
    for quoted in re.findall(rb'"((?:[^"\\\n]|\\.)*)"', src):
        used |= code_points(decode_c_bytes(quoted), path, problems)
    for quoted in re.findall(rb"'([^'\n]*)'", src):
        used |= code_points(quoted, path, problems)
    # Unquoted `text:` scalars, which are what an ESPHome widget draws.
    for value in re.findall(rb"^\s*text:\s*([^\"'\n#][^\n#]*)$", src, re.M):
        used |= code_points(value.strip(), path, problems)
    return used, set(m.decode() for m in re.findall(rb"LV_SYMBOL_[A-Z0-9_]+", src))


def main():
    problems = []
    ui_yaml = os.path.join(FIRMWARE, "ui.yaml")
    fonts = ui_yaml_font_glyphs(ui_yaml)
    custom = set()
    for glyphs in fonts.values():
        custom |= glyphs
    builtin, builtin_source = builtin_montserrat()
    symbols = lv_symbols()

    used = {}     # code point -> the files it was seen in
    used_symbols = {}
    for name in ("hb_ui.h", "hb_copy.h", "hallboard.yaml", "ui.yaml"):
        path = os.path.join(FIRMWARE, name)
        if not os.path.exists(path):
            continue
        if name.endswith(".h"):
            points, syms = literals_from_c(path, problems)
        else:
            points, syms = literals_from_yaml(path, problems)
        for cp in points:
            used.setdefault(cp, set()).add(name)
        for sym in syms:
            used_symbols.setdefault(sym, set()).add(name)

    # Control characters are never drawn: a newline is a line break to LVGL and the rest are
    # sentinels (hb_ui.h seeds the shown pairing code with "\x01" so the first set_code draws).
    have = PRINTABLE_ASCII | custom | builtin
    missing = sorted(cp for cp in used if cp not in have and cp >= 0x20 and cp != 0x7F)
    for cp in missing:
        problems.append("U+%04X %s is in %s and in no font"
                        % (cp, repr(chr(cp)), ", ".join(sorted(used[cp]))))

    for sym in sorted(used_symbols):
        if sym not in symbols:
            continue     # no LVGL checkout to resolve it against, or a name LVGL dropped
        for cp in sorted(symbols[sym] - have):
            problems.append("U+%04X, used as %s in %s, is in no font"
                            % (cp, sym, ", ".join(sorted(used_symbols[sym]))))

    print("custom fonts in ui.yaml: %s" % ", ".join(sorted(fonts)) or "none")
    print("custom glyphs: %d, built-in coverage from %s: %d"
          % (len(custom), builtin_source, len(builtin)))
    print("code points used by the firmware: %d, of which non-ASCII: %d"
          % (len(used), len([cp for cp in used if cp not in PRINTABLE_ASCII])))
    if problems:
        print("")
        for line in problems:
            print("MISSING: %s" % line)
        return 1
    print("every character the firmware draws has a glyph.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
