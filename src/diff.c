/* ":diff old.pdf": what changed since an older version, word by word.
 * New or changed text is shaded green, a red mark shows where text was
 * deleted; ]c and [c go through the changes and say what the old text was.
 * Nothing is written to the PDF. When the PDF is recompiled (LaTeX) the
 * comparison is made again.
 *
 * Words come from PDFium's text; line-end hyphens and ligatures are undone,
 * and running heads and page numbers (the top and bottom 4% of a page) are
 * left out, so page breaks do not look like changes. The comparison is a
 * patience diff: words found once in both versions anchor it, the gaps
 * between anchors are compared again, small gaps word by word.
 *
 * The notes are compared too: highlights, comments, text boxes, pictures,
 * drawings. A note is the same one in both versions when it has the same name
 * (/NM) or, without one, the same kind in the same place; one that is only in
 * the new version is "new", in the old one "removed", and one whose text
 * changed "changed". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

#define EDGE 0.04f           /* page margin ignored at the top and bottom */
#define DP_MAX 4000000       /* cells for the word-by-word comparison */

typedef struct {
    uint64_t hash;
    int page, start, end;    /* characters on the page, [start, end) */
    int text, len;           /* in the words' text */
} Word;

typedef struct {
    Word *w;
    int n, cap;
    char *text;
    size_t tlen, tcap;
} Words;

enum { H_TEXT, H_NOTE_NEW, H_NOTE_CHANGED, H_NOTE_GONE };

typedef struct {
    int b0, b1;              /* new words [b0, b1): added or changed */
    char *was;               /* the old words, "" when only added (a note: its old text) */
    int page;                /* where it is, for ]c [c */
    float y;
    int kind;                /* H_TEXT, or a note that is new, changed or gone */
    PRect r;                 /* a note: where it is (was), on the page */
    int subtype;             /* a note: what it is */
    bool textbox;
    char *now;               /* a note: its text now */
} Hunk;

struct Diff {
    char *old_path;
    Words b;                 /* the words of the document shown */
    Hunk *h;
    int nh;
    int cur;                 /* the change ]c / [c showed last, -1 none */
    Pos cur_pos;             /* where the view was put for it */
};

/* ---------- words ---------- */
static void put_text(Words *ws, const char *s, size_t n)
{
    if (ws->tlen + n + 1 > ws->tcap) {
        ws->tcap = (ws->tlen + n + 1) * 2;
        ws->text = xrealloc(ws->text, ws->tcap);
    }
    memcpy(ws->text + ws->tlen, s, n);
    ws->tlen += n;
    ws->text[ws->tlen] = 0;
}

static void put_char(Words *ws, unsigned c)
{
    static const char *lig[] = {"ff", "fi", "fl", "ffi", "ffl", "st", "st"}; /* U+FB00..FB06 */
    if (c >= 0xFB00 && c <= 0xFB06) {
        put_text(ws, lig[c - 0xFB00], strlen(lig[c - 0xFB00]));
        return;
    }
    char u[4];
    int n = c < 0x80 ? (u[0] = (char)c, 1)
          : c < 0x800 ? (u[0] = (char)(0xC0 | c >> 6), u[1] = (char)(0x80 | (c & 63)), 2)
                      : (u[0] = (char)(0xE0 | c >> 12), u[1] = (char)(0x80 | ((c >> 6) & 63)), u[2] = (char)(0x80 | (c & 63)), 3);
    put_text(ws, u, (size_t)n);
}

static bool is_space(unsigned c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0xA0 || c == 0; }

static bool is_word_char(unsigned c)
{
    if (c < 0x80)
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    return !(c >= 0x2000 && c <= 0x206F) && c != 0xAB && c != 0xBB; /* dashes, quotes: punctuation */
}

/* A page number: a short number alone on its line (the text next to it in
 * reading order is on another line), in the top or bottom fifth of the page. */
