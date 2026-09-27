/* Markup annotations: highlights with optional comments ("notes").
 * They are standard PDF annotations, so other readers show them too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

static bool is_markup(int subtype)
{
    switch (subtype) {
    case FPDF_ANNOT_TEXT: case FPDF_ANNOT_FREETEXT: case FPDF_ANNOT_SQUARE: case FPDF_ANNOT_CIRCLE:
    case FPDF_ANNOT_HIGHLIGHT: case FPDF_ANNOT_UNDERLINE: case FPDF_ANNOT_SQUIGGLY:
    case FPDF_ANNOT_STRIKEOUT: case FPDF_ANNOT_INK: case FPDF_ANNOT_STAMP:
        return true;
    default:
        return false;
    }
}

static PRect norm(FS_RECTF r)
{
    return (PRect){SDL_min(r.left, r.right), SDL_max(r.top, r.bottom),
                   SDL_max(r.left, r.right), SDL_min(r.top, r.bottom)};
}

static void free_notes(PageSlot *s)
{
    for (int i = 0; i < s->nnotes; i++)
        free(s->notes[i].quads);
    free(s->notes);
    s->notes = NULL;
    s->nnotes = 0;
}

/* The colour of an annotation. Once it has an appearance stream PDFium no
 * longer reports /C, so read the fill of the first drawing in it. */
static uint32_t note_color(FPDF_ANNOTATION a)
{
    unsigned int r, g, b, al;
    if (FPDFAnnot_GetColor(a, FPDFANNOT_COLORTYPE_Color, &r, &g, &b, &al))
        return r << 16 | g << 8 | b;
    for (int i = 0; i < FPDFAnnot_GetObjectCount(a); i++) {
        FPDF_PAGEOBJECT o = FPDFAnnot_GetObject(a, i);
        if (o && (FPDFPageObj_GetFillColor(o, &r, &g, &b, &al) || FPDFPageObj_GetStrokeColor(o, &r, &g, &b, &al)))
            return r << 16 | g << 8 | b;
    }
    return cfg.highlights[0];
}

void notes_page(Doc *d, int page)
{
    if (!doc_page_ok(d, page))
        return;
    PageSlot *s = &d->slots[page];
    if (s->notes_version == s->version + 1)
        return;
    FPDF_PAGE p = doc_page(d, page);
    if (!p)
        return;
    free_notes(s);
    int n = FPDFPage_GetAnnotCount(p);
    s->notes = xcalloc(n ? n : 1, sizeof *s->notes);
    for (int i = 0; i < n; i++) {
        FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, i);
        if (!a)
            continue;
        FS_RECTF r;
        int type = FPDFAnnot_GetSubtype(a);
        if (is_markup(type) && FPDFAnnot_GetRect(a, &r)) {
            Note *nt = &s->notes[s->nnotes++];
            *nt = (Note){.idx = i, .subtype = type, .r = norm(r)};
            size_t nq = FPDFAnnot_CountAttachmentPoints(a);
            nt->quads = xcalloc(nq ? nq : 1, sizeof *nt->quads);
            for (size_t q = 0; q < nq; q++) {
                FS_QUADPOINTSF qp;
                if (!FPDFAnnot_GetAttachmentPoints(a, q, &qp))
                    continue;
                nt->quads[nt->nquads++] = (PRect){
                    SDL_min(SDL_min(qp.x1, qp.x2), SDL_min(qp.x3, qp.x4)),
                    SDL_max(SDL_max(qp.y1, qp.y2), SDL_max(qp.y3, qp.y4)),
                    SDL_max(SDL_max(qp.x1, qp.x2), SDL_max(qp.x3, qp.x4)),
                    SDL_min(SDL_min(qp.y1, qp.y2), SDL_min(qp.y3, qp.y4))};
            }
            if (!nt->nquads)
                nt->quads[nt->nquads++] = nt->r;
            /* length in bytes includes the UTF-16 terminator */
            nt->has_comment = FPDFAnnot_GetStringValue(a, "Contents", NULL, 0) > 2;
            nt->color = note_color(a);
        }
        FPDFPage_CloseAnnot(a);
    }
    s->notes_version = s->version + 1;
}

