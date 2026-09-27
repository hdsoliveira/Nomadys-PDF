# Nomadys-PDF

<img src="docs/icon.svg" width="128" align="right" alt="the Nomadys-PDF icon">

*Nomadys*: a nomad, with no fixed place, always looking for something better.
The icon is a tent at home anywhere, a trail towards the mountain and the
rising sun, and a page lit in the doorway: reading goes wherever you go.
`\a` in the program shows the version and how to reach the author.

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
make                 # builds ./npdf
make install         # ~/.local/bin/npdf and ~/.local/lib/nomadys-pdf/libpdfium.so
make uninstall
```

## Usage

```sh
npdf file.pdf [other.pdf ...]    # each file opens in a tab
```

Press `\c` for the help screen: every key, in tabs like the panel (Reading,
Text cursor, Free cursor, Comments, Panel, Preview, Commands, Forms), grouped
and drawn as keycaps. It opens on the tab of the mode you are in; `Tab` / `h`
`l` / `1`–`8` switch tabs, `j` `k` move, `/` searches every tab, `Esc` closes.
It is generated from the keymaps in `input.c`, so it is always up to date.

A small coloured badge in the bottom-left corner shows the mode: `NORMAL`,
`CURSOR`, `VISUAL` (selecting), `AREA`, `INSERT` (filling a form field),
`COMMAND`, `SEARCH`, `COMMENT`, `PANEL`, `HELP`. Its colours come from the theme
(`mode_normal`, `mode_cursor`, `mode_visual`, `mode_area`, `mode_insert`,
`mode_command`); `mode_indicator = false` hides it.

### Reading

| Key | Action |
|---|---|
| `j` `k` `h` `l`, arrows | scroll |
| `Ctrl-d` `Ctrl-u` / `Ctrl-f` `Ctrl-b`, `Space` | half screen / full screen |
| `J` `K` | next / previous page |
| `gg` `G` `42G` (or `:42`) | first page / last page / page 42 |
| `+` `-` `=` `z` | zoom / fit width / fit page (also `Ctrl` + wheel) |
| `d` | two pages side by side |
| `r` / `R` | rotate the view 90° clockwise / counter-clockwise (the file is not changed) |
| `i` | dark mode (images keep their colours) |
| `/` `n` `N` | search / next / previous (`Esc` clears) |
| `\g` | all pages as thumbnails: `h j k l` choose, `J K` a screen, `+ -` size, `Enter` go there (`Ctrl-o` returns), `Esc` close; there the pages can also be changed (below) |
| `\o` | open a recent file: type parts of its name (fuzzy), `Ctrl-j` `Ctrl-k` move, `Enter` open |
| `\f` | find any PDF in your folders (`pdf_dirs`, the home folder by default; hidden folders skipped), the same way; newest first. The list of the last search shows at once while a new one runs (the whole home folder takes about a second) |
| `\/` | search the **text of all your PDFs** (those in `pdf_dirs`): words in any order on the same page, pages with them together first; `Tab` switches to comments and highlights; `Enter` opens the PDF at that page with the word highlighted (`n` `N` go on) |
| `\n` | search the **comments and highlights of all your PDFs** (with nothing typed: every comment) |

**Search** is incremental: while you type after `/` the view goes to the
first match from where you were (`Esc` goes back there, `Enter` keeps it, as
a jump). Every match on screen is highlighted until `Esc` or `:noh`, and the
message says which match it is ("match 3 of 17", for documents up to 150
pages). Smart case: a query with a capital letter matches case, otherwise
not; `Ctrl-t` in the prompt forces matching case (the prompt shows `/Aa`).

**Comparing versions**: `:diff old.pdf` compares the PDF shown with an older
version, word by word: new text is shaded green, changed text amber, and a red
mark shows where text was deleted. `]c` / `[c` go through the changes, and the
message says what the old text was ("change 3 of 9: changed, was “0.47”").
Nothing is written to the PDF; `:nodiff` hides it. When LaTeX recompiles the
PDF the comparison is made again, which is handy for answers to reviewers.
Line-end hyphens and ligatures are undone and page numbers ignored, so page
breaks do not show as changes.

**Merging a co-author's notes**: `:merge their-copy.pdf` brings the
highlights, comments, frames, drawings, sticky notes and images of another
copy of the same PDF (made in any reader) into this one, with their author
and colour. Those already here are left out, so merging twice changes
nothing; text boxes are not brought in. One `u` takes the whole merge back.

**Undo**: `u` undoes the last highlight, comment, image, move / resize or
deletion, `Ctrl-r` redoes it (per document, up to 200 steps, until the file is
closed). Deleted notes are only hidden until then, and removed from the file
when it is closed.

**Presenting**: `F5` (or `:present`) shows one page at a time, fullscreen on
black, nothing else: `j` `l` `Space` `→` `Enter` or a click go forward, `k`
`h` `←` `Backspace` or a right click go back, `gg` `G` `12G`, `.` or `b` a
black screen, `Esc` `q` `F5` stop (reading goes on at that slide). The next
slide is prepared while you talk.

**Password-protected PDFs** ask for the password (hidden) in the prompt, or
on the terminal when opened from one. It is kept in memory only, to reload
and save (the file stays encrypted), and wiped when the PDF closes.

**Sessions**: the open tabs are remembered as they change, so `npdf` with no
file reopens them (also `:session`). Recent files and the session live in
`~/.local/share/nomadys-pdf/`.

### Links and references

| Key | Action |
|---|---|
| `f` | letters appear on every link on screen (citations, "Fig. 3", "Eq. 5", table of contents, URLs and DOIs, even ones only written in the text); type them to follow. Inside the PDF it is a jump, so `Ctrl-o` comes back; web links open in the browser (`xdg-open`). `Esc` cancels, `Backspace` undoes a letter |
| `F` | the same letters, but **previews** the target in a box without leaving the page: the figure, the equation, the bibliography entry (a mark in the margin shows the exact spot). `j` `k` / `Ctrl-d` `Ctrl-u` scroll it, `Enter` goes there, `Esc` / `q` closes. On a web link it shows the address |
| click | a plain click on a link follows it (double click / `Ctrl`+click stays SyncTeX) |

Only `http(s)`, `mailto:` and `doi:` links are opened; links to files or
programs are shown and copied to the clipboard instead.

### Marks and jumps

For going back and forth between places while reading (text, a figure, an
equation, the references):

| Key | Action |
|---|---|
| `\ma`, `\mb`, ... | remember the current position as mark `a`, `b`, ... (`\` is the leader key) |
| `ma`, `mb`, ... | jump to that mark |
| `mm` | back to where you were before the last jump; again to return |
| `Ctrl-o` / `Ctrl-i` | older / newer position in the jump history (every `gg`, `G`, `42G`, search, mark and contents jump) |
| `\l` | panel with four tabs — **Tabs** (the open files: `Enter` switches, `x` closes), **Summary** (the PDF's outline, as a tree), **Marks**, and **Highlights** (colour, comment and the highlighted text); `Tab` / `h` `l` / `1`–`4` switch panel tabs, `j` `k` move, `Enter` goes, `r` renames a mark or edits a comment, `d` deletes only the comment, `x` deletes the highlight or the mark, `Esc` closes |

Marks and the last reading position are stored outside the PDF, in
`~/.local/share/nomadys-pdf/marks/`, keyed by the file's path (so they survive
LaTeX recompiling it); a file reopens where you stopped.

### Highlights and comments

Everything by keyboard. Highlights are standard PDF annotations: other readers
(Acrobat, Okular, Evince, ...) show them and their comments.

| Key | Action |
|---|---|
| `v` | text cursor (a steady block; rings close in on it when it appears): `h j k l w b 0 $` move |
| then `v` | start/stop selecting text |
| then `C` `V` `B` `N` `M` | highlight in pastel yellow / red / green / pink / blue (`highlight_colors` in the config) |
| then `c` | highlight (yellow) and write a comment in a box next to it (see below) |
| `Ctrl-v` | free cursor (a thin outlined cross), for figures and areas: `h j k l` move, `w b J K` (or `Ctrl-h/j/k/l`) move faster |
| then `v`, `C` `V` `B` `N` `M` / `c` | select a rectangle, highlight it in a colour / with a comment |
| `c` on a highlight | edit its comment (shown in a box while the cursor is on it) |
| `x` on a highlight | delete it |
| `u` / `Ctrl-r` | undo / redo the last highlight, comment, image, move or deletion (per document, up to 200 steps; kept until the file is closed) |
| `y` | copy the selected text |
| `Y` | copy it as a quotation with its page: `“…” (p. 5)` (the printed page number; `cite_format` in the config) |
| `Esc` | cancel the selection, then leave the cursor |

Highlights with a comment get a small dot in their corner.

**Comments with equations.** The comment box opens right next to the
highlight. Write plain text; anything between `$...$` is an equation, typeset
by LaTeX as you write, and `$$...$$` is an equation on its own line:

```
Neo-Hookean: $W = \frac{\mu}{2}(I_1 - 3)$ with $\mu = G$
$$\sigma = E\,\varepsilon$$
```

| Key in the box | Action |
|---|---|
| `Enter` | save |
| `Shift-Enter` | new line |
| `Esc` | cancel (the comment stays as it was) |
| arrows, `Home` `End` | move; a formula shows its source while the cursor is inside it |
| `Backspace` `Del` `Ctrl-w` `Ctrl-u` | delete a character / the word before / everything |
| `Ctrl-v` | paste |

`\$` is a literal dollar. Errors are shown under the text ("LaTeX: Undefined
control sequence."). The formula is typeset by `pdflatex` (TeX Live) with
`amsmath` and `amssymb`, in the background, and cached in
`~/.cache/nomadys-pdf/math/`, so each formula is typeset only once. Your own
macros go in `~/.config/nomadys-pdf/math.tex` (e.g.
`\newcommand{\R}{\mathbb{R}}`). Without TeX Live, comments still work and
formulas stay as written.

The comment is stored in the PDF as the text you typed, so other readers show
`$\sigma = E\varepsilon$` as is, and `:export` gives Markdown that Obsidian
and similar apps render with the equations.

**Copying a region as an image** (for slides): in the free cursor mark a
rectangle with `v`, then `y` copies it to the clipboard as a PNG, or `Y`
saves it next to the PDF (`paper-p5-1.png`, ...). It is drawn at `copy_dpi`
(300 by default, in the config) in the page's own colours, dark mode or not,
with highlights and filled forms as they are.

**Images.** Put a picture on the page (a signature, a screenshot, a figure):
in the free cursor (`Ctrl-v`) press `I` for a file (PNG, JPEG, BMP; `Tab`
completes) or `P` to paste an image from the clipboard; or type `:image
file.png` anywhere. With a rectangle marked (`v`) the picture fits inside it;
otherwise its top-left corner goes at the cross, at its natural size but never
more than 80% of the page. Transparent PNGs stay transparent.

Quality: PDFium enlarges pictures without smoothing (blocky pixels), so an
image is kept with more pixels than any screen shows: small ones (a
signature, a screenshot) are enlarged with a bicubic filter to 360 dpi at
their placed size, so they stay smooth up to ~250% zoom on a HiDPI screen;
images up to 4096 pixels are kept exactly (a JPEG is copied byte for byte);
only huge ones are reduced, with a bicubic filter, and never below 4096
pixels on the longer side. Measured: a 24-megapixel photo goes in in 0.7 s
and adds 1.9 MB; a screenshot in 0.2 s; memory returns to normal right after.
Pictures over 120 megapixels are refused (decoding takes 4 bytes a pixel). Images are standard stamp annotations:
other readers show them, `x` deletes them, and the panel lists them.

**Moving and resizing** an image or a highlighted area, even long after it was
placed: put the cross on it and press `e` (a new image starts there directly).

| Key | Action |
|---|---|
| `h` `j` `k` `l` (`Ctrl-` for faster) | move |
| `+` `-` | bigger / smaller, keeping the proportions |
| `H` `L` | narrower / wider |
| `K` `J` | shorter / taller |
| `=` | an image back to its own proportions |
| `r` `R` | turn an image 90° clockwise / counter-clockwise |
| `c` | crop an image (below) |
| `Enter` | done |
| `Esc` | undo: back where it was |
| `x` | delete it |

Highlighted text stays with its words; areas and images move.

**Cropping** (`c` on an image): `h` `j` `k` `l` cut the left / bottom / top /
right side as you see it (`Ctrl-` cuts faster), `H` `J` `K` `L` give that side
back, `u` shows the whole picture again, `Enter` returns to moving and
resizing, `Esc` cancels this cropping. The picture itself is never cut or
re-encoded: the PDF keeps it whole and only shows a part of it, so a crop can
be undone at any time, even after the file was closed. Turning works the same
way (no quality loss).

**Export to Markdown.** `:export` writes every highlight and comment, plus the
marks, to `paper-notes.md` next to `paper.pdf` (or `:export name.md`; a
relative name is taken from the PDF's folder). Page by page, in reading order,
with the full highlighted text (words broken across lines joined again), its
colour and the comment:

```markdown
## Page 3

- *(yellow)* “stiffness amplification through successive instabilities, ...”

  **Comment:** check this claim against [43]

- *(blue, area)* highlighted region — figure, table or equation
```

Running it again replaces the file; a Markdown file that `:export` did not
write is never overwritten unless you use `:export!`.
`npdf --export paper.pdf` prints the same to the terminal.

**Only some colours.** Give each colour a meaning (say green = to cite,
red = problems) and export just those: `:export green`, `:export red
commented` (only red ones with a comment), `:export review.md red green`.
In the panel's Highlights tab, `C V B N M` show or hide a colour, `c` keeps
only those with a comment, `a` shows all again and `e` exports what is
shown.

### Changing the pages

In the page grid (`\g`) the selected page, or several chosen with `v`
(then move to the other end), can be changed; the file itself changes, and
`u` / `Ctrl-r` undo and redo it (the document before each change is kept in
`~/.cache/nomadys-pdf/undo` until the PDF is closed). Marks follow their
pages. A signed PDF is left alone, since this would break its signature.

| Key in `\g` | Action |
|---|---|
| `v` | choose several pages |
| `x` or `d` | delete them |
| `r` / `R` | turn them right / left (in the file, not only the view) |
| `<` / `>` | move them one place earlier / later (`3>`: three places) |
| `y` | save them as a new PDF (the name is asked, `paper-p3-7.pdf` offered) |
| `i` | insert another PDF after the selected page (`:insert file`, `Tab` completes) |
| `u` / `Ctrl-r` | undo / redo |

### LaTeX with VimTeX

- **Auto-reload:** when LaTeX writes a new PDF, it reloads in place (same
  page, zoom and mode). Unsaved highlights in that PDF are dropped, since the
  file was rewritten.
- **Source → PDF** (forward search, `\lv` in VimTeX): the line under the
  editor cursor is shown and briefly highlighted. A second
  `npdf --synctex-forward ...` hands the request to the running window
  through a socket and exits (`$XDG_RUNTIME_DIR/nomadys-pdf.sock`).
- **PDF → source** (inverse search): **double click** or **Ctrl+click** a spot,
  or press `Enter` in the text/free cursor; the editor opens that line.
  Requires `-synctex=1` when compiling.

Neovim + VimTeX:

```vim
let g:vimtex_view_method = 'general'
let g:vimtex_view_general_viewer = 'npdf'
let g:vimtex_view_general_options = '--synctex-forward @line:@col:@tex @pdf'
```

The editor command for inverse search is `synctex_editor` in the config
(default `nvim --headless -c "VimtexInverseSearch %l '%f'"`; `%l` line,
`%c` column, `%f` file), or `-x CMD` on the command line.

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

### Split view

Two panes, each with its own place, zoom, dark mode and search: a figure
and the text about it, or two papers side by side. Keys, the mouse wheel
and clicks go to the pane with the thin accent line on top.

| Key | Action |
|---|---|
| `Ctrl-w v` / `Ctrl-w s` | split side by side / one above the other (the same PDF) |
| `:vsplit other.pdf` / `:split other.pdf` | the same with another PDF |
| `Ctrl-w w` or `Ctrl-w Ctrl-w` | the other pane (`Ctrl-w h j k l` by direction) |
| `Ctrl-w x` | the panes change places |
| `Ctrl-w q`, `:close` / `Ctrl-w o`, `:only` | close this pane / keep only this one |

### Commands (`:`)

`Tab` completes command names and file paths; with several options they are
listed above the line and `Tab` / `Shift-Tab` cycle through them (`:` then
`Tab` lists every command).

| Command | Action |
|---|---|
| `:w` | save |
| `:q` / `:q!` | close the tab / close it discarding changes |
| `:wq`, `:x` | save and close the tab |
| `:e file` | open a PDF in a new tab |
| `:42`, `:page 42` | go to page 42 |
| `:zoom 150` | zoom in percent |
| `:rotate 90`, `:rotate -90`, `:rotate 180` | rotate the view, relative to the current angle |
| `:dark` / `:light` | dark / light mode |
| `:marks`, `:notes` | the panel |
| `:noh` | clear the search highlight |
| `:image file.png` | put a picture on the page, then move / resize it |
| `:pages` | all pages as thumbnails (`\g`) |
| `:extract 3-7 [file.pdf]` | pages 3 to 7 as a new PDF (`paper-p3-7.pdf` by default) |
| `:delete 3-7` | delete pages (`u` undoes it) |
| `:insert other.pdf` / `:append other.pdf` | another PDF's pages after this page / at the end |
| `:bibtex` | copy the paper's BibTeX entry: its DOI (or arXiv number) is found in the PDF and the entry fetched from doi.org / arxiv.org (needs curl) |
| `:merge copy.pdf` | bring in a co-author's highlights and comments, without duplicates |
| `:diff old.pdf` | compare with an older version (`]c` `[c`); `:nodiff` hides it |
| `:present` | presentation (`F5`) |
| `:recent` | open a recent file (`\o`) |
| `:find` | find any PDF in your folders (`\f`) |
| `:library words` / `:comments words` | search the text / the comments and highlights of all your PDFs (`\/`, `\n`) |
| `:session` | reopen the tabs of the last session |
| `:export [file.md] [colours] [commented]` | highlights, comments and marks to Markdown, all or only some colours (`:export green`); `:export!` overwrites a file it did not write |
| `:reload` | reload the PDF from disk |
| `:print`, `Ctrl-p` | print through CUPS; options in any order: pages `3-7,10`, copies `x2`, printer `@name` (`Tab` lists them), `duplex`. Unsaved highlights and form entries are saved first, so they are printed too |
| `:help` | every key |

Commands live in one table in `commands.c` (name, argument, action,
description); the help screen and the completion list are built from it.

## Saving

By default, form entries and highlights are saved automatically about 1 s
after a change (also when leaving a field and when closing). The file is
rewritten compactly (an incremental save would append every object PDFium
loaded, photos included, making big papers tens of MB larger each time; it
also drops old revisions left by earlier incremental saves). Digitally signed
PDFs are saved incrementally, which keeps the signature valid. The file is
written to a temporary file and atomically renamed over the original. With
`autosave = false`, save with `Ctrl-s`/`:w`; closing with pending changes asks
for confirmation.

## Memory and speed

Built to stay light with big PDFs and many tabs:

- **Page textures**: one budget of 128 MB for all tabs and windows; the least
  recently used go first (tabs out of sight), never what is on screen.
  Neighbour pages are prepared ahead only while there is room.
- **Open pages**: at most 8 PDFium pages loaded, over all documents; an open
  page holds its decoded images (a 24-megapixel photo is ~70 MB).
- **Document caches**: PDFium keeps every object it has parsed until the
  document is closed (tens of MB after reading a big paper through). A tab out
  of sight is reopened after 3 s, the one you read after 15 s without input,
  when it has no unsaved changes and the file is unchanged; nothing on screen
  changes. Annotations are listed only when the panel needs them.
- **Heavy pages** (thousands of vector strokes, big plots) are rendered a
  slice at a time: about 30 ms per frame and the rest while idle. Meanwhile the
  page shows what was drawn of it at the previous zoom, stretched, so
  scrolling and zooming never wait; zooming ten steps on such a page renders
  only the last one (5 s instead of 38 s in the test with 120 000 strokes).
- **The library index** (for `\/` and `\n`) is made by three background
  processes at low priority, apart from the reader so a broken PDF cannot
  take it down, and kept in `~/.cache/nomadys-pdf/index` (about 9 KB per PDF,
  compressed). The first time takes about 16 ms per PDF (4 minutes for 14 000);
  afterwards only new or changed files are read. Network and FUSE mounts
  inside `pdf_dirs` are skipped (they could download every file). A search
  over 2 700 PDFs answers in under 0.2 s.
- Freed memory is given back to the system when idle (`malloc_trim`).
- **Idle**: no timers, no polling; the program sleeps until something happens.
- **Saving** rewrites the file compactly (0.1 s for a 255 MB paper) and waits
  longer after a change when saving takes long, so it never gets in the way.

Measured on a 255 MB, 26-page paper with 24-megapixel photos: 93 MB for the
program itself, ~100–130 MB with 8 such papers open in tabs, peaks of ~500 MB
while pages with the photos are drawn.

## Configuration

`~/.config/nomadys-pdf/config`; `config.example` lists every setting with its
default. The main ones:

```ini
theme = dark              # dark | light
autosave = true
restore_position = true
pdf_dirs = ~               # where \f looks, comma separated: ~/Papers, ~/Books
cite_format = “%t” (p. %p)  # Y in the cursor: %t the text, %p the page(s)
remember_view = true      # each file keeps its zoom, dark mode, two pages, rotation
show_tabs = false
subpixel_text = false
highlight_colors = #FFEE93, #FF9E9E, #A8E6A3, #FFB3DE, #A7C7F2   # C V B N M
area_style = highlight    # or frame: a rectangle around figures
leader = \                # or , ; space
font = JetBrains Mono     # any installed font (fontconfig name)
font_size = 11

[dark]                    # colours of the dark theme; [light] for the light one
background = #101010
tab_active_bg = #2E2E2E
bar_bg = #1E1E1E          # ':' and '/' line
panel_bg = #1C1C1C
comment_bg = #2B2A1E
page_bg = #1C1C1C         # dark mode: page paper and ink colours
page_fg = #E4E4E4
```

## Themes

Nine colour themes, each with a light and a dark mode (`i` switches), from
the official palettes: **gruvbox**, **tokyonight** (Day / Night), **catppuccin**
(Latte / Mocha), **rosepine** (Dawn / Main), **kanagawa** (Lotus / Wave),
**everforest**, **dracula** (Alucard / Dracula), **nord** and **solarized**.

**Choosing one**: `\T` (or `:theme`) lists them with samples of both modes;
moving with `j` `k` applies each at once, `i` shows the other mode, `Enter`
keeps it and writes `include = NAME` in the config (the rest of the config is
left as it is), `Esc` goes back. `:theme NAME` switches directly (`Tab`
completes the names).

**Your own**: `:theme new NAME` writes `~/.config/nomadys-pdf/themes/NAME.conf`,
started from the theme in use, with every colour explained on its line:

```ini
# Around the pages
background = #282828         # the window behind and between the pages
page_bg = #282828            # dark mode: the paper of the pages
...
search_match = #fabd2f       # search matches, link letters (f), the crop frame
```

Edit any `#rrggbb` and save: the open window shows it at once. A wrong value
is reported with its line, and the rest still works. Your themes appear in the
list as "yours". Keys can also be overridden in the config itself, in
`[light]` / `[dark]` sections after the include line. `page_shadow = false`
removes the soft shadow around pages.

![themes](docs/themes.png)

## Dark mode

Inverts lightness while keeping hue (red stays red), with the theme's
`page_bg` / `page_fg` as paper and ink. Images are judged one by one
(`dark_images = smart`, the default): **photos** keep their colours, while
**figures drawn on white** (plots, diagrams, schematics, most scientific
figures) are recoloured with the page, so no white blocks glare in the dark.
`dark_images = keep` leaves every image untouched; `invert` recolours all.

## Code layout

One feature per file in `src/`; `nomadys.h` has the shared types and a module map.

| File | Contents |
|---|---|
| `main.c` | windows, tabs, drawing order, event loop, test mode |
| `input.c` | key tokens and **all keymaps** (one table per mode) |
| `config.c` | config file and the colour themes (one table of theme keys) |
| `doc.c` | PDFium document, page cache, form environment, saving |
| `render.c` | layout, page rendering (in slices for heavy pages), texture cache, dark mode |
| `split.c` | split view: the two panes, focus, entering a pane |
| `cite.c` | `Y` quotations with the page, `:bibtex` (DOI / arXiv, curl in a thread) |
| `forms.c` | form field navigation and typing |
| `caret.c` | text cursor, free cursor, selections |
| `annot.c` | highlights and comments, undo / redo, `:merge` |
| `marks.c` | marks, jump history, last position |
| `search.c` | text search |
| `commands.c` | the `:` commands and their Tab completion |
| `synctex.c` | LaTeX forward/inverse search through the `synctex` tool |
| `ipc.c` | socket for VimTeX requests, inotify for auto-reload |
| `print.c` | printing through CUPS (`lp`, `lpstat`) |
| `export.c` | `:export`: highlights and comments to Markdown |
| `comment.c` | comment box: layout of text and equations, editing |
| `math.c` | equations: `pdflatex` in the background, cache, textures |
| `about.c` | the icon (built in from `src/icon_png.h`, made by `tools/make_icons.sh` from `docs/icon.svg`) and the about screen |
| `theme.c` | the theme list, `:theme new`, reloading a saved theme |
| `links.c` | link hints (`f`), reference preview (`F`), clicking links |
| `image.c` | images on the page: loading (stb_image), size, stamp annotations |
| `object.c` | moving and resizing images and highlighted areas |
| `diff.c` | `:diff`: words of both versions, patience diff, drawing, `]c` `[c` |
| `present.c` | presentation mode |
| `recent.c` | recent files, the session, the fuzzy file picker, the PDF finder (`\f`) |
| `library.c` | `\/` `\n`: the index workers (`npdf --index-worker`), searching it in threads, the result list |
| `grid.c` | the page grid (thumbnails drawn while idle) |
| `pages.c` | deleting, turning, moving, extracting and inserting pages; their undo snapshots |
| `help.c` | the help screen (`\c`): tabs, groups, keycaps, search |
| `panel.c` | the marks / notes / contents panel |
| `font.c` | UI text through fontconfig and FreeType |
| `ui.c` | tab bar, prompt, messages, comment box |

To add or change a key, edit a line in the keymap tables of `input.c`,
e.g. `{"<leader>l", a_panel, "painel: marcas, destaques e sumário"}`; the
third field is the description on the help screen (`\c`), `NULL` hides it; `GROUP("Title")` starts a titled group there. To add a colour, add a field to `Theme`
(`nomadys.h`), its defaults and one `COLOR(name)` line in `config.c`.

## Tests

`tools/make_test_pdf.py` generates `tests/sample.pdf` (text, image, form).
Headless test mode with scripted keys and screenshots:

```sh
SDL_VIDEODRIVER=offscreen ./npdf --test-out /tmp/shots \
    --test-script '<C-j>text<Esc><shot:form>vjw<shot:caret>' copy.pdf
```

Tokens: plain characters, any key name from `input.c` (`<Esc>`, `<Enter>`,
`<C-j>`, ...), `<wait:ms>` and `<shot:name>` (saves `name.bmp`).

## Stress testing

`tests/qa/run.sh [WORKDIR] [SEEDS] [KEYS]` builds a sanitizer binary
(AddressSanitizer + UndefinedBehaviorSanitizer), generates malformed PDFs
(`make_corpus.py`: truncated, garbage, empty, no pages, cyclic outline, huge
and tiny pages, rotated pages, 3000 pages, many form fields...), runs each
through a broad key sequence, then feeds random key sequences built from every
keymap (`fuzz.py`, reproducible by seed, including window resizes).
`check.py` reports memory errors, undefined behaviour, leaks in our code,
crashes and hangs; it exits non-zero on any finding.

## Licences

Nomadys-PDF code: MIT (`LICENSE`). PDFium: BSD-3/Apache-2.0
(`third_party/pdfium/LICENSE` and `licenses/`).
`third_party/stb/` (stb_image, stb_image_write by Sean Barrett): public
domain or MIT, at your choice; checksums in `third_party/stb/SHA256SUMS`.
