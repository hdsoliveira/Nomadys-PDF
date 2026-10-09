/* Free text on the page: you mark where, write, and the box grows with the
 * text. The box is a stamp annotation (the PDF library can only put objects
 * in stamps and ink) holding the text as real text in Helvetica, one of the
 * 14 fonts every reader has, so nothing is embedded and the file grows by a
 * few bytes a letter; other readers can select and search it. The text, its
 * size and the width are kept in the annotation, so it can be written again;
 * in other readers it can be moved and deleted, not edited.
 *
 * While you write (or move, or resize) the PDF is left alone and this file
 * draws the text over the page; the PDF gets it once, at the end. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "nomadys.h"

/* ---------- Helvetica's widths (the Adobe font metrics), per 1000 ---------- */
static const short afm_regular[191] = { /* 32-126, then 160-255 */
    278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,
    556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,
    1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,
    667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,
    333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,
    556,556,333,500,278,556,500,722,500,500,500,334,260,334,584,278,
    333,556,556,556,556,260,556,333,737,370,556,584,333,737,333,400,
    584,333,333,333,556,537,278,333,333,365,556,834,834,834,611,667,
    667,667,667,667,667,1000,722,667,667,667,667,278,278,278,278,722,
    722,778,778,778,778,778,584,778,722,722,722,722,667,667,611,556,
    556,556,556,556,556,889,500,556,556,556,556,278,278,278,278,556,
    556,556,556,556,556,556,584,611,556,556,556,556,500,556,500,
};
/* WinAnsi's 0x80-0x9F: the Unicode letter and its width */
static const struct { unsigned cp; short width; } afm_extra[] = {
    {0x20AC, 556},
    {0x201A, 222},
    {0x192, 556},
    {0x201E, 333},
    {0x2026, 1000},
    {0x2020, 556},
    {0x2021, 556},
    {0x2C6, 333},
    {0x2030, 1000},
    {0x160, 667},
    {0x2039, 333},
    {0x152, 1000},
    {0x17D, 611},
    {0x2018, 222},
    {0x2019, 222},
    {0x201C, 333},
    {0x201D, 333},
    {0x2022, 350},
    {0x2013, 556},
    {0x2014, 1000},
    {0x2DC, 333},
    {0x2122, 1000},
    {0x161, 500},
    {0x203A, 333},
    {0x153, 944},
    {0x17E, 500},
    {0x178, 667},
};

/* The width of a character in Helvetica per 1000, or -1: not in its WinAnsi
 * encoding (such a letter shows as "?" for now). */
static int afm_width(unsigned cp)
{
    if (cp >= 32 && cp <= 126)
        return afm_regular[cp - 32];
    if (cp >= 160 && cp <= 255)
        return afm_regular[95 + cp - 160];
    for (size_t i = 0; i < SDL_arraysize(afm_extra); i++)
        if (afm_extra[i].cp == cp)
            return afm_extra[i].width;
    return -1;
}

static unsigned shown_cp(unsigned cp) { return afm_width(cp) >= 0 ? cp : '?'; }

/* A font to draw the letters while they change: one with Helvetica's
 * metrics when the system has it (Nimbus Sans, Liberation Sans, Arimo). */
static FT_Library ft;
static FT_Face face;
static bool face_tried;

static FT_Face get_face(void)
{
    if (face || face_tried)
        return face;
    face_tried = true;
    if (!ft && FT_Init_FreeType(&ft))
        return NULL;
    static const char *names[] = {"Helvetica", "sans", "monospace"};
    for (size_t i = 0; i < SDL_arraysize(names) && !face; i++) {
        int index = 0;
        char *file = font_file(names[i], &index);
        if (file && FT_New_Face(ft, file, index, &face))
            face = NULL;
        free(file);
    }
    return face;
}

static void gtex_clear(SDL_Renderer *ren);

void textbox_shutdown(void)
{
    gtex_clear(NULL);
    if (face)
        FT_Done_Face(face);
    if (ft)
        FT_Done_FreeType(ft);
    face = NULL, ft = NULL;
    face_tried = false;
}

