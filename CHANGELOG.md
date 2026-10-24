# Changelog

## 0.7.0
- **Back to the tab you came from**: `Backspace` (and again to return).
- **Smooth scrolling** for `Ctrl-d` `Ctrl-u` `Ctrl-f` `Ctrl-b` and `Space`:
  `smooth_scroll = true` (off by default).
- **The matches of a search as a list**, with the words around each: `\s`
  after `/`, or `:hits words`; `Enter` goes to the match.
- **Notes of a whole folder** in one Markdown file: `:exportall [folder]`
  (`all-notes.md`), or `npdf --export-dir folder`.
- **Saved forms**: `u` undoes a whole `:form` (`Ctrl-r` redoes it);
  `:form P80 empty` (or `e` in the `\F` menu) fills only the empty fields; a
  list box keeps every option chosen; field names match when they differ only
  in capitals, spaces or punctuation (`Full name`, `full_name`).
- **Mouse**: the middle button drags the page; `Shift` + drag marks a rectangle
  and copies it as an image.
- **`:set name value`** changes a setting now and keeps it in the config file.
- **New version notice**: `:update` asks GitHub (with `curl`); with
  `check_updates = true` it asks once a day. Nothing is installed.
- A formula typeset by two npdf at once no longer fails with "LaTeX error".

## 0.6.0
- **Saved forms.** Fill a form once and keep the answers under a name
  (`:form save P80`, or `n` in the `\F` menu). `\F` (or `:form`) lists them;
  `Enter` on one, or `:form P80`, fills every blank of any PDF that has fields
  of those names. Text, multi-line text, combo and list boxes, checkboxes and
  radio buttons. `x` / `:form delete NAME` removes one.
- **Mouse in the free cursor.** In `Ctrl-v` the pointer is a cross that follows
  the mouse; click to put the cross, drag to mark a rectangle. The same in the
  text cursor (`v`): the cursor follows the pointer. `hjkl` still work.
- **Walk through what you type.** In `:`, `/` and the comment box, `←` `→`,
  `Home` `End` (`Ctrl-a` `Ctrl-e`), `Delete`, `Ctrl-w` (word) and `Ctrl-u`
  (to the start) edit the line where the cursor is.
- In the free cursor the red cross is the only pointer (the system one is hidden).
- Fixed a "LaTeX error" in comments when two npdf typeset the same equation
  at once.

## 0.5.1
- The page and the total (`3/50`) next to the mode badge.
- `↑` and `↓` in the `:` and `/` lines bring back what you typed before, also
  in the next run.
- `npdf --tab file.pdf` opens the file as a tab of the npdf that is running
  (for neomutt's mailcap, see the FAQ).

## 0.5.0, the first public release

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
- Tasks, where `t` puts a highlight on your task list and `d` ticks it.
- Replies to reviewers (`r`) and the response letter (`:response`).
- **Pictures.** Place images and signatures; move, resize, turn (90° or any
  angle), mirror and crop them, **by keyboard or by mouse** (click to select,
  drag, corner and side handles, a knob to turn). The picture is shown from its
  pixels while you drag, so it follows the mouse at once, even on a heavy page.
  Nothing is re-encoded, so every change can be undone.
- **Free text** (`T`). Write anywhere on a page, and the box grows with the text and
  the letters can be resized. It is real Helvetica text (a few bytes a letter,
  selectable and searchable in other readers), kept editable in the PDF.
- Undo and redo everything; filter by colour; export to Markdown; merge a
  co-author's notes.
- A page grid to choose, turn, move, delete, extract and insert pages.

### Searching and comparing
- Search in the PDF, in every PDF you own (`\/`) and in every comment (`\n`).
- `:diff old.pdf` compares two versions word by word **and the notes** (new,
  changed and removed highlights, comments, text boxes and pictures).
- `:w name.pdf` saves a copy under another name (`:w!` replaces a file).
- **A backup.** Before the first save of a session the file is kept next to it
  as `name.pdf.bak` (a hard link, so it is instant whatever the size), and `backup = false`
  turns it off.

### For writing with LaTeX
- SyncTeX both ways with Vim / Neovim (VimTeX), automatic reload; `Y` quotes
  with the page number; `:bibtex`.

### Safety
- A PDF, or a file next to it, is never trusted. The file name that SyncTeX
  gives for an inverse search must be a plain existing file before an editor
  command sees it, formulas in comments are typeset by LaTeX that cannot read or
  write other files, the process that indexes your library cannot use the
  network, start programs or write outside its own folder (Landlock and
  seccomp), the socket between windows answers only you, and `mailto:` links
  that ask to attach a file are not opened. Numbers read from annotations are
  checked before they are used. The PDF engine is PDFium chromium/8086.
- `:extract` and the page grid never write over a file that is already there.
- `tests/qa/security.py` and `tests/qa/mutate.py` (400 spoiled files under the
  sanitizers) keep these checks running.

### Installing
- `.deb` (Debian, Ubuntu, Linux Mint, Pop!_OS), AppImage, an Arch package
  (`PKGBUILD` for the AUR) and a build from source. Tried on Ubuntu 22.04 and
  24.04, Debian 12 and 13, Fedora, Arch and openSUSE Tumbleweed, under X11 and
  Wayland.

### Known limits
- Linux only (Wayland and X11).
- Free text shows letters outside the Latin alphabet (Chinese, Japanese, Korean,
  Greek, Cyrillic) as `?` for now, and has no bold, italic or equations yet.
- Autosave is on by default and saves the open file by itself (the file as it
  was is kept as `name.pdf.bak`); `:w name.pdf` makes a copy, but the open file
  is saved too. Set `autosave = false` to keep an original untouched.
- `:diff` compares text and notes, not figures.
- Pages turned with `r` hide the live preview of free text while you write it.
