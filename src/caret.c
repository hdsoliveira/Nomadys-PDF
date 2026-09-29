/* Keyboard cursors: the text caret (mode CARET) that walks characters, and
 * the free cursor (mode AREA) that moves in page space to mark figures.
 * Both select with 'v' and create highlights, optionally with a comment. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

#define AREA_STEP 6.0f       /* points per hjkl step in the free cursor */

/* ---------- text caret ---------- */
static PageSlot *chars(View *v, int page)
{
    static PageSlot none; /* a page that no longer exists has no characters */
    if (!doc_page_ok(v->doc, page))
        return &none;
    doc_page_chars(v->doc, page);
    return &v->doc->slots[page];
}

static bool valid(View *v, TextPos p)
{
    if (p.page < 0 || p.page >= v->doc->npages)
        return false;
    PageSlot *s = chars(v, p.page);
    return p.ch >= 0 && p.ch < s->nchars && s->line[p.ch] >= 0;
}

/* A position left over from before the pages changed (a reload, a page
 * deleted or undone) has no box and no line. */
static PRect box(View *v, TextPos p) { return valid(v, p) ? chars(v, p.page)->boxes[p.ch] : (PRect){0}; }
static int line_of(View *v, TextPos p) { return valid(v, p) ? chars(v, p.page)->line[p.ch] : -1; }

static unsigned uni(View *v, TextPos p)
{
    FPDF_TEXTPAGE tp = doc_text(v->doc, p.page);
    return tp ? FPDFText_GetUnicode(tp, p.ch) : 0;
}

static int cmp(TextPos a, TextPos b)
{
    return a.page != b.page ? (a.page < b.page ? -1 : 1) : (a.ch > b.ch) - (a.ch < b.ch);
}

static bool same_line(View *v, TextPos a, TextPos b) { return a.page == b.page && line_of(v, a) == line_of(v, b); }
static bool is_space(unsigned c) { return c == ' ' || c == '\t' || c == 0xA0 || c == '\r' || c == '\n'; }

/* Next/previous selectable character, crossing page boundaries. */
static bool step(View *v, TextPos *p, int dir)
{
    TextPos q = *p;
    for (;;) {
        q.ch += dir;
        PageSlot *s = chars(v, q.page);
        if (q.ch < 0 || q.ch >= s->nchars) {
            q.page += dir;
            if (q.page < 0 || q.page >= v->doc->npages)
                return false;
            q.ch = dir > 0 ? -1 : chars(v, q.page)->nchars;
            continue;
        }
        if (s->line[q.ch] >= 0) {
            *p = q;
            return true;
        }
    }
}

/* Line below/above, keeping the preferred horizontal position. */
static void move_line(View *v, int dir)
{
    TextPos q = v->caret, best = {-1, -1}, first = {-1, -1};
    double bestd = 1e30;
    while (step(v, &q, dir)) {
        if (same_line(v, q, v->caret))
            continue;
        if (first.page < 0)
            first = q;
        else if (!same_line(v, q, first))
            break;
        PRect r = box(v, q);
        double d = SDL_fabs((r.l + r.r) / 2 - v->caret_x);
        if (d < bestd) {
            bestd = d;
            best = q;
        }
    }
    if (best.page >= 0)
        v->caret = best;
}

static void line_edge(View *v, int dir)
{
    TextPos q = v->caret;
    while (step(v, &q, dir) && same_line(v, q, v->caret))
        v->caret = q;
}

/* A word boundary between two neighbouring characters (a before b). PDFs
 * made by LaTeX have no space characters: PDFium generates them, and the
 * caret leaves generated characters out, so a skipped index or a visible
 * gap between the letters also separates words. */
static bool word_gap(View *v, TextPos a, TextPos b)
{
    if (!same_line(v, a, b) || is_space(uni(v, a)) || is_space(uni(v, b)))
        return true;
    if (a.page == b.page && b.ch > a.ch + 1)
        return true;
    PRect ra = box(v, a), rb = box(v, b);
    return rb.l - ra.r > 0.2f * (ra.t - ra.b);
}

static void word(View *v, int dir)
{
    TextPos q = v->caret;
    if (dir > 0) {
        TextPos prev = q;
        while (step(v, &q, 1)) {
            if (!is_space(uni(v, q)) && word_gap(v, prev, q))
                break;
            prev = q;
        }
    } else {
        step(v, &q, -1);
        while (is_space(uni(v, q)) && step(v, &q, -1))
            ;
        TextPos r = q;
        while (step(v, &r, -1) && !word_gap(v, r, q))
            q = r;
    }
    v->caret = q;
}

