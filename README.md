# Nomadys-PDF

A minimal, fast PDF reader for Linux (Wayland/X11), driven by vim-style keys.
When opened it shows the PDF and nothing else.

- Engine: [PDFium](https://pdfium.googlesource.com/pdfium/) (Chrome's PDF
  engine), prebuilt binary from
  [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries),
  release `chromium/8066`, in `third_party/pdfium/` (package SHA-256
  `0b43f405477cf2cfc4dbff06905093c3309756c6bca1fb9da99234a2ca97fed2`).
  Built without V8: forms work, but form JavaScript (automatic field
  calculations) is not executed.
- Window and keyboard: SDL3. UI text: any system font via fontconfig and
  FreeType.

## Build and install

System dependencies: `sudo apt install libsdl3-dev libfreetype-dev libfontconfig-dev`. PDFium is downloaded
automatically on the first `make` by `tools/fetch_pdfium.sh` (pinned release,
SHA-256 verified) and is not stored in the repository.

```sh
make                 # builds ./nomadys-pdf
make install         # ~/.local/bin/nomadys-pdf and ~/.local/lib/nomadys-pdf/libpdfium.so
make uninstall
```

## Usage

```sh
nomadys-pdf file.pdf [other.pdf ...]    # each file opens in a tab
```

### Reading

| Key | Action |
|---|---|
| `j` `k` `h` `l`, arrows | scroll |
| `Ctrl-d` `Ctrl-u` / `Ctrl-f` `Ctrl-b`, `Space` | half screen / full screen |
| `J` `K` | next / previous page |
| `gg` `G` `42G` (or `:42`) | first page / last page / page 42 |
| `+` `-` `=` `z` | zoom / fit width / fit page (also `Ctrl` + wheel) |
| `d` | two pages side by side |
| `i` | dark mode (images keep their colours) |
| `/` `n` `N` | search / next / previous (`Esc` clears) |

### Marks and jumps

For going back and forth between places while reading (text, a figure, an
equation, the references):

| Key | Action |
|---|---|
| `\ma`, `\mb`, ... | remember the current position as mark `a`, `b`, ... (`\` is the leader key) |
| `ma`, `mb`, ... | jump to that mark |
| `mm` | back to where you were before the last jump; again to return |
| `Ctrl-o` / `Ctrl-i` | older / newer position in the jump history (every `gg`, `G`, `42G`, search, mark and contents jump) |
| `\l` | panel: marks, highlights/comments and the PDF's contents; `j` `k` move, `Enter` goes, `r` renames a mark or edits a comment, `x` deletes, `Esc` closes |

Marks and the last reading position are stored outside the PDF, in
`~/.local/share/nomadys-pdf/marks/`; a file reopens where you stopped.

### Highlights and comments

Everything by keyboard. Highlights are standard PDF annotations: other readers
(Acrobat, Okular, Evince, ...) show them and their comments.

| Key | Action |
|---|---|
| `v` | text cursor (blinking): `h j k l w b 0 $` move |
| then `v` | start/stop selecting text |
| then `H` / `c` | highlight the selection / highlight it and type a comment (`Enter` saves) |
| `Ctrl-v` | free cursor, for figures and areas: `h j k l` move, `w b J K` move faster |
| then `v`, `H` / `c` | select a rectangle, highlight it / with a comment |
| `c` on a highlight | edit its comment (shown in a box while the cursor is on it) |
| `x` on a highlight | delete it |
| `y` | copy the selected text |
| `Esc` | cancel the selection, then leave the cursor |

Highlights with a comment get a small dot in their corner.

### Forms, tabs, windows

| Key | Action |
|---|---|
| `Ctrl-j` `Ctrl-k` | next / previous form field (also `Tab`) |
| in a field | type; `Space` toggles a checkbox; `Ctrl-a` `Ctrl-z` `Ctrl-v`; `Esc` leaves |
| `t` | show/hide tabs (initial state: `show_tabs` in the config) |
| `Ctrl-,` `Ctrl-.` | previous / next tab |
| `:e file` | open in a new tab (`Tab` completes the path) |
| `Ctrl-n` | new window on the same document (shared edits) |
| `Ctrl-s`, `:w` | save |
| `:q` / `q` | close tab / close window |

A number before a command repeats it (`5j`, `3n`).

## Saving

By default, form entries and highlights are saved automatically about 1 s
after a change (also when leaving a field and when closing). Saving is
incremental: the original content is kept and changes are appended; the file
is written to a temporary file and atomically renamed over the original. With
`autosave = false`, save with `Ctrl-s`/`:w`; closing with pending changes asks
for confirmation.

## Configuration

`~/.config/nomadys-pdf/config`; `config.example` lists every setting with its
default. The main ones:

```ini
theme = dark              # dark | light
autosave = true
restore_position = true
show_tabs = false
subpixel_text = false
highlight_color = #FFE14D
leader = \                # or , ; space
font = JetBrains Mono     # any installed font (fontconfig name)
font_size = 11

[dark]                    # colours of the dark theme; [light] for the light one
background = #101010
tab_active_bg = #2E2E2E
bar_bg = #1E1E1E          # ':' and '/' line
panel_bg = #1C1C1C
comment_bg = #2B2A1E
page_low = 28             # page luminance range in dark mode
page_high = 228
```

## Dark mode

Inverts lightness while keeping hue (red stays red), except in raster image
and gradient regions, which are left untouched. An image covering almost the
whole page (a scanned PDF) is inverted as well. Vector figures (plots saved as
PDF) darken with the background but keep their hues.

## Code layout

One feature per file in `src/`; `nomadys.h` has the shared types and a module map.

| File | Contents |
|---|---|
| `main.c` | windows, tabs, drawing order, event loop, test mode |
| `input.c` | key tokens and **all keymaps** (one table per mode) |
| `config.c` | config file and the colour themes (one table of theme keys) |
| `doc.c` | PDFium document, page cache, form environment, saving |
| `render.c` | layout, page rendering, texture cache, dark mode |
| `forms.c` | form field navigation and typing |
| `caret.c` | text cursor, free cursor, selections |
| `annot.c` | highlights and comments |
| `marks.c` | marks, jump history, last position |
| `search.c` | text search |
| `panel.c` | the marks / notes / contents panel |
| `font.c` | UI text through fontconfig and FreeType |
| `ui.c` | tab bar, prompt, messages, comment box |

To add or change a key, edit a line in the tables at the end of `input.c`,
e.g. `{"<leader>l", a_panel}`. To add a colour, add a field to `Theme`
(`nomadys.h`), its defaults and one `COLOR(name)` line in `config.c`.

## Tests

`tools/make_test_pdf.py` generates `tests/sample.pdf` (text, image, form).
Headless test mode with scripted keys and screenshots:

```sh
SDL_VIDEODRIVER=offscreen ./nomadys-pdf --test-out /tmp/shots \
    --test-script '<C-j>text<Esc><shot:form>vjw<shot:caret>' copy.pdf
```

Tokens: plain characters, any key name from `input.c` (`<Esc>`, `<Enter>`,
`<C-j>`, ...), `<wait:ms>` and `<shot:name>` (saves `name.bmp`).

## Licences

Nomadys-PDF code: MIT (`LICENSE`). PDFium: BSD-3/Apache-2.0
(`third_party/pdfium/LICENSE` and `licenses/`).