/* ---------- layout ---------- */
#define LINE_EM 1.2f         /* line height, per size */
#define ASC_EM 0.856f        /* from the top of a line to its baseline */

typedef struct {
    int off;                 /* byte offset of the character in the text */
    unsigned cp;
    float x, adv;            /* from the left of the text area, points */
    int line;
} Glyph;

typedef struct {
    Glyph *g;
    int n, cap, nlines;
    float lh, asc;           /* line height and ascent, points */
} Layout;

static void push(Layout *L, Glyph g)
{
    if (L->n == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 64;
        L->g = xrealloc(L->g, (size_t)L->cap * sizeof *L->g);
    }
    L->g[L->n++] = g;
}

static bool printable_cp(unsigned cp) { return cp >= 32 && cp != 127 && !(cp >= 128 && cp < 160); }

static float advance_pt(unsigned cp, float size) { return (float)afm_width(shown_cp(cp)) * size / 1000.0f; }

/* Words wrap at spaces; a word wider than the box breaks where it must. */
static void layout_run(const char *text, float size, float width, Layout *L)
{
    *L = (Layout){0};
    L->lh = LINE_EM * size;
    L->asc = ASC_EM * size;
    float inner = SDL_max(width - 2 * TEXT_PAD, size), x = 0;
    int line = 0;
    const char *p = text;
    while (*p) {
        const char *at = p;
        unsigned cp = utf8_next(&p);
        int off = (int)(at - text);
        if (cp == '\n') {
            push(L, (Glyph){off, cp, x, 0, line});
            line++, x = 0;
            continue;
        }
        if (cp == '\t')
            cp = ' ';
        if (!printable_cp(cp))
            continue;
        if (cp == ' ') {
            float a = advance_pt(cp, size);
            push(L, (Glyph){off, cp, x, a, line});
            x += a;
            continue;
        }
        /* the word from here: wrap before it when it does not fit */
        float ww = 0;
        for (const char *q = at; *q && *q != ' ' && *q != '\n' && *q != '\t';) {
            unsigned c = utf8_next(&q);
            if (printable_cp(c))
                ww += advance_pt(c, size);
        }
        if (x > 0 && x + ww > inner)
            line++, x = 0;
        float a = advance_pt(cp, size);
        if (x > 0 && x + a > inner) /* a long word: break it */
            line++, x = 0;
        push(L, (Glyph){off, cp, x, a, line});
        x += a;
    }
    L->nlines = line + 1;
}

static void layout_free(Layout *L) { free(L->g); }

float textbox_height(const TextBox *b)
{
    Layout L;
    layout_run(b->text, b->size, b->width, &L);
    float h = L.nlines * L.lh + 2 * TEXT_PAD;
    layout_free(&L);
    return h;
}

/* Where the cursor is for a byte offset: x from the text area's left, and the line. */
static void caret_at(const Layout *L, const char *text, int off, float *x, int *line)
{
    for (int i = 0; i < L->n; i++)
        if (L->g[i].off == off) {
            *x = L->g[i].x, *line = L->g[i].line;
            return;
        }
    *x = 0, *line = 0;
    if (L->n) { /* past the end */
        const Glyph *g = &L->g[L->n - 1];
        if (g->cp == '\n')
            *x = 0, *line = g->line + 1;
        else
            *x = g->x + g->adv, *line = g->line;
    }
    (void)text;
}

/* ---------- the text, in the PDF ---------- */
static size_t put_utf16(unsigned short *o, unsigned cp)
{
    o[0] = (unsigned short)shown_cp(cp); /* everything we show is in the first plane */
    return 1;
}

