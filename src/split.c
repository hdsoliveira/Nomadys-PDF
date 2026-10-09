/* Split view: two panes, side by side (Ctrl-w v, :vsplit) or one above the
 * other (Ctrl-w s, :split), each with its own place, zoom and search: the
 * figure and the text that discusses it, or two papers at once.
 *
 * The first pane shows the current tab, the second its own view (of the
 * same PDF, or of another one). Everything that works on pages does so in
 * the focused pane: while a pane is "entered", w->W and w->H are its size,
 * top_of() is 0 and the renderer's viewport is the pane, so the rest of the
 * program needs to know nothing about panes. Overlays (messages, the command
 * line, the panel, help) use the whole window. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "nomadys.h"

/* A pane's rectangle in window pixels (below the tab bar). */
static SDL_Rect pane_rect(Window *w, bool second)
{
    int W = w->in_pane ? w->full_W : w->W, H = w->in_pane ? w->full_H : w->H;
    int y0 = ui_tabbar_height(w), h = SDL_max(1, H - y0), gap = SDL_max(3, (int)(5 * w->density)); /* half the gutter */
    if (w->split_side) {
        int half = W / 2;
        return second ? (SDL_Rect){half + gap, y0, SDL_max(1, W - half - gap), h}
                      : (SDL_Rect){0, y0, SDL_max(1, half - gap), h};
    }
    int half = h / 2;
    return second ? (SDL_Rect){0, y0 + half + gap, W, SDL_max(1, h - half - gap)}
                  : (SDL_Rect){0, y0, W, SDL_max(1, half - gap)};
}

View *pane_view(Window *w, bool second) { return second ? w->split : w->tabs[w->cur]; }

/* Works in a pane from now on (see the top); false if there is no split or
 * one is entered already, and then pane_leave must not be called. */
bool pane_enter(Window *w, bool second)
{
    if (!w->split || w->in_pane || !w->ntabs)
        return false;
    SDL_Rect r = pane_rect(w, second);
    w->full_W = w->W, w->full_H = w->H;
    w->in_pane = true;
    w->pane_second = second;
    w->pane_x = r.x, w->pane_y = r.y;
    w->W = r.w, w->H = r.h;
    SDL_SetRenderViewport(w->ren, &r);
    SDL_SetRenderClipRect(w->ren, &(SDL_Rect){0, 0, r.w, r.h});
    return true;
}

void pane_leave(Window *w)
{
    if (!w->in_pane)
        return;
    w->W = w->full_W, w->H = w->full_H;
    w->in_pane = false;
    SDL_SetRenderClipRect(w->ren, NULL);
    SDL_SetRenderViewport(w->ren, NULL);
}

/* The focused pane, for keys. */
bool pane_enter_focused(Window *w) { return pane_enter(w, w->split_focus); }

/* The pane under a point (window pixels); it gets the focus. The point
 * becomes relative to the pane once entered. */
bool pane_enter_at(Window *w, float *x, float *y)
{
    if (!w->split || w->in_pane)
        return false;
    SDL_Rect second = pane_rect(w, true);
    bool in_second = w->split_side ? *x >= second.x : *y >= second.y;
    if (in_second != w->split_focus) {
        w->split_focus = in_second;
        update_title(w);
    }
    if (!pane_enter(w, in_second))
        return false;
    *x -= w->pane_x, *y -= w->pane_y;
    return true;
}

/* The file name of a pane, in a small pill at its top right. */
static void pane_label(Window *w, SDL_Rect r, View *v, bool focused)
{
    const Theme *t = uitheme(w);
    float d = w->density, pad = 8 * d, dot = focused ? 7 * d : 0;
    const char *path = v->doc->path, *slash = strrchr(path, '/');
    char name[200];
    snprintf(name, sizeof name - 4, "%s", slash ? slash + 1 : path);
    size_t n = strlen(name);
    if (n > 4 && !strcasecmp(name + n - 4, ".pdf"))
        name[n - 4] = 0;
    font_fit(w->font, name, (int)(r.w * 0.45f));
    float fh = (float)font_height(w->font), ph = fh + 6 * d;
    float pw = font_width(w->font, name) + 2 * pad + (dot ? dot + 6 * d : 0);
    float x = r.x + r.w - pw - 12 * d, y = r.y + 10 * d;
    ui_round(w, x, y + 1.5f * d, pw, ph, ph / 2, 0x000000, 40);
    ui_round(w, x, y, pw, ph, ph / 2, t->panel_bg, 236);
    float tx = x + pad;
    if (dot) { /* the pane that has the keys */
        ui_round(w, tx, y + (ph - dot) / 2, dot, dot, dot / 2, t->tab_accent, 255);
        tx += dot + 6 * d;
    }
    font_draw(w->font, tx, y + 3 * d, name, focused ? t->panel_fg : t->panel_header);
}

