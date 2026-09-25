/* Keyboard cursors: the text caret (mode CARET) that walks characters, and
 * the free cursor (mode AREA) that moves in page space to mark figures.
 * Both select with 'v' and create highlights, optionally with a comment. */
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

#define AREA_STEP 6.0f       /* points per hjkl step in the free cursor */

/* ---------- text caret ---------- */
static PageSlot *chars(View *v, int page)
{
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

static PRect box(View *v, TextPos p) { return chars(v, p.page)->boxes[p.ch]; }
static int line_of(View *v, TextPos p) { return chars(v, p.page)->line[p.ch]; }

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

static void word(View *v, int dir)
{
    TextPos q = v->caret;
    if (dir > 0) {
        TextPos prev = q;
        while (step(v, &q, 1)) {
            if (!is_space(uni(v, q)) && (is_space(uni(v, prev)) || !same_line(v, q, prev)))
                break;
            prev = q;
        }
    } else {
        step(v, &q, -1);
        while (is_space(uni(v, q)) && step(v, &q, -1))
            ;
        TextPos r = q;
        while (step(v, &r, -1) && !is_space(uni(v, r)) && same_line(v, r, q))
            q = r;
    }
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
                w->redraw = true;
                return true;
            }
        }
    }
    ui_message(w, "nenhum texto selecionável aqui: use Ctrl-v para o cursor livre");
    return false;
}