/* e: the last letter of this word, or of the next one (vim's e). */
static void word_end(View *v)
{
    TextPos q = v->caret, r;
    if (!step(v, &q, 1))
        return;
    while (is_space(uni(v, q)) && step(v, &q, 1))
        ;
    r = q;
    while (step(v, &r, 1) && !word_gap(v, q, r))
        q = r;
    v->caret = q;
}

static void remember_x(View *v)
{
    PRect r = box(v, v->caret);
    v->caret_x = (r.l + r.r) / 2;
}

static void show_rect(Window *w, View *v, int page, PRect r)
{
    view_show_rect(v, page, r, w->W, w->H, top_of(w));
    w->redraw = true;
}

bool caret_enter(Window *w, View *v)
{
    int top = top_of(w), first = view_top_page(v);
    for (int page = first; page < v->doc->npages && page <= first + 3; page++) {
        PageSlot *s = chars(v, page);
        for (int i = 0; i < s->nchars; i++) {
            float x, y;
            if (s->line[i] < 0)
                continue;
            page_to_screen(v, page, s->boxes[i].l, s->boxes[i].t, &x, &y, top);
            if (y >= top) {
                v->caret = (TextPos){page, i};
                v->selecting = false;
                remember_x(v);
                w->mode = MODE_CARET;
                w->locate_at = SDL_GetTicks();
                w->redraw = true;
                return true;
            }
        }
    }
    ui_message(w, "no selectable text here: use Ctrl-v for the free cursor");
    return false;
}

void caret_move(Window *w, View *v, char motion, int count)
{
    if (!valid(v, v->caret)) { /* the pages changed under the cursor */
        w->mode = MODE_NORMAL;
        v->selecting = false;
        ui_message(w, "the pages changed: v shows the cursor again");
        w->redraw = true;
        return;
    }
    for (int i = 0; i < SDL_max(1, count); i++) {
        switch (motion) {
        case 'h': step(v, &v->caret, -1); break;
        case 'l': step(v, &v->caret, 1); break;
        case 'j': move_line(v, 1); break;
        case 'k': move_line(v, -1); break;
        case 'w': word(v, 1); break;
        case 'e': word_end(v); break;
        case 'b': word(v, -1); break;
        case '0': line_edge(v, -1); break;
        case '$': line_edge(v, 1); break;
        }
    }
    if (motion != 'j' && motion != 'k')
        remember_x(v);
    show_rect(w, v, v->caret.page, box(v, v->caret));
}

void caret_toggle_select(Window *w, View *v)
{
    v->selecting = !v->selecting;
    v->anchor = v->caret;
    w->redraw = true;
}

/* Calls fn once per line of the selection with that line's box. */
typedef void (*LineFn)(View *v, int page, PRect r, void *ctx);

static void selection_lines(View *v, LineFn fn, void *ctx)
{
    TextPos a = v->anchor, b = v->caret;
    if (cmp(a, b) > 0) {
        TextPos t = a;
        a = b;
        b = t;
    }
    for (int page = a.page; page <= b.page; page++) {
        PageSlot *s = chars(v, page);
        int first = page == a.page ? a.ch : 0, last = page == b.page ? b.ch : s->nchars - 1;
        PRect cur = {0, 0, 0, 0};
        int line = -1;
        for (int i = first; i <= last && i < s->nchars; i++) {
            if (s->line[i] < 0)
                continue;
            PRect r = s->boxes[i];
            if (s->line[i] != line) {
                if (line >= 0)
                    fn(v, page, cur, ctx);
                cur = r;
                line = s->line[i];
            } else {
                cur.l = SDL_min(cur.l, r.l), cur.r = SDL_max(cur.r, r.r);
                cur.t = SDL_max(cur.t, r.t), cur.b = SDL_min(cur.b, r.b);
            }
        }
        if (line >= 0)
            fn(v, page, cur, ctx);
    }
}

typedef struct {
    int page;
    PRect *quads;
    int n, cap;
    const char *comment;
    uint32_t color;
    Doc *doc;
    int created;
} MarkCtx;

static void flush_marks(MarkCtx *c)
{
    if (c->n && note_create(c->doc, c->page, c->quads, c->n, c->comment, c->color))
        c->created++;
    c->n = 0;
}

