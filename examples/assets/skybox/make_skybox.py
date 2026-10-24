"""Writes the six faces of the demo apps' skybox cubemap, skybox.cubemap's PNGs, beside this script.

The sky is made, not photographed: a blue gradient from the horizon to the zenith, a sun ahead of a camera looking
along -Z (up and a little to its right), a ridge of hills round the horizon that differs all the way round, and dark
ground below. Every pixel is the colour of the world direction it shows, mapped onto the faces as GL and D3D lay out a
cubemap's, so the faces meet with no seams when the renderer samples by world direction. Standard library only:

    python make_skybox.py [side]
"""

import math
import os
import struct
import sys
import zlib

SUN = (0.35, 0.35, -1.0)


def normalise(v):
    length = math.sqrt(sum(c * c for c in v))
    return tuple(c / length for c in v)


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def ridge(azimuth):
    """The hills' height above the horizon, as the sine of an elevation, at an azimuth in radians."""
    return 0.06 + 0.035 * math.sin(3.0 * azimuth + 0.4) + 0.025 * math.sin(7.0 * azimuth + 1.3) + 0.012 * math.sin(
        17.0 * azimuth)


def coverage(distance, width):
    """How much of a texel `width` wide lies on the far side of an edge `distance` away: 0 to 1, for smooth edges."""
    return min(1.0, max(0.0, distance / width + 0.5))


def sky(d, sun, texel):
    """The linear colour seen along the unit direction `d`; `texel` is about one texel's size, to soften edges by."""
    x, y, z = d
    azimuth = math.atan2(x, -z)
    colour = mix((0.78, 0.86, 0.95), (0.18, 0.38, 0.78), math.sqrt(max(0.0, y)))
    cosine = x * sun[0] + y * sun[1] + z * sun[2]
    colour = mix(colour, (1.0, 0.92, 0.75), max(0.0, cosine) ** 64 * 0.8)
    # The sun's disc, 2 degrees across its radius.
    colour = mix(colour, (1.0, 0.97, 0.88), coverage(math.radians(2.0) - math.acos(min(1.0, cosine)), texel))
    # Hills, hazier at their foot, in front of the sky.
    height = ridge(azimuth)
    hills = mix((0.42, 0.48, 0.55), (0.22, 0.27, 0.33), min(1.0, max(0.0, y / height)) if height > 1e-4 else 1.0)
    colour = mix(hills, colour, coverage(y - height, texel))
    # Ground, darker towards the nadir, in front of everything.
    ground = mix((0.30, 0.27, 0.22), (0.12, 0.11, 0.09), min(1.0, max(0.0, -y * 2.0)))
    return mix(ground, colour, coverage(y, texel))


def face_direction(face, s, t):
    """The world direction of a face's texel at (s, t) in [-1, 1], s rightwards and t downwards in its image."""
    return {
        0: (1.0, -t, -s),
        1: (-1.0, -t, s),
        2: (s, 1.0, t),
        3: (s, -1.0, -t),
        4: (s, -t, 1.0),
        5: (-s, -t, -1.0),
    }[face]


def write_png(path, side, rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rows)
    header = struct.pack(">IIBBBBB", side, side, 8, 2, 0, 0, 0)
    with open(path, "wb") as png:
        png.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) +
                  chunk(b"IEND", b""))


def to_byte(linear):
    """A linear value as an sRGB-encoded byte, as the faces are read."""
    linear = min(1.0, max(0.0, linear))
    encoded = linear * 12.92 if linear <= 0.0031308 else 1.055 * linear ** (1.0 / 2.4) - 0.055
    return int(round(encoded * 255.0))


def main():
    side = int(sys.argv[1]) if len(sys.argv) > 1 else 512
    here = os.path.dirname(os.path.abspath(__file__))
    sun = normalise(SUN)
    names = ["px", "nx", "py", "ny", "pz", "nz"]
    for face, name in enumerate(names):
        rows = []
        for j in range(side):
            t = (j + 0.5) / side * 2.0 - 1.0
            row = bytearray()
            for i in range(side):
                s = (i + 0.5) / side * 2.0 - 1.0
                r, g, b = sky(normalise(face_direction(face, s, t)), sun, 2.0 / side)
                row += bytes((to_byte(r), to_byte(g), to_byte(b)))
            rows.append(bytes(row))
        write_png(os.path.join(here, name + ".png"), side, rows)


if __name__ == "__main__":
    main()
