/* Markup annotations: highlights with optional comments ("notes").
 * They are standard PDF annotations, so other readers show them too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

static bool get_string(FPDF_ANNOTATION a, const char *key, char *out, size_t cap);

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
        int fill;
        FPDF_BOOL stroke;
        if (o && FPDFPageObj_GetType(o) == FPDF_PAGEOBJ_PATH && FPDFPath_GetDrawMode(o, &fill, &stroke)) {
            /* a frame or a drawing has only a line: its colour, not the unused fill */
            if (fill ? FPDFPageObj_GetFillColor(o, &r, &g, &b, &al) : stroke && FPDFPageObj_GetStrokeColor(o, &r, &g, &b, &al))
                return r << 16 | g << 8 | b;
            continue;
        }
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
        bool hidden = FPDFAnnot_GetFlags(a) & FPDF_ANNOT_FLAG_HIDDEN; /* deleted (undo can bring it back) */
        if (!hidden && is_markup(type) && FPDFAnnot_GetRect(a, &r)) {
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
            /* marked by note_create_area; older ones: one quad taller than a line */
            char mark[8];
            nt->area = type == FPDF_ANNOT_HIGHLIGHT &&
                       ((get_string(a, "NomadysArea", mark, sizeof mark) && mark[0] == '1') ||
                        (nt->nquads == 1 && nt->quads[0].t - nt->quads[0].b > 40));
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

/* Widgets are listed by annotation index, which removing one shifts. */
static void forget_widgets(PageSlot *s)
{
    free(s->widgets);
    s->widgets = NULL;
    s->nwidgets = 0;
    s->widgets_ready = false;
}

/* A long text value (a comment), or "" . */
static void get_long_string(FPDF_ANNOTATION a, const char *key, char *out, size_t cap)
{
    unsigned long bytes = FPDFAnnot_GetStringValue(a, key, NULL, 0);
    out[0] = 0;
    if (bytes <= 2)
        return;
    unsigned short *u = xmalloc(bytes);
    FPDFAnnot_GetStringValue(a, key, u, bytes);
    char *t = utf16_to_utf8(u, (int)(bytes / 2) - 1);
    snprintf(out, cap, "%s", t);
    free(t);
    free(u);
}

/* ---------- identity: every annotation we touch gets an /NM ---------- */
static bool get_string(FPDF_ANNOTATION a, const char *key, char *out, size_t cap)
{
    unsigned long bytes = FPDFAnnot_GetStringValue(a, key, NULL, 0);
    out[0] = 0;
    if (bytes <= 2 || bytes > 1024)
        return false;
    unsigned short u[512];
    FPDFAnnot_GetStringValue(a, key, u, sizeof u);
    char *t = utf16_to_utf8(u, (int)(bytes / 2) - 1);
    snprintf(out, cap, "%s", t);
    free(t);
    return true;
}

/* The annotation's name (/NM), given one if it has none. */
static void annot_name(FPDF_ANNOTATION a, char *out, size_t cap)
{
    static unsigned counter;
    if (get_string(a, "NM", out, cap) && out[0])
        return;
    snprintf(out, cap, "npdf-%llx-%u", (unsigned long long)time(NULL), ++counter);
    set_string(a, "NM", out);
}

/* The index of the annotation named nm on the page, or -1. */
static int annot_by_name(Doc *d, int page, const char *nm)
{
    FPDF_PAGE p = doc_page(d, page);
    char got[64];
    for (int i = 0; p && i < FPDFPage_GetAnnotCount(p); i++) {
        FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, i);
        bool same = a && get_string(a, "NM", got, sizeof got) && !strcmp(got, nm);
        if (a)
            FPDFPage_CloseAnnot(a);
        if (same)
            return i;
    }
    return -1;
}

static bool set_hidden(Doc *d, int page, const char *nm, bool hidden)
{
    int idx = annot_by_name(d, page, nm);
    FPDF_PAGE p = doc_page(d, page);
    FPDF_ANNOTATION a = idx >= 0 && p ? FPDFPage_GetAnnot(p, idx) : NULL;
    if (!a)
        return false;
    int flags = FPDFAnnot_GetFlags(a);
    FPDFAnnot_SetFlags(a, hidden ? flags | FPDF_ANNOT_FLAG_HIDDEN : flags & ~FPDF_ANNOT_FLAG_HIDDEN);
    set_string(a, "NomadysDeleted", hidden ? "1" : "");
    FPDFPage_CloseAnnot(a);
    doc_reload_page(d, page); /* PDFium keeps the old flags of a loaded page */
    doc_mark_changed(d, page);
    return true;
}

