# Getting started

## Opening files

```sh
npdf paper.pdf                 # one PDF
npdf paper.pdf notes.pdf       # each in its own tab
npdf                           # the tabs you had open last time
```

A file reopens where you stopped reading, with the zoom, dark mode and
rotation you left it in. From inside the program, `\o` opens a recent file,
`\f` finds any PDF on your computer and `:e file.pdf` opens one by path.

## The first five minutes

1. **Move around.** `j` `k` scroll, `J` `K` go to the next / previous page,
   `Space` goes a screen down, `gg` and `G` go to the first and last page,
   `42G` to page 42.
2. **Zoom.** `+` and `-` change it, `=` fits the width and `z` the whole page.
3. **Dark mode.** Press `i`. Photos keep their colours; plots and diagrams on
   white are recoloured so nothing glares.
4. **Search.** Type `/stiffness` and `Enter`, then `n` and `N` go through the
   matches.
5. **Follow a reference.** Press `F` and letters appear on every link,
   citation, figure and equation reference on screen. Type one to see the
   target in a small window without leaving the page.
6. **Highlight.** `v` shows a text cursor, `w` moves it word by word, `v`
   starts selecting, move to the end, then `C` (yellow) or `c` to write a
   comment.
7. **See everything you marked.** `\l` opens the panel, and its Highlights tab
   lists every highlight and comment.
8. **Stuck?** Type `\c` and a menu with every key appears. `Esc` always takes you back.

## The leader key

The backslash `\` is the **leader key**. On its own it does nothing, but it
starts many different functions, depending on the key you press after it.
`\c` opens the help, `\l` the panel, `\g` the page grid, `\o` the recent
files, `\f` the finder for any PDF, `\/` the search across all PDFs, `\n` the
search across all comments, `\T` the themes, `\a` the about screen, and `\ma`
sets mark *a*.

When you do not remember a key, type `\c`. A menu appears with every key of
the mode you are in, in tabs, with a search box, and `Esc` closes it. It is
the quickest way to learn the program.

The leader key can be changed to `,`, `;` or space with `leader =` in the
[configuration](Configuration.md).

## Modes

Like vim, Nomadys-PDF has modes. A small badge in the bottom-left corner
shows which one you are in.

| Badge | Mode | How you get there |
|---|---|---|
| `NORMAL` | reading | the start; `Esc` from anywhere |
| `CURSOR` | text cursor, to select text | `v` |
| `AREA` | free cursor, for figures and regions | `Ctrl-v` |
| `VISUAL` | selecting (text, or a rectangle) | `v` in either cursor |
| `FOLLOW` / `PREVIEW` | link letters on screen / a preview window | `f` / `F` |
| `MOVE` / `IMAGE` / `CROP` | moving or resizing an area or image, cropping it | `e` on it in the free cursor, then `c` |
| `INSERT` | typing in a form field | `Ctrl-j` or a click on a field |
| `COMMAND` / `SEARCH` | the `:` or `/` line | `:` / `/` |
| `COMMENT` | the comment box | `c` on a selection or highlight |
| `PANEL` | the panel | `\l` |
| `PAGES` | the page grid | `\g` |
| `OPEN` | recent files or the PDF finder | `\o` / `\f` |
| `LIBRARY` | search across all PDFs | `\/` / `\n` |
| `THEME` | the theme chooser | `\T` |
| `HELP` | the help screen | `\c` |

`mode_indicator = false` in the configuration hides the badge.

## The help screen

`\c` opens the help, with every key of every mode, grouped, drawn as keycaps,
in tabs (Reading, Text cursor, Free cursor, Comments, Panel, Preview,
Commands, Forms, Move/resize, Crop, Pages, Present). It opens on the tab of
the mode you are in, and each tab starts with *How to get here*.

- `Tab`, `h` `l` or `1`–`9` change tab; `j` `k` move
- `/` searches the keys of every tab ("split", "comment", "zoom")
- `Esc` or `\c` again closes it

The help is generated from the program's own key tables, so it is never
out of date.

## Counts

A number before a key repeats it. `5j` scrolls five times, `3n` goes to the
third next match, and `3>` in the page grid moves a page three places.

## The command line

`:` opens the command line, where `:w` saves, `:q` closes the tab, `:42` goes to
page 42 and `:e file.pdf` opens a file. `Tab` completes command names and file
paths; `:` followed by `Tab` lists every command. See [Commands](Commands.md).

## Saving

Highlights, comments and form entries are **saved automatically** about a
second after each change, into the PDF itself, as standard annotations that
Acrobat, Okular, Evince, Zotero and others show. See
[Files, memory and speed](Files-Memory-and-Speed.md#saving).