static void collect_line(View *v, int page, PRect r, void *p)
{
    (void)v;
    MarkCtx *c = p;
    if (page != c->page)
        flush_marks(c);
    c->page = page;
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 16;
        c->quads = xrealloc(c->quads, c->cap * sizeof *c->quads);
    }
    c->quads[c->n++] = r;
}

static void highlight_selection(Window *w, View *v, const char *comment)
{
    if (!v->selecting)
        return; /* e.g. the PDF was reloaded meanwhile */
    MarkCtx c = {.page = -1, .comment = comment, .color = v->mark_color, .doc = v->doc};
    selection_lines(v, collect_line, &c);
    flush_marks(&c);
    free(c.quads);
    v->selecting = false;
    ui_message(w, comment && *comment ? "highlighted with a comment" : "highlighted");
}

static void comment_done_selection(Window *w, const char *text) { highlight_selection(w, cur_view(w), text); }

static void comment_done_edit(Window *w, const char *text)
{
    int page = w->prompt_arg >> 16, note = w->prompt_arg & 0xFFFF;
    note_set_comment(cur_view(w)->doc, page, note, text);
    ui_message(w, *text ? "comment saved" : "comment removed");
}

/* Edit the comment of the note under the cursor; false if there is none. */
static bool edit_note_comment(Window *w, View *v)
{
    int page, note;
    if (!cursor_note(w, v, &page, &note))
        return false;
    char *old = note_comment(v->doc, page, note);
    prompt_open(w, "comment: ", old ? old : "", comment_done_edit, (page << 16) | note);
    comment_editor_open(w, page, v->doc->slots[page].notes[note].r);
    free(old);
    return true;
}

static void reply_done(Window *w, const char *text)
{
    int page = w->prompt_arg >> 16, note = w->prompt_arg & 0xFFFF;
    note_set_reply(cur_view(w)->doc, page, note, text);
    ui_message(w, *text ? "reply saved" : "reply removed");
}

/* r: our reply to the note under the cursor, in the comment box. */
bool note_reply_edit(Window *w, View *v)
{
    int page, note;
    if (!cursor_note(w, v, &page, &note))
        return false;
    NoteReply *r;
    int n = note_replies(v->doc, page, note, &r);
    const char *old = "";
    for (int i = 0; i < n; i++)
        if (r[i].mine)
            old = r[i].text; /* the last of ours */
    prompt_open(w, "reply: ", old, reply_done, (page << 16) | note);
    comment_editor_open(w, page, v->doc->slots[page].notes[note].r);
    note_replies_free(r, n);
    return true;
}

/* A note's comment followed by its replies, "— author: text"; NULL if
 * there is nothing to show. */
char *note_thread(Doc *d, int page, int note)
{
    char *c = note_comment(d, page, note);
    if (!d->slots[page].notes[note].has_reply)
        return c;
    NoteReply *r;
    int n = note_replies(d, page, note, &r);
    size_t len = (c ? strlen(c) : 0) + 1;
    for (int i = 0; i < n; i++)
        len += strlen(r[i].author) + strlen(r[i].text) + 16;
    char *out = xmalloc(len);
    snprintf(out, len, "%s", c ? c : "");
    for (int i = 0; i < n; i++) {
        size_t at = strlen(out);
        snprintf(out + at, len - at, "%s— %s: %s", at ? "\n\n" : "", r[i].author[0] ? r[i].author : "reply", r[i].text);
    }
    note_replies_free(r, n);
    free(c);
    return out;
}

void caret_mark(Window *w, View *v, bool comment, uint32_t color)
{
    v->mark_color = color;
    if (v->selecting) {
        if (comment) {
            prompt_open(w, "comment: ", "", comment_done_selection, 0);
            comment_editor_open(w, v->caret.page, box(v, v->caret));
        }
        else
            highlight_selection(w, v, NULL);
    } else if (!(comment && edit_note_comment(w, v))) {
        ui_message(w, "select with v first");
    }
    w->redraw = true;
}

