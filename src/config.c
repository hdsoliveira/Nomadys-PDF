/* ~/.config/nomadys-pdf/config: "key = value" lines, '#' comments, and
 * [light] / [dark] sections for the colour themes. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

Config cfg = {
    .dark = false,
    .autosave = true,
    .autosave_delay_ms = 1000,
    .subpixel_text = false,
    .show_tabs = false,
    .restore_position = true,
    .highlights = {0xFFEE93, 0xFF9E9E, 0xA8E6A3, 0xFFB3DE, 0xA7C7F2}, /* pastel yellow, red, green, pink, blue */
    .zoom = 0,
    .font = "monospace",
    .font_size = 10.5,
    .leader = "\\",
    .synctex_editor = "nvim --headless -c \"VimtexInverseSearch %l '%f'\"",
    .light = {
        .background = 0xD9D9D9,
        .tab_bg = 0xECECEC, .tab_fg = 0x5A5A5A, .tab_active_bg = 0xFFFFFF,
        .tab_active_fg = 0x1E1E1E, .tab_accent = 0x3C5488,
        .bar_bg = 0xF4F4F4, .bar_fg = 0x1E1E1E, .bar_cursor = 0x3C5488,
        .message_bg = 0x333333, .message_fg = 0xFAFAFA,
        .panel_bg = 0xFAFAFA, .panel_fg = 0x222222, .panel_selected_bg = 0x3C5488,
        .panel_selected_fg = 0xFFFFFF, .panel_header = 0x8A8A8A,
        .comment_bg = 0xFFF8C4, .comment_fg = 0x222222, .comment_marker = 0xE64B35,
        .selection = 0x3C8CE7, .search_match = 0xFF9D00, .caret = 0xE64B35, .form_focus = 0x4DBBD5,
        .page_low = 0, .page_high = 255,
    },
    .dark_theme = {
        .background = 0x101010,
        .tab_bg = 0x1A1A1A, .tab_fg = 0x8A8A8A, .tab_active_bg = 0x2E2E2E,
        .tab_active_fg = 0xECECEC, .tab_accent = 0x4DBBD5,
        .bar_bg = 0x1E1E1E, .bar_fg = 0xECECEC, .bar_cursor = 0x4DBBD5,
        .message_bg = 0x2A2A2A, .message_fg = 0xECECEC,
        .panel_bg = 0x1C1C1C, .panel_fg = 0xDADADA, .panel_selected_bg = 0x2F4F6F,
        .panel_selected_fg = 0xFFFFFF, .panel_header = 0x7A7A7A,
        .comment_bg = 0x2B2A1E, .comment_fg = 0xF2EBC0, .comment_marker = 0xF39B7F,
        .selection = 0x3C8CE7, .search_match = 0xFF9D00, .caret = 0xF39B7F, .form_focus = 0x4DBBD5,
        .page_low = 28, .page_high = 228,
    },
};

const Theme *theme_of(bool dark) { return dark ? &cfg.dark_theme : &cfg.light; }

/* The leader key as shown in messages. */
const char *leader_label(void) { return !strcmp(cfg.leader, " ") ? "espaço " : cfg.leader; }

/* Theme keys, valid inside [light] and [dark]. */
#define COLOR(name) {#name, offsetof(Theme, name), false}
#define LEVEL(name) {#name, offsetof(Theme, name), true}
static const struct { const char *name; size_t off; bool level; } theme_keys[] = {
    COLOR(background),
    COLOR(tab_bg), COLOR(tab_fg), COLOR(tab_active_bg), COLOR(tab_active_fg), COLOR(tab_accent),
    COLOR(bar_bg), COLOR(bar_fg), COLOR(bar_cursor),
    COLOR(message_bg), COLOR(message_fg),
    COLOR(panel_bg), COLOR(panel_fg), COLOR(panel_selected_bg), COLOR(panel_selected_fg),
    COLOR(panel_header),
    COLOR(comment_bg), COLOR(comment_fg), COLOR(comment_marker),
    COLOR(selection), COLOR(search_match), COLOR(caret), COLOR(form_focus),
    LEVEL(page_low), LEVEL(page_high),
};

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

static bool parse_bool(const char *v)
{
    return !strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on") || !strcmp(v, "1");
}

static bool parse_color(const char *v, uint32_t *out)
{
    if (*v == '#')
        v++;
    char *end;
    unsigned long c = strtoul(v, &end, 16);
    if (*end || end - v != 6)
        return false;
    *out = (uint32_t)c;
    return true;
}

