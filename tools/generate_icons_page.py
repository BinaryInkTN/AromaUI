#!/usr/bin/env python3
"""Regenerate the docs Material Icons browser.

Reads the AROMA_ICON_* codepoints from include/aroma_material_icons.h,
verifies each one exists in the embedded icon font
(include/aroma_material_font.h), writes a subset WOFF2 for the docs
site, and writes the docs/icons.html fragment (searchable grid) that
docs-config.json points at.

Usage:
    python3 tools/generate_icons_page.py
"""

import html as _htmlesc
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
HEADER = os.path.join(ROOT, "include", "aroma_material_icons.h")
FONT_HEADER = os.path.join(ROOT, "include", "aroma_material_font.h")
HTML_OUT = os.path.join(ROOT, "docs", "icons.html")
FONT_OUT = os.path.join(ROOT, "docs", "material-icons.woff2")


def load_defines(path):
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    defs = re.findall(r'#define\s+(AROMA_ICON_\w+)\s+"\\u([0-9a-fA-F]{4})"', src)
    seen = set()
    out = []
    for name, code in defs:
        if name in seen:
            continue
        seen.add(name)
        out.append((name, int(code, 16)))
    return out


def load_font_bytes(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()
    m = re.search(r"static const unsigned char icon_ttf\[\] = \{(.*?)\};", src, re.S)
    if not m:
        raise RuntimeError("icon_ttf array not found in %s" % path)
    return bytes(int(h, 16) for h in re.findall(r"0[xX]([0-9a-fA-F]{1,2})", m.group(1)))


def subset_font(ttf_bytes, unicodes, out_path):
    from fontTools import subset

    with open("/tmp/aroma_icons_full.ttf", "wb") as f:
        f.write(ttf_bytes)
    opts = subset.Options()
    opts.flavor = "woff2"
    font = subset.load_font("/tmp/aroma_icons_full.ttf", opts)
    ss = subset.Subsetter(opts)
    ss.populate(unicodes=set(unicodes))
    ss.subset(font)
    font.save(out_path)


def build_fragment(icons):
    tiles = []
    for const, code in icons:
        short = const[len("AROMA_ICON_"):].lower()
        blob = "%s %s %04x" % (short.replace("_", " "), const.lower(), code)
        tiles.append(
            '<div class="icon-tile" data-name="%s" onclick="copyIconName(\'%s\',this)"'
            ' title="%s (U+%04X) — click to copy">'
            '<span class="icon-glyph">&#x%x;</span>'
            '<span class="icon-name">%s</span>'
            '<span class="icon-code">U+%04X</span>'
            "</div>"
            % (
                _htmlesc.escape(blob, quote=True),
                const,
                const,
                code,
                code,
                _htmlesc.escape(short),
                code,
            )
        )
    return """<p>Type to filter {n} embedded icons by name. Click any tile to copy its <code>AROMA_ICON_*</code> constant. Glyphs render with the same font bytes shipped in <code>aroma_material_font.h</code>, so what you see is what the device draws.</p>
<p>Use an icon in Incense with <code>IconButton {{ icon: "AROMA_ICON_HOME" }}</code> or <code>Icon {{ text: "AROMA_ICON_HOME" }}</code>, or in C with the <code>AROMA_ICON_*</code> macros from <code>aroma_material_icons.h</code>.</p>
<div class="icon-search-wrap">
  <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="11" cy="11" r="8"/><path d="m21 21-4.35-4.35"/></svg>
  <input class="icon-search" type="text" placeholder="Search icons, e.g. wifi, arrow, battery"
         autocomplete="off" oninput="filterIcons(this.value)" aria-label="Search icons">
</div>
<div class="icon-count" id="iconCount"></div>
<div class="icon-grid" id="iconGrid">
{tiles}
</div>
""".format(
        n=len(icons), tiles="\n".join(tiles)
    )


def main():
    from fontTools.ttLib import TTFont

    defs = load_defines(HEADER)
    print("defines: %d" % len(defs))
    ttf = load_font_bytes(FONT_HEADER)
    with open("/tmp/aroma_icons_full.ttf", "wb") as f:
        f.write(ttf)
    cmap = TTFont("/tmp/aroma_icons_full.ttf").getBestCmap()
    icons = [(n, c) for n, c in defs if c in cmap]
    skipped = [n for n, c in defs if c not in cmap]
    for n in skipped:
        print("  SKIP (not in embedded font): %s" % n)
    print("icons in font: %d" % len(icons))
    subset_font(ttf, [c for _, c in icons], FONT_OUT)
    print("wrote %s" % FONT_OUT)
    with open(HTML_OUT, "w", encoding="utf-8") as f:
        f.write(build_fragment(icons))
    print("wrote %s" % HTML_OUT)


if __name__ == "__main__":
    sys.exit(main())