/* The selected text, hyphens at line ends joined; its first and last page. */
static char *selection_text(View *v, int *first_page, int *last_page)
{
    TextPos a = v->anchor, b = v->caret;
    if (cmp(a, b) > 0) {
        TextPos t = a;
        a = b;
        b = t;
    }
    char *buf = NULL;
    size_t len = 0;
    for (int page = a.page; page <= b.page; page++) {
        FPDF_TEXTPAGE tp = doc_text(v->doc, page);
        int first = page == a.page ? a.ch : 0;
        int last = page == b.page ? b.ch : chars(v, page)->nchars - 1;
        if (!tp || last < first)
            continue;
        int n = last - first + 1;
        unsigned short *u = xmalloc(((size_t)n + 1) * sizeof *u);
        int got = FPDFText_GetText(tp, first, n, u);
        char *s = utf16_to_utf8(u, got > 0 ? got : 0);
        text_join_hyphens(s);
        size_t sl = strlen(s);
        buf = xrealloc(buf, len + sl + 1);
        memcpy(buf + len, s, sl + 1);
        len += sl;
        free(s);
        free(u);
    }
    *first_page = a.page, *last_page = b.page;
    return buf;
}

void caret_copy(Window *w, View *v, bool cite)
{
    if (!v->selecting) {
        ui_message(w, "select with v before copying");
        return;
    }
    int first, last;
    char *text = selection_text(v, &first, &last);
    if (text && cite) {
        cite_copy(w, v, text, first, last);
    } else if (text) {
        SDL_SetClipboardText(text);
        ui_message(w, "copied (%zu characters)", strlen(text));
    }
    free(text);
    v->selecting = false;
    w->redraw = true;
}

/* ---------- selecting with the mouse ---------- */
/* Dragging over the text selects it, as the caret would, and the selection
 * goes to the clipboard (and the primary selection, for a middle click)
 * when the button is let go. The caret stays on it, so H, C, y and Y work
 * on it as after v. A press away from the text, or one that does not move,
 * is a plain click (links, form fields). */
static struct {
    Window *w;
    bool pressed, dragging;
    TextPos start;
    float x, y;
} drag;

/* The character nearest to a point of a page; false if the page has none
 * or it is more than `within` points away (< 0: at any distance). */
static bool char_near(View *v, int page, double px, double py, double within, TextPos *out)
{
    PageSlot *s = chars(v, page);
    double best = -1;
    for (int i = 0; i < s->nchars; i++) {
        if (s->line[i] < 0)
            continue;
        PRect r = s->boxes[i];
        double dx = px < r.l ? r.l - px : px > r.r ? px - r.r : 0;
        double dy = py > r.t ? py - r.t : py < r.b ? r.b - py : 0;
        double d = dx * dx + 4 * dy * dy; /* the same line before the next one */
        if (best < 0 || d < best)
            best = d, *out = (TextPos){page, i};
    }
    return best >= 0 && (within < 0 || best <= within * within);
}

void caret_mouse_down(Window *w, View *v, float x, float y)
{
    if (w->mode == MODE_CARET) { /* a click puts a selection away */
        v->selecting = false;
        w->mode = MODE_NORMAL;
        w->redraw = true;
    }
    int page;
    double px, py;
    drag.w = w;
    drag.x = x, drag.y = y;
    drag.dragging = false;
    drag.pressed = w->mode == MODE_NORMAL && screen_to_page(v, x, y, top_of(w), &page, &px, &py) &&
                   char_near(v, page, px, py, 10, &drag.start);
}

void caret_mouse_drag(Window *w, View *v, float x, float y)
{
    if (!drag.pressed || drag.w != w)
        return;
    if (!drag.dragging) {
        if (SDL_fabsf(x - drag.x) + SDL_fabsf(y - drag.y) < 6 * w->density || !valid(v, drag.start))
            return;
        drag.dragging = true;
        w->mode = MODE_CARET;
        v->anchor = v->caret = drag.start;
        v->selecting = true;
    }
    int page;
    double px, py;
    TextPos p;
    if (screen_to_page(v, x, y, top_of(w), &page, &px, &py) && char_near(v, page, px, py, -1, &p)) {
        v->caret = p;
        remember_x(v);
        w->redraw = true;
    }
}

void caret_mouse_up(Window *w, View *v)
{
    bool dragged = drag.pressed && drag.dragging && drag.w == w;
    drag.pressed = drag.dragging = false;
    if (!dragged || w->mode != MODE_CARET || !v->selecting)
        return;
    int first, last;
    char *text = selection_text(v, &first, &last);
    if (text && *text) {
        SDL_SetClipboardText(text);
        SDL_SetPrimarySelectionText(text);
        ui_message(w, "copied (%zu characters)", strlen(text));
    }
    free(text);
    w->redraw = true;
}

