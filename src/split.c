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

#include "nomadys.h"

/* A pane's rectangle in window pixels (below the tab bar). */
static SDL_Rect pane_rect(Window *w, bool second)
{
    int W = w->in_pane ? w->full_W : w->W, H = w->in_pane ? w->full_H : w->H;
    int y0 = ui_tabbar_height(w), h = SDL_max(1, H - y0), gap = SDL_max(1, (int)w->density);
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

/* The line between the panes, and a thin accent on the focused one. */
void split_draw_frame(Window *w)
{
    if (!w->split)
        return;
    const Theme *t = wtheme(w);
    SDL_Rect a = pane_rect(w, false), b = pane_rect(w, true), f = w->split_focus ? b : a;
    float line = SDL_max(1.0f, w->density);
    if (w->split_side)
        ui_fill(w, (float)(a.x + a.w), (float)a.y, (float)(b.x - a.x - a.w), (float)a.h, t->panel_header, 160);
    else
        ui_fill(w, (float)a.x, (float)(a.y + a.h), (float)a.w, (float)(b.y - a.y - a.h), t->panel_header, 160);
    /* the focused pane: an accent along its top */
    ui_fill(w, (float)f.x, (float)f.y, (float)f.w, 2 * line, t->tab_accent, 255);
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
