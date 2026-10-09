#!/usr/bin/env python3
"""Draw openbv's icon: a babo, a shaded blue ball with a red band round it, on a transparent background.

Our own mark, drawn from scratch (no game art). Standard library only, so it runs on CI runners without
Pillow; edges are anti-aliased analytically. Writes an 8-bit RGBA PNG.

    tools/icon.py <size> <out.png>
"""
import math
import struct
import sys
import zlib


def clamp(v, lo=0.0, hi=1.0):
    return lo if v < lo else hi if v > hi else v


def babo(size):
    s = float(size)
    cx, cy, r = s * 0.5, s * 0.47, s * 0.40
    lx, ly, lz = -0.45, -0.55, 0.70          # light from the upper left, towards the viewer
    ll = math.sqrt(lx * lx + ly * ly + lz * lz)
    lx, ly, lz = lx / ll, ly / ll, lz / ll
    px = bytearray(size * size * 4)
    for y in range(size):
        for x in range(size):
            fx, fy = x + 0.5, y + 0.5
            # the shadow under the ball: a soft ellipse
            sx, sy = (fx - cx) / (r * 0.95), (fy - (cy + r * 0.98)) / (r * 0.22)
            shadow = clamp(1.0 - math.sqrt(sx * sx + sy * sy)) * 0.45
            dx, dy = (fx - cx) / r, (fy - cy) / r
            d = math.sqrt(dx * dx + dy * dy)
            cover = clamp((1.0 - d) * r + 0.5)      # coverage of the pixel by the disc
            out = [0.0, 0.0, 0.0, shadow]
            if cover > 0:
                nz = math.sqrt(max(0.0, 1.0 - min(1.0, d) ** 2))
                diffuse = clamp(dx * lx + dy * ly + nz * lz)
                # a red band round the ball's upper part (a latitude of the sphere, its axis tilted
                # towards the viewer), the rest of the ball blue
                ax, ay, az = 0.25, -0.80, 0.55
                al = math.sqrt(ax * ax + ay * ay + az * az)
                lat = (dx * ax + dy * ay + nz * az) / al
                band = clamp((0.11 - abs(lat - 0.42)) * r * 0.5 + 0.5)
                blue, red = (0.16, 0.42, 0.92), (0.88, 0.20, 0.16)
                base = tuple(blue[k] + (red[k] - blue[k]) * band for k in range(3))
                hx, hy, hz = lx, ly, lz + 1.0                  # Blinn half vector (viewer on +z)
                hl = math.sqrt(hx * hx + hy * hy + hz * hz)
                spec = clamp((dx * hx + dy * hy + nz * hz) / hl) ** 40
                shade = 0.28 + 0.72 * diffuse
                col = [clamp(c * shade + spec * 0.85) for c in base]
                rim = clamp((d - 0.93) / 0.07) * 0.35          # a dark outline at the edge
                col = [c * (1.0 - rim) for c in col]
                a = cover + out[3] * (1.0 - cover)
                out = [col[i] * cover / a if a else 0 for i in range(3)] + [a]
            i = (y * size + x) * 4
            px[i:i + 4] = bytes(int(round(clamp(v) * 255)) for v in out)
    return px


def write_png(path, size, px):
    raw = b''.join(b'\x00' + bytes(px[y * size * 4:(y + 1) * size * 4]) for y in range(size))
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        raise SystemExit('usage: icon.py <size> <out.png>')
    n = int(sys.argv[1])
    write_png(sys.argv[2], n, babo(n))