/* One text object a line, Helvetica, black, at its place in the box. */
bool textbox_objects(Doc *d, FPDF_ANNOTATION a, const TextBox *b)
{
    Layout L;
    layout_run(b->text, b->size, b->width, &L);
    unsigned short *line = xmalloc(((size_t)L.n + 1) * sizeof *line);
    bool ok = true;
    for (int first = 0; ok && first < L.n;) {
        int ln = L.g[first].line, last = first;
        while (last + 1 < L.n && L.g[last + 1].line == ln)
            last++;
        /* the text of the line, without what hangs off its end (spaces, the line break) */
        int end = last;
        while (end >= first && (L.g[end].cp == ' ' || L.g[end].cp == '\n'))
            end--;
        if (end >= first) {
            size_t k = 0;
            for (int i = first; i <= end; i++)
                k += put_utf16(line + k, L.g[i].cp);
            line[k] = 0;
            FPDF_PAGEOBJECT t = FPDFPageObj_NewTextObj(d->pdf, "Helvetica", b->size);
            if (!t || !FPDFText_SetText(t, line)) {
                if (t)
                    FPDFPageObj_Destroy(t);
                ok = false;
                break;
            }
            FPDFPageObj_SetFillColor(t, 0, 0, 0, 255);
            FPDFPageObj_Transform(t, 1, 0, 0, 1, b->left + TEXT_PAD + L.g[first].x,
                                  b->top - TEXT_PAD - (float)ln * L.lh - L.asc);
            if (!FPDFAnnot_AppendObject(a, t)) {
                FPDFPageObj_Destroy(t);
                ok = false;
            }
        }
        first = last + 1;
    }
    free(line);
    layout_free(&L);
    return ok;
}

/* ---------- the letters, drawn over the page while they change ---------- */
typedef struct {
    SDL_Renderer *ren;
    unsigned cp;
    int px;
    SDL_Texture *tex;
    int left, top, w, h;
    bool used;
} GlyphTex;

#define GLYPH_SLOTS 1024
static GlyphTex gtex[GLYPH_SLOTS];
static int gtex_n;

static void gtex_clear(SDL_Renderer *ren)
{
    gtex_n = 0;
    for (int i = 0; i < GLYPH_SLOTS; i++) {
        if (gtex[i].used && (!ren || gtex[i].ren == ren) && gtex[i].tex)
            SDL_DestroyTexture(gtex[i].tex);
        if (!ren || gtex[i].ren == ren)
            gtex[i] = (GlyphTex){0};
        gtex_n += gtex[i].used;
    }
}

/* The textures belong to a renderer: dropped before it goes away. */
void textbox_forget_renderer(SDL_Renderer *ren) { gtex_clear(ren); }

static unsigned gtex_slot(SDL_Renderer *ren, unsigned cp, int px)
{
    return (cp * 2654435761u + (unsigned)px * 40503u + (unsigned)(uintptr_t)ren) % GLYPH_SLOTS;
}

static GlyphTex *glyph_tex(SDL_Renderer *ren, unsigned cp, int px)
{
    unsigned i = gtex_slot(ren, cp, px);
    for (int probe = 0; probe < GLYPH_SLOTS; probe++, i = (i + 1) % GLYPH_SLOTS) {
        GlyphTex *g = &gtex[i];
        if (g->used && g->ren == ren && g->cp == cp && g->px == px)
            return g;
        if (!g->used)
            break;
    }
    if (gtex_n > GLYPH_SLOTS * 3 / 4) { /* full: start again */
        gtex_clear(NULL);
        i = gtex_slot(ren, cp, px);
    }
    while (gtex[i].used)
        i = (i + 1) % GLYPH_SLOTS;
    GlyphTex *g = &gtex[i];
    *g = (GlyphTex){.ren = ren, .cp = cp, .px = px, .used = true};
    gtex_n++;
    FT_Face f = get_face();
    if (!f || FT_Set_Pixel_Sizes(f, 0, (FT_UInt)SDL_max(1, px)) || FT_Load_Char(f, cp, FT_LOAD_RENDER))
        return g;
    FT_GlyphSlot s = f->glyph;
    g->left = s->bitmap_left, g->top = s->bitmap_top, g->w = (int)s->bitmap.width, g->h = (int)s->bitmap.rows;
    if (g->w > 0 && g->h > 0 && s->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
        uint32_t *pix = xmalloc((size_t)g->w * g->h * 4);
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++)
                pix[y * g->w + x] = ((uint32_t)s->bitmap.buffer[y * s->bitmap.pitch + x] << 24) | 0xFFFFFF;
        g->tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, g->w, g->h);
        if (g->tex) {
            SDL_UpdateTexture(g->tex, NULL, pix, g->w * 4);
            SDL_SetTextureBlendMode(g->tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(g->tex, SDL_SCALEMODE_LINEAR);
        }
        free(pix);
    }
    return g;
}

