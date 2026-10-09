# Keyboard reference

Every key, by mode. `\` is the leader key (configurable). `Ctrl-x` means
holding Control. A number before a key repeats it (`5j`). The same list is
always one key away in the program: `\c`.

## Reading (NORMAL)

**Moving**

| Key | Action |
|---|---|
| `j` `k` `h` `l`, arrows | scroll |
| `Ctrl-d` / `Ctrl-u` | half a screen down / up |
| `Ctrl-f` / `Ctrl-b`, `Space` | a screen down / up |
| `J` / `K` | next / previous page |
| `gg` / `G` | first / last page |
| `42G`, `42gg` | page 42 |

**View**

| Key | Action |
|---|---|
| `+` / `-` | zoom in / out |
| `=` | fit width |
| `z` | fit page |
| `d` | two pages side by side |
| `r` / `R` | turn the view 90° clockwise / counter-clockwise |
| `i` | dark mode |
| `t` | show / hide tabs |

**Search and links**

| Key | Action |
|---|---|
| `/` | search this PDF |
| `n` / `N` | next / previous match |
| `Esc` | clear the search |
| `]c` / `[c` | next / previous change (after `:diff`) |
| `f` | follow a link (type its letters) |
| `F` | preview a link's target in place |

**Highlights, marks and jumps**

| Key | Action |
|---|---|
| `v` | text cursor |
| `Ctrl-v` | free cursor |
| `u` / `Ctrl-r` | undo / redo |
| `\ma` … `\mz` | set a mark |
| `ma` … `mz` | jump to a mark |
| `mm` | back to where you were |
| `Ctrl-o` / `Ctrl-i` | older / newer place in the jump history |

**Files, windows, panels**

| Key | Action |
|---|---|
| `:` | command line |
| `\l` | panel: tabs, summary, marks, highlights |
| `\g` | page grid |
| `\o` | recent files |
| `\f` | find a PDF in your folders |
| `\/` | search the text of all your PDFs |
| `\n` | search the comments and highlights of all your PDFs |
| `\T` | choose a theme |
| `\c` | help |
| `\a` | about |
| `F5` | present |
| `Ctrl-s` | save |
| `Ctrl-p` | print |
| `Ctrl-,` / `Ctrl-.` | previous / next tab |
| `Ctrl-n` | new window on the same PDF |
| `Ctrl-j` / `Ctrl-k` | next / previous form field |
| `q` | close the window |

**Split view**

| Key | Action |
|---|---|
| `Ctrl-w v` / `Ctrl-w s` | split side by side / one above the other |
| `Ctrl-w w`, `Ctrl-w Ctrl-w` | the other pane |
| `Ctrl-w h` `j` `k` `l` | the pane in that direction |
| `Ctrl-w x` | swap the panes |
| `Ctrl-w q`, `Ctrl-w c` | close this pane |
| `Ctrl-w o` | keep only this pane |

**Mouse**

| Action | Effect |
|---|---|
| wheel | scroll (`Ctrl` + wheel: zoom) |
| click on a link | follow it |
| drag over text | select it and copy it (then `C` `V` `B` `N` `M`, `c`, `Y` work on it) |
| click on a form field | fill it |
| double click, `Ctrl` + click | open that spot in the LaTeX source |
| click on a pane | focus it |
| click on a picture, or the edge of a framed area | select it: [move, resize, turn](Images-and-Regions.md#with-the-mouse) |

## Text cursor (CURSOR / VISUAL)

| Key | Action |
|---|---|
| `h` `j` `k` `l` | move |
| `w` / `b` | next / previous word |
| `e` | end of the word |
| `0` / `$` | start / end of the line |
| `v` | start / stop selecting |
| `C` `V` `B` `N` `M` | highlight: yellow, red, green, pink, blue |
| `c` | highlight with a comment, or edit the comment under the cursor |
| `y` | copy the selected text |
| `Y` | copy it as a quotation with the page |
| `x` | delete the highlight under the cursor |
| `t` | the highlight under the cursor: on / off the task list |
| `d` | the task under the cursor: done / to do |
| `r` | reply to the comment under the cursor (a reviewer's), or change the reply |
| `Enter` | LaTeX: open this spot in the editor |
| `Ctrl-v` | switch to the free cursor |
| `Esc` | cancel the selection, then leave |

Scrolling, marks, jumps, undo, `\l` and `\c` work here too.

## Free cursor (AREA / VISUAL)

| Key | Action |
|---|---|
| `h` `j` `k` `l` | move |
| `w` `b` `J` `K`, `Ctrl-h` `Ctrl-j` `Ctrl-k` `Ctrl-l` | move faster |
| `v` | start / stop a rectangle |
| `C` `V` `B` `N` `M` | highlight the area in that colour |
| `c` | highlight with a comment, or edit the comment under the cross |
| `y` / `Y` | copy the rectangle as an image / save it as a PNG |
| `I` | put an image file here |
| `P` | paste an image from the clipboard here |
| `e` | move / resize the image, text or area under the cross |
| `T` | write text here: at the cross, or with the width marked by `v` |
| `x` | delete the highlight under the cross |
| `t` | the highlight under the cross: on / off the task list |
| `d` | the task under the cross: done / to do |
| `r` | reply to the comment under the cross, or change the reply |
| `Enter` | LaTeX: open this spot in the editor |
| `Ctrl-v` | back to the text cursor |
| `Esc` | cancel / leave |

## Writing text (TEXT)

| Key | Action |
|---|---|
| (type) | write; the box grows with the text |
| `Enter` | new line |
| `Ctrl-=` / `Ctrl--` | bigger / smaller letters |
| arrows, `Home`, `End` | move in the text |
| `Backspace`, `Delete`, `Ctrl-w` | erase a letter, the next one, the word before |
| `Ctrl-v` | paste |
| `Esc` | done |

Afterwards `e` on the box moves it (`H` `L`: width, `+` `-`: letters) and `i`
writes in it again.

## Moving and resizing (MOVE / IMAGE)

| Key | Action |
|---|---|
| `h` `j` `k` `l` (`Ctrl`: faster) | move |
| `+` / `-` | bigger / smaller, keeping the proportions |
| `H` / `L` | narrower / wider |
| `K` / `J` | shorter / taller |
| `=` | an image back to its own proportions |
| `i` | a text box: write in it again |
| `r` / `R` | turn an image 90° |
| `o` | turn an image to any angle (below) |
| `f` / `F` | mirror an image left-right / top-bottom |
| `c` | crop an image |
| `x` | delete it |
| `Enter` | done |
| `Esc` | put it back where it was |

With the mouse: drag the picture to move it, a corner to resize it (`Shift`:
freely), a side to stretch it (past the opposite side it mirrors), the
round knob above it to turn it.

## Turning freely (ROTATE)

| Key | Action |
|---|---|
| `h` / `l` | 1° counter-clockwise / clockwise (a count multiplies: `45l`) |
| `H` / `L` | 15° |
| `Ctrl-h` / `Ctrl-l` | 5° |
| `u` | upright again |
| `r` / `R` | turn 90° |
| `f` / `F` | mirror left-right / top-bottom |
| mouse | drag anywhere: it turns around its centre (`Shift`: steps of 15°) |
| `Enter` | back to moving and resizing |
| `Esc` | cancel this turning |

## Cropping (CROP)

| Key | Action |
|---|---|
| `h` `j` `k` `l` (`Ctrl`: faster) | cut the left / bottom / top / right side |
| `H` `J` `K` `L` | give that side back |
| `u` | the whole picture again |
| `Enter` | back to moving and resizing |
| `Esc` | cancel this cropping |

## Comment box (COMMENT)

| Key | Action |
|---|---|
| `Enter` | save |
| `Shift-Enter` | new line |
| `Esc` | cancel |
| arrows, `Home`, `End` | move |
| `Backspace`, `Del` | delete a character |
| `Ctrl-w` / `Ctrl-u` | delete the word before / everything |
| `Ctrl-v` | paste |

## Link letters and preview (FOLLOW / PREVIEW)

| Key | Action |
|---|---|
| letters | choose the link |
| `Backspace` | undo a letter |
| `Esc` | cancel |
| `j` `k`, `Ctrl-d` `Ctrl-u` | scroll the preview |
| `Enter` | go there (`Ctrl-o` returns) |
| `Esc`, `q` | close the preview |

## Panel (PANEL)

| Key | Action |
|---|---|
| `Tab` `Shift-Tab`, `h` `l`, `1`–`5` | change tab |
| `j` `k`, `gg` `G` | move |
| `Enter` | go there |
| `r` | rename the mark / edit the comment |
| `d` | Highlights: delete only the comment; Tasks: done / to do |
| `x` | delete the highlight or mark (in Tabs: close the file) |
| `C` `V` `B` `N` `M` | Highlights: show / hide a colour |
| `c` / `a` | Highlights: only commented / all |
| `t` | Highlights, Tasks: on / off the task list |
| `Space` | Tasks: done / to do |
| `D` | Tasks: hide / show the done ones |
| `e` | Highlights: export what is shown |
| `Esc`, `q` | close |

## Page grid (PAGES)

| Key | Action |
|---|---|
| `h` `j` `k` `l` | choose a page |
| `J` `K`, `Ctrl-d` `Ctrl-u` | a screen down / up |
| `gg` / `G` | first / last page |
| `+` / `-` | bigger / smaller thumbnails |
| `Enter` | go to the page |
| `Space` | choose / unchoose this page (pages apart too) |
| `v` | choose a run of pages |
| `x`, `d` | delete them |
| `r` / `R` | turn them in the file |
| `←` `→`, `<` `>` | move them earlier / later (side by side only) |
| `↑` `↓` | move them a row up / down |
| `y` | save them as a new PDF |
| `i` | insert a PDF after this page |
| `u` / `Ctrl-r` | undo / redo |
| `Esc`, `q`, `\g` | close |

## Presentation

| Key | Action |
|---|---|
| `j` `l` `Space` `→` `↓` `Enter` `PageDown`, click | next slide |
| `k` `h` `←` `↑` `Backspace` `PageUp`, right click | previous slide |
| `gg` `G` `12G` | first / last / slide 12 |
| `.`, `b` | black screen |
| `Esc`, `q`, `F5` | stop |

## Lists: recent files, finder, library (OPEN / LIBRARY)

| Key | Action |
|---|---|
| typing, `Backspace` | filter / search |
| `Ctrl-u` | clear |
| `Ctrl-j` `Ctrl-k`, `Ctrl-n` `Ctrl-p`, `↓` `↑` | move |
| `Tab` | recent / finder: move; library: text ⇄ comments |
| `Ctrl-t` | library: every comment ⇄ the tasks to do |
| `Enter` | open |
| `Esc` | close |

## Themes (THEME)

| Key | Action |
|---|---|
| `j` `k` | move (each theme is applied at once) |
| `i` | the other mode |
| `Enter` | keep it |
| `Esc`, `q` | back to the previous theme |

## Forms (INSERT)

| Key | Action |
|---|---|
| `Ctrl-j` `Ctrl-k`, `Tab` | next / previous field |
| typing | fill |
| `Space` | tick a checkbox |
| `Ctrl-a` `Ctrl-z` `Ctrl-v` | select all, undo, paste |
| `Esc` | leave |

## Command and search lines (COMMAND / SEARCH)

| Key | Action |
|---|---|
| `Tab` / `Shift-Tab` | complete a command or path; cycle through the options |
| `Enter` | run |
| `Esc` | cancel |
| `Ctrl-t` | search: match case exactly |
| `Backspace` | delete a character |
| `Ctrl-u` | delete everything |

## Help (HELP)

| Key | Action |
|---|---|
| `Tab`, `h` `l`, `1`–`9` | change tab |
| `j` `k`, `Ctrl-d` `Ctrl-u`, `gg` `G` | move |
| `/` | search every key |
| `Esc`, `q`, `\c` | close |
