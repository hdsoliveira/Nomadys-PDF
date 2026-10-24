/* Moving, resizing, turning and mirroring an image or an area highlight
 * after it was placed ('e' on it in the free cursor, or a click on it; a
 * new image starts here directly), by keys or by the mouse.
 * Every step is applied to the PDF at once, so what you see is the real
 * result; Esc puts it back where it was. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

#define MIN_SIZE 6.0f

/* Areas and images can move; highlighted text cannot (it belongs to its words). */
static bool movable(const Note *n)
{
    return n->subtype == FPDF_ANNOT_STAMP || n->subtype == FPDF_ANNOT_SQUARE || n->subtype == FPDF_ANNOT_CIRCLE ||
           (n->subtype == FPDF_ANNOT_HIGHLIGHT && n->nquads == 1);
}

static bool view_same(const ImageView *a, const ImageView *b)
{
    return !memcmp(a->box, b->box, sizeof a->box) && a->u0 == b->u0 && a->v0 == b->v0 && a->u1 == b->u1 &&
           a->v1 == b->v1 && a->turns == b->turns && a->flip_h == b->flip_h && a->flip_v == b->flip_v &&
           a->angle == b->angle && a->rw == b->rw && a->rh == b->rh;
}

static bool cropped(const ImageView *iv) { return iv->u0 != 0 || iv->v0 != 0 || iv->u1 != 1 || iv->v1 != 1; }

/* While the mouse drags, the window must not jump around to show the object. */
static bool scroll_ok(const Window *w) { return !w->obj_drag.kind; }

static void describe(Window *w)
{
    PRect r = w->obj_rect;
    if (w->mode == MODE_ROTATE) {
        ui_message_long(w, 60000, "turn %.1f\u00b0   h l 1\u00b0   H L 15\u00b0   Ctrl-h/l 5\u00b0   u upright   f F mirror   "
                        "Enter done   Esc cancel", w->obj_view.angle);
        return;
    }
    if (w->mode == MODE_CROP) {
        ui_message_long(w, 60000, "crop %.0f \u00d7 %.0f pt   h j k l cut that side   H J K L give back   u uncrop   "
                        "Enter done   Esc cancel", r.r - r.l, r.t - r.b);
        return;
    }
    float wd = r.r - r.l, ht = r.t - r.b;
    char turn[24] = "";
    if (w->obj_view_ok && w->obj_view.angle != 0) { /* the picture, not the box around it */
        float k = wd / (w->obj_view.box[2] - w->obj_view.box[0]);
        wd = k * w->obj_view.rw, ht = k * w->obj_view.rh;
        snprintf(turn, sizeof turn, " at %.1f\u00b0", w->obj_view.angle);
    }
    if (w->obj_text) {
        ui_message_long(w, 60000, "text %.0f \u00d7 %.0f pt, letters %.1f pt   hjkl move   H L width   + - letters   i write   Enter done   Esc undo",
                        wd, ht, w->obj_tb.size);
        return;
    }
    ui_message_long(w, 60000, "%s %.0f \u00d7 %.0f pt%s   hjkl move   HL JK size   + - scale%s   Enter done   Esc undo",
                    w->obj_image ? "image" : "area", wd, ht, turn,
                    w->obj_view_ok ? "   r R turn   o rotate   f F mirror   c crop" : w->obj_image ? "   = proportions" : "");
}

/* Width / height of what the image shows: the cropped part, turned. */
static float view_ratio(const Window *w)
{
    const ImageView *iv = &w->obj_view;
    float rw = w->obj_pxw * (iv->u1 - iv->u0), rh = w->obj_pxh * (iv->v1 - iv->v0);
    float ratio = rh > 0 ? rw / rh : 1;
    return iv->turns % 2 ? 1 / ratio : ratio;
}

/* Lets go of a free text's state. */
static void text_forget(Window *w)
{
    if (w->obj_text)
        textbox_free(&w->obj_tb);
    w->obj_text = false;
    free(w->obj_tx_before);
    w->obj_tx_before = NULL;
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
    text_forget(w);
    if (n->textbox && note_text_get(d, page, annot, &w->obj_tb)) { /* moved and resized by writing it again */
        w->obj_text = true;
        w->obj_tx_before = textbox_encode(&w->obj_tb);
    }
    w->obj_image = n->subtype == FPDF_ANNOT_STAMP && !w->obj_text;
    w->obj_view_ok = w->obj_image && note_image_view(d, page, annot, &w->obj_view, &w->obj_pxw, &w->obj_pxh);
    w->obj_view_orig = w->obj_view;
    if (w->obj_view_ok)
        w->obj_ratio = view_ratio(w);
    else
        w->obj_ratio = ratio > 0 ? ratio : (n->r.r - n->r.l) / SDL_max(1.0f, n->r.t - n->r.b);
    w->obj_return = from == MODE_OBJECT || from == MODE_CROP || from == MODE_ROTATE ? w->obj_return : from;
    w->mode = MODE_OBJECT;
    if (scroll_ok(w))
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
    if (!w->obj_drag.live && !note_image_apply(v->doc, w->obj_page, w->obj_annot, iv, r)) {
        ui_message(w, "cannot change the image");
        return;
    }
    w->obj_view = *iv;
    w->obj_rect = r;
    w->obj_ratio = view_ratio(w);
    if (scroll_ok(w))
        view_show_rect(v, w->obj_page, r, w->W, w->H, top_of(w));
    w->redraw = true;
    describe(w);
}

