/* ~/.config/nomadys-pdf/config: "key = value" lines, '#' comments, and
 * [light] / [dark] sections for the colour themes, and "include = name" to
 * load a theme file from themes/. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "nomadys.h"

Config cfg = {
    .dark = false,
    .autosave = true,
    .autosave_delay_ms = 1000,
    .subpixel_text = false,
    .copy_dpi = 300,
    .show_tabs = false,
    .page_shadow = true,
    .mode_indicator = true,
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
        .mode_normal = 0x7A7A7A, .mode_cursor = 0x3C8CE7, .mode_visual = 0x8E5CC7, .mode_area = 0xE07A2E,
        .mode_insert = 0x3E9E4F, .mode_command = 0xC9A227,
        .page_bg = 0xFFFFFF, .page_fg = 0x000000, .shadow = 0x000000,
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
        .mode_normal = 0x6A6A6A, .mode_cursor = 0x5AA2F0, .mode_visual = 0xB08AE0, .mode_area = 0xF39B5F,
        .mode_insert = 0x6CC47E, .mode_command = 0xE3C25A,
        .page_bg = 0x1C1C1C, .page_fg = 0xE4E4E4, .shadow = 0x000000,
    },
};

static int strcmp_ptr(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

const Theme *theme_of(bool dark) { return dark ? &cfg.dark_theme : &cfg.light; }

/* The leader key as shown in messages. */
const char *leader_label(void) { return !strcmp(cfg.leader, " ") ? "space " : cfg.leader; }

