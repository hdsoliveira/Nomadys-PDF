# Nomadys-PDF

<img src="docs/icon.svg" width="120" align="right" alt="the Nomadys-PDF icon">

**For people who read PDFs for hours on end.**
If you spend your days inside papers, books and reports, you know the
choice between readers bloated with toolbars, side panels, accounts and cloud sync,
and bare viewers that leave you alone with the page. Nomadys-PDF sits in
between. It is a minimal, fast, keyboard-driven reader for Linux (the page gets
the whole window, and everything is one or two keys away) that brings the
quality-of-life tools that make long reading sessions easier.

Made first for **researchers who love keyboard-driven tools**, but anyone
can use it. The mouse scrolls, follows links, selects text and moves, resizes
and turns pictures. The backslash `\` is the leader key, which starts many
functions, and typing `\c` opens a menu with every key.

Free and open source, for everyone. If it saves you time, you can
buy me a coffee.

[![Buy me a coffee on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/hugooliveira35800)

![Highlighting, and a comment with an equation](site/assets/demos/highlight.gif)

## Why I am developing it

I could not find a PDF reader for Linux that fit the way I work.
[Zathura](https://pwmt.org/projects/zathura/) is great, but too limited for
the daily work of a researcher who reads and writes papers.
[Sioyek](https://sioyek.info/) is very good, but it did not give me the flow
I wanted either. For anything beyond reading, I kept falling back on Master
PDF Editor, PDF Studio or even online tools to do basic things.

What I wanted was simple to say.

- **vim-friendly** through and through, with every action from the keyboard and
  the keys a vim user already knows;
- a **minimal** window, with the page and nothing else on screen;
- **tabs**, split view and sessions;
- **themes** I can switch in a second, with a dark mode that respects figures;
- **highlights and comments** saved in the PDF itself, so every other reader
  shows them, with LaTeX equations in the comments;
- a real **search**, in the paper and across all the papers I own;
- a good companion for **LaTeX and Vim** (SyncTeX, auto-reload);
- the basic things done in place, such as **printing**, filling in **forms**,
  moving or extracting **pages**, placing a **signature** or an image.

Nothing ticked all of these boxes, so I started writing my own PDF reader.
With the help of [Claude](https://claude.com/product/claude-code) I could
move forward a lot, and quickly. I hope it is as useful to you as it is to me.

*Nomadys* means a nomad, with no fixed place, always looking for something
better. The icon is a tent at home anywhere, a trail towards the mountain
and the rising sun, and a page lit in the doorway, because reading goes wherever
you go.

## What it does

**Reading**
- vim keys such as `j` `k`, `gg` `G`, `42G`, counts (`5j`), marks and a jump history
  (`Ctrl-o` / `Ctrl-i`)
- zoom, fit width or page, two pages side by side, rotation; each file
  reopens where you left it, with its own zoom and dark mode
- `f` follows any link by typing its letters; `F` **previews** a citation,
  figure or equation in place, without losing your page
- a panel with the table of contents, your marks and all your highlights
- a dark mode that recolours text and plots but keeps photographs and the
  pictures you placed, with readable highlights and firm letters
- presentation mode (`F5`)

**Annotating**
- a text cursor and a free cursor for figures, highlights in five colours,
  comments with **LaTeX equations** typeset as you write
- all stored as standard PDF annotations (Acrobat, Okular, Zotero… show them)
- **tasks** (`t` puts a highlight or comment on your task list, with a check
  box in the margin, and `d` marks it done). The panel's Tasks tab lists what is
  left and what is done, `:tasks` what is still to do in all your PDFs, and
  Markdown export writes `- [ ]` / `- [x]`
- undo and redo everything; filter by colour; export to Markdown
- merge a co-author's highlights and comments into your copy
- **answer the reviewers** with `r`, which replies to a comment they left in the PDF
  (Acrobat and Okular show it in the thread), while `:response` writes the
  response letter in Markdown, with each point, your reply and what is left
- place images and signatures; move, resize, turn (any angle), mirror and crop
  them, by keyboard or mouse
- write text anywhere on a page (`T`), in a box that grows with the text, as real
  Helvetica text that takes a few bytes a letter and can be edited again later

**Searching**
- incremental search in the PDF, smart case
- `\/` searches the **text of every PDF you own**, `\n` every comment you
  ever wrote; `\f` finds any PDF by name; `\o` opens recent files
- `:diff old.pdf` compares two versions word by word, and their notes

**Writing with LaTeX**
- SyncTeX both ways with Vim / Neovim (VimTeX), automatic reload
- `Y` copies a quotation with its page, `:bibtex` the paper's BibTeX entry
- copy a figure as an image for slides

**Everything else**
- tabs, split view (`Ctrl-w v`), several windows, sessions
- fifteen themes (gruvbox, catppuccin, tokyonight, nord, flexoki, github,
  one, ayu, …), each with a light and a dark side, all checked for contrast,
  or your own; any system font
- a page grid (`\g`) where you choose pages one by one (`Space`) or a run (`v`), then
  delete, turn, move them with the arrows, or save just those as a new PDF;
  insert another PDF
- forms, printing through CUPS, password-protected and signed PDFs
- light on memory, and fast on heavy pages (they render a slice at a time)

## See it

| | |
|---|---|
| ![Tasks](site/assets/demos/tasks.gif) | ![Previewing a citation](site/assets/demos/preview.gif) |
| `t` puts a highlight on the task list, and `d` ticks it | `F` previews a reference in place |
| ![Quoting with the page](site/assets/demos/quote.gif) | ![Searching every PDF](site/assets/demos/library.gif) |
| `Y` copies a quotation with its page number | `\/` searches the text of every PDF |
| ![Comparing versions](site/assets/demos/diff.gif) | ![Split view](site/assets/demos/split.gif) |
| `:diff` shows what changed since the submitted version | `Ctrl-w v` puts the figure and the text side by side |
| ![Changing pages](site/assets/demos/pages.gif) | ![Dark mode](site/assets/demos/reading.gif) |
| `\g` chooses pages apart, turns them and moves a block | `i` switches to a dark mode that keeps photographs |
| ![Themes](site/assets/demos/themes.gif) | |
| `\T` tries the themes live | |

### The themes

Each theme has a light and a dark side, and `i` switches between them while reading.

![The fifteen themes, light and dark](docs/themes.png)

## Install

Linux (Wayland or X11). The easiest ways come from the
[releases page](https://github.com/hdsoliveira/Nomadys-PDF/releases/latest).

| | |
|---|---|
| **Debian, Ubuntu, Linux Mint, Pop!_OS** | `sudo apt install ./nomadys-pdf_0.5.0_amd64.deb` |
| **Any distribution** | the AppImage, with `chmod +x Nomadys-PDF-0.5.0-x86_64.AppImage` and then run it |
| **Arch Linux** | `nomadys-pdf` from the AUR |

Every file there has a SHA-256 in `SHA256SUMS`. Tried on Ubuntu 22.04 and
24.04, Debian 12 and 13, Fedora, Arch and openSUSE Tumbleweed, under X11 and
Wayland. You can also build it yourself, on Debian / Ubuntu for example.

```sh
sudo apt install build-essential pkg-config libsdl3-dev libfreetype-dev libfontconfig-dev zlib1g-dev
git clone https://github.com/hdsoliveira/Nomadys-PDF.git
cd Nomadys-PDF
make && make install        # installs npdf in ~/.local/bin
```

The PDF engine, [PDFium](https://pdfium.googlesource.com/pdfium/) (the one
in Chrome), is downloaded and checked automatically by the first `make`.
TeX Live (equations in comments and SyncTeX), `curl` (`:bibtex`) and
CUPS (printing) are optional. `make uninstall` removes everything.

## First steps

```sh
npdf paper.pdf              # or several files: one tab each
npdf                        # your last session
```

The backslash `\` is the **leader key**. It starts many functions, such as
`\c` (help), `\l` (the panel), `\g` (all pages), `\f` (find a PDF) and `\/`
(search all your PDFs). When you do not remember a key, type `\c` and a menu
with every key of the mode you are in appears.

| Key | |
|---|---|
| `\c` | a menu with **every key**, for the mode you are in |
| `j` `k` / `J` `K` / `gg` `G` | scroll / page / first, last |
| `+` `-` `=` / `i` | zoom, fit width / dark mode |
| `/` then `n` `N` | search |
| `v`, `v`, then `C` `V` `B` `N` `M` / `c` | highlight in a colour / with a comment |
| `u` / `Ctrl-r` | undo / redo |
| `f` / `F` | follow / preview a link |
| `\l` / `\g` | panel / all pages |
| `\/` / `\f` | search all your PDFs / find a PDF |
| `:w` / `:q` | save (it also saves by itself) / close; `:w name.pdf` saves a copy |
| `Esc` | back to reading, from anywhere |

## Limits, honestly

- Linux only for now (Wayland and X11).
- Free text shows letters outside the Latin alphabet (Chinese, Japanese, Greek,
  Cyrillic…) as `?` for now, and bold, italic and equations are planned.
- Autosave is on, so the open file is saved by itself, and the file as it was
  before the first save of a session is kept next to it as `name.pdf.bak`
  (`backup = false` turns that off). `:w name.pdf` makes a copy, and
  `autosave = false` keeps an original untouched.
- `:diff` compares text and notes, not figures.

## Documentation

The [wiki](docs/wiki/Home.md) explains every feature, with the full
[keyboard reference](docs/wiki/Keyboard-Reference.md), the
[commands](docs/wiki/Commands.md) and the
[configuration](docs/wiki/Configuration.md). The same pages are part of the
website in [`site/`](site/README.md).

A taste of the configuration (`~/.config/nomadys-pdf/config`) looks like this.

```ini
include = catppuccin          # the theme (\T chooses one live)
font = JetBrains Mono
font_size = 11
pdf_dirs = ~/Papers, ~/Books  # where \f and \/ look
```

## Little things that add up

- **Reopens where you left off.** Each file keeps its page, zoom and dark
  mode, and `npdf` alone brings back your last session
- **Never lose your place.** `Ctrl-o` jumps back after any jump, and `F`
  previews a reference without leaving the page
- **Every link by its letters.** Press `f`, then the letters shown on the link
- **Find any paper.** `\f` searches by name, `\/` by text, and `\o` opens the recent ones
- **Notes that travel.** Highlights and comments are saved in the PDF and shown by
  every other reader
- **A to-do list in your papers.** `t` puts a highlight on it, `d` ticks it, and
  `:tasks` shows what is left in all of them
- **Undo anything.** Highlights, comments, images, text and deleted pages all come back
- **Easy on the eyes.** A dark mode that keeps photographs, fifteen themes,
  menus always in the theme's dark side, and any system font
- **No learning wall.** The `\` leader key starts many functions, `\c` shows a menu
  with every key of the mode you are in, and messages say what the next key does

## Support

Nomadys-PDF has no ads, no accounts and no paid version. I build it in my
free time and share it with everyone. A star helps others find it, and every
bug report or idea in the
[issues](https://github.com/hdsoliveira/Nomadys-PDF/issues) makes it better.
If it saves you time, you can also buy me a coffee.

[![Buy me a coffee on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/hugooliveira35800)

## Development

Written in C (SDL3, FreeType, fontconfig, PDFium, stb). `make` builds
`./npdf`. Every change is checked with AddressSanitizer and
UndefinedBehaviorSanitizer on a collection of broken PDFs and random key
sequences, with `tests/qa/run.sh`. The code is organised one feature per file in
`src/`, with the shared types in `src/nomadys.h`.

For releases, `tools/package.sh` builds the AppImage and the `.deb` in an Ubuntu
22.04 container (with podman), so they run on that and anything newer;
`tools/distro-test.sh` then tries them on Ubuntu, Debian, Fedora, Arch and
openSUSE, each in its own container, both without a window and in a real one on
X11 (Xvfb) and Wayland (headless weston), copying text and following links.
The AUR package is `packaging/aur/PKGBUILD`, and `tools/aur-build.sh` builds it
from this tree in an Arch container, with namcap, and `tools/distro-test.sh`
then tries it too.

## Licence

MIT (`LICENSE`), by Hugo Oliveira. The libraries it uses (PDFium and the
libraries inside it, SDL3, FreeType, fontconfig, zlib, stb) and the colour
themes it follows are all under permissive licences, listed with their texts in
[`licenses/`](licenses/README.md). `make install`
copies them to `share/doc/nomadys-pdf/`.

Portions of this software are copyright © The FreeType Project
(www.freetype.org). All rights reserved. This software is based in part on
the work of the Independent JPEG Group.

---

<p align="center">
Nomadys-PDF is free and stays free. If it saves you time, you can help it grow:<br>
<a href="https://ko-fi.com/hugooliveira35800"><img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Buy me a coffee on Ko-fi"></a>
</p>