/* A free text's box as it is now. The PDF is left alone (and the box hidden
 * from it) until the end: the letters are drawn over the page meanwhile. */
static void text_put(Window *w)
{
    View *v = cur_view(w);
    Doc *d = v->doc;
    TextBox *b = &w->obj_tb;
    float pw = d->pw[w->obj_page], ph = d->ph[w->obj_page];
    b->size = SDL_clamp(b->size, TEXT_SIZE_MIN, TEXT_SIZE_MAX);
    b->width = SDL_clamp(b->width, 3 * b->size, pw);
    b->left = SDL_clamp(b->left, 0.0f, pw - b->width);
    float h = textbox_height(b);
    b->top = SDL_clamp(b->top, SDL_min(h, ph), ph);
    if (!w->text_hidden) {
        note_set_shown(d, w->obj_page, w->obj_annot, false);
        w->text_hidden = true;
    }
    w->obj_rect = (PRect){b->left, b->top, b->left + b->width, b->top - h};
    if (scroll_ok(w))
        view_show_rect(v, w->obj_page, w->obj_rect, w->W, w->H, top_of(w));
    w->redraw = true;
    describe(w);
}

/* The end of the session: the box shows again, written as it is (or as it was). */
static void text_commit(Window *w, bool write)
{
    View *v = cur_view(w);
    if (w->text_hidden) {
        note_set_shown(v->doc, w->obj_page, w->obj_annot, true);
        w->text_hidden = false;
    }
    if (write) {
        int idx = note_text_update(v->doc, w->obj_page, w->obj_annot, &w->obj_tb);
        if (idx < 0)
            ui_message(w, "cannot change the text");
        else
            w->obj_annot = idx;
    }
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
    if (w->obj_text) { /* only where it is and how wide: the height is the text's */
        w->obj_tb.left = r.l, w->obj_tb.top = r.t, w->obj_tb.width = r.r - r.l;
        text_put(w);
        return;
    }
    r = on_page(d, w->obj_page, r);
    int annot = w->obj_drag.live ? w->obj_annot : note_move(d, w->obj_page, w->obj_annot, r);
    if (annot < 0) {
        ui_message(w, "cannot change it");
        w->mode = w->obj_return;
        return;
    }
    w->obj_annot = annot;
    w->obj_rect = r;
    if (scroll_ok(w))
        view_show_rect(v, w->obj_page, r, w->W, w->H, top_of(w));
    w->redraw = true;
    describe(w);
}

void object_move(Window *w, float dx, float dy)
{
    PRect r = w->obj_rect;
    apply(w, (PRect){r.l + dx, r.t + dy, r.r + dx, r.b + dy});
}

/* A picture as it shows: the centre and the size of what is turned (page
 * points). With a free angle the annotation's rectangle is bigger than the
 * picture (the box is fixed and fills it), so the size is kept in the view. */
typedef struct { float cx, cy, W, H; } Pose;

static bool turned(const Window *w) { return w->obj_view_ok && w->obj_view.angle != 0; }

static Pose pose_of(const Window *w)
{
    PRect r = w->obj_rect;
    Pose p = {(r.l + r.r) / 2, (r.t + r.b) / 2, r.r - r.l, r.t - r.b};
    if (turned(w)) {
        const ImageView *iv = &w->obj_view;
        float k = (r.r - r.l) / (iv->box[2] - iv->box[0]);
        p.W = k * iv->rw, p.H = k * iv->rh;
    }
    return p;
}

/* The rectangle for a pose and a view; with a free angle it sets the size
 * of the picture inside the box (rw, rh) and keeps everything on the page. */
static PRect layout(Doc *d, int page, ImageView *iv, Pose p)
{
    p.W = SDL_max(p.W, MIN_SIZE), p.H = SDL_max(p.H, MIN_SIZE);
    if (iv->angle == 0)
        return on_page(d, page, (PRect){p.cx - p.W / 2, p.cy + p.H / 2, p.cx + p.W / 2, p.cy - p.H / 2});
    float pw = d->pw[page], ph = d->ph[page], rad = iv->angle * (float)M_PI / 180;
    float ca = fabsf(cosf(rad)), sa = fabsf(sinf(rad));
    float bw = iv->box[2] - iv->box[0], bh = iv->box[3] - iv->box[1];
    float k = SDL_max((p.W * ca + p.H * sa) / bw, (p.W * sa + p.H * ca) / bh);
    float g = SDL_min(1.0f, SDL_min(pw / (k * bw), ph / (k * bh)));
    k *= g, p.W *= g, p.H *= g; /* too big for the page: smaller */
    iv->rw = p.W / k, iv->rh = p.H / k;
    float wd = k * bw, ht = k * bh;
    float x = SDL_clamp(p.cx - wd / 2, 0.0f, pw - wd), top = SDL_clamp(p.cy + ht / 2, ht, ph);
    return (PRect){x, top, x + wd, top - ht};
}

/* Applies a view and a pose (a free angle, or a size the box cannot follow). */
static void apply_pose(Window *w, ImageView iv, Pose p)
{
    View *v = cur_view(w);
    if (!doc_page_ok(v->doc, w->obj_page)) {
        w->mode = MODE_NORMAL;
        return;
    }
    PRect r = layout(v->doc, w->obj_page, &iv, p);
    apply_view(w, &iv, r);
}

