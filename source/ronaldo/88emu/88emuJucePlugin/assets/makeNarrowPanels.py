#!/usr/bin/env python3
"""Derive the plugin's front panel artwork from the standalone player's.

The player's panels (88emuplayer/assets/*_panel.png, 1836 x 561 = 612 x 187 dp at 3x)
carry its MIDI file player: a recessed PLAYLIST column with the list, the add button and
the transport switches, from 16 dp to 96 dp. The plugin has no file player, so that
column is cut out and every panel becomes 80 dp narrower, 532 x 187 dp.

Cutting is per row, because the two tiers of a board's front do not have the same free
space. Rows that cross the upper tier lose the recess itself; rows that carry the device
name at the bottom left lose an empty column to the right of it instead, so the name -
the plugin's device selector, and the only painted control left down there - keeps its
place. The two intervals are equally wide, so the result stays a rectangle and every
element right of the recess moves by exactly 80 dp.

Two details the artwork forces:
  - The cut is 240 px, which is 15 periods of the CM bezel's 16 px ridge strip, so the
    ridges stay in phase across the join. The recess frame is 246 px wide, so the six
    pixels of it that survive are painted over with panel surface from next to it.
  - The SC-8850 is the one board whose lower tier has switches (F1..F4, INST MAP), and
    they sit where the name would have to move to. There the name is erased and
    re-stamped, scaled to fit left of F1's new position.

Run it after changing a player panel; it only reads the player's assets and only writes
the twelve panels next to this script. No dependencies beyond the standard library.

Usage: python3 makeNarrowPanels.py
"""

import os
import statistics
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "88emuplayer", "assets")

CUT_X, CUT_W = 48, 240      # the recess, 16 dp .. 96 dp, in the artwork's 3x pixels
PATCH_SRC, PATCH_DST, PATCH_W = 298, 46, 12     # panel surface over the recess frame's remains
RECESS_BOTTOM = 368         # below it no frame is left to paint over

PANELS = ["sc88", "sc88vl", "sc88pro", "sc88exp", "sc8850", "sc8820",
          "sc55", "sc55mk2", "sc55pc", "cm32p", "cm32l", "cm64"]

# The SC-8850's name, scaled to end left of F1, which the cut moves to 98 dp. The clean
# columns are the gap between the name and F1, tiled over the fragment the cut leaves.
RESTAMP = {"sc8850": dict(scale=0.78, right=285, clean=(415, 530), erase=(40, 188))}


# ---------------------------------------------------------------- 8 bit RGBA PNG I/O

