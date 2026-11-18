#!/usr/bin/env python3
"""
Converts a TrueType font into an anti-aliased bitmap font header (8 bit alpha
per pixel) for the display firmware.

The glyphs are rendered with FreeType (through Pillow) at the same pixel size as
Adafruit's fontconvert (141 DPI), so the anti-aliased fonts line up with the
old 1-bit GFX fonts.

With --draw-order, a second 8 bit plane is generated for the digits: for every
pixel it stores how far along the stroke the pixel lies (0 = where the pen
starts, 255 = where it ends). The firmware uses it to "write" a digit.
The order is the distance from a start point measured inside the glyph, so the
ink flows along the strokes. Start points and small barriers (to make a loop
run in one direction) are defined in DRAW_HINTS below.

Usage:
    python3 tools/fontconvert_aa.py <font.ttf> <size_pt> <name> <chars> [--draw-order] > lib/Font/<name>.h

Example (the fonts used by the firmware):
    python3 tools/fontconvert_aa.py ~/Library/Fonts/Roboto-Light.ttf 50 Roboto_Light50pt_aa "0123456789" --draw-order > lib/Font/Roboto_Light50pt_aa.h
    python3 tools/fontconvert_aa.py ~/Library/Fonts/Roboto-Light.ttf 20 Roboto_Light20pt_aa "s%!?" > lib/Font/Roboto_Light20pt_aa.h
"""
import heapq
import math
import sys
from PIL import Image, ImageDraw, ImageFont

DPI = 141  # same as Adafruit fontconvert

# Pixels with at least this alpha form the stroke the pen moves along
STROKE_ALPHA = 64

# Writing hints per digit:
#   start:   relative position (0..1 of the glyph box) where the pen starts
#   barrier: optional cut that keeps the ink from flowing in one direction
#            ("right":  no flow to the right of the start in the upper part,
#             "up":     ink leaves the start upwards only)
#   path:    instead of start/barrier: the pen path drawn by hand as relative
#            points; None lifts the pen and moves it to the next point.
#            Used where strokes cross (8) or the pen has to be lifted (4).
DRAW_HINTS = {
    "0": {"start": (0.5, 0.0), "barrier": "right"},
    "1": {"start": (0.0, 0.25)},
    "2": {"start": (0.0, 0.28)},
    "3": {"start": (0.0, 0.22)},
    "4": {"path": [(0.72, 0.02), (0.04, 0.72), (0.98, 0.72), None,
                   (0.72, 0.02), (0.72, 0.99)]},
    "5": {"start": (1.0, 0.0)},
    "6": {"path": [(0.97, 0.02), (0.6, 0.06), (0.3, 0.17), (0.12, 0.35), (0.06, 0.6),
                   (0.12, 0.82), (0.3, 0.95), (0.5, 0.98), (0.75, 0.94), (0.92, 0.78),
                   (0.95, 0.6), (0.85, 0.43), (0.6, 0.34), (0.35, 0.36), (0.12, 0.44)]},
    "7": {"start": (0.0, 0.0)},
    "8": {"path": [(0.5, 0.03), (0.2, 0.08), (0.07, 0.25), (0.18, 0.4), (0.5, 0.48),
                   (0.85, 0.56), (0.97, 0.73), (0.85, 0.91), (0.5, 0.97), (0.15, 0.91),
                   (0.03, 0.73), (0.15, 0.56), (0.5, 0.48), (0.82, 0.4), (0.93, 0.25),
                   (0.8, 0.08), (0.5, 0.03)]},
    "9": {"start": (1.0, 0.3), "barrier": "up"},
}


