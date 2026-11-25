"""Generate the Siltwalk texture pack, using only Python's standard library."""

import math
from pathlib import Path
import random
import struct
import zipfile
import zlib


TRANSPARENT = (0, 0, 0, 0)
INK = (24, 37, 45, 255)
BRASS = (227, 172, 98, 255)
SURF = (129, 192, 171, 255)
GLYPH = (239, 245, 225, 255)
TILE = 16


class Surface:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.rgba = bytearray(width * height * 4)

    def pixel(self, x, y, color):
        if 0 <= x < self.width and 0 <= y < self.height:
            index = (y * self.width + x) * 4
            self.rgba[index:index + 4] = bytes(color)

    def rect(self, x, y, width, height, color):
        left = max(x, 0)
        right = min(x + width, self.width)
        if left >= right:
            return
        scanline = bytes(color) * (right - left)
        for row in range(max(y, 0), min(y + height, self.height)):
            index = (row * self.width + left) * 4
            self.rgba[index:index + len(scanline)] = scanline

    def line(self, x0, y0, x1, y1, color):
        dx = abs(x1 - x0)
        dy = -abs(y1 - y0)
        stepx = 1 if x0 < x1 else -1
        stepy = 1 if y0 < y1 else -1
        error = dx + dy
        while True:
            self.pixel(x0, y0, color)
            if (x0, y0) == (x1, y1):
                break
            advance = 2 * error
            if advance >= dy:
                error += dy
                x0 += stepx
            if advance <= dx:
                error += dx
                y0 += stepy

    def paste(self, image, x, y):
        if x < 0 or y < 0 or x + image.width > self.width or y + image.height > self.height:
            raise ValueError("image region falls outside the atlas")
        width = image.width * 4
        for row in range(image.height):
            index = ((y + row) * self.width + x) * 4
            self.rgba[index:index + width] = image.rgba[row * width:(row + 1) * width]

    def to_png(self):
        def chunk(tag, payload):
            return (struct.pack(">I", len(payload)) + tag + payload +
                    struct.pack(">I", zlib.crc32(tag + payload)))

        width = self.width * 4
        scanlines = []
        previous = bytes(width)
        for offset in range(0, len(self.rgba), width):
            row = self.rgba[offset:offset + width]
            sub = bytes((value - (row[i - 4] if i >= 4 else 0)) & 255
                        for i, value in enumerate(row))
            up = bytes((a - b) & 255 for a, b in zip(row, previous))
            candidates = (row, sub, up)
            best = min(range(3), key=lambda n: sum(min(b, 256 - b) for b in candidates[n]))
            scanlines.append(bytes((best,)) + candidates[best])
            previous = row
        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 6, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                chunk(b"IDAT", zlib.compress(b"".join(scanlines), level=9)) + chunk(b"IEND", b""))


def tone(color, shade=0, alpha=255):
    return tuple(max(0, min(x + shade, 255)) for x in color[:3]) + (alpha,)


def grain(x, y, seed, spread=5):
    code = (x * 374761393 + y * 668265263 + seed * 1274126177) & 0xFFFFFFFF
    code = ((code ^ (code >> 13)) * 3266489917) & 0xFFFFFFFF
    return ((code ^ (code >> 16)) % (2 * spread + 1)) - spread


def mineral(seed, base, formation="strata"):
    canvas = Surface(TILE, TILE)
    rng = random.Random(seed + 34819)
    if formation == "rubble":
        stones = [(rng.randrange(16), rng.randrange(16), rng.randrange(-16, 17))
                  for _ in range(12)]
    for y in range(TILE):
        for x in range(TILE):
            if formation == "rubble":
                nearest, second = sorted(
                    (min(abs(x - a), 16 - abs(x - a)) ** 2 +
                     min(abs(y - b), 16 - abs(y - b)) ** 2, tint)
                    for a, b, tint in stones)[:2]
                shade = nearest[1] - (17 if second[0] - nearest[0] < 5 else 0)
            else:
                band = (y + (2 * x + 2 * math.sin(x * math.tau / 16)) / 4) % 4
                shade = -12 if band < 0.8 else 6 if band < 1.6 else 0
            canvas.pixel(x, y, tone(base, shade + grain(x, y, seed)))
    return canvas


