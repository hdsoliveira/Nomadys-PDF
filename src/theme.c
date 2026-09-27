/* Choosing the colour theme while reading: \T or :theme opens a list where
 * moving applies each theme at once (Enter keeps it and writes it in the
 * config, Esc goes back); :theme NAME switches directly. :theme new NAME
 * writes a theme of your own, every colour explained, and saving that file
 * shows the change at once. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "nomadys.h"

/* What every colour paints: written into a new theme file. */
static const struct { const char *group, *key, *what; } theme_docs[] = {
    {"Around the pages", "background", "the window behind and between the pages"},
    {NULL, "page_bg", "dark mode: the paper of the pages (light mode keeps pages white)"},
    {NULL, "page_fg", "dark mode: the ink of the pages"},
    {"Tabs (t shows them)", "tab_bg", "the tab bar"},
    {NULL, "tab_fg", "names of the other tabs"},
    {NULL, "tab_active_bg", "the tab shown"},
    {NULL, "tab_active_fg", "its name"},
    {NULL, "tab_accent", "underline of the tab shown; titles in help and panel; some mode badges"},
    {"Command line ( : and / )", "bar_bg", "the line at the bottom"},
    {NULL, "bar_fg", "its text"},
    {NULL, "bar_cursor", "its cursor, and the cursor in the comment box"},
    {"Messages", "message_bg", "the short messages at the bottom right"},
    {NULL, "message_fg", "their text"},
    {"Panel, help, lists (\\l, \\c, \\o, \\T)", "panel_bg", "their background"},
    {NULL, "panel_fg", "their text"},
    {NULL, "panel_selected_bg", "the chosen row"},
    {NULL, "panel_selected_fg", "its text"},
    {NULL, "panel_header", "dim text: folders, counts, key hints, frames"},
    {"Comments", "comment_bg", "the comment box"},
    {NULL, "comment_fg", "its text and equations"},
    {NULL, "comment_marker", "its frame, and the dot on highlights that have a comment"},
    {"On the page", "selection", "text being selected"},
    {NULL, "search_match", "search matches, link letters (f), the crop frame, the preview marks"},
    {NULL, "caret", "text cursor, free-cursor cross, the object frame, LaTeX errors"},
    {NULL, "form_focus", "the form field being filled"},
    {"Mode badge (bottom left)", "mode_normal", "reading"},
    {NULL, "mode_cursor", "text cursor"},
    {NULL, "mode_visual", "selecting; moving an image"},
    {NULL, "mode_area", "free cursor"},
    {NULL, "mode_insert", "typing in a form or a comment"},
    {NULL, "mode_command", "command line and search"},
    {NULL, "shadow", "the soft shadow around pages"},
};

/* The colour of a key in a theme (the keys above are Theme fields). */
static uint32_t color_of(const Theme *t, const char *key)
{
#define K(name) if (!strcmp(key, #name)) return t->name
    K(background); K(page_bg); K(page_fg); K(tab_bg); K(tab_fg); K(tab_active_bg); K(tab_active_fg);
    K(tab_accent); K(bar_bg); K(bar_fg); K(bar_cursor); K(message_bg); K(message_fg); K(panel_bg);
    K(panel_fg); K(panel_selected_bg); K(panel_selected_fg); K(panel_header); K(comment_bg); K(comment_fg);
    K(comment_marker); K(selection); K(search_match); K(caret); K(form_focus); K(mode_normal);
    K(mode_cursor); K(mode_visual); K(mode_area); K(mode_insert); K(mode_command); K(shadow);
#undef K
    return 0;
}

/* ---------- applying ---------- */
static int user_wd = -1, config_wd = -1;
static uint64_t own_write; /* we wrote the config: not a change to reload */