def png_read(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, w, h = 8, bytearray(), None, None
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        typ, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert (depth, color, interlace) == (8, 6, 0), path
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        pos += 12 + ln
    raw, stride = zlib.decompress(bytes(idat)), w * 4
    out, prev, p = bytearray(h * stride), bytearray(stride), 0
    for y in range(h):
        ft, p = raw[p], p + 1
        line, p = bytearray(raw[p:p + stride]), p + stride
        if ft == 1:
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 0xff
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xff
        elif ft == 3:
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xff
        elif ft == 4:
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                b, c = prev[i], prev[i - 4] if i >= 4 else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xff
        elif ft != 0:
            raise ValueError("filter %d" % ft)
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, out


def png_write(path, w, h, data):
    stride = w * 4
    raw, prev = bytearray(), bytearray(stride)
    for y in range(h):
        line = data[y * stride:(y + 1) * stride]
        best = None
        for ft in (0, 1, 2, 4):
            if ft == 0:
                cand = bytes(line)
            elif ft == 1:
                cand = bytes([line[i] if i < 4 else (line[i] - line[i - 4]) & 0xff for i in range(stride)])
            elif ft == 2:
                cand = bytes([(line[i] - prev[i]) & 0xff for i in range(stride)])
            else:
                out = bytearray(stride)
                for i in range(stride):
                    a = line[i - 4] if i >= 4 else 0
                    b, c = prev[i], prev[i - 4] if i >= 4 else 0
                    pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                    out[i] = (line[i] - (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xff
                cand = bytes(out)
            score = sum(min(v, 256 - v) for v in cand)
            if best is None or score < best[0]:
                best = (score, ft, cand)
        raw.append(best[1])
        raw += best[2]
        prev = line

    def chunk(typ, body):
        return struct.pack(">I", len(body)) + typ + body + struct.pack(">I", zlib.crc32(typ + body) & 0xffffffff)

    open(path, "wb").write(b"\x89PNG\r\n\x1a\n"
                           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
                           + chunk(b"IEND", b""))


# ------------------------------------------------------------------------- analysis

def green(d, w, x, y):
    return d[(y * w + x) * 4 + 1]


def background(d, w, y, x0=20, x1=None, step=7):
    """The row's own surface level: most of a row is panel, so its median is the surface."""
    return statistics.median([green(d, w, x, y) for x in range(x0, x1 if x1 else w - 20, step)])


def content(d, w, y, x0, x1, thr=30):
    level = background(d, w, y)
    return [x for x in range(x0, x1) if abs(green(d, w, x, y) - level) > thr]


def analyse(d, w, h):
    """The device name's box and the column the name's rows can lose instead of the recess."""
    rows = {}
    for y in range(400, h):
        marks = content(d, w, y, 66, 462)   # 22 dp .. 154 dp: the name, and nothing else
        if len(marks) > 2:
            rows[y] = marks
    name = (min(rows), max(rows) + 1, min(min(m) for m in rows.values()), max(max(m) for m in rows.values()) + 1)
    top, bottom, left, right = name
    assert 75 <= left <= 90 and 480 <= top <= 500, "device name not where it was: %s" % (name,)

    # Whatever reaches into the rows above the name shifts with them, so the window has
    # to end before it - on the SC-8850 that leaves none, and the name is moved instead.
    crossing = content(d, w, top - 14, CUT_X + CUT_W, w - 40)
    limit = min(crossing) if crossing else w
    for start in range(right + 18, w - CUT_W - 30, 3):
        if start + CUT_W > limit:
            break
        if all(not content(d, w, y, start, start + CUT_W) for y in range(top - 6, h, 2)):
            return name, start
    return name, None


# -------------------------------------------------------------------------- panels

def copy_row(dst, dw, dx, src, sw, sx, y, n):
    d0, s0 = (y * dw + dx) * 4, (y * sw + sx) * 4
    dst[d0:d0 + n * 4] = src[s0:s0 + n * 4]


def narrow(d, w, h, name, window):
    top = name[0] - 6
    nw = w - CUT_W
    out = bytearray(nw * h * 4)
    for y in range(h):
        cut = CUT_X if window is None or y < top else window
        copy_row(out, nw, 0, d, w, 0, y, cut)
        copy_row(out, nw, cut, d, w, cut + CUT_W, y, nw - cut)
        if cut == CUT_X and y <= RECESS_BOTTOM:
            copy_row(out, nw, PATCH_DST, d, w, PATCH_SRC, y, PATCH_W)
    return nw, out


def restamp(src, sw, dst, dw, name, scale, right, clean, erase):
    """Erase the name fragment the cut left behind, then composite the name scaled down."""
    top, bottom, left, rightSrc = name
    cx0, cx1 = clean
    y0, y1 = top - 8, min(bottom + 10, 561)

    # Tile clean columns of the same rows over the fragment. The surface carries a gentle
    # horizontal gradient, so every tiled column is levelled to what the rows just above
    # and below the fragment show at that very x.
    near = list(range(y0 - 22, y0 - 4)) + list(range(y1 + 4, min(y1 + 22, 561)))
    for x in range(*erase):
        t = (x - erase[0]) % ((cx1 - cx0) * 2)
        sx = cx0 + t if t < cx1 - cx0 else cx1 - 1 - (t - (cx1 - cx0))
        offset = [statistics.median([dst[(y * dw + x) * 4 + c] for y in near])
                  - statistics.median([src[(y * sw + sx) * 4 + c] for y in near]) for c in range(3)]
        for y in range(y0, y1):
            i, j = (y * sw + sx) * 4, (y * dw + x) * 4
            for c in range(3):
                dst[j + c] = min(255, max(0, round(src[i + c] + offset[c])))

    # Composite through a coverage matte taken from how far a pixel is off the surface, so
    # only the glyphs are written and the destination keeps its own background and grain.
    level = {y: background(src, sw, y, cx0, cx1, 3) for y in range(top, bottom)}
    tw, th = round((rightSrc - left) * scale), round((bottom - top) * scale)
    tx, ty = right - tw, bottom - th
    for y in range(th):
        for x in range(tw):
            r = g = b = cov = 0.0
            n = 0
            for sy in range(round(y / scale), max(round((y + 1) / scale), round(y / scale) + 1)):
                if top + sy >= bottom:
                    continue
                for sx in range(round(x / scale), max(round((x + 1) / scale), round(x / scale) + 1)):
                    if left + sx >= rightSrc:
                        continue
                    i = ((top + sy) * sw + left + sx) * 4
                    r, g, b = r + src[i], g + src[i + 1], b + src[i + 2]
                    cov += min(1.0, abs(src[i + 1] - level[top + sy]) / 25.0)
                    n += 1
            if not n:
                continue
            r, g, b, cov = r / n, g / n, b / n, cov / n
            j = ((ty + y) * dw + tx + x) * 4
            for c, v in enumerate((r, g, b)):
                dst[j + c] = round(dst[j + c] * (1 - cov) + v * cov)


def main():
    for panel in PANELS:
        w, h, d = png_read(os.path.join(SRC, panel + "_panel.png"))
        assert (w, h) == (1836, 561), "%s is %dx%d, not the 3x 612 x 187 dp panel" % (panel, w, h)
        name, window = analyse(d, w, h)
        assert (window is None) == (panel in RESTAMP), "%s: free column window %s" % (panel, window)
        nw, out = narrow(d, w, h, name, window)
        if panel in RESTAMP:
            restamp(d, w, out, nw, name, **RESTAMP[panel])
        path = os.path.join(HERE, panel + "_panel.png")
        png_write(path, nw, h, out)
        print("%-8s -> %dx%d  %d KiB" % (panel, nw, h, os.path.getsize(path) // 1024))


if __name__ == "__main__":
    sys.exit(main())
