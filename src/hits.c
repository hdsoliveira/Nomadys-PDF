/* The matches of a search as a list with the words around each one (\s, or
 * :hits [words]); Enter goes to the match. Same words, same smart case as '/'. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpdf_text.h"
#include "nomadys.h"

#define MAX_HITS 500
#define BEFORE 28 /* characters of context before a match, and after it */
#define AFTER 80

typedef struct {
    int page, start, len;
    char *text; /* the words around: before + match + after */
    int at, hl; /* where the match is in text (bytes) */
} Item;

typedef struct {
    Item *a;
    int n, sel, first;
    bool more; /* stopped at MAX_HITS */
    char query[CMD_MAX];
    Mode back;
} List;

static char *piece(FPDF_TEXTPAGE tp, int from, int count)
{
    if (count <= 0)
        return xstrdup("");
    unsigned short *u = xmalloc(((size_t)count + 1) * sizeof *u);
    int got = FPDFText_GetText(tp, from, count, u);
    for (int i = 0; i + 1 < got; i++)
        if (u[i] < 32)
            u[i] = ' ';
    char *s = utf16_to_utf8(u, got > 0 ? got - 1 : 0), *o = s;
    free(u);
    for (const char *c = s; *c; c++) /* single spaces */
        if (*c != ' ' || o == s || o[-1] != ' ')
            *o++ = *c;
    *o = 0;
    return s;
}

static void list_free(List *l)
{
    for (int i = 0; i < l->n; i++)
        free(l->a[i].text);
    free(l->a);
    free(l);
}

void hits_free(Window *w)
{
    if (w->hits)
        list_free(w->hits);
    w->hits = NULL;
}

void hits_open(Window *w, View *v, const char *query)
{
    if (!*query)
        query = v->query;
    if (!*query) {
        ui_message(w, "search first (/), or :hits words");
        return;
    }
    hits_free(w);
    List *l = xmalloc(sizeof *l);
    *l = (List){.back = w->mode};
    snprintf(l->query, sizeof l->query, "%s", query);
    for (int page = 0; page < v->doc->npages && !l->more; page++) {
        Match *m;
        int n = search_page_matches(v, page, query, &m);
        FPDF_TEXTPAGE tp = n ? doc_text(v->doc, page) : NULL;
        for (int i = 0; tp && i < n; i++) {
            if (l->n == MAX_HITS) {
                l->more = true;
                break;
            }
            int from = SDL_max(0, m[i].start - BEFORE), after = m[i].start + m[i].len;
            int total = FPDFText_CountChars(tp);
            char *pre = piece(tp, from, m[i].start - from), *mid = piece(tp, m[i].start, m[i].len),
                 *post = piece(tp, after, SDL_min(AFTER, total - after));
            size_t cap = strlen(pre) + strlen(mid) + strlen(post) + 1;
            Item it = {page, m[i].start, m[i].len, xmalloc(cap), (int)strlen(pre), (int)strlen(mid)};
            snprintf(it.text, cap, "%s%s%s", pre, mid, post);
            free(pre), free(mid), free(post);
            l->a = xrealloc(l->a, (size_t)(l->n + 1) * sizeof *l->a);
            l->a[l->n++] = it;
        }
        free(m);
    }
    if (!l->n) {
        ui_message(w, "not found: %s", query);
        list_free(l);
        return;
    }
    /* start at the first match from where you are reading */
    int here = view_top_page(v);
    for (int i = 0; i < l->n; i++)
        if (l->a[i].page >= here) {
            l->sel = i;
            break;
        }
    w->hits = l;
    w->mode = MODE_HITS;
    w->redraw = true;
}

static void close_list(Window *w)
{
    List *l = w->hits;
    w->mode = l && l->back != MODE_HITS ? l->back : MODE_NORMAL;
    hits_free(w);
    w->redraw = true;
}