/* ---------- free cursor ---------- */
static PRect area_rect(View *v)
{
    return (PRect){SDL_min(v->area_x, v->area_ax), SDL_max(v->area_y, v->area_ay),
                   SDL_max(v->area_x, v->area_ax), SDL_min(v->area_y, v->area_ay)};
}

/* The free cursor: the rectangle being marked, or just its point
 * (rect.l, rect.t). Where a new image goes. */
bool area_target(View *v, int *page, PRect *rect, bool *is_rect)
{
    if (!doc_page_ok(v->doc, v->area_page))
        return false;
    *page = v->area_page;
    PRect r = area_rect(v);
    *is_rect = v->area_selecting && r.r - r.l >= 2 && r.t - r.b >= 2;
    *rect = *is_rect ? r : (PRect){v->area_x, v->area_y, v->area_x, v->area_y};
    return true;
}

void area_enter(Window *w, View *v)
{
    if (w->mode == MODE_CARET && valid(v, v->caret)) {
        PRect r = box(v, v->caret);
        v->area_page = v->caret.page;
        v->area_x = r.l;
        v->area_y = (r.t + r.b) / 2;
    } else {
        Pos p = view_pos(v);
        v->area_page = p.page;
        v->area_x = v->doc->pw[p.page] / 2;
        v->area_y = v->doc->ph[p.page] * (1 - SDL_clamp(p.y, 0.0f, 1.0f)) - 72;
    }
    v->area_selecting = false;
    w->mode = MODE_AREA;
    w->locate_at = SDL_GetTicks();
    ui_message(w, "free cursor: hjkl move (J K faster), v selects, C V B N M highlight, c comments");
    w->redraw = true;
}

void area_move(Window *w, View *v, float dx, float dy)
{
    Doc *d = v->doc;
    /* screen directions -> page directions on a rotated view */
    for (int r = 0; r < v->rotation; r++) {
        float t = dx;
        dx = dy;
        dy = -t;
    }
    v->area_x = SDL_clamp(v->area_x + dx * AREA_STEP, 0, d->pw[v->area_page]);
    v->area_y -= dy * AREA_STEP; /* page y grows upwards */
    if (!v->area_selecting && v->area_y < 0 && v->area_page + 1 < d->npages) {
        v->area_page++;
        v->area_y += d->ph[v->area_page];
    } else if (!v->area_selecting && v->area_y > d->ph[v->area_page] && v->area_page > 0) {
        v->area_y -= d->ph[v->area_page];
        v->area_page--;
    }
    v->area_y = SDL_clamp(v->area_y, 0, d->ph[v->area_page]);
    PRect r = {v->area_x - 4, v->area_y + 4, v->area_x + 4, v->area_y - 4};
    show_rect(w, v, v->area_page, r);
}

void area_toggle_select(Window *w, View *v)
{
    v->area_selecting = !v->area_selecting;
    v->area_ax = v->area_x;
    v->area_ay = v->area_y;
    w->redraw = true;
}

static void area_done(Window *w, const char *text)
{
    View *v = cur_view(w);
    PRect r = area_rect(v);
    note_create_area(v->doc, v->area_page, r, text, v->mark_color);
    v->area_selecting = false;
    ui_message(w, text && *text ? "area highlighted with a comment" : "area highlighted");
}

void area_mark(Window *w, View *v, bool comment, uint32_t color)
{
    v->mark_color = color;
    if (!v->area_selecting) {
        if (!(comment && edit_note_comment(w, v)))
            ui_message(w, "mark the area with v first");
        return;
    }
    PRect r = area_rect(v);
    if (r.r - r.l < 2 || r.t - r.b < 2) {
        ui_message(w, "empty area: move the cursor after v");
        return;
    }
    if (comment) {
        prompt_open(w, "comment: ", "", area_done, 0);
        comment_editor_open(w, v->area_page, r);
    }
    else
        area_done(w, NULL);
}

/* ---------- shared ---------- */
bool cursor_note(Window *w, View *v, int *page, int *note)
{
    Mode m = w->mode == MODE_PROMPT ? w->prompt_return : w->mode;
    if (m == MODE_CARET && valid(v, v->caret)) {
        PRect r = box(v, v->caret);
        *page = v->caret.page;
        *note = note_at(v->doc, *page, (r.l + r.r) / 2, (r.t + r.b) / 2);
    } else if (m == MODE_AREA) {
        *page = v->area_page;
        *note = note_at(v->doc, *page, v->area_x, v->area_y);
    } else {
        return false;
    }
    return *note >= 0;
}