def meadow(seed, base, side=False):
    canvas = mineral(seed, (131, 101, 86), "rubble") if side else Surface(TILE, TILE)
    for y in range(TILE):
        for x in range(TILE):
            if side and y >= 4 + (x ^ (x >> 1)) % 3:
                continue
            tide = 7 if ((x + y * 2) % 9 < 3) else -2
            canvas.pixel(x, y, tone(base, tide + grain(x, y, seed)))
    rng = random.Random(seed + 8821)
    for _ in range(16 if not side else 8):
        x = rng.randrange(TILE)
        y = rng.randrange(4 if side else TILE)
        hue = tone(base, 28 if (x ^ y) & 1 else -21)
        canvas.line(x, y, x + (2 if x & 1 else -1), y - 1, hue)
        if x % 7 == 0:
            canvas.pixel(x, y - 2, tone((194, 172, 112), grain(x, y, seed)))
    return canvas


def braid(seed, base, panel=False):
    canvas = Surface(TILE, TILE)
    for y in range(TILE):
        for x in range(TILE):
            group = (x + (y // 8 if panel else y // 6)) % 4
            shade = (-19, 4, 10, -3)[group] + grain(x, y, seed, 4)
            if panel and y % 8 == 0:
                shade -= 16
            canvas.pixel(x, y, tone(base, shade))
    if panel:
        for x in (2, 12):
            for y in (3, 12):
                canvas.pixel(x, y, tone((231, 194, 119), -18))
    return canvas


def bark_end(seed):
    canvas = Surface(TILE, TILE)
    for y in range(TILE):
        for x in range(TILE):
            dx, dy = abs(2 * x - 15), abs(2 * y - 15)
            edge = max(dx, dy) + min(dx, dy) // 2
            base = (175, 131, 82) if edge < 16 else (105, 86, 76)
            shade = (edge // 2 % 3) * 9 - 12 + grain(x, y, seed, 3)
            canvas.pixel(x, y, tone(base, shade))
    return canvas


def ceramic(seed, base, metal=False):
    canvas = mineral(seed, base)
    light = tone(base, 28)
    edge = tone(base, -28)
    canvas.line(0, 0, 15, 0, light)
    canvas.line(0, 15, 15, 15, edge)
    canvas.line(0, 0, 0, 15, light)
    canvas.line(15, 0, 15, 15, edge)
    if metal:
        for x in (2, 13):
            for y in (2, 13):
                canvas.pixel(x, y, GLYPH)
                canvas.pixel(x + 1, y + 1, INK)
    else:
        canvas.line(3, 8, 8, 3, light)
        canvas.line(8, 3, 13, 8, light)
        canvas.line(13, 8, 8, 13, edge)
        canvas.line(8, 13, 3, 8, edge)
    return canvas


def crystal(seed, inset):
    canvas = mineral(seed, (89, 95, 104))
    rng = random.Random(seed + 91)
    for _ in range(6):
        x, y = rng.randrange(2, 14), rng.randrange(2, 14)
        canvas.line(x - 1, y + 2, x + 1, y - 2, tone(inset, -25))
        canvas.line(x, y + 1, x + 2, y, tone(inset, 16))
        canvas.pixel(x + 1, y - 1, tone(inset, 32))
    return canvas


def sprig(seed, petal, sprawl=False):
    canvas = Surface(TILE, TILE)
    rng = random.Random(seed + 391)
    for i in range(5 if sprawl else 3):
        x = 3 + i * (2 if sprawl else 4)
        height = rng.randrange(4, 10)
        tip = x + rng.randrange(-2, 3)
        canvas.line(x + 1, 14, tip, height, (72, 133, 113, 255))
        canvas.line(x + 1, 13, x - 1, 10, (98, 167, 130, 255))
        canvas.line(tip - 2, height + 1, tip + 1, height - 1, tone(petal, -18))
        canvas.line(tip - 1, height - 1, tip + 2, height + 1, tone(petal, 12))
        canvas.pixel(tip, height, tone(petal, 34))
    return canvas


def flow(seed, phase, hot=False):
    canvas = Surface(TILE, TILE)
    for y in range(TILE):
        for x in range(TILE):
            u = y + 1.1 * math.sin((x + 2 * phase) * math.tau / TILE)
            ripple = ((u + (0 if hot else phase)) % (5 if hot else 7))
            shade = 32 if ripple < 1.1 else -7 if ripple > 3.5 else 5
            base = (198, 94, 66) if hot else (50, 123, 128)
            canvas.pixel(x, y, tone(base, shade + grain(x, y, seed, 3), 255 if hot else 212))
    return canvas


def ember(phase):
    canvas = Surface(TILE, TILE)
    for y in range(3, 16):
        width = (y - 1) // 3
        center = 7 + math.floor(math.sin((y + 2 * phase) * 0.59) * 1.9)
        for x in range(center - width, center + width + 1):
            outer = abs(x - center) + (y < 7)
            canvas.pixel(x, y, tone((234, 166, 91) if outer < 2 else (178, 77, 66),
                                    grain(x, y, phase), 234))
    return canvas


def glaze(seed, base):
    canvas = Surface(TILE, TILE)
    canvas.rect(0, 0, 16, 16, tone(base, -27, 68))
    tint = tone(base, 29, 163)
    for x, y, dx, dy in ((0, 0, 1, 1), (15, 0, -1, 1), (0, 15, 1, -1), (15, 15, -1, -1)):
        canvas.line(x, y, x + 4 * dx, y, tint)
        canvas.line(x, y, x, y + 4 * dy, tint)
    x = 3 + seed % 4
    canvas.line(x, 11, x + 7, 4, tone(base, 43, 93))
    return canvas


def foliage(seed):
    canvas = meadow(seed, (61, 130, 116))
    for y in range(TILE):
        for x in range(TILE):
            diagonal = (x + y * 3) % 11
            if diagonal in (3, 4) and grain(x, y, seed, 7) < 0:
                canvas.pixel(x, y, TRANSPARENT)
            elif diagonal < 3:
                canvas.pixel(x, y, tone((104, 157, 118), grain(x, y, seed)))
    return canvas


FABRIC = [
    (155, 91, 89), (192, 119, 78), (205, 164, 96), (155, 165, 93),
    (98, 141, 105), (74, 129, 117), (102, 163, 150), (73, 140, 152),
    (83, 118, 157), (100, 101, 141), (119, 92, 140), (160, 96, 137),
    (173, 121, 128), (47, 55, 70), (115, 127, 135), (207, 213, 190),
    (199, 147, 158), (60, 105, 92), (119, 91, 74), (53, 78, 115), (74, 155, 145),
]


def terrain_tile(index):
    if index == 0:
        return meadow(index, (83, 147, 115))
    if index == 3:
        return meadow(index, (75, 135, 108), side=True)
    if index == 22:
        return foliage(index)
    if index in (12, 13, 15, 28, 29):
        colors = {12: (180, 108, 154), 13: (226, 185, 95), 15: (137, 190, 126),
                  28: (222, 122, 91), 29: (172, 154, 111)}
        return sprig(index, colors[index], sprawl=(index == 15))
    if index == 11:
        canvas = Surface(TILE, TILE)
        canvas.line(6, 0, 6, 15, tone((146, 117, 86)))
        canvas.line(10, 0, 10, 15, tone((183, 146, 108)))
        for y in range(1, 16, 3):
            canvas.line(6, y, 10, y - 1, (220, 175, 116, 255))
        return canvas
    if index in (14, 30, 86):
        return flow(index, 0, hot=(index != 14))
    if index == 38:
        return ember(0)
    if index in (49, 51):
        return glaze(index, (151, 199, 183) if index == 49 else (137, 176, 198))
    if index in (32, 33, 34):
        return crystal(index, {32: (215, 170, 84), 33: (166, 139, 113), 34: (58, 71, 83)}[index])
    if index in (4, 8, 9, 10, 20, 35, 53):
        colors = {4: (174, 140, 94), 8: (154, 121, 90), 9: (181, 144, 98),
                  10: (129, 102, 74), 20: (115, 89, 72), 35: (145, 114, 91),
                  53: (159, 134, 98)}
        return braid(index, colors[index], panel=(index != 20))
    if index == 21:
        return bark_end(index)
    if index == 54:
        canvas = ceramic(index, (121, 96, 83), metal=True)
        canvas.rect(4, 4, 8, 8, (166, 104, 58, 255))
        canvas.rect(5, 3, 6, 10, (193, 126, 62, 255))
        canvas.rect(6, 4, 4, 8, (240, 191, 104, 255))
        canvas.line(4, 12, 11, 12, BRASS)
        canvas.line(5, 3, 10, 3, BRASS)
        canvas.line(7, 6, 7, 9, GLYPH)
        return canvas
    if index in (7, 23, 24, 39, 40, 52, 55, 56):
        colors = {7: (136, 74, 83), 23: (128, 155, 150), 24: (189, 145, 82),
                  39: (111, 135, 131), 40: (164, 116, 65), 52: (120, 112, 127),
                  55: (110, 128, 124), 56: (179, 134, 83)}
        return ceramic(index, colors[index], metal=(index in (23, 24, 39, 40, 55, 56)))
    if 64 <= index < 64 + len(FABRIC):
        return braid(index, FABRIC[index - 64])

    stones = {
        1: (109, 112, 122), 2: (138, 109, 93), 5: (172, 157, 129),
        6: (191, 171, 137), 16: (102, 111, 124), 17: (53, 61, 71),
        18: (212, 178, 125), 19: (150, 133, 121), 25: (198, 156, 109),
        26: (163, 171, 162), 36: (87, 123, 107), 37: (69, 64, 88),
        41: (179, 139, 96), 42: (139, 155, 147), 48: (151, 157, 83),
        50: (212, 222, 209), 57: (172, 144, 115), 58: (146, 155, 159),
    }
    base = stones.get(index, (68 + index % 4 * 16, 70 + index % 5 * 10, 87 + index % 3 * 12))
    return mineral(index, base, "rubble" if index in (2, 16, 17, 19, 36, 48) else "strata")


# Readable survey lettering drawn on a 5-by-7 grid. Dots are transparent.
LETTERS = {
    "A": [14, 17, 17, 31, 17, 17, 17], "B": [30, 17, 17, 30, 17, 17, 30],
    "C": [15, 16, 16, 16, 16, 16, 15], "D": [30, 17, 17, 17, 17, 17, 30],
    "E": [31, 16, 16, 30, 16, 16, 31], "F": [31, 16, 16, 30, 16, 16, 16],
    "G": [15, 16, 16, 23, 17, 17, 15], "H": [17, 17, 17, 31, 17, 17, 17],
    "I": [14, 4, 4, 4, 4, 4, 14], "J": [7, 2, 2, 2, 2, 18, 12],
    "K": [17, 18, 20, 24, 20, 18, 17], "L": [16, 16, 16, 16, 16, 16, 31],
    "M": [17, 27, 21, 21, 17, 17, 17], "N": [17, 25, 21, 19, 17, 17, 17],
    "O": [14, 17, 17, 17, 17, 17, 14], "P": [30, 17, 17, 30, 16, 16, 16],
    "Q": [14, 17, 17, 17, 21, 18, 13], "R": [30, 17, 17, 30, 20, 18, 17],
    "S": [15, 16, 16, 14, 1, 1, 30], "T": [31, 4, 4, 4, 4, 4, 4],
    "U": [17, 17, 17, 17, 17, 17, 14], "V": [17, 17, 17, 17, 10, 10, 4],
    "W": [17, 17, 17, 21, 21, 27, 17], "X": [17, 17, 10, 4, 10, 17, 17],
    "Y": [17, 17, 10, 4, 4, 4, 4], "Z": [31, 1, 2, 4, 8, 16, 31],
    "0": [14, 17, 19, 21, 25, 17, 14], "1": [4, 12, 4, 4, 4, 4, 14],
    "2": [14, 17, 1, 6, 8, 16, 31], "3": [30, 1, 1, 14, 1, 1, 30],
    "4": [2, 6, 10, 18, 31, 2, 2], "5": [31, 16, 30, 1, 1, 17, 14],
    "6": [7, 8, 16, 30, 17, 17, 14], "7": [31, 1, 2, 4, 8, 8, 8],
    "8": [14, 17, 17, 14, 17, 17, 14], "9": [14, 17, 17, 15, 1, 2, 28],
    "!": [4, 4, 4, 4, 4, 0, 4], "?": [14, 17, 1, 6, 4, 0, 4],
    ".": [0, 0, 0, 0, 0, 0, 4], ",": [0, 0, 0, 0, 0, 4, 8],
    ":": [0, 0, 4, 0, 4, 0, 0], ";": [0, 0, 4, 0, 4, 4, 8],
    "-": [0, 0, 0, 14, 0, 0, 0], "_": [0, 0, 0, 0, 0, 0, 31],
    "+": [0, 4, 4, 31, 4, 4, 0], "=": [0, 0, 31, 0, 31, 0, 0],
    "/": [1, 1, 2, 4, 8, 16, 16], "\\": [16, 16, 8, 4, 2, 1, 1],
    "(": [2, 4, 8, 8, 8, 4, 2], ")": [8, 4, 2, 2, 2, 4, 8],
    "[": [14, 8, 8, 8, 8, 8, 14], "]": [14, 2, 2, 2, 2, 2, 14],
    "<": [1, 2, 4, 8, 4, 2, 1], ">": [16, 8, 4, 2, 4, 8, 16],
    "'": [4, 4, 0, 0, 0, 0, 0], "\"": [10, 10, 0, 0, 0, 0, 0],
    "%": [17, 18, 2, 4, 8, 9, 17], "#": [10, 10, 31, 10, 31, 10, 10],
    "*": [0, 21, 14, 31, 14, 21, 0], "&": [12, 18, 20, 8, 21, 18, 13],
    "@": [14, 17, 21, 23, 22, 16, 15], "|": [4, 4, 4, 4, 4, 4, 4],
    "$": [4, 15, 20, 14, 5, 30, 4], "^": [4, 10, 17, 0, 0, 0, 0],
    "~": [0, 0, 0, 9, 22, 0, 0], "\x60": [8, 4, 0, 0, 0, 0, 0],
    "{": [3, 4, 4, 8, 4, 4, 3], "}": [24, 4, 4, 2, 4, 4, 24],
    " ": [0, 0, 0, 0, 0, 0, 0],
}


def font_atlas():
    canvas = Surface(128, 128)
    for code in range(32, 256):
        char = bytes((code,)).decode("cp437")
        glyph = LETTERS.get(char.upper(), LETTERS["?"])
        x, y = (code % 16) * 8, (code // 16) * 8
        for row, bits in enumerate(glyph):
            for bit in range(5):
                if bits & (1 << (4 - bit)):
                    canvas.pixel(x + bit, y + row, GLYPH)
    return canvas


def frame(canvas, x, y, width, height, bg, edge, accent=None):
    canvas.rect(x + 3, y, width - 6, height, bg)
    canvas.rect(x, y + 3, width, height - 6, bg)
    canvas.rect(x + 1, y + 1, width - 2, height - 2, bg)
    canvas.line(x + 3, y, x + width - 4, y, edge)
    canvas.line(x, y + 3, x, y + height - 4, edge)
    canvas.line(x + 3, y + height - 1, x + width - 4, y + height - 1, edge)
    canvas.line(x + width - 1, y + 3, x + width - 1, y + height - 4, edge)
    for xx, yy, sx, sy in ((x, y, 1, 1), (x + width - 1, y, -1, 1),
                           (x, y + height - 1, 1, -1), (x + width - 1, y + height - 1, -1, -1)):
        canvas.line(xx + 3 * sx, yy, xx, yy + 3 * sy, edge)
    if accent:
        canvas.rect(x + 4, y + 4, 2, height - 8, accent)
        canvas.rect(x + width - 6, y + 4, 2, height - 8, accent)


def gui_atlas():
    canvas = Surface(256, 256)
    for slot in range(9):
        x = slot * 20 + 1
        frame(canvas, x, 2, 20, 18, (29, 51, 57, 230), (82, 132, 129, 255))
        canvas.rect(x + 8, 18, 4, 2, SURF)
    for x, sx in ((2, 1), (21, -1)):
        for y, sy in ((24, 1), (41, -1)):
            canvas.line(x, y + 5 * sy, x, y + 2 * sy, BRASS)
            canvas.line(x, y + 2 * sy, x + 2 * sx, y, BRASS)
            canvas.line(x + 2 * sx, y, x + 5 * sx, y, BRASS)
    for y, bg, edge, accent in (
            (46, (37, 48, 51, 255), (72, 91, 90, 255), None),
            (66, (31, 62, 68, 255), (109, 169, 158, 255), SURF),
            (86, (85, 74, 54, 255), (230, 176, 99, 255), BRASS)):
        frame(canvas, 0, y, 200, 20, bg, edge, accent)
    return canvas


def sights():
    canvas = Surface(256, 256)
    canvas.line(5, 3, 7, 1, INK)
    canvas.line(7, 1, 9, 3, INK)
    canvas.line(1, 7, 3, 5, INK)
    canvas.line(1, 7, 3, 9, INK)
    canvas.line(13, 7, 11, 5, INK)
    canvas.line(13, 7, 11, 9, INK)
    canvas.line(5, 11, 7, 13, INK)
    canvas.line(7, 13, 9, 11, INK)
    for x, y in ((7, 2), (2, 7), (12, 7), (7, 12), (7, 7)):
        canvas.pixel(x, y, BRASS)
    return canvas


def survey_suit():
    canvas = Surface(64, 32)
    # Survey automaton in blue-green enamel with a metallic visor and utility panel.
    # Texture coordinates match ClassiCube's generic cuboid UVs.
    canvas.paste(ceramic(201, (93, 139, 139), metal=True), 0, 0)
    canvas.paste(ceramic(202, (93, 139, 139), metal=True), 16, 0)
    canvas.paste(braid(205, (72, 99, 102)), 0, 16)
    canvas.paste(ceramic(206, (185, 129, 82), metal=True), 16, 16)
    canvas.paste(braid(207, (177, 133, 90), panel=True), 32, 16)
    canvas.paste(braid(208, (177, 133, 90), panel=True), 48, 16)
    canvas.rect(8, 9, 8, 4, (43, 67, 77, 255))
    canvas.rect(8, 10, 8, 1, SURF)
    canvas.line(9, 11, 14, 9, GLYPH)
    canvas.rect(22, 23, 6, 5, INK)
    canvas.rect(23, 24, 4, 1, BRASS)
    return canvas


def touch_atlas():
    canvas = Surface(256, 256)
    # Utility-style bevels let the native board overlay pick a control glyph.
    for i in range(16):
        x, y = (i % 4) * 64 + 12, (i // 4) * 64 + 12
        frame(canvas, x, y, 40, 40, (26, 52, 59, 166), SURF)
        if i % 4 == 0:
            canvas.line(x + 10, y + 20, x + 29, y + 20, BRASS)
        elif i % 4 == 1:
            canvas.line(x + 20, y + 10, x + 20, y + 29, BRASS)
        elif i % 4 == 2:
            canvas.line(x + 11, y + 28, x + 28, y + 11, BRASS)
        else:
            canvas.line(x + 11, y + 11, x + 28, y + 28, BRASS)
    return canvas


def weather(kind):
    canvas = Surface(64, 64)
    rng = random.Random(399 + len(kind))
    if kind == "clouds":
        for y in range(64):
            for x in range(64):
                a = (math.sin(x * math.tau / 64) + math.cos(y * math.tau / 32) +
                     math.sin((x - y) * math.tau / 32))
                if a > 0.8:
                    canvas.pixel(x, y, tone((187, 203, 193), (int)(a * 7), min(220, int(a * 61))))
    elif kind == "rain":
        for _ in range(30):
            x, y = rng.randrange(64), rng.randrange(64)
            canvas.line(x, y, x - 1, y + rng.randrange(3, 9), (151, 189, 177, 121))
    else:
        for _ in range(38):
            x, y = rng.randrange(64), rng.randrange(64)
            canvas.pixel(x, y, GLYPH)
            if (x ^ y) & 1:
                canvas.pixel(x - 1, y + 1, (171, 198, 188, 132))
    return canvas


def particle_atlas():
    canvas = Surface(128, 128)
    hue = (161, 195, 172)
    for i in range(64):
        x0, y0 = (i % 8) * 16, (i // 8) * 16
        # Irregular mote in every engine slot with a brighter core.
        for x, y in ((2, 14), (3, 14), (3, 15), (4, 15), (5, 14)):
            canvas.pixel(x0 + x, y0 + y, tone(hue, grain(x, y, i, 21), 178 + (i % 3) * 21))
    return canvas


def make_pack(path):
    terrain = Surface(256, 256)
    for index in range(256):
        terrain.paste(terrain_tile(index), index % 16 * TILE, index // 16 * TILE)
    animation = Surface(64, 64)
    for phase in range(4):
        animation.paste(flow(14, phase), phase * TILE, 0)
        animation.paste(flow(30, phase, hot=True), phase * TILE, TILE)
        animation.paste(ember(phase), phase * TILE, TILE * 2)
    terrain_png = terrain.to_png()
    gui = gui_atlas().to_png()
    textures = {
        "terrain.png": terrain_png,
        "default.png": font_atlas().to_png(),
        "gui.png": gui,
        "gui_classic.png": gui,
        "icons.png": sights().to_png(),
        "touch.png": touch_atlas().to_png(),
        "char.png": survey_suit().to_png(),
        "clouds.png": weather("clouds").to_png(),
        "rain.png": weather("rain").to_png(),
        "snow.png": weather("snow").to_png(),
        "particles.png": particle_atlas().to_png(),
        "animations.png": animation.to_png(),
        "animations.txt": b"14 0 0 0 16 4 6\n14 1 0 16 16 4 5\n6 2 0 32 16 4 4\n",
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w") as archive:
        for name, payload in textures.items():
            entry = zipfile.ZipInfo(name, date_time=(2026, 11, 25, 0, 0, 0))
            archive.writestr(entry, payload, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("default.zip"))
    make_pack(parser.parse_args().output)