static bool page_number(FPDF_TEXTPAGE tp, int start, int end, float ph)
{
    if (end - start > 4)
        return false;
    for (int i = start; i < end; i++) {
        unsigned c = FPDFText_GetUnicode(tp, i);
        if (c < '0' || c > '9')
            return false;
    }
    double l, r, b, t;
    if (!FPDFText_GetCharBox(tp, start, &l, &r, &b, &t) || (t > ph * 0.2f && b < ph * 0.8f))
        return false;
    int n = FPDFText_CountChars(tp);
    for (int dir = -1; dir <= 1; dir += 2) { /* the nearest text before and after */
        for (int i = dir < 0 ? start - 1 : end; i >= 0 && i < n; i += dir) {
            if (is_space(FPDFText_GetUnicode(tp, i)))
                continue;
            double l2, r2, b2, t2;
            if (FPDFText_GetCharBox(tp, i, &l2, &r2, &b2, &t2) && t2 > b && b2 < t)
                return false; /* on the same line: part of the text */
            break;
        }
    }
    return true;
}

static void end_word(Words *ws, int page, int start, int end, size_t text_start)
{
    if (ws->tlen == text_start)
        return;
    if (ws->n == ws->cap) {
        ws->cap = ws->cap ? ws->cap * 2 : 4096;
        ws->w = xrealloc(ws->w, (size_t)ws->cap * sizeof *ws->w);
    }
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = text_start; i < ws->tlen; i++)
        h = (h ^ (unsigned char)ws->text[i]) * 0x100000001b3ULL;
    ws->w[ws->n++] = (Word){h, page, start, end, (int)text_start, (int)(ws->tlen - text_start)};
    put_text(ws, "", 0);
    ws->tlen++; /* a NUL between words */
}

static void page_words(FPDF_PAGE p, FPDF_TEXTPAGE tp, int page, Words *ws)
{
    float ph = FPDF_GetPageHeightF(p), lo = ph * EDGE, hi = ph * (1 - EDGE);
    int n = FPDFText_CountChars(tp), start = -1;
    size_t text_start = ws->tlen;
    for (int i = 0; i <= n; i++) {
        unsigned c = i < n ? FPDFText_GetUnicode(tp, i) : 0;
        if (c == 0x02 || c == 0xAD) { /* a word broken at the line end goes on */
            while (i + 1 < n && is_space(FPDFText_GetUnicode(tp, i + 1)))
                i++;
            continue;
        }
        bool word = is_word_char(c) && !is_space(c);
        if ((c == '.' || c == ',') && start >= 0 && i + 1 < n) { /* 3.2, 1,000: one number */
            unsigned before = FPDFText_GetUnicode(tp, i - 1), after = FPDFText_GetUnicode(tp, i + 1);
            word = before >= '0' && before <= '9' && after >= '0' && after <= '9';
        }
        if (word && start >= 0) {
            put_char(ws, c);
            continue;
        }
        if (start >= 0) { /* the word ends here */
            if (page_number(tp, start, i, ph))
                ws->tlen = text_start; /* dropped: page breaks are not changes */
            else
                end_word(ws, page, start, i, text_start);
            start = -1;
        }
        if (i == n || is_space(c))
            continue;
        double l, r, b, t;
        if (FPDFText_GetCharBox(tp, i, &l, &r, &b, &t) && (t < lo || b > hi))
            continue; /* running head or page number */
        text_start = ws->tlen;
        put_char(ws, c);
        if (word) {
            start = i;
        } else {
            end_word(ws, page, i, i + 1, text_start); /* punctuation: a word of its own */
        }
    }
}

static bool doc_words(FPDF_DOCUMENT pdf, Words *ws)
{
    int n = FPDF_GetPageCount(pdf);
    for (int page = 0; page < n; page++) {
        FPDF_PAGE p = FPDF_LoadPage(pdf, page);
        FPDF_TEXTPAGE tp = p ? FPDFText_LoadPage(p) : NULL;
        if (tp)
            page_words(p, tp, page, ws);
        if (tp)
            FPDFText_ClosePage(tp);
        if (p)
            FPDF_ClosePage(p);
    }
    return true;
}

static void free_words(Words *ws)
{
    free(ws->w);
    free(ws->text);
    memset(ws, 0, sizeof *ws);
}

/* ---------- comparing ---------- */
typedef struct {
    const Word *a, *b;
    bool *gone, *added;      /* per old word, per new word */
} Cmp;

typedef struct {
    uint64_t hash;
    int index;
} Key;

static int key_cmp(const void *x, const void *y)
{
    const Key *a = x, *b = y;
    return a->hash < b->hash ? -1 : a->hash > b->hash ? 1 : a->index - b->index;
}

