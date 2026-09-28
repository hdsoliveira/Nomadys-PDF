#!/usr/bin/env python3
"""Records the demo videos of the website with npdf's headless test mode.

Each demo is a list of steps (keys, how long the frame shows, the keys drawn
in the corner). npdf runs with SDL's offscreen driver, takes one screenshot
per step, and the frames become an MP4 (for the site) and a GIF.

    python3 site/demos/make_demos.py [NAME ...]      (all demos by default)

Needs: the npdf binary (make), pdflatex, ffmpeg, Pillow. Everything is made
under site/demos/work (not committed); the results go to site/assets/demos.
The PDFs are fictional papers made for this (paper/paper.tex and the library
below): no real paper is shown.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
NPDF = ROOT / "npdf"
WORK = HERE / "work"
OUT = ROOT / "site" / "assets" / "demos"
W, H = 1280, 800
FONT_UI = "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf"
FONT_KEY = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

# ---------- the PDFs ----------
LIBRARY = [
    ("buckling-smart-applications-review.pdf", "Buckling-induced smart applications",
     "Snap-through and buckling are usually avoided in design. We review devices that use them on purpose: switches, energy harvesters, grippers and sensors whose stiffness changes abruptly at the instability."),
    ("indentation-pressurised-shells.pdf", "Indentation of pressurised elastic shells",
     "Poking a shell reveals its internal pressure and its stiffness. We show that the indentation stiffness of a thin spherical shell is set by the pressure once it exceeds a threshold, and discuss snap-through at large indentation."),
    ("metamaterial-energy-absorption.pdf", "Multistable metamaterials for energy absorption",
     "Arrays of bistable beams absorb energy through sequential snap-through events. The energy barrier of each unit and its stiffness determine how much energy is trapped, which we tune with the beam geometry."),
    ("morphing-wing-bistable-laminates.pdf", "Bistable composite laminates for morphing wings",
     "Unsymmetric laminates have two stable cylindrical shapes. We use them as morphing surfaces and measure the actuation force required to switch between the shapes, together with the stiffness of each state."),
    ("popper-toy-dynamics.pdf", "The dynamics of the jumping popper",
     "A jumping popper toy stores elastic energy when turned inside out and releases it in a rapid snap-through. High-speed imaging reveals a delay governed by viscoelasticity rather than by inertia."),
    ("soft-valves-snap-through.pdf", "Soft valves that remember: snap-through in fluidic circuits",
     "Soft robots need valves without electronics. A bistable membrane switches between open and closed states when the pressure crosses a threshold, giving memory and logic to fluidic circuits."),
]

TEMPLATE = r"""\documentclass[10pt,twocolumn]{article}
\usepackage[margin=1.8cm]{geometry}\usepackage{amsmath}
\title{\bfseries %(title)s}\author{C.~Wanderer \and D.~Nomadson\\\small Demo University}\date{}
\begin{document}\maketitle
\begin{abstract}%(abstract)s\end{abstract}
\section{Introduction}%(abstract)s The response is governed by the balance between bending and stretching, and the
energy stored before the instability is released when the structure changes shape.
\section{Results}The measured force first increases linearly, then drops as the structure approaches the limit point,
where the stiffness vanishes. Beyond it, the structure moves to its second stable state. The stiffness in that
state is higher, and the energy barrier to return grows with the thickness.
\section{Discussion}These results show how geometry controls the instability. The same principles apply to shells,
beams and plates, and suggest design rules for devices that rely on snap-through.
\end{document}
"""


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def make_pdfs():
    paper = HERE / "paper"
    for job, pre in (("paper-v1", ""), ("paper", r"\def\revised{1}")):
        if not (paper / f"{job}.pdf").exists():
            for _ in range(2):
                run(["pdflatex", "-interaction=batchmode", f"-jobname={job}", pre + r"\input{paper}"],
                    cwd=paper, stdout=subprocess.DEVNULL)
    lib = WORK / "library"
    lib.mkdir(parents=True, exist_ok=True)
    for name, title, abstract in LIBRARY:
        if (lib / name).exists():
            continue
        tex = lib / "tmp.tex"
        tex.write_text(TEMPLATE % {"title": title, "abstract": abstract})
        for _ in range(1):
            run(["pdflatex", "-interaction=batchmode", "tmp.tex"], cwd=lib, stdout=subprocess.DEVNULL)
        (lib / "tmp.pdf").rename(lib / name)
    for f in lib.glob("tmp.*"):
        f.unlink()
    # comments in some of them, as a colleague would leave (for \\n)
    notes = {
        "metamaterial-energy-absorption.pdf": [("sequential snap-through events", "A8E6A3", "Compare with our energy barrier: same scaling with $t^2$?")],
        "popper-toy-dynamics.pdf": [("governed by viscoelasticity", "FF9E9E", "Check: our shells are elastomers too, is the delay the same?")],
        "soft-valves-snap-through.pdf": [("switches between open and closed states", "A7C7F2", "Nice application for the introduction")],
    }
    for name, marks in notes.items():
        tagged = lib / (name + ".annotated")
        if tagged.exists():
            continue
        args = []
        for phrase, color, comment in marks:
            args += [phrase, color, comment]
        run(["mutool", "run", str(HERE / "annotate.js"), str(lib / name), str(lib / "tmp.pdf")] + args,
            stdout=subprocess.DEVNULL)
        (lib / "tmp.pdf").rename(lib / name)
        tagged.touch()
    # a longer document for the page grid
    book = WORK / "collection.pdf"
    if not book.exists():
        parts = [str(HERE / "paper" / "paper.pdf")] + [str(lib / n) for n, _, _ in LIBRARY]
        run(["mutool", "merge", "-o", str(book)] + parts, stdout=subprocess.DEVNULL)


# ---------- a demo: steps -> frames ----------
class Demo:
    def __init__(self, name, files, title, cwd=None, config=""):
        self.name, self.files, self.title = name, files, title
        self.cwd, self.config = cwd, config
        self.steps = []   # (tokens, hold_ms, label, wait_ms)
        self.setup = ""   # keys before the first frame (not shown)

    def key(self, tokens, hold=700, label=None, wait=60):
        self.steps.append((tokens, hold, tokens if label is None else label, wait))
        return self

    def type(self, text, per=85, label=True):
        for ch in text:
            tok = "<lt>" if ch == "<" else ch
            self.steps.append((tok, per, ("typed", text if label is True else label), 30))
        return self

    def pause(self, hold=1200, wait=60):
        self.steps.append(("", hold, None, wait))
        return self


def pretty(label):
    """Key tokens as a person would write them: <C-w>v -> Ctrl-w v."""
    out, i = [], 0
    names = {"<Esc>": "Esc", "<Enter>": "Enter", "<Tab>": "Tab", "<BS>": "⌫", "<lt>": "<", " ": "Space"}
    while i < len(label):
        if label[i] == "<" and ">" in label[i:]:
            j = label.index(">", i)
            t = label[i:j + 1]
            if t in names:
                out.append(names[t])
            elif t.startswith("<C-"):
                out.append("Ctrl-" + t[3:-1])
            else:
                out.append(t[1:-1])
            i = j + 1
        else:
            out.append(label[i])
            i += 1
    return out


def overlay(img, label, title):
    """The keys just pressed, as keycaps in the bottom-right corner."""
    d = ImageDraw.Draw(img, "RGBA")
    if label:
        font = ImageFont.truetype(FONT_KEY, 22)
        keys = [label[1]] if isinstance(label, tuple) else pretty(label)  # typed text: one cap
        pad, gap, x = 12, 8, 0
        sizes = [d.textbbox((0, 0), k, font=font) for k in keys]
        widths = [max(34, b[2] - b[0] + 2 * pad) for b in sizes]
        total = sum(widths) + gap * (len(keys) - 1)
        x = (W - total) / 2  # top centre: clear of messages and pane labels
        y = 26
        d.rounded_rectangle((x - 14, y - 12, x + total + 14, y + 52), 14, fill=(20, 22, 30, 200))
        for k, wdt in zip(keys, widths):
            d.rounded_rectangle((x, y, x + wdt, y + 40), 8, fill=(250, 250, 252, 255), outline=(170, 175, 190, 255), width=2)
            tb = d.textbbox((0, 0), k, font=font)
            d.text((x + (wdt - (tb[2] - tb[0])) / 2, y + 6), k, font=font, fill=(25, 28, 38, 255))
            x += wdt + gap
    return img


def record(demo):
    work = WORK / "rec" / demo.name
    shutil.rmtree(work, ignore_errors=True)
    (work / "frames").mkdir(parents=True)
    cfg = work / "cfg" / "nomadys-pdf"
    cfg.mkdir(parents=True)
    (cfg / "config").write_text(
        "font = Inter\nfont_size = 12\npdf_dirs = %s\nrestore_position = false\nremember_view = false\n%s"
        % (WORK / "library", demo.config))
    data = work / "data"
    data.mkdir()
    fonts = Path.home() / ".local/share/fonts"
    if fonts.exists():
        os.symlink(fonts, data / "fonts")
    env = dict(os.environ, XDG_CONFIG_HOME=str(work / "cfg"), XDG_DATA_HOME=str(data),
               XDG_CACHE_HOME=str(work / "cache"), SDL_VIDEODRIVER="offscreen")
    script = "<wait:400>" + demo.setup
    for i, (tokens, hold, label, wait) in enumerate(demo.steps):
        script += tokens + (f"<wait:{wait}>" if wait else "") + f"<shot:f{i:04d}>"
    script += "<wait:100>"
    files = [str(f) for f in demo.files]
    run([str(NPDF), "--test-size", f"{W}x{H}", "--test-out", str(work / "frames"), "--test-script", script] + files, env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600, cwd=demo.cwd or work)
    # frames with their keys, repeated to their duration at 25 fps
    seq = work / "seq"
    seq.mkdir()
    n = 0
    fps = 25
    for i, (tokens, hold, label, wait) in enumerate(demo.steps):
        src = work / "frames" / f"f{i:04d}.bmp"
        if not src.exists():
            continue
        img = Image.open(src).convert("RGB").resize((W, H))
        img = overlay(img, label, demo.title)
        for _ in range(max(1, round(hold / 1000 * fps))):
            img.save(seq / f"{n:05d}.png")
            n += 1
    OUT.mkdir(parents=True, exist_ok=True)
    mp4 = OUT / f"{demo.name}.mp4"
    run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(fps), "-i", str(seq / "%05d.png"),
         "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "24", "-preset", "slow", "-movflags", "+faststart",
         str(mp4)])
    gif = OUT / f"{demo.name}.gif"
    palette = work / "palette.png"
    run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(mp4), "-vf",
         "fps=12,scale=880:-1:flags=lanczos,palettegen=max_colors=128:stats_mode=diff", str(palette)])
    run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(mp4), "-i", str(palette), "-lavfi",
         "fps=12,scale=880:-1:flags=lanczos[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle",
         str(gif)])
    # a still for the page (the poster of the video)
    last = sorted(seq.glob("*.png"))
    Image.open(last[len(last) * 2 // 3]).save(OUT / f"{demo.name}.jpg", quality=85)
    print(f"{demo.name}: {n / fps:.1f} s, mp4 {mp4.stat().st_size // 1024} KB, gif {gif.stat().st_size // 1024} KB")


# ---------- the demos ----------
def demos():
    paper = WORK / "paper.pdf"          # copies: the demos write highlights into them
    lib = WORK / "library"
    out = []

    d = Demo("reading", [paper], "Reading")
    d.pause(900).key("j", 220).key("j", 220).key("j", 220).key("j", 220).key("+", 500).key("+", 700)
    d.key("=", 800).key("i", 1700, wait=400).key("J", 2200, wait=300).key("K", 800).key("i", 1300, wait=300)
    out.append(d)

    d = Demo("highlight", [paper], "Highlights and comments")
    d.pause(700).key("v", 800).key("8j", 700).key("0", 500).key("v", 600)
    for _ in range(7):
        d.key("e", 170)
    d.key("B", 1300)
    d.key("j", 350).key("0", 350).key("v", 400)
    for _ in range(9):
        d.key("e", 150)
    d.key("c", 900, wait=200)
    d.type("Barrier grows as $\\Delta E \\propto H^2$, see Fig. 1", per=55)
    d.pause(2600, wait=2500)
    d.key("<Enter>", 1400, wait=300)
    d.key("<Esc><Esc>", 600).key("\\l4", 2600, label="\\l 4", wait=300)
    out.append(d)

    d = Demo("preview", [paper], "Reference preview")
    d.setup = "jjjjjj"
    d.pause(700).key("F", 1400).key("d", 2600, wait=300).key("j", 500).key("<Esc>", 600)
    d.key("F", 900).key("f", 2400, wait=300).key("<Enter>", 1800, wait=300).key("<C-o>", 1400, wait=200)
    out.append(d)

    d = Demo("library", [paper], "Search every PDF")
    d.pause(600).key("\\/", 900, label="\\/", wait=3000)
    d.type("snap-through stiffness", per=75)
    d.pause(1600, wait=400).key("<C-j>", 500).key("<C-j>", 700).key("<Enter>", 2400, wait=400)
    d.key("<Esc>", 500).key("\\n", 2800, label="\\n", wait=600).key("<C-j>", 900).key("<Enter>", 2400, wait=400)
    out.append(d)

    d = Demo("split", [paper], "Split view", cwd=lib)
    d.pause(700).key("<C-w>v", 1400, wait=200).key("J", 1300, wait=300).key("<C-w>w", 800)
    d.key("j", 250).key("j", 250).key("j", 250).key("j", 600)
    d.key(":", 300)
    d.type("vsplit pop", per=70, label=":vsplit pop")
    d.key("<Tab>", 900, label="Tab").key("<Enter>", 2400, wait=500)
    out.append(d)

    d = Demo("diff", [paper], "Compare versions", cwd=WORK)
    d.pause(600).key(":", 300)
    d.type("diff paper-v1.pdf", per=60, label=":diff paper-v1.pdf")
    d.key("<Enter>", 2200, wait=600).key("]c", 1900).key("]c", 1900).key("]c", 1900).key("]c", 2200)
    out.append(d)

    d = Demo("pages", [WORK / "collection.pdf"], "Changing pages")
    d.pause(500).key("\\g", 1500, label="\\g", wait=800)
    # pages apart: Space on each, then turn them together
    d.key(" ", 700, label="Space").key("l", 300).key("l", 300).key("l", 400).key(" ", 900, label="Space")
    d.key("r", 1500, wait=300).key("u", 1300, wait=300)
    # a run of pages, walked along with the arrows
    d.key("h", 300).key("h", 350).key("v", 500).key("l", 700)
    d.key("<Right>", 1100, label="\u2192", wait=300).key("<Right>", 1100, label="\u2192", wait=300)
    d.key("<Right>", 1300, label="\u2192", wait=300).key("x", 1500, wait=300).key("u", 1600, wait=300)
    d.key("<Esc>", 900)
    out.append(d)

    d = Demo("themes", [paper], "Themes")
    d.pause(500).key("\\T", 1300, label="\\T", wait=300)
    for _ in range(5):
        d.key("j", 900, wait=150)
    d.key("i", 1300, wait=150).key("j", 900, wait=150).key("j", 900, wait=150).key("<Enter>", 1200)
    out.append(d)

    d = Demo("quote", [paper], "Quoting")
    d.pause(500).key("v", 600).key("8j", 500).key("0", 400).key("v", 500)
    for _ in range(7):
        d.key("e", 150)
    d.key("Y", 3200, wait=100)
    out.append(d)
    return out


def fresh_copies():
    shutil.copy(HERE / "paper" / "paper.pdf", WORK / "paper.pdf")
    shutil.copy(HERE / "paper" / "paper-v1.pdf", WORK / "paper-v1.pdf")


if __name__ == "__main__":
    if not NPDF.exists():
        sys.exit("build npdf first: make")
    make_pdfs()
    wanted = sys.argv[1:]
    for demo in demos():
        if not wanted or demo.name in wanted:
            fresh_copies()
            record(demo)
