/* The help screen (\c): every key, in tabs like the panel (\l), with titled
 * groups and the keys drawn as keycaps. Generated from the keymaps
 * (help_lines in input.c). '/' searches every tab. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "nomadys.h"

#define HELP_MAX_TABS 16

/* ---------- which rows are shown ---------- */
typedef struct {
    HelpLine *lines;
    int n;
    int tab_at[HELP_MAX_TABS], ntabs; /* index of each tab's header line */
    int counts[HELP_MAX_TABS];        /* keys per tab */
    int *rows, nrows;                 /* shown: indices into lines */
    const char *filter;               /* "" = the current tab */
} Help;

static bool contains(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; hay && *hay; hay++)
        if (!strncasecmp(hay, needle, n))
            return true;
    return false;
}

static const char *live_filter(Window *w)
{
    /* while typing after '/', the list follows the text */
    if (w->mode == MODE_PROMPT && w->prompt_return == MODE_HELP)
        return w->cmd;
    return w->help_filter;
}

static void help_load(Window *w, Help *h)
{
    memset(h, 0, sizeof *h);
    h->n = help_lines(&h->lines);
    for (int i = 0; i < h->n; i++) {
        if (h->lines[i].header && h->ntabs < HELP_MAX_TABS)
            h->tab_at[h->ntabs++] = i;
        else if (!h->lines[i].group && h->ntabs)
            h->counts[h->ntabs - 1]++;
    }
    w->help_tab = SDL_clamp(w->help_tab, 0, SDL_max(0, h->ntabs - 1));
    h->filter = live_filter(w);
    h->rows = xmalloc((size_t)(h->n + 1) * sizeof *h->rows);
    if (h->filter[0]) { /* matches from every tab, under the tab's name */
        int tab_line = -1, shown_tab = -1;
        for (int i = 0; i < h->n; i++) {
            HelpLine *l = &h->lines[i];
            if (l->header)
                tab_line = i;
            if (l->header || l->group || !(contains(l->keys, h->filter) || contains(l->text, h->filter)))
                continue;
            if (shown_tab != tab_line && tab_line >= 0)
                h->rows[h->nrows++] = shown_tab = tab_line;
            h->rows[h->nrows++] = i;
        }
        return;
    }
    if (!h->ntabs)
        return;
    int from = h->tab_at[w->help_tab] + 1;
    for (int i = from; i < h->n && !h->lines[i].header; i++)
        h->rows[h->nrows++] = i;
}

static void help_free(Help *h)
{
    free(h->lines);
    free(h->rows);
}

static bool is_entry(const Help *h, int row)
{
    const HelpLine *l = &h->lines[h->rows[row]];
    return !l->header && !l->group;
}

/* The selection sits on a key, never on a title. */
static void fix_selection(Help *h, Window *w, int dir)
{
    if (!h->nrows) {
        w->help_sel = 0;
        return;
    }
    w->help_sel = SDL_clamp(w->help_sel, 0, h->nrows - 1);
    for (int step = 0; step < 2; step++, dir = -dir) {
        int i = w->help_sel;
        while (i >= 0 && i < h->nrows && !is_entry(h, i))
            i += dir;
        if (i >= 0 && i < h->nrows) {
            w->help_sel = i;
            return;
        }
    }
}

/* ---------- actions ---------- */
static int tab_for_mode(Window *w)
{
    switch (w->mode) {
    case MODE_CARET: return 1;
    case MODE_AREA: return 2;
    case MODE_PANEL: return 4;
    case MODE_PREVIEW: return 5;
    case MODE_FORM: return 7;
    default: return 0;
    }
}

void help_open(Window *w)
{
    w->help_tab = tab_for_mode(w);
    w->help_return = w->mode;
    w->help_sel = w->help_top = 0;
    w->help_filter[0] = 0;
    w->mode = MODE_HELP;
    Help h;
    help_load(w, &h);
    fix_selection(&h, w, 1);
    help_free(&h);
    w->redraw = true;
}

