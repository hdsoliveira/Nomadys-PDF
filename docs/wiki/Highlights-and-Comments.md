# Highlights and comments

Everything is done by keyboard, and everything is stored **in the PDF** as
standard annotations: Acrobat, Okular, Evince, Zotero, Preview and every
other reader show your highlights and comments. They are saved
automatically about a second after each change.

## Highlighting text

1. `v` shows the **text cursor**, a steady block (rings close in on it for a
   moment so you can find it). Move it with `h` `j` `k` `l`, word by word
   with `w` `b`, to the end of a word with `e`, to the start / end of the
   line with `0` `$`. A number repeats a move: `8j`, `5e`.
2. `v` again starts **selecting**; move to the end of what you want.
3. Choose a colour:

| Key | Colour (default) |
|---|---|
| `C` | pastel yellow |
| `V` | pastel red |
| `B` | pastel green |
| `N` | pastel pink |
| `M` | pastel blue |
| `c` | yellow, and write a comment |

The keys are next to each other on the keyboard, under your left hand. The
colours can be changed with `highlight_colors` in the
[configuration](Configuration.md).

`Esc` cancels the selection; `Esc` again leaves the cursor.

## Highlighting a figure, a table, an equation

Figures and equations are not text, so they have their own cursor:

1. `Ctrl-v` shows the **free cursor**, a thin cross that can go anywhere.
   `h` `j` `k` `l` move it; `w` `b` `J` `K` (or `Ctrl-h` `Ctrl-j` `Ctrl-k`
   `Ctrl-l`) move it faster.
2. `v` starts a rectangle; move to the opposite corner.
3. `C` `V` `B` `N` `M` highlights the area in that colour, `c` with a
   comment.

With `area_style = frame` in the configuration, areas get a coloured frame
instead of a transparent highlight. The free cursor can also copy a region
as an image, or place a picture: see [Images and regions](Images-and-Regions.md).

`Ctrl-v` in the text cursor switches to the free cursor.

## Editing and deleting

Put either cursor on a highlight:

| Key | Action |
|---|---|
| `c` | write or edit its comment |
| `x` | delete the highlight (and its comment) |
| `e` (free cursor) | move or resize a highlighted area (see [Images and regions](Images-and-Regions.md)) |

While a cursor is on a highlight that has a comment, the comment is shown in
a box next to it. Highlights with a comment have a small dot in a corner.

The panel (`\l`, Highlights tab) lists every highlight with its comment and
text; there `r` edits a comment, `d` deletes only the comment and `x` the
whole highlight.

## Undo and redo

| Key | Action |
|---|---|
| `u` | undo the last change |
| `Ctrl-r` | redo it |

Undo covers new highlights, comments, deletions, images, moves, resizes,
crops, merges and [page changes](Changing-Pages.md), up to 200 steps per
document, until the file is closed. `5u` undoes five steps. The message
says what was undone and where ("undone: deletion (p. 4)"), and the view
goes there if it is out of sight.

A deleted highlight is only hidden until you close the file (so it can come
back); then it is removed from the PDF for good.

## Comments, with equations

`c` opens a box right next to the highlight. Write plain text; anything
between `$…$` is an **equation**, typeset by LaTeX as you type, and
`$$…$$` is an equation on its own line:

```
Neo-Hookean: $W = \frac{\mu}{2}(I_1 - 3)$ with $\mu = G$
$$\sigma = E\,\varepsilon$$
```

| Key in the comment box | Action |
|---|---|
| `Enter` | save |
| `Shift-Enter` | new line |
| `Esc` | cancel (the comment stays as it was) |
| arrows, `Home`, `End` | move; an equation shows its LaTeX while the cursor is inside it |
| `Backspace`, `Del` | delete a character |
| `Ctrl-w` / `Ctrl-u` | delete the word before / everything |
| `Ctrl-v` | paste |

- `\$` writes a literal dollar sign.
- Errors appear under the text ("LaTeX: Undefined control sequence.").
- Equations are typeset by `pdflatex` with `amsmath` and `amssymb`, in the
  background, and cached in `~/.cache/nomadys-pdf/math/`, so each is
  typeset only once.