static Key *sorted_keys(const Word *w, int from, int to)
{
    Key *k = xmalloc((size_t)(to - from + 1) * sizeof *k);
    for (int i = from; i < to; i++)
        k[i - from] = (Key){w[i].hash, i};
    qsort(k, (size_t)(to - from), sizeof *k, key_cmp);
    return k;
}

/* Words found once in [a0, a1) and once in [b0, b1): pairs (ia, ib). */
static int unique_pairs(const Cmp *c, int a0, int a1, int b0, int b1, int **pa, int **pb)
{
    Key *ka = sorted_keys(c->a, a0, a1), *kb = sorted_keys(c->b, b0, b1);
    int na = a1 - a0, nb = b1 - b0, i = 0, j = 0, n = 0;
    *pa = xmalloc((size_t)(SDL_min(na, nb) + 1) * sizeof(int));
    *pb = xmalloc((size_t)(SDL_min(na, nb) + 1) * sizeof(int));
    while (i < na && j < nb) {
        uint64_t h = ka[i].hash;
        if (h < kb[j].hash) {
            i++;
            continue;
        }
        if (h > kb[j].hash) {
            j++;
            continue;
        }
        int ci = 1, cj = 1;
        while (i + ci < na && ka[i + ci].hash == h)
            ci++;
        while (j + cj < nb && kb[j + cj].hash == h)
            cj++;
        if (ci == 1 && cj == 1)
            (*pa)[n] = ka[i].index, (*pb)[n] = kb[j].index, n++;
        i += ci, j += cj;
    }
    free(ka);
    free(kb);
    return n;
}

typedef struct {
    int a, i;                /* old position, pair index */
} ByOld;

static int by_old(const void *x, const void *y) { return ((const ByOld *)x)->a - ((const ByOld *)y)->a; }

/* The longest run of pairs increasing in both: the anchors (patience). */
static int anchors(int *pa, int *pb, int n)
{
    ByOld *sorted = xmalloc((size_t)(n + 1) * sizeof *sorted); /* pairs by old position */
    for (int i = 0; i < n; i++)
        sorted[i] = (ByOld){pa[i], i};
    qsort(sorted, (size_t)n, sizeof *sorted, by_old);
    int *order = xmalloc((size_t)(n + 1) * sizeof(int));
    for (int i = 0; i < n; i++)
        order[i] = sorted[i].i;
    free(sorted);
    int *tail = xmalloc((size_t)(n + 1) * sizeof(int)), *prev = xmalloc((size_t)(n + 1) * sizeof(int)), len = 0;
    for (int k = 0; k < n; k++) {
        int x = order[k], lo = 0, hi = len;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (pb[tail[mid]] < pb[x])
                lo = mid + 1;
            else
                hi = mid;
        }
        prev[x] = lo ? tail[lo - 1] : -1;
        tail[lo] = x;
        if (lo == len)
            len++;
    }
    int *ra = xmalloc((size_t)(len + 1) * sizeof(int)), *rb = xmalloc((size_t)(len + 1) * sizeof(int));
    for (int k = len - 1, x = len ? tail[len - 1] : -1; k >= 0; k--, x = prev[x])
        ra[k] = pa[x], rb[k] = pb[x];
    memcpy(pa, ra, (size_t)len * sizeof(int));
    memcpy(pb, rb, (size_t)len * sizeof(int));
    free(ra), free(rb), free(order), free(tail), free(prev);
    return len;
}

/* Word by word (longest common subsequence), for small gaps. */
static void lcs(const Cmp *c, int a0, int a1, int b0, int b1)
{
    int n = a1 - a0, m = b1 - b0;
    uint16_t *len = xcalloc((size_t)(n + 1) * (m + 1), sizeof *len);
#define L(i, j) len[(size_t)(i) * (m + 1) + (j)]
    for (int i = n - 1; i >= 0; i--)
        for (int j = m - 1; j >= 0; j--)
            L(i, j) = c->a[a0 + i].hash == c->b[b0 + j].hash ? (uint16_t)(L(i + 1, j + 1) + 1)
                                                              : SDL_max(L(i + 1, j), L(i, j + 1));
    int i = 0, j = 0;
    while (i < n && j < m) {
        if (c->a[a0 + i].hash == c->b[b0 + j].hash)
            i++, j++;
        else if (L(i + 1, j) >= L(i, j + 1))
            c->gone[a0 + i++] = true;
        else
            c->added[b0 + j++] = true;
    }
#undef L
    for (; i < n; i++)
        c->gone[a0 + i] = true;
    for (; j < m; j++)
        c->added[b0 + j] = true;
    free(len);
}

