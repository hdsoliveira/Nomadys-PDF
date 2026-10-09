# LaTeX and VimTeX

Nomadys-PDF is made to sit next to Vim or Neovim while you write.

## Auto-reload

When LaTeX writes a new PDF, it reloads in place, with the same page, zoom and mode.
No key to press, no flicker. If a [`:diff`](Searching.md#comparing-two-versions-with-diff)
comparison is on, it is made again with the new version.

Highlights made in a PDF that LaTeX then rewrites are lost with the old
version (the file was replaced), so annotate the final PDF, or a copy.

## From the source to the PDF (forward search)

In VimTeX, `\lv` shows the line under the editor cursor in the PDF, briefly
highlighted. A second `npdf --synctex-forward …` hands the request to the
window already open (through a socket in `$XDG_RUNTIME_DIR`) and exits, so
no second window appears.

## From the PDF to the source (inverse search)

**Double click** or **Ctrl + click** a spot in the PDF, or press `Enter`
in the text or free cursor, and the editor opens that line of the source.

Compile with `-synctex=1` (latexmk and VimTeX do by default).

## Setting it up in Neovim / Vim

```vim
let g:vimtex_view_method = 'general'
let g:vimtex_view_general_viewer = 'npdf'
let g:vimtex_view_general_options = '--synctex-forward @line:@col:@tex @pdf'
```

The editor command for inverse search is `synctex_editor` in the
[configuration](Configuration.md); the default works with Neovim and
VimTeX.

```ini
synctex_editor = nvim --headless -c "VimtexInverseSearch %l '%f'"
```

`%l` is the line, `%c` the column and `%f` the file. It can also be given
on the command line, as in `npdf -x "code -g %f:%l" paper.pdf`.

## Useful while writing

- `:diff submitted.pdf` shows everything that changed since the submitted
  version, updated at every recompilation. See [Searching](Searching.md#comparing-two-versions-with-diff).
- `Y` copies a quotation with its page, `:bibtex` the BibTeX of the paper
  you are reading. See [Quoting and citing](Quoting-and-Citing.md).
- `y` in the free cursor copies a figure as an image, for slides.
