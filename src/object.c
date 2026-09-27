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
    ui_message_long(w, 60000, "%s %.0f × %.0f pt   hjkl move   H L width   J K height   + - size%s   Enter done   Esc undo",
                    w->obj_image ? "image" : "area", r.r - r.l, r.t - r.b, w->obj_image ? "   = proportions" : "");
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
    w->obj_return = w->mode == MODE_OBJECT ? w->obj_return : w->mode;
    w->obj_page = page;
    w->obj_annot = annot;
    w->obj_rect = w->obj_orig = n->r;
    w->obj_image = n->subtype == FPDF_ANNOT_STAMP;
    w->obj_ratio = ratio > 0 ? ratio : (n->r.r - n->r.l) / SDL_max(1.0f, n->r.t - n->r.b);
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
    const Note *n = &v->doc->slots[v->area_page].notes[note];
    float ratio = 0;
    if (n->subtype == FPDF_ANNOT_STAMP) { /* the picture's own proportions, for '=' */
        FPDF_PAGE p = doc_page(v->doc, v->area_page);
        FPDF_ANNOTATION a = p ? FPDFPage_GetAnnot(p, n->idx) : NULL;
        for (int i = 0; a && i < FPDFAnnot_GetObjectCount(a); i++) {
            FPDF_PAGEOBJECT o = FPDFAnnot_GetObject(a, i);
            unsigned int iw, ih;
            if (o && FPDFPageObj_GetType(o) == FPDF_PAGEOBJ_IMAGE && FPDFImageObj_GetImagePixelSize(o, &iw, &ih) &&
                ih > 0) {
                ratio = (float)iw / (float)ih;
                break;
            }
        }
        if (a)
            FPDFPage_CloseAnnot(a);
    }
    object_begin(w, v, v->area_page, n->idx, ratio);
}

/* Applies a new rectangle, kept on the page and not too small. */
static void apply(Window *w, PRect r)
{
    View *v = cur_view(w);
    Doc *d = v->doc;
    if (!doc_page_ok(d, w->obj_page)) {
        w->mode = MODE_NORMAL;
        return;
    }
    float pw = d->pw[w->obj_page], ph = d->ph[w->obj_page];
    float wd = SDL_clamp(r.r - r.l, MIN_SIZE, pw), ht = SDL_clamp(r.t - r.b, MIN_SIZE, ph);
    float x = SDL_clamp(r.l, 0.0f, pw - wd), top = SDL_clamp(r.t, ht, ph);
    r = (PRect){x, top, x + wd, top - ht};
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
    if (!keep && (r.l != o.l || r.t != o.t || r.r != o.r || r.b != o.b))
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

void object_delete(Window *w)
{
    View *v = cur_view(w);
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
    if (w->mode != MODE_OBJECT || !doc_page_ok(v->doc, w->obj_page))
        return;
    const Theme *t = wtheme(w);
    float x0, y0, x1, y1;
    page_to_screen(v, w->obj_page, w->obj_rect.l, w->obj_rect.t, &x0, &y0, top);
    page_to_screen(v, w->obj_page, w->obj_rect.r, w->obj_rect.b, &x1, &y1, top);
    float x = SDL_min(x0, x1), y = SDL_min(y0, y1), wd = SDL_fabsf(x1 - x0), ht = SDL_fabsf(y1 - y0);
    float th = SDL_max(1.0f, w->density), hs = 7 * w->density;
    uint32_t halo = v->dark ? 0xFFFFFF : 0x1A1A1A;
    ui_frame(w, x - 2 * th, y - 2 * th, wd + 4 * th, ht + 4 * th, 3 * th, halo);
    ui_frame(w, x - th, y - th, wd + 2 * th, ht + 2 * th, th, t->caret);
    float cx[2] = {x, x + wd}, cy[2] = {y, y + ht};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            ui_fill(w, cx[i] - hs / 2 - th, cy[j] - hs / 2 - th, hs + 2 * th, hs + 2 * th, halo, 255);
            ui_fill(w, cx[i] - hs / 2, cy[j] - hs / 2, hs, hs, t->caret, 255);
        }
}