static void compare(const Cmp *c, int a0, int a1, int b0, int b1, int depth)
{
    while (a0 < a1 && b0 < b1 && c->a[a0].hash == c->b[b0].hash)
        a0++, b0++;
    while (a0 < a1 && b0 < b1 && c->a[a1 - 1].hash == c->b[b1 - 1].hash)
        a1--, b1--;
    if (a0 == a1 || b0 == b1) {
        for (int i = a0; i < a1; i++)
            c->gone[i] = true;
        for (int j = b0; j < b1; j++)
            c->added[j] = true;
        return;
    }
    int *pa, *pb, n = depth < 200 ? unique_pairs(c, a0, a1, b0, b1, &pa, &pb) : 0;
    if (depth < 200)
        n = anchors(pa, pb, n);
    if (n == 0) {
        if (depth < 200)
            free(pa), free(pb);
        if ((long long)(a1 - a0) * (b1 - b0) <= DP_MAX && a1 - a0 < 65000 && b1 - b0 < 65000) {
            lcs(c, a0, a1, b0, b1);
        } else { /* nothing in common that is sure: all of it changed */
            for (int i = a0; i < a1; i++)
                c->gone[i] = true;
            for (int j = b0; j < b1; j++)
                c->added[j] = true;
        }
        return;
    }
    int pa0 = a0, pb0 = b0;
    for (int k = 0; k < n; k++) {
        compare(c, pa0, pa[k], pb0, pb[k], depth + 1);
        pa0 = pa[k] + 1, pb0 = pb[k] + 1;
    }
    compare(c, pa0, a1, pb0, b1, depth + 1);
    free(pa);
    free(pb);
}

/* The old words of a change, as text: "stiffness of 0.47". */
static char *join(const Words *a, int from, int to)
{
    size_t cap = 256, len = 0;
    char *s = xmalloc(cap);
    s[0] = 0;
    for (int i = from; i < to && len < 400; i++) {
        const char *t = a->text + a->w[i].text;
        bool punct = a->w[i].len == 1 && strchr(".,;:!?)]}", t[0]);
        size_t tl = (size_t)a->w[i].len + 1;
        if (len + tl + 2 > cap)
            s = xrealloc(s, cap = (len + tl + 2) * 2);
        if (len && !punct && !(len && strchr("([{", s[len - 1])))
            s[len++] = ' ';
        memcpy(s + len, t, (size_t)a->w[i].len);
        len += (size_t)a->w[i].len;
        s[len] = 0;
    }
    if (len >= 400)
        strcpy(s + 396, "…");
    return s;
}

/* ---------- the notes ---------- */
typedef struct {
    int page, subtype;
    PRect r;
    char nm[64];
    char *text;              /* its comment, malloc'd, "" for none */
    bool textbox, matched;
} NoteRec;

typedef struct {
    NoteRec *n;
    int count, cap;
} NoteRecs;

static bool diff_kind(int t)
{
    return t == FPDF_ANNOT_TEXT || t == FPDF_ANNOT_FREETEXT || t == FPDF_ANNOT_SQUARE || t == FPDF_ANNOT_CIRCLE ||
           t == FPDF_ANNOT_HIGHLIGHT || t == FPDF_ANNOT_UNDERLINE || t == FPDF_ANNOT_SQUIGGLY ||
           t == FPDF_ANNOT_STRIKEOUT || t == FPDF_ANNOT_STAMP || t == FPDF_ANNOT_INK;
}

static char *annot_string(FPDF_ANNOTATION a, const char *key)
{
    unsigned long bytes = FPDFAnnot_GetStringValue(a, key, NULL, 0);
    if (bytes <= 2)
        return xstrdup("");
    unsigned short *u = xmalloc(bytes);
    FPDFAnnot_GetStringValue(a, key, u, bytes);
    char *out = utf16_to_utf8(u, (int)(bytes / 2) - 1);
    free(u);
    return out;
}

