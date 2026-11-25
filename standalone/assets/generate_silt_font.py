"""Rasterize the standalone interface's Open Font License glyph subset.

Usage: python generate_silt_font.py NotoSansCJKsc-Bold.otf
Requires Pillow with FreeType. The font file is only needed for regeneration.
Input: https://github.com/notofonts/noto-cjk/tree/main/Sans/OTF/SimplifiedChinese
License and attribution: NotoSansCJK-OFL.txt
"""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


SIZE = 14
EXTRAS = "、。·，！？：；（）《》【】「」『』…—＋－×／％Ｎ" \
         "饥饿芦苇盐沼泥淖砾滩藻藓漂流木斧镐燧礁碛榫桩汊兜晕笺"
OUTPUT = Path(__file__).with_name("silt_font_data.inc")


def characters(extras):
    letters = set(EXTRAS + extras + "\u3000")
    # GB2312 level one, the 3,755 commonly used simplified Chinese characters.
    for high in range(0xB0, 0xD8):
        for low in range(0xA1, 0xFF):
            try:
                letters.add(bytes((high, low)).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return sorted(letter for letter in letters if 0x80 <= ord(letter) <= 0xFFFF)


def raster(character, font):
    canvas = Image.new("L", (SIZE, SIZE))
    draw = ImageDraw.Draw(canvas)
    left, top, right, bottom = font.getbbox(character, anchor="lt")
    x = (SIZE - (right - left)) // 2 - left
    y = (SIZE - (bottom - top)) // 2 - top
    if character in "、。，":
        x, y = 1 - left, min(9, SIZE - (bottom - top)) - top
    draw.text((x, y), character, fill=255, font=font, anchor="lt")
    return [sum(1 << i for i in range(SIZE) if canvas.getpixel((i, row)) >= 115)
            for row in range(SIZE)]


def generate(path, output, extras):
    font = ImageFont.truetype(str(path), SIZE)
    glyphs = characters(extras)
    with output.open("w", encoding="utf-8", newline="\n") as out:
        out.write("/* Generated Siltwalk UI Bitmap. Font glyph data: OFL-1.1.\n"
                  " * Rasterized from Noto Sans CJK SC Bold.\n"
                  " * See NotoSansCJK-OFL.txt for copyright and license. */\n"
                  "static const struct SiltGlyph silt_glyphs[] = {\n")
        for char in glyphs:
            rows = ", ".join(f"0x{row:04X}" for row in raster(char, font))
            out.write(f"    {{0x{ord(char):04X}, {{{rows}}}}},\n")
        out.write("};\n")
    print(f"Generated {len(glyphs)} glyphs, {len(glyphs) * 30:,} bytes of flash data.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--extra", default="", help="additional characters to embed")
    args = parser.parse_args()
    generate(args.font, args.output, args.extra)