def draw_order_path(img, hint):
    """
    Draw order from a hand-drawn pen path: every pixel takes the position
    along the path of the closest inked path point.
    """
    w, h = img.size
    px = img.load()
    stroke = [(x, y) for y in range(h) for x in range(w) if px[x, y] >= STROKE_ALPHA]

    # Dense samples along the path (every half pixel), flagged ink / travel
    samples = []  # (x, y, ink)
    prev = None
    ink = True
    for point in hint["path"]:
        if point is None:
            ink = False
            continue
        p = (point[0] * (w - 1), point[1] * (h - 1))
        if prev is None:
            samples.append((p[0], p[1], True))
        else:
            length = math.hypot(p[0] - prev[0], p[1] - prev[1])
            steps = max(1, int(length * 2))
            for i in range(1, steps + 1):
                t = i / steps
                samples.append((prev[0] + (p[0] - prev[0]) * t, prev[1] + (p[1] - prev[1]) * t, ink))
        prev = p
        ink = True

    # Snap ink samples onto the middle of the stroke
    snapped = []
    for x, y, is_ink in samples:
        if is_ink:
            near = [q for q in stroke if (q[0] - x) ** 2 + (q[1] - y) ** 2 <= 16]
            if near:
                x = sum(q[0] for q in near) / len(near)
                y = sum(q[1] for q in near) / len(near)
        snapped.append((x, y, is_ink))

    # Arc length along the (snapped) path
    arc = [0.0]
    for i in range(1, len(snapped)):
        arc.append(arc[-1] + math.hypot(snapped[i][0] - snapped[i - 1][0], snapped[i][1] - snapped[i - 1][1]))
    total = arc[-1] or 1.0

    ink_samples = [(snapped[i][0], snapped[i][1], arc[i]) for i in range(len(snapped)) if snapped[i][2]]
    order = []
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:
                order.append(255)
                continue
            _, _, a = min(ink_samples, key=lambda q: (q[0] - x) ** 2 + (q[1] - y) ** 2)
            order.append(min(255, round(a / total * 255)))

    main_stroke = [(snapped[i][0], snapped[i][1], arc[i] / total * 255) for i in range(len(snapped))]
    return order, main_stroke


def draw_order(img, hint):
    """
    Returns the per-pixel draw order (0..255) of a glyph image and the main
    stroke: the path from the pen start to the pixel that is drawn last.
    """
    if "path" in hint:
        return draw_order_path(img, hint)

    w, h = img.size
    px = img.load()
    stroke = {(x, y) for y in range(h) for x in range(w) if px[x, y] >= STROKE_ALPHA}

    # Start: stroke pixel closest to the hinted position
    hx, hy = hint["start"][0] * (w - 1), hint["start"][1] * (h - 1)
    sx, sy = min(stroke, key=lambda p: (p[0] - hx) ** 2 + (p[1] - hy) ** 2)
    barrier = hint.get("barrier")

    def blocked(x, y, nx, ny):
        if barrier == "right":
            # vertical cut right of the start, in the upper part of the glyph
            crosses = (x <= sx < nx) or (nx <= sx < x)
            return crosses and min(y, ny) <= sy + 0.2 * h
        if barrier == "up":
            # horizontal cut below the start, on the right half of the glyph
            crosses = (y <= sy < ny) or (ny <= sy < y)
            return crosses and min(x, nx) >= 0.5 * w
        return False

    # Dijkstra inside the stroke
    dist = {(sx, sy): 0.0}
    prev = {}
    queue = [(0.0, sx, sy)]
    while queue:
        d, x, y = heapq.heappop(queue)
        if d > dist[(x, y)]:
            continue
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                n = (x + dx, y + dy)
                if n not in stroke or blocked(x, y, n[0], n[1]):
                    continue
                nd = d + (math.sqrt(2) if dx and dy else 1.0)
                if nd < dist.get(n, float("inf")):
                    dist[n] = nd
                    prev[n] = (x, y)
                    heapq.heappush(queue, (nd, n[0], n[1]))

    # Pixels that were not reached (fringe, cut off parts) take the order of
    # the closest reached pixel
    reached = list(dist.items())
    max_dist = max(d for _, d in reached) or 1.0
    order = []
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:
                order.append(255)
                continue
            d = dist.get((x, y))
            if d is None:
                (_, d) = min(reached, key=lambda item: (item[0][0] - x) ** 2 + (item[0][1] - y) ** 2)
            order.append(min(255, round(d / max_dist * 255)))

    # Main stroke: walk back from the last drawn pixel to the start
    node = max(dist, key=dist.get)
    path = [node]
    while node in prev:
        node = prev[node]
        path.append(node)
    path.reverse()
    main_stroke = [(x, y, dist[(x, y)] / max_dist * 255) for x, y in path]
    return order, main_stroke


PEN_PATH_POINTS = 48