/* The notes of a page that are shown (not deleted: undo hides them). */
static void page_notes(FPDF_PAGE p, int page, NoteRecs *out)
{
    for (int i = 0, n = FPDFPage_GetAnnotCount(p); i < n; i++) {
        FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, i);
        if (!a)
            continue;
        int type = FPDFAnnot_GetSubtype(a);
        FS_RECTF r;
        if (diff_kind(type) && !(FPDFAnnot_GetFlags(a) & FPDF_ANNOT_FLAG_HIDDEN) && FPDFAnnot_GetRect(a, &r)) {
            if (out->count == out->cap)
                out->n = xrealloc(out->n, (size_t)(out->cap = out->cap ? out->cap * 2 : 32) * sizeof *out->n);
            NoteRec *nr = &out->n[out->count++];
            *nr = (NoteRec){.page = page, .subtype = type,
                            .r = {SDL_min(r.left, r.right), SDL_max(r.top, r.bottom), SDL_max(r.left, r.right),
                                  SDL_min(r.top, r.bottom)}};
            char *nm = annot_string(a, "NM"), *box = annot_string(a, "NomadysText");
            snprintf(nr->nm, sizeof nr->nm, "%s", nm);
            nr->textbox = type == FPDF_ANNOT_STAMP && box[0];
            nr->text = annot_string(a, "Contents");
            free(nm);
            free(box);
        }
        FPDFPage_CloseAnnot(a);
    }
}

static void free_notes(NoteRecs *ns)
{
    for (int i = 0; i < ns->count; i++)
        free(ns->n[i].text);
    free(ns->n);
    *ns = (NoteRecs){0};
}

static bool same_place(PRect a, PRect b)
{
    return SDL_fabsf(a.l - b.l) < 1 && SDL_fabsf(a.r - b.r) < 1 && SDL_fabsf(a.t - b.t) < 1 && SDL_fabsf(a.b - b.b) < 1;
}

static void add_hunk(Diff *d, int *cap, Hunk h)
{
    if (d->nh == *cap)
        d->h = xrealloc(d->h, (size_t)(*cap = *cap ? *cap * 2 : 64) * sizeof *d->h);
    d->h[d->nh++] = h;
}

static int hunk_order(const void *x, const void *y)
{
    const Hunk *a = x, *b = y;
    if (a->page != b->page)
        return a->page - b->page;
    return a->y < b->y ? 1 : a->y > b->y ? -1 : 0; /* the top of the page first */
}

/* The text changes (the first ntext, in reading order, which two columns need) and the notes
 * (after them, by position) in one list by page; on a page the text comes first. */
static void merge_hunks(Diff *d, int ntext)
{
    int nnote = d->nh - ntext;
    if (!nnote)
        return;
    qsort(d->h + ntext, (size_t)nnote, sizeof *d->h, hunk_order);
    Hunk *out = xmalloc((size_t)d->nh * sizeof *out);
    int i = 0, j = ntext, k = 0;
    while (i < ntext || j < d->nh)
        out[k++] = j >= d->nh || (i < ntext && d->h[i].page <= d->h[j].page) ? d->h[i++] : d->h[j++];
    free(d->h);
    d->h = out;
}

/* The notes that are new, changed or gone, as changes. */
static void compare_notes(Diff *d, int *cap, const NoteRecs *a, NoteRecs *b)
{
    for (int i = 0; i < b->count; i++) { /* the same one in both: by name, then by kind and place */
        NoteRec *nb = &b->n[i];
        for (int pass = 0; pass < 2 && !nb->matched; pass++)
            for (int k = 0; k < a->count; k++) {
                NoteRec *na = &a->n[k];
                if (na->matched || na->page != nb->page || na->subtype != nb->subtype)
                    continue;
                bool hit = pass == 0 ? nb->nm[0] && !strcmp(na->nm, nb->nm)
                                     : (!nb->nm[0] || !na->nm[0]) && same_place(na->r, nb->r);
                if (!hit)
                    continue;
                na->matched = nb->matched = true;
                if (strcmp(na->text, nb->text) || (pass == 0 && !same_place(na->r, nb->r) && nb->textbox))
                    add_hunk(d, cap, (Hunk){.kind = H_NOTE_CHANGED, .page = nb->page, .y = nb->r.t, .r = nb->r,
                                            .subtype = nb->subtype, .textbox = nb->textbox, .was = xstrdup(na->text),
                                            .now = xstrdup(nb->text)});
                break;
            }
    }
    for (int i = 0; i < b->count; i++)
        if (!b->n[i].matched)
            add_hunk(d, cap, (Hunk){.kind = H_NOTE_NEW, .page = b->n[i].page, .y = b->n[i].r.t, .r = b->n[i].r,
                                    .subtype = b->n[i].subtype, .textbox = b->n[i].textbox, .was = xstrdup(""),
                                    .now = xstrdup(b->n[i].text)});
    for (int k = 0; k < a->count; k++)
        if (!a->n[k].matched)
            add_hunk(d, cap, (Hunk){.kind = H_NOTE_GONE, .page = a->n[k].page, .y = a->n[k].r.t, .r = a->n[k].r,
                                    .subtype = a->n[k].subtype, .textbox = a->n[k].textbox,
                                    .was = xstrdup(a->n[k].text), .now = xstrdup("")});
}

