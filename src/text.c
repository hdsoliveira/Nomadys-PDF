/* Text features: caret mode, selection, highlights, copying, search. */
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

#define SELECT_RGB 0x3C8CE7
#define MATCH_RGB 0xFF9D00
#define CARET_RGB 0xE64B35

static PageSlot *chars(View *v, int page)
{
    doc_page_chars(v->doc, page);
    return &v->doc->slots[page];
}

static bool valid(View *v, TextPos p)
{
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

static void remember_x(View *v)
{
    PRect r = box(v, v->caret);
    v->caret_x = (r.l + r.r) / 2;
}

/* Move to the line below/above, keeping the preferred horizontal position. */
static bool move_line(View *v, int dir)
{
    TextPos p = v->caret;
    int line = line_of(v, p);
    TextPos best = {-1, -1};
    double bestd = 1e30;
    int target_page = -1, target_line = -1;
    TextPos q = p;
    while (step(v, &q, dir)) {
        int l = line_of(v, q);
        if (target_page < 0) {
            if (q.page == p.page && l == line)
                continue;
            target_page = q.page;
            target_line = l;
        }
        if (q.page != target_page || l != target_line)
            break;
        PRect r = box(v, q);
        double d = SDL_fabs((r.l + r.r) / 2 - v->caret_x);
        if (d < bestd) {
            bestd = d;
            best = q;
        }
    }
    if (best.page < 0)
        return false;
    v->caret = best;
    return true;
}

static void line_edge(View *v, int dir)
{
    TextPos q = v->caret;
    int line = line_of(v, q);
    TextPos r = q;
    while (step(v, &r, dir) && r.page == q.page && line_of(v, r) == line)
        q = r;
    v->caret = q;
}

static bool is_space(unsigned c) { return c == ' ' || c == '\t' || c == 0xA0 || c == '\r' || c == '\n'; }

static void word(View *v, int dir)
{
    TextPos q = v->caret;
    if (dir > 0) {
        TextPos prev = q;
        while (step(v, &q, 1)) {
            bool newline = q.page != prev.page || line_of(v, q) != line_of(v, prev);
            if (!is_space(uni(v, q)) && (is_space(uni(v, prev)) || newline))
                break;
            prev = q;
        }
    } else {
        step(v, &q, -1);
        while (is_space(uni(v, q)) && step(v, &q, -1))
            ;
        TextPos r = q;
        while (step(v, &r, -1) && !is_space(uni(v, r)) && r.page == q.page && line_of(v, r) == line_of(v, q))
            q = r;
    }
    v->caret = q;
}

static void show_caret(Window *w, View *v)
{
    view_show_rect(v, v->caret.page, box(v, v->caret), w->W, w->H, ui_tabbar_height(w));
}

void caret_enter(Window *w, View *v)
{
    int top = ui_tabbar_height(w);
    for (int page = view_top_page(v); page < v->doc->npages; page++) {
        PageSlot *s = chars(v, page);
        for (int i = 0; i < s->nchars; i++) {
            if (s->line[i] < 0)
                continue;
            float x, y;
            page_to_screen(v, page, s->boxes[i].l, s->boxes[i].t, &x, &y, top);
            if (y >= top) {
                v->caret = (TextPos){page, i};
                v->selecting = false;
                remember_x(v);
                w->mode = MODE_CARET;
                ui_message(w, "cursor: hjkl w b 0 $  v selecionar  H destacar  y copiar  Esc sair");
                return;
            }
        }
        if (page > view_top_page(v) + 3)
            break;
    }
    ui_message(w, "nenhum texto selecionável aqui");
}

/* Iterate the selection as (page, first, last) character ranges. */
typedef void (*RangeFn)(View *v, int page, int a, int b, void *ctx);

static void for_selection(View *v, RangeFn fn, void *ctx)
{
    TextPos a = v->anchor, b = v->caret;
    if (cmp(a, b) > 0) {
        TextPos t = a;
        a = b;
        b = t;
    }
    for (int page = a.page; page <= b.page; page++) {
        int first = page == a.page ? a.ch : 0;
        int last = page == b.page ? b.ch : chars(v, page)->nchars - 1;
        if (last >= first)
            fn(v, page, first, last, ctx);
    }
}

/* Per-line boxes of a character range. */
typedef void (*LineFn)(View *v, int page, PRect r, void *ctx);

static void line_boxes(View *v, int page, int a, int b, LineFn fn, void *ctx)
{
    PageSlot *s = chars(v, page);
    PRect cur = {0, 0, 0, 0};
    int line = -1;
    for (int i = a; i <= b && i < s->nchars; i++) {
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

/* ---------- highlight annotations ---------- */
typedef struct { FPDF_ANNOTATION annot; PRect bounds; bool any; } HlCtx;

static void hl_line(View *v, int page, PRect r, void *p)
{
    (void)v, (void)page;
    HlCtx *c = p;
    FS_QUADPOINTSF q = {r.l, r.t, r.r, r.t, r.l, r.b, r.r, r.b};
    FPDFAnnot_AppendAttachmentPoints(c->annot, &q);
    if (!c->any)
        c->bounds = r;
    c->bounds.l = SDL_min(c->bounds.l, r.l), c->bounds.r = SDL_max(c->bounds.r, r.r);
    c->bounds.t = SDL_max(c->bounds.t, r.t), c->bounds.b = SDL_min(c->bounds.b, r.b);
    c->any = true;
}

static void hl_range(View *v, int page, int a, int b, void *ctx)
{
    (void)ctx;
    FPDF_PAGE p = doc_page(v->doc, page);
    if (!p)
        return;
    FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(p, FPDF_ANNOT_HIGHLIGHT);
    if (!annot)
        return;
    uint32_t c = cfg.highlight;
    FPDFAnnot_SetColor(annot, FPDFANNOT_COLORTYPE_Color, c >> 16, (c >> 8) & 0xFF, c & 0xFF, 255);
    FPDFAnnot_SetFlags(annot, FPDF_ANNOT_FLAG_PRINT);
    HlCtx h = {.annot = annot};
    line_boxes(v, page, a, b, hl_line, &h);
    if (h.any) {
        FS_RECTF r = {h.bounds.l, h.bounds.t, h.bounds.r, h.bounds.b};
        FPDFAnnot_SetRect(annot, &r);
    }
    FPDFPage_CloseAnnot(annot);
    doc_mark_changed(v->doc, page);
}

/* ---------- copying ---------- */
typedef struct { char *buf; size_t len; } CopyCtx;

static void copy_range(View *v, int page, int a, int b, void *p)
{
    CopyCtx *c = p;
    FPDF_TEXTPAGE tp = doc_text(v->doc, page);
    if (!tp)
        return;
    int n = b - a + 1;
    unsigned short *u = malloc(((size_t)n + 1) * sizeof *u);
    int got = FPDFText_GetText(tp, a, n, u);
    char *s = utf16_to_utf8(u, got > 0 ? got : 0);
    size_t sl = strlen(s);
    c->buf = realloc(c->buf, c->len + sl + 2);
    memcpy(c->buf + c->len, s, sl);
    c->len += sl;
    c->buf[c->len] = 0;
    free(s);
    free(u);
}

bool caret_key(Window *w, View *v, SDL_Keycode key, SDL_Keymod mod, const char *text)
{
    (void)mod;
    bool moved = false, horizontal = false;
    if (key == SDLK_ESCAPE) {
        if (v->selecting)
            v->selecting = false;
        else
            w->mode = MODE_NORMAL;
        w->redraw = true;
        return true;
    }
    if (!text || !text[0] || text[1])
        return false;
    int n = w->count ? w->count : 1;
    switch (text[0]) {
    case 'h': case 'l':
        for (int i = 0; i < n; i++)
            moved |= step(v, &v->caret, text[0] == 'l' ? 1 : -1);
        horizontal = true;
        break;
    case 'j': case 'k':
        for (int i = 0; i < n; i++)
            moved |= move_line(v, text[0] == 'j' ? 1 : -1);
        break;
    case 'w': case 'b':
        for (int i = 0; i < n; i++)
            word(v, text[0] == 'w' ? 1 : -1);
        moved = horizontal = true;
        break;
    case '0': case '^':
        if (w->count)
            return false;
        line_edge(v, -1);
        moved = horizontal = true;
        break;
    case '$':
        line_edge(v, 1);
        moved = horizontal = true;
        break;
    case 'v':
        v->selecting = !v->selecting;
        v->anchor = v->caret;
        break;
    case 'H':
        if (!v->selecting) {
            ui_message(w, "selecione com v antes de destacar");
            break;
        }
        for_selection(v, hl_range, NULL);
        v->selecting = false;
        ui_message(w, "destacado");
        break;
    case 'y': {
        if (!v->selecting) {
            ui_message(w, "selecione com v antes de copiar");
            break;
        }
        CopyCtx c = {NULL, 0};
        for_selection(v, copy_range, &c);
        if (c.buf) {
            SDL_SetClipboardText(c.buf);
            ui_message(w, "copiado (%zu caracteres)", c.len);
            free(c.buf);
        }
        v->selecting = false;
        break;
    }
    default:
        return false;
    }
    w->count = 0;
    if (horizontal)
        remember_x(v);
    if (moved || horizontal)
        show_caret(w, v);
    w->redraw = true;
    return true;
}

/* ---------- drawing ---------- */
typedef struct { Window *w; int top; uint32_t rgb; uint8_t a; } DrawCtx;

static void draw_line_box(View *v, int page, PRect r, void *p)
{
    DrawCtx *c = p;
    float x0, y0, x1, y1;
    page_to_screen(v, page, r.l, r.t, &x0, &y0, c->top);
    page_to_screen(v, page, r.r, r.b, &x1, &y1, c->top);
    ui_fill(c->w, SDL_min(x0, x1), SDL_min(y0, y1), SDL_fabsf(x1 - x0), SDL_fabsf(y1 - y0), c->rgb, c->a);
}

static void draw_range(View *v, int page, int a, int b, void *ctx)
{
    line_boxes(v, page, a, b, draw_line_box, ctx);
}

void caret_draw(Window *w, View *v, int top, bool blink_on)
{
    if (v->caret.page < 0 || !valid(v, v->caret))
        return;
    if (v->selecting) {
        DrawCtx c = {w, top, SELECT_RGB, 90};
        for_selection(v, draw_range, &c);
    }
    if (!blink_on)
        return;
    PRect r = box(v, v->caret);
    float x0, y0, x1, y1;
    page_to_screen(v, v->caret.page, r.l, r.t, &x0, &y0, top);
    page_to_screen(v, v->caret.page, r.r, r.b, &x1, &y1, top);
    float wd = SDL_max(2.0f, w->density * 2);
    ui_fill(w, SDL_min(x0, x1) - wd / 2, SDL_min(y0, y1), wd, SDL_fabsf(y1 - y0), CARET_RGB, 255);
}

/* ---------- search ---------- */
typedef struct { int start, len; } Match;

static int page_matches(View *v, int page, Match **out)
{
    FPDF_TEXTPAGE tp = doc_text(v->doc, page);
    *out = NULL;
    if (!tp)
        return 0;
    unsigned short q[CMD_MAX];
    utf8_to_utf16(v->query, q, CMD_MAX);
    FPDF_SCHHANDLE h = FPDFText_FindStart(tp, q, 0, 0);
    if (!h)
        return 0;
    int n = 0, cap = 0;
    while (FPDFText_FindNext(h)) {
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            *out = realloc(*out, cap * sizeof **out);
        }
        (*out)[n++] = (Match){FPDFText_GetSchResultIndex(h), FPDFText_GetSchCount(h)};
    }
    FPDFText_FindClose(h);
    return n;
}

bool search_run(Window *w, View *v, int dir, bool from_current)
{
    if (!v->query[0])
        return false;
    int npages = v->doc->npages, top = ui_tabbar_height(w);
    int start_page = v->match_page >= 0 && !from_current ? v->match_page : view_top_page(v);
    int cur = v->match_page == start_page && !from_current ? v->match_start : (dir > 0 ? -1 : 1 << 30);
    for (int k = 0; k <= npages; k++) {
        int page = ((start_page + dir * k) % npages + npages) % npages;
        Match *m;
        int n = page_matches(v, page, &m), pick = -1;
        for (int i = 0; i < n; i++) {
            if (k == 0 && (dir > 0 ? m[i].start <= cur : m[i].start >= cur))
                continue;
            if (dir > 0) {
                pick = i;
                break;
            }
            pick = i;
        }
        if (pick >= 0) {
            v->match_page = page;
            v->match_start = m[pick].start;
            v->match_len = m[pick].len;
            free(m);
            FPDF_TEXTPAGE tp = doc_text(v->doc, page);
            double l, t, r, b;
            if (FPDFText_CountRects(tp, v->match_start, v->match_len) > 0 && FPDFText_GetRect(tp, 0, &l, &t, &r, &b))
                view_show_rect(v, page, (PRect){(float)l, (float)t, (float)r, (float)b}, w->W, w->H, top);
            ui_message(w, "/%s  página %d", v->query, page + 1);
            w->redraw = true;
            return true;
        }
        free(m);
    }
    v->match_page = -1;
    ui_message(w, "não encontrado: %s", v->query);
    return false;
}

void search_draw(Window *w, View *v, int top)
{
    if (v->match_page < 0 || !v->query[0])
        return;
    FPDF_TEXTPAGE tp = doc_text(v->doc, v->match_page);
    if (!tp)
        return;
    int n = FPDFText_CountRects(tp, v->match_start, v->match_len);
    for (int i = 0; i < n; i++) {
        double l, t, r, b;
        if (!FPDFText_GetRect(tp, i, &l, &t, &r, &b))
            continue;
        DrawCtx c = {w, top, MATCH_RGB, 110};
        draw_line_box(v, v->match_page, (PRect){(float)l, (float)t, (float)r, (float)b}, &c);
    }
}
