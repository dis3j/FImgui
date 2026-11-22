# Generates the Fimgui logo ("Fi" where the i is a paint brush) as SVG, PNG and a multi-size ICO without external libraries.
# Same geometry as DrawLogo() in src/icons.cpp.
# Usage: python tools/make_logo.py  (run from the repository root)
import math, struct, zlib

S = 256.0
BG_RADIUS = 58.0
C0, C1 = (43, 140, 255), (138, 92, 255)          # gradient: top-left blue -> bottom-right violet
# "F" on a 256 grid: stem, top arm, middle arm
BARS = [(58, 60, 88, 196), (58, 60, 146, 88), (58, 114, 130, 140)]
BAR_RADIUS = 5.0
# Brush "i": paint drop (dot), handle, ferrule (72% white), tapered bristles
DOT = (182, 72, 13)
HANDLE = (175, 98, 189, 140, 7)
FERRULE = (169, 142, 195, 156, 3)
FERRULE_ALPHA = 0.72

def bezier(p0, p1, p2, p3, n=24):
    out = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        out.append(tuple(u * u * u * p0[k] + 3 * u * u * t * p1[k] + 3 * u * t * t * p2[k] + t * t * t * p3[k] for k in range(2)))
    return out

TIP = [(169, 158)] + bezier((195, 158), (196, 176), (190, 188), (182, 198)) + bezier((182, 198), (174, 188), (168, 176), (169, 158))[1:]

def inside_poly(px, py, poly):
    c = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > py) != (yj > py) and px < (xj - xi) * (py - yi) / (yj - yi) + xi: c = not c
        j = i
    return c

def sd_round_rect(px, py, x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2, (y1 - y0) / 2
    qx, qy = abs(px - cx) - hx + r, abs(py - cy) - hy + r
    return min(max(qx, qy), 0.0) + math.hypot(max(qx, 0.0), max(qy, 0.0)) - r

def render(size):
    k = S / size
    ss = 4 if size <= 64 else 2
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    px, py = (x + (sx + 0.5) / ss) * k, (y + (sy + 0.5) / ss) * k
                    if sd_round_rect(px, py, 0, 0, S, S, BG_RADIUS) > 0: continue
                    t = (px + py) / (2 * S)
                    col = [C0[i] + (C1[i] - C0[i]) * t for i in range(3)]
                    white = (any(sd_round_rect(px, py, *b, BAR_RADIUS) <= 0 for b in BARS) or math.hypot(px - DOT[0], py - DOT[1]) <= DOT[2]
                             or sd_round_rect(px, py, *HANDLE) <= 0 or inside_poly(px, py, TIP))
                    if white: col = [255, 255, 255]
                    elif sd_round_rect(px, py, *FERRULE) <= 0: col = [c + (255 - c) * FERRULE_ALPHA for c in col]
                    for i in range(3): acc[i] += col[i]
                    acc[3] += 255
            n = ss * ss
            a = acc[3] / n
            if a > 0: row += bytes([int(acc[0] / (acc[3] / 255)), int(acc[1] / (acc[3] / 255)), int(acc[2] / (acc[3] / 255)), int(a + 0.5)])
            else: row += bytes(4)
        rows.append(bytes(row))
    return rows

def png(rows, size):
    raw = b"".join(b"\x00" + r for r in rows)
    def chunk(tag, data): return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")

def svg():
    rect = lambda x0, y0, x1, y1, r, extra="": f'<rect x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}" rx="{r:g}" fill="#FFFFFF"{extra}/>'
    bars = "".join(rect(*b, BAR_RADIUS) for b in BARS)
    bars += f'<circle cx="{DOT[0]}" cy="{DOT[1]}" r="{DOT[2]}" fill="#FFFFFF"/>' + rect(*HANDLE) + rect(*FERRULE, f' fill-opacity="{FERRULE_ALPHA}"')
    bars += '<path d="M169 158L195 158C196 176 190 188 182 198C174 188 168 176 169 158Z" fill="#FFFFFF"/>'
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">'
            f'<defs><linearGradient id="g" x1="0" y1="0" x2="256" y2="256" gradientUnits="userSpaceOnUse">'
            f'<stop offset="0" stop-color="#{C0[0]:02X}{C0[1]:02X}{C0[2]:02X}"/><stop offset="1" stop-color="#{C1[0]:02X}{C1[1]:02X}{C1[2]:02X}"/></linearGradient></defs>'
            f'<rect width="256" height="256" rx="{BG_RADIUS:g}" fill="url(#g)"/>{bars}</svg>\n')

open("resources/logo.svg", "w", encoding="utf-8").write(svg())
open("docs/logo.png", "wb").write(png(render(256), 256))
sizes = [16, 20, 24, 32, 40, 48, 64, 256]
images = [png(render(s), s) for s in sizes]
ico = struct.pack("<HHH", 0, 1, len(sizes))
offset = 6 + 16 * len(sizes)
for s, data in zip(sizes, images):
    ico += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
    offset += len(data)
open("resources/fimgui.ico", "wb").write(ico + b"".join(images))
print("logo written")
