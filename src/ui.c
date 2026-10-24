/* Overlays drawn on top of the pages: tab bar, prompt line, messages,
 * comment box and comment markers; plus small drawing and UTF helpers. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

static int pad(Window *w) { return (int)(8 * w->density); }

void ui_fill(Window *w, float x, float y, float wd, float ht, uint32_t rgb, uint8_t a)
{
    SDL_SetRenderDrawBlendMode(w->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(w->ren, rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF, a);
    SDL_FRect r = {x, y, wd, ht};
    SDL_RenderFillRect(w->ren, &r);
}

void ui_frame(Window *w, float x, float y, float wd, float ht, float t, uint32_t rgb)
{
    ui_fill(w, x, y, wd, t, rgb, 255);
    ui_fill(w, x, y + ht - t, wd, t, rgb, 255);
    ui_fill(w, x, y, t, ht, rgb, 255);
    ui_fill(w, x + wd - t, y, t, ht, rgb, 255);
}

static void message(Window *w, int ms, const char *fmt, va_list ap)
{
    vsnprintf(w->msg, sizeof w->msg, fmt, ap);
    w->msg_until = SDL_GetTicks() + ms;
    w->redraw = true;
    const char *log = getenv("NPDF_MESSAGES"); /* tests: every message, one a line, in this file */
    FILE *f = log ? fopen(log, "a") : NULL;
    if (f) {
        fprintf(f, "%s\n", w->msg);
        fclose(f);
    }
}

void ui_message(Window *w, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    message(w, 1800, fmt, ap);
    va_end(ap);
}

void ui_message_long(Window *w, int ms, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    message(w, ms, fmt, ap);
    va_end(ap);
}

int ui_bar_height(Window *w) { return font_height(w->font) + pad(w); }
int ui_tabbar_height(Window *w) { return w->show_tabs ? ui_bar_height(w) : 0; }

static void draw_tabbar(Window *w)
{
    const Theme *t = uitheme(w);
    int h = ui_bar_height(w), p = pad(w) + (int)(4 * w->density);
    ui_fill(w, 0, 0, (float)w->W, (float)h, t->tab_bg, 255);
    float x = 0;
    int maxw = w->ntabs ? w->W / w->ntabs : w->W;
    for (int i = 0; i < w->ntabs; i++) {
        const char *path = w->tabs[i]->doc->path, *name = strrchr(path, '/');
        char label[164];
        snprintf(label, 160, "%d %s", i + 1, name ? name + 1 : path);
        font_fit(w->font, label, maxw - 2 * p);
        float tw = (float)(font_width(w->font, label) + 2 * p);
        bool active = i == w->cur;
        if (active) {
            ui_fill(w, x, 0, tw, (float)h, t->tab_active_bg, 255);
            ui_fill(w, x, h - 2 * w->density, tw, 2 * w->density, t->tab_accent, 255);
        }
        font_draw(w->font, x + p, pad(w) / 2.0f, label, active ? t->tab_active_fg : t->tab_fg);
        x += tw;
    }
}

/* How a completion option is listed: files by name, directories as "name/". */
static const char *option_label(const char *s)
{
    size_t n = strlen(s);
    const char *end = n && s[n - 1] == '/' ? s + n - 1 : s + n, *start = end;
    while (start > s && start[-1] != '/')
        start--;
    return start;
}

/* Completion options above the prompt, the current one highlighted. */
static void draw_wildmenu(Window *w)
{
    const Theme *t = uitheme(w);
    int h = ui_bar_height(w), p = pad(w), gap = 2 * p;
    float y = (float)(w->H - 2 * h);
    ui_fill(w, 0, y, (float)w->W, (float)h, t->panel_bg, 250);
    int first = 0, used = 0; /* scroll so that the current option is visible */
    for (int i = SDL_max(0, w->comp_at); i >= 0; i--) {
        used += font_width(w->font, option_label(w->comp[i])) + gap;
        if (used > w->W * 3 / 4)
            break;
        first = i;
    }
    float x = (float)p;
    for (int i = first; i < w->ncomp && x < w->W - p; i++) {
        const char *s = option_label(w->comp[i]);
        int tw = font_width(w->font, s);
        bool cur = i == w->comp_at;
        if (cur)
            ui_fill(w, x - p / 2.0f, y, (float)(tw + p), (float)h, t->panel_selected_bg, 255);
        font_draw(w->font, x, y + p / 2.0f, s, cur ? t->panel_selected_fg : t->panel_fg);
        x += tw + gap;
    }
    const char *about = w->comp_at >= 0 && w->comp_start == 0 ? command_help_for(w->comp[w->comp_at]) : NULL;
    if (about && x + font_width(w->font, about) + gap < w->W)
        font_draw(w->font, (float)(w->W - font_width(w->font, about) - p), y + p / 2.0f, about, t->panel_header);
}

