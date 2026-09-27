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

## Moving, resizing, turning

Put the cross on a picture or a highlighted area and press `e`:

| Key | Action |
|---|---|
| `h` `j` `k` `l` (with `Ctrl`: faster) | move |
| `+` / `-` | bigger / smaller, keeping the proportions |
| `H` / `L` | narrower / wider |
| `K` / `J` | shorter / taller |
| `=` | a picture back to its own proportions |
| `r` / `R` | turn a picture 90° clockwise / counter-clockwise |
| `c` | crop a picture (below) |
| `x` | delete it |
| `Enter` | done |
| `Esc` | cancel: back where it was |

Highlighted *text* stays with its words; only areas and pictures move.

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
