# Nomadys-PDF

<img src="../icon.svg" width="120" align="right" alt="the Nomadys-PDF icon">

**Nomadys-PDF** is a fast, minimal PDF reader for Linux, driven entirely by
the keyboard with vim-style keys. When it opens, it shows your PDF and
nothing else: no toolbars, no menus, no sidebars. Everything is one or two
keys away, and `\c` shows every key at any moment.

It is made for people who read, annotate and write papers: highlights and
comments that every other reader can see, comments with LaTeX equations,
a search over the text of every PDF you own, SyncTeX with Vim/Neovim,
citations and BibTeX in one key, and a comparison of two versions of a
paper word by word.

*Nomadys*: a nomad, with no fixed place, always looking for something
better. The icon is a tent at home anywhere, a trail towards the mountain
and the rising sun, and a page lit in the doorway: reading goes wherever
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
| Open any PDF on your computer | `\f` (recent ones: `\o`) |
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
- [Installation](Installation.md): building, installing, updating
- [Getting started](Getting-Started.md): the first five minutes, modes, the help screen

**Using it**
- [Reading and navigation](Reading-and-Navigation.md): scrolling, zoom, dark mode, marks, jumps, the panel, links, presenting
- [Searching](Searching.md): in a PDF, across all your PDFs, finding files, comparing versions
- [Highlights and comments](Highlights-and-Comments.md): cursors, colours, comments with equations, undo, filters, export, merging a co-author's notes
- [Quoting and citing](Quoting-and-Citing.md): copying text, quotations with the page, BibTeX
- [Images and regions](Images-and-Regions.md): putting pictures on pages, moving, resizing, cropping, copying a region for slides
- [Changing pages](Changing-Pages.md): delete, turn, move, extract and insert pages
- [Tabs, windows and split view](Tabs-Windows-and-Split-View.md): several PDFs at once, sessions
- [LaTeX and VimTeX](LaTeX-and-VimTeX.md): auto-reload, forward and inverse search
- [Forms, printing and passwords](Forms-Printing-and-Passwords.md)

**Making it yours**
- [Configuration](Configuration.md): every setting
- [Themes and dark mode](Themes-and-Dark-Mode.md): fifteen themes, your own themes, fonts

**Reference**
- [Keyboard reference](Keyboard-Reference.md): every key, by mode
- [Commands](Commands.md): every `:` command
- [Files, memory and speed](Files-Memory-and-Speed.md): where things are kept, saving, performance
- [FAQ and troubleshooting](FAQ.md)

## About

Version 0.4.0. Made by Hugo Oliveira (hs.oliveira@mailbox.org),
MIT licence. Source: [github.com/hdsoliveira/Nomadys-PDF](https://github.com/hdsoliveira/Nomadys-PDF).
Built on [PDFium](https://pdfium.googlesource.com/pdfium/) (the PDF engine
of Chrome), SDL3, FreeType, fontconfig and stb. `\a` in the program shows
the version and how to reach the author.
