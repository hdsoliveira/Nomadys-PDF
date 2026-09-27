# Themes and dark mode

## Nine themes, each light and dark

Built from the official palettes, each with a light and a dark mode (`i`
switches between them while reading):

| Theme | Light / dark |
|---|---|
| **gruvbox** | light / dark |
| **tokyonight** | Day / Night |
| **catppuccin** | Latte / Mocha |
| **rosepine** | Dawn / Main |
| **kanagawa** | Lotus / Wave |
| **everforest** | light / dark |
| **dracula** | Alucard / Dracula |
| **nord** | light / dark |
| **solarized** | light / dark |

![the themes](../themes.png)

## Choosing one

`\T` (or `:theme`) lists the themes with small samples of both modes.

| Key | Action |
|---|---|
| `j` `k` (`↓` `↑`, `Tab`) | move; each theme is **applied at once**, so you see it on your PDF |
| `i` | show the other mode (light / dark) |
| `Enter` | keep it: writes `include = NAME` in your configuration (the rest of the file is left as it is) |
| `Esc`, `q` | go back to the theme you had |

`:theme NAME` switches directly (`Tab` completes the names).

## Your own theme

`:theme new NAME` writes `~/.config/nomadys-pdf/themes/NAME.conf`, started
from the theme in use, with every colour explained on its line:

```ini
# Around the pages
background = #282828         # the window behind and between the pages
page_bg = #282828            # dark mode: the paper of the pages
...
search_match = #fabd2f       # search matches, link letters (f), the crop frame
```

Change any `#rrggbb` and save: the open window shows it **immediately**. A
wrong value is reported with its line, and the rest still works. Your
themes appear in the `\T` list marked "yours".

A theme file has a `[light]` and a `[dark]` section. Single colours can
also be changed in the [configuration](Configuration.md#colours), in
`[light]` / `[dark]` sections after the `include` line.

## Every colour

| Name | Paints |
|---|---|
| `background` | the window behind and between the pages |
| `page_bg`, `page_fg` | dark mode: the paper and the ink of the pages |
| `shadow` | the soft shadow around pages |
| `tab_bg`, `tab_fg` | the tab bar |
| `tab_active_bg`, `tab_active_fg` | the current tab |
| `tab_accent` | accents: the current tab, the focused pane, selected rows, highlights of matches in lists |
| `bar_bg`, `bar_fg`, `bar_cursor` | the `:` and `/` line, and its cursor |
| `message_bg`, `message_fg` | messages in the corner |
| `panel_bg`, `panel_fg`, `panel_header` | the panel, help, lists; its dim text |
| `panel_selected_bg`, `panel_selected_fg` | the selected row |
| `comment_bg`, `comment_fg` | the comment box |
| `comment_marker` | the dot on highlights that have a comment |
| `selection` | text being selected |
| `search_match` | search matches, link letters (`f`), the crop frame |
| `caret` | the text cursor and the free cursor |
| `form_focus` | the form field being filled |
| `mode_normal`, `mode_cursor`, `mode_visual`, `mode_area`, `mode_insert`, `mode_command` | the mode badge |

## Dark mode

Dark mode recolours the pages: lightness is inverted but hue is kept (red
stays red), with the theme's `page_bg` and `page_fg` as paper and ink.
Images are judged one by one (`dark_images = smart`, the default):

- **photos** keep their colours;
- **figures drawn on white** (plots, diagrams, schematics: most scientific
  figures) are recoloured with the page, so no white blocks glare.

`dark_images = keep` leaves every image as it is; `invert` recolours all of
them. Copying a region as an image and printing always use the page's own
colours.

Dark mode is remembered per file.

## Fonts

The interface font is any font installed on your system, Nerd Fonts
included (`fc-list : family` lists them):

```ini
font = CaskaydiaCove NF
font_size = 10.5
```

Pages always use the fonts of the PDF itself.
