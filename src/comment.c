/* Comments with equations. Plain text where "$...$" (inline) and "$$...$$"
 * (on its own line) are typeset by LaTeX (math.c). Comments are edited in a
 * box next to the highlight: formulas are shown typeset while you write,
 * except the one the cursor is in, which shows its source. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

/* ---------- layout ---------- */
typedef enum { IT_TEXT, IT_MATH, IT_BREAK } ItemKind;

typedef struct {
    ItemKind kind;
    int start, len;          /* source bytes, delimiters included for math */
    int line;
    float x, w;
    bool display;
    uint32_t color;          /* text: 0 = normal */
    MathImage img;
} Item;

typedef struct {
    float y, asc, desc;
} Line;

typedef struct {
    Item *it;
    int n, cap;
    Line *lines;
    int nlines, lcap;
    float height;
    char error[160];         /* first LaTeX error, or "" (a copy: entries move) */
    bool pending;
} Layout;

static float math_scale(Window *w) { return font_em(w->font) / 10.0f * 1.2f; } /* 10pt LaTeX at the UI size */

static float width_n(Font *f, const char *s, int n)
{
    char buf[CMD_MAX];
    n = SDL_min(n, CMD_MAX - 1);
    memcpy(buf, s, (size_t)n);
    buf[n] = 0;
    return (float)font_width(f, buf);
}

static void draw_n(Font *f, float x, float y, const char *s, int n, uint32_t rgb)
{
    char buf[CMD_MAX];
    n = SDL_min(n, CMD_MAX - 1);
    memcpy(buf, s, (size_t)n);
    buf[n] = 0;
    font_draw(f, x, y, buf, rgb);
}

typedef struct {
    Window *win;
    const char *text;
    float maxw, x;
    int line;
    Layout *L;
} Builder;

static Item *push(Builder *b, Item it)
{
    Layout *L = b->L;
    if (L->n == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 64;
        L->it = xrealloc(L->it, (size_t)L->cap * sizeof *L->it);
    }
    it.line = b->line;
    it.x = b->x;
    b->x += it.w;
    L->it[L->n++] = it;
    return &L->it[L->n - 1];
}

static void new_line(Builder *b)
{
    b->line++;
    b->x = 0;
}

/* A run of text without line breaks, wrapped at spaces (or anywhere when a
 * single word is wider than the box). */
static void add_text(Builder *b, int start, int end, uint32_t color)
{
    Font *f = b->win->font;
    const char *s = b->text;
    int i = start;
    while (i < end) {
        int j = i;
        if (s[i] == ' ') {
            while (j < end && s[j] == ' ')
                j++;
            push(b, (Item){.kind = IT_TEXT, .start = i, .len = j - i, .w = width_n(f, s + i, j - i), .color = color});
            i = j;
            continue;
        }
        while (j < end && s[j] != ' ')
            j++;
        float ww = width_n(f, s + i, j - i);
        if (b->x > 0 && b->x + ww > b->maxw)
            new_line(b);
        while (ww > b->maxw && j > i) { /* a word longer than a line: split it */
            const char *p = s + i;
            int k = i;
            float acc = 0;
            while (k < j) {
                const char *q = p;
                utf8_next(&q);
                float cw = width_n(f, p, (int)(q - p));
                if (acc + cw > b->maxw && k > i)
                    break;
                acc += cw;
                k += (int)(q - p);
                p = q;
            }
            push(b, (Item){.kind = IT_TEXT, .start = i, .len = k - i, .w = acc, .color = color});
            new_line(b);
            i = k;
            ww = width_n(f, s + i, j - i);
        }
        if (j > i)
            push(b, (Item){.kind = IT_TEXT, .start = i, .len = j - i, .w = ww, .color = color});
        i = j;
    }
}

/* End of the formula starting at i ("$" or "$$"), just past its closing
 * delimiter, or -1 when it is never closed. */
static int math_end(const char *s, int i, int n, bool display)
{
    int j = i + (display ? 2 : 1);
    for (; j < n; j++) {
        if (s[j] == '\\' && j + 1 < n) {
            j++;
            continue;
        }
        if (s[j] != '$')
            continue;
        if (!display)
            return j > i + 1 ? j + 1 : -1; /* "$$" is not an empty inline formula */
        if (j + 1 < n && s[j + 1] == '$')
            return j > i + 2 ? j + 2 : -1;
    }
    return -1;
}

