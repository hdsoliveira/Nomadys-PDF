# Images and regions

All of this starts in the **free cursor** (`Ctrl-v`): a thin cross that can
go anywhere on the page. `h` `j` `k` `l` move it; `w` `b` `J` `K` (or
`Ctrl-h/j/k/l`) move it faster; `v` starts a rectangle.

## Copying a region as an image

For slides, a report, a message: mark a rectangle with `v`, then

| Key | Action |
|---|---|
| `y` | copy it to the clipboard as a PNG; paste it anywhere |
| `Y` | save it as a PNG next to the PDF (`paper-p5-1.png`, `paper-p5-2.png`, …) |

It is drawn at `copy_dpi` (300 dpi by default), in the page's own colours
(also in dark mode), with highlights and filled-in forms as they are. The
message says its size and resolution.

## Putting a picture on a page

A signature, a screenshot, a figure, a stamp:

| How | Action |
|---|---|
| `I` in the free cursor | choose an image file (PNG, JPEG, BMP; `Tab` completes the path) |
| `P` in the free cursor | paste an image from the clipboard |
| `:image file.png` | from anywhere |

- With a rectangle marked (`v`), the picture fits inside it.
- Otherwise its top-left corner goes at the cross, at its natural size, but
  never larger than 80% of the page.
- Transparent PNGs stay transparent.
- Right after it is placed, it can be moved and resized (below).

Pictures are standard stamp annotations: other readers show them, the
panel lists them, `x` deletes them and `u` undoes.

**Quality.** Pictures stay sharp when you zoom in. Small ones (a signature,
a screenshot) are stored with more pixels than the screen shows, enlarged
with a bicubic filter, so they stay smooth up to about 250% zoom on a HiDPI
screen. Images up to 4096 pixels are kept exactly (a JPEG is copied byte
for byte); only very large ones are reduced, never below 4096 pixels on the
longer side. A 24-megapixel photo goes in in under a second and adds about
2 MB to the file. Pictures over 120 megapixels are refused.

## Writing text on a page

For a note in the margin, a label on a figure, a line in a form that has no
field: in the free cursor (`Ctrl-v`) put the cross where the text goes, or
mark its **width** with `v` (a rectangle works too), and press `T`.

| Key | Action |
|---|---|
| (type) | write: the box **grows with the text**, which wraps at the box's width |
| `Enter` | new line |
| `Ctrl-=` / `Ctrl--` | bigger / smaller letters (`Ctrl` + wheel too); 12 pt to start with |
| arrows, `Home`, `End` | move in the text |
| `Ctrl-w`, `Backspace`, `Delete` | erase |
| `Ctrl-v` | paste |
| `Esc` | done (`u` undoes the whole box) |

The box is then like a picture. Put the cross on it (or **click** it) and
press `e`:

| Key | Action |
|---|---|
| `h` `j` `k` `l` | move it |
| `H` / `L` | narrower / wider (the text wraps again) |
| `+` / `-` | bigger / smaller letters |
| `i` | write in it again (a double click too) |
| `x` | delete it |
| `Enter` / `Esc` | keep / put it back |

With the mouse: drag the box to move it, a side or corner to change its width.

The text is **real text** in Helvetica, one of the fonts every PDF reader
has, so nothing is embedded: a paragraph adds only a few bytes a letter to the
file, and other readers show it sharp at any zoom and can select, copy and
search it. They can also move or delete the box; to *edit* it, use
Nomadys-PDF (the text, the size and the width are kept in the box).
Accents and the usual signs (quotes, dashes, the euro) work; letters outside
the Latin alphabet (Chinese, Japanese, Korean, Greek, Cyrillic) show as `?` for
now, and the text you wrote is kept in the box. Bold, italic and equations are
planned.

## Moving, resizing, turning

Put the cross on a picture or a highlighted area and press `e` (or click
the picture with the mouse: see [below](#with-the-mouse)):

| Key | Action |
|---|---|
| `h` `j` `k` `l` (with `Ctrl`: faster) | move |
| `+` / `-` | bigger / smaller, keeping the proportions |
| `H` / `L` | narrower / wider |
| `K` / `J` | shorter / taller |
| `=` | a picture back to its own proportions |
| `r` / `R` | turn a picture 90° clockwise / counter-clockwise |
| `o` | turn a picture to any angle (below) |
| `f` / `F` | mirror a picture left-right / top-bottom |
| `c` | crop a picture (below) |
| `x` | delete it |
| `Enter` | done |
| `Esc` | cancel: back where it was |

Highlighted *text* stays with its words; only areas and pictures move.

## Turning to any angle

`o` while moving a picture starts the turning mode:

| Key | Action |
|---|---|
| `h` / `l` | 1° counter-clockwise / clockwise (a count multiplies: `45l` turns 45°) |
| `H` / `L` | 15° |
| `Ctrl-h` / `Ctrl-l` | 5° |
| `u` | upright again |
| `r` / `R`, `f` / `F` | 90° turns and mirrors work here too |
| `Enter` | back to moving and resizing |
| `Esc` | cancel this turning |

The picture turns around its centre and every reader shows it turned. A
mirror (`f` left-right, `F` top-bottom) is as seen on screen, so it also
reverses the direction of the angle. Like the 90° turns, the angle and the
mirror are kept in the PDF without touching the picture: they can be undone
at any time, with no loss of quality.

A cropped picture cannot be turned to a free angle (uncrop it first with `c`
then `u`), and a picture turned to a free angle cannot be cropped (make it
upright with `o` then `u`).

## With the mouse

Everything above can be done with the mouse too; the keys and the mouse mix
freely. **Click** a picture (or the edge of a framed area) to select it: a
frame with handles appears, and the keys above work on it. Click anywhere
else to finish.

| Drag | Effect |
|---|---|
| the picture | move it |
| a corner | resize it, keeping the proportions (`Shift`: freely) |
| a side | stretch it one way |
| a side or corner **past the opposite one** | mirror it |
| the round knob above it | turn it (`Shift`: steps of 15°) |
| anywhere, in the turning mode (`o`) | turn it around its centre |

The pointer changes shape over a handle. While you drag, the picture is
drawn straight from its pixels, so it follows the mouse at once even on a
heavy page; the PDF is changed when you let go, and `Esc` or `u` puts it
back.

## Cropping

`c` while moving a picture:

| Key | Action |
|---|---|
| `h` `j` `k` `l` | cut the left / bottom / top / right side, as you see it (`Ctrl` cuts faster) |
| `H` `J` `K` `L` | give that side back |
| `u` | the whole picture again |
| `Enter` | back to moving and resizing |
| `Esc` | cancel this cropping |

The picture itself is never cut or re-encoded: the PDF keeps it whole and
only shows a part of it. So a crop, or a turn, can be undone at any time,
even after the file was closed and opened again, with no loss of quality.