void help_move(Window *w, int delta)
{
    Help h;
    help_load(w, &h);
    int dir = delta > 0 ? 1 : -1, i = w->help_sel;
    for (int k = SDL_abs(delta); k > 0 && h.nrows; k--) { /* count keys, skip titles */
        int j = i + dir;
        while (j >= 0 && j < h.nrows && !is_entry(&h, j))
            j += dir;
        if (j < 0 || j >= h.nrows)
            break;
        i = j;
    }
    w->help_sel = i;
    fix_selection(&h, w, dir);
    help_free(&h);
    w->redraw = true;
}

void help_edge(Window *w, bool last)
{
    w->help_sel = last ? 1 << 20 : 0;
    Help h;
    help_load(w, &h);
    fix_selection(&h, w, last ? -1 : 1);
    help_free(&h);
    w->redraw = true;
}

/* -2 next, -1 previous, otherwise that tab. */
void help_switch(Window *w, int tab)
{
    Help h;
    help_load(w, &h);
    int nt = SDL_max(1, h.ntabs);
    help_free(&h);
    w->help_filter[0] = 0; /* a tab means leaving the search */
    if (tab == -2)
        w->help_tab = (w->help_tab + 1) % nt;
    else if (tab == -1)
        w->help_tab = (w->help_tab + nt - 1) % nt;
    else if (tab < nt)
        w->help_tab = tab;
    w->help_sel = w->help_top = 0;
    help_load(w, &h);
    fix_selection(&h, w, 1);
    help_free(&h);
    w->redraw = true;
}

static void filter_done(Window *w, const char *text)
{
    snprintf(w->help_filter, sizeof w->help_filter, "%s", text);
    w->help_sel = w->help_top = 0;
    Help h;
    help_load(w, &h);
    fix_selection(&h, w, 1);
    help_free(&h);
}

void help_search(Window *w)
{
    prompt_open(w, "search keys: ", w->help_filter, filter_done, 0);
    w->help_sel = w->help_top = 0;
}

/* Esc: leave the search first, then the help. */
void help_escape(Window *w)
{
    if (w->help_filter[0]) {
        w->help_filter[0] = 0;
        w->help_sel = w->help_top = 0;
        Help h;
        help_load(w, &h);
        fix_selection(&h, w, 1);
        help_free(&h);
    } else {
        w->mode = w->help_return;
    }
    w->redraw = true;
}

/* ---------- drawing ---------- */
typedef struct {
    Window *w;
    const Theme *t;
    float x, y, bw;
    int pad, lh, kp;         /* padding, text height, keycap padding */
} Box;

static int row_h(const Box *b, const HelpLine *l)
{
    return l->header || l->group ? b->lh + b->pad : b->lh + (int)(7 * b->w->density);
}

/* Keys separated by spaces, each in a keycap; returns the width used.
 * A command (":print [options]") is one cap followed by its arguments. */
static float draw_keys(const Box *b, const HelpLine *l, float x, float y, bool sel, bool draw)
{
    Window *w = b->w;
    const Theme *t = b->t;
    uint32_t base = sel ? t->panel_selected_bg : t->panel_bg, ink = sel ? t->panel_selected_fg : t->panel_fg;
    uint32_t cap = rgb_mix(base, ink, 0.13f), edge = rgb_mix(base, ink, 0.28f);
    float start = x, gap = 4 * w->density, ch = (float)b->lh + 2 * w->density;
    char buf[sizeof l->keys];
    snprintf(buf, sizeof buf, "%s", l->keys);
    char *args = NULL;
    if (l->command && (args = strchr(buf, ' ')))
        *args++ = 0;
    char *save = NULL;
    for (char *k = l->command ? buf : strtok_r(buf, " ", &save); k; k = l->command ? NULL : strtok_r(NULL, " ", &save)) {
        float kw = (float)font_width(w->font, k) + 2 * b->kp;
        if (draw) {
            ui_fill(w, x, y, kw, ch, cap, 255);
            ui_fill(w, x, y + ch - 2 * w->density, kw, 2 * w->density, edge, 255); /* keycap edge */
            font_draw(w->font, x + b->kp, y + w->density, k, ink);
        }
        x += kw + gap;
    }
    if (args && *args) {
        if (draw)
            font_draw(w->font, x, y + w->density, args, sel ? t->panel_selected_fg : t->panel_header);
        x += font_width(w->font, args) + gap;
    }
    return x - start;
}