int note_at(Doc *d, int page, float x, float y)
{
    if (!doc_page_ok(d, page))
        return -1;
    notes_page(d, page);
    PageSlot *s = &d->slots[page];
    for (int i = s->nnotes - 1; i >= 0; i--) /* topmost first */
        for (int q = 0; q < s->notes[i].nquads; q++) {
            PRect r = s->notes[i].quads[q];
            if (x >= r.l - 1 && x <= r.r + 1 && y <= r.t + 1 && y >= r.b - 1)
                return i;
        }
    return -1;
}

static void set_string(FPDF_ANNOTATION a, const char *key, const char *text)
{
    size_t n = strlen(text) * 2 + 2; /* worst case: every byte a UTF-16 unit */
    unsigned short *u = xmalloc(n * sizeof *u);
    utf8_to_utf16(text, u, (int)n);
    FPDFAnnot_SetStringValue(a, key, u);
    free(u);
}

/* Colour, author and comment shared by every note we create. */
static void finish_note(Doc *d, int page, FPDF_ANNOTATION a, PRect bounds, const char *comment, uint32_t c)
{
    FPDFAnnot_SetColor(a, FPDFANNOT_COLORTYPE_Color, c >> 16, (c >> 8) & 0xFF, c & 0xFF, 255);
    FPDFAnnot_SetFlags(a, FPDF_ANNOT_FLAG_PRINT);
    FS_RECTF rect = {bounds.l, bounds.t, bounds.r, bounds.b};
    FPDFAnnot_SetRect(a, &rect);
    const char *user = getenv("USER");
    if (user)
        set_string(a, "T", user); /* author, shown by other readers */
    if (comment && *comment)
        set_string(a, "Contents", comment);
    FPDFPage_CloseAnnot(a);
    doc_mark_changed(d, page);
}

bool note_create(Doc *d, int page, const PRect *quads, int n, const char *comment, uint32_t color)
{
    FPDF_PAGE p = doc_page(d, page);
    if (!p || n <= 0)
        return false;
    FPDF_ANNOTATION a = FPDFPage_CreateAnnot(p, FPDF_ANNOT_HIGHLIGHT);
    if (!a)
        return false;
    PRect u = quads[0];
    for (int i = 0; i < n; i++) {
        PRect r = quads[i];
        FS_QUADPOINTSF q = {r.l, r.t, r.r, r.t, r.l, r.b, r.r, r.b};
        FPDFAnnot_AppendAttachmentPoints(a, &q);
        u.l = SDL_min(u.l, r.l), u.r = SDL_max(u.r, r.r);
        u.t = SDL_max(u.t, r.t), u.b = SDL_min(u.b, r.b);
    }
    finish_note(d, page, a, u, comment, color);
    return true;
}

/* An area such as a figure: a highlight (like Acrobat) or, with
 * area_style = frame, a rectangle drawn around it. */
bool note_create_area(Doc *d, int page, PRect r, const char *comment, uint32_t color)
{
    if (!cfg.area_frame)
        return note_create(d, page, &r, 1, comment, color);
    FPDF_PAGE p = doc_page(d, page);
    FPDF_ANNOTATION a = p ? FPDFPage_CreateAnnot(p, FPDF_ANNOT_SQUARE) : NULL;
    if (!a)
        return false;
    FPDFAnnot_SetBorder(a, 0, 0, 2);
    finish_note(d, page, a, r, comment, color);
    return true;
}

/* An image, placed as a stamp annotation: other readers show it, and it
 * can be moved, resized or deleted like any note. Takes the image object.
 * Returns the annotation index, or -1. */