void diff_free(Diff *d)
{
    if (!d)
        return;
    for (int i = 0; i < d->nh; i++)
        free(d->h[i].was), free(d->h[i].now);
    free(d->h);
    free_words(&d->b);
    free(d->old_path);
    free(d);
}

/* Compares the view's document with old_path; NULL (and err) on failure. */
static Diff *make_diff(View *v, const char *old_path, char *err, size_t cap)
{
    FPDF_DOCUMENT old = FPDF_LoadDocument(old_path, NULL);
    if (!old) {
        snprintf(err, cap, "cannot open %s", old_path);
        return NULL;
    }
    Words a = {0}, b = {0};
    NoteRecs na = {0}, nb = {0};
    doc_words(old, &a);
    for (int page = 0, n = FPDF_GetPageCount(old); page < n; page++) {
        FPDF_PAGE p = FPDF_LoadPage(old, page);
        if (p) {
            page_notes(p, page, &na);
            FPDF_ClosePage(p);
        }
    }
    FPDF_CloseDocument(old);
    for (int page = 0; page < v->doc->npages; page++) { /* the document shown, through its cache */
        FPDF_PAGE p = doc_page(v->doc, page);
        FPDF_TEXTPAGE tp = doc_text(v->doc, page);
        if (p && tp)
            page_words(p, tp, page, &b);
        if (p)
            page_notes(p, page, &nb);
    }
    Cmp c = {a.w, b.w, xcalloc((size_t)a.n + 1, sizeof(bool)), xcalloc((size_t)b.n + 1, sizeof(bool))};
    compare(&c, 0, a.n, 0, b.n, 0);
    Diff *d = xcalloc(1, sizeof *d);
    d->old_path = xstrdup(old_path);
    d->cur = -1;
    int i = 0, j = 0, cap_h = 0;
    while (i < a.n || j < b.n) {
        if ((i < a.n && c.gone[i]) || (j < b.n && c.added[j])) {
            int i0 = i, j0 = j;
            while ((i < a.n && c.gone[i]) || (j < b.n && c.added[j])) {
                while (i < a.n && c.gone[i])
                    i++;
                while (j < b.n && c.added[j])
                    j++;
            }
            if (d->nh == cap_h)
                d->h = xrealloc(d->h, (size_t)(cap_h = cap_h ? cap_h * 2 : 64) * sizeof *d->h);
            const Word *at = j0 < b.n ? &b.w[j0] : b.n ? &b.w[b.n - 1] : NULL;
            float y = 0;
            if (at) {
                FPDF_TEXTPAGE tp = doc_text(v->doc, at->page);
                double l, r, bb, t;
                if (tp && FPDFText_GetCharBox(tp, at->start, &l, &r, &bb, &t))
                    y = (float)t;
            }
            d->h[d->nh++] = (Hunk){.b0 = j0, .b1 = j, .was = join(&a, i0, i), .page = at ? at->page : 0, .y = y,
                                   .now = xstrdup("")};
        } else {
            i++, j++;
        }
    }
    free(c.gone);
    free(c.added);
    free_words(&a);
    int ntext = d->nh;
    compare_notes(d, &cap_h, &na, &nb);
    free_notes(&na);
    free_notes(&nb);
    merge_hunks(d, ntext);
    d->b = b;
    memory_freed = true;
    return d;
}