static void draw_tabs(const Box *b, const Help *h)
{
    Window *w = b->w;
    const Theme *t = b->t;
    float y = b->y + b->pad / 2.0f, avail = b->bw - 2 * b->pad;
    char label[HELP_MAX_TABS][96];
    float width[HELP_MAX_TABS], gap = 1.7f * b->pad;
    for (int k = 0; k < h->ntabs; k++) {
        snprintf(label[k], sizeof label[k], "%s %d", h->lines[h->tab_at[k]].keys, h->counts[k]);
        width[k] = (float)font_width(w->font, label[k]);
    }
    /* narrow window: scroll the tab bar so the current tab shows */
    int first = 0;
    for (;;) {
        float used = 0;
        for (int k = first; k <= w->help_tab && k < h->ntabs; k++)
            used += width[k] + gap;
        if (used <= avail || first >= w->help_tab)
            break;
        first++;
    }
    float x = b->x + b->pad, right = b->x + b->bw - b->pad;
    float more = (float)font_width(w->font, "›") + b->pad / 2.0f;
    if (first > 0) { /* tabs hidden on either side */
        font_draw(w->font, x, y, "‹", t->panel_header);
        x += more;
    }
    for (int k = first; k < h->ntabs; k++) {
        float limit = k + 1 < h->ntabs ? right - more : right;
        if (x + width[k] > limit) {
            font_draw(w->font, right - more + b->pad / 2.0f, y, "›", t->panel_header);
            break;
        }
        bool cur = k == w->help_tab && !h->filter[0];
        if (cur)
            ui_fill(w, x - b->pad / 2.0f, y + b->lh + 1, width[k] + b->pad, 2 * w->density, t->tab_accent, 255);
        font_draw(w->font, x, y, label[k], cur ? t->panel_fg : t->panel_header);
        x += width[k] + gap;
    }
}