/* ---------- undo / redo ---------- */
#define UNDO_MAX 200

static bool undoing; /* applying an undo or redo: not recorded again */

static void undo_free(Undo *u)
{
    if (u->kind == U_PAGES) { /* snapshots of the document: temporary files */
        if (u->text_before)
            unlink(u->text_before);
        if (u->text_after)
            unlink(u->text_after);
    }
    free(u->text_before);
    free(u->text_after);
    free(u->marks_before);
    free(u->marks_after);
}

static void push(Doc *d, Undo u)
{
    if (undoing) {
        undo_free(&u);
        return;
    }
    for (int i = d->undo_at; i < d->nundo; i++) /* a new change: nothing to redo */
        undo_free(&d->undo[i]);
    d->nundo = d->undo_at;
    if (d->nundo == UNDO_MAX) { /* the oldest goes */
        undo_free(&d->undo[0]);
        memmove(d->undo, d->undo + 1, (size_t)(UNDO_MAX - 1) * sizeof *d->undo);
        d->nundo--;
    }
    if (!d->undo)
        d->undo = xcalloc(UNDO_MAX, sizeof *d->undo);
    u.group = u.group ? u.group : ++d->undo_group;
    d->undo[d->nundo++] = u;
    d->undo_at = d->nundo;
}

/* Several changes undone as one (a :merge). */
int undo_group_begin(Doc *d) { return d->undo_group_open = ++d->undo_group; }
void undo_group_end(Doc *d) { d->undo_group_open = 0; }

static void record(Doc *d, UndoKind kind, int page, FPDF_ANNOTATION a)
{
    Undo u = {.kind = kind, .page = page, .group = d->undo_group_open};
    annot_name(a, u.nm, sizeof u.nm);
    push(d, u);
}

/* Pages deleted, moved, turned or added: the document before is kept in a
 * snapshot file, with the marks (their pages changed too). */
void undo_pages(Doc *d, int page, char *snapshot, const Mark *marks_before)
{
    Undo u = {.kind = U_PAGES, .page = page, .text_before = snapshot, .group = d->undo_group_open};
    u.marks_before = xmalloc(NMARKS * sizeof *u.marks_before);
    memcpy(u.marks_before, marks_before, NMARKS * sizeof *u.marks_before);
    push(d, u);
}

/* A move / resize / crop, from start to end of the edit. */
void undo_edit(Doc *d, int page, int annot, PRect before, PRect after, const ImageView *vbefore, const ImageView *vafter)
{
    FPDF_PAGE p = doc_page(d, page);
    FPDF_ANNOTATION a = p ? FPDFPage_GetAnnot(p, annot) : NULL;
    if (!a)
        return;
    Undo u = {.kind = U_EDIT, .page = page, .before = before, .after = after, .group = d->undo_group_open};
    annot_name(a, u.nm, sizeof u.nm);
    FPDFPage_CloseAnnot(a);
    if (vbefore && vafter) {
        u.has_view = true;
        u.vbefore = *vbefore;
        u.vafter = *vafter;
    }
    push(d, u);
}

static bool apply(Doc *d, Undo *u, bool redo)
{
    if (u->kind == U_PAGES) {
        if (!redo && !u->text_after) { /* the state to redo, taken once */
            u->text_after = pages_snapshot(d);
            u->marks_after = xmalloc(NMARKS * sizeof *u->marks_after);
            memcpy(u->marks_after, d->marks, NMARKS * sizeof *u->marks_after);
            if (!u->text_after)
                return false;
        }
        return pages_restore(d, redo ? u->text_after : u->text_before, redo ? u->marks_after : u->marks_before);
    }
    int idx = annot_by_name(d, u->page, u->nm);
    switch (u->kind) {
    case U_CREATE: return set_hidden(d, u->page, u->nm, !redo);
    case U_DELETE: return set_hidden(d, u->page, u->nm, redo);
    case U_COMMENT: {
        FPDF_PAGE p = doc_page(d, u->page);
        FPDF_ANNOTATION a = idx >= 0 && p ? FPDFPage_GetAnnot(p, idx) : NULL;
        if (!a)
            return false;
        const char *t = redo ? u->text_after : u->text_before;
        set_string(a, "Contents", t ? t : "");
        FPDFPage_CloseAnnot(a);
        doc_mark_changed(d, u->page);
        return true;
    }
    case U_PAGES:
        return false; /* above */
    case U_EDIT:
        if (idx < 0)
            return false;
        if (u->has_view)
            return note_image_apply(d, u->page, idx, redo ? &u->vafter : &u->vbefore, redo ? u->after : u->before);
        return note_move(d, u->page, idx, redo ? u->after : u->before) >= 0;
    }
    return false;
}

