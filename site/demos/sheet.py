# contact sheet of a recorded demo: python3 sheet.py NAME OUT.png [N]
import sys, glob
from PIL import Image
name, out = sys.argv[1], sys.argv[2]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 6
fs = sorted(glob.glob(f"work/rec/{name}/seq/*.png"))
pick = [fs[round(i * (len(fs) - 1) / (n - 1))] for i in range(n)]
cols = 3
im = Image.new("RGB", (cols * 640, ((n + cols - 1) // cols) * 400))
for k, f in enumerate(pick):
    im.paste(Image.open(f).resize((640, 400)), ((k % cols) * 640, (k // cols) * 400))
im.save(out)