static void draw_prompt(Window *w)
{
    const Theme *t = uitheme(w);
    int h = ui_bar_height(w), p = pad(w);
    if (w->ncomp > 1)
        draw_wildmenu(w);
    char line[CMD_MAX + 80];
    size_t at; /* where the cursor is in line */
    if (w->prompt_secret) { /* a password: one dot a character */
        int n = 0, before = 0;
        for (const char *c = w->cmd; *c; utf8_next(&c)) {
            n++;
            before += c - w->cmd < w->cmdpos;
        }
        snprintf(line, sizeof line, "%s", w->prompt_label);
        at = strlen(line);
        for (int i = 0; i < n && strlen(line) + 4 < sizeof line; i++) {
            SDL_strlcat(line, "\u2022", sizeof line);
            if (i < before)
                at = strlen(line);
        }
    } else {
        snprintf(line, sizeof line, "%s%s", w->prompt_label, w->cmd);
        at = strlen(w->prompt_label) + (size_t)w->cmdpos;
    }
    /* too long: scroll so that the cursor stays in view */
    char head[CMD_MAX + 80];
    const char *shown = line;
    for (;;) {
        snprintf(head, sizeof head, "%.*s", (int)(at - (size_t)(shown - line)), shown);
        if (!*shown || font_width(w->font, head) <= w->W - 3 * p)
            break;
        utf8_next(&shown);
    }
    if (shown != line) {
        char tail[CMD_MAX + 80] = "\u2026";
        SDL_strlcat(tail, shown, sizeof tail);
        snprintf(head, sizeof head, "\u2026%.*s", (int)(at - (size_t)(shown - line)), shown);
        SDL_strlcpy(line, tail, sizeof line);
    }
    ui_fill(w, 0, (float)(w->H - h), (float)w->W, (float)h, t->bar_bg, 250);
    font_draw(w->font, (float)p, (float)(w->H - h + p / 2), line, t->bar_fg);
    float cx = (float)(p + font_width(w->font, head));
    ui_fill(w, cx, (float)(w->H - h + p / 2), SDL_max(2.0f, w->density * 2), (float)font_height(w->font),
            t->bar_cursor, 255);
}

static void draw_message(Window *w)
{
    const Theme *t = uitheme(w);
    int h = ui_bar_height(w), p = pad(w);
    char text[sizeof w->msg + 4];
    snprintf(text, sizeof w->msg, "%s", w->msg);
    font_fit(w->font, text, w->W - 4 * p);
    int tw = font_width(w->font, text) + 2 * p;
    ui_fill(w, (float)(w->W - tw - p), (float)(w->H - h - p), (float)tw, (float)h, t->message_bg, 235);
    font_draw(w->font, (float)(w->W - tw), (float)(w->H - h - p + p / 2), text, t->message_fg);
}

/* The current mode, as a small coloured badge in the bottom-left corner,
 * and after it the page: 3/50. */
