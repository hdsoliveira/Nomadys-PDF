/* The page grid (\g or :pages): every page as a thumbnail, hjkl to choose,
 * Enter to go there. Space chooses pages one by one, v a run of them; the
 * page changes act on those chosen. Thumbnails are drawn while idle, a few
 * at a time, so the grid shows at once even for heavy papers; they are
 * freed when the grid closes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

#define THUMBS 128           /* textures kept while the grid is open */

struct Thumb {
    const Doc *doc;
    int page, w, h;
    unsigned version;
    bool dark;
    SDL_Texture *tex;
    uint64_t used;
};

typedef struct {
    float x, y, cw, ch, pad; /* first cell, cell size */
    int cols, rows_visible;
    float label;             /* height of the page number line */
} Layout;

static uint64_t thumb_clock;

static int thumb_width(Window *w) { return (int)(w->grid_size * w->density); }

static Layout layout(Window *w, View *v)
{
    Layout L;
    L.pad = 14 * w->density;
    L.label = (float)font_height(w->font) + 4 * w->density;
    L.cw = (float)thumb_width(w);
    float ratio = v->doc->npages ? v->doc->ph[0] / v->doc->pw[0] : 1.414f; /* the first page decides the cell */
    ratio = SDL_clamp(ratio, 0.3f, 3.0f);
    L.ch = L.cw * ratio;
    int top = top_of(w);
    L.cols = SDL_max(1, (int)((w->W - L.pad) / (L.cw + L.pad)));
    float used = L.cols * (L.cw + L.pad) - L.pad;
    L.x = (w->W - used) / 2;
    L.y = top + L.pad;
    L.rows_visible = SDL_max(1, (int)((w->H - top - L.pad - ui_bar_height(w)) / (L.ch + L.label + L.pad)));
    return L;
}

static Thumb *find(Window *w, View *v, int page)
{
    for (int i = 0; i < THUMBS; i++) {
        Thumb *t = &w->thumbs[i];
        if (t->tex && t->doc == v->doc && t->page == page && t->version == v->doc->slots[page].version &&
            t->dark == v->dark && t->w == thumb_width(w)) {
            t->used = ++thumb_clock;
            return t;
        }
    }
    return NULL;
}

/* The colours changed: thumbnails are drawn again, the grid stays open. */
void grid_reset(Window *w)
{
    for (int i = 0; w->thumbs && i < THUMBS; i++)
        if (w->thumbs[i].tex) {
            SDL_DestroyTexture(w->thumbs[i].tex);
            w->thumbs[i] = (Thumb){0};
        }
}

void grid_free(Window *w)
{
    free(w->grid_marked);
    free(w->grid_extract);
    w->grid_marked = w->grid_extract = NULL;
    w->grid_marked_n = w->grid_extract_n = 0;
    if (!w->thumbs)
        return;
    for (int i = 0; i < THUMBS; i++)
        if (w->thumbs[i].tex)
            SDL_DestroyTexture(w->thumbs[i].tex);
    SDL_free(w->thumbs);
    w->thumbs = NULL;
    memory_freed = true;
}

static void keep_visible(Window *w, View *v)
{
    Layout L = layout(w, v);
    int row = w->grid_sel / L.cols;
    if (row < w->grid_top)
        w->grid_top = row;
    if (row >= w->grid_top + L.rows_visible)
        w->grid_top = row - L.rows_visible + 1;
    int rows = (v->doc->npages + L.cols - 1) / L.cols; /* no empty screen below the last row */
    w->grid_top = SDL_clamp(w->grid_top, 0, SDL_max(0, rows - L.rows_visible));
}

void grid_open(Window *w, View *v)
{
    if (!w->thumbs)
        w->thumbs = SDL_calloc(THUMBS, sizeof(Thumb));
    if (!w->grid_size)
        w->grid_size = 170;
    w->grid_return = w->mode;
    w->grid_sel = view_top_page(v);
    w->grid_anchor = -1;
    w->grid_top = 0;
    w->mode = MODE_GRID;
    keep_visible(w, v);
    w->redraw = true;
}

void grid_close(Window *w)
{
    w->mode = w->grid_return == MODE_GRID ? MODE_NORMAL : w->grid_return;
    grid_free(w);
    w->redraw = true;
}

/* The pages chosen with Space, one per page; none if the page count
 * changed under them (another window, :append). */