- Your own macros go in `~/.config/nomadys-pdf/math.tex`, for example
  `\newcommand{\R}{\mathbb{R}}`.
- Without TeX Live, comments still work and equations stay as written.

The comment is stored as the text you typed, so other readers show
`$\sigma = E\varepsilon$` as it is, and exported Markdown shows the equations
in Obsidian and similar apps.

## Tasks: tick your highlights off

Any highlight or comment can become a **task**: a point to answer in a
review, a figure to redo, a claim to check. Tasks get a check box in the
page's left margin, at the height of their line, and you tick them off as
you go.

| Key | Where | Action |
|---|---|---|
| `t` | text cursor (`v`), free cursor (`Ctrl-v`), panel | make it a task; again: tick it (done); again: to do |
| `T` | text cursor, free cursor | a plain highlight again |
| `T` | panel, Highlights tab | only the tasks still to do |
| `u` | anywhere | undo the last tick, like any change |

- The panel's Highlights tab shows each task's box and counts them:
  "tasks 5/8 done". A done task steps back (dimmer).
- **`:tasks`** lists the tasks still to do in **all your PDFs**, newest
  files first; typing words narrows the list, `Enter` opens the paper at the
  task. In `\n`, `Ctrl-t` switches between every comment and the tasks.
- Exported to Markdown, tasks are check boxes, `- [ ]` and `- [x]`, which
  Obsidian and GitHub show as check lists; `:export tasks` writes only those
  still to do.
- A task is stored in the annotation itself (`NomadysTask`, with the day it
  was done), so it travels with the PDF and with `:merge`. Other readers show
  the highlight as usual, without the box. Check marks set in **Acrobat**
  (right click on a comment, *Mark with Checkmark*) are read as tasks done.

## Filtering by colour

Give each colour a meaning, for example green = to cite, red = problems,
blue = methods. Then, in the panel's **Highlights** tab (`\l`, then `4`):

| Key | Action |
|---|---|
| `C` `V` `B` `N` `M` | show / hide that colour (several can be combined) |
| `c` | only highlights with a comment |
| `T` | only the tasks still to do |
| `a` | all again |
| `e` | export what is shown to Markdown |

The tab title says how many are shown ("Highlights 3 of 12").

## Exporting to Markdown

`:export` writes every highlight and comment, plus your marks, to
`paper-notes.md` next to `paper.pdf`, page by page, in reading order, with
the full highlighted text (words broken across lines joined again), its
colour and the comment:

```markdown
## Page 3

- *(yellow)* “stiffness amplification through successive instabilities, ...”

  **Comment:** check this claim against [43]

- *(blue, area)* highlighted region — figure, table or equation

- [ ] *(red)* “the boundary conditions are not stated”

  **Comment:** reviewer 2, point 3

- [x] *(green)* “Fig. 4 uses the old data”
```

| Command | Writes |
|---|---|
| `:export` | everything, to `paper-notes.md` |
| `:export review.md` | to another file (a relative name goes next to the PDF) |
| `:export green` | only the green highlights |
| `:export red commented` | only red highlights that have a comment |
| `:export review.md red green` | red and green, to `review.md` |
| `:export!` | the same, allowed to overwrite a file that `:export` did not write |

Colour words: `yellow`, `red`, `green`, `pink`, `blue` (the five keys, in
order) and `other`; `commented` keeps only those with a comment, `tasks` only
the tasks still to do.

Running `:export` again replaces the file it wrote before, but a Markdown
file written by someone else is never overwritten without `!`. From a
terminal, `npdf --export paper.pdf` prints the same to standard output.

To search your comments across **all** your PDFs, use `\n` ([Searching](Searching.md)).

## Merging a co-author's notes

A colleague highlighted and commented a copy of the same paper, in any
reader? `:merge their-copy.pdf` brings their highlights, comments, frames,
drawings, sticky notes and images into your copy, keeping their author name
and colours.

- Notes you already have are left out, so merging twice changes nothing.
- Free text boxes are not brought in.
- One `u` takes the whole merge back.
- The message says how many were merged, already present or left out.