static const char *what(const Undo *u)
{
    switch (u->kind) {
    case U_CREATE: return "new highlight";
    case U_DELETE: return "deletion";
    case U_COMMENT: return "comment";
    case U_EDIT: return u->has_view ? "image change" : "move / resize";
    case U_PAGES: return "page change";
    }
    return "change";
}

/* Undoes (or redoes) the last change, or the last group; returns what it
 * was ("new highlight"), NULL when there is nothing. *page gets where. */
const char *doc_undo(Doc *d, bool redo, int *page)
{
    if (redo ? d->undo_at >= d->nundo : d->undo_at <= 0)
        return NULL;
    undoing = true;
    int group = d->undo[redo ? d->undo_at : d->undo_at - 1].group;
    const char *label = NULL;
    static char many[48];
    int count = 0;
    while (redo ? d->undo_at < d->nundo : d->undo_at > 0) {
        Undo *u = &d->undo[redo ? d->undo_at : d->undo_at - 1];
        if (u->group != group)
            break;
        apply(d, u, redo);
        label = what(u);
        if (++count > 1) {
            snprintf(many, sizeof many, "%d changes", count);
            label = many;
        }
        *page = u->page;
        d->undo_at += redo ? 1 : -1;
    }
    undoing = false;
    return label;
}

void doc_undo_clear(Doc *d)
{
    for (int i = 0; i < d->nundo; i++)
        undo_free(&d->undo[i]);
    free(d->undo);
    d->undo = NULL;
    d->nundo = d->undo_at = 0;
}

/* Closing: deleted annotations were only hidden (for undo); now they go. */
void doc_purge_deleted(Doc *d)
{
    bool moved = false; /* pages changed since: the recorded page numbers may be others */
    for (int k = 0; k < d->nundo; k++)
        moved |= d->undo[k].kind == U_PAGES;
    for (int k = 0; k < (moved ? d->npages : d->nundo); k++) {
        int page = moved ? k : d->undo[k].page;
        if (!moved && d->undo[k].kind != U_DELETE && d->undo[k].kind != U_CREATE)
            continue;
        FPDF_PAGE p = doc_page(d, page);
        for (int i = p ? FPDFPage_GetAnnotCount(p) - 1 : -1; i >= 0; i--) {
            FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, i);
            char mark[8];
            bool gone = a && get_string(a, "NomadysDeleted", mark, sizeof mark) && mark[0] == '1';
            if (a)
                FPDFPage_CloseAnnot(a);
            if (gone) {
                FPDFPage_RemoveAnnot(p, i);
                forget_widgets(&d->slots[page]);
                doc_mark_changed(d, page);
            }
        }
    }
    doc_undo_clear(d);
}

/* Colour, author and comment shared by every note we create. */
/* keep_name: the /NM of the annotation this one replaces (a move), which is
 * not a new change; NULL for a new annotation, recorded for undo. */