/* Resizes keeping the top-left corner where it is (a turned picture: its
 * centre; the size is then along the picture's own sides). */
void object_resize(Window *w, float dw, float dh)
{
    if (turned(w)) {
        Pose p = pose_of(w);
        p.W += dw, p.H += dh;
        apply_pose(w, w->obj_view, p);
        return;
    }
    PRect r = w->obj_rect;
    apply(w, (PRect){r.l, r.t, r.r + dw, r.b - dh});
}

void object_scale(Window *w, float f)
{
    if (w->obj_text) { /* the letters */
        float s = SDL_clamp(roundf(w->obj_tb.size * f * 2) / 2, TEXT_SIZE_MIN, TEXT_SIZE_MAX);
        w->obj_tb.size = s == w->obj_tb.size ? s + (f > 1 ? 0.5f : -0.5f) : s;
        text_put(w);
        return;
    }
    if (turned(w)) {
        Pose p = pose_of(w);
        if ((p.W * f < MIN_SIZE || p.H * f < MIN_SIZE) && f < 1)
            return;
        p.W *= f, p.H *= f;
        apply_pose(w, w->obj_view, p);
        return;
    }
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
    if (turned(w)) {
        Pose p = pose_of(w);
        p.H = p.W / w->obj_ratio;
        apply_pose(w, w->obj_view, p);
        return;
    }
    PRect r = w->obj_rect;
    apply(w, (PRect){r.l, r.t, r.r, r.t - (r.r - r.l) / w->obj_ratio});
}

static void cursor_default(void);
static void drag_end(Window *w);

void object_done(Window *w, bool keep)
{
    View *v = cur_view(w);
    drag_end(w);
    cursor_default();
    PRect r = w->obj_rect, o = w->obj_orig;
    if (w->obj_text) { /* a free text: undone as the text it was, not as a rectangle */
        char *after = textbox_encode(&w->obj_tb);
        bool changed = strcmp(after, w->obj_tx_before) != 0;
        text_commit(w, keep && changed);
        if (keep && changed)
            undo_textbox(v->doc, w->obj_page, w->obj_annot, w->obj_tx_before, after);
        else if (!keep && changed) { /* Esc: as it was */
            w->obj_rect = w->obj_orig;
        }
        free(after);
        text_forget(w);
        o = r = w->obj_rect;
    }
    bool view_changed = w->obj_view_ok && !view_same(&w->obj_view, &w->obj_view_orig);
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

/* i: write in the text box again. */
void object_text_edit(Window *w)
{
    if (!w->obj_text)
        return;
    View *v = cur_view(w);
    int page = w->obj_page;
    object_done(w, true);
    textbox_edit(w, v, page, w->obj_annot, w->mode);
}

/* r / R: a quarter turn; the top-left corner stays, width and height swap. */
void object_rotate(Window *w, int dir)
{
    if (!w->obj_view_ok) {
        ui_message(w, w->obj_image ? "only images placed with Nomadys-PDF can be turned" : "only images can be turned");
        return;
    }
    ImageView iv = w->obj_view;
    if (iv.flip_h != iv.flip_v) /* turned after a mirror: the picture goes the other way round */
        dir = -dir;
    iv.turns = (iv.turns + dir + 4) % 4;
    if (turned(w)) { /* around the centre: the free angle stays */
        Pose p = pose_of(w);
        float t = p.W;
        p.W = p.H, p.H = t;
        apply_pose(w, iv, p);
        return;
    }
    PRect r = w->obj_rect;
    apply_view(w, &iv, (PRect){r.l, r.t, r.l + (r.t - r.b), r.t - (r.r - r.l)});
}

/* f / F: mirrored left-right / top-bottom, as seen on screen. */
void object_flip(Window *w, bool horizontal)
{
    if (!w->obj_view_ok) {
        ui_message(w, w->obj_image ? "only images placed with Nomadys-PDF can be mirrored" : "only images can be mirrored");
        return;
    }
    ImageView iv = w->obj_view;
    if (horizontal)
        iv.flip_h = !iv.flip_h;
    else
        iv.flip_v = !iv.flip_v;
    iv.angle = iv.angle != 0 ? -iv.angle : 0; /* a mirror turns the other way round */
    apply_view(w, &iv, w->obj_rect);
    ui_message(w, horizontal ? "mirrored left-right" : "mirrored top-bottom");
}

/* ---------- turning freely ---------- */
void object_rotate_begin(Window *w)
{
    if (!w->obj_view_ok) {
        ui_message(w, w->obj_image ? "only images placed with Nomadys-PDF can be turned" : "only images can be turned");
        return;
    }
    w->rot_view = w->obj_view;
    w->rot_rect = w->obj_rect;
    w->mode = MODE_ROTATE;
    w->redraw = true;
    describe(w);
}

/* A free angle for the image, in degrees clockwise. */
static void set_angle(Window *w, ImageView iv, Pose p, float angle)
{
    angle = fmodf(angle, 360);
    if (angle > 180)
        angle -= 360;
    else if (angle <= -180)
        angle += 360;
    angle = roundf(angle * 100) / 100;
    if (fabsf(angle) < 0.005f)
        angle = 0;
    if (cropped(&iv) && angle != 0) {
        ui_message(w, "a cropped picture cannot be turned freely: uncrop it first (c, then u)");
        return;
    }
    iv.angle = angle;
    apply_pose(w, iv, p);
}

void object_angle(Window *w, float degrees)
{
    set_angle(w, w->obj_view, pose_of(w), w->obj_view.angle + degrees);
}

void object_angle_reset(Window *w) { set_angle(w, w->obj_view, pose_of(w), 0); }

void object_rotate_done(Window *w, bool keep)
{
    drag_end(w);
    if (!keep)
        apply_view(w, &w->rot_view, w->rot_rect);
    cursor_default();
    w->mode = MODE_OBJECT;
    w->redraw = true;
    describe(w);
}

/* ---------- cropping ---------- */
void object_crop_begin(Window *w)
{
    if (!w->obj_view_ok) {
        ui_message(w, w->obj_image ? "only images placed with Nomadys-PDF can be cropped" : "only images can be cropped");
        return;
    }
    if (w->obj_view.angle != 0) {
        ui_message(w, "a picture turned freely cannot be cropped: make it upright first (o, then u)");
        return;
    }
    w->crop_view = w->obj_view;
    w->crop_rect = w->obj_rect;
    w->mode = MODE_CROP;
    w->redraw = true;
    describe(w);
}

/* Which edge of the picture is on screen side s (0 left, 1 right, 2 top,
 * 3 bottom) after the turns (s is already the side before the mirror):
 * 0 u0, 1 u1, 2 v0, 3 v1. */
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
    int e = picture_edge(iv.turns, side < 2 ? (iv.flip_h ? 1 - side : side) : (iv.flip_v ? 5 - side : side));
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
    drag_end(w);
    PRect r = w->obj_rect, o = w->obj_orig;
    if (w->obj_text) { /* what was changed meanwhile is dropped: u brings the box back as it was */
        text_commit(w, false);
        text_forget(w);
        o = r;
    }
    if (r.l != o.l || r.t != o.t || r.r != o.r || r.b != o.b) /* undo puts it back where it was, too */
        undo_edit(v->doc, w->obj_page, w->obj_annot, o, r, w->obj_view_ok ? &w->obj_view_orig : NULL,
                  w->obj_view_ok ? &w->obj_view : NULL);
    int note = note_of_annot(v->doc, w->obj_page, w->obj_annot);
    if (note >= 0)
        note_delete(v->doc, w->obj_page, note);
    cursor_default();
    w->mode = w->obj_return;
    w->msg_until = 0;
    ui_message(w, w->obj_image ? "image deleted" : "highlight deleted");
    w->redraw = true;
}