static void draw_mode(Window *w)
{
    const Theme *t = uitheme(w);
    View *v = cur_view(w);
    const char *label = "NORMAL";
    uint32_t color = t->mode_normal;
    switch (w->mode) {
    case MODE_NORMAL: break;
    case MODE_CARET:
        label = v->selecting ? "VISUAL" : "CURSOR";
        color = v->selecting ? t->mode_visual : t->mode_cursor;
        break;
    case MODE_AREA:
        label = v->area_selecting ? "VISUAL" : "AREA";
        color = v->area_selecting ? t->mode_visual : t->mode_area;
        break;
    case MODE_FORM: label = "INSERT", color = t->mode_insert; break;
    case MODE_PANEL: label = "PANEL", color = t->tab_accent; break;
    case MODE_HELP: label = "HELP", color = t->tab_accent; break;
    case MODE_HINTS: label = w->hint_preview ? "PREVIEW" : "FOLLOW", color = t->search_match; break;
    case MODE_PREVIEW: label = "PREVIEW", color = t->search_match; break;
    case MODE_OBJECT: label = w->obj_image ? "IMAGE" : "MOVE", color = t->mode_visual; break;
    case MODE_CROP: label = "CROP", color = t->search_match; break;
    case MODE_ROTATE: label = "ROTATE", color = t->search_match; break;
    case MODE_TEXT: label = "TEXT", color = t->mode_insert; break;
    case MODE_PICKER: label = "OPEN", color = t->tab_accent; break;
    case MODE_LIBRARY: label = "LIBRARY", color = t->tab_accent; break;
    case MODE_GRID: label = "PAGES", color = t->tab_accent; break;
    case MODE_PRESENT: label = "", color = 0; break;
    case MODE_THEMES: label = "THEME", color = t->tab_accent; break;
    case MODE_ABOUT: label = "ABOUT", color = t->tab_accent; break;
    case MODE_FORMS: label = "FORMS", color = t->tab_accent; break;
    case MODE_HITS: label = "MATCHES", color = t->tab_accent; break;
    case MODE_PROMPT:
        label = prompt_kind(w);
        color = !strcmp(label, "COMMAND") || !strcmp(label, "SEARCH") ? t->mode_command : t->mode_insert;
        break;
    }
    int p = pad(w), h = font_height(w->font) + p / 2, tw = font_width(w->font, label);
    float y = (float)(w->H - p - h);
    if (w->mode == MODE_PROMPT) /* above the prompt line and its completions */
        y -= ui_bar_height(w) * (w->ncomp > 1 ? 2 : 1);
    /* dark text on light colours, light text on dark ones */
    int lum = (int)(((color >> 16) & 0xFF) * 299 + ((color >> 8) & 0xFF) * 587 + (color & 0xFF) * 114) / 1000;
    float x = (float)p;
    if (cfg.mode_indicator) {
        ui_fill(w, x, y, (float)(tw + p), (float)h, color, 220);
        font_draw(w->font, p + p / 2.0f, y + p / 4.0f, label, lum > 150 ? 0x1E1E1E : 0xFAFAFA);
        x += tw + p + p / 2.0f;
    }
    Mode shown = w->mode == MODE_PROMPT ? w->prompt_return : w->mode; /* the pages, not a list or a panel */
    if (shown == MODE_PANEL || shown == MODE_HELP || shown == MODE_PICKER || shown == MODE_LIBRARY ||
        shown == MODE_GRID || shown == MODE_PRESENT || shown == MODE_THEMES || shown == MODE_ABOUT || shown == MODE_FORMS || shown == MODE_HITS)
        return;
    View *pv = w->split && w->split_focus ? w->split : v;
    if (!pv->doc->npages)
        return;
    char page[40];
    snprintf(page, sizeof page, "%d/%d", view_top_page(pv) + 1, pv->doc->npages);
    ui_fill(w, x, y, (float)(font_width(w->font, page) + p), (float)h, t->message_bg, 220);
    font_draw(w->font, x + p / 2.0f, y + p / 4.0f, page, t->message_fg);
}

void ui_draw_overlays(Window *w, uint64_t now)
{
    if (w->show_tabs || now < w->tabs_flash_until)
        draw_tabbar(w);
    if (w->mode == MODE_PANEL || (w->mode == MODE_PROMPT && w->prompt_return == MODE_PANEL))
        panel_draw(w);
    if (w->mode == MODE_HELP || (w->mode == MODE_PROMPT && w->prompt_return == MODE_HELP))
        help_draw(w);
    if (w->mode == MODE_HINTS || w->mode == MODE_PREVIEW || (w->mode == MODE_PROMPT && w->prompt_box)) {
        /* on the pages: in a split, those of the focused pane */
        bool pane = pane_enter_focused(w);
        if (w->mode == MODE_HINTS)
            hints_draw(w);
        else if (w->mode == MODE_PREVIEW)
            preview_draw(w);
        else
            comment_editor_draw(w);
        if (pane)
            pane_leave(w);
    }
    if (w->mode == MODE_PICKER)
        picker_draw(w);
    if (w->mode == MODE_LIBRARY)
        library_draw(w);
    if (w->mode == MODE_THEMES)
        theme_draw(w);
    if (w->mode == MODE_ABOUT)
        about_draw(w);
    if (w->mode == MODE_HITS)
        hits_draw(w);
    if (w->mode == MODE_FORMS || (w->mode == MODE_PROMPT && w->prompt_return == MODE_FORMS))
        fills_draw(w);
    draw_mode(w);
    if (w->mode == MODE_PROMPT && w->prompt_box)
        ; /* drawn above */
    else if (w->mode == MODE_PROMPT)
        draw_prompt(w);
    else if (now < w->msg_until && w->msg[0])
        draw_message(w);
}