/* Theme keys, valid inside [light] and [dark]. */
#define COLOR(name) {#name, offsetof(Theme, name)}
static const struct { const char *name; size_t off; } theme_keys[] = {
    COLOR(background),
    COLOR(tab_bg), COLOR(tab_fg), COLOR(tab_active_bg), COLOR(tab_active_fg), COLOR(tab_accent),
    COLOR(bar_bg), COLOR(bar_fg), COLOR(bar_cursor),
    COLOR(message_bg), COLOR(message_fg),
    COLOR(panel_bg), COLOR(panel_fg), COLOR(panel_selected_bg), COLOR(panel_selected_fg),
    COLOR(panel_header),
    COLOR(comment_bg), COLOR(comment_fg), COLOR(comment_marker),
    COLOR(selection), COLOR(search_match), COLOR(caret), COLOR(form_focus),
    COLOR(mode_normal), COLOR(mode_cursor), COLOR(mode_visual), COLOR(mode_area), COLOR(mode_insert),
    COLOR(mode_command),
    COLOR(page_bg), COLOR(page_fg), COLOR(shadow),
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

static uint32_t gray(const char *v)
{
    uint32_t g = (uint32_t)SDL_clamp(atoi(v), 0, 255);
    return g << 16 | g << 8 | g;
}

static bool set_theme_key(Theme *t, const char *k, const char *v)
{
    if (!strcmp(k, "page_low")) { /* version 0.2: grey levels */
        t->page_bg = gray(v);
        return true;
    }
    if (!strcmp(k, "page_high")) {
        t->page_fg = gray(v);
        return true;
    }
    for (size_t i = 0; i < SDL_arraysize(theme_keys); i++)
        if (!strcmp(k, theme_keys[i].name))
            return parse_color(v, (uint32_t *)((char *)t + theme_keys[i].off));
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
    else if (!strcmp(k, "copy_dpi"))
        cfg.copy_dpi = SDL_clamp(atoi(v), 72, 1200);
    else if (!strcmp(k, "dark_images")) {
        if (!strcmp(v, "smart"))
            cfg.dark_images = DARK_IMAGES_SMART;
        else if (!strcmp(v, "keep"))
            cfg.dark_images = DARK_IMAGES_KEEP;
        else if (!strcmp(v, "invert"))
            cfg.dark_images = DARK_IMAGES_INVERT;
        else
            return false;
    } else if (!strcmp(k, "mode_indicator")) {
        cfg.mode_indicator = parse_bool(v);
    } else if (!strcmp(k, "page_shadow"))
        cfg.page_shadow = parse_bool(v);
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
        cfg.dark_theme.page_bg = gray(v);
    else if (!strcmp(k, "dark_contrast_high"))
        cfg.dark_theme.page_fg = gray(v);
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

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* "include = gruvbox" finds themes/gruvbox.conf next to the config, in
 * ~/.local/share/nomadys-pdf, or next to the program; a path is also fine. */
static bool find_include(const char *value, const char *from, char *out, size_t cap)
{
    const char *home = getenv("HOME"), *data = getenv("XDG_DATA_HOME");
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", from);
    *strrchr(dir, '/') = 0;
    if (value[0] == '~' && home) {
        snprintf(out, cap, "%s%s", home, value + 1);
        return exists(out);
    }
    if (strchr(value, '/')) {
        if (value[0] == '/')
            snprintf(out, cap, "%s", value);
        else
            snprintf(out, cap, "%s/%s", dir, value);
        return exists(out);
    }
    char exe[1024] = "";
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        *strrchr(exe, '/') = 0;
    }
    char local[1024];
    snprintf(local, sizeof local, "%s/.local/share", home ? home : "");
    const char *places[] = {dir, data && *data ? data : local, exe};
    const char *layouts[] = {"%s/themes/%s.conf", "%s/nomadys-pdf/themes/%s.conf", "%s/themes/%s.conf"};
    for (int i = 0; i < 3; i++) {
        snprintf(out, cap, layouts[i], places[i], value);
        if (exists(out))
            return true;
    }
    if (exe[0]) { /* installed: <prefix>/bin/nomadys-pdf, <prefix>/share/nomadys-pdf/themes */
        snprintf(out, cap, "%s/../share/nomadys-pdf/themes/%s.conf", exe, value);
        return exists(out);
    }
    return false;
}

/* What parse_file applies: the global settings (and includes), the colour
 * sections, or both. */
enum { P_GLOBALS = 1, P_THEMES = 2, P_ALL = 3 };

char config_error[256];      /* the first problem found, for the window */
static char config_path[1024];
static Theme builtin_light, builtin_dark;

static void problem(const char *fmt, const char *path, int n, const char *a, const char *b)
{
    char msg[256];
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    snprintf(msg, sizeof msg, fmt, name, n, a, b);
    fprintf(stderr, "npdf: %s\n", msg);
    if (!config_error[0])
        snprintf(config_error, sizeof config_error, "%s", msg);
}

static void parse_file(const char *path, int depth, Theme *light, Theme *dark, int what)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    Theme *section = NULL;
    bool in_section = false;
    char line[512];
    for (int n = 1; fgets(line, sizeof line, f); n++) {
        strip_comment(line);
        char *s = trim(line);
        if (!*s)
            continue;
        if (*s == '[') {
            in_section = true;
            if (!strcmp(s, "[light]"))
                section = light;
            else if (!strcmp(s, "[dark]"))
                section = dark;
            else
                section = NULL, problem("%s:%d: unknown section %s%s", path, n, s, "");
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) {
            problem("%s:%d: expected key = value%s%s", path, n, "", "");
            continue;
        }
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        if (!in_section && !strcmp(k, "include")) {
            char inc[2048];
            if (depth < 4 && find_include(v, path, inc, sizeof inc)) {
                if (depth == 0 && (what & P_GLOBALS)) { /* the theme the config chose */
                    snprintf(cfg.theme, sizeof cfg.theme, "%s", v);
                    snprintf(cfg.theme_path, sizeof cfg.theme_path, "%s", inc);
                }
                if (depth > 0 || (what & P_GLOBALS))
                    parse_file(inc, depth + 1, light, dark, what);
            } else {
                problem("%s:%d: cannot find the theme '%s'%s", path, n, v, "");
            }
            continue;
        }
        if (in_section && !section)
            continue;
        if (section ? !(what & P_THEMES) : !(what & P_GLOBALS))
            continue;
        bool ok = section ? set_theme_key(section, k, v) : set_global_key(k, v);
        if (!ok)
            problem("%s:%d: invalid setting '%s = %s'", path, n, k, v);
    }
    fclose(f);
}

const char *config_file(void)
{
    if (!config_path[0]) {
        const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
        if (xdg && *xdg)
            snprintf(config_path, sizeof config_path, "%s/nomadys-pdf/config", xdg);
        else
            snprintf(config_path, sizeof config_path, "%s/.config/nomadys-pdf/config", home ? home : "");
    }
    return config_path;
}

void config_load(void)
{
    builtin_light = cfg.light;
    builtin_dark = cfg.dark_theme;
    parse_file(config_file(), 0, &cfg.light, &cfg.dark_theme, P_ALL);
}

/* ---------- themes while running ---------- */
/* The colours of a theme, without applying it (the list's samples). */
bool theme_peek(const char *name, Theme *light, Theme *dark)
{
    char path[2048];
    if (!find_include(name, config_file(), path, sizeof path))
        return false;
    *light = builtin_light;
    *dark = builtin_dark;
    char saved[sizeof config_error];
    snprintf(saved, sizeof saved, "%s", config_error);
    parse_file(path, 1, light, dark, P_THEMES);
    snprintf(config_error, sizeof config_error, "%s", saved);
    return true;
}

/* Uses a theme now: its file, then the config's own [light] / [dark] lines
 * on top, as at start. False if it is not found. */