/* ---------- the mouse ---------- */

/* Page points <-> window pixels, as floats (page_to_screen rounds). */
typedef struct { float ox, oy, ax, ay, bx, by; } Map;

static bool map_of(View *v, int page, int top, Map *m)
{
    const float L = 2000;
    float x0, y0, x1, y1, x2, y2;
    page_to_screen(v, page, 0, 0, &x0, &y0, top);
    page_to_screen(v, page, L, 0, &x1, &y1, top);
    page_to_screen(v, page, 0, L, &x2, &y2, top);
    *m = (Map){x0, y0, (x1 - x0) / L, (y1 - y0) / L, (x2 - x0) / L, (y2 - y0) / L};
    return m->ax * m->by - m->bx * m->ay != 0;
}

static void to_screen(const Map *m, float x, float y, float *sx, float *sy)
{
    *sx = m->ox + m->ax * x + m->bx * y;
    *sy = m->oy + m->ay * x + m->by * y;
}

static void to_page(const Map *m, float sx, float sy, float *x, float *y)
{
    float det = m->ax * m->by - m->bx * m->ay, dx = sx - m->ox, dy = sy - m->oy;
    *x = (m->by * dx - m->bx * dy) / det;
    *y = (m->ax * dy - m->ay * dx) / det;
}

/* The picture's own axes on the page: x to its right, y to its top. */
static void axes(float angle, float *ex, float *ey, float *fx, float *fy)
{
    float rad = angle * (float)M_PI / 180;
    *ex = cosf(rad), *ey = -sinf(rad), *fx = sinf(rad), *fy = cosf(rad);
}

static void local_to_page(Pose p, float angle, float lx, float ly, float *x, float *y)
{
    float ex, ey, fx, fy;
    axes(angle, &ex, &ey, &fx, &fy);
    *x = p.cx + lx * ex + ly * fx;
    *y = p.cy + lx * ey + ly * fy;
}

static float angle_of(const Window *w) { return w->obj_view_ok ? w->obj_view.angle : 0; }

#define HANDLE_PX 9 /* how close counts as on a handle */

/* The knob above the top edge that turns the picture, in window pixels. */
static bool knob_pos(const Window *w, const Map *m, Pose p, float *kx, float *ky)
{
    float cx, cy, tx, ty, x, y;
    to_screen(m, p.cx, p.cy, &cx, &cy);
    local_to_page(p, angle_of(w), 0, p.H / 2, &x, &y);
    to_screen(m, x, y, &tx, &ty);
    float len = hypotf(tx - cx, ty - cy);
    if (len < 1)
        return false;
    *kx = tx + (tx - cx) / len * 24 * w->density;
    *ky = ty + (ty - cy) / len * 24 * w->density;
    return true;
}

/* Whether the edge handle (i: -1 or 1 for a left or right edge, 0 for a top
 * or bottom one) fits: its side must be long enough on screen. */
static bool edge_shown(const Window *w, const Map *m, Pose p, int i)
{
    return (i ? p.H : p.W) * hypotf(m->ax, m->ay) >= 3 * HANDLE_PX * w->density;
}

