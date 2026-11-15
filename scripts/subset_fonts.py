#!/usr/bin/env python3
"""Subsets the reading faces for the firmware image.

assets/fonts-src/ holds the complete fonts as distributed (SIL OFL 1.1);
assets/fonts/ holds what is embedded in the firmware: the same outlines and
metrics, restricted to the scripts the reader is used with and without TrueType
hinting (stb_truetype ignores it). Pages render pixel-identical; the image
shrinks by about 2 MB.

    python3 scripts/subset_fonts.py

Needs fontTools (pip install fonttools).
"""

from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "fonts-src"
DST = ROOT / "assets" / "fonts"

RANGES = [
    (0x0020, 0x007E),  # Basic Latin
    (0x00A0, 0x00FF),  # Latin-1 Supplement
    (0x0100, 0x024F),  # Latin Extended-A and -B
    (0x02B0, 0x02FF),  # spacing modifier letters
    (0x0300, 0x036F),  # combining diacritics
    (0x0370, 0x03FF),  # Greek (formulas, the odd quotation)
    (0x0400, 0x045F),  # basic Cyrillic (names)
    (0x1E00, 0x1EFF),  # Latin Extended Additional
    (0x2000, 0x206F),  # general punctuation: dashes, quotes, ellipsis, spaces
    (0x2070, 0x209F),  # superscripts and subscripts
    (0x20A0, 0x20BF),  # currency
    (0x2100, 0x214F),  # letterlike
    (0x2150, 0x218F),  # number forms
    (0x2190, 0x21FF),  # arrows
    (0x2200, 0x22FF),  # mathematical operators
    (0x2300, 0x23FF),  # technical
    (0x2460, 0x24FF),  # enclosed alphanumerics
    (0x2500, 0x259F),  # box drawing, blocks
    (0x25A0, 0x25FF),  # geometric shapes
    (0x2600, 0x26FF),  # miscellaneous symbols
    (0x2700, 0x27BF),  # dingbats
    (0xFB00, 0xFB06),  # Latin ligatures (layout substitutes fi, fl, ffi...)
    (0xFFFC, 0xFFFD),  # replacement characters
]


def main() -> None:
    unicodes = [cp for lo, hi in RANGES for cp in range(lo, hi + 1)]
    total_in = total_out = 0
    for src in sorted(SRC.glob("*.[ot]tf")):
        options = subset.Options()
        # What the engine reads: pair kerning and mark positioning (GPOS). The
        # ligatures it applies are the precomposed U+FB00..FB06 codepoints.
        options.layout_features = ["kern", "mark", "mkmk", "ccmp", "liga", "locl"]
        options.hinting = False
        options.notdef_outline = True
        options.name_IDs = [1, 2, 4, 6]
        options.drop_tables += ["DSIG", "meta", "STAT", "avar", "fvar", "gvar", "HVAR", "MVAR"]
        font = TTFont(src)
        subsetter = subset.Subsetter(options)
        subsetter.populate(unicodes=unicodes)
        subsetter.subset(font)
        dst = DST / src.name
        font.save(dst)
        total_in += src.stat().st_size
        total_out += dst.stat().st_size
        print(f"{src.name:28s} {src.stat().st_size:8d} -> {dst.stat().st_size:8d}  {len(font.getGlyphOrder())} glyphs")
    print(f"{'total':28s} {total_in:8d} -> {total_out:8d}")


if __name__ == "__main__":
    main()