/* The box's letters over the page, where the PDF does not show them. */
void textbox_paint(Window *w, View *v, int top, int page, const TextBox *b)
{
    if (!doc_page_ok(v->doc, page) || v->rotation != 0)
        return;
    Layout L;
    layout_run(b->text, b->size, b->width, &L);
    float ppp = (float)v->pwpx[page] / v->doc->pw[page]; /* pixels per point */
    int px = SDL_max(1, (int)(b->size * ppp + 0.5f));
    uint32_t ink = v->dark ? theme_of(true)->page_fg : 0x000000;
    for (int i = 0; i < L.n; i++) {
        const Glyph *g = &L.g[i];
        if (g->cp == ' ' || g->cp == '\n')
            continue;
        GlyphTex *t = glyph_tex(w->ren, shown_cp(g->cp), px);
        if (!t->tex)
            continue;
        float sx, sy;
        page_to_screen(v, page, b->left + TEXT_PAD + g->x, b->top - TEXT_PAD - (float)g->line * L.lh - L.asc, &sx, &sy,
                       top);
        SDL_SetTextureColorMod(t->tex, ink >> 16, (ink >> 8) & 0xFF, ink & 0xFF);
        SDL_FRect dst = {sx + (float)t->left, sy - (float)t->top, (float)t->w, (float)t->h};
        SDL_RenderTexture(w->ren, t->tex, NULL, &dst);
    }
    layout_free(&L);
}

/* ---------- keeping a box ---------- */
char *textbox_encode(const TextBox *b)
{
    size_t n = strlen(b->text) + 96;
    char *s = xmalloc(n);
    snprintf(s, n, "%g %g %g %g\n%s", b->size, b->width, b->left, b->top, b->text);
    return s;
}

bool textbox_decode(const char *s, TextBox *b)
{
    float size, width, left, top;
    const char *nl = strchr(s, '\n');
    if (!nl || sscanf(s, "%f %f %f %f", &size, &width, &left, &top) != 4 || !(size > 0) || !(width > 0))
        return false;
    *b = (TextBox){xstrdup(nl + 1), size, width, left, top};
    return true;
}

void textbox_free(TextBox *b)
{
    free(b->text);
    b->text = NULL;
}

/* ---------- writing ---------- */
static float last_size = 12;

static int prev_char(const char *s, int i)
{
    if (i <= 0)
        return 0;
    do
        i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80);
    return i;
}

static int next_char(const char *s, int i)
{
    const char *p = s + i;
    if (*p)
        utf8_next(&p);
    return (int)(p - s);
}

/* The box as it must be: on its page. */
static void keep_on_page(Doc *d, int page, TextBox *b)
{
    float pw = d->pw[page], ph = d->ph[page];
    b->width = SDL_clamp(b->width, 3 * b->size, pw);
    b->left = SDL_clamp(b->left, 0.0f, pw - b->width);
    float h = textbox_height(b);
    b->top = SDL_clamp(b->top, SDL_min(h, ph), ph);
}

/* Shows the cursor's line, moving the view when it is off the screen. */
static void show_cursor(Window *w, View *v)
{
    TextEd *t = &w->tx;
    Layout L;
    layout_run(t->box.text, t->box.size, t->box.width, &L);
    float x;
    int line;
    caret_at(&L, t->box.text, t->cursor, &x, &line);
    float y = t->box.top - TEXT_PAD - (float)line * L.lh;
    PRect r = {t->box.left + TEXT_PAD + x, y, t->box.left + TEXT_PAD + x + 1, y - L.lh};
    layout_free(&L);
    view_show_rect(v, t->page, r, w->W, w->H, top_of(w));
}