/* "#FFE14D, #FF9E9E, ..." into up to n colours. */
static bool parse_color_list(const char *v, uint32_t *out, int n)
{
    char buf[256];
    snprintf(buf, sizeof buf, "%s", v);
    int i = 0;
    for (char *tok = strtok(buf, ", \t"); tok && i < n; tok = strtok(NULL, ", \t"))
        if (!parse_color(tok, &out[i++]))
            return false;
    return i > 0;
}

static bool set_theme_key(Theme *t, const char *k, const char *v)
{
    for (size_t i = 0; i < SDL_arraysize(theme_keys); i++) {
        if (strcmp(k, theme_keys[i].name))
            continue;
        char *field = (char *)t + theme_keys[i].off;
        if (theme_keys[i].level)
            *(uint8_t *)field = (uint8_t)SDL_clamp(atoi(v), 0, 255);
        else if (!parse_color(v, (uint32_t *)field))
            return false;
        return true;
    }
    return false;
}

static bool set_global_key(const char *k, const char *v)
{
    if (!strcmp(k, "theme"))
        cfg.dark = !strcmp(v, "dark");
    else if (!strcmp(k, "autosave"))
        cfg.autosave = parse_bool(v);
    else if (!strcmp(k, "autosave_delay_ms"))
        cfg.autosave_delay_ms = atoi(v);
    else if (!strcmp(k, "subpixel_text"))
        cfg.subpixel_text = parse_bool(v);
    else if (!strcmp(k, "show_tabs"))
        cfg.show_tabs = parse_bool(v);
    else if (!strcmp(k, "restore_position"))
        cfg.restore_position = parse_bool(v);
    else if (!strcmp(k, "area_style"))
        cfg.area_frame = !strcmp(v, "frame");
    else if (!strcmp(k, "highlight_color"))
        return parse_color(v, &cfg.highlights[0]);
    else if (!strcmp(k, "highlight_colors"))
        return parse_color_list(v, cfg.highlights, 5);
    else if (!strcmp(k, "zoom"))
        cfg.zoom = !strcmp(v, "fit-width") ? 0 : atof(v) / 100.0;
    else if (!strcmp(k, "font"))
        snprintf(cfg.font, sizeof cfg.font, "%s", v);
    else if (!strcmp(k, "font_size"))
        cfg.font_size = SDL_clamp(atof(v), 5, 72);
    else if (!strcmp(k, "synctex_editor"))
        snprintf(cfg.synctex_editor, sizeof cfg.synctex_editor, "%s", v);
    else if (!strcmp(k, "leader"))
        snprintf(cfg.leader, sizeof cfg.leader, "%s", !strcmp(v, "space") ? " " : v);
    /* keys from version 0.1 */
    else if (!strcmp(k, "background_light"))
        return parse_color(v, &cfg.light.background);
    else if (!strcmp(k, "background_dark"))
        return parse_color(v, &cfg.dark_theme.background);
    else if (!strcmp(k, "dark_contrast_low"))
        cfg.dark_theme.page_low = (uint8_t)atoi(v);
    else if (!strcmp(k, "dark_contrast_high"))
        cfg.dark_theme.page_high = (uint8_t)atoi(v);
    else
        return false;
    return true;
}

/* '#' starts a comment at the line start or after a space, except where it
 * begins a value ("highlight_color = #FFE14D"). */
static void strip_comment(char *line)
{
    char *eq = strchr(line, '=');
    char *value = eq ? eq + 1 + strspn(eq + 1, " \t") : NULL;
    for (char *p = line; *p; p++) {
        if (*p == '#' && p != value && (p == line || isspace((unsigned char)p[-1]))) {
            *p = 0;
            return;
        }
    }
}

void config_load(void)
{
    char path[1024];
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(path, sizeof path, "%s/nomadys-pdf/config", xdg);
    else if (home)
        snprintf(path, sizeof path, "%s/.config/nomadys-pdf/config", home);
    else
        return;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    Theme *section = NULL;
    char line[512];
    for (int n = 1; fgets(line, sizeof line, f); n++) {
        strip_comment(line);
        char *s = trim(line);
        if (!*s)
            continue;
        if (*s == '[') {
            if (!strcmp(s, "[light]"))
                section = &cfg.light;
            else if (!strcmp(s, "[dark]"))
                section = &cfg.dark_theme;
            else
                fprintf(stderr, "nomadys-pdf: %s:%d: unknown section %s\n", path, n, s);
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) {
            fprintf(stderr, "nomadys-pdf: %s:%d: expected key = value\n", path, n);
            continue;
        }
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        bool ok = section ? set_theme_key(section, k, v) : set_global_key(k, v);
        if (!ok)
            fprintf(stderr, "nomadys-pdf: %s:%d: invalid setting '%s = %s'\n", path, n, k, v);
    }
    fclose(f);
}
