#!/usr/bin/env python3
"""Draws docs/themes.png: every theme in themes/, light and dark side by side,
on the demo paper, with npdf's headless test mode.

    python3 site/demos/themes.py

Needs: the npdf binary (make), site/demos/paper/paper.pdf (make_demos.py
builds it), Pillow. Works under site/demos/work/themes (not committed).
"""
import os
import shutil
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
NPDF = ROOT / "npdf"
WORK = HERE / "work" / "themes"
PAPERS = [HERE / "paper" / "paper.pdf", HERE / "paper" / "paper-v1.pdf"]
OUT = ROOT / "docs" / "themes.png"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
W, H = 760, 520          # one screenshot
SCALE = 0.5              # in the picture
COLS = 3                 # themes a row (each: light | dark)


def shoot(theme, dark):
    work = WORK / f"{theme}-{'dark' if dark else 'light'}"
    shutil.rmtree(work, ignore_errors=True)
    cfg = work / "cfg" / "nomadys-pdf"
    cfg.mkdir(parents=True)
    (cfg / "config").write_text(f"include = {theme}\nfont_size = 12\nrestore_position = false\n"
                                "remember_view = false\nzoom = 55\n")
    env = dict(os.environ, XDG_CONFIG_HOME=str(work / "cfg"), XDG_DATA_HOME=str(work / "data"),
               XDG_CACHE_HOME=str(work / "cache"), HOME=str(work), SDL_VIDEODRIVER="offscreen")
    script = f"<wait:300>{':dark' if dark else ':light'}<Enter>tgg<wait:600><shot:shot>"
    subprocess.run([str(NPDF), "--test-size", f"{W}x{H}", "--test-out", str(work), "--test-script", script]
                   + [str(p) for p in PAPERS], env=env, check=True, timeout=120,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return Image.open(work / "shot.bmp").convert("RGB")


def main():
    themes = sorted(p.stem for p in (ROOT / "themes").glob("*.conf"))
    w, h = int(W * SCALE), int(H * SCALE)
    gap, label = 10, 30
    cell_w = 2 * w + gap
    rows = (len(themes) + COLS - 1) // COLS
    sheet = Image.new("RGB", (COLS * cell_w + (COLS + 1) * 2 * gap, rows * (h + label + gap) + gap), "white")
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype(FONT, 17)
    for i, theme in enumerate(themes):
        x = 2 * gap + (i % COLS) * (cell_w + 2 * gap)
        y = gap + (i // COLS) * (h + label + gap)
        draw.text((x, y + 4), f"{theme}  ·  light | dark", fill=(40, 40, 40), font=font)
        for k, dark in enumerate((False, True)):
            shot = shoot(theme, dark).resize((w, h), Image.LANCZOS)
            sheet.paste(shot, (x + k * (w + gap), y + label))
        print(theme)
    sheet.save(OUT, optimize=True)
    print(OUT)


if __name__ == "__main__":
    main()
