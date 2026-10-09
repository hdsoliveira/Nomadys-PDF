# Changing pages

Delete, turn, reorder, extract or insert pages, in the page grid or from
the command line. The **file itself** changes (it is saved like any other
change), and every change can be undone.

## In the page grid

Open the grid with `\g`. The page under the frame is the *selected* one;
`h` `j` `k` `l` move the frame. The changes act on the pages *chosen*
(tinted), or on the selected page when none is chosen. There are two ways to choose pages.

- `Space` chooses or unchooses one page. The chosen pages need not be side
  by side. Choose page 1, go to page 4, choose it, and `y` saves a PDF with
  just pages 1 and 4.
- `v` chooses a run of pages. Press it, then move to the other end.

| Key | Action |
|---|---|
| `Space` | choose / unchoose this page |
| `v` | choose a run of pages (press it again to stop) |
| `x` or `d` | delete them |
| `r` / `R` | turn them 90° right / left, in the file |
| `←` / `→` | move them one place earlier / later (`3→` moves three places); also `<` / `>` |
| `↑` / `↓` | move them a row up / down |
| `y` | save them as a new PDF; the name is asked (`paper-p3-7.pdf` or `paper-p1,4.pdf` is offered) |
| `i` | insert another PDF after the selected page (`Tab` completes the path) |
| `u` / `Ctrl-r` | undo / redo |
| `Enter` | go to the page |
| `Esc` | close the grid |

Only pages side by side move with the arrows, as one block, and they stay
chosen where they go, so `→` `→` `→` walks them along. Chosen pages apart
can be deleted, turned or saved, not moved.

The bottom line of the grid always lists the keys and says which pages are
chosen.

## From the command line

| Command | Action |
|---|---|
| `:delete 3-7` | delete pages 3 to 7 (`:delete 4` one page, `:delete 10-` to the end) |
| `:extract 3-7` | save pages 3 to 7 as `paper-p3-7.pdf` next to the PDF |
| `:extract 3-7 ~/slides/fig.pdf` | … under another name |
| `:insert other.pdf` | insert all pages of another PDF after the page you are reading (in the grid, after the selected one) |
| `:append other.pdf` | add them at the end |

## Good to know

- **Undo** works for every page change (`u`, `Ctrl-r`), mixed with the undo
  of highlights and comments. Before each change, the document is kept in
  `~/.cache/nomadys-pdf/undo`; those copies are deleted when the PDF is
  closed, after which the change is final.
- **Marks follow their pages.** A mark on page 10 is on page 9 after page 3
  is deleted. A mark on a deleted page is removed. You stay on the page you
  were reading.
- Highlights and comments go with their pages, into extracted PDFs too.
- Turning here changes the file; `r` while reading only turns the view.
- **Signed PDFs** are left alone, because changing their pages would break the
  signature.
- A PDF needs at least one page, so the last page cannot be deleted.