static void layout_build(Window *win, const char *text, int cursor, float maxw, Layout *L)
{
    const Theme *t = wtheme(win);
    uint32_t dim = rgb_mix(t->comment_fg, t->comment_bg, 0.45f), bad = t->caret;
    L->n = 0;
    L->error[0] = 0;
    L->pending = false;
    Builder b = {win, text, maxw, 0, 0, L};
    int n = (int)strlen(text), run = 0;
    for (int i = 0; i < n;) {
        if (text[i] == '\\' && i + 1 < n && text[i + 1] == '$') {
            i += 2; /* an escaped dollar is text */
            continue;
        }
        if (text[i] == '\n') {
            add_text(&b, run, i, 0);
            const Item *last = L->n ? &L->it[L->n - 1] : NULL;
            if (last && last->kind == IT_MATH && last->display && b.x == 0) {
                b.line--; /* a display formula already ended its line */
                b.x = last->x + last->w;
            }
            push(&b, (Item){.kind = IT_BREAK, .start = i, .len = 1});
            new_line(&b);
            run = ++i;
            continue;
        }
        if (text[i] != '$') {
            i++;
            continue;
        }
        bool display = i + 1 < n && text[i + 1] == '$';
        int end = math_end(text, i, n, display);
        if (end < 0) {
            i += display ? 2 : 1;
            continue;
        }
        add_text(&b, run, i, 0);
        run = end;
        int d = display ? 2 : 1;
        if (cursor > i && cursor < end) { /* being written: its source */
            add_text(&b, i, end, dim);
            i = end;
            continue;
        }
        MathImage img;
        MathStatus st = math_get(win->ren, text + i + d, (size_t)(end - i - 2 * d), display, math_scale(win), &img);
        if (st != MATH_READY) {
            if (st == MATH_FAILED && !L->error[0])
                snprintf(L->error, sizeof L->error, "%s", img.error);
            L->pending |= st == MATH_PENDING;
            add_text(&b, i, end, st == MATH_FAILED ? bad : dim);
            i = end;
            continue;
        }
        if (img.w > maxw) { /* too wide for the box: shrink it */
            float f = maxw / img.w;
            img.w = (int)maxw;
            img.h = SDL_max(1, (int)(img.h * f));
            img.baseline = (int)(img.baseline * f);
        }
        if (display && b.x > 0)
            new_line(&b);
        else if (!display && b.x > 0 && b.x + img.w > maxw)
            new_line(&b);
        Item *it = push(&b, (Item){.kind = IT_MATH, .start = i, .len = end - i, .w = (float)img.w,
                                   .display = display, .img = img});
        if (display) {
            it->x = SDL_max(0.0f, (maxw - img.w) / 2);
            new_line(&b);
        }
        i = end;
    }
    add_text(&b, run, n, 0);
    /* line heights: formulas may be taller than the text */
    int nl = b.line + 1;
    if (L->lcap < nl) {
        L->lcap = nl;
        L->lines = xrealloc(L->lines, (size_t)nl * sizeof *L->lines);
    }
    float asc = (float)font_ascent(win->font), desc = (float)(font_height(win->font) - font_ascent(win->font));
    for (int l = 0; l < nl; l++)
        L->lines[l] = (Line){0, asc, desc};
    for (int k = 0; k < L->n; k++) {
        Item *it = &L->it[k];
        if (it->kind != IT_MATH)
            continue;
        Line *ln = &L->lines[it->line];
        ln->asc = SDL_max(ln->asc, (float)it->img.baseline);
        ln->desc = SDL_max(ln->desc, (float)(it->img.h - it->img.baseline));
    }
    float y = 0, gap = font_height(win->font) * 0.25f;
    for (int l = 0; l < nl; l++) {
        L->lines[l].y = y;
        y += L->lines[l].asc + L->lines[l].desc + gap;
    }
    L->nlines = nl;
    L->height = y - gap;
}

static void layout_free(Layout *L)
{
    free(L->it);
    free(L->lines);
}