/* Between the panes: a gutter in the theme's chrome colour with a small
 * grip; the focused pane framed in the accent, the other one shown as it
 * is; each pane's file named in a pill. */
void split_draw_frame(Window *w)
{
    if (!w->split)
        return;
    const Theme *t = uitheme(w);
    SDL_Rect a = pane_rect(w, false), b = pane_rect(w, true), f = w->split_focus ? b : a;
    float d = w->density, line = SDL_max(2.0f, SDL_roundf(2 * d));
    uint32_t gutter = t->tab_bg, grip = rgb_mix(t->tab_bg, t->panel_header, 0.7f);
    if (w->split_side) {
        float gx = (float)(a.x + a.w), gw = (float)(b.x - a.x - a.w), cy = a.y + a.h / 2.0f;
        ui_fill(w, gx, (float)a.y, gw, (float)a.h, gutter, 255);
        ui_round(w, gx + (gw - 3 * d) / 2, cy - 18 * d, 3 * d, 36 * d, 1.5f * d, grip, 255);
    } else {
        float gy = (float)(a.y + a.h), gh = (float)(b.y - a.y - a.h), cx = a.x + a.w / 2.0f;
        ui_fill(w, (float)a.x, gy, (float)a.w, gh, gutter, 255);
        ui_round(w, cx - 18 * d, gy + (gh - 3 * d) / 2, 36 * d, 3 * d, 1.5f * d, grip, 255);
    }
    /* the focused pane: a frame in the accent, inside its edges */
    ui_fill(w, (float)f.x, (float)f.y, (float)f.w, line, t->tab_accent, 255);
    ui_fill(w, (float)f.x, (float)(f.y + f.h) - line, (float)f.w, line, t->tab_accent, 255);
    ui_fill(w, (float)f.x, (float)f.y, line, (float)f.h, t->tab_accent, 255);
    ui_fill(w, (float)(f.x + f.w) - line, (float)f.y, line, (float)f.h, t->tab_accent, 255);
    pane_label(w, a, pane_view(w, false), !w->split_focus);
    pane_label(w, b, pane_view(w, true), w->split_focus);
}

void split_open(Window *w, bool side_by_side, const char *path)
{
    if (!w->ntabs)
        return;
    View *v = NULL;
    if (path && *path) {
        char err[512];
        bool locked;
        Doc *d = doc_open_locked(path, NULL, &locked, err, sizeof err);
        if (!d) {
            ui_message(w, "%s", locked ? "that PDF has a password: open it in a tab first (:e)" : err);
            return;
        }
        v = view_new(d);
        if (!v->fit_width && v->zoom <= 0)
            v->zoom = (cfg.remember_view && d->prefs.set ? d->prefs.zoom : cfg.zoom) * 96.0 / 72.0 * w->density;
        recent_add(path);
    } else {
        v = view_clone(cur_view(w)); /* the same place, then each goes its own way */
    }
    if (w->split)
        view_free(w->split);
    w->split = v;
    w->split_side = side_by_side;
    w->split_focus = true;
    w->redraw = true;
    update_title(w);
    ui_message(w, "%s   Ctrl-w w switches   Ctrl-w q closes", side_by_side ? "side by side" : "one above the other");
}

/* Closes the focused pane (Ctrl-w q, :close) or the other one (:only). */
void split_close(Window *w, bool focused)
{
    if (!w->split) {
        ui_message(w, "there is only one pane");
        return;
    }
    pane_leave(w);
    bool drop_second = focused ? w->split_focus : !w->split_focus;
    if (!drop_second) { /* the first pane goes: its tab shows what the second showed */
        View *keep = w->split;
        view_free(w->tabs[w->cur]);
        w->tabs[w->cur] = keep;
    } else {
        view_free(w->split);
    }
    w->split = NULL;
    w->split_focus = false;
    w->redraw = true;
    update_title(w);
    session_save();
}

void split_focus_other(Window *w)
{
    if (!w->split) {
        ui_message(w, "one pane only: Ctrl-w v or Ctrl-w s splits");
        return;
    }
    w->split_focus = !w->split_focus;
    w->redraw = true;
    update_title(w);
}

/* Ctrl-w h j k l: the pane in that direction. */
void split_focus_dir(Window *w, char dir)
{
    if (!w->split)
        return;
    bool second = w->split_side ? dir == 'l' : dir == 'j';
    bool first = w->split_side ? dir == 'h' : dir == 'k';
    if (second || first) {
        w->split_focus = second;
        w->redraw = true;
        update_title(w);
    }
}

/* Ctrl-w x: the two panes change places. */
void split_swap(Window *w)
{
    if (!w->split)
        return;
    View *t = w->tabs[w->cur];
    w->tabs[w->cur] = w->split;
    w->split = t;
    w->split_focus = !w->split_focus;
    w->redraw = true;
    session_save();
}