static void finish_note(Doc *d, int page, FPDF_ANNOTATION a, PRect bounds, const char *comment, uint32_t c,
                        const char *keep_name)
{
    if (keep_name)
        set_string(a, "NM", keep_name);
    else
        record(d, U_CREATE, page, a);
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

static bool highlight(Doc *d, int page, const PRect *quads, int n, const char *comment, uint32_t color, bool area)
{
    FPDF_PAGE p = doc_page(d, page);
    if (!p || n <= 0)
        return false;
    FPDF_ANNOTATION a = FPDFPage_CreateAnnot(p, FPDF_ANNOT_HIGHLIGHT);
    if (!a)
        return false;
    if (area)
        set_string(a, "NomadysArea", "1"); /* dark mode tints it more lightly than text */
    PRect u = quads[0];
    for (int i = 0; i < n; i++) {
        PRect r = quads[i];
        FS_QUADPOINTSF q = {r.l, r.t, r.r, r.t, r.l, r.b, r.r, r.b};
        FPDFAnnot_AppendAttachmentPoints(a, &q);
        u.l = SDL_min(u.l, r.l), u.r = SDL_max(u.r, r.r);
        u.t = SDL_max(u.t, r.t), u.b = SDL_min(u.b, r.b);
    }
    finish_note(d, page, a, u, comment, color, NULL);
    return true;
}

bool note_create(Doc *d, int page, const PRect *quads, int n, const char *comment, uint32_t color)
{
    return highlight(d, page, quads, n, comment, color, false);
}

/* An area such as a figure: a highlight (like Acrobat) or, with
 * area_style = frame, a rectangle drawn around it. */
bool note_create_area(Doc *d, int page, PRect r, const char *comment, uint32_t color)
{
    if (!cfg.area_frame)
        return highlight(d, page, &r, 1, comment, color, true);
    FPDF_PAGE p = doc_page(d, page);
    FPDF_ANNOTATION a = p ? FPDFPage_CreateAnnot(p, FPDF_ANNOT_SQUARE) : NULL;
    if (!a)
        return false;
    FPDFAnnot_SetBorder(a, 0, 0, 2);
    finish_note(d, page, a, r, comment, color, NULL);
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
    record(d, U_CREATE, page, a);
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
    char name[64], area[8] = "";
    annot_name(a, name, sizeof name); /* the new one is the same annotation */
    get_string(a, "NomadysArea", area, sizeof area);
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
        if (area[0] == '1')
            set_string(n, "NomadysArea", "1");
    }
    finish_note(d, page, n, r, comment, color, name);
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
    char before[4096];
    get_long_string(a, "Contents", before, sizeof before);
    Undo u = {.kind = U_COMMENT, .page = page, .text_before = xstrdup(before),
              .text_after = xstrdup(comment ? comment : ""), .group = d->undo_group_open};
    annot_name(a, u.nm, sizeof u.nm);
    push(d, u);
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
    FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, s->notes[note].idx);
    if (!a)
        return false;
    char nm[64];
    annot_name(a, nm, sizeof nm); /* hidden, not removed: u brings it back */
    record(d, U_DELETE, page, a);
    FPDFPage_CloseAnnot(a);
    return set_hidden(d, page, nm, true);
}

/* ---------- :merge — a co-author's highlights and comments ---------- */

/* What tells two annotations apart: the same one already here is skipped. */
typedef struct {
    int subtype;
    PRect r;
    char nm[64];
    char *contents;
} Known;

typedef struct {
    Known *k;
    int n, cap;
} KnownList;

static void known_add(KnownList *l, int subtype, PRect r, const char *nm, const char *contents)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->k = xrealloc(l->k, (size_t)l->cap * sizeof *l->k);
    }
    Known *k = &l->k[l->n++];
    *k = (Known){subtype, r, "", xstrdup(contents)};
    snprintf(k->nm, sizeof k->nm, "%s", nm);
}

static void known_free(KnownList *l)
{
    for (int i = 0; i < l->n; i++)
        free(l->k[i].contents);
    free(l->k);
    *l = (KnownList){0};
}

static bool near(int subtype, PRect a, PRect b)
{
    if (subtype == FPDF_ANNOT_TEXT) /* a note's icon: readers give it their own size */
        return SDL_fabsf(a.l - b.l) <= 2 && SDL_fabsf(a.b - b.b) <= 2;
    return SDL_fabsf(a.l - b.l) <= 1 && SDL_fabsf(a.r - b.r) <= 1 && SDL_fabsf(a.t - b.t) <= 1 &&
           SDL_fabsf(a.b - b.b) <= 1;
}

static bool known(const KnownList *l, int subtype, PRect r, const char *nm, const char *contents)
{
    for (int i = 0; i < l->n; i++) {
        const Known *k = &l->k[i];
        if ((nm[0] && !strcmp(k->nm, nm)) ||
            (k->subtype == subtype && near(subtype, k->r, r) && !strcmp(k->contents, contents)))
            return true;
    }
    return false;
}

