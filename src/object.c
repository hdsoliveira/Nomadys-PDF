/* Moving and resizing an image or an area highlight after it was placed
 * ('e' on it in the free cursor; a new image starts here directly).
 * Every step is applied to the PDF at once, so what you see is the real
 * result; Esc puts it back where it was. */
#include <stdio.h>
#include <string.h>

#include "nomadys.h"

#define MIN_SIZE 6.0f

/* Areas and images can move; highlighted text cannot (it belongs to its words). */
static bool movable(const Note *n)
{
    return n->subtype == FPDF_ANNOT_STAMP || n->subtype == FPDF_ANNOT_SQUARE || n->subtype == FPDF_ANNOT_CIRCLE ||
           (n->subtype == FPDF_ANNOT_HIGHLIGHT && n->nquads == 1);
}

static void describe(Window *w)
{
    PRect r = w->obj_rect;
    if (w->mode == MODE_CROP) {
        ui_message_long(w, 60000, "crop %.0f \u00d7 %.0f pt   h j k l cut that side   H J K L give back   u uncrop   "
                        "Enter done   Esc cancel", r.r - r.l, r.t - r.b);
        return;
    }
    ui_message_long(w, 60000, "%s %.0f \u00d7 %.0f pt   hjkl move   H L width   J K height   + - size%s   Enter done   Esc undo",
                    w->obj_image ? "image" : "area", r.r - r.l, r.t - r.b,
                    w->obj_view_ok ? "   = proportions   r R turn   c crop" : w->obj_image ? "   = proportions" : "");
}

/* Width / height of what the image shows: the cropped part, turned. */
static float view_ratio(const Window *w)
{
    const ImageView *iv = &w->obj_view;
    float rw = w->obj_pxw * (iv->u1 - iv->u0), rh = w->obj_pxh * (iv->v1 - iv->v0);
    float ratio = rh > 0 ? rw / rh : 1;
    return iv->turns % 2 ? 1 / ratio : ratio;
}

void object_begin(Window *w, View *v, int page, int annot, float ratio)
{
    Doc *d = v->doc;
    int note = note_of_annot(d, page, annot);
    if (note < 0)
        return;
    const Note *n = &d->slots[page].notes[note];
    if (!movable(n)) {
        ui_message(w, "highlighted text stays with its words: only areas and images move");
        return;
    }
    Mode from = w->mode;
    w->obj_page = page;
    w->obj_annot = annot;
    w->obj_rect = w->obj_orig = n->r;
    w->obj_image = n->subtype == FPDF_ANNOT_STAMP;
    w->obj_view_ok = w->obj_image && note_image_view(d, page, annot, &w->obj_view, &w->obj_pxw, &w->obj_pxh);
    w->obj_view_orig = w->obj_view;
    if (w->obj_view_ok)
        w->obj_ratio = view_ratio(w);
    else
        w->obj_ratio = ratio > 0 ? ratio : (n->r.r - n->r.l) / SDL_max(1.0f, n->r.t - n->r.b);
    w->obj_return = from == MODE_OBJECT || from == MODE_CROP ? w->obj_return : from;
    w->mode = MODE_OBJECT;
    view_show_rect(v, page, n->r, w->W, w->H, top_of(w)); /* it may be off screen */
    w->redraw = true;
    describe(w);
}

/* 'e' in the free cursor: the area or image under the cross. */
void object_begin_at_cursor(Window *w, View *v)
{
    int note = note_at(v->doc, v->area_page, v->area_x, v->area_y);
    if (note < 0) {
        ui_message(w, "put the cross on an image or a highlighted area first");
        return;
    }
    object_begin(w, v, v->area_page, v->doc->slots[v->area_page].notes[note].idx, 0);
}

/* A rectangle kept on the page and not too small. */
static PRect on_page(Doc *d, int page, PRect r)
{
    float pw = d->pw[page], ph = d->ph[page];
    float wd = SDL_clamp(r.r - r.l, MIN_SIZE, pw), ht = SDL_clamp(r.t - r.b, MIN_SIZE, ph);
    float x = SDL_clamp(r.l, 0.0f, pw - wd), top = SDL_clamp(r.t, ht, ph);
    return (PRect){x, top, x + wd, top - ht};
}

/* A new view (crop, turn) and rectangle for the image. */
static void apply_view(Window *w, const ImageView *iv, PRect r)
{
    View *v = cur_view(w);
    if (!doc_page_ok(v->doc, w->obj_page)) {
        w->mode = MODE_NORMAL;
        return;
    }
    r = on_page(v->doc, w->obj_page, r);
    if (!note_image_apply(v->doc, w->obj_page, w->obj_annot, iv, r)) {
        ui_message(w, "cannot change the image");
        return;
    }
    w->obj_view = *iv;
    w->obj_rect = r;
    w->obj_ratio = view_ratio(w);
    view_show_rect(v, w->obj_page, r, w->W, w->H, top_of(w));
    w->redraw = true;
    describe(w);
}