enum { DRAG_MOVE = 1, DRAG_SIZE, DRAG_TURN };

/* What is at a point of the window: a handle, the knob, the inside. */
static bool hit_test(Window *w, const Map *m, float x, float y, int *kind, int *hx, int *hy)
{
    Pose p = pose_of(w);
    *hx = *hy = 0;
    if (w->mode == MODE_ROTATE) {
        *kind = DRAG_TURN;
        return true;
    }
    float r = HANDLE_PX * w->density, sx, sy, kx, ky, px, py;
    if (w->mode == MODE_OBJECT && w->obj_view_ok && knob_pos(w, m, p, &kx, &ky) && hypotf(x - kx, y - ky) <= r) {
        *kind = DRAG_TURN;
        return true;
    }
    for (int pass = 0; pass < 2; pass++) /* corners before edges */
        for (int j = -1; j <= 1; j++)
            for (int i = -1; i <= 1; i++) {
                bool corner = i && j;
                if ((pass == 0) != corner || (!i && !j) || (!corner && !edge_shown(w, m, p, i)))
                    continue;
                local_to_page(p, angle_of(w), i * p.W / 2, j * p.H / 2, &px, &py);
                to_screen(m, px, py, &sx, &sy);
                if (hypotf(x - sx, y - sy) <= r) {
                    *kind = DRAG_SIZE, *hx = i, *hy = j;
                    return true;
                }
            }
    float gx, gy, ex, ey, fx, fy;
    to_page(m, x, y, &gx, &gy);
    axes(angle_of(w), &ex, &ey, &fx, &fy);
    float lx = (gx - p.cx) * ex + (gy - p.cy) * ey, ly = (gx - p.cx) * fx + (gy - p.cy) * fy;
    float slack = 3 / SDL_max(0.01f, hypotf(m->ax, m->ay)); /* a few pixels, in points */
    if (SDL_fabsf(lx) <= p.W / 2 + slack && SDL_fabsf(ly) <= p.H / 2 + slack) {
        *kind = DRAG_MOVE;
        return true;
    }
    return false;
}

static SDL_Cursor *cursors[SDL_SYSTEM_CURSOR_COUNT];
static bool cursor_changed;

static void cursor_set(SDL_SystemCursor which)
{
    if (!cursors[which])
        cursors[which] = SDL_CreateSystemCursor(which);
    if (cursors[which])
        SDL_SetCursor(cursors[which]);
    cursor_changed = true;
}

static void cursor_default(void)
{
    if (cursor_changed)
        SDL_SetCursor(SDL_GetDefaultCursor());
    cursor_changed = false;
}

/* With a cursor on the page (v, Ctrl-v) the pointer is a cross; in the free
 * cursor the red cross is the pointer, so the system one is hidden. */
void cursor_cross(Mode mode)
{
    static bool cross, hidden;
    bool want_cross = mode == MODE_CARET, want_hidden = mode == MODE_AREA;
    if (want_hidden != hidden) {
        hidden = want_hidden;
        if (hidden)
            SDL_HideCursor();
        else
            SDL_ShowCursor();
    }
    if (want_cross == cross)
        return;
    cross = want_cross;
    if (cross)
        cursor_set(SDL_SYSTEM_CURSOR_CROSSHAIR);
    else
        cursor_default();
}

