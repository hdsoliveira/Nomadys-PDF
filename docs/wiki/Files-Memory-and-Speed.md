# Files, memory and speed

## What goes where

**Inside the PDF**: your highlights, comments, pictures and form entries,
as standard PDF annotations. Any reader shows them; nothing is lost if you
stop using Nomadys-PDF.

**Outside the PDF**:

| Path | Contents |
|---|---|
| `~/.config/nomadys-pdf/config` | your [configuration](Configuration.md) |
| `~/.config/nomadys-pdf/themes/` | your own themes (`:theme new`) |
| `~/.config/nomadys-pdf/math.tex` | your LaTeX macros for equations in comments |
| `~/.local/share/nomadys-pdf/marks/` | marks, last page and view of each file (one small file per PDF, keyed by its path) |
| `~/.local/share/nomadys-pdf/recent` | recent files (`\o`) |
| `~/.local/share/nomadys-pdf/session` | the tabs open now (`npdf` alone reopens them) |
| `~/.local/share/nomadys-pdf/files` | the list of PDFs found by `\f` |
| `~/.cache/nomadys-pdf/index2/` | the text index for `\/`, `\n` and `:tasks` |
| `~/.cache/nomadys-pdf/math/` | typeset equations |
| `~/.cache/nomadys-pdf/undo/` | copies kept for undoing page changes, deleted when the PDF closes |

The folders follow `XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_CACHE_HOME`
when they are set. Everything under `~/.cache/nomadys-pdf` can be deleted at
any time; it is made again when needed. To remove every trace:

```sh
make uninstall
rm -rf ~/.config/nomadys-pdf ~/.local/share/nomadys-pdf ~/.cache/nomadys-pdf
```

## Saving

- Changes are saved **automatically** about a second after the last change,
  when you leave a form field and when you close the file. With
  `autosave = false`, save with `Ctrl-s` or `:w`; closing with unsaved
  changes then asks first.
- `:w name.pdf` saves a copy under another name. With `autosave` on, the
  open file is saved by itself as well, so to keep the original untouched
  set `autosave = false` before you start.
- The file is written to a temporary file and then renamed over the
  original, so a crash or a full disk never leaves half a PDF.
- The file is **rewritten compactly**: saving never makes a PDF grow beyond
  what was added (0.1 s for a 255 MB paper). A big file waits a little
  longer after a change, so saving never gets in the way of reading.
- Digitally signed PDFs are saved incrementally instead, which keeps the
  signature valid.
- When another program rewrites the PDF (LaTeX, a sync client), it is
  reloaded in place.

## Memory

Made to stay light with big PDFs and many tabs:

- **Page images**: one budget of 128 MB for all tabs and windows; what is
  out of sight is dropped first, never what is on screen. Neighbouring pages
  are prepared ahead only while there is room.
- **Open pages**: at most 8 pages are kept loaded, over all documents (a
  page holds its decoded pictures: a 24-megapixel photo takes about 70 MB).
- **Document caches**: the PDF engine keeps what it parsed until a document
  is closed; Nomadys-PDF quietly reopens documents to free that (a tab out
  of sight after 3 s, the one you read after 15 s without input, only when
  it has no unsaved changes). Nothing changes on screen.
- Freed memory is given back to the system while idle.

Measured with a 255 MB, 26-page paper full of 24-megapixel photos: 93 MB for
the program, about 100–130 MB with eight such papers open in tabs.

## Speed

- **Idle means idle**: no timers, no polling; the program sleeps until you
  press a key or something changes.
- **Heavy pages** (thousands of vector strokes, dense plots) are drawn a
  slice at a time, about 30 ms per frame and the rest while idle, showing
  the previous zoom meanwhile, so scrolling and zooming never freeze.
  Zooming ten steps on such a page draws only the last one (5 s instead of
  38 s in a test with 120 000 strokes).
- **The page grid** draws its thumbnails while idle, a few at a time, so it
  opens at once even for long books.
- **Presentations** prepare the next slide while you talk.
- **The library index** is built by background processes at low priority:
  about 16 ms per PDF the first time, and only changed files afterwards.
  Searching 2 700 PDFs answers in under 0.2 s.
- **Finding PDFs** (`\f`): the whole home folder (14 000 PDFs) is scanned in
  under a second.

## Robustness

Every release is tested with AddressSanitizer and UndefinedBehaviorSanitizer
on a collection of broken PDFs (truncated, garbage, empty, without pages,
with cyclic outlines, huge and tiny pages, 3000 pages, many form fields)
and on long random key sequences through every mode. The library index runs
in separate processes, so a PDF that crashes the PDF engine only skips that
file.