int note_create_image(Doc *d, int page, PRect r, FPDF_PAGEOBJECT image)
{
    FPDF_PAGE p = doc_page(d, page);
    FPDF_ANNOTATION a = p ? FPDFPage_CreateAnnot(p, FPDF_ANNOT_STAMP) : NULL;
    if (!a) {
        FPDFPageObj_Destroy(image);
        return -1;
    }
    FS_RECTF rect = {r.l, r.t, r.r, r.b};
    FPDFAnnot_SetRect(a, &rect);
    FPDFImageObj_SetMatrix(image, r.r - r.l, 0, 0, r.t - r.b, r.l, r.b);
    if (!FPDFAnnot_AppendObject(a, image)) {
        FPDFPageObj_Destroy(image);
        FPDFPage_CloseAnnot(a);
        FPDFPage_RemoveAnnot(p, FPDFPage_GetAnnotCount(p) - 1);
        return -1;
    }
    FPDFAnnot_SetFlags(a, FPDF_ANNOT_FLAG_PRINT);
    set_string(a, "Name", "Image");
    const char *user = getenv("USER");
    if (user)
        set_string(a, "T", user);
    FPDFPage_CloseAnnot(a);
    doc_mark_changed(d, page);
    return FPDFPage_GetAnnotCount(p) - 1;
}

/* The note (index into slots[page].notes) of an annotation, or -1. */
int note_of_annot(Doc *d, int page, int annot)
{
    if (!doc_page_ok(d, page))
        return -1;
    notes_page(d, page);
    PageSlot *s = &d->slots[page];
    for (int i = 0; i < s->nnotes; i++)
        if (s->notes[i].idx == annot)
            return i;
    return -1;
}

/* Widgets are listed by annotation index, which removing one shifts. */
static void forget_widgets(PageSlot *s)
{
    free(s->widgets);
    s->widgets = NULL;
    s->nwidgets = 0;
    s->widgets_ready = false;
}

/* Moves or resizes an area or an image to r. An image only gets the new
 * rectangle (its appearance is fitted into it); a highlight or frame is
 * made again there (same kind, colour, comment), since its appearance
 * cannot be edited. Returns the annotation's new
 * index, or -1. */
int note_move(Doc *d, int page, int annot, PRect r)
{
    FPDF_PAGE p = doc_page(d, page);
    FPDF_ANNOTATION a = p ? FPDFPage_GetAnnot(p, annot) : NULL;
    if (!a)
        return -1;
    FS_RECTF rect = {r.l, r.t, r.r, r.b};
    int sub = FPDFAnnot_GetSubtype(a);
    if (sub == FPDF_ANNOT_STAMP) {
        /* only the rectangle: readers fit the appearance (its BBox) into
         * it, so the picture follows, without re-encoding anything */
        FPDFAnnot_SetRect(a, &rect);
        FPDFPage_CloseAnnot(a);
        doc_reload_page(d, page); /* PDFium keeps the old placement otherwise */
        doc_mark_changed(d, page);
        return annot;
    }
    uint32_t color = note_color(a);
    char *comment = NULL;
    unsigned long bytes = FPDFAnnot_GetStringValue(a, "Contents", NULL, 0);
    if (bytes > 2) {
        unsigned short *u = xmalloc(bytes);
        FPDFAnnot_GetStringValue(a, "Contents", u, bytes);
        comment = utf16_to_utf8(u, (int)(bytes / 2) - 1);
        free(u);
    }
    FPDFPage_CloseAnnot(a);
    if (d->focus_page == page)
        doc_kill_focus(d);
    if (!FPDFPage_RemoveAnnot(p, annot)) {
        free(comment);
        return -1;
    }
    forget_widgets(&d->slots[page]);
    FPDF_ANNOTATION n = FPDFPage_CreateAnnot(p, sub == FPDF_ANNOT_SQUARE || sub == FPDF_ANNOT_CIRCLE ? sub
                                                                                                    : FPDF_ANNOT_HIGHLIGHT);
    if (!n) {
        free(comment);
        doc_mark_changed(d, page);
        return -1;
    }
    if (sub == FPDF_ANNOT_SQUARE || sub == FPDF_ANNOT_CIRCLE) {
        FPDFAnnot_SetBorder(n, 0, 0, 2);
    } else {
        FS_QUADPOINTSF q = {r.l, r.t, r.r, r.t, r.l, r.b, r.r, r.b};
        FPDFAnnot_AppendAttachmentPoints(n, &q);
    }
    finish_note(d, page, n, r, comment, color);
    free(comment);
    return FPDFPage_GetAnnotCount(p) - 1;
}

