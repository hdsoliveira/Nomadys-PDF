/* The program's icon (window, about screen) and the about screen (\a or
 * :about): who made it, the version, what the name and the icon mean. */
#include <stdio.h>
#include <string.h>

#include "icon_png.h"
#include "nomadys.h"

/* The icon as an SDL surface (decoded from the PNG built in); caller frees. */
SDL_Surface *about_icon(void) { return image_surface(icon_png, sizeof icon_png); }

void about_open(Window *w)
{
    if (!w->about_tex) {
        SDL_Surface *s = about_icon();
        if (s) {
            w->about_tex = SDL_CreateTextureFromSurface(w->ren, s);
            SDL_DestroySurface(s);
            if (w->about_tex)
                SDL_SetTextureScaleMode(w->about_tex, SDL_SCALEMODE_LINEAR);
        }
    }
    w->about_return = w->mode == MODE_ABOUT ? w->about_return : w->mode;
    w->mode = MODE_ABOUT;
    w->redraw = true;
}

void about_close(Window *w)
{
    w->mode = w->about_return == MODE_ABOUT ? MODE_NORMAL : w->about_return;
    if (w->about_tex)
        SDL_DestroyTexture(w->about_tex);
    w->about_tex = NULL;
    w->redraw = true;
}

void about_key(Window *w, const char *tok)
{
    (void)tok; /* any key closes it */
    about_close(w);
}

/* Lines of text, centred and wrapped at spaces to width bw; returns the
 * y below them. With draw false it only measures. */
static float centred(Window *w, float x, float bw, float y, const char *text, uint32_t rgb, bool draw)
{
    char line[256] = "", word[128];
    const char *p = text;
    float lh = font_height(w->font) + 4 * w->density;
    while (*p) {
        size_t n = strcspn(p, " ");
        snprintf(word, sizeof word, "%.*s", (int)SDL_min(n, sizeof word - 1), p);
        p += n + (p[n] == ' ');
        char next[sizeof line + sizeof word + 2];
        snprintf(next, sizeof next, "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && font_width(w->font, next) > bw) { /* the word goes on the next line */
            if (draw)
                font_draw(w->font, x + (bw - font_width(w->font, line)) / 2, y, line, rgb);
            y += lh;
            SDL_strlcpy(line, word, sizeof line);
        } else {
            SDL_strlcpy(line, next, sizeof line); /* a very long line is cut */
        }
    }
    if (line[0] && draw)
        font_draw(w->font, x + (bw - font_width(w->font, line)) / 2, y, line, rgb);
    return y + lh;
}

/* The text below the icon; returns its height, drawing it when draw is true. */
static float body(Window *w, float x, float bw, float y, bool draw)
{
    const Theme *t = wtheme(w);
    float y0 = y, gap = font_height(w->font) * 0.6f;
    y = centred(w, x, bw, y, "Nomadys-PDF", t->panel_fg, draw);
    y = centred(w, x, bw, y, "version " NOMADYS_VERSION "   \u00b7   the command is npdf", t->panel_header, draw);
    y += gap;
    y = centred(w, x, bw, y, "Nomadys: a nomad, with no fixed place, always looking for something better.", t->panel_fg, draw);
    y = centred(w, x, bw, y,
                "A tent at home anywhere, a trail towards the mountain and the rising sun, and a page lit in "
                "the doorway: reading goes wherever you go.",
                t->panel_header, draw);
    y += gap;
    if (draw)
        ui_fill(w, x + bw * 0.3f, y, bw * 0.4f, SDL_max(1.0f, w->density), t->panel_header, 120);
    y += gap;
    y = centred(w, x, bw, y, "Made by " NOMADYS_AUTHOR, t->panel_fg, draw);
    y = centred(w, x, bw, y, NOMADYS_CONTACT, t->tab_accent, draw);
    y = centred(w, x, bw, y, NOMADYS_URL, t->tab_accent, draw);
    y += gap;
    y = centred(w, x, bw, y, "MIT licence   \u00b7   PDFium, SDL3, FreeType, fontconfig, zlib, stb", t->panel_header, draw);
    y = centred(w, x, bw, y, "their licences: share/doc/nomadys-pdf/licenses", t->panel_header, draw);
    y += gap * 0.5f;
    y = centred(w, x, bw, y, "any key closes this", t->panel_header, draw);
    return y - y0;
}

void about_draw(Window *w)
{
    const Theme *t = wtheme(w);
    float pad = 18 * w->density;
    float icon = SDL_min(160 * w->density, (w->H - top_of(w)) * 0.28f);
    float bw = (float)SDL_min(w->W - 2 * (int)pad, (int)(600 * w->density)), inner = bw - 2 * pad;
    float bh = pad * 2 + icon + pad * 0.8f + body(w, 0, inner, 0, false);
    float x = (w->W - bw) / 2, y = top_of(w) + SDL_max(pad, (w->H - top_of(w) - bh) / 2);
    ui_fill(w, x, y, bw, bh, t->panel_bg, 255);
    ui_frame(w, x, y, bw, bh, SDL_max(1.0f, w->density), t->panel_header);
    if (w->about_tex) {
        SDL_FRect dst = {x + (bw - icon) / 2, y + pad, icon, icon};
        SDL_RenderTexture(w->ren, w->about_tex, NULL, &dst);
    }
    body(w, x + pad, inner, y + pad + icon + pad * 0.8f, true);
}