void diff_start(Window *w, View *v, const char *old_path)
{
    char err[512];
    ui_message(w, "comparing…");
    Diff *d = make_diff(v, old_path, err, sizeof err);
    if (!d) {
        ui_message_long(w, 5000, "%s", err);
        return;
    }
    diff_free(v->diff);
    v->diff = d;
    w->redraw = true;
    const char *name = strrchr(old_path, '/') ? strrchr(old_path, '/') + 1 : old_path;
    if (d->nh)
        ui_message_long(w, 6000, "%d change%s since %s   ]c [c go through them   :nodiff hides them", d->nh,
                        d->nh == 1 ? "" : "s", name);
    else
        ui_message_long(w, 6000, "no change in the text or the notes since %s", name);
}

void diff_stop(Window *w, View *v)
{
    diff_free(v->diff);
    v->diff = NULL;
    w->redraw = true;
    ui_message(w, "comparison off");
}

/* The document was reloaded (LaTeX recompiled it): compare again. */
void diff_refresh(View *v)
{
    if (!v->diff)
        return;
    char err[512], *path = xstrdup(v->diff->old_path);
    Diff *d = make_diff(v, path, err, sizeof err);
    free(path);
    diff_free(v->diff);
    v->diff = d;
}

/* ]c / [c: the next or previous change from where the view is. */
void diff_step(Window *w, View *v, int dir)
{
    Diff *d = v->diff;
    if (!d || !d->nh) {
        ui_message(w, d ? "no change" : "no comparison: :diff old.pdf");
        return;
    }
    Pos here = view_pos(v);
    int pick = -1;
    if (d->cur >= 0 && here.page == d->cur_pos.page && SDL_fabsf(here.y - d->cur_pos.y) < 0.002f) {
        pick = d->cur + dir; /* not moved since the last one: simply the next */
        if (pick < 0 || pick >= d->nh)
            pick = -1;
    } else { /* from where the reader is: changes are shown 40 pt below the top */
        float ref = v->doc->ph[here.page] * (1 - here.y) - 40;
        for (int k = 0; k < d->nh; k++) {
            int i = dir > 0 ? k : d->nh - 1 - k;
            const Hunk *h = &d->h[i];
            bool after = h->page > here.page || (h->page == here.page && h->y < ref - 1);
            bool before = h->page < here.page || (h->page == here.page && h->y > ref + 1);
            if (dir > 0 ? after : before) {
                pick = i;
                break;
            }
        }
    }
    if (pick < 0) {
        ui_message(w, dir > 0 ? "no more changes below" : "no more changes above");
        return;
    }
    const Hunk *h = &d->h[pick];
    float ph = v->doc->ph[h->page];
    jump_to(w, v, (Pos){h->page, SDL_clamp(1 - (h->y + 40) / ph, 0.0f, 1.0f)});
    d->cur = pick;
    d->cur_pos = view_pos(v);
    if (h->kind != H_TEXT) {
        const char *what = h->textbox ? "text box" : h->subtype == FPDF_ANNOT_STAMP ? "picture"
                         : h->subtype == FPDF_ANNOT_TEXT ? "comment" : h->subtype == FPDF_ANNOT_INK ? "drawing"
                         : h->subtype == FPDF_ANNOT_SQUARE || h->subtype == FPDF_ANNOT_CIRCLE ? "frame" : "highlight";
        const char *text = h->kind == H_NOTE_GONE ? h->was : h->now;
        char brief[100];
        snprintf(brief, sizeof brief, "%.90s%s", text, strlen(text) > 90 ? "…" : "");
        for (char *c = brief; *c; c++)
            if (*c == '\n' || *c == '\r')
                *c = ' ';
        if (h->kind == H_NOTE_NEW)
            ui_message_long(w, 8000, "change %d of %d: new %s%s%s%s", pick + 1, d->nh, what, brief[0] ? " “" : "", brief,
                            brief[0] ? "”" : "");
        else if (h->kind == H_NOTE_GONE)
            ui_message_long(w, 8000, "change %d of %d: %s removed%s%s%s", pick + 1, d->nh, what, brief[0] ? " “" : "",
                            brief, brief[0] ? "”" : "");
        else {
            char was[100];
            snprintf(was, sizeof was, "%.90s%s", h->was, strlen(h->was) > 90 ? "…" : "");
            for (char *c = was; *c; c++)
                if (*c == '\n' || *c == '\r')
                    *c = ' ';
            ui_message_long(w, 8000, "change %d of %d: %s changed, was “%s”, now “%s”", pick + 1, d->nh, what, was, brief);
        }
    } else if (h->b0 == h->b1)
        ui_message_long(w, 8000, "change %d of %d: deleted “%s”", pick + 1, d->nh, h->was);
    else if (h->was[0])
        ui_message_long(w, 8000, "change %d of %d: changed, was “%s”", pick + 1, d->nh, h->was);
    else
        ui_message_long(w, 8000, "change %d of %d: added", pick + 1, d->nh);
}

