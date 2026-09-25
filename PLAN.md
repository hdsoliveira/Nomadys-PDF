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