/* Word-wrapped comment box at the bottom of the window. */
/* A ring (annulus) centred on (cx, cy): outer radius r, width th. */
void ui_ring(Window *w, float cx, float cy, float r, float th, uint32_t rgb, uint8_t a)
{
    enum { SEG = 72 };
    SDL_Vertex v[2 * (SEG + 1)];
    int idx[6 * SEG];
    SDL_FColor c = {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a / 255.0f};
    float ri = SDL_max(0.0f, r - th);
    for (int i = 0; i <= SEG; i++) {
        float ang = (float)i / SEG * 2 * SDL_PI_F, cs = SDL_cosf(ang), sn = SDL_sinf(ang);
        v[2 * i] = (SDL_Vertex){{cx + r * cs, cy + r * sn}, c, {0, 0}};
        v[2 * i + 1] = (SDL_Vertex){{cx + ri * cs, cy + ri * sn}, c, {0, 0}};
    }
    for (int i = 0; i < SEG; i++) {
        int o = 2 * i;
        int q[6] = {o, o + 1, o + 2, o + 1, o + 3, o + 2};
        memcpy(idx + 6 * i, q, sizeof q);
    }
    SDL_SetRenderDrawBlendMode(w->ren, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(w->ren, NULL, v, 2 * (SEG + 1), idx, 6 * SEG);
}

/* A rectangle with rounded corners (radius r), filled. */
void ui_round(Window *w, float x, float y, float wd, float ht, float r, uint32_t rgb, uint8_t a)
{
    enum { ARC = 6 };                    /* segments per corner */
    r = SDL_min(r, SDL_min(wd, ht) / 2);
    if (r < 0.5f) {
        ui_fill(w, x, y, wd, ht, rgb, a);
        return;
    }
    SDL_FColor c = {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a / 255.0f};
    SDL_Vertex v[1 + 4 * (ARC + 1)];
    int idx[3 * 4 * (ARC + 1)], n = 1, k = 0;
    v[0] = (SDL_Vertex){{x + wd / 2, y + ht / 2}, c, {0, 0}};
    float cx[4] = {x + wd - r, x + r, x + r, x + wd - r}, cy[4] = {y + r, y + r, y + ht - r, y + ht - r};
    for (int q = 0; q < 4; q++) /* corners counter-clockwise from the top right */
        for (int i = 0; i <= ARC; i++) {
            float ang = (q * 90.0f + i * 90.0f / ARC) * SDL_PI_F / 180.0f;
            v[n++] = (SDL_Vertex){{cx[q] + r * SDL_cosf(ang), cy[q] - r * SDL_sinf(ang)}, c, {0, 0}};
        }
    for (int i = 1; i < n; i++) {
        idx[k++] = 0;
        idx[k++] = i;
        idx[k++] = i + 1 < n ? i + 1 : 1;
    }
    SDL_SetRenderDrawBlendMode(w->ren, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(w->ren, NULL, v, n, idx, k);
}

/* A thick line from (x0, y0) to (x1, y1). */
void ui_line(Window *w, float x0, float y0, float x1, float y1, float th, uint32_t rgb)
{
    float dx = x1 - x0, dy = y1 - y0, len = SDL_sqrtf(dx * dx + dy * dy);
    if (len <= 0)
        return;
    float nx = -dy / len * th / 2, ny = dx / len * th / 2;
    SDL_FColor c = {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1};
    SDL_Vertex v[4] = {{{x0 + nx, y0 + ny}, c, {0, 0}}, {{x1 + nx, y1 + ny}, c, {0, 0}},
                       {{x1 - nx, y1 - ny}, c, {0, 0}}, {{x0 - nx, y0 - ny}, c, {0, 0}}};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetRenderDrawBlendMode(w->ren, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(w->ren, NULL, v, 4, idx, 6);
}

/* A task's check box, size across, its top left at (x, y): an outline in
 * rgb on bg while to do, filled with a tick in bg once done. */
void ui_task_box(Window *w, float x, float y, float size, bool done, uint32_t rgb, uint32_t bg)
{
    float th = SDL_max(1.5f, size * 0.14f), r = size * 0.22f;
    ui_round(w, x, y, size, size, r, rgb, 255);
    if (!done) {
        ui_round(w, x + th, y + th, size - 2 * th, size - 2 * th, SDL_max(0.0f, r - th), bg, 255);
        return;
    }
    float lw = SDL_max(1.5f, size * 0.16f);
    ui_line(w, x + size * 0.24f, y + size * 0.52f, x + size * 0.43f, y + size * 0.71f, lw, bg);
    ui_line(w, x + size * 0.40f, y + size * 0.72f, x + size * 0.77f, y + size * 0.30f, lw, bg);
}

/* WCAG contrast of two colours (1 to 21). */
static float lum(uint32_t c)
{
    float ch[3];
    for (int i = 0; i < 3; i++) {
        float x = ((c >> (16 - 8 * i)) & 0xFF) / 255.0f;
        ch[i] = x <= 0.03928f ? x / 12.92f : SDL_powf((x + 0.055f) / 1.055f, 2.4f);
    }
    return 0.2126f * ch[0] + 0.7152f * ch[1] + 0.0722f * ch[2];
}

static float contrast(uint32_t a, uint32_t b)
{
    float la = lum(a), lb = lum(b);
    return (SDL_max(la, lb) + 0.05f) / (SDL_min(la, lb) + 0.05f);
}

/* Of two inks (the theme's light and dark), the one that reads better on
 * bg; white or black when neither reads well. */
uint32_t ink_on(uint32_t bg, uint32_t a, uint32_t b)
{
    uint32_t best = contrast(a, bg) >= contrast(b, bg) ? a : b;
    if (contrast(best, bg) >= 4.5f)
        return best;
    return contrast(0xFFFFFF, bg) >= contrast(0x000000, bg) ? 0xFFFFFF : 0x111111;
}

/* The link letters: the theme's own colours, or its accent. */
uint32_t hint_bg(const Theme *t) { return t->hint_bg & THEME_AUTO ? t->tab_accent : t->hint_bg; }

/* Letters on the badge: the theme's light ink when it reads (short bold
 * letters need 3:1), which looks best on an accent; else its dark ink. */
uint32_t hint_fg(const Theme *t)
{
    if (!(t->hint_fg & THEME_AUTO))
        return t->hint_fg;
    uint32_t bg = hint_bg(t), light = lum(t->panel_bg) > lum(t->panel_fg) ? t->panel_bg : t->panel_fg;
    uint32_t dark = light == t->panel_bg ? t->panel_fg : t->panel_bg;
    if (contrast(light, bg) >= 3.0f)
        return light;
    return ink_on(bg, dark, light);
}

/* Mixes two 0xRRGGBB colours: f = 0 gives a, 1 gives b. */
uint32_t rgb_mix(uint32_t a, uint32_t b, float f)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        float ca = (float)((a >> sh) & 0xFF), cb = (float)((b >> sh) & 0xFF);
        out |= (uint32_t)(ca + (cb - ca) * f + 0.5f) << sh;
    }
    return out;
}

/* A small corner marker on highlights that carry a comment; a check box
 * in front of those that are tasks. */
void ui_draw_notes(Window *w, View *v, int top)
{
    Doc *d = v->doc;
    float size = 7 * w->density;
    for (int page = 0; page < d->npages; page++) {
        double sy = v->py[page] - v->sy + top;
        if (sy >= w->H)
            break;
        if (sy + v->phpx[page] <= top)
            continue;
        notes_page(d, page);
        PageSlot *s = &d->slots[page];
        for (int i = 0; i < s->nnotes; i++) {
            PRect r = s->notes[i].quads[0];
            float x, y;
            if (s->notes[i].task != TASK_NONE) {
                /* in the page's left margin, at the height of the first line
                 * (an area: its top), so it never covers the text */
                float box = 17 * w->density, x0, y0, x1, y1;
                page_to_screen(v, page, r.l, r.t, &x0, &y0, top);
                page_to_screen(v, page, r.l, r.b, &x1, &y1, top);
                float ty = SDL_min(y0, y1), by = SDL_max(y0, y1);
                float cy = r.t - r.b > 40 ? ty : (ty + by - box) / 2;
                ui_task_box(w, page_left_screen(v, page) + 12 * w->density, cy, box, s->notes[i].task == TASK_DONE,
                            wtheme(w)->tab_accent, v->dark ? theme_of(true)->page_bg : 0xFFFFFF);
            }
            if (!s->notes[i].has_comment || s->notes[i].textbox) /* a text box is its own text */
                continue;
            page_to_screen(v, page, r.r, r.t, &x, &y, top);
            ui_fill(w, x - size / 2, y - size / 2, size, size, wtheme(w)->comment_marker, 255);
        }
    }
}

/* ---------- memory ---------- */
static void *checked(void *p)
{
    if (!p) {
        fputs("npdf: out of memory\n", stderr);
        abort();
    }
    return p;
}

void *xmalloc(size_t n) { return checked(malloc(n ? n : 1)); }
void *xcalloc(size_t n, size_t size) { return checked(calloc(n ? n : 1, size ? size : 1)); }
void *xrealloc(void *p, size_t n) { return checked(realloc(p, n ? n : 1)); }
char *xstrdup(const char *s) { return checked(strdup(s)); }

/* ---------- UTF helpers ---------- */
unsigned utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    unsigned c = *p++;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (extra)
        c &= 0x3F >> extra;
    while (extra-- && (*p & 0xC0) == 0x80)
        c = (c << 6) | (*p++ & 0x3F);
    *s = (const char *)p;
    return c;
}