/* Green over new text, a red mark where text went. */
void diff_draw(Window *w, View *v, int top)
{
    Diff *d = v->diff;
    if (!d)
        return;
    int first = -1, last = -1;
    for (int page = 0; page < v->doc->npages; page++) {
        double sy = v->py[page] - v->sy + top;
        if (sy >= w->H)
            break;
        if (sy + v->phpx[page] > top) {
            if (first < 0)
                first = page;
            last = page;
        }
    }
    if (first < 0)
        return;
    for (int k = 0; k < d->nh; k++) {
        const Hunk *h = &d->h[k];
        if (h->kind != H_TEXT) { /* a note: a frame round it, new green, changed amber, gone red */
            if (h->page < first || h->page > last)
                continue;
            float x0, y0, x1, y1;
            page_to_screen(v, h->page, h->r.l, h->r.t, &x0, &y0, top);
            page_to_screen(v, h->page, h->r.r, h->r.b, &x1, &y1, top);
            uint32_t c = h->kind == H_NOTE_NEW ? 0x43A047 : h->kind == H_NOTE_CHANGED ? 0xF9A825 : 0xE53935;
            float fx = SDL_min(x0, x1) - 2 * w->density, fy = SDL_min(y0, y1) - 2 * w->density;
            float fw = SDL_fabsf(x1 - x0) + 4 * w->density, fh = SDL_fabsf(y1 - y0) + 4 * w->density;
            ui_fill(w, fx, fy, fw, fh, c, h->kind == H_NOTE_GONE ? 40 : 55);
            ui_frame(w, fx, fy, fw, fh, SDL_max(2.0f, 2 * w->density), c);
            continue;
        }
        if (h->b0 == h->b1) { /* a deletion: a red bar before the next word */
            if (h->b0 >= d->b.n)
                continue;
            const Word *wd = &d->b.w[h->b0];
            if (wd->page < first || wd->page > last)
                continue;
            FPDF_TEXTPAGE tp = doc_text(v->doc, wd->page);
            double l, r, b, t;
            if (!tp || !FPDFText_GetCharBox(tp, wd->start, &l, &r, &b, &t))
                continue;
            float x0, y0, x1, y1;
            page_to_screen(v, wd->page, (float)l - 1.5f, (float)t + 2, &x0, &y0, top);
            page_to_screen(v, wd->page, (float)l - 0.5f, (float)b - 2, &x1, &y1, top);
            ui_fill(w, SDL_min(x0, x1) - w->density, SDL_min(y0, y1), SDL_max(3 * w->density, SDL_fabsf(x1 - x0)),
                    SDL_fabsf(y1 - y0), 0xE53935, 230);
            continue;
        }
        if (d->b.w[h->b0].page > last || d->b.w[h->b1 - 1].page < first)
            continue;
        for (int i = h->b0; i < h->b1; i++) {
            const Word *wd = &d->b.w[i];
            if (wd->page < first || wd->page > last)
                continue;
            FPDF_TEXTPAGE tp = doc_text(v->doc, wd->page);
            int nr = tp ? FPDFText_CountRects(tp, wd->start, wd->end - wd->start) : 0;
            for (int q = 0; q < nr; q++) {
                double l, t, r, b;
                if (!FPDFText_GetRect(tp, q, &l, &t, &r, &b))
                    continue;
                float x0, y0, x1, y1;
                page_to_screen(v, wd->page, (float)l, (float)t, &x0, &y0, top);
                page_to_screen(v, wd->page, (float)r, (float)b, &x1, &y1, top);
                ui_fill(w, SDL_min(x0, x1), SDL_min(y0, y1), SDL_fabsf(x1 - x0), SDL_fabsf(y1 - y0),
                        h->was[0] ? 0xF9A825 : 0x43A047, 85); /* changed: amber, added: green */
            }
        }
    }
}
