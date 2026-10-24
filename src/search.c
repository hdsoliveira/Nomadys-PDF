/* Text search ('/', 'n', 'N'). Incremental: while the query is typed the
 * view goes to the first match from where the search began (Esc goes back
 * there); every match on screen is highlighted until Esc or :noh.
 * Smart case: a query with a capital letter matches case, otherwise not;
 * Ctrl-t in the prompt forces matching case. Matches are jumps: Ctrl-o
 * returns. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

#define LIVE_PAGES 30        /* pages searched per key while typing */
#define COUNT_PAGES_MAX 150  /* "match 3 of 17" for documents up to this size */

static bool has_upper(const char *s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z')
            return true;
    return false;
}

static unsigned long search_flags(const View *v, const char *query)
{
    return v->match_case || has_upper(query) ? FPDF_MATCHCASE : 0;
}

int search_page_matches(View *v, int page, const char *query, Match **out)
{
    FPDF_TEXTPAGE tp = doc_text(v->doc, page);
    *out = NULL;
    if (!tp || !*query)
        return 0;
    unsigned short q[CMD_MAX];
    utf8_to_utf16(query, q, CMD_MAX);
    FPDF_SCHHANDLE h = FPDFText_FindStart(tp, q, search_flags(v, query), 0);
    if (!h)
        return 0;
    int n = 0, cap = 0;
    while (FPDFText_FindNext(h)) {
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            *out = xrealloc(*out, cap * sizeof **out);
        }
        (*out)[n++] = (Match){FPDFText_GetSchResultIndex(h), FPDFText_GetSchCount(h)};
    }
    FPDFText_FindClose(h);
    return n;
}

/* The position that shows the match, without moving the view. */
static Pos match_pos(Window *w, View *v)
{
    FPDF_TEXTPAGE tp = doc_text(v->doc, v->match_page);
    double l, t, r, b;
    Pos before = view_pos(v);
    if (!tp || FPDFText_CountRects(tp, v->match_start, v->match_len) <= 0 || !FPDFText_GetRect(tp, 0, &l, &t, &r, &b))
        return before;
    view_show_rect(v, v->match_page, (PRect){(float)l, (float)t, (float)r, (float)b}, w->W, w->H, top_of(w));
    Pos after = view_pos(v);
    view_set_pos(v, before);
    return after;
}

/* Finds the next match in direction dir; at most max_pages pages. */
static bool find(Window *w, View *v, const char *query, int dir, bool from_current, int max_pages, bool *whole)
{
    int npages = v->doc->npages;
    bool continuing = v->match_page >= 0 && !from_current;
    int start_page = continuing ? v->match_page : view_top_page(v);
    int cur = continuing ? v->match_start : (dir > 0 ? -1 : 1 << 30);
    int limit = SDL_min(npages, max_pages);
    *whole = limit >= npages;
    for (int k = 0; k <= limit; k++) {
        int page = ((start_page + dir * k) % npages + npages) % npages;
        Match *m;
        int n = search_page_matches(v, page, query, &m), pick = -1;
        for (int i = 0; i < n; i++) {
            if (k == 0 && (dir > 0 ? m[i].start <= cur : m[i].start >= cur))
                continue;
            if (k == npages && (dir > 0 ? m[i].start > cur : m[i].start < cur))
                continue; /* wrapped round to where we began */
            pick = i;
            if (dir > 0)
                break;
        }
        if (pick >= 0) {
            v->match_page = page;
            v->match_start = m[pick].start;
            v->match_len = m[pick].len;
            free(m);
            return true;
        }
        free(m);
    }
    return false;
}

/* "match 3 of 17", for documents small enough to count quickly. */
static void report(Window *w, View *v)
{
    if (v->doc->npages > COUNT_PAGES_MAX) {
        ui_message(w, "/%s  p. %d", v->query, v->match_page + 1);
        return;
    }
    int total = 0, index = 0;
    for (int page = 0; page < v->doc->npages; page++) {
        Match *m;
        int n = search_page_matches(v, page, v->query, &m);
        for (int i = 0; i < n; i++)
            if (page < v->match_page || (page == v->match_page && m[i].start <= v->match_start))
                index++;
        total += n;
        free(m);
    }
    ui_message(w, "/%s  p. %d  (match %d of %d)", v->query, v->match_page + 1, index, total);
}

bool search_run(Window *w, View *v, int dir, bool from_current)
{
    if (!v->query[0])
        return false;
    if (v->query_absent) { /* already searched everything: do not freeze again */
        ui_message(w, "not found: %s", v->query);
        return false;
    }
    bool whole;
    if (find(w, v, v->query, dir, from_current, v->doc->npages, &whole)) {
        v->highlight_all = true;
        jump_to(w, v, match_pos(w, v));
        report(w, v);
        w->redraw = true;
        return true;
    }
    v->match_page = -1;
    v->query_absent = true;
    ui_message(w, "not found: %s", v->query);
    return false;
}

/* Go to one match of a query (Enter in the list of matches). */
void search_goto(Window *w, View *v, const char *query, int page, int start, int len)
{
    snprintf(v->query, sizeof v->query, "%s", query);
    v->match_page = page;
    v->match_start = start;
    v->match_len = len;
    v->query_absent = false;
    v->highlight_all = true;
    jump_to(w, v, match_pos(w, v));
    report(w, v);
    w->redraw = true;
}

/* ---------- while the query is typed ---------- */
void search_begin(Window *w, View *v)
{
    v->search_origin = view_pos(v);
    v->search_saved_page = v->match_page;
    snprintf(v->search_saved, sizeof v->search_saved, "%s", v->query);
    (void)w;
}