/* Where the cursor (a byte offset) is drawn: x and line. */
static void cursor_place(Window *win, const char *text, const Layout *L, int cursor, float *x, int *line)
{
    *x = 0;
    *line = 0;
    for (int k = 0; k < L->n; k++) {
        const Item *it = &L->it[k];
        if (cursor < it->start)
            break;
        *line = it->line;
        if (cursor >= it->start + it->len) {
            *x = it->x + it->w;
            if (it->kind == IT_BREAK)
                *line = it->line + 1, *x = 0;
            continue;
        }
        *x = it->kind == IT_TEXT ? it->x + width_n(win->font, text + it->start, cursor - it->start) : it->x;
        return;
    }
}

/* The byte offset on a line closest to x. */
static int offset_at(Window *win, const char *text, const Layout *L, int line, float x)
{
    int best = -1;
    float best_d = 1e30f;
    for (int k = 0; k < L->n; k++) {
        const Item *it = &L->it[k];
        if (it->line != line)
            continue;
        if (it->kind == IT_BREAK) {
            if (SDL_fabsf(it->x - x) < best_d)
                best_d = SDL_fabsf(it->x - x), best = it->start;
            continue;
        }
        if (it->kind == IT_MATH) {
            float d0 = SDL_fabsf(it->x - x), d1 = SDL_fabsf(it->x + it->w - x);
            if (d0 < best_d)
                best_d = d0, best = it->start;
            if (d1 < best_d)
                best_d = d1, best = it->start + it->len;
            continue;
        }
        const char *p = text + it->start, *e = p + it->len;
        float px = it->x;
        for (;;) {
            if (SDL_fabsf(px - x) < best_d)
                best_d = SDL_fabsf(px - x), best = (int)(p - text);
            if (p >= e)
                break;
            const char *q = p;
            utf8_next(&q);
            px += width_n(win->font, p, (int)(q - p));
            p = q;
        }
    }
    return best;
}

