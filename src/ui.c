/* Minimal UI: embedded bitmap font, tab bar, messages, command line. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "nomadys.h"

#define NGLYPHS ((int)(sizeof font_glyphs / sizeof font_glyphs[0]))
#define OVERLAY_BG 0x1E1E1E
#define OVERLAY_FG 0xECECEC
#define ACCENT 0x4DBBD5

static int glyph_index(unsigned c)
{
    if (c >= 32 && c < 127)
        return (int)c - 32;
    if (c >= FONT_FIRST_HI && c < 256)
        return 95 + (int)c - FONT_FIRST_HI;
    return '?' - 32;
}

void ui_init(Window *w)
{
    int aw = FONT_W * NGLYPHS;
    uint32_t *px = malloc((size_t)aw * FONT_H * 4);
    for (int g = 0; g < NGLYPHS; g++)
        for (int y = 0; y < FONT_H; y++)
            for (int x = 0; x < FONT_W; x++)
                px[y * aw + g * FONT_W + x] = ((uint32_t)font_glyphs[g][y * FONT_W + x] << 24) | 0xFFFFFF;
    w->font = SDL_CreateTexture(w->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, aw, FONT_H);
    SDL_UpdateTexture(w->font, NULL, px, aw * 4);
    SDL_SetTextureBlendMode(w->font, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(w->font, SDL_SCALEMODE_LINEAR);
    free(px);
}

static int scale_of(Window *w) { return w->density >= 1.5f ? 2 : 1; }

/* Decode one UTF-8 sequence; returns the code point and advances *s. */
static unsigned next_cp(const char **s)
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

int ui_text_width(const char *s, int scale)
{
    int n = 0;
    while (*s) {
        next_cp(&s);
        n++;
    }
    return n * FONT_W * scale;
}

void ui_text(Window *w, float x, float y, const char *s, uint32_t rgb, int scale)
{
    SDL_SetTextureColorMod(w->font, rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF);
    while (*s) {
        int g = glyph_index(next_cp(&s));
        SDL_FRect src = {(float)(g * FONT_W), 0, FONT_W, FONT_H};
        SDL_FRect dst = {x, y, (float)(FONT_W * scale), (float)(FONT_H * scale)};
        SDL_RenderTexture(w->ren, w->font, &src, &dst);
        x += FONT_W * scale;
    }
}

void ui_fill(Window *w, float x, float y, float wd, float ht, uint32_t rgb, uint8_t a)
{
    SDL_SetRenderDrawBlendMode(w->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(w->ren, rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF, a);
    SDL_FRect r = {x, y, wd, ht};
    SDL_RenderFillRect(w->ren, &r);
}

void ui_message(Window *w, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(w->msg, sizeof w->msg, fmt, ap);
    va_end(ap);
    w->msg_until = SDL_GetTicks() + 1800;
    w->redraw = true;
}

int ui_tabbar_height(Window *w)
{
    return w->show_tabs ? (FONT_H + 8) * scale_of(w) : 0;
}

static void draw_tabbar(Window *w)
{
    int sc = scale_of(w), h = (FONT_H + 8) * sc, pad = 10 * sc;
    ui_fill(w, 0, 0, (float)w->W, (float)h, OVERLAY_BG, 235);
    float x = 0;
    int maxw = w->ntabs ? w->W / w->ntabs : w->W;
    for (int i = 0; i < w->ntabs; i++) {
        const char *path = w->tabs[i]->doc->path, *name = strrchr(path, '/');
        name = name ? name + 1 : path;
        char label[128];
        snprintf(label, sizeof label, "%d %s", i + 1, name);
        int chars = SDL_max(4, (maxw - 2 * pad) / (FONT_W * sc));
        if ((int)strlen(label) > chars) {
            label[chars - 1] = 0;
            strcat(label, "~");
        }
        float tw = (float)(ui_text_width(label, sc) + 2 * pad);
        if (i == w->cur) {
            ui_fill(w, x, 0, tw, (float)h, 0x3A3A3A, 255);
            ui_fill(w, x, (float)(h - 2 * sc), tw, (float)(2 * sc), ACCENT, 255);
        }
        ui_text(w, x + pad, (float)(4 * sc), label, i == w->cur ? OVERLAY_FG : 0x9A9A9A, sc);
        x += tw;
    }
}

void ui_draw_overlays(Window *w, uint64_t now)
{
    int sc = scale_of(w), lh = (FONT_H + 8) * sc, pad = 8 * sc;
    if (w->show_tabs || now < w->tabs_flash_until)
        draw_tabbar(w);
    if (w->mode == MODE_COMMAND || w->mode == MODE_SEARCH) {
        char line[CMD_MAX + 2];
        snprintf(line, sizeof line, "%c%s", w->mode == MODE_COMMAND ? ':' : '/', w->cmd);
        ui_fill(w, 0, (float)(w->H - lh), (float)w->W, (float)lh, OVERLAY_BG, 240);
        ui_text(w, (float)pad, (float)(w->H - lh + 4 * sc), line, OVERLAY_FG, sc);
        float cx = (float)(pad + ui_text_width(line, sc));
        ui_fill(w, cx, (float)(w->H - lh + 3 * sc), (float)(2 * sc), (float)(FONT_H * sc + 2 * sc), ACCENT, 255);
    } else if (now < w->msg_until && w->msg[0]) {
        int tw = ui_text_width(w->msg, sc) + 2 * pad;
        ui_fill(w, (float)(w->W - tw - pad), (float)(w->H - lh - pad), (float)tw, (float)lh, OVERLAY_BG, 225);
        ui_text(w, (float)(w->W - tw), (float)(w->H - lh - pad + 4 * sc), w->msg, OVERLAY_FG, sc);
    }
}

/* ---------- UTF helpers ---------- */
int utf8_to_utf16(const char *s, unsigned short *out, int cap)
{
    int n = 0;
    while (*s && n < cap - 2) {
        unsigned c = next_cp(&s);
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
