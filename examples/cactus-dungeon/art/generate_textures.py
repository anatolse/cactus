"""Generate original, repeatable dungeon textures using Python's standard library."""

from pathlib import Path
import math
import random
import struct
import zlib


SIZE = 64
HERE = Path(__file__).resolve().parent
PALETTE = {
    "rock": (70, 77, 90),
    "earth": (112, 72, 44),
    "gold": (192, 143, 39),
    "infinite_gold": (232, 183, 62),
    "floor": (108, 100, 85),
    "claimed_blue": (54, 94, 150),
    "claimed_red": (154, 57, 57),
    "water": (30, 103, 164),
    "lava": (235, 71, 15),
    "lair": (105, 72, 137),
    "farm": (71, 133, 60),
    "training": (159, 114, 65),
    "bridge": (133, 95, 54),
    "white": (255, 255, 255),
}
# Textures drawn as vertical strips of square frames that play as flipbooks.
FRAMES = {"water": 8, "lava": 8, "gold": 8}


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def frame_rows(name, color, seed, phase):
    """One SIZE x SIZE frame; `phase` in [0, 1) moves the liquid and the gold glint."""
    rng = random.Random(seed)
    rows = bytearray()
    turn = phase * 2.0 * math.pi
    for y in range(SIZE):
        rows.append(0)
        for x in range(SIZE):
            shade = rng.uniform(0.85, 1.12)
            if name == "white":
                shade = 1.0
            elif name in ("water", "lava"):
                shade += 0.14 * math.sin(x * 0.3 + math.sin(y * 0.2 + turn) * 2.0 + turn)
            elif name == "bridge":
                shade *= 0.55 if y % 16 in (0, 15) else 1.0
            else:
                seam = x % 32 == (16 if y // 16 % 2 else 0) or y % 16 == 0
                shade *= 0.6 if seam else 1.0
                if name == "gold":
                    glint = (x + y - phase * 2 * SIZE) % (2 * SIZE)
                    shade *= 1.0 + 0.35 * max(0.0, 1.0 - abs(glint - SIZE) / 6.0)
            rows.extend(max(0, min(255, round(channel * shade))) for channel in color)
    return rows


def texture(name, color, seed):
    frames = FRAMES.get(name, 1)
    rows = bytearray()
    for frame in range(frames):
        rows.extend(frame_rows(name, color, seed, frame / frames))
    header = struct.pack(">IIBBBBB", SIZE, SIZE * frames, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    for index, (name, color) in enumerate(PALETTE.items()):
        (HERE / f"{name}.png").write_bytes(texture(name, color, 100 + index))