int utf8_to_utf16(const char *s, unsigned short *out, int cap)
{
    int n = 0;
    while (*s && n < cap - 2) {
        unsigned c = utf8_next(&s);
        if (c >= 0x10000) {
            c -= 0x10000;
            out[n++] = (unsigned short)(0xD800 | (c >> 10));
            out[n++] = (unsigned short)(0xDC00 | (c & 0x3FF));
        } else {
            out[n++] = (unsigned short)c;
        }
    }
    out[n] = 0;
    return n;
}

/* PDFium marks a word broken at the end of a line ("lat-" / "tice") with
 * U+0002: drop it and the line break after it, so the word is whole again. */
void text_join_hyphens(char *s)
{
    char *o = s;
    for (char *i = s; *i;) {
        if (*i == 0x02) {
            i++;
            while (*i == '\r' || *i == '\n' || *i == ' ')
                i++;
            continue;
        }
        if ((unsigned char)i[0] == 0xEF && (unsigned char)i[1] == 0xBF && (unsigned char)i[2] == 0xBE) {
            /* U+FFFE: a real hyphen at the end of a line ("high-|speed"): kept, the line joined */
            *o++ = '-';
            i += 3;
            while (*i == '\r' || *i == '\n' || *i == ' ')
                i++;
            continue;
        }
        *o++ = *i++;
    }
    *o = 0;
}

char *utf16_to_utf8(const unsigned short *s, int n)
{
    char *out = xmalloc((size_t)n * 4 + 1), *o = out;
    for (int i = 0; i < n; i++) {
        unsigned c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
            c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00);
        if (c == 0)
            break;
        if (c < 0x80) {
            *o++ = (char)c;
        } else if (c < 0x800) {
            *o++ = (char)(0xC0 | c >> 6);
            *o++ = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            *o++ = (char)(0xE0 | c >> 12);
            *o++ = (char)(0x80 | ((c >> 6) & 0x3F));
            *o++ = (char)(0x80 | (c & 0x3F));
        } else {
            *o++ = (char)(0xF0 | c >> 18);
            *o++ = (char)(0x80 | ((c >> 12) & 0x3F));
            *o++ = (char)(0x80 | ((c >> 6) & 0x3F));
            *o++ = (char)(0x80 | (c & 0x3F));
        }
    }
    *o = 0;
    return out;
}