/* The prompt text changed: show its first match from where we began. */
void search_live(Window *w, View *v, const char *text)
{
    view_set_pos(v, v->search_origin);
    snprintf(v->query, sizeof v->query, "%s", text);
    v->query_absent = false;
    v->match_page = -1;
    w->redraw = true;
    if (!*text)
        return;
    bool whole;
    if (find(w, v, text, 1, true, LIVE_PAGES, &whole)) {
        view_set_pos(v, match_pos(w, v)); /* not a jump yet: Enter makes it one */
        v->highlight_all = true;
    } else if (whole) {
        v->query_absent = true;
    }
}

/* Esc: back where the search began, with the previous search. */
void search_cancel(Window *w, View *v)
{
    view_set_pos(v, v->search_origin);
    snprintf(v->query, sizeof v->query, "%s", v->search_saved);
    v->match_page = v->search_saved_page;
    v->query_absent = false;
    w->redraw = true;
}

/* Enter: the match shown becomes a jump from where the search began. */
void search_commit(Window *w, View *v, const char *text)
{
    snprintf(v->query, sizeof v->query, "%s", text);
    if (!text[0]) {
        search_cancel(w, v);
        return;
    }
    if (v->match_page >= 0) {
        Pos here = view_pos(v);
        view_set_pos(v, v->search_origin);
        jump_to(w, v, here);
        v->highlight_all = true;
        report(w, v);
        return;
    }
    view_set_pos(v, v->search_origin); /* not in the first pages: search it all */
    v->query_absent = false;
    search_run(w, v, 1, true);
}

/* ---------- drawing ---------- */
/* Match rectangles of a page, cached: drawing happens on every scroll. */
#define HL_CACHE 8
static struct {
    const Doc *doc;
    int page;
    unsigned version;
    unsigned long flags;
    char query[CMD_MAX];
    PRect *rects;
    int n;
    uint64_t used;
} hl[HL_CACHE];
static uint64_t hl_clock;

static const PRect *page_rects(View *v, int page, int *n)
{
    unsigned long flags = search_flags(v, v->query);
    unsigned version = v->doc->slots[page].version;
    int slot = 0;
    for (int i = 0; i < HL_CACHE; i++) {
        if (hl[i].doc == v->doc && hl[i].page == page && hl[i].version == version && hl[i].flags == flags &&
            !strcmp(hl[i].query, v->query)) {
            hl[i].used = ++hl_clock;
            *n = hl[i].n;
            return hl[i].rects;
        }
        if (hl[i].used < hl[slot].used)
            slot = i;
    }
    free(hl[slot].rects);
    hl[slot].rects = NULL;
    hl[slot].n = 0;
    Match *m;
    int count = search_page_matches(v, page, v->query, &m), cap = 0;
    FPDF_TEXTPAGE tp = doc_text(v->doc, page);
    for (int i = 0; tp && i < count; i++) {
        int nr = FPDFText_CountRects(tp, m[i].start, m[i].len);
        for (int k = 0; k < nr; k++) {
            double l, t, r, b;
            if (!FPDFText_GetRect(tp, k, &l, &t, &r, &b))
                continue;
            if (hl[slot].n == cap) {
                cap = cap ? cap * 2 : 16;
                hl[slot].rects = xrealloc(hl[slot].rects, (size_t)cap * sizeof *hl[slot].rects);
            }
            hl[slot].rects[hl[slot].n++] = (PRect){(float)l, (float)t, (float)r, (float)b};
        }
    }
    free(m);
    hl[slot].doc = v->doc;
    hl[slot].page = page;
    hl[slot].version = version;
    hl[slot].flags = flags;
    snprintf(hl[slot].query, sizeof hl[slot].query, "%s", v->query);
    hl[slot].used = ++hl_clock;
    *n = hl[slot].n;
    return hl[slot].rects;
}

/* A document is closing or reloading: its cached rectangles go. */
void search_forget(const Doc *d)
{
    for (int i = 0; i < HL_CACHE; i++)
        if (hl[i].doc == d) {
            free(hl[i].rects);
            memset(&hl[i], 0, sizeof hl[i]);
        }
}

static void fill(Window *w, View *v, int page, PRect r, int top, uint8_t alpha)
{
    float x0, y0, x1, y1;
    page_to_screen(v, page, r.l, r.t, &x0, &y0, top);
    page_to_screen(v, page, r.r, r.b, &x1, &y1, top);
    ui_fill(w, SDL_min(x0, x1), SDL_min(y0, y1), SDL_fabsf(x1 - x0), SDL_fabsf(y1 - y0), wtheme(w)->search_match, alpha);
}

void search_draw(Window *w, View *v, int top)
{
    if (!v->query[0] || !v->highlight_all)
        return;
    for (int page = 0; page < v->doc->npages; page++) { /* every match on screen, lightly */
        double sy = v->py[page] - v->sy + top;
        if (sy >= w->H)
            break;
        if (sy + v->phpx[page] <= top)
            continue;
        int n;
        const PRect *r = page_rects(v, page, &n);
        for (int i = 0; i < n; i++)
            fill(w, v, page, r[i], top, 75);
    }
    if (v->match_page < 0 || !doc_page_ok(v->doc, v->match_page))
        return;
    FPDF_TEXTPAGE tp = doc_text(v->doc, v->match_page);
    int n = tp ? FPDFText_CountRects(tp, v->match_start, v->match_len) : 0;
    for (int i = 0; i < n; i++) { /* the current one, strongly */
        double l, t, r, b;
        if (FPDFText_GetRect(tp, i, &l, &t, &r, &b))
            fill(w, v, v->match_page, (PRect){(float)l, (float)t, (float)r, (float)b}, top, 140);
    }
}
