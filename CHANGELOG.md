# Changelog

## 0.5.0 — the first public release

Nomadys-PDF is a minimal, keyboard-driven PDF reader for Linux, made for
people who read papers for hours. Everything below is in this first release.

### Reading
- vim keys, counts, marks and a jump history; `f` follows any link by its
  letters, `F` previews a citation, figure or equation in place.
- Fit width or page, two pages side by side, rotation; each file reopens where
  you left it, with its zoom and dark mode.
- A dark mode that recolours text and plots but keeps photographs and the
  pictures you placed.
- Fifteen themes, each with a light and a dark side, checked for contrast; the
  menus and bars are always in the theme's dark mode, whatever the pages show.
- Tabs, split view, several windows, sessions; presentation mode (`F5`).
- Light on memory and fast on heavy pages (they render a slice at a time).

### Annotating
- Highlights in five colours and comments with LaTeX equations, saved as
  standard PDF annotations.
- Tasks: `t` puts a highlight on your task list, `d` ticks it.
- Replies to reviewers (`r`) and the response letter (`:response`).
- **Pictures**: place images and signatures; move, resize, turn (90° or any
  angle), mirror and crop them, **by keyboard or by mouse** (click to select,
  drag, corner and side handles, a knob to turn). The picture is shown from its
  pixels while you drag, so it follows the mouse at once, even on a heavy page.
  Nothing is re-encoded: every change can be undone.
- **Free text** (`T`): write anywhere on a page; the box grows with the text and
  the letters can be resized. It is real Helvetica text (a few bytes a letter,
  selectable and searchable in other readers), kept editable in the PDF.
- Undo and redo everything; filter by colour; export to Markdown; merge a
  co-author's notes.
- Page grid: choose, turn, move, delete, extract and insert pages.

### Searching and comparing
- Search in the PDF, in every PDF you own (`\/`) and in every comment (`\n`).
- `:diff old.pdf` compares two versions word by word **and the notes** (new,
  changed and removed highlights, comments, text boxes and pictures).
- `:w name.pdf` saves a copy under another name (`:w!` replaces a file).
- **A backup**: before the first save of a session the file is kept next to it
  as `name.pdf.bak` (a hard link: instant, whatever the size); `backup = false`
  turns it off.

### For writing with LaTeX
- SyncTeX both ways with Vim / Neovim (VimTeX), automatic reload; `Y` quotes
  with the page number; `:bibtex`.

### Installing
- `.deb` (Debian, Ubuntu, Linux Mint, Pop!_OS), AppImage, an Arch package
  (`PKGBUILD` for the AUR) and a build from source. Tried on Ubuntu 22.04 and
  24.04, Debian 12 and 13, Fedora, Arch and openSUSE Tumbleweed, under X11 and
  Wayland.

### Known limits
- Linux only (Wayland and X11).
- Free text: letters outside the Latin alphabet (Chinese, Japanese, Korean,
  Greek, Cyrillic) show as `?` for now; no bold, italic or equations yet.
- Autosave is on by default and saves the open file by itself (the file as it
  was is kept as `name.pdf.bak`); `:w name.pdf` makes a copy, but the open file
  is saved too. Set `autosave = false` to keep an original untouched.
- `:diff` compares text and notes, not figures.
- Pages turned with `r` hide the live preview of free text while you write it.