void help_draw(Window *w)
{
    const Theme *t = wtheme(w);
    Box b = {w, t, 0, 0, 0, (int)(12 * w->density), font_height(w->font), (int)(5 * w->density)};
    b.bw = (float)SDL_min(w->W - 2 * b.pad, (int)(880 * w->density));
    float bh = (w->H - top_of(w) - ui_bar_height(w)) * 0.86f;
    b.x = (w->W - b.bw) / 2;
    b.y = top_of(w) + (w->H - top_of(w) - ui_bar_height(w) - bh) / 2;
    Help h;
    help_load(w, &h);
    fix_selection(&h, w, 1);
    ui_fill(w, b.x, b.y, b.bw, bh, t->panel_bg, 255);
    ui_frame(w, b.x, b.y, b.bw, bh, SDL_max(1.0f, w->density), t->panel_header);
    float list_top = b.y + b.lh + b.pad * 1.5f, list_bottom = b.y + bh - b.lh - b.pad;
    if (h.filter[0]) {
        char head[128];
        int keys = 0;
        for (int i = 0; i < h.nrows; i++)
            keys += is_entry(&h, i);
        snprintf(head, sizeof head - 4, "Search: %.60s   %d key%s", h.filter, keys, keys == 1 ? "" : "s");
        font_fit(w->font, head, (int)b.bw - 2 * b.pad);
        font_draw(w->font, b.x + b.pad, b.y + b.pad / 2.0f, head, t->panel_fg);
    } else {
        draw_tabs(&b, &h);
    }
    ui_fill(w, b.x, list_top - b.pad / 2.0f, b.bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    ui_fill(w, b.x, list_bottom, b.bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    char foot[200];
    snprintf(foot, sizeof foot - 4, "%s", h.filter[0] ? "j k move   / new search   Esc leaves the search"
                                                  : "Tab h l  sections   1-8  go to one   j k  move   /  search   Esc  close");
    font_fit(w->font, foot, (int)b.bw - 3 * b.pad - font_width(w->font, "\u2193  888/888")); /* room for the position */
    font_draw(w->font, b.x + b.pad, list_bottom + b.pad / 2.0f, foot, t->panel_header);
    if (!h.nrows) {
        font_draw(w->font, b.x + b.pad, list_top + b.pad / 2.0f, "no key matches", t->panel_header);
        help_free(&h);
        return;
    }
    /* the key column fits the widest keys of this list, up to 45% */
    float keyw = 0;
    for (int i = 0; i < h.nrows; i++)
        if (is_entry(&h, i))
            keyw = SDL_max(keyw, draw_keys(&b, &h.lines[h.rows[i]], 0, 0, false, false));
    keyw = SDL_min(keyw, b.bw * 0.45f);
    /* keep the selection in view (rows have different heights) */
    float avail = list_bottom - list_top;
    if (w->help_sel < w->help_top)
        w->help_top = w->help_sel;
    if (w->help_top > 0 && !is_entry(&h, w->help_top - 1) && w->help_top - 1 <= w->help_sel)
        w->help_top--; /* show the title of the first group too */
    for (;;) {
        float used = 0;
        for (int i = w->help_top; i <= w->help_sel && i < h.nrows; i++)
            used += row_h(&b, &h.lines[h.rows[i]]);
        if (used <= avail || w->help_top >= w->help_sel)
            break;
        w->help_top++;
    }
    float y = list_top, colx = b.x + 1.5f * b.pad, descx = colx + keyw + b.pad;
    int last = w->help_top;
    for (int i = w->help_top; i < h.nrows; i++) {
        const HelpLine *l = &h.lines[h.rows[i]];
        int rh = row_h(&b, l);
        if (y + rh > list_bottom)
            break;
        last = i;
        if (l->header || l->group) { /* a title, with a rule after it */
            float ty = y + b.pad * 0.6f;
            font_draw(w->font, b.x + b.pad, ty, l->keys, t->tab_accent);
            float after = b.x + b.pad + font_width(w->font, l->keys) + b.pad;
            ui_fill(w, after, ty + b.lh / 2.0f, b.x + b.bw - b.pad - after, SDL_max(1.0f, w->density), t->panel_header, 60);
        } else {
            bool sel = i == w->help_sel;
            if (sel)
                ui_fill(w, b.x + 2, y, b.bw - 4, (float)rh, t->panel_selected_bg, 255);
            float ky = y + (rh - b.lh - 2 * w->density) / 2.0f;
            float used = draw_keys(&b, l, colx, ky, sel, true);
            char text[260];
            snprintf(text, 256, "%s", l->text ? l->text : "");
            float dx = SDL_max(descx, colx + used + b.pad / 2.0f);
            font_fit(w->font, text, (int)(b.x + b.bw - b.pad - dx));
            font_draw(w->font, dx, ky + w->density, text, sel ? t->panel_selected_fg : t->panel_fg);
        }
        y += rh;
    }
    /* position, and whether there is more below */
    int entries = 0, at = 0;
    for (int i = 0; i < h.nrows; i++)
        if (is_entry(&h, i)) {
            entries++;
            if (i <= w->help_sel)
                at = entries;
        }
    char pos[48];
    snprintf(pos, sizeof pos, "%s%d/%d", last + 1 < h.nrows ? "↓  " : "", at, entries);
    font_draw(w->font, b.x + b.bw - b.pad - font_width(w->font, pos), list_bottom + b.pad / 2.0f, pos, t->panel_header);
    help_free(&h);
}