void caret_move(Window *w, View *v, char motion, int count)
{
    for (int i = 0; i < SDL_max(1, count); i++) {
        switch (motion) {
        case 'h': step(v, &v->caret, -1); break;
        case 'l': step(v, &v->caret, 1); break;
        case 'j': move_line(v, 1); break;
        case 'k': move_line(v, -1); break;
        case 'w': word(v, 1); break;
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
        c->quads = realloc(c->quads, c->cap * sizeof *c->quads);
    }
    c->quads[c->n++] = r;
}

static void highlight_selection(Window *w, View *v, const char *comment)
{
    MarkCtx c = {.page = -1, .comment = comment, .color = v->mark_color, .doc = v->doc};
    selection_lines(v, collect_line, &c);
    flush_marks(&c);
    free(c.quads);
    v->selecting = false;
    ui_message(w, comment && *comment ? "destacado com comentário" : "destacado");
}

static void comment_done_selection(Window *w, const char *text) { highlight_selection(w, cur_view(w), text); }

static void comment_done_edit(Window *w, const char *text)
{
    int page = w->prompt_arg >> 16, note = w->prompt_arg & 0xFFFF;
    note_set_comment(cur_view(w)->doc, page, note, text);
    ui_message(w, *text ? "comentário salvo" : "comentário removido");
}

/* Edit the comment of the note under the cursor; false if there is none. */
static bool edit_note_comment(Window *w, View *v)
{
    int page, note;
    if (!cursor_note(w, v, &page, &note))
        return false;
    char *old = note_comment(v->doc, page, note);
    prompt_open(w, "comentário: ", old ? old : "", comment_done_edit, (page << 16) | note);
    free(old);
    return true;
}

void caret_mark(Window *w, View *v, bool comment, uint32_t color)
{
    v->mark_color = color;
    if (v->selecting) {
        if (comment)
            prompt_open(w, "comentário: ", "", comment_done_selection, 0);
        else
            highlight_selection(w, v, NULL);
    } else if (!(comment && edit_note_comment(w, v))) {
        ui_message(w, "selecione com v primeiro");
    }
    w->redraw = true;
}

typedef struct { char *buf; size_t len; } CopyCtx;

void caret_copy(Window *w, View *v)
{
    if (!v->selecting) {
        ui_message(w, "selecione com v antes de copiar");
        return;
    }
    TextPos a = v->anchor, b = v->caret;
    if (cmp(a, b) > 0) {
        TextPos t = a;
        a = b;
        b = t;
    }
    CopyCtx c = {NULL, 0};
    for (int page = a.page; page <= b.page; page++) {
        FPDF_TEXTPAGE tp = doc_text(v->doc, page);
        int first = page == a.page ? a.ch : 0;
        int last = page == b.page ? b.ch : chars(v, page)->nchars - 1;
        if (!tp || last < first)
            continue;
        int n = last - first + 1;
        unsigned short *u = malloc(((size_t)n + 1) * sizeof *u);
        int got = FPDFText_GetText(tp, first, n, u);
        char *s = utf16_to_utf8(u, got > 0 ? got : 0);
        size_t sl = strlen(s);
        c.buf = realloc(c.buf, c.len + sl + 1);
        memcpy(c.buf + c.len, s, sl + 1);
        c.len += sl;
        free(s);
        free(u);
    }
    if (c.buf) {
        SDL_SetClipboardText(c.buf);
        ui_message(w, "copiado (%zu caracteres)", c.len);
        free(c.buf);
    }
    v->selecting = false;
    w->redraw = true;
}

/* ---------- free cursor ---------- */
static PRect area_rect(View *v)
{
    return (PRect){SDL_min(v->area_x, v->area_ax), SDL_max(v->area_y, v->area_ay),
                   SDL_max(v->area_x, v->area_ax), SDL_min(v->area_y, v->area_ay)};
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
    ui_message(w, "cursor livre: hjkl move (J K rápido), v seleciona, C V B N M destacam, c comenta");
    w->redraw = true;
}

void area_move(Window *w, View *v, float dx, float dy)
{
    Doc *d = v->doc;
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
    ui_message(w, text && *text ? "área destacada com comentário" : "área destacada");
}

void area_mark(Window *w, View *v, bool comment, uint32_t color)
{
    v->mark_color = color;
    if (!v->area_selecting) {
        if (!(comment && edit_note_comment(w, v)))
            ui_message(w, "marque a área com v primeiro");
        return;
    }
    PRect r = area_rect(v);
    if (r.r - r.l < 2 || r.t - r.b < 2) {
        ui_message(w, "área vazia: mova o cursor depois do v");
        return;
    }
    if (comment)
        prompt_open(w, "comentário: ", "", area_done, 0);
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

void cursor_draw(Window *w, View *v, int top, bool blink_on)
{
    const Theme *t = wtheme(w);
    Mode m = w->mode == MODE_PROMPT ? w->prompt_return : w->mode;
    float th = SDL_max(2.0f, w->density * 2);
    if (m == MODE_CARET && valid(v, v->caret)) {
        if (v->selecting) {
            DrawCtx c = {w, top, t->selection, 90};
            selection_lines(v, fill_page_rect, &c);
        }
        if (blink_on) {
            PRect r = box(v, v->caret);
            float x0, y0, x1, y1;
            page_to_screen(v, v->caret.page, r.l, r.t, &x0, &y0, top);
            page_to_screen(v, v->caret.page, r.r, r.b, &x1, &y1, top);
            /* block over the character, translucent so the letter stays readable */
            ui_fill(w, SDL_min(x0, x1), SDL_min(y0, y1), SDL_max(SDL_fabsf(x1 - x0), th),
                    SDL_fabsf(y1 - y0), t->caret, 150);
        }
    } else if (m == MODE_AREA) {
        if (v->area_selecting) {
            DrawCtx c = {w, top, t->selection, 70};
            fill_page_rect(v, v->area_page, area_rect(v), &c);
        }
        if (blink_on) {
            float x, y, arm = 14 * w->density, bar = 4 * w->density;
            page_to_screen(v, v->area_page, v->area_x, v->area_y, &x, &y, top);
            ui_fill(w, x - arm, y - bar / 2, 2 * arm, bar, t->caret, 255);
            ui_fill(w, x - bar / 2, y - arm, bar, 2 * arm, t->caret, 255);
        }
    } else {
        return;
    }
    int page, note;
    if (cursor_note(w, v, &page, &note)) {
        char *text = note_comment(v->doc, page, note);
        if (text && *text)
            ui_draw_comment(w, text);
        free(text);
    }
}