#define VIEW_KEY "NomadysImage" /* ImageView, kept in the annotation */

static FPDF_PAGEOBJECT stamp_image(FPDF_ANNOTATION a)
{
    for (int i = 0; i < FPDFAnnot_GetObjectCount(a); i++) {
        FPDF_PAGEOBJECT o = FPDFAnnot_GetObject(a, i);
        if (o && FPDFPageObj_GetType(o) == FPDF_PAGEOBJ_IMAGE)
            return o;
    }
    return NULL;
}

/* The view of an image placed by us: saved with it, or, for one never
 * cropped or turned, its matrix (then the picture fills the box). */
bool note_image_view(Doc *d, int page, int annot, ImageView *iv, int *pxw, int *pxh)
{
    FPDF_PAGE p = doc_page(d, page);
    FPDF_ANNOTATION a = p ? FPDFPage_GetAnnot(p, annot) : NULL;
    if (!a)
        return false;
    FPDF_PAGEOBJECT o = FPDFAnnot_GetSubtype(a) == FPDF_ANNOT_STAMP ? stamp_image(a) : NULL;
    unsigned int w = 0, h = 0;
    bool ok = o && FPDFImageObj_GetImagePixelSize(o, &w, &h) && w && h;
    char buf[256] = "";
    unsigned long n = ok ? FPDFAnnot_GetStringValue(a, VIEW_KEY, NULL, 0) : 0;
    if (n > 2 && n < sizeof buf) {
        unsigned short u[128];
        FPDFAnnot_GetStringValue(a, VIEW_KEY, u, sizeof u);
        char *s = utf16_to_utf8(u, (int)(n / 2) - 1);
        snprintf(buf, sizeof buf, "%s", s);
        free(s);
    }
    if (ok && buf[0]) {
        ok = sscanf(buf, "%f %f %f %f %f %f %f %f %d", &iv->box[0], &iv->box[1], &iv->box[2], &iv->box[3], &iv->u0,
                    &iv->v0, &iv->u1, &iv->v1, &iv->turns) == 9;
    } else if (ok) {
        FS_MATRIX m;
        ok = FPDFPageObj_GetMatrix(o, &m) && m.b == 0 && m.c == 0 && m.a > 0 && m.d > 0;
        *iv = (ImageView){{m.e, m.f, m.e + m.a, m.f + m.d}, 0, 0, 1, 1, 0};
    }
    ok = ok && iv->u1 > iv->u0 && iv->v1 > iv->v0 && iv->box[2] > iv->box[0] && iv->box[3] > iv->box[1];
    iv->turns = ((iv->turns % 4) + 4) % 4;
    *pxw = (int)w, *pxh = (int)h;
    FPDFPage_CloseAnnot(a);
    return ok;
}