/* The annotations on one of our pages (not the deleted ones). */
static void known_page(Doc *d, int page, KnownList *l)
{
    FPDF_PAGE p = doc_page(d, page);
    char nm[64], text[4096];
    for (int i = 0; p && i < FPDFPage_GetAnnotCount(p); i++) {
        FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, i);
        FS_RECTF r;
        if (!a)
            continue;
        if (!(FPDFAnnot_GetFlags(a) & FPDF_ANNOT_FLAG_HIDDEN) && FPDFAnnot_GetRect(a, &r)) {
            get_string(a, "NM", nm, sizeof nm);
            get_long_string(a, "Contents", text, sizeof text);
            known_add(l, FPDFAnnot_GetSubtype(a), norm(r), nm, text);
        }
        FPDFPage_CloseAnnot(a);
    }
}

/* A copy of a stamp's picture, placed as it was. */
static bool copy_stamp(Doc *d, FPDF_PAGE to, FPDF_DOCUMENT src_doc, FPDF_PAGE src_page, FPDF_ANNOTATION src,
                       FPDF_ANNOTATION dst)
{
    FPDF_PAGEOBJECT o = stamp_image(src);
    FS_MATRIX m;
    unsigned int pw, ph;
    if (!o || !FPDFPageObj_GetMatrix(o, &m) || !FPDFImageObj_GetImagePixelSize(o, &pw, &ph) || !pw || !ph ||
        (uint64_t)pw * ph > 64u << 20)
        return false;
    /* drawn at its own pixel size, so the transparency (a mask, which
     * GetBitmap leaves out) comes too; the other PDF is not saved */
    FPDFImageObj_SetMatrix(o, pw, 0, 0, ph, 0, 0);
    FPDF_BITMAP bmp = FPDFImageObj_GetRenderedBitmap(src_doc, src_page, o);
    FPDFImageObj_SetMatrix(o, m.a, m.b, m.c, m.d, m.e, m.f);
    if (!bmp)
        return false;
    FPDF_PAGEOBJECT img = FPDFPageObj_NewImageObj(d->pdf);
    bool ok = img && FPDFImageObj_SetBitmap(&to, 1, img, bmp);
    FPDFBitmap_Destroy(bmp);
    if (ok) {
        FPDFImageObj_SetMatrix(img, m.a, m.b, m.c, m.d, m.e, m.f);
        ok = FPDFAnnot_AppendObject(dst, img); /* the appearance keeps the rectangle as its box */
    }
    if (!ok && img)
        FPDFPageObj_Destroy(img);
    char view[256];
    if (ok && get_string(src, VIEW_KEY, view, sizeof view))
        set_string(dst, VIEW_KEY, view);
    return ok;
}

/* One annotation of the other PDF, made again on our page. */
static bool copy_annot(Doc *d, int page, FPDF_DOCUMENT sd, FPDF_PAGE sp, FPDF_ANNOTATION s, int sub, PRect r, const char *nm, const char *contents)
{
    FPDF_PAGE p = doc_page(d, page);
    FPDF_ANNOTATION a = p ? FPDFPage_CreateAnnot(p, sub) : NULL;
    if (!a)
        return false;
    FS_RECTF rect = {r.l, r.t, r.r, r.b};
    FPDFAnnot_SetRect(a, &rect);
    bool ok = true;
    if (sub == FPDF_ANNOT_STAMP) {
        ok = copy_stamp(d, p, sd, sp, s, a);
    } else {
        for (size_t q = 0; q < FPDFAnnot_CountAttachmentPoints(s); q++) {
            FS_QUADPOINTSF qp;
            if (FPDFAnnot_GetAttachmentPoints(s, q, &qp))
                FPDFAnnot_AppendAttachmentPoints(a, &qp);
        }
        for (unsigned long i = 0; i < FPDFAnnot_GetInkListCount(s); i++) {
            unsigned long n = FPDFAnnot_GetInkListPath(s, i, NULL, 0);
            if (!n || n > 100000)
                continue;
            FS_POINTF *pts = xmalloc(n * sizeof *pts);
            FPDFAnnot_GetInkListPath(s, i, pts, n);
            FPDFAnnot_AddInkStroke(a, pts, n);
            free(pts);
        }
        float hr, vr, width;
        if (FPDFAnnot_GetBorder(s, &hr, &vr, &width))
            FPDFAnnot_SetBorder(a, hr, vr, width);
        else if (sub == FPDF_ANNOT_SQUARE || sub == FPDF_ANNOT_CIRCLE || sub == FPDF_ANNOT_INK)
            FPDFAnnot_SetBorder(a, 0, 0, 2);
        uint32_t c = note_color(s);
        FPDFAnnot_SetColor(a, FPDFANNOT_COLORTYPE_Color, c >> 16, (c >> 8) & 0xFF, c & 0xFF, 255);
        unsigned int ir, ig, ib, ia;
        if (FPDFAnnot_GetColor(s, FPDFANNOT_COLORTYPE_InteriorColor, &ir, &ig, &ib, &ia))
            FPDFAnnot_SetColor(a, FPDFANNOT_COLORTYPE_InteriorColor, ir, ig, ib, ia);
    }
    if (!ok) {
        FPDFPage_CloseAnnot(a);
        FPDFPage_RemoveAnnot(p, FPDFPage_GetAnnotCount(p) - 1);
        return false;
    }
    FPDFAnnot_SetFlags(a, (FPDFAnnot_GetFlags(s) & ~FPDF_ANNOT_FLAG_HIDDEN) | FPDF_ANNOT_FLAG_PRINT);
    char author[256], name[64];
    if (get_string(s, "T", author, sizeof author))
        set_string(a, "T", author);
    if (get_string(s, "Name", name, sizeof name))
        set_string(a, "Name", name);
    if (*contents)
        set_string(a, "Contents", contents);
    if (*nm && annot_by_name(d, page, nm) < 0)
        set_string(a, "NM", nm); /* the same name: merging again finds it */
    record(d, U_CREATE, page, a);
    FPDFPage_CloseAnnot(a);
    doc_mark_changed(d, page);
    return true;
}

