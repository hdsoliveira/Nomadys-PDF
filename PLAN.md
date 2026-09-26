# Nomadys-PDF — plan

Minimal, fast PDF reader for Linux (Wayland/sway). Written in C.

## Stack

- **PDFium** (Chrome's PDF engine, BSD/Apache licence) for rendering, forms,
  text and annotations. Prebuilt binary from bblanchon/pdfium-binaries,
  release chromium/8066, `third_party/pdfium/` (SHA-256 verified
  `0b43f405…fed2`). Build without V8: no form JavaScript (field calculations);
  the V8 variant can replace the library later if needed.
- **SDL3** for windows, keyboard, text input and clipboard (needs
  `sudo apt install libsdl3-dev`).
- Plain `Makefile`. UI text (tab bar, short messages) uses an embedded bitmap
  font, no extra runtime dependency.

## Behaviour

- Opens showing only the PDF: no bars, menus or borders. Short messages
  (saved, page 12/40) appear in a corner for 1–2 s.
- Vim keys: `j/k`, `h/l`, `Ctrl-d/u`, `Ctrl-f/b`, `gg`, `G`, `42G`,
  `+`/`-` zoom, `=` fit width, `q` quit.
- Forms: text, checkbox, radio, lists. `Ctrl-j` / `Ctrl-k` jump to the next /
  previous field across pages; type directly; `Esc` leaves the field.
- `d` toggles two pages side by side.
- `Ctrl-n` opens another window on the same document (shared edits).
- Tabs: hidden by default; `t` shows/hides a thin tab bar; `Ctrl-,` / `Ctrl-.`
  switch left/right. `nomadys-pdf a.pdf b.pdf` opens tabs; `:e file` opens one;
  `:q` closes the tab. Each tab keeps page, zoom, dual mode and dark mode.
- Caret mode: `v` shows a blinking cursor in the text; `h/j/k/l`, `w/b`, `0/$`
  move; `v` starts a selection; `H` creates a real highlight annotation;
  `y` copies; `Esc` leaves.
- Forms/highlights are saved automatically (incremental save, atomic replace).
  With `autosave = false`: `Ctrl-s` or `:w`; quitting with unsaved changes
  warns once, press `q` again to discard.
- Config `~/.config/nomadys-pdf/config`: `theme = dark|light`, highlight
  colour, initial zoom.
- `i` toggles dark mode. Raster images keep their original colours; everything
  else gets a hue-preserving luminance inversion (vector plots darken but keep
  their hues).

## Build order

1. Rendering, vim navigation, zoom, dual pages, dark mode, config.
2. Forms with `Ctrl-j/k`, saving.
3. Multiple windows.
4. Tabs.
5. Caret mode and highlights.
6. Text search (`/`, `n`, `N`).

## Decisions (user, 2026-09-25)

- Save: automatic by default (debounced after edits, and on quit), with
  `autosave = false` in the config for manual `Ctrl-s` / `:w`.
- Short corner messages: yes.
- Text search in v1: yes (`/`, `n`, `N`).
- Tabs: `t` toggles the bar; switching with `Ctrl-,` / `Ctrl-.` while hidden
  flashes the bar ~1 s.
- PDFium without V8 (no form JavaScript) for now.

## Status (2026-09-25)

v0.1.0 implements all items above. Verified headless (SDL offscreen driver +
`--test-script`): rendering, dark mode with images preserved, dual pages,
form navigation/typing/checkbox, autosave (valid PDF per `mutool`, values
persisted), caret mode, selection, highlight annotation saved, search, tabs,
second window. 644-page PDF: open + first frame 0.15 s; ~12 ms per page flip.
Not yet tested in a real sway window by the user.

## v0.2.0 (2026-09-26)

User requests: keyboard-only highlights with comments (text and free areas
such as figures), marks for jumping around while reading, colour themes for
dark and light, UI font choice, and maintainable code.

- Marks: user's design, leader-based — `\ma` sets, `ma` jumps (the frequent
  action gets the short key); `mm` toggles back; `Ctrl-o`/`Ctrl-i` jump list;
  `\l` panel. Stored outside the PDF (~/.local/share/nomadys-pdf/marks/),
  keyed by the PDF file ID; also restores the last reading position.
- Highlights/comments: standard Highlight annotations with /Contents, so
  other readers show them. Text cursor (`v`) or free cursor (`Ctrl-v`).
- Themes: [light]/[dark] sections, 25 colour keys; UI font through
  fontconfig + FreeType (the embedded bitmap font was removed).
- Code: one feature per file; all keys in tables in input.c; ambiguous
  sequences (space leader vs Space) wait 700 ms like vim's timeoutlen.
- Verified headless: regression (forms, dark, dual, search, tabs), text and
  area highlights with comments saved as /Contents, comment box, panel with a
  real 644-page book's contents, rename/delete, marks/mm/Ctrl-o/Ctrl-i,
  restore position, custom theme/font, space leader. Panel opens in ~0.1 s
  after background annotation listing (was ~0.9 s). Speed unchanged.
- Open: area highlights use the standard multiply blend, which darkens
  colourful figures; a frame style could be offered.

## Roadmap (suggested 2026-09-26, accepted by the user)

Most valuable for reading and reviewing papers by keyboard:

1. **Follow links by keyboard** — `f` shows letter hints on every visible link
   (citations, figure/equation references, outline, URLs, DOIs); typing the
   letters follows it, as a jump (`Ctrl-o` returns); URLs open in the browser.
   *In progress.*
2. **Reference preview** — show the target of "Fig. 3", "Eq. 5" or "[12]" in a
   popup without leaving the page (`F` + hint letters), Enter to go there.
   *In progress.*
3. **Export highlights and comments to Markdown** (`:export`): text, comment,
   page and colour, for notes / Obsidian / review reports.
4. **Copy a region as an image** — free cursor (`Ctrl-v`), select, `y` copies
   a high-resolution image to the clipboard (for slides).
5. **Compare two versions of a PDF** (`:diff old.pdf`): highlight what changed
   (answers to reviewers, co-author edits).

Everyday comfort:

6. Incremental search (highlight while typing, all matches on the page,
   case toggle).
7. Sessions: reopen the last tabs (`:session`), recent files with fuzzy search.
8. Thumbnail grid of pages, navigated with hjkl.
9. Presentation mode: fullscreen, one page, nothing else (Beamer).
10. Password-protected PDFs: ask for the password in the prompt line.

More ambitious:

11. OCR for scanned PDFs (tesseract, if installed): search/select/highlight.
12. Background rendering thread: smooth zoom/scroll on very heavy pages.
