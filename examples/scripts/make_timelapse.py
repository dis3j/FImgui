# Builds a GIF from frames recorded by:  Fimgui.exe --ops-stream ops.jsonl [framesPerOp] --record <dir> [every] [tail]
# Usage: python examples/scripts/make_timelapse.py <frames dir> <out.gif> [width=960] [ms per frame=50]
import glob, os, sys
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
width = int(sys.argv[3]) if len(sys.argv) > 3 else 960
ms = int(sys.argv[4]) if len(sys.argv) > 4 else 50

files = sorted(glob.glob(os.path.join(src, "frame_*.png")))
if not files:
    sys.exit("no frames in " + src)
frames = []
for f in files:
    im = Image.open(f).convert("RGB")
    h = round(im.height * width / im.width)
    im = im.resize((width, h), Image.LANCZOS)
    # UI art: an adaptive palette without dithering keeps flat colors and text clean
    frames.append(im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE))
durations = [ms] * len(frames)
durations[-1] = 1500  # hold the finished menu
frames[0].save(out, save_all=True, append_images=frames[1:], duration=durations, loop=0, optimize=True, disposal=1)
print(f"{out}: {len(frames)} frames, {os.path.getsize(out) / 1e6:.1f} MB")