bool theme_apply(const char *name)
{
    char path[2048];
    if (!find_include(name, config_file(), path, sizeof path))
        return false;
    config_error[0] = 0;
    cfg.light = builtin_light;
    cfg.dark_theme = builtin_dark;
    parse_file(path, 1, &cfg.light, &cfg.dark_theme, P_THEMES);
    parse_file(config_file(), 0, &cfg.light, &cfg.dark_theme, P_THEMES);
    snprintf(cfg.theme, sizeof cfg.theme, "%s", name);
    snprintf(cfg.theme_path, sizeof cfg.theme_path, "%s", path);
    return true;
}

/* The folder of your own themes: next to the config. */
void theme_user_dir(char *out, size_t cap)
{
    snprintf(out, cap, "%s", config_file());
    *strrchr(out, '/') = 0;
    SDL_strlcat(out, "/themes", cap);
}

/* Every theme: your own first, then the ones that come with the program.
 * Returns how many. */
int theme_list(char ***names, bool **yours)
{
    const char *home = getenv("HOME"), *data = getenv("XDG_DATA_HOME");
    char dirs[4][1100], exe[1024] = "";
    theme_user_dir(dirs[0], sizeof dirs[0]);
    if (data && *data)
        snprintf(dirs[1], sizeof dirs[1], "%s/nomadys-pdf/themes", data);
    else
        snprintf(dirs[1], sizeof dirs[1], "%s/.local/share/nomadys-pdf/themes", home ? home : "");
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
    if (strrchr(exe, '/'))
        *strrchr(exe, '/') = 0;
    snprintf(dirs[2], sizeof dirs[2], "%s/themes", exe);
    snprintf(dirs[3], sizeof dirs[3], "%s/../share/nomadys-pdf/themes", exe);
    *names = NULL;
    *yours = NULL;
    int count = 0;
    for (int d = 0; d < 4; d++) {
        char **list = NULL;
        int nfiles = 0;
        DIR *dir = opendir(dirs[d]);
        struct dirent *e;
        while (dir && (e = readdir(dir))) {
            size_t len = strlen(e->d_name);
            if (len < 6 || strcmp(e->d_name + len - 5, ".conf"))
                continue;
            list = xrealloc(list, (size_t)(nfiles + 1) * sizeof *list);
            list[nfiles] = xstrdup(e->d_name);
            list[nfiles++][len - 5] = 0;
        }
        if (dir)
            closedir(dir);
        if (nfiles)
            qsort(list, (size_t)nfiles, sizeof *list, strcmp_ptr);
        for (int i = 0; i < nfiles; i++) {
            bool seen = false;
            for (int k = 0; k < count && !seen; k++)
                seen = !strcmp((*names)[k], list[i]);
            if (seen) {
                free(list[i]);
                continue;
            }
            *names = xrealloc(*names, (size_t)(count + 1) * sizeof **names);
            *yours = xrealloc(*yours, (size_t)(count + 1) * sizeof **yours);
            (*names)[count] = list[i];
            (*yours)[count++] = d == 0;
        }
        free(list);
    }
    return count;
}

/* Remembers the theme: the config's "include = ..." line changes (or one
 * is added at the top); everything else stays as written. */
bool theme_save(const char *name, char *err, size_t cap)
{
    const char *path = config_file();
    FILE *f = fopen(path, "r");
    char *text = NULL, line[1024];
    size_t len = 0, tcap = 0;
    bool done = false, in_section = false;
    while (f && fgets(line, sizeof line, f)) {
        const char *t = line + strspn(line, " \t");
        if (*t == '[')
            in_section = true;
        char out[1100];
        if (!done && !in_section && !strncmp(t, "include", 7) && strchr(t, '=')) {
            snprintf(out, sizeof out, "include = %s\n", name);
            done = true;
        } else {
            snprintf(out, sizeof out, "%s", line);
        }
        size_t n = strlen(out);
        if (len + n + 1 > tcap)
            text = xrealloc(text, tcap = (len + n + 1) * 2);
        memcpy(text + len, out, n + 1);
        len += n;
    }
    if (f)
        fclose(f);
    char dir[1024], tmp[1100];
    snprintf(dir, sizeof dir, "%s", path);
    *strrchr(dir, '/') = 0;
    mkdir(dir, 0755);
    snprintf(tmp, sizeof tmp, "%s.tmp%d", path, (int)getpid());
    FILE *o = fopen(tmp, "w");
    if (!o) {
        snprintf(err, cap, "cannot write %s", path);
        free(text);
        return false;
    }
    if (!done)
        fprintf(o, "# Colour theme (\\T or :theme choose it)\ninclude = %s\n\n", name);
    if (text)
        fputs(text, o);
    free(text);
    bool ok = !ferror(o);
    ok = fclose(o) == 0 && ok;
    if (!ok || rename(tmp, path)) {
        unlink(tmp);
        snprintf(err, cap, "cannot write %s", path);
        return false;
    }
    return true;
}
