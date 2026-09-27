/* Presentation (F5 or :present): fullscreen, one page at a time on black,
 * nothing else on screen. Slides are drawn at the screen's size, apart from
 * the reading view (its zoom and place stay as they were), and the next
 * one is prepared while idle, so going forward is instant. */
#include <stdio.h>
#include <string.h>

#include "nomadys.h"

#define SLIDES 3             /* the one shown, the next, the previous */

struct Slide {
    const Doc *doc;
    int page, w, h;
    bool dark;
    unsigned version;
    SDL_Texture *tex;
    uint64_t used;
};

static uint64_t slide_clock;

/* The page's size in pixels to fill the screen, keeping its shape. */
static void slide_size(Window *w, View *v, int page, int *sw, int *sh)
{
    float pw = view_page_w(v, page), ph = view_page_h(v, page);
    float s = SDL_min(w->W / pw, w->H / ph);
    *sw = SDL_max(1, (int)(pw * s)), *sh = SDL_max(1, (int)(ph * s));
}

static Slide *slide(Window *w, View *v, int page, bool render)
{
    int sw, sh;
    slide_size(w, v, page, &sw, &sh);
    Slide *free_slot = &w->slides[0];
    for (int i = 0; i < SLIDES; i++) {
        Slide *s = &w->slides[i];
        if (s->tex && s->doc == v->doc && s->page == page && s->w == sw && s->h == sh && s->dark == v->dark &&
            s->version == v->doc->slots[page].version) {
            s->used = ++slide_clock;
            return s;
        }
        if (!s->tex || s->used < free_slot->used)
            free_slot = s;
    }
    if (!render)
        return NULL;
    if (free_slot->tex)
        SDL_DestroyTexture(free_slot->tex);
    SDL_Texture *tex = view_render_area(w, v, page, sw, sh, v->rotation, 0, 0, sw, sh);
    *free_slot = (Slide){v->doc, page, sw, sh, v->dark, v->doc->slots[page].version, tex, ++slide_clock};
    return tex ? free_slot : NULL;
}

void present_start(Window *w, View *v)
{
    if (!w->slides)
        w->slides = SDL_calloc(SLIDES, sizeof(Slide));
    w->present_return = w->mode;
    w->present_page = view_top_page(v);
    w->present_black = false;
    w->present_was_full = SDL_GetWindowFlags(w->win) & SDL_WINDOW_FULLSCREEN;
    SDL_SetWindowFullscreen(w->win, true);
    SDL_HideCursor();
    w->mode = MODE_PRESENT;
    w->redraw = true;
}

void present_reset(Window *w)
{
    for (int i = 0; w->slides && i < SLIDES; i++)
        if (w->slides[i].tex) {
            SDL_DestroyTexture(w->slides[i].tex);
            w->slides[i] = (Slide){0};
        }
}

void present_free(Window *w)
{
    if (!w->slides)
        return;
    for (int i = 0; i < SLIDES; i++)
        if (w->slides[i].tex)
            SDL_DestroyTexture(w->slides[i].tex);
    SDL_free(w->slides);
    w->slides = NULL;
    memory_freed = true;
}

/* Back to reading, at the slide the talk stopped on. */
void present_stop(Window *w)
{
    View *v = cur_view(w);
    if (!w->present_was_full)
        SDL_SetWindowFullscreen(w->win, false);
    SDL_ShowCursor();
    w->mode = w->present_return == MODE_PRESENT ? MODE_NORMAL : w->present_return;
    present_free(w);
    if (doc_page_ok(v->doc, w->present_page) && w->present_page != view_top_page(v))
        jump_to(w, v, (Pos){w->present_page, 0});
    w->redraw = true;
}

void present_go(Window *w, int page)
{
    View *v = cur_view(w);
    w->present_page = SDL_clamp(page, 0, v->doc->npages - 1);
    w->present_black = false;
    w->redraw = true;
}

void present_step(Window *w, int delta) { present_go(w, w->present_page + delta); }

void present_blank(Window *w)
{
    w->present_black = !w->present_black;
    w->redraw = true;
}

void present_draw(Window *w)
{
    SDL_SetRenderDrawColor(w->ren, 0, 0, 0, 255);
    SDL_RenderClear(w->ren);
    View *v = cur_view(w);
    if (w->present_black || !doc_page_ok(v->doc, w->present_page))
        return;
    Slide *s = slide(w, v, w->present_page, true);
    if (!s)
        return;
    SDL_FRect dst = {(float)((w->W - s->w) / 2), (float)((w->H - s->h) / 2), (float)s->w, (float)s->h};
    SDL_RenderTexture(w->ren, s->tex, NULL, &dst);
}

/* While the speaker talks: the next slide, then the previous. */
bool present_work(void)
{
    for (int k = 0; k < nwindows; k++) {
        Window *w = windows[k];
        if (w->mode != MODE_PRESENT || !w->slides)
            continue;
        View *v = cur_view(w);
        int around[2] = {w->present_page + 1, w->present_page - 1};
        for (int i = 0; i < 2; i++)
            if (doc_page_ok(v->doc, around[i]) && !slide(w, v, around[i], false)) {
                slide(w, v, around[i], true);
                return true;
            }
    }
    return false;
}