/* The text changed: the box is kept on the page and the screen redrawn (the
 * PDF is written at the end). */
static void apply(Window *w)
{
    View *v = cur_view(w);
    TextEd *t = &w->tx;
    keep_on_page(v->doc, t->page, &t->box);
    show_cursor(w, v);
    w->redraw = true;
}

static void describe(Window *w)
{
    ui_message_long(w, 60000, "text %.1f pt   Ctrl-= Ctrl-- size   Enter new line   Esc done   (a box can be moved with e, i writes again)",
                    w->tx.box.size);
}

void textbox_begin(Window *w, View *v)
{
    int page;
    PRect r;
    bool is_rect;
    if (w->mode != MODE_AREA || !area_target(v, &page, &r, &is_rect)) {
        ui_message(w, "put the cross where the text goes (Ctrl-v), or mark its width with v; then T");
        return;
    }
    Doc *d = v->doc;
    if (!doc_page_ok(d, page))
        return;
    TextBox b = {.text = xstrdup(""), .size = last_size};
    b.left = r.l, b.top = r.t;
    b.width = is_rect ? SDL_max(r.r - r.l, 6 * b.size) : SDL_min(200.0f, d->pw[page] - 2 * TEXT_PAD);
    if (!is_rect && v->area_selecting && SDL_fabsf(v->area_x - v->area_ax) >= 2) { /* only a width marked: along a line */
        b.left = SDL_min(v->area_x, v->area_ax);
        b.top = SDL_max(v->area_y, v->area_ay);
        b.width = SDL_max(SDL_fabsf(v->area_x - v->area_ax), 6 * b.size);
    }
    keep_on_page(d, page, &b);
    w->tx = (TextEd){.page = page, .annot = -1, .box = b, .cursor = 0, .fresh = true, .back = w->mode};
    v->area_selecting = false;
    w->mode = MODE_TEXT;
    describe(w);
    w->redraw = true;
}

void textbox_edit(Window *w, View *v, int page, int annot, int back)
{
    TextBox b;
    if (!note_text_get(v->doc, page, annot, &b)) {
        ui_message(w, "that is not a text box");
        return;
    }
    w->tx = (TextEd){.page = page, .annot = annot, .box = b, .cursor = (int)strlen(b.text), .back = back,
                     .before = textbox_encode(&b)};
    note_set_shown(v->doc, page, annot, false); /* while it is written the letters are drawn here */
    w->text_hidden = true;
    w->mode = MODE_TEXT;
    describe(w);
    show_cursor(w, v);
    w->redraw = true;
}

void textbox_finish(Window *w)
{
    View *v = cur_view(w);
    Doc *d = v->doc;
    TextEd *t = &w->tx;
    bool empty = !t->box.text[0];
    keep_on_page(d, t->page, &t->box);
    if (t->annot < 0 && !empty) { /* new: it goes into the PDF now */
        t->annot = note_text_create(d, t->page, &t->box);
        if (t->annot < 0)
            ui_message_long(w, 4000, "cannot put the text in the PDF");
    } else if (t->annot >= 0 && empty) { /* nothing left: the box goes, as it was (u brings it back) */
        note_set_shown(d, t->page, t->annot, true);
        w->text_hidden = false;
        int note = note_of_annot(d, t->page, t->annot);
        if (note >= 0)
            note_delete(d, t->page, note);
    } else if (t->annot >= 0) {
        note_set_shown(d, t->page, t->annot, true);
        w->text_hidden = false;
        char *after = textbox_encode(&t->box);
        bool changed = strcmp(after, t->before);
        int idx = changed ? note_text_update(d, t->page, t->annot, &t->box) : t->annot;
        if (idx < 0)
            ui_message_long(w, 4000, "cannot change the text");
        else
            t->annot = idx;
        if (changed && idx >= 0) /* one undo step for all the writing */
            undo_textbox(d, t->page, t->annot, t->before, after);
        free(after);
    }
    w->text_hidden = false;
    last_size = t->box.size;
    int back = t->back;
    textbox_free(&t->box);
    free(t->before);
    *t = (TextEd){.annot = -1};
    w->mode = back;
    w->msg_until = 0;
    ui_message(w, empty ? "no text" : "text written (e moves it, i writes again)");
    w->redraw = true;
}