/* After the colours changed: what was drawn with the old ones goes. */
static void refresh_all(void)
{
    for (int k = 0; k < nwindows; k++) {
        Window *w = windows[k];
        for (int t = 0; t < w->ntabs; t++)
            view_invalidate_cache(w->tabs[t]); /* dark pages are drawn in the theme's paper and ink */
        if (w->split)
            view_invalidate_cache(w->split);
        if (w->shadow)
            SDL_DestroyTexture(w->shadow);
        w->shadow = NULL;
        grid_reset(w);
        present_reset(w);
        w->redraw = true;
    }
    need_redraw_all = true;
}

static bool use(Window *w, const char *name)
{
    if (!theme_apply(name)) {
        ui_message_long(w, 5000, "no theme '%s' (\\T lists them)", name);
        return false;
    }
    refresh_all();
    if (config_error[0])
        ui_message_long(w, 8000, "%s", config_error);
    return true;
}

static void remember(Window *w, const char *name)
{
    char err[256];
    own_write = SDL_GetTicks();
    if (theme_save(name, err, sizeof err))
        ui_message_long(w, 5000, "theme %s (kept in the config)", name);
    else
        ui_message_long(w, 6000, "theme %s, but %s", name, err);
}

/* ---------- the list ---------- */
static void free_list(Window *w)
{
    for (int i = 0; i < w->ntheme; i++)
        free(w->theme_names[i]);
    free(w->theme_names);
    free(w->theme_yours);
    free(w->theme_samples);
    w->theme_names = NULL;
    w->theme_yours = NULL;
    w->theme_samples = NULL;
    w->ntheme = 0;
}

void theme_open(Window *w)
{
    free_list(w);
    w->ntheme = theme_list(&w->theme_names, &w->theme_yours);
    if (!w->ntheme) {
        ui_message(w, "no themes found");
        return;
    }
    w->theme_samples = xcalloc((size_t)w->ntheme * 2, sizeof(Theme));
    w->theme_sel = 0;
    for (int i = 0; i < w->ntheme; i++) {
        theme_peek(w->theme_names[i], &w->theme_samples[2 * i], &w->theme_samples[2 * i + 1]);
        if (!strcmp(w->theme_names[i], cfg.theme))
            w->theme_sel = i;
    }
    snprintf(w->theme_orig, sizeof w->theme_orig, "%s", cfg.theme);
    w->theme_return = w->mode == MODE_THEMES ? w->theme_return : w->mode;
    w->theme_first = 0;
    w->mode = MODE_THEMES;
    w->redraw = true;
}

static void close_list(Window *w)
{
    w->mode = w->theme_return == MODE_THEMES ? MODE_NORMAL : w->theme_return;
    free_list(w);
    w->redraw = true;
}

void theme_key(Window *w, const char *tok)
{
    int d = 0;
    if (!strcmp(tok, "j") || !strcmp(tok, "<Down>") || !strcmp(tok, "<C-j>") || !strcmp(tok, "<Tab>"))
        d = 1;
    else if (!strcmp(tok, "k") || !strcmp(tok, "<Up>") || !strcmp(tok, "<C-k>") || !strcmp(tok, "<S-Tab>"))
        d = -1;
    if (d) { /* the theme under the selection, on screen at once */
        w->theme_sel = SDL_clamp(w->theme_sel + d, 0, w->ntheme - 1);
        use(w, w->theme_names[w->theme_sel]);
    } else if (!strcmp(tok, "i")) {
        cur_view(w)->dark = !cur_view(w)->dark; /* see the other half of the theme */
    } else if (!strcmp(tok, "<Enter>")) {
        char name[64];
        snprintf(name, sizeof name, "%s", w->theme_names[w->theme_sel]);
        use(w, name);
        close_list(w);
        remember(w, name);
        return;
    } else if (!strcmp(tok, "<Esc>") || !strcmp(tok, "q")) {
        if (w->theme_orig[0])
            use(w, w->theme_orig);
        close_list(w);
        return;
    }
    w->redraw = true;
}

