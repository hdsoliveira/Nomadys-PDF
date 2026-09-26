/* Text search ('/', 'n', 'N'); matches are jumps, so Ctrl-o returns. */
#include <stdlib.h>

#include "nomadys.h"

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
            *out = xrealloc(*out, cap * sizeof **out);
        }
        (*out)[n++] = (Match){FPDFText_GetSchResultIndex(h), FPDFText_GetSchCount(h)};
    }
    FPDFText_FindClose(h);
    return n;
}

static void show_match(Window *w, View *v)
{
    FPDF_TEXTPAGE tp = doc_text(v->doc, v->match_page);
    double l, t, r, b;
    if (!tp || FPDFText_CountRects(tp, v->match_start, v->match_len) <= 0 || !FPDFText_GetRect(tp, 0, &l, &t, &r, &b))
        return;
    Pos before = view_pos(v);
    view_show_rect(v, v->match_page, (PRect){(float)l, (float)t, (float)r, (float)b}, w->W, w->H, top_of(w));
    Pos after = view_pos(v);
    view_set_pos(v, before);
    jump_to(w, v, after);
}

bool search_run(Window *w, View *v, int dir, bool from_current)
{
    if (!v->query[0])
        return false;
    int npages = v->doc->npages;
    bool continuing = v->match_page >= 0 && !from_current;
    int start_page = continuing ? v->match_page : view_top_page(v);
    int cur = continuing ? v->match_start : (dir > 0 ? -1 : 1 << 30);
    for (int k = 0; k <= npages; k++) {
        int page = ((start_page + dir * k) % npages + npages) % npages;
        Match *m;
        int n = page_matches(v, page, &m), pick = -1;
        for (int i = 0; i < n; i++) {
            if (k == 0 && (dir > 0 ? m[i].start <= cur : m[i].start >= cur))
                continue;
            pick = i;
            if (dir > 0)
                break;
        }
        if (pick >= 0) {
            v->match_page = page;
            v->match_start = m[pick].start;
            v->match_len = m[pick].len;
            free(m);
            show_match(w, v);
            ui_message(w, "/%s  p. %d", v->query, page + 1);
            w->redraw = true;
            return true;
        }
        free(m);
    }
    v->match_page = -1;
    ui_message(w, "not found: %s", v->query);
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
        float x0, y0, x1, y1;
        page_to_screen(v, v->match_page, (float)l, (float)t, &x0, &y0, top);
        page_to_screen(v, v->match_page, (float)r, (float)b, &x1, &y1, top);
        ui_fill(w, SDL_min(x0, x1), SDL_min(y0, y1), SDL_fabsf(x1 - x0), SDL_fabsf(y1 - y0),
                wtheme(w)->search_match, 110);
    }
}