/* The window is closing in the middle of it: the box must not stay hidden. */
void textbox_abort(Window *w)
{
    if (w->text_hidden && w->ntabs && doc_page_ok(cur_view(w)->doc, w->tx.page) && w->tx.annot >= 0)
        note_set_shown(cur_view(w)->doc, w->tx.page, w->tx.annot, true);
    w->text_hidden = false;
}

static void insert(Window *w, const char *s)
{
    TextEd *t = &w->tx;
    size_t n = strlen(t->box.text), k = strlen(s);
    t->box.text = xrealloc(t->box.text, n + k + 1);
    memmove(t->box.text + t->cursor + k, t->box.text + t->cursor, n - (size_t)t->cursor + 1);
    memcpy(t->box.text + t->cursor, s, k);
    t->cursor += (int)k;
    t->want_x = -1;
    apply(w);
}

static void erase(Window *w, int from, int to)
{
    TextEd *t = &w->tx;
    if (to <= from)
        return;
    memmove(t->box.text + from, t->box.text + to, strlen(t->box.text + to) + 1);
    t->cursor = from;
    t->want_x = -1;
    apply(w);
}

/* Up and Down keep the column. */
static void vertical(Window *w, int dir)
{
    TextEd *t = &w->tx;
    Layout L;
    layout_run(t->box.text, t->box.size, t->box.width, &L);
    float x;
    int line;
    caret_at(&L, t->box.text, t->cursor, &x, &line);
    if (t->want_x < 0)
        t->want_x = x;
    int target = line + dir;
    if (target >= 0 && target < L.nlines) {
        int best = -1;
        float bd = 0;
        for (int i = 0; i < L.n; i++) {
            if (L.g[i].line != target)
                continue;
            float dist = SDL_fabsf(L.g[i].x - t->want_x);
            if (best < 0 || dist < bd)
                best = i, bd = dist;
        }
        if (best >= 0)
            t->cursor = L.g[best].off;
        else
            t->cursor = (int)strlen(t->box.text); /* an empty last line */
    }
    layout_free(&L);
}

/* Home and End: the line's first character, and just before its last. */
static void line_edge(Window *w, bool end)
{
    TextEd *t = &w->tx;
    Layout L;
    layout_run(t->box.text, t->box.size, t->box.width, &L);
    float x;
    int line;
    caret_at(&L, t->box.text, t->cursor, &x, &line);
    int first = -1, last = -1;
    for (int i = 0; i < L.n; i++)
        if (L.g[i].line == line) {
            if (first < 0)
                first = i;
            last = i;
        }
    if (first >= 0) {
        if (!end)
            t->cursor = L.g[first].off;
        else if (L.g[last].cp == '\n' || last + 1 == L.n)
            t->cursor = L.g[last].cp == '\n' ? L.g[last].off : (int)strlen(t->box.text);
        else
            t->cursor = L.g[last].off; /* a wrapped line: before the space it wrapped at */
    }
    layout_free(&L);
    t->want_x = -1;
}

static void resize_font(Window *w, float factor)
{
    TextEd *t = &w->tx;
    float s = t->box.size * factor;
    s = roundf(s * 2) / 2; /* half points */
    if (s == t->box.size)
        s += factor > 1 ? 0.5f : -0.5f;
    t->box.size = SDL_clamp(s, TEXT_SIZE_MIN, TEXT_SIZE_MAX);
    describe(w);
    apply(w);
}

