#!/usr/bin/env python3
"""Generate placeholder PNG assets for the Cactus Platformer example."""

import struct
import zlib
import os

def create_png(filename, width, height, color_rgb):
    r, g, b = color_rgb
    sig = b'\x89PNG\r\n\x1a\n'

    ihdr_data = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    ihdr_crc = zlib.crc32(b'IHDR' + ihdr_data) & 0xffffffff
    ihdr = struct.pack('>I', 13) + b'IHDR' + ihdr_data + struct.pack('>I', ihdr_crc)

    raw = b''
    for y in range(height):
        raw += b'\x00'
        for x in range(width):
            raw += bytes([r, g, b])

    compressed = zlib.compress(raw)
    idat_crc = zlib.crc32(b'IDAT' + compressed) & 0xffffffff
    idat = struct.pack('>I', len(compressed)) + b'IDAT' + compressed + struct.pack('>I', idat_crc)

    iend_crc = zlib.crc32(b'IEND') & 0xffffffff
    iend = struct.pack('>I', 0) + b'IEND' + struct.pack('>I', iend_crc)

    with open(filename, 'wb') as f:
        f.write(sig + ihdr + idat + iend)
    print(f'  Created {filename} ({width}x{height})')

if __name__ == '__main__':
    base = os.path.dirname(os.path.abspath(__file__))

    os.makedirs(os.path.join(base, 'sprites'), exist_ok=True)
    os.makedirs(os.path.join(base, 'tiles'), exist_ok=True)
    os.makedirs(os.path.join(base, 'backgrounds'), exist_ok=True)

    print('Generating sprites...')
    create_png(os.path.join(base, 'sprites', 'player.png'), 32, 48, (100, 149, 237))
    create_png(os.path.join(base, 'sprites', 'enemy.png'), 32, 32, (204, 51, 51))
    create_png(os.path.join(base, 'sprites', 'gem.png'), 16, 16, (255, 215, 0))

    print('Generating tiles...')
    create_png(os.path.join(base, 'tiles', 'platform.png'), 64, 16, (139, 105, 20))
    create_png(os.path.join(base, 'tiles', 'ground.png'), 64, 32, (91, 140, 62))

    print('Generating backgrounds...')
    create_png(os.path.join(base, 'backgrounds', 'sky.png'), 256, 256, (135, 206, 235))
    create_png(os.path.join(base, 'backgrounds', 'mountains.png'), 256, 256, (119, 136, 153))

    print('Done! All placeholder assets generated.')