/* The cursor for a handle: the arrow that lies along its direction on screen. */
static void cursor_for(Window *w, const Map *m, int kind, int hx, int hy)
{
    Pose p = pose_of(w);
    if (kind == DRAG_MOVE) {
        cursor_set(SDL_SYSTEM_CURSOR_MOVE);
        return;
    }
    if (kind == DRAG_TURN) {
        cursor_set(SDL_SYSTEM_CURSOR_CROSSHAIR);
        return;
    }
    float ax, ay, bx, by, cx, cy;
    local_to_page(p, angle_of(w), hx * p.W / 2, hy * p.H / 2, &ax, &ay);
    to_screen(m, ax, ay, &bx, &by);
    to_screen(m, p.cx, p.cy, &cx, &cy);
    float deg = fmodf(atan2f(by - cy, bx - cx) * 180 / (float)M_PI + 360, 180);
    if (deg < 22.5f || deg >= 157.5f)
        cursor_set(SDL_SYSTEM_CURSOR_EW_RESIZE);
    else if (deg < 67.5f)
        cursor_set(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
    else if (deg < 112.5f)
        cursor_set(SDL_SYSTEM_CURSOR_NS_RESIZE);
    else
        cursor_set(SDL_SYSTEM_CURSOR_NESW_RESIZE);
}

/* The pointer, in page points of the object's page. */
static bool pointer(Window *w, float x, float y, Map *m, float *gx, float *gy)
{
    View *v = cur_view(w);
    if (!doc_page_ok(v->doc, w->obj_page) || !map_of(v, w->obj_page, top_of(w), m))
        return false;
    to_page(m, x, y, gx, gy);
    return true;
}

static bool begin_drag(Window *w, float x, float y)
{
    Map m;
    float gx, gy;
    int kind, hx, hy;
    if (!pointer(w, x, y, &m, &gx, &gy) || !hit_test(w, &m, x, y, &kind, &hx, &hy))
        return false;
    if (w->obj_text && kind == DRAG_SIZE) { /* only the width: the height follows the text */
        hy = 0;
        if (!hx)
            kind = DRAG_MOVE;
    }
    Pose p = pose_of(w);
    w->obj_drag = (ObjDrag){.kind = kind, .sx = hx, .sy = hy, .px = gx, .py = gy, .cx = p.cx, .cy = p.cy, .W = p.W,
                            .H = p.H, .rect0 = w->obj_rect, .view0 = w->obj_view, .wx = x, .wy = y, .x0 = x, .y0 = y};
    cursor_for(w, &m, kind, hx, hy);
    return true;
}

/* The first real movement of a drag: the PDF is left alone from now on (a
 * page is drawn again by PDFium at every step otherwise, which is slow) and
 * the picture is drawn by us, from its pixels, where the mouse puts it. */
static void go_live(Window *w)
{
    ObjDrag *d = &w->obj_drag;
    Doc *doc = cur_view(w)->doc;
    if (w->obj_text) /* nothing is written to the PDF while a text box moves anyway */
        return;
    int note = note_of_annot(doc, w->obj_page, w->obj_annot);
    if (note < 0)
        return;
    const Note *n = &doc->slots[w->obj_page].notes[note];
    int subtype = n->subtype;
    uint32_t color = n->color;
    if (subtype == FPDF_ANNOT_STAMP) {
        uint8_t *px;
        int tw, th;
        if (!w->obj_view_ok || !note_image_pixels(doc, w->obj_page, w->obj_annot, 1024, &px, &tw, &th))
            return;
        d->tex = SDL_CreateTexture(w->ren, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC, tw, th);
        if (d->tex) {
            SDL_UpdateTexture(d->tex, NULL, px, tw * 4);
            SDL_SetTextureBlendMode(d->tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(d->tex, SDL_SCALEMODE_LINEAR);
        }
        free(px);
        if (!d->tex)
            return;
    }
    d->color = color, d->area_kind = subtype == FPDF_ANNOT_STAMP ? 0 : subtype;
    note_set_shown(doc, w->obj_page, w->obj_annot, false);
    d->live = true;
}

/* The end of a drag: what is on screen is written to the PDF, once. */
static void drag_end(Window *w)
{
    ObjDrag *d = &w->obj_drag;
    if (d->live) {
        Doc *doc = cur_view(w)->doc;
        if (doc_page_ok(doc, w->obj_page)) {
            note_set_shown(doc, w->obj_page, w->obj_annot, true);
            d->live = false;
            if (d->area_kind)
                apply(w, w->obj_rect);
            else
                apply_view(w, &w->obj_view, w->obj_rect);
        }
        d->live = false;
    }
    if (d->tex)
        SDL_DestroyTexture(d->tex);
    d->tex = NULL;
    d->kind = 0, d->pending = false;
}

/* A selectable thing under the pointer, or false. */
static bool select_at(Window *w, View *v, float x, float y)
{
    int page;
    double px, py;
    Map m;
    if (!screen_to_page(v, x, y, top_of(w), &page, &px, &py) || !map_of(v, page, top_of(w), &m))
        return false;
    int note = note_pick(v->doc, page, (float)px, (float)py, 6 / SDL_max(0.01f, hypotf(m.ax, m.ay)));
    if (note < 0)
        return false;
    object_begin(w, v, page, v->doc->slots[page].notes[note].idx, 0);
    return w->mode == MODE_OBJECT;
}

/* A press: on a picture or area it is selected, and dragging moves it, its
 * handles resize it, the knob turns it. Returns true when the click was used. */
bool object_mouse_down(Window *w, float x, float y, int clicks)
{
    View *v = cur_view(w);
    bool was_editing = w->mode == MODE_OBJECT || w->mode == MODE_ROTATE;
    if (w->mode == MODE_OBJECT || w->mode == MODE_ROTATE) {
        if (clicks >= 2 && w->obj_text && w->mode == MODE_OBJECT) { /* a double click writes in it */
            Map m;
            float gx, gy;
            int kind, hx, hy;
            if (pointer(w, x, y, &m, &gx, &gy) && hit_test(w, &m, x, y, &kind, &hx, &hy) && kind == DRAG_MOVE) {
                object_text_edit(w);
                return true;
            }
        }
        if (begin_drag(w, x, y))
            return true;
        if (w->mode == MODE_ROTATE)
            object_rotate_done(w, true);
        object_done(w, true); /* a click elsewhere ends it */
    } else if (w->mode != MODE_NORMAL && w->mode != MODE_AREA) {
        return false;
    }
    if (w->mode != MODE_NORMAL && w->mode != MODE_AREA)
        return was_editing;
    if (!select_at(w, v, x, y))
        return was_editing;
    begin_drag(w, x, y);
    return true;
}

/* The pointer moved: during a drag only remembered (applied once per frame
 * by object_mouse_flush); otherwise the cursor says what a press would do. */
bool object_mouse_motion(Window *w, float x, float y)
{
    if (w->obj_drag.kind) {
        w->obj_drag.wx = x, w->obj_drag.wy = y;
        w->obj_drag.pending = true;
        return true;
    }
    if (w->mode == MODE_OBJECT || w->mode == MODE_ROTATE) {
        Map m;
        float gx, gy;
        int kind, hx, hy;
        if (pointer(w, x, y, &m, &gx, &gy) && hit_test(w, &m, x, y, &kind, &hx, &hy))
            cursor_for(w, &m, kind, hx, hy);
        else
            cursor_default();
    } else {
        cursor_default();
    }
    return false;
}

bool object_mouse_active(const Window *w) { return w->obj_drag.kind != 0; }

/* One side of a resize: the edge opposite the handle stays, the pointer
 * (lm, in the picture's axes) is the other edge; crossing mirrors. */
typedef struct { float size, centre; bool crossed; float anchor, dir; } Side;

static Side side_of(int s, float half, float lm, bool may_cross)
{
    if (!s)
        return (Side){2 * half, 0, false, 0, 1};
    float a = -s * half, d = lm - a;
    bool crossed = d * s < 0 && may_cross;
    float dir = crossed ? -s : s, size = SDL_max(MIN_SIZE, may_cross ? SDL_fabsf(d) : d * s);
    return (Side){size, a + dir * size / 2, crossed, a, dir};
}

/* The last pointer position, applied: the one pose-and-view computation per frame. */
void object_mouse_flush(Window *w)
{
    ObjDrag *d = &w->obj_drag;
    if (!d->kind || !d->pending)
        return;
    d->pending = false;
    Map m;
    float gx, gy;
    if (!pointer(w, d->wx, d->wy, &m, &gx, &gy))
        return;
    if (!d->live) { /* a click that wobbles is no drag */
        if (hypotf(d->wx - d->x0, d->wy - d->y0) < 4 * w->density)
            return;
        go_live(w);
    }
    SDL_Keymod mods = SDL_GetModState();
    PRect r0 = d->rect0;
    if (d->kind == DRAG_MOVE) {
        float dx = gx - d->px, dy = gy - d->py;
        PRect r = {r0.l + dx, r0.t + dy, r0.r + dx, r0.b + dy};
        if (r.l != w->obj_rect.l || r.t != w->obj_rect.t)
            apply(w, r);
        return;
    }
    float angle0 = d->view0.angle;
    if (d->kind == DRAG_TURN) {
        if (!w->obj_view_ok)
            return;
        float a0 = atan2f(d->px - d->cx, d->py - d->cy), a1 = atan2f(gx - d->cx, gy - d->cy);
        float a = angle0 + (a1 - a0) * 180 / (float)M_PI;
        if (mods & SDL_KMOD_SHIFT)
            a = roundf(a / 15) * 15;
        set_angle(w, d->view0, (Pose){d->cx, d->cy, d->W, d->H}, a);
        return;
    }
    /* resizing, in the picture's own axes */
    float ex, ey, fx, fy;
    axes(w->obj_view_ok ? angle0 : 0, &ex, &ey, &fx, &fy);
    float lx = (gx - d->cx) * ex + (gy - d->cy) * ey, ly = (gx - d->cx) * fx + (gy - d->cy) * fy;
    bool may_cross = w->obj_view_ok && angle0 == 0;
    Side sx = side_of(d->sx, d->W / 2, lx, may_cross), sy = side_of(d->sy, d->H / 2, ly, may_cross);
    if (d->sx && d->sy && w->obj_image && !(mods & SDL_KMOD_SHIFT)) { /* a corner keeps the proportions */
        float f = SDL_max(sx.size / d->W, sy.size / d->H);
        sx.size = d->W * f, sy.size = d->H * f;
        sx.centre = sx.anchor + sx.dir * sx.size / 2;
        sy.centre = sy.anchor + sy.dir * sy.size / 2;
    }
    ImageView iv = d->view0;
    if (sx.crossed)
        iv.flip_h = !iv.flip_h;
    if (sy.crossed)
        iv.flip_v = !iv.flip_v;
    Pose p;
    local_to_page((Pose){d->cx, d->cy, 0, 0}, w->obj_view_ok ? angle0 : 0, sx.centre, sy.centre, &p.cx, &p.cy);
    p.W = sx.size, p.H = sy.size;
    View *v = cur_view(w);
    if (w->obj_view_ok && (iv.angle != 0 || !view_same(&iv, &w->obj_view))) {
        apply_pose(w, iv, p);
    } else {
        PRect r = layout(v->doc, w->obj_page, &iv, p);
        if (memcmp(&r, &w->obj_rect, sizeof r))
            apply(w, r);
    }
}

bool object_mouse_up(Window *w, float x, float y)
{
    if (!w->obj_drag.kind)
        return false;
    object_mouse_motion(w, x, y);
    object_mouse_flush(w);
    drag_end(w);
    Map m;
    float gx, gy;
    int kind, hx, hy;
    if (pointer(w, x, y, &m, &gx, &gy) && hit_test(w, &m, x, y, &kind, &hx, &hy))
        cursor_for(w, &m, kind, hx, hy);
    else
        cursor_default();
    describe(w);
    w->redraw = true;
    return true;
}

/* While the mouse drags, the picture (or area) as the PDF does not show it yet. */
static void draw_live(Window *w, const Map *m, Pose p, float angle)
{
    const ObjDrag *d = &w->obj_drag;
    float q[4][2];
    if (d->tex) { /* the corners of the picture's cropped part, where the view puts them */
        const ImageView *iv = &w->obj_view;
        float f[6];
        note_view_unit(iv, f);
        SDL_Vertex vt[4];
        for (int k = 0; k < 4; k++) {
            float cu = k & 1 ? iv->u1 : iv->u0, cv = k & 2 ? iv->v1 : iv->v0;
            float u = f[0] * cu + f[1] * cv + f[2], v = f[3] * cu + f[4] * cv + f[5], x, y;
            local_to_page(p, angle, (u - 0.5f) * p.W, (v - 0.5f) * p.H, &x, &y);
            to_screen(m, x, y, &vt[k].position.x, &vt[k].position.y);
            vt[k].color = (SDL_FColor){1, 1, 1, 1};
            vt[k].tex_coord = (SDL_FPoint){cu, 1 - cv};
        }
        static const int idx[6] = {0, 1, 2, 1, 3, 2};
        SDL_RenderGeometry(w->ren, d->tex, vt, 4, idx, 6);
        return;
    }
    for (int k = 0; k < 4; k++) {
        float x, y;
        local_to_page(p, angle, (k & 1 ? 1 : -1) * p.W / 2, (k & 2 ? -1 : 1) * p.H / 2, &x, &y);
        to_screen(m, x, y, &q[k][0], &q[k][1]);
    }
    if (d->area_kind == FPDF_ANNOT_HIGHLIGHT) {
        SDL_FColor c = {((d->color >> 16) & 0xFF) / 255.0f, ((d->color >> 8) & 0xFF) / 255.0f, (d->color & 0xFF) / 255.0f, 0.4f};
        SDL_Vertex vt[4];
        for (int k = 0; k < 4; k++)
            vt[k] = (SDL_Vertex){{q[k][0], q[k][1]}, c, {0, 0}};
        static const int idx[6] = {0, 1, 2, 1, 3, 2};
        SDL_SetRenderDrawBlendMode(w->ren, SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(w->ren, NULL, vt, 4, idx, 6);
    } else { /* a frame or a circle: its outline */
        static const int next[4] = {1, 3, 0, 2};
        for (int k = 0; k < 4; k++)
            ui_line(w, q[k][0], q[k][1], q[next[k]][0], q[next[k]][1], 2 * SDL_max(1.0f, w->density), d->color);
    }
}

/* A frame around the object, turned with it, with handles at the corners
 * and the middle of the sides, and a knob above that turns it. */
void object_draw(Window *w, View *v, int top)
{
    if ((w->mode != MODE_OBJECT && w->mode != MODE_CROP && w->mode != MODE_ROTATE) || !doc_page_ok(v->doc, w->obj_page))
        return;
    Map m;
    if (!map_of(v, w->obj_page, top, &m))
        return;
    const Theme *t = wtheme(w);
    Pose p = pose_of(w);
    float angle = angle_of(w), cs[4][2];
    if (w->obj_drag.live)
        draw_live(w, &m, p, angle);
    if (w->obj_text && w->text_hidden)
        textbox_paint(w, v, top, w->obj_page, &w->obj_tb);
    for (int k = 0; k < 4; k++) {
        float x, y;
        local_to_page(p, angle, (k == 0 || k == 3 ? -1 : 1) * p.W / 2, (k < 2 ? 1 : -1) * p.H / 2, &x, &y);
        to_screen(&m, x, y, &cs[k][0], &cs[k][1]);
    }
    float th = SDL_max(1.0f, w->density), hs = 7 * w->density;
    uint32_t halo = v->dark ? 0xFFFFFF : 0x1A1A1A;
    uint32_t c = w->mode == MODE_CROP || w->mode == MODE_ROTATE ? t->search_match : t->caret;
    for (int k = 0; k < 4; k++)
        ui_line(w, cs[k][0], cs[k][1], cs[(k + 1) % 4][0], cs[(k + 1) % 4][1], 4 * th, halo);
    for (int k = 0; k < 4; k++)
        ui_line(w, cs[k][0], cs[k][1], cs[(k + 1) % 4][0], cs[(k + 1) % 4][1], 2 * th, c);
    float kx, ky;
    if (w->mode == MODE_OBJECT && w->obj_view_ok && knob_pos(w, &m, p, &kx, &ky)) {
        float tx = (cs[0][0] + cs[1][0]) / 2, ty = (cs[0][1] + cs[1][1]) / 2;
        ui_line(w, tx, ty, kx, ky, 3 * th, halo);
        ui_line(w, tx, ty, kx, ky, th, c);
        float kr = 5 * w->density;
        ui_round(w, kx - kr - th, ky - kr - th, 2 * (kr + th), 2 * (kr + th), kr + th, halo, 255);
        ui_round(w, kx - kr, ky - kr, 2 * kr, 2 * kr, kr, c, 255);
    }
    if (w->mode == MODE_ROTATE) { /* the centre it turns around */
        float cx, cy;
        to_screen(&m, p.cx, p.cy, &cx, &cy);
        ui_line(w, cx - 8 * th, cy, cx + 8 * th, cy, 3 * th, halo);
        ui_line(w, cx, cy - 8 * th, cx, cy + 8 * th, 3 * th, halo);
        ui_line(w, cx - 8 * th, cy, cx + 8 * th, cy, th, c);
        ui_line(w, cx, cy - 8 * th, cx, cy + 8 * th, th, c);
        return;
    }
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            if ((!i && !j) || (!(i && j) && !edge_shown(w, &m, p, i)))
                continue;
            float x, y, sx, sy, s = i && j ? hs : hs * 0.75f;
            local_to_page(p, angle, i * p.W / 2, j * p.H / 2, &x, &y);
            to_screen(&m, x, y, &sx, &sy);
            ui_fill(w, sx - s / 2 - th, sy - s / 2 - th, s + 2 * th, s + 2 * th, halo, 255);
            ui_fill(w, sx - s / 2, sy - s / 2, s, s, c, 255);
        }
}