bool textbox_key(Window *w, const char *tok)
{
    TextEd *t = &w->tx;
    w->redraw = true;
    if (!strcmp(tok, "<Esc>")) {
        textbox_finish(w);
    } else if (!strcmp(tok, "<Enter>") || !strcmp(tok, "<S-Enter>")) {
        insert(w, "\n");
    } else if (!strcmp(tok, "<BS>")) {
        erase(w, prev_char(t->box.text, t->cursor), t->cursor);
    } else if (!strcmp(tok, "<Del>")) {
        erase(w, t->cursor, next_char(t->box.text, t->cursor));
    } else if (!strcmp(tok, "<C-w>")) { /* the word before the cursor */
        int i = t->cursor;
        while (i > 0 && (t->box.text[i - 1] == ' ' || t->box.text[i - 1] == '\n'))
            i--;
        while (i > 0 && t->box.text[i - 1] != ' ' && t->box.text[i - 1] != '\n')
            i--;
        erase(w, i, t->cursor);
    } else if (!strcmp(tok, "<Left>")) {
        t->cursor = prev_char(t->box.text, t->cursor), t->want_x = -1;
        show_cursor(w, cur_view(w));
    } else if (!strcmp(tok, "<Right>")) {
        t->cursor = next_char(t->box.text, t->cursor), t->want_x = -1;
        show_cursor(w, cur_view(w));
    } else if (!strcmp(tok, "<Up>") || !strcmp(tok, "<Down>")) {
        vertical(w, tok[1] == 'U' ? -1 : 1);
        show_cursor(w, cur_view(w));
    } else if (!strcmp(tok, "<Home>") || !strcmp(tok, "<End>")) {
        line_edge(w, tok[1] == 'E');
        show_cursor(w, cur_view(w));
    } else if (!strcmp(tok, "<C-=>") || !strcmp(tok, "<C-+>")) {
        resize_font(w, 1.1f);
    } else if (!strcmp(tok, "<C-->")) {
        resize_font(w, 1 / 1.1f);
    } else if (!strcmp(tok, "<C-v>")) {
        char *clip = SDL_GetClipboardText();
        if (clip) {
            char *o = clip;
            for (const char *c = clip; *c; c++) { /* no carriage returns, tabs as spaces */
                if (*c != '\r')
                    *o++ = *c == '\t' ? ' ' : *c;
            }
            *o = 0;
            insert(w, clip);
            SDL_free(clip);
        }
    } else if (!strcmp(tok, "<lt>")) {
        insert(w, "<");
    } else if (tok[0] != '<' || !tok[1]) {
        insert(w, tok);
    } else {
        return false;
    }
    return true;
}

void textbox_insert(Window *w, const char *text)
{
    char clean[1024];
    size_t n = 0;
    for (const char *s = text; *s && n + 1 < sizeof clean; s++)
        if ((unsigned char)*s >= 32 || *s == '\n')
            clean[n++] = *s;
    clean[n] = 0;
    if (n)
        insert(w, clean);
    w->redraw = true;
}

/* The box and the cursor, over the page. */
void textbox_draw(Window *w, View *v, int top)
{
    if (w->mode != MODE_TEXT || !doc_page_ok(v->doc, w->tx.page))
        return;
    const TextEd *t = &w->tx;
    const Theme *th = wtheme(w);
    textbox_paint(w, v, top, t->page, &t->box);
    float h = textbox_height(&t->box), x0, y0, x1, y1;
    page_to_screen(v, t->page, t->box.left, t->box.top, &x0, &y0, top);
    page_to_screen(v, t->page, t->box.left + t->box.width, t->box.top - h, &x1, &y1, top);
    float line = SDL_max(1.0f, w->density);
    ui_frame(w, SDL_min(x0, x1), SDL_min(y0, y1), SDL_fabsf(x1 - x0), SDL_fabsf(y1 - y0), line, th->search_match);
    Layout L;
    layout_run(t->box.text, t->box.size, t->box.width, &L);
    float cx;
    int cl;
    caret_at(&L, t->box.text, t->cursor, &cx, &cl);
    float py = t->box.top - TEXT_PAD - (float)cl * L.lh, px = t->box.left + TEXT_PAD + cx, ax, ay, bx, by;
    page_to_screen(v, t->page, px, py, &ax, &ay, top);
    page_to_screen(v, t->page, px, py - L.lh, &bx, &by, top);
    layout_free(&L);
    ui_line(w, ax, ay, bx, by, 2 * line, th->caret);
}
