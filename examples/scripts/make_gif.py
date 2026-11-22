# Writes a small animated GIF (rotating colored stripes) without external libraries.
import math, struct, sys

W = H = 64
FRAMES = 12
palette = [(20, 20, 40), (255, 80, 140), (60, 140, 255), (40, 230, 170)]

def lzw(pixels, min_size=2):
    # Emit a clear code every few symbols so the code size stays constant (simple, valid LZW).
    clear, end = 1 << min_size, (1 << min_size) + 1
    size = min_size + 1
    codes = []
    for i, p in enumerate(pixels):
        if i % ((1 << min_size) - 2 or 1) == 0:
            codes.append(clear)
        codes.append(p)
    codes.append(end)
    out, acc, nbits = bytearray(), 0, 0
    for c in codes:
        acc |= c << nbits
        nbits += size
        while nbits >= 8:
            out.append(acc & 0xFF); acc >>= 8; nbits -= 8
    if nbits: out.append(acc & 0xFF)
    blocks = bytearray()
    for i in range(0, len(out), 255):
        chunk = out[i:i + 255]
        blocks += bytes([len(chunk)]) + chunk
    return bytes([min_size]) + blocks + b"\x00"

data = bytearray(b"GIF89a" + struct.pack("<HHBBB", W, H, 0xF1, 0, 0))
for c in palette: data += bytes(c)
data += b"\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00"   # loop forever
for f in range(FRAMES):
    a = f / FRAMES * math.pi
    px = []
    for y in range(H):
        for x in range(W):
            u = (x - W / 2) * math.cos(a) + (y - H / 2) * math.sin(a)
            px.append(int((u + 64) / 10) % 4)
    data += b"\x21\xF9\x04\x00" + struct.pack("<H", 8) + b"\x00\x00"   # 80 ms
    data += b"\x2C" + struct.pack("<HHHHB", 0, 0, W, H, 0)
    data += lzw(px)
data += b"\x3B"
open(sys.argv[1], "wb").write(data)
print(len(data), "bytes")