typedef struct { Window *w; int top; uint32_t rgb; uint8_t a; } DrawCtx;

static void fill_page_rect(View *v, int page, PRect r, void *p)
{
    DrawCtx *c = p;
    float x0, y0, x1, y1;
    page_to_screen(v, page, r.l, r.t, &x0, &y0, c->top);
    page_to_screen(v, page, r.r, r.b, &x1, &y1, c->top);
    ui_fill(c->w, SDL_min(x0, x1), SDL_min(y0, y1), SDL_fabsf(x1 - x0), SDL_fabsf(y1 - y0), c->rgb, c->a);
}

bool cursor_locating(Window *w, uint64_t now) { return w->locate_at && now < w->locate_at + LOCATE_MS; }

/* Two rings closing in on the cursor right after it appears, so the eye
 * finds it at once (the cursors do not blink). */
static void draw_locate(Window *w, float cx, float cy, uint32_t halo)
{
    uint64_t now = SDL_GetTicks();
    if (!cursor_locating(w, now))
        return;
    float t = (float)(now - w->locate_at) / LOCATE_MS;
    for (int k = 0; k < 2; k++) {
        float u = (t - 0.3f * k) / 0.7f; /* the second ring starts a little later */
        if (u <= 0 || u >= 1)
            continue;
        float ease = 1 - (1 - u) * (1 - u) * (1 - u);
        float r = (110 - 96 * ease) * w->density, th = 3.5f * w->density;
        uint8_t a = (uint8_t)(255 * SDL_min(1.0f, (1 - u) * 2.5f));
        ui_ring(w, cx, cy, r + 1.5f * w->density, th + 3 * w->density, halo, (uint8_t)(a * 0.7f));
        ui_ring(w, cx, cy, r, th, wtheme(w)->caret, a);
    }
}

void cursor_draw(Window *w, View *v, int top)
{
    const Theme *t = wtheme(w);
    Mode m = w->mode == MODE_PROMPT ? w->prompt_return : w->mode;
    float th = SDL_max(2.0f, w->density * 2);
    if (m == MODE_CARET && valid(v, v->caret)) {
        if (v->selecting) {
            DrawCtx c = {w, top, t->selection, 90};
            selection_lines(v, fill_page_rect, &c);
        }
        PRect r = box(v, v->caret);
        float x0, y0, x1, y1;
        page_to_screen(v, v->caret.page, r.l, r.t, &x0, &y0, top);
        page_to_screen(v, v->caret.page, r.r, r.b, &x1, &y1, top);
        /* block over the character, translucent so the letter stays readable */
        float bx = SDL_min(x0, x1), by = SDL_min(y0, y1), bw = SDL_max(SDL_fabsf(x1 - x0), th), bh = SDL_fabsf(y1 - y0);
        ui_fill(w, bx, by, bw, bh, t->caret, 150);
        draw_locate(w, bx + bw / 2, by + bh / 2, v->dark ? 0xFFFFFF : 0x1A1A1A);
    } else if (m == MODE_AREA) {
        if (v->area_selecting) {
            DrawCtx c = {w, top, t->selection, 70};
            fill_page_rect(v, v->area_page, area_rect(v), &c);
        }
        /* a thin cross with a contrasting outline: visible on text, figures
         * and dark pages alike */
        float x, y, arm = 10 * w->density, bar = SDL_max(2.0f, 2 * w->density), o = SDL_max(1.0f, w->density);
        page_to_screen(v, v->area_page, v->area_x, v->area_y, &x, &y, top);
        x = SDL_roundf(x), y = SDL_roundf(y);
        uint32_t halo = v->dark ? 0xFFFFFF : 0x1A1A1A; /* the opposite of the page */
        ui_fill(w, x - arm - o, y - bar / 2 - o, 2 * (arm + o), bar + 2 * o, halo, 220);
        ui_fill(w, x - bar / 2 - o, y - arm - o, bar + 2 * o, 2 * (arm + o), halo, 220);
        ui_fill(w, x - arm, y - bar / 2, 2 * arm, bar, t->caret, 255);
        ui_fill(w, x - bar / 2, y - arm, bar, 2 * arm, t->caret, 255);
        draw_locate(w, x, y, halo);
    } else {
        return;
    }
    int page, note;
    if (cursor_note(w, v, &page, &note)) {
        char *text = note_thread(v->doc, page, note);
        if (text && *text)
            comment_draw(w, text);
        free(text);
    }
}
