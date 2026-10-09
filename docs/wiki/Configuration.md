# Configuration

Settings live in one plain text file, `~/.config/nomadys-pdf/config`. It
does not need to exist, since every setting has a default. `config.example` in
the source lists them all with comments, and you can copy it to start.

```sh
mkdir -p ~/.config/nomadys-pdf
cp config.example ~/.config/nomadys-pdf/config
```

- One setting per line, written as `name = value`.
- `#` starts a comment (at the start of a line or after a space).
- Colours are `#RRGGBB`.
- A mistake is reported with its line number when the program starts, and
  the other lines still apply.
- Changes apply the next time you open Nomadys-PDF; themes reload at once.

## Reading

| Setting | Default | Meaning |
|---|---|---|
| `theme` | `light` | start in `light` or `dark` mode (`i` switches while reading) |
| `zoom` | `fit-width` | zoom for a file opened the first time, `fit-width` or a percentage such as `125` |
| `restore_position` | `true` | reopen each file at the page where you stopped |
| `remember_view` | `true` | and with its own zoom, dark mode, two-page layout and rotation |
| `dark_images` | `smart` | in dark mode, `smart` keeps photos and the pictures you placed, and recolours figures on white; `keep` leaves every image; `invert` recolours all |
| `dark_text_weight` | `2.5` | in dark mode, how firm light letters and lines are drawn; `1` is a plain inversion, higher is bolder |
| `subpixel_text` | `false` | sharper text on LCD monitors (subpixel rendering) |
| `page_shadow` | `true` | the soft shadow around pages |
| `show_tabs` | `false` | keep the tab bar visible (`t` toggles it) |
| `mode_indicator` | `true` | the small badge showing the mode (`NORMAL`, `CURSOR`, …) |

## Keys

| Setting | Default | Meaning |
|---|---|---|
| `leader` | `\` | the key that starts `\c`, `\l`, `\g` and more, which can be `\`, `,`, `;` or `space` |

## Highlights and notes

| Setting | Default | Meaning |
|---|---|---|
| `highlight_colors` | `#FFEE93, #FF9E9E, #A8E6A3, #FFB3DE, #A7C7F2` | the five colours of `C` `V` `B` `N` `M` (yellow, red, green, pink, blue) |
| `area_style` | `highlight` | for figures and areas, `highlight` (transparent colour) or `frame` (a coloured rectangle around) |
| `cite_format` | `“%t” (p. %p)` | what `Y` copies, with `%t` for the text and `%p` for the page(s) |
| `copy_dpi` | `300` | resolution of a region copied as an image (`y` / `Y` in the free cursor), 72–1200 |

## Files and saving

| Setting | Default | Meaning |
|---|---|---|
| `autosave` | `true` | save highlights, comments and form entries automatically; `false` saves only with `Ctrl-s` / `:w` (closing then asks) |
| `autosave_delay_ms` | `1000` | how long after a change to save |
| `backup` | `true` | before the first save of a session, keep the file as it was, next to it, as `name.pdf.bak` (a hard link, so it is instant whatever the size); the next session replaces it |
| `pdf_dirs` | `~` | where `\f`, `\/` and `\n` look for PDFs, as a comma separated list such as `~/Papers, ~/Books, ~/Nextcloud` |

## LaTeX

| Setting | Default | Meaning |
|---|---|---|
| `synctex_editor` | `nvim --headless -c "VimtexInverseSearch %l '%f'"` | the command for inverse search, where `%l` is the line, `%c` the column and `%f` the file |

## Look

| Setting | Default | Meaning |
|---|---|---|
| `include` | (none) | a colour theme, as in `include = gruvbox` (`\T` writes this line for you) |
| `font` | `monospace` | the font of the interface (tabs, help, panel, command line, comments, messages), any installed family such as `JetBrains Mono`, `CaskaydiaCove NF` or `DejaVu Sans`; `fc-list : family` lists them |
| `font_size` | `10.5` | its size in points |

A font name that is not installed is reported, and a default is used.

## Colours

After the settings, `[light]` and `[dark]` sections set single colours of
the light and dark mode, on top of the theme.

```ini
include = nord

[dark]
background = #101010
page_bg = #1C1C1C          # dark mode: the paper of the pages
page_fg = #E4E4E4          # dark mode: their ink
```

Every colour name, with what it paints, is listed in
[Themes and dark mode](Themes-and-Dark-Mode.md#every-colour).

## An example

```ini
include = gruvbox
theme = dark
zoom = 125
pdf_dirs = ~/Papers, ~/Nextcloud/Library
highlight_colors = #FFE066, #FF8A80, #B9F6CA, #F8BBD0, #90CAF9
area_style = frame
cite_format = "%t" (p. %p)
font = JetBrains Mono
font_size = 11
leader = space
```

## Older names

Settings from earlier versions are still understood, such as `highlight_color`
(the first colour only), `background_light`, `background_dark`,
`dark_contrast_low` / `page_low` (the dark paper, as a grey level) and
`dark_contrast_high` / `page_high` (the dark ink).
