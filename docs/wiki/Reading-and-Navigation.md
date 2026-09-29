# Reading and navigation

## Moving through a PDF

| Key | Action |
|---|---|
| `j` `k` (or `↓` `↑`) | scroll down / up |
| `h` `l` (or `←` `→`) | scroll left / right (when zoomed in) |
| `Ctrl-d` / `Ctrl-u` | half a screen down / up |
| `Ctrl-f` / `Ctrl-b`, `Space` | a screen down / up |
| `J` / `K` | next / previous page |
| `gg` / `G` | first / last page |
| `42G`, `42gg` or `:42` | page 42 |
| mouse wheel | scroll |

A number repeats a key: `5j`, `3J`.

## Zoom and layout

| Key | Action |
|---|---|
| `+` / `-` (or `Ctrl` + wheel) | zoom in / out |
| `=` | fit the width of the window |
| `z` | fit the whole page |
| `:zoom 150` | a zoom in percent |
| `d` | two pages side by side (again: one page) |
| `r` / `R` | turn the view 90° clockwise / counter-clockwise |
| `:rotate 90`, `:rotate -90`, `:rotate 180` | the same, by an angle |
| `i` | dark mode on / off (also `:dark`, `:light`) |

Turning the view with `r` does **not** change the file. To turn pages in the
file itself, use the page grid ([Changing pages](Changing-Pages.md)).

**Each file remembers how you read it.** Zoom, dark mode, two-page layout
and rotation are kept per file and restored when you open it again, together
with the page you were on. A file you never changed opens with the defaults
of the [configuration](Configuration.md) (`zoom`, `theme`). Turn this off
with `remember_view = false` (and `restore_position = false` for the page).

Pages are drawn at the exact resolution of your screen (HiDPI included),
never stretched. Very heavy pages (thousands of vector lines, dense plots)
are drawn a little at a time, so scrolling and zooming never freeze: while
such a page is being drawn, it shows how it looked at the previous zoom.

## Dark mode

`i` switches between the light and dark versions of the current
[theme](Themes-and-Dark-Mode.md). In dark mode the pages are recoloured,
keeping hues (red stays red). Images are judged one by one: photos keep
their colours, while figures drawn on white (plots, diagrams) are
recoloured with the page so no white blocks glare. `dark_images` in the
configuration changes this. Dark mode is remembered per file.

## All pages at once: the page grid

`\g` (or `:pages`) shows every page as a thumbnail.

| Key | Action |
|---|---|
| `h` `j` `k` `l`, arrows | choose a page |
| `J` `K`, `Ctrl-d` `Ctrl-u` | a screen down / up |
| `gg` / `G` | first / last page |
| `+` / `-` | bigger / smaller thumbnails |
| `Enter` | go to that page (`Ctrl-o` returns) |
| `Esc`, `q`, `\g` | close |

The grid is also where pages are deleted, turned, moved, extracted and
inserted: see [Changing pages](Changing-Pages.md).

## Marks and jumps

For going back and forth between places: the text, a figure, an equation,
the references.

| Key | Action |
|---|---|
| `\ma`, `\mb`, … `\mz` | remember this place as mark *a*, *b*, … |
| `ma`, `mb`, … | jump to that mark |
| `mm` | back to where you were before the last jump (again: return) |
| `Ctrl-o` / `Ctrl-i` | older / newer place in the jump history |

Every big move is a *jump* and goes into the history: `gg`, `G`, `42G`, a
search, a mark, a link, a contents entry, a page chosen in the grid. So
`Ctrl-o` always brings you back.

Marks get a name from the first line of text where they were set; you can
rename them in the panel. They are stored outside the PDF (keyed by its
path), so they survive LaTeX recompiling the file; if pages are deleted or
moved in the page grid, marks follow their pages.

## The panel

`\l` (or `:marks`, `:notes`) opens a panel with five tabs:

| Tab | Shows |
|---|---|
| **Tabs** | the open files (`Enter` switches to one, `x` closes it) |
| **Summary** | the PDF's table of contents, as a tree |
| **Marks** | your marks with their names |
| **Highlights** | every highlight with its colour, comment and highlighted text, and every image |
| **Tasks** | the highlights you put on the task list (`t`): to do first, then done ([more](Highlights-and-Comments.md#tasks-tick-your-highlights-off)) |

| Key | Action |
|---|---|
| `Tab`, `h` `l`, `1`–`5` | change tab |
| `j` `k`, `gg` `G` | move |
| `Enter` | go there |
| `r` | rename the mark / edit the comment |
| `d` | delete only the comment (Tasks tab: done / to do) |
| `x` | delete the highlight (with its comment) or the mark |
| `t` | Highlights, Tasks: on / off the task list |
| `Space`, `D` | Tasks tab: done / to do; hide / show the done ones |
| `C` `V` `B` `N` `M` | Highlights tab: show or hide a colour |
| `c` / `a` | Highlights tab: only those with a comment / all again |
| `e` | Highlights tab: export what is shown to Markdown |
| `Esc`, `q` | close |

## Links, references and previews

| Key | Action |
|---|---|
| `f` | letters appear on every link on screen; type one to follow it |
| `F` | the same letters, but the target is **previewed** in a window, without leaving the page |
| click | a plain click on a link follows it |

The letters go on every link of the PDF on screen: citations, references
to figures, tables, equations and sections (papers made with LaTeX and
`hyperref`, and most journal PDFs, have them), entries of a table of
contents, and web links. Web addresses and DOIs written in the text as plain
words get letters too. Following a link inside the PDF is a jump, so
`Ctrl-o` comes back. Web links (`http`, `https`, `www.`, `mailto:`, `doi:`)
open in your browser; links to local files or programs are never run: they
are shown and copied to the clipboard instead.

The **preview** (`F`) shows the target of a link inside the PDF (the
figure, the equation, the bibliography entry) in a floating window, with a mark in the margin at the exact spot.

| Key in the preview | Action |
|---|---|
| `j` `k`, `Ctrl-d` `Ctrl-u` | scroll it |
| `Enter` | go there for real |
| `Esc`, `q` | close |

While typing letters, `Backspace` undoes one and `Esc` cancels.

## Presenting

`F5` (or `:present`) shows one page at a time, fullscreen, on black, with
nothing else on screen.

| Key | Action |
|---|---|
| `j` `l` `Space` `→` `↓` `Enter` `PageDown`, left click | next slide |
| `k` `h` `←` `↑` `Backspace` `PageUp`, right click | previous slide |
| `gg` `G` `12G` | first, last, slide 12 |
| `.` or `b` | black screen (again: back) |
| `Esc`, `q`, `F5` | stop; reading continues at that slide |

The next slide is prepared while you talk, so moving forward is instant,
even for heavy pages. Slides are drawn at the screen's full resolution;
your reading zoom and place are untouched.

## About

`\a` (or `:about`) shows the version, the author and the contact address.
