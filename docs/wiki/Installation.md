# Installation

Nomadys-PDF runs on Linux, on Wayland and X11. The command is `npdf`.

## Dependencies

```sh
sudo apt install build-essential pkg-config libsdl3-dev libfreetype-dev libfontconfig-dev zlib1g-dev
```

PDFium (the PDF engine) is **downloaded automatically** by the first `make`.
It is a pinned release (`chromium/8086` from
[bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries)),
checked against its SHA-256. It is not stored in the repository.

These programs are optional and add extra features.

| Program | Used for |
|---|---|
| `pdflatex` (TeX Live) with `amsmath`, `amssymb` | equations in comments ([Highlights and comments](Highlights-and-Comments.md)) |
| `synctex` (comes with TeX Live) | jumping between the PDF and the LaTeX source ([LaTeX and VimTeX](LaTeX-and-VimTeX.md)) |
| `curl` | `:bibtex` ([Quoting and citing](Quoting-and-Citing.md)) |
| CUPS (`lp`, `lpstat`) | printing |
| `xdg-open` | opening web links in the browser |

Everything else works without them.

## Building and installing

```sh
git clone https://github.com/hdsoliveira/Nomadys-PDF.git
cd Nomadys-PDF
make              # builds ./npdf
make install      # installs for your user
```

`make install` puts these files under `~/.local`.

- `bin/npdf`, the program
- `lib/nomadys-pdf/libpdfium.so`, the PDF engine
- `share/nomadys-pdf/themes/`, the colour themes
- `share/applications/npdf.desktop` and the icons, so it appears in your
  application menu and can be chosen as the program for PDFs

Make sure `~/.local/bin` is in your `PATH`.

`make uninstall` removes all of it. Your settings (`~/.config/nomadys-pdf`)
and data (marks, recent files, the search index) are left alone; see
[Files, memory and speed](Files-Memory-and-Speed.md) to remove those too.

## Updating

```sh
git pull && make && make install
```

## Making it the default PDF reader

```sh
xdg-mime default npdf.desktop application/pdf
```

## Checking the installation

```sh
npdf --version      # Nomadys-PDF (npdf) 0.5.1
npdf --help         # a short list of keys and options
```

The next page is [Getting started](Getting-Started.md).
