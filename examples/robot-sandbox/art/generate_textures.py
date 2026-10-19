"""Writes the robot sandbox's block textures as 64x64 PNG files.

The images are drawn procedurally by this script, so they are original project
assets with no third-party license. Run it from any directory:

    python examples/robot-sandbox/art/generate_textures.py
"""

import math
import os
import random
import struct
import zlib

SIZE = 64
HERE = os.path.dirname(os.path.abspath(__file__))


def write_png(name, pixels):
    rows = b"".join(b"\x00" + bytes(channel for pixel in row for channel in pixel) for row in pixels)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
    with open(os.path.join(HERE, name), "wb") as out:
        out.write(png)


def clamp(value):
    return max(0, min(255, int(round(value))))


def value_noise(rng, cells):
    grid = [[rng.random() for _ in range(cells + 1)] for _ in range(cells + 1)]
    for row in grid:
        row[cells] = row[0]
    grid[cells] = grid[0]

    def sample(x, y):
        fx, fy = x * cells / SIZE, y * cells / SIZE
        ix, iy = int(fx), int(fy)
        tx, ty = fx - ix, fy - iy
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        top = grid[iy][ix] * (1 - tx) + grid[iy][ix + 1] * tx
        bottom = grid[iy + 1][ix] * (1 - tx) + grid[iy + 1][ix + 1] * tx
        return top * (1 - ty) + bottom * ty

    return sample


def wood():
    rng = random.Random(11)
    grain = value_noise(rng, 8)
    pixels = []
    for y in range(SIZE):
        plank = y // 16
        offset = (plank * 23) % SIZE
        row = []
        for x in range(SIZE):
            line = math.sin((x + offset) * 0.35 + grain(x, y) * 6.0) * 0.5 + 0.5
            shade = 0.75 + 0.25 * line - 0.12 * (plank % 2)
            seam = y % 16 in (0, 15) or (x + offset) % 32 == 0
            if seam:
                shade *= 0.55
            row.append((clamp(150 * shade), clamp(98 * shade), clamp(52 * shade)))
        pixels.append(row)
    return pixels


def stone():
    rng = random.Random(23)
    coarse = value_noise(rng, 4)
    fine = value_noise(rng, 16)
    seeds = [(rng.randrange(SIZE), rng.randrange(SIZE)) for _ in range(14)]
    pixels = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            distances = sorted(
                min(abs(x - sx), SIZE - abs(x - sx)) ** 2 + min(abs(y - sy), SIZE - abs(y - sy)) ** 2 for sx, sy in seeds
            )
            edge = math.sqrt(distances[1]) - math.sqrt(distances[0])
            mortar = 0.55 if edge < 1.6 else 1.0
            shade = (0.62 + 0.28 * coarse(x, y) + 0.14 * fine(x, y)) * mortar
            row.append((clamp(128 * shade), clamp(128 * shade), clamp(134 * shade)))
        pixels.append(row)
    return pixels


def iron():
    rng = random.Random(37)
    brushed = value_noise(rng, 32)
    pixels = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            shade = 0.72 + 0.18 * brushed(x, (y * 7) % SIZE)
            border = x < 2 or y < 2 or x > SIZE - 3 or y > SIZE - 3
            if border:
                shade *= 0.6
            for cx, cy in ((7, 7), (56, 7), (7, 56), (56, 56)):
                d = math.hypot(x - cx, y - cy)
                if d < 3.2:
                    shade = 1.05 - d * 0.12
            row.append((clamp(150 * shade), clamp(158 * shade), clamp(170 * shade)))
        pixels.append(row)
    return pixels


def soil():
    rng = random.Random(41)
    coarse = value_noise(rng, 6)
    fine = value_noise(rng, 24)
    pixels = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            mix = 0.6 * coarse(x, y) + 0.4 * fine(x, y)
            green = rng.random() < 0.08 + 0.12 * coarse(x, y)
            if green:
                row.append((clamp(70 + 30 * mix), clamp(120 + 50 * mix), clamp(48 + 20 * mix)))
            else:
                row.append((clamp(96 + 50 * mix), clamp(70 + 36 * mix), clamp(44 + 22 * mix)))
        pixels.append(row)
    return pixels


if __name__ == "__main__":
    for name, draw in (("wood.png", wood), ("stone.png", stone), ("iron.png", iron), ("soil.png", soil)):
        write_png(name, draw())
        print("wrote", os.path.join(HERE, name))