/* The image matrix for a view: the part shown fills the box, turned. */
static FS_MATRIX view_matrix(const ImageView *iv)
{
    float cw = iv->u1 - iv->u0, ch = iv->v1 - iv->v0;
    /* u' = pu*u + r0, v' = qv*v + s0: the part shown becomes 0..1 */
    float pu = 1 / cw, r0 = -iv->u0 / cw, qv = 1 / ch, s0 = -iv->v0 / ch;
    /* u'' = A*u + B*v + C, v'' = D*u + E*v + F after the turn */
    float A, B, C, D, E, F;
    switch (iv->turns) {
    case 1: A = 0, B = qv, C = s0, D = -pu, E = 0, F = 1 - r0; break;       /* (v', 1 - u') */
    case 2: A = -pu, B = 0, C = 1 - r0, D = 0, E = -qv, F = 1 - s0; break;  /* (1 - u', 1 - v') */
    case 3: A = 0, B = -qv, C = 1 - s0, D = pu, E = 0, F = r0; break;       /* (1 - v', u') */
    default: A = pu, B = 0, C = r0, D = 0, E = qv, F = s0; break;
    }
    float bw = iv->box[2] - iv->box[0], bh = iv->box[3] - iv->box[1];
    return (FS_MATRIX){bw * A, bh * D, bw * B, bh * E, iv->box[0] + bw * C, iv->box[1] + bh * F};
}

bool note_image_apply(Doc *d, int page, int annot, const ImageView *iv, PRect r)
{
    FPDF_PAGE p = doc_page(d, page);
    FPDF_ANNOTATION a = p ? FPDFPage_GetAnnot(p, annot) : NULL;
    FPDF_PAGEOBJECT o = a ? stamp_image(a) : NULL;
    if (!o) {
        if (a)
            FPDFPage_CloseAnnot(a);
        return false;
    }
    FS_MATRIX m = view_matrix(iv);
    FPDFImageObj_SetMatrix(o, m.a, m.b, m.c, m.d, m.e, m.f);
    FPDFAnnot_UpdateObject(a, o);
    FS_RECTF rect = {r.l, r.t, r.r, r.b};
    FPDFAnnot_SetRect(a, &rect);
    char buf[256];
    snprintf(buf, sizeof buf, "%g %g %g %g %g %g %g %g %d", iv->box[0], iv->box[1], iv->box[2], iv->box[3], iv->u0,
             iv->v0, iv->u1, iv->v1, iv->turns);
    set_string(a, VIEW_KEY, buf);
    FPDFPage_CloseAnnot(a);
    doc_reload_page(d, page);
    doc_mark_changed(d, page);
    return true;
}

static FPDF_ANNOTATION open_note(Doc *d, int page, int note)
{
    if (!doc_page_ok(d, page))
        return NULL;
    notes_page(d, page);
    PageSlot *s = &d->slots[page];
    FPDF_PAGE p = doc_page(d, page);
    return (p && note >= 0 && note < s->nnotes) ? FPDFPage_GetAnnot(p, s->notes[note].idx) : NULL;
}

char *note_comment(Doc *d, int page, int note)
{
    FPDF_ANNOTATION a = open_note(d, page, note);
    if (!a)
        return NULL;
    unsigned long bytes = FPDFAnnot_GetStringValue(a, "Contents", NULL, 0);
    char *out = NULL;
    if (bytes > 2) {
        unsigned short *u = xmalloc(bytes);
        FPDFAnnot_GetStringValue(a, "Contents", u, bytes);
        out = utf16_to_utf8(u, (int)(bytes / 2) - 1);
        free(u);
    }
    FPDFPage_CloseAnnot(a);
    return out;
}

bool note_set_comment(Doc *d, int page, int note, const char *comment)
{
    FPDF_ANNOTATION a = open_note(d, page, note);
    if (!a)
        return false;
    set_string(a, "Contents", comment ? comment : "");
    FPDFPage_CloseAnnot(a);
    doc_mark_changed(d, page);
    return true;
}

bool note_delete(Doc *d, int page, int note)
{
    if (!doc_page_ok(d, page))
        return false;
    notes_page(d, page);
    PageSlot *s = &d->slots[page];
    FPDF_PAGE p = doc_page(d, page);
    if (!p || note < 0 || note >= s->nnotes)
        return false;
    if (d->focus_page == page)
        doc_kill_focus(d);
    if (!FPDFPage_RemoveAnnot(p, s->notes[note].idx))
        return false;
    forget_widgets(s); /* annotation indices after the removed one have shifted */
    doc_mark_changed(d, page);
    return true;
}