/* Applies a new rectangle (the view of an image stays). */
static void apply(Window *w, PRect r)
{
    View *v = cur_view(w);
    Doc *d = v->doc;
    if (!doc_page_ok(d, w->obj_page)) {
        w->mode = MODE_NORMAL;
        return;
    }
    r = on_page(d, w->obj_page, r);
    int annot = note_move(d, w->obj_page, w->obj_annot, r);
    if (annot < 0) {
        ui_message(w, "cannot change it");
        w->mode = w->obj_return;
        return;
    }
    w->obj_annot = annot;
    w->obj_rect = r;
    view_show_rect(v, w->obj_page, r, w->W, w->H, top_of(w));
    w->redraw = true;
    describe(w);
}

void object_move(Window *w, float dx, float dy)
{
    PRect r = w->obj_rect;
    apply(w, (PRect){r.l + dx, r.t + dy, r.r + dx, r.b + dy});
}

/* Resizes keeping the top-left corner where it is. */
void object_resize(Window *w, float dw, float dh)
{
    PRect r = w->obj_rect;
    apply(w, (PRect){r.l, r.t, r.r + dw, r.b - dh});
}

void object_scale(Window *w, float f)
{
    PRect r = w->obj_rect;
    float wd = r.r - r.l, ht = r.t - r.b;
    if ((wd * f < MIN_SIZE || ht * f < MIN_SIZE) && f < 1)
        return;
    apply(w, (PRect){r.l, r.t, r.l + wd * f, r.t - ht * f});
}

/* '=': back to the picture's own proportions (the width stays). */
void object_proportions(Window *w)
{
    if (!w->obj_image || w->obj_ratio <= 0)
        return;
    PRect r = w->obj_rect;
    apply(w, (PRect){r.l, r.t, r.r, r.t - (r.r - r.l) / w->obj_ratio});
}

void object_done(Window *w, bool keep)
{
    View *v = cur_view(w);
    PRect r = w->obj_rect, o = w->obj_orig;
    bool view_changed = w->obj_view_ok && memcmp(&w->obj_view, &w->obj_view_orig, sizeof w->obj_view);
    bool rect_changed = r.l != o.l || r.t != o.t || r.r != o.r || r.b != o.b;
    if (keep && (view_changed || rect_changed)) /* one undo step for the whole edit */
        undo_edit(v->doc, w->obj_page, w->obj_annot, o, r, view_changed ? &w->obj_view_orig : NULL,
                  view_changed ? &w->obj_view : NULL);
    if (!keep && view_changed)
        apply_view(w, &w->obj_view_orig, o);
    else if (!keep && (r.l != o.l || r.t != o.t || r.r != o.r || r.b != o.b))
        apply(w, o);
    w->mode = w->obj_return;
    if (w->mode == MODE_AREA) { /* the cross where the object now is */
        v->area_page = w->obj_page;
        v->area_x = (w->obj_rect.l + w->obj_rect.r) / 2;
        v->area_y = (w->obj_rect.t + w->obj_rect.b) / 2;
        v->area_selecting = false;
    }
    w->msg_until = 0;
    ui_message(w, keep ? "done" : "put back where it was");
    w->redraw = true;
}

/* r / R: a quarter turn; the top-left corner stays, width and height swap. */
void object_rotate(Window *w, int dir)
{
    if (!w->obj_view_ok) {
        ui_message(w, w->obj_image ? "only images placed with Nomadys-PDF can be turned" : "only images can be turned");
        return;
    }
    ImageView iv = w->obj_view;
    iv.turns = (iv.turns + dir + 4) % 4;
    PRect r = w->obj_rect;
    apply_view(w, &iv, (PRect){r.l, r.t, r.l + (r.t - r.b), r.t - (r.r - r.l)});
}

/* ---------- cropping ---------- */
void object_crop_begin(Window *w)
{
    if (!w->obj_view_ok) {
        ui_message(w, w->obj_image ? "only images placed with Nomadys-PDF can be cropped" : "only images can be cropped");
        return;
    }
    w->crop_view = w->obj_view;
    w->crop_rect = w->obj_rect;
    w->mode = MODE_CROP;
    w->redraw = true;
    describe(w);
}

/* Which edge of the picture is on screen side s (0 left, 1 right, 2 top,
 * 3 bottom) after the turns: 0 u0, 1 u1, 2 v0, 3 v1. */