def pen_path(main_stroke):
    """
    Returns PEN_PATH_POINTS (x, y) positions of the pen tip along the main
    stroke, evenly spaced in draw order. Side branches (e.g. the middle tip of
    a 3) are drawn by the flowing ink without the pen.
    """
    # Smooth the pixel path a little, it zigzags by one pixel
    n = len(main_stroke)
    smooth = []
    for i in range(n):
        window = main_stroke[max(0, i - 2):min(n, i + 3)]
        smooth.append((sum(p[0] for p in window) / len(window),
                       sum(p[1] for p in window) / len(window),
                       main_stroke[i][2]))
    points = []
    j = 0
    for i in range(PEN_PATH_POINTS):
        level = i / (PEN_PATH_POINTS - 1) * 255
        while j < n - 1 and smooth[j + 1][2] <= level:
            j += 1
        if j < n - 1 and smooth[j + 1][2] > smooth[j][2]:
            # interpolate between two path pixels
            t = (level - smooth[j][2]) / (smooth[j + 1][2] - smooth[j][2])
            t = min(1.0, max(0.0, t))
            x = smooth[j][0] + (smooth[j + 1][0] - smooth[j][0]) * t
            y = smooth[j][1] + (smooth[j + 1][1] - smooth[j][1]) * t
        else:
            x, y = smooth[j][0], smooth[j][1]
        points.append((min(255, max(0, round(x))), min(255, max(0, round(y)))))
    return points


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    with_order = "--draw-order" in sys.argv
    if len(args) != 4:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    path, size_pt, name, chars = args[0], int(args[1]), args[2], args[3]
    pixel_size = round(size_pt * DPI / 72)
    font = ImageFont.truetype(path, pixel_size)

    codes = sorted(set(ord(c) for c in chars))
    first, last = codes[0], codes[-1]

    bitmap = []
    order = []
    pen = []
    glyphs = []
    for code in range(first, last + 1):
        ch = chr(code)
        if code not in codes:
            glyphs.append((len(bitmap), 0, 0, 0, 0, 0, ch))
            if with_order:
                pen.extend([0] * (2 * PEN_PATH_POINTS))
            continue
        # Bounding box relative to the left end of the baseline.
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        w, h = x1 - x0, y1 - y0
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        # Crop to the pixels that are actually inked.
        ink = img.getbbox()
        if ink:
            img = img.crop(ink)
            x0, y0 = x0 + ink[0], y0 + ink[1]
            w, h = img.size
        advance = round(font.getlength(ch))
        glyphs.append((len(bitmap), w, h, advance, x0, y0, ch))
        bitmap.extend(img.getdata())
        if with_order:
            if ch not in DRAW_HINTS:
                sys.exit("no draw hint for character '%s'" % ch)
            glyph_order, main_stroke = draw_order(img, DRAW_HINTS[ch])
            order.extend(glyph_order)
            for x, y in pen_path(main_stroke):
                pen.extend((x, y))

    y_advance = sum(font.getmetrics())

    def byte_table(values):
        return ["  " + ", ".join("0x%02X" % v for v in values[i:i + 16]) + ","
                for i in range(0, len(values), 16)]

    out = []
    out.append("#ifndef _%s_H" % name.upper())
    out.append("#define _%s_H" % name.upper())
    out.append("")
    out.append("#include <aafont.h>")
    out.append("")
    out.append("/**")
    out.append(" * Anti-aliased (8 bit alpha) version of the Roboto font from")
    out.append(" * https://fonts.google.com/specimen/Roboto, see LICENSE.txt.")
    out.append(" * Generated by tools/fontconvert_aa.py, do not edit by hand.")
    out.append(" */")
    out.append("")
    out.append("const uint8_t %sBitmaps[] = {" % name)
    out.extend(byte_table(bitmap))
    out.append("};")
    out.append("")
    if with_order:
        out.append("// Draw order per pixel (0 = pen starts here, 255 = pen ends here)")
        out.append("const uint8_t %sOrder[] = {" % name)
        out.extend(byte_table(order))
        out.append("};")
        out.append("")
        out.append("// Pen tip positions along the stroke, %d (x, y) pairs per glyph," % PEN_PATH_POINTS)
        out.append("// relative to the top left corner of the glyph's inked area")
        out.append("const uint8_t %sPen[] = {" % name)
        out.extend(byte_table(pen))
        out.append("};")
        out.append("")
    out.append("const AAGlyph %sGlyphs[] = {" % name)
    for off, w, h, adv, xo, yo, ch in glyphs:
        out.append("  { %6d, %3d, %3d, %3d, %4d, %4d },   // 0x%02X '%s'"
                   % (off, w, h, adv, xo, yo, ord(ch), ch if ch != "\\" else "backslash"))
    out.append("};")
    out.append("")
    out.append("const AAFont %s = {" % name)
    out.append("  %sBitmaps," % name)
    out.append("  %sGlyphs," % name)
    out.append("  %s," % ("%sOrder" % name if with_order else "nullptr"))
    out.append("  %s, %d," % (("%sPen" % name, PEN_PATH_POINTS) if with_order else ("nullptr", 0)))
    out.append("  0x%02X, 0x%02X, %d };" % (first, last, y_advance))
    out.append("")
    out.append("// Approx. %d bytes" % (len(bitmap) + len(order) + len(pen) + 8 * len(glyphs) + 24))
    out.append("")
    out.append("#endif")
    print("\n".join(out))


if __name__ == "__main__":
    main()
