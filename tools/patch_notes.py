# Prints the CHANGELOG.md section for a version; exits with an error when it is missing or empty.
# Usage: python tools/patch_notes.py 1.0.0 [CHANGELOG.md]
import re, sys

version = sys.argv[1].lstrip("vV")
path = sys.argv[2] if len(sys.argv) > 2 else "CHANGELOG.md"
lines = open(path, encoding="utf-8").read().splitlines()
out, inside = [], False
for line in lines:
    m = re.match(r"^##\s+\[?v?([0-9][0-9A-Za-z.\-]*)", line)
    if m:
        if inside:
            break
        inside = m.group(1) == version
        continue
    if inside:
        out.append(line)
text = "\n".join(out).strip()
if not text:
    sys.exit(f"CHANGELOG.md has no patch notes for {version}. Add a '## {version}' section before releasing.")
sys.stdout.reconfigure(encoding="utf-8")
print(text)
