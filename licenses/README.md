# Third-party licences

Nomadys-PDF itself is MIT (`../LICENSE`). It uses the work of others, all
under permissive licences; their texts are here. `make install` copies this
folder, with PDFium's own, to `share/doc/nomadys-pdf/`.

## Libraries

| Component | Licence | Text | How it is used |
|---|---|---|---|
| PDFium (Google), build by Benoit Blanchon (pdfium-binaries) | BSD-3-Clause; build scripts MIT | `third_party/pdfium/LICENSE`, `third_party/pdfium/licenses/pdfium.txt` | shipped: `libpdfium.so` |
| Inside PDFium: FreeType, libjpeg-turbo, OpenJPEG, libpng, Little CMS, zlib, ICU, Abseil, Anti-Grain Geometry, fast_float, simdutf, LLVM libc | FTL, IJG / BSD-3 / zlib, BSD-2, libpng 2, MIT, zlib, Unicode 3, Apache-2.0, AGG, MIT, MIT, Apache-2.0 with LLVM exception | `third_party/pdfium/licenses/` (downloaded with PDFium) | shipped, inside `libpdfium.so` |
| SDL3 | zlib | `SDL3.txt` | linked |
| FreeType | FreeType License (FTL) | `freetype.txt` | linked (UI text) |
| fontconfig | MIT-style | `fontconfig.txt` | linked (finding the UI font) |
| zlib | zlib | `zlib.txt` | linked |
| stb_image, stb_image_write (Sean Barrett) | MIT or public domain | `stb.txt` | compiled in (`third_party/stb/`) |

Credits the licences ask for:

- Portions of this software are copyright © The FreeType Project
  (www.freetype.org). All rights reserved.
- This software is based in part on the work of the Independent JPEG Group.

## Colour themes

The palettes in `themes/` follow these projects:

| Theme | Licence | Text |
|---|---|---|
| Catppuccin | MIT | `themes/catppuccin.txt` |
| Dracula | MIT | `themes/dracula.txt` |
| Everforest | MIT | `themes/everforest.txt` |
| Gruvbox | MIT/X11 | `themes/gruvbox.txt` |
| Kanagawa | MIT | `themes/kanagawa.txt` |
| Nord | MIT | `themes/nord.txt` |
| Rosé Pine | MIT | `themes/rosepine.txt` |
| Solarized | MIT | `themes/solarized.txt` |
| Tokyo Night | Apache-2.0 | `themes/tokyonight.txt` |
| Flexoki | MIT | `themes/flexoki.txt` |
| GitHub (Primer) | MIT | `themes/github.txt` |
| One Light / One Dark (Atom) | MIT | `themes/one.txt` |
| Ayu | MIT | `themes/ayu.txt` |
| Nightfox / Dayfox | MIT | `themes/nightfox.txt` |
| Selenized | MIT | `themes/selenized.txt` |

Programs that Nomadys-PDF only runs when they are installed (pdflatex,
synctex, lp, xdg-open) are not part of it and keep their own licences.
