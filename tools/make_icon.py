#!/usr/bin/env python3
"""Draws the ZeroSub launcher icon.

Pure standard library: the icon is described as a few shapes, rendered with
anti-aliasing, and written as PNG. Run it again after changing the design:

    python tools/make_icon.py

It writes the sizes the package installs into assets/images/.
"""

import math
import os
import struct
import sys
import zlib

# Colours from the app's dark theme (src/view/ui_const.h).
BACKGROUND_TOP = (0x1E, 0x22, 0x2B)
BACKGROUND_BOTTOM = (0x0C, 0x0D, 0x11)
RING = (0xF4, 0xF6, 0xF8)
WAVES = (0x63, 0xE2, 0xB7)
PULSES = (0xF2, 0xC9, 0x7D)

# Geometry in a 0..1 square, y down.
CORNER_RADIUS = 0.225
INSET = 0.03
RING_CENTER = (0.31, 0.425)
RING_RADIUS = 0.125
RING_STROKE = 0.058
WAVE_RADII = (0.265, 0.37, 0.475)
WAVE_STROKE = 0.052
WAVE_HALF_ANGLE = math.radians(32)
WAVE_OPACITY = (1.0, 0.78, 0.52)
PULSE_Y_LOW = 0.86
PULSE_Y_HIGH = 0.765
PULSE_STROKE = 0.042
# x positions where the pulse line changes level, starting low.
PULSE_EDGES = (0.17, 0.25, 0.33, 0.47, 0.55, 0.63, 0.71, 0.83)


def clamp(value, low=0.0, high=1.0):
    return max(low, min(high, value))


def coverage(distance, pixel):
    """Coverage of a pixel whose centre is `distance` outside a shape edge."""
    return clamp(0.5 - distance / pixel)


def rounded_rect_distance(x, y, left, top, right, bottom, radius):
    cx = (left + right) / 2
    cy = (top + bottom) / 2
    qx = abs(x - cx) - ((right - left) / 2 - radius)
    qy = abs(y - cy) - ((bottom - top) / 2 - radius)
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    inside = min(max(qx, qy), 0.0)
    return outside + inside - radius


def segment_distance(x, y, x0, y0, x1, y1):
    dx, dy = x1 - x0, y1 - y0
    length_sq = dx * dx + dy * dy
    t = 0.0 if length_sq == 0 else clamp(((x - x0) * dx + (y - y0) * dy) / length_sq)
    return math.hypot(x - (x0 + t * dx), y - (y0 + t * dy))


def arc_distance(x, y, cx, cy, radius, half_angle):
    """Distance to an arc centred on the positive x direction, with round ends."""
    dx, dy = x - cx, y - cy
    angle = math.atan2(dy, dx)
    if abs(angle) <= half_angle:
        return abs(math.hypot(dx, dy) - radius)
    end_y = math.copysign(math.sin(half_angle), dy) * radius
    end_x = math.cos(half_angle) * radius
    return math.hypot(dx - end_x, dy - end_y)


def pulse_points():
    points = [(PULSE_EDGES[0], PULSE_Y_LOW)]
    high = False
    for edge in PULSE_EDGES[1:-1]:
        points.append((edge, PULSE_Y_HIGH if high else PULSE_Y_LOW))
        high = not high
        points.append((edge, PULSE_Y_HIGH if high else PULSE_Y_LOW))
    points.append((PULSE_EDGES[-1], PULSE_Y_HIGH if high else PULSE_Y_LOW))
    return points


PULSE_LINE = pulse_points()


def blend(base, colour, alpha):
    return tuple(base[i] + (colour[i] - base[i]) * alpha for i in range(3))


def render(size):
    pixel = 1.0 / size
    rows = []
    for py in range(size):
        row = bytearray()
        y = (py + 0.5) * pixel
        for px in range(size):
            x = (px + 0.5) * pixel

            shape = coverage(
                rounded_rect_distance(x, y, INSET, INSET, 1 - INSET, 1 - INSET, CORNER_RADIUS), pixel)
            if shape <= 0.0:
                row += b"\x00\x00\x00\x00"
                continue

            t = clamp((y - INSET) / (1 - 2 * INSET))
            colour = blend(BACKGROUND_TOP, BACKGROUND_BOTTOM, t)

            for radius, opacity in zip(WAVE_RADII, WAVE_OPACITY):
                d = arc_distance(x, y, RING_CENTER[0], RING_CENTER[1], radius, WAVE_HALF_ANGLE)
                colour = blend(colour, WAVES, opacity * coverage(d - WAVE_STROKE / 2, pixel))

            d = abs(math.hypot(x - RING_CENTER[0], y - RING_CENTER[1]) - RING_RADIUS)
            colour = blend(colour, RING, coverage(d - RING_STROKE / 2, pixel))

            d = min(segment_distance(x, y, *PULSE_LINE[i], *PULSE_LINE[i + 1])
                    for i in range(len(PULSE_LINE) - 1))
            colour = blend(colour, PULSES, coverage(d - PULSE_STROKE / 2, pixel))

            row += bytes(int(round(clamp(c, 0, 255))) for c in colour)
            row.append(int(round(shape * 255)))
        rows.append(bytes(row))
    return rows


def write_png(path, rows):
    size = len(rows)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rows)
    with open(path, "wb") as output:
        output.write(b"\x89PNG\r\n\x1a\n")
        output.write(chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)))
        output.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        output.write(chunk(b"IEND", b""))


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "images")
    targets = [("zerosub.png", 100), ("zerosub_100.png", 100), ("zerosub_80.png", 80), ("zerosub_256.png", 256)]
    # Extra sizes on request, e.g. `python tools/make_icon.py 512 out.png`.
    if len(sys.argv) == 3:
        write_png(sys.argv[2], render(int(sys.argv[1])))
        print("wrote", sys.argv[2])
        return
    for name, size in targets:
        path = os.path.normpath(os.path.join(root, name))
        write_png(path, render(size))
        print("wrote", path, size)


if __name__ == "__main__":
    main()
