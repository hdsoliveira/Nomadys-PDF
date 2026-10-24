# Nomadys-PDF

<img src="../icon.svg" width="120" align="right" alt="the Nomadys-PDF icon">

**Nomadys-PDF** is a fast, minimal PDF reader for Linux, driven entirely by
the keyboard with vim-style keys. When it opens, it shows your PDF and
nothing else, with no toolbars, no menus and no sidebars. Everything is one or two
keys away. The backslash `\` is the leader key and starts many functions, and
typing `\c` at any moment opens a menu with every key to help you.

It is made for people who read, annotate and write papers. It has highlights and
comments that every other reader can see, comments with LaTeX equations,
a search over the text of every PDF you own, SyncTeX with Vim/Neovim,
citations and BibTeX in one key, and a comparison of two versions of a
paper word by word.

*Nomadys* means a nomad, with no fixed place, always looking for something
better. The icon is a tent at home anywhere, a trail towards the mountain
and the rising sun, and a page lit in the doorway, because reading goes wherever
you go.

## At a glance

| You want to… | Press |
|---|---|
| See every key | `\c` |
| Scroll, change page | `j` `k`, `J` `K`, `gg` `G`, `42G` |
| Zoom, fit width, fit page | `+` `-`, `=`, `z` |
| Dark mode | `i` |
| Search this PDF | `/`, then `n` `N` |
| Search **all** your PDFs | `\/` |
| Search all your comments and highlights | `\n` |
| Open any PDF on your computer | `\f` (`\o` for recent ones) |
| Highlight text | `v` (cursor), `v` (select), then `C` `V` `B` `N` `M` |
| Comment (with `$equations$`) | `c` on a selection or a highlight |
| Undo / redo | `u` / `Ctrl-r` |
| Follow / preview a link, figure or citation | `f` / `F` |
| See all pages, reorder or delete them | `\g` |
| Two panes side by side | `Ctrl-w v` |
| Present fullscreen | `F5` |
| Export your notes to Markdown | `:export` |

## The wiki

**Start here**
- [Installation](Installation.md) covers building, installing and updating
- [Getting started](Getting-Started.md) covers the first five minutes, the modes and the help screen

**Using it**
- [Reading and navigation](Reading-and-Navigation.md) covers scrolling, zoom, dark mode, marks, jumps, the panel, links and presenting
- [Searching](Searching.md) covers searching in a PDF and across all your PDFs, finding files and comparing versions
- [Highlights and comments](Highlights-and-Comments.md) covers cursors, colours, comments with equations, undo, filters, export and merging a co-author's notes
- [Quoting and citing](Quoting-and-Citing.md) covers copying text, quotations with the page and BibTeX
- [Images and regions](Images-and-Regions.md) covers putting pictures and text on pages, moving, resizing, turning, mirroring and cropping them, and copying a region for slides
- [Changing pages](Changing-Pages.md) covers deleting, turning, moving, extracting and inserting pages
- [Tabs, windows and split view](Tabs-Windows-and-Split-View.md) covers several PDFs at once and sessions
- [LaTeX and VimTeX](LaTeX-and-VimTeX.md) covers auto-reload and forward and inverse search
- [Forms, printing and passwords](Forms-Printing-and-Passwords.md)

**Making it yours**
- [Configuration](Configuration.md) lists every setting
- [Themes and dark mode](Themes-and-Dark-Mode.md) covers fifteen themes, your own themes and fonts

**Reference**
- [Keyboard reference](Keyboard-Reference.md) lists every key, by mode
- [Commands](Commands.md) lists every `:` command
- [Files, memory and speed](Files-Memory-and-Speed.md) covers where things are kept, saving and performance
- [FAQ and troubleshooting](FAQ.md)

## About

Version 0.6.0. Made by Hugo Oliveira (nomadys-pdf@mailbox.org),
MIT licence. The source is at [github.com/hdsoliveira/Nomadys-PDF](https://github.com/hdsoliveira/Nomadys-PDF).
Built on [PDFium](https://pdfium.googlesource.com/pdfium/) (the PDF engine
of Chrome), SDL3, FreeType, fontconfig and stb. `\a` in the program shows
the version and how to reach the author.