void theme_draw(Window *w)
{
    const Theme *t = wtheme(w);
    int pad = (int)(12 * w->density), lh = font_height(w->font), row = lh + (int)(12 * w->density);
    float bw = (float)SDL_min(w->W - 2 * pad, (int)(620 * w->density));
    float bh = SDL_min((w->H - top_of(w)) * 0.8f, (float)(w->ntheme * row + 3 * lh + 3 * pad));
    float x = (w->W - bw) / 2, y = top_of(w) + (w->H - top_of(w) - bh) / 2;
    ui_fill(w, x, y, bw, bh, t->panel_bg, 255);
    ui_frame(w, x, y, bw, bh, SDL_max(1.0f, w->density), t->panel_header);
    font_draw(w->font, x + pad, y + pad / 2.0f, "Theme", t->panel_fg);
    const char *mode = cur_view(w)->dark ? "dark" : "light";
    char side[64];
    snprintf(side, sizeof side, "showing %s (i: the other)", mode);
    font_draw(w->font, x + bw - pad - font_width(w->font, side), y + pad / 2.0f, side, t->panel_header);
    float list_top = y + lh + pad * 1.5f, list_bottom = y + bh - lh - pad;
    ui_fill(w, x, list_top - pad / 2.0f, bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    ui_fill(w, x, list_bottom, bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    char foot[160];
    snprintf(foot, sizeof foot - 4, "j k try   Enter keep (written in the config)   Esc back   :theme new NAME yours");
    font_fit(w->font, foot, (int)bw - 2 * pad);
    font_draw(w->font, x + pad, list_bottom + pad / 2.0f, foot, t->panel_header);
    int rows = SDL_max(1, (int)((list_bottom - list_top) / row));
    if (w->theme_sel < w->theme_first)
        w->theme_first = w->theme_sel;
    if (w->theme_sel >= w->theme_first + rows)
        w->theme_first = w->theme_sel - rows + 1;
    float sw = lh * 0.9f, gap = 3 * w->density;
    for (int i = w->theme_first; i < w->ntheme && i < w->theme_first + rows; i++) {
        float ry = list_top + (i - w->theme_first) * row;
        bool sel = i == w->theme_sel;
        if (sel)
            ui_fill(w, x + 2, ry, bw - 4, (float)row, t->panel_selected_bg, 255);
        uint32_t fg = sel ? t->panel_selected_fg : t->panel_fg, dim = sel ? t->panel_selected_fg : t->panel_header;
        float ty = ry + (row - lh) / 2.0f;
        font_draw(w->font, x + pad, ty, w->theme_names[i], fg);
        if (w->theme_yours[i])
            font_draw(w->font, x + pad + font_width(w->font, w->theme_names[i]) + pad, ty, "yours", dim);
        /* samples: light, then dark: paper, ink, accent, search, cursor, selection */
        float sx = x + bw - pad - 13 * (sw + gap);
        for (int v = 0; v < 2; v++) {
            const Theme *th = &w->theme_samples[2 * i + v];
            uint32_t c[6] = {v ? th->page_bg : 0xFFFFFF, v ? th->page_fg : th->bar_fg, th->tab_accent, th->search_match,
                             th->caret, th->selection};
            for (int k = 0; k < 6; k++) {
                ui_fill(w, sx, ty + (lh - sw) / 2, sw, sw, c[k], 255);
                sx += sw + gap;
            }
            sx += sw + gap;
        }
    }
}

/* :theme NAME switches and keeps it; :theme alone opens the list. */
void theme_command(Window *w, const char *arg)
{
    while (*arg == ' ')
        arg++;
    if (!*arg) {
        theme_open(w);
        return;
    }
    if (!strncmp(arg, "new", 3) && (arg[3] == ' ' || !arg[3])) {
        theme_new(w, arg + 3);
        return;
    }
    char name[64];
    snprintf(name, sizeof name, "%s", arg);
    name[strcspn(name, " ")] = 0;
    if (use(w, name))
        remember(w, name);
}

/* ---------- a theme of your own ---------- */
static void write_section(FILE *f, const char *title, const Theme *t)
{
    fprintf(f, "[%s]\n", title);
    for (size_t i = 0; i < SDL_arraysize(theme_docs); i++) {
        if (theme_docs[i].group)
            fprintf(f, "\n# %s\n", theme_docs[i].group);
        char line[64];
        snprintf(line, sizeof line, "%s = #%06x", theme_docs[i].key, color_of(t, theme_docs[i].key) & 0xFFFFFF);
        fprintf(f, "%-28s # %s\n", line, theme_docs[i].what);
    }
    fputc('\n', f);
}

void theme_new(Window *w, const char *arg)
{
    char name[64];
    while (*arg == ' ')
        arg++;
    snprintf(name, sizeof name, "%s", arg);
    name[strcspn(name, " ")] = 0;
    bool ok = name[0] != 0;
    for (const char *c = name; *c; c++)
        ok &= isalnum((unsigned char)*c) || *c == '-' || *c == '_';
    if (!ok) {
        ui_message_long(w, 5000, "usage: :theme new NAME  (letters, digits, - and _)");
        return;
    }
    char dir[1100], path[1300];
    theme_user_dir(dir, sizeof dir);
    char parent[1100];
    snprintf(parent, sizeof parent, "%s", dir);
    *strrchr(parent, '/') = 0;
    mkdir(parent, 0755);
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/%s.conf", dir, name);
    struct stat st;
    if (stat(path, &st) == 0) {
        ui_message_long(w, 6000, "%s exists: edit it, or pick another name", path);
        return;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        ui_message_long(w, 6000, "cannot write %s", path);
        return;
    }
    fprintf(f, "# Nomadys-PDF theme: %s, started from %s\n"
               "#\n"
               "# Change any colour (#rrggbb) and save: the open window shows it at once.\n"
               "# [light] is used in light mode, [dark] in dark mode (i switches).\n"
               "# A line you delete keeps the default; a wrong value is reported with\n"
               "# its line and the rest still works. \\T lists this theme as \"yours\".\n\n",
            name, cfg.theme[0] ? cfg.theme : "the built-in colours");
    write_section(f, "light", &cfg.light);
    write_section(f, "dark", &cfg.dark_theme);
    bool good = fclose(f) == 0;
    if (!good) {
        ui_message_long(w, 6000, "cannot write %s", path);
        return;
    }
    theme_watch(); /* the folder may be new */
    use(w, name);
    remember(w, name);
    ui_message_long(w, 10000, "your theme: %s  (edit it; saving shows it at once)", path);
}

/* ---------- saving a theme file shows it at once ---------- */
void theme_watch(void)
{
    char dir[1100];
    theme_user_dir(dir, sizeof dir);
    if (user_wd < 0)
        user_wd = ipc_watch_dir(dir);
    *strrchr(dir, '/') = 0;
    if (config_wd < 0)
        config_wd = ipc_watch_dir(dir);
}

/* A file changed in the config or themes folder. */
bool theme_file_changed(int wd, const char *name)
{
    if (wd < 0 || (wd != user_wd && wd != config_wd) || !cfg.theme[0])
        return false;
    const char *base = strrchr(cfg.theme_path, '/') ? strrchr(cfg.theme_path, '/') + 1 : cfg.theme_path;
    bool is_config = wd == config_wd && !strcmp(name, "config");
    if (!strcmp(name, base) == 0 && !is_config)
        return false;
    if (is_config && SDL_GetTicks() - own_write < 1500)
        return true; /* our own "include = ..." */
    char theme[64];
    snprintf(theme, sizeof theme, "%s", cfg.theme);
    if (!theme_apply(theme))
        return true;
    refresh_all();
    for (int k = 0; k < nwindows; k++)
        ui_message_long(windows[k], config_error[0] ? 8000 : 2500, "%s", config_error[0] ? config_error : "theme reloaded");
    return true;
}