void hits_key(Window *w, const char *tok)
{
    List *l = w->hits;
    if (!l) {
        w->mode = MODE_NORMAL;
        return;
    }
    int d = 0, rows = 10;
    if (!strcmp(tok, "j") || !strcmp(tok, "<Down>") || !strcmp(tok, "<C-j>") || !strcmp(tok, "<Tab>"))
        d = 1;
    else if (!strcmp(tok, "k") || !strcmp(tok, "<Up>") || !strcmp(tok, "<C-k>") || !strcmp(tok, "<S-Tab>"))
        d = -1;
    else if (!strcmp(tok, "<C-d>") || !strcmp(tok, "<PageDown>"))
        d = rows;
    else if (!strcmp(tok, "<C-u>") || !strcmp(tok, "<PageUp>"))
        d = -rows;
    else if (!strcmp(tok, "gg") || !strcmp(tok, "<Home>"))
        l->sel = 0;
    else if (!strcmp(tok, "G") || !strcmp(tok, "<End>"))
        l->sel = l->n - 1;
    if (d) {
        l->sel = SDL_clamp(l->sel + d, 0, l->n - 1);
    } else if (!strcmp(tok, "<Enter>")) {
        Item it = l->a[l->sel];
        char query[CMD_MAX];
        snprintf(query, sizeof query, "%s", l->query);
        close_list(w);
        search_goto(w, cur_view(w), query, it.page, it.start, it.len);
        return;
    } else if (!strcmp(tok, "<Esc>") || !strcmp(tok, "q")) {
        close_list(w);
        return;
    }
    w->redraw = true;
}

void hits_draw(Window *w)
{
    List *l = w->hits;
    if (!l)
        return;
    const Theme *t = uitheme(w);
    int pad = (int)(12 * w->density), lh = font_height(w->font), row = lh + (int)(10 * w->density);
    float bw = (float)SDL_min(w->W - 2 * pad, (int)(860 * w->density));
    float bh = SDL_min((w->H - top_of(w)) * 0.85f, (float)(l->n * row + 3 * lh + 3 * pad));
    float x = (w->W - bw) / 2, y = top_of(w) + (w->H - top_of(w) - bh) / 2;
    ui_fill(w, x, y, bw, bh, t->panel_bg, 255);
    ui_frame(w, x, y, bw, bh, SDL_max(1.0f, w->density), t->panel_header);
    char head[CMD_MAX + 64];
    snprintf(head, sizeof head, "%d%s match%s for \"%s\"", l->n, l->more ? "+" : "", l->n == 1 ? "" : "es", l->query);
    font_fit(w->font, head, (int)bw - 2 * pad);
    font_draw(w->font, x + pad, y + pad / 2.0f, head, t->panel_fg);
    float list_top = y + lh + pad * 1.5f, list_bottom = y + bh - lh - pad;
    ui_fill(w, x, list_top - pad / 2.0f, bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    ui_fill(w, x, list_bottom, bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    char foot[120];
    snprintf(foot, sizeof foot - 4, "j k move   Enter go to it   gg G   Esc close");
    font_fit(w->font, foot, (int)bw - 2 * pad);
    font_draw(w->font, x + pad, list_bottom + pad / 2.0f, foot, t->panel_header);
    int rows = SDL_max(1, (int)((list_bottom - list_top) / row));
    if (l->sel < l->first)
        l->first = l->sel;
    if (l->sel >= l->first + rows)
        l->first = l->sel - rows + 1;
    int pagew = font_width(w->font, "p. 0000") + pad;
    for (int i = l->first; i < l->n && i < l->first + rows; i++) {
        const Item *it = &l->a[i];
        float ry = list_top + (i - l->first) * row, ty = ry + (row - lh) / 2.0f;
        bool sel = i == l->sel;
        if (sel)
            ui_fill(w, x + 2, ry, bw - 4, (float)row, t->panel_selected_bg, 255);
        uint32_t fg = sel ? t->panel_selected_fg : t->panel_fg, dim = sel ? t->panel_selected_fg : t->panel_header,
                 hot = t->tab_accent;
        char num[24];
        snprintf(num, sizeof num, "p. %d", it->page + 1);
        font_draw(w->font, x + pad, ty, num, dim);
        float cx = x + pad + pagew, room = bw - 2 * pad - pagew;
        char pre[256], mid[256], post[512];
        snprintf(pre, sizeof pre, "%.*s", SDL_min(it->at, (int)sizeof pre - 1), it->text);
        snprintf(mid, sizeof mid, "%.*s", SDL_min(it->hl, (int)sizeof mid - 1), it->text + it->at);
        snprintf(post, sizeof post - 4, "%s", it->text + it->at + it->hl);
        font_draw(w->font, cx, ty, pre, fg);
        cx += font_width(w->font, pre);
        font_draw(w->font, cx, ty, mid, hot);
        cx += font_width(w->font, mid);
        font_fit(w->font, post, (int)SDL_max(0.0f, room - (cx - (x + pad + pagew))));
        font_draw(w->font, cx, ty, post, fg);
    }
}
