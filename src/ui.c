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
    const Theme *t = wtheme(w);
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

static void draw_prompt(Window *w)
{
    const Theme *t = wtheme(w);
    int h = ui_bar_height(w), p = pad(w);
    char line[CMD_MAX + 80];
    snprintf(line, sizeof line, "%s%s", w->prompt_label, w->cmd);
    ui_fill(w, 0, (float)(w->H - h), (float)w->W, (float)h, t->bar_bg, 250);
    font_draw(w->font, (float)p, (float)(w->H - h + p / 2), line, t->bar_fg);
    float cx = (float)(p + font_width(w->font, line));
    ui_fill(w, cx, (float)(w->H - h + p / 2), SDL_max(2.0f, w->density * 2), (float)font_height(w->font),
            t->bar_cursor, 255);
}

static void draw_message(Window *w)
{
    const Theme *t = wtheme(w);
    int h = ui_bar_height(w), p = pad(w);
    char text[sizeof w->msg + 4];
    snprintf(text, sizeof w->msg, "%s", w->msg);
    font_fit(w->font, text, w->W - 4 * p);
    int tw = font_width(w->font, text) + 2 * p;
    ui_fill(w, (float)(w->W - tw - p), (float)(w->H - h - p), (float)tw, (float)h, t->message_bg, 235);
    font_draw(w->font, (float)(w->W - tw), (float)(w->H - h - p + p / 2), text, t->message_fg);
}

void ui_draw_overlays(Window *w, uint64_t now)
{
    if (w->show_tabs || now < w->tabs_flash_until)
        draw_tabbar(w);
    if (w->mode == MODE_PANEL || (w->mode == MODE_PROMPT && w->prompt_return == MODE_PANEL))
        panel_draw(w);
    if (w->mode == MODE_PROMPT)
        draw_prompt(w);
    else if (now < w->msg_until && w->msg[0])
        draw_message(w);
}

/* Word-wrapped comment box at the bottom of the window. */
void ui_draw_comment(Window *w, const char *text)
{
    const Theme *t = wtheme(w);
    int p = pad(w), lh = font_height(w->font) + p / 2;
    int bw = SDL_min(w->W - 4 * p, (int)(640 * w->density)), maxw = bw - 2 * p;
    char lines[8][256];
    int n = 0;
    const char *s = text;
    while (*s && n < 8) {
        char cur[256] = "";
        const char *last_space = NULL, *start = s;
        size_t len = 0;
        while (*s && *s != '\n' && len < sizeof cur - 4) {
            const char *c = s;
            utf8_next(&s);
            memcpy(cur + len, c, (size_t)(s - c));
            len += (size_t)(s - c);
            cur[len] = 0;
            if (*c == ' ')
                last_space = s;
            if (font_width(w->font, cur) > maxw) {
                if (last_space && last_space > start) {
                    len -= (size_t)(s - last_space);
                    s = last_space;
                } else {
                    len -= (size_t)(s - c);
                    s = c;
                }
                cur[len] = 0;
                break;
            }
        }
        if (*s == '\n')
            s++;
        snprintf(lines[n++], sizeof lines[0], "%s", cur);
        if (len == 0 && *s)
            utf8_next(&s); /* a single glyph wider than the box */
    }
    int bh = n * lh + p;
    float x = (w->W - bw) / 2.0f, y = (float)(w->H - ui_bar_height(w) - 2 * p - bh);
    ui_fill(w, x, y, (float)bw, (float)bh, t->comment_bg, 245);
    ui_fill(w, x, y, 3 * w->density, (float)bh, t->comment_marker, 255);
    for (int i = 0; i < n; i++)
        font_draw(w->font, x + p, y + p / 2.0f + i * lh, lines[i], t->comment_fg);
}

/* A small corner marker on highlights that carry a comment. */
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
            if (!s->notes[i].has_comment)
                continue;
            PRect r = s->notes[i].quads[0];
            float x, y;
            page_to_screen(v, page, r.r, r.t, &x, &y, top);
            ui_fill(w, x - size / 2, y - size / 2, size, size, wtheme(w)->comment_marker, 255);
        }
    }
}

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

char *utf16_to_utf8(const unsigned short *s, int n)
{
    char *out = malloc((size_t)n * 4 + 1), *o = out;
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