static bool *marks(Window *w, View *v)
{
    int n = v->doc->npages;
    if (w->grid_marked && w->grid_marked_n == n)
        return w->grid_marked;
    free(w->grid_marked);
    w->grid_marked = xcalloc((size_t)SDL_max(1, n), sizeof *w->grid_marked);
    w->grid_marked_n = n;
    return w->grid_marked;
}

static bool chosen(Window *w, View *v, int page)
{
    if (w->grid_anchor >= 0 && page >= SDL_min(w->grid_anchor, w->grid_sel) && page <= SDL_max(w->grid_anchor, w->grid_sel))
        return true;
    return marks(w, v)[page];
}

/* Those chosen with Space and with v; none: the page selected. */
bool *grid_pick(Window *w, View *v)
{
    int n = v->doc->npages;
    bool *pick = pages_pick(n, w->grid_sel, w->grid_sel), any = false;
    for (int i = 0; i < n; i++)
        any |= chosen(w, v, i);
    for (int i = 0; any && i < n; i++)
        pick[i] = chosen(w, v, i);
    return pick;
}

void grid_mark(Window *w)
{
    View *v = cur_view(w);
    bool *m = marks(w, v);
    m[w->grid_sel] = !m[w->grid_sel];
    w->redraw = true;
}

void grid_marks_remap(Window *w, const Doc *d, const int *map, int old_n)
{
    View *v = cur_view(w);
    if (!w->grid_marked || !v || v->doc != d)
        return;
    bool *old = w->grid_marked;
    int from = w->grid_marked_n;
    w->grid_marked = NULL;
    bool *m = marks(w, v);
    for (int i = 0; map && from == old_n && i < old_n; i++)
        if (old[i] && map[i] >= 0 && map[i] < d->npages)
            m[map[i]] = true;
    free(old);
}

int grid_cols(Window *w) { return layout(w, cur_view(w)).cols; }

void grid_move(Window *w, int dx, int dy)
{
    View *v = cur_view(w);
    Layout L = layout(w, v);
    int n = v->doc->npages;
    w->grid_sel = SDL_clamp(w->grid_sel + dx + dy * L.cols, 0, n - 1);
    keep_visible(w, v);
    w->redraw = true;
}

void grid_edge(Window *w, bool last)
{
    View *v = cur_view(w);
    w->grid_sel = last ? v->doc->npages - 1 : 0;
    keep_visible(w, v);
    w->redraw = true;
}

void grid_screen(Window *w, int dir)
{
    View *v = cur_view(w);
    Layout L = layout(w, v);
    grid_move(w, 0, dir * SDL_max(1, L.rows_visible - 1));
}

/* + / -: bigger or smaller thumbnails. */
void grid_zoom(Window *w, int dir)
{
    w->grid_size = SDL_clamp(w->grid_size + dir * 40, 90, 450);
    keep_visible(w, cur_view(w));
    w->redraw = true;
}

void grid_go(Window *w)
{
    View *v = cur_view(w);
    int page = w->grid_sel;
    grid_close(w);
    if (doc_page_ok(v->doc, page))
        jump_to(w, v, (Pos){page, 0});
    ui_message(w, "p. %d  (Ctrl-o returns)", page + 1);
}

/* Draws the next missing thumbnail on screen; true if one was drawn. */
bool grid_work(void)
{
    for (int k = 0; k < nwindows; k++) {
        Window *w = windows[k];
        if (w->mode != MODE_GRID || !w->thumbs)
            continue;
        View *v = cur_view(w);
        Layout L = layout(w, v);
        int first = w->grid_top * L.cols, last = SDL_min(v->doc->npages, first + (L.rows_visible + 1) * L.cols);
        /* the selected page first, then in reading order */
        for (int j = -1; j < last - first; j++) {
            int page = j < 0 ? w->grid_sel : first + j;
            if (!doc_page_ok(v->doc, page) || find(w, v, page))
                continue;
            float pw = view_page_w(v, page), ph = view_page_h(v, page);
            float s = SDL_min(L.cw / pw, L.ch / ph);
            int tw = SDL_max(1, (int)(pw * s)), th = SDL_max(1, (int)(ph * s));
            SDL_Texture *tex = view_render_area(w, v, page, tw, th, v->rotation, 0, 0, tw, th);
            Thumb *slot = &w->thumbs[0];
            for (int i = 0; i < THUMBS; i++)
                if (!w->thumbs[i].tex || w->thumbs[i].used < slot->used)
                    slot = &w->thumbs[i];
            if (slot->tex)
                SDL_DestroyTexture(slot->tex);
            *slot = (Thumb){v->doc, page, thumb_width(w), th, v->doc->slots[page].version, v->dark, tex, ++thumb_clock};
            if (tex)
                SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
            w->redraw = true;
            return true;
        }
    }
    return false;
}

