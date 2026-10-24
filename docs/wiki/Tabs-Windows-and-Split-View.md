# Tabs, windows and split view

## Tabs

Each PDF opens in a tab of the same window, using `npdf a.pdf b.pdf c.pdf` or, from
inside, `:e file.pdf`, `\o` or `\f`.

| Key | Action |
|---|---|
| `Ctrl-,` / `Ctrl-.` | previous / next tab |
| `Backspace` | back to the tab you came from (again to return) |
| `t` | show / hide the tab bar |
| `:e file.pdf` | open in a new tab (`Tab` completes the path) |
| `:q` | close the tab (`:q!` discarding unsaved changes; `:wq`, `:x` save first) |
| `\l`, then the **Tabs** tab | all open files, where `Enter` switches and `x` closes |

From a shell or another program, `npdf --tab file.pdf` adds a tab to the npdf
that is running (see the [FAQ](FAQ.md) for neomutt).

The tab bar is hidden by default, to leave the whole window to the page; it
shows for a moment when you switch tabs. `show_tabs = true` in the
[configuration](Configuration.md) keeps it visible.

## Windows

| Key | Action |
|---|---|
| `Ctrl-n` | a new window on the same PDF (edits are shared) |
| `q` | close the window |

## Split view

Two panes in one window, each with its own place, zoom, dark mode and
search, for example the figure and the text that discusses it, the paper and its
supplementary material, or two papers side by side.

| Key | Action |
|---|---|
| `Ctrl-w v` | split side by side, on the same PDF |
| `Ctrl-w s` | split one above the other |
| `:vsplit other.pdf` / `:split other.pdf` | the same, with another PDF in the new pane |
| `Ctrl-w w`, `Ctrl-w Ctrl-w` | go to the other pane |
| `Ctrl-w h` `j` `k` `l` | go to the pane to the left / below / above / right |
| `Ctrl-w x` | the panes change places |
| `Ctrl-w q`, `Ctrl-w c`, `:close` | close this pane |
| `Ctrl-w o`, `:only` | keep only this pane |

Keys act on the **focused** pane, which is framed in the theme's accent colour
and has a dot in its name, while the other pane is shown as it is, not dimmed. Each
pane shows its file name in a small label at the top right. A click or the
mouse wheel over a pane focuses it. Search, marks, highlighting and zoom
all work in each pane as in a whole window.

The first pane shows the current tab; switching tabs changes it, and the
second pane stays.

## Sessions

The open tabs are remembered as they change, even if the program is
killed. `npdf` with no file reopens them (so does `:session` from inside).
Each file opens where you stopped reading.