/* Brings the highlights, comments, frames, drawings and images of another
 * copy of this PDF (a co-author's) into this one, leaving out those already
 * here. One undo takes the whole merge back. */
bool doc_merge(Doc *d, const char *path, MergeResult *res, char *err, size_t cap)
{
    *res = (MergeResult){.first_page = -1};
    FPDF_DOCUMENT other = FPDF_LoadDocument(path, NULL);
    if (!other) {
        unsigned long e = FPDF_GetLastError();
        snprintf(err, cap, "cannot merge %s: %s", path,
                 e == FPDF_ERR_FILE ? "no such file" : e == FPDF_ERR_PASSWORD ? "it has a password" : "not a PDF");
        return false;
    }
    int pages = FPDF_GetPageCount(other);
    res->pages_differ = pages != d->npages;
    undo_group_begin(d);
    char nm[64], text[4096];
    for (int page = 0; page < pages; page++) {
        FPDF_PAGE sp = FPDF_LoadPage(other, page);
        int n = sp ? FPDFPage_GetAnnotCount(sp) : 0;
        KnownList here = {0};
        bool listed = false;
        for (int i = 0; i < n; i++) {
            FPDF_ANNOTATION s = FPDFPage_GetAnnot(sp, i);
            FS_RECTF fr;
            if (!s)
                continue;
            int sub = FPDFAnnot_GetSubtype(s);
            bool take = sub != FPDF_ANNOT_FREETEXT && is_markup(sub) &&
                        !(FPDFAnnot_GetFlags(s) & FPDF_ANNOT_FLAG_HIDDEN) && FPDFAnnot_GetRect(s, &fr);
            if (take && page >= d->npages) {
                res->skipped++;
                take = false;
            }
            if (take) {
                PRect r = norm(fr);
                get_string(s, "NM", nm, sizeof nm);
                get_long_string(s, "Contents", text, sizeof text);
                if (!listed) {
                    known_page(d, page, &here);
                    listed = true;
                }
                if (known(&here, sub, r, nm, text)) {
                    res->already++;
                } else if (copy_annot(d, page, other, sp, s, sub, r, nm, text)) {
                    known_add(&here, sub, r, nm, text);
                    res->added++;
                    if (res->first_page < 0)
                        res->first_page = page;
                } else {
                    res->skipped++;
                }
            } else if (sub == FPDF_ANNOT_FREETEXT) {
                res->skipped++;
            }
            FPDFPage_CloseAnnot(s);
        }
        known_free(&here);
        if (sp)
            FPDF_ClosePage(sp);
    }
    undo_group_end(d);
    FPDF_CloseDocument(other);
    return true;
}