void grid_draw(Window *w)
{
    View *v = cur_view(w);
    const Theme *t = uitheme(w);
    Layout L = layout(w, v);
    int n = v->doc->npages, current = view_top_page(v);
    float th = SDL_max(1.0f, w->density);
    for (int row = 0; row <= L.rows_visible; row++) {
        for (int col = 0; col < L.cols; col++) {
            int page = (w->grid_top + row) * L.cols + col;
            if (page >= n)
                break;
            float cx = L.x + col * (L.cw + L.pad), cy = L.y + row * (L.ch + L.label + L.pad);
            if (cy >= w->H)
                break;
            Thumb *tb = find(w, v, page);
            float pw = view_page_w(v, page), ph = view_page_h(v, page), s = SDL_min(L.cw / pw, L.ch / ph);
            float dw = pw * s, dh = ph * s, dx = cx + (L.cw - dw) / 2, dy = cy + (L.ch - dh) / 2;
            dx = SDL_floorf(dx), dy = SDL_floorf(dy);
            if (tb && tb->tex) {
                float tw, tth;
                SDL_GetTextureSize(tb->tex, &tw, &tth);
                SDL_FRect dst = {dx, dy, tw, tth};
                SDL_RenderTexture(w->ren, tb->tex, NULL, &dst);
            } else { /* not drawn yet */
                ui_fill(w, dx, dy, dw, dh, v->dark ? t->page_bg : 0xFFFFFF, 255);
                ui_fill(w, dx, dy, dw, dh, t->panel_header, 40);
            }
            bool sel = page == w->grid_sel;
            if (chosen(w, v, page)) /* Space, v: the pages the next change acts on */
                ui_fill(w, dx, dy, dw, dh, t->caret, 60);
            if (sel) {
                ui_frame(w, dx - 5 * th, dy - 5 * th, dw + 10 * th, dh + 10 * th, 3 * th, t->caret);
            } else {
                ui_frame(w, dx - th, dy - th, dw + 2 * th, dh + 2 * th, th, page == current ? t->tab_accent : t->panel_header);
            }
            char label[24];
            snprintf(label, sizeof label, "%d", page + 1);
            float lw = (float)font_width(w->font, label);
            font_draw(w->font, cx + (L.cw - lw) / 2, cy + L.ch + 4 * w->density, label,
                      sel ? t->caret : page == current ? t->tab_accent : t->panel_fg);
        }
    }
    char info[240];
    int nchosen = 0, first = -1, last = -1;
    for (int i = 0; i < n; i++)
        if (chosen(w, v, i))
            first = first < 0 ? i : first, last = i, nchosen++;
    if (nchosen == 1)
        snprintf(info, sizeof info - 4, "p. %d chosen   x delete   r R turn   \u2190 \u2192 move   y save as PDF   Space unchoose", first + 1);
    else if (nchosen && nchosen == last - first + 1)
        snprintf(info, sizeof info - 4, "pp. %d\u2013%d chosen   x delete   r R turn   \u2190 \u2192 move   y save as PDF%s",
                 first + 1, last + 1, w->grid_anchor >= 0 ? "   v stop" : "");
    else if (nchosen)
        snprintf(info, sizeof info - 4, "%d pages chosen   x delete   r R turn   y save them as a PDF   Space choose / unchoose", nchosen);
    else
        snprintf(info, sizeof info - 4, "p. %d of %d   Enter go   Space v choose pages   x delete   r turn   \u2190 \u2192 move   y save as PDF   i insert a PDF   u undo",
                 w->grid_sel + 1, n);
    int bar = ui_bar_height(w), p = (int)(8 * w->density);
    ui_fill(w, 0, (float)(w->H - bar), (float)w->W, (float)bar, t->bar_bg, 240);
    float x0 = cfg.mode_indicator ? (float)(3 * p + font_width(w->font, "PAGES")) : (float)p; /* after the mode badge */
    font_fit(w->font, info, (int)(w->W - x0 - p));
    font_draw(w->font, x0, (float)(w->H - bar + p / 2), info, t->bar_fg);
}