static int picture_edge(int turns, int s)
{
    static const int edge[4][4] = {
        {0, 1, 3, 2}, /* upright: left u0, right u1, top v1, bottom v0 */
        {2, 3, 0, 1}, /* a quarter turn clockwise */
        {1, 0, 2, 3},
        {3, 2, 1, 0},
    };
    return edge[turns][s];
}

/* Cuts (amount > 0) or gives back (amount < 0) a side as seen on screen,
 * by a fraction of the whole picture; the rest keeps its size and place. */
void object_crop(Window *w, int side, float amount)
{
    ImageView iv = w->obj_view;
    float *edges[4] = {&iv.u0, &iv.u1, &iv.v0, &iv.v1};
    int e = picture_edge(iv.turns, side);
    float *x = edges[e], before = *x, extent = e < 2 ? iv.u1 - iv.u0 : iv.v1 - iv.v0, min = 0.02f;
    if (e == 0)
        *x = SDL_clamp(*x + amount, 0.0f, iv.u1 - min);
    else if (e == 1)
        *x = SDL_clamp(*x - amount, iv.u0 + min, 1.0f);
    else if (e == 2)
        *x = SDL_clamp(*x + amount, 0.0f, iv.v1 - min);
    else
        *x = SDL_clamp(*x - amount, iv.v0 + min, 1.0f);
    float cut = SDL_fabsf(*x - before) * (amount > 0 ? 1 : -1); /* of the picture, positive when cutting */
    if (cut == 0)
        return;
    PRect r = w->obj_rect;
    float size = side < 2 ? r.r - r.l : r.t - r.b, delta = size * cut / extent; /* points on screen */
    if (side == 0)
        r.l += delta;
    else if (side == 1)
        r.r -= delta;
    else if (side == 2)
        r.t -= delta;
    else
        r.b += delta;
    apply_view(w, &iv, r);
}

/* u: the whole picture again. */
void object_crop_reset(Window *w)
{
    for (int side = 0; side < 4; side++)
        object_crop(w, side, -1);
}

void object_crop_done(Window *w, bool keep)
{
    if (!keep)
        apply_view(w, &w->crop_view, w->crop_rect);
    w->mode = MODE_OBJECT;
    w->redraw = true;
    describe(w);
}

void object_delete(Window *w)
{
    View *v = cur_view(w);
    PRect r = w->obj_rect, o = w->obj_orig;
    if (r.l != o.l || r.t != o.t || r.r != o.r || r.b != o.b) /* undo puts it back where it was, too */
        undo_edit(v->doc, w->obj_page, w->obj_annot, o, r, w->obj_view_ok ? &w->obj_view_orig : NULL,
                  w->obj_view_ok ? &w->obj_view : NULL);
    int note = note_of_annot(v->doc, w->obj_page, w->obj_annot);
    if (note >= 0)
        note_delete(v->doc, w->obj_page, note);
    w->mode = w->obj_return;
    w->msg_until = 0;
    ui_message(w, w->obj_image ? "image deleted" : "highlight deleted");
    w->redraw = true;
}

/* A frame with corner handles around the object. */
void object_draw(Window *w, View *v, int top)
{
    if ((w->mode != MODE_OBJECT && w->mode != MODE_CROP) || !doc_page_ok(v->doc, w->obj_page))
        return;
    const Theme *t = wtheme(w);
    float x0, y0, x1, y1;
    page_to_screen(v, w->obj_page, w->obj_rect.l, w->obj_rect.t, &x0, &y0, top);
    page_to_screen(v, w->obj_page, w->obj_rect.r, w->obj_rect.b, &x1, &y1, top);
    float x = SDL_min(x0, x1), y = SDL_min(y0, y1), wd = SDL_fabsf(x1 - x0), ht = SDL_fabsf(y1 - y0);
    float th = SDL_max(1.0f, w->density), hs = 7 * w->density;
    uint32_t halo = v->dark ? 0xFFFFFF : 0x1A1A1A;
    ui_frame(w, x - 2 * th, y - 2 * th, wd + 4 * th, ht + 4 * th, 3 * th, halo);
    uint32_t c = w->mode == MODE_CROP ? t->search_match : t->caret; /* cropping: another colour */
    ui_frame(w, x - th, y - th, wd + 2 * th, ht + 2 * th, th, c);
    float cx[2] = {x, x + wd}, cy[2] = {y, y + ht};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            ui_fill(w, cx[i] - hs / 2 - th, cy[j] - hs / 2 - th, hs + 2 * th, hs + 2 * th, halo, 255);
            ui_fill(w, cx[i] - hs / 2, cy[j] - hs / 2, hs, hs, c, 255);
        }
}
