# Highlights and comments

Everything is done by keyboard, and everything is stored **in the PDF** as
standard annotations, so Acrobat, Okular, Evince, Zotero, Preview and every
other reader show your highlights and comments. They are saved
automatically about a second after each change.

## Highlighting text

1. `v` shows the **text cursor**, a steady block (rings close in on it for a
   moment so you can find it). Move it with `h` `j` `k` `l`, word by word
   with `w` `b`, to the end of a word with `e`, to the start / end of the
   line with `0` `$`. A number repeats a move, as in `8j` or `5e`.
2. `v` again starts **selecting**; move to the end of what you want.
   Or drag the mouse over the text, which selects and copies it at once,
   and the keys below work on it as well.
3. Choose a colour with one of these keys.

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

Figures and equations are not text, so they have their own cursor.

1. `Ctrl-v` shows the **free cursor**, a thin cross that can go anywhere.
   `h` `j` `k` `l` move it; `w` `b` `J` `K` (or `Ctrl-h` `Ctrl-j` `Ctrl-k`
   `Ctrl-l`) move it faster.
2. `v` starts a rectangle; move to the opposite corner.
3. `C` `V` `B` `N` `M` highlights the area in that colour, `c` with a
   comment.

With `area_style = frame` in the configuration, areas get a coloured frame
instead of a transparent highlight. The free cursor can also copy a region
as an image, or place a picture, as described in [Images and regions](Images-and-Regions.md).

`Ctrl-v` in the text cursor switches to the free cursor.

## Editing and deleting

Put either cursor on a highlight and use these keys.

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
`$$…$$` is an equation on its own line, as in this example.

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

## Tasks, ticking your highlights off

Any highlight or comment can go on your **task list**, which can hold a point to answer in
a review, a figure to redo or a claim to check. Two keys do it, and they are the same everywhere.
**`t`** puts it on the list or takes it off (the highlight stays), **`d`**
marks it **d**one or to do again. Tasks get a check box in the page's left
margin, at the height of their line.

| Key | Where | Action |
|---|---|---|
| `t` | text cursor (`v`), free cursor (`Ctrl-v`) | put the highlight under it on the task list, or take it off |
| `d` | text cursor, free cursor | mark the task under it as done, or as to do again |
| `t` | panel, Highlights and Tasks tabs | on / off the task list |
| `d`, `Space` | panel, Tasks tab | done / to do |
| `D` | panel, Tasks tab | hide / show the done ones |
| `u` | anywhere | undo, like any change |

- The panel's **Tasks** tab (`\l`, then `5`) lists them, with those to do first,
  in page order, then a "Done" line and the done ones, dimmer, with when
  ("done yesterday"). The tab title counts them, as in "Tasks 5/8". After a tick
  the selection stays in place, so the next task comes under it.
- In the **Highlights** tab, the notes that are tasks carry a small box.
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
blue = methods. Then, in the panel's **Highlights** tab (`\l`, then `4`), use these keys.

| Key | Action |
|---|---|
| `C` `V` `B` `N` `M` | show / hide that colour (several can be combined) |
| `c` | only highlights with a comment |
| `a` | all again |
| `e` | export what is shown to Markdown |

The tab title says how many are shown ("Highlights 3 of 12").

## Exporting to Markdown

`:export` writes every highlight and comment, plus your marks, to
`paper-notes.md` next to `paper.pdf`, page by page, in reading order, with
the full highlighted text (words broken across lines joined again), its
colour and the comment, like this.

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

The colour words are `yellow`, `red`, `green`, `pink`, `blue` (the five keys, in
order) and `other`; `commented` keeps only those with a comment, `tasks` only
the tasks still to do.

Running `:export` again replaces the file it wrote before, but a Markdown
file written by someone else is never overwritten without `!`. From a
terminal, `npdf --export paper.pdf` prints the same to standard output.

To search your comments across **all** your PDFs, use `\n` ([Searching](Searching.md)).

## Answering the reviewers

Did the reviewers comment on your manuscript's PDF (in Acrobat, Okular or any
other reader)? Open it, go through their comments and answer each one as follows.

1. Put the text cursor (`v`) or the free cursor (`Ctrl-v`) on a comment,
   and press `r`. Your answer is written in the comment box (equations too),
   and `Enter` keeps it.
2. `r` again on the same comment changes your answer; emptied, it is
   deleted. `u` undoes either.
3. `t` and `d` keep track of which points are dealt with, as for any task.

The answer is stored as a **reply** in the PDF, the way Acrobat and Okular
write theirs, so they show it in the comment's thread. Send the PDF back
and the reviewer sees your answers under their comments. Replies written
in other readers show up here as well. In the panel (`\l`), each comment
shows its latest reply under it; the box that opens on a comment shows the
whole thread.

`:response` writes the **response letter** in Markdown, next to the PDF
(`paper-response.md`; `:response letter.md` for another name). It is
grouped by reviewer (the author of the comments) and numbered. Each point
has its page, the text the reviewer marked, the comment, and your reply. It
ends by counting the answered points and listing those still without a
reply. Your own highlights are left out. As with `:export`, a file written
by something else is only replaced with `:response!`.

## Merging a co-author's notes

A colleague highlighted and commented a copy of the same paper, in any
reader? `:merge their-copy.pdf` brings their highlights, comments, frames,
drawings, sticky notes and images into your copy, keeping their author name
and colours.

- Notes you already have are left out, so merging twice changes nothing.
- Free text boxes are not brought in.
- One `u` takes the whole merge back.
- The message says how many were merged, already present or left out.