static void layout_paint(Window *win, const char *text, const Layout *L, float x0, float y0)
{
    const Theme *t = wtheme(win);
    for (int k = 0; k < L->n; k++) {
        const Item *it = &L->it[k];
        const Line *ln = &L->lines[it->line];
        float base = y0 + ln->y + ln->asc;
        if (it->kind == IT_TEXT) {
            draw_n(win->font, x0 + it->x, base - font_ascent(win->font), text + it->start, it->len,
                   it->color ? it->color : t->comment_fg);
        } else if (it->kind == IT_MATH && it->img.tex) {
            uint32_t c = t->comment_fg;
            SDL_SetTextureColorMod(it->img.tex, c >> 16, (c >> 8) & 0xFF, c & 0xFF);
            /* whole pixels: drawn 1:1, a fractional place would drop or double a row */
            SDL_FRect dst = {SDL_roundf(x0 + it->x), SDL_roundf(base - it->img.baseline), (float)it->img.w,
                             (float)it->img.h};
            float full = 0;
            SDL_GetTextureSize(it->img.tex, &full, NULL);
            SDL_SetTextureScaleMode(it->img.tex, it->img.w < full ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
            SDL_RenderTexture(win->ren, it->img.tex, NULL, &dst);
        }
    }
}

/* ---------- the comment under the cursor ---------- */
void comment_draw(Window *w, const char *text)
{
    const Theme *t = wtheme(w);
    int p = (int)(10 * w->density);
    float bw = (float)SDL_min(w->W - 4 * p, (int)(640 * w->density)), maxw = bw - 2 * p;
    Layout L = {0};
    layout_build(w, text, -1, maxw, &L);
    float maxh = (w->H - top_of(w)) * 0.45f, ch = SDL_min(L.height, maxh), bh = ch + p;
    float x = (w->W - bw) / 2, y = w->H - ui_bar_height(w) - 2.0f * p - bh;
    ui_fill(w, x, y, bw, bh, t->comment_bg, 245);
    ui_fill(w, x, y, 3 * w->density, bh, t->comment_marker, 255);
    SDL_Rect clip = {(int)x, (int)(y + p / 2), (int)bw, (int)ch + 1};
    SDL_SetRenderClipRect(w->ren, &clip);
    layout_paint(w, text, &L, x + p, y + p / 2.0f);
    SDL_SetRenderClipRect(w->ren, NULL);
    layout_free(&L);
}

/* ---------- the editor ---------- */
void comment_editor_open(Window *w, int page, PRect anchor)
{
    w->prompt_box = true;
    w->cmdpos = w->cmdlen;
    w->box_page = page;
    w->box_anchor = anchor;
    w->box_scroll = 0;
    w->box_maxw = 0;
}

static void box_geometry(Window *w, float bh, float bw, float *x, float *y)
{
    int p = (int)(10 * w->density), top = top_of(w);
    View *v = cur_view(w);
    if (w->box_page < 0 || !doc_page_ok(v->doc, w->box_page)) {
        *x = (w->W - bw) / 2;
        *y = SDL_max((float)top + p, (w->H - bh) / 3);
        return;
    }
    float x0, y0, x1, y1;
    PRect a = w->box_anchor;
    page_to_screen(v, w->box_page, a.l, a.t, &x0, &y0, top);
    page_to_screen(v, w->box_page, a.r, a.b, &x1, &y1, top);
    float ax = SDL_min(x0, x1), atop = SDL_min(y0, y1), abot = SDL_max(y0, y1);
    *x = SDL_clamp(ax, (float)p, SDL_max((float)p, w->W - bw - p));
    *y = abot + p; /* below the highlight, or above it when there is no room */
    if (*y + bh > w->H - p && atop - p - bh >= top + p)
        *y = atop - p - bh;
    *y = SDL_clamp(*y, (float)top + p, SDL_max((float)top + p, w->H - bh - p));
}

void comment_editor_draw(Window *w)
{
    const Theme *t = wtheme(w);
    int p = (int)(10 * w->density), fh = font_height(w->font);
    float bw = (float)SDL_min(w->W - 2 * p, (int)(620 * w->density)), maxw = bw - 2 * p;
    w->box_maxw = maxw;
    Layout L = {0};
    layout_build(w, w->cmd, w->cmdpos, maxw, &L);
    float cx;
    int cl;
    cursor_place(w, w->cmd, &L, w->cmdpos, &cx, &cl);
    const Line *ln = &L.lines[SDL_min(cl, L.nlines - 1)];
    /* the box grows with the text, up to half the window; then it scrolls */
    float maxh = (w->H - top_of(w)) * 0.5f, ch = SDL_max((float)fh, SDL_min(L.height, maxh));
    float ctop = ln->y, cbot = ln->y + ln->asc + ln->desc;
    if (ctop - w->box_scroll < 0)
        w->box_scroll = ctop;
    if (cbot - w->box_scroll > ch)
        w->box_scroll = cbot - ch;
    w->box_scroll = SDL_clamp(w->box_scroll, 0.0f, SDL_max(0.0f, L.height - ch));
    float hint_h = fh + p / 2.0f, bh = p + ch + p / 2.0f + hint_h;
    float x, y;
    box_geometry(w, bh, bw, &x, &y);
    ui_fill(w, x - 1, y - 1, bw + 2, bh + 2, t->comment_marker, 255);
    ui_fill(w, x, y, bw, bh, t->comment_bg, 255);
    float cy0 = y + p;
    SDL_Rect clip = {(int)x, (int)cy0, (int)bw, (int)ch + 1};
    SDL_SetRenderClipRect(w->ren, &clip);
    if (!w->cmd[0])
        font_draw(w->font, x + p, cy0, "write a comment; $E = mc^2$ for equations", rgb_mix(t->comment_fg, t->comment_bg, 0.55f));
    layout_paint(w, w->cmd, &L, x + p, cy0 - w->box_scroll);
    float cur_h = ln->asc + ln->desc;
    ui_fill(w, x + p + cx, cy0 + ln->y - w->box_scroll, SDL_max(2.0f, 2 * w->density), cur_h, t->bar_cursor, 255);
    SDL_SetRenderClipRect(w->ren, NULL);
    /* the last line: keys, or what LaTeX did not like */
    char hint[256];
    uint32_t hc = rgb_mix(t->comment_fg, t->comment_bg, 0.4f);
    if (L.error[0]) {
        snprintf(hint, sizeof hint - 4, "LaTeX: %s", L.error);
        hc = t->caret;
    } else {
        snprintf(hint, sizeof hint - 4, "%sEnter saves   Shift-Enter new line   Esc cancels",
                 L.pending ? "typesetting…   " : "");
    }
    font_fit(w->font, hint, (int)maxw);
    float hy = y + p + ch + p / 2.0f;
    ui_fill(w, x + p, hy - p / 4.0f, maxw, 1, rgb_mix(t->comment_fg, t->comment_bg, 0.8f), 255);
    font_draw(w->font, x + p, hy, hint, hc);
    layout_free(&L);
}

/* ---------- editing keys ---------- */
static void insert(Window *w, const char *s)
{
    size_t n = strlen(s);
    if (w->cmdlen + n >= CMD_MAX - 1)
        return;
    memmove(w->cmd + w->cmdpos + n, w->cmd + w->cmdpos, (size_t)(w->cmdlen - w->cmdpos) + 1);
    memcpy(w->cmd + w->cmdpos, s, n);
    w->cmdlen += (int)n;
    w->cmdpos += (int)n;
}

void comment_editor_insert(Window *w, const char *text) { insert(w, text); }

static int prev_char(const char *s, int i)
{
    while (i > 0 && ((unsigned char)s[--i] & 0xC0) == 0x80)
        ;
    return i;
}

static int next_char(const char *s, int i, int n)
{
    if (i >= n)
        return n;
    const char *p = s + i;
    utf8_next(&p);
    return (int)(p - s);
}

static void erase(Window *w, int from, int to)
{
    if (to <= from)
        return;
    memmove(w->cmd + from, w->cmd + to, (size_t)(w->cmdlen - to) + 1);
    w->cmdlen -= to - from;
    w->cmdpos = from;
}

static void vertical(Window *w, int dir, bool to_edge)
{
    Layout L = {0};
    layout_build(w, w->cmd, w->cmdpos, w->box_maxw > 0 ? w->box_maxw : 400, &L);
    float cx;
    int cl;
    cursor_place(w, w->cmd, &L, w->cmdpos, &cx, &cl);
    if (to_edge) { /* Home / End */
        int o = offset_at(w, w->cmd, &L, cl, dir < 0 ? -1e9f : 1e9f);
        if (o >= 0)
            w->cmdpos = o;
    } else if (cl + dir < 0) {
        w->cmdpos = 0;
    } else if (cl + dir >= L.nlines) {
        w->cmdpos = w->cmdlen;
    } else {
        int o = offset_at(w, w->cmd, &L, cl + dir, cx);
        if (o >= 0)
            w->cmdpos = o;
    }
    layout_free(&L);
}

bool comment_editor_key(Window *w, const char *tok)
{
    w->redraw = true;
    if (!strcmp(tok, "<Esc>")) {
        w->mode = w->prompt_return;
        ui_message(w, "comment not changed");
    } else if (!strcmp(tok, "<Enter>")) {
        w->mode = w->prompt_return;
        char *text = xstrdup(w->cmd);
        w->prompt_done(w, text);
        free(text);
    } else if (!strcmp(tok, "<S-Enter>")) {
        insert(w, "\n");
    } else if (!strcmp(tok, "<BS>")) {
        erase(w, prev_char(w->cmd, w->cmdpos), w->cmdpos);
    } else if (!strcmp(tok, "<Del>")) {
        erase(w, w->cmdpos, next_char(w->cmd, w->cmdpos, w->cmdlen));
    } else if (!strcmp(tok, "<C-w>")) { /* the word before the cursor */
        int i = w->cmdpos;
        while (i > 0 && w->cmd[i - 1] == ' ')
            i--;
        while (i > 0 && w->cmd[i - 1] != ' ' && w->cmd[i - 1] != '\n')
            i--;
        erase(w, i, w->cmdpos);
    } else if (!strcmp(tok, "<C-u>")) {
        erase(w, 0, w->cmdlen);
    } else if (!strcmp(tok, "<Left>")) {
        w->cmdpos = prev_char(w->cmd, w->cmdpos);
    } else if (!strcmp(tok, "<Right>")) {
        w->cmdpos = next_char(w->cmd, w->cmdpos, w->cmdlen);
    } else if (!strcmp(tok, "<Up>") || !strcmp(tok, "<Down>")) {
        vertical(w, tok[1] == 'U' ? -1 : 1, false);
    } else if (!strcmp(tok, "<Home>") || !strcmp(tok, "<End>")) {
        vertical(w, tok[1] == 'H' ? -1 : 1, true);
    } else if (!strcmp(tok, "<C-v>")) {
        char *clip = SDL_GetClipboardText();
        if (clip) {
            for (char *c = clip; *c; c++)
                if (*c == '\r' || *c == '\t')
                    *c = ' ';
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
