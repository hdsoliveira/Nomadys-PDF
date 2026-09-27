/* ":export [file.md]": highlights, comments and marks as Markdown, in
 * reading order, with the full highlighted text. Also "--export file.pdf"
 * on the command line, which prints it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "fpdf_annot.h"
#include "nomadys.h"

#define EXPORT_TAG "<!-- nomadys-pdf export -->" /* files we may overwrite */

static const char *color_names[5] = {"yellow", "red", "green", "pink", "blue"}; /* C V B N M */

/* Which configured highlight colour c is (0-4, the keys C V B N M), or 5. */
int note_color_index(uint32_t c)
{
    int best = -1, best_d = 1 << 30;
    for (int i = 0; i < 5; i++) {
        uint32_t h = cfg.highlights[i];
        int dr = (int)((c >> 16) & 0xFF) - (int)((h >> 16) & 0xFF);
        int dg = (int)((c >> 8) & 0xFF) - (int)((h >> 8) & 0xFF);
        int db = (int)(c & 0xFF) - (int)(h & 0xFF);
        int dist = dr * dr + dg * dg + db * db;
        if (dist < best_d)
            best_d = dist, best = i;
    }
    return best >= 0 && best_d < 90 * 90 * 3 ? best : 5;
}

/* ---------- which notes: by colour, with a comment ---------- */
bool note_filter_word(const char *word, NoteFilter *f)
{
    for (int i = 0; i < 5; i++)
        if (!strcasecmp(word, color_names[i])) {
            f->colors |= 1u << i;
            return true;
        }
    if (!strcasecmp(word, "other") || !strcasecmp(word, "others")) {
        f->colors |= 1u << 5;
        return true;
    }
    if (!strcasecmp(word, "commented") || !strcasecmp(word, "comments") || !strcasecmp(word, "comment")) {
        f->commented = true;
        return true;
    }
    return false;
}

bool note_filter_pass(const NoteFilter *f, int subtype, uint32_t color, bool has_comment)
{
    if (!f)
        return true;
    if (f->commented && !has_comment)
        return false;
    if (!f->colors)
        return true;
    if (subtype == FPDF_ANNOT_STAMP) /* an image has no colour */
        return false;
    return f->colors & (1u << note_color_index(color));
}

/* "red, green, with comments", or "" for everything. */
void note_filter_describe(const NoteFilter *f, char *out, size_t cap)
{
    out[0] = 0;
    for (int i = 0; f && i < 6; i++)
        if (f->colors & (1u << i))
            snprintf(out + strlen(out), cap - strlen(out), "%s%s", out[0] ? ", " : "", i < 5 ? color_names[i] : "other colours");
    if (f && f->commented)
        snprintf(out + strlen(out), cap - strlen(out), "%s", out[0] ? ", with comments" : "with comments");
}

/* The name of the configured highlight colour closest to c, or its hex code. */
static const char *color_name(uint32_t c, char *buf, size_t cap)
{
    int best = -1, best_d = 1 << 30;
    for (int i = 0; i < 5; i++) {
        uint32_t h = cfg.highlights[i];
        int dr = (int)((c >> 16) & 0xFF) - (int)((h >> 16) & 0xFF);
        int dg = (int)((c >> 8) & 0xFF) - (int)((h >> 8) & 0xFF);
        int db = (int)(c & 0xFF) - (int)(h & 0xFF);
        int dist = dr * dr + dg * dg + db * db;
        if (dist < best_d)
            best_d = dist, best = i;
    }
    if (best >= 0 && best_d < 90 * 90 * 3)
        return color_names[best];
    snprintf(buf, cap, "#%06x", c & 0xFFFFFF);
    return buf;
}

/* ---------- text under a highlight ---------- */
typedef struct {
    char *s;
    size_t len, cap;
} Buf;

static void put(Buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->s = xrealloc(b->s, b->cap);
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}

/* A region marked with the free cursor (a figure, an equation), not a run
 * of text: a frame, or a highlight box taller than two lines. */
static bool is_area(const Note *n)
{
    if (n->subtype == FPDF_ANNOT_SQUARE || n->subtype == FPDF_ANNOT_CIRCLE || n->subtype == FPDF_ANNOT_INK ||
        n->subtype == FPDF_ANNOT_TEXT)
        return true;
    for (int q = 0; q < n->nquads; q++)
        if (n->quads[q].t - n->quads[q].b > 24)
            return true;
    return false;
}

/* Joins the text of each highlighted line: spaces collapsed, and a word
 * split by a hyphen at the end of a line ("stiff-" + "ness") joined again. */
static char *note_text(Doc *d, int page, const Note *n)
{
    if (is_area(n)) /* its text would be cut mid-word */
        return NULL;
    FPDF_TEXTPAGE tp = doc_text(d, page);
    Buf b = {0};
    for (int q = 0; tp && q < n->nquads; q++) {
        PRect r = n->quads[q];
        int got = FPDFText_GetBoundedText(tp, r.l, r.t, r.r, r.b, NULL, 0);
        if (got <= 0)
            continue;
        unsigned short *u = xmalloc(((size_t)got + 1) * sizeof *u);
        got = FPDFText_GetBoundedText(tp, r.l, r.t, r.r, r.b, u, got);
        char *t = utf16_to_utf8(u, got);
        free(u);
        for (char *c = t; *c; c++)
            if (*c == '\r' || *c == '\n' || *c == '\t')
                *c = ' ';
        char *start = t + strspn(t, " ");
        size_t tl = strlen(start);
        while (tl && start[tl - 1] == ' ')
            tl--;
        if (tl) {
            /* "configuration-" / "dependent" at a line end: a real hyphen, no space */
            if (b.len && b.s[b.len - 1] != '-')
                put(&b, " ", 1);
            put(&b, start, tl);
        }
        free(t);
    }
    if (!b.s)
        return NULL;
    text_join_hyphens(b.s); /* "lat\x02 tice" -> "lattice" */
    char *o = b.s; /* collapse runs of spaces */
    for (char *i = b.s; *i; i++)
        if (*i != ' ' || o == b.s || o[-1] != ' ')
            *o++ = *i;
    *o = 0;
    return b.s;
}

/* ---------- reading order ---------- */
typedef struct {
    int note;
    int key;                 /* index of the nearest character: text order */
    float top;
} Order;

static int order_cmp(const void *a, const void *b)
{
    const Order *x = a, *y = b;
    if (x->key != y->key)
        return x->key < y->key ? -1 : 1;
    return x->top > y->top ? -1 : x->top < y->top;
}

/* Sorts a page's notes the way the text reads (columns included): by the
 * character closest to where each note starts. */
static Order *page_order(Doc *d, int page)
{
    PageSlot *s = &d->slots[page];
    doc_page_chars(d, page);
    Order *o = xcalloc((size_t)s->nnotes, sizeof *o);
    for (int i = 0; i < s->nnotes; i++) {
        const Note *n = &s->notes[i];
        PRect first = n->nquads ? n->quads[0] : n->r;
        float x = first.l, y = (first.t + first.b) / 2;
        if (is_area(n))
            y = first.t; /* where it begins */
        int best = s->nchars;
        float best_d = 1e30f;
        for (int k = 0; k < s->nchars; k++) {
            if (s->line[k] < 0)
                continue;
            PRect c = s->boxes[k];
            float dx = x < c.l ? c.l - x : x > c.r ? x - c.r : 0;
            float dy = y > c.t ? y - c.t : y < c.b ? c.b - y : 0;
            float dist = dx * dx + dy * dy * 4; /* lines matter more than columns */
            if (dist < best_d)
                best_d = dist, best = k;
        }
        o[i] = (Order){i, best, first.t};
    }
    qsort(o, (size_t)s->nnotes, sizeof *o, order_cmp);
    return o;
}

/* ---------- writing ---------- */
static void write_indented(FILE *f, const char *s, const char *indent)
{
    for (const char *p = s; *p;) {
        size_t n = strcspn(p, "\n");
        fprintf(f, "%s%.*s\n", p == s ? "" : indent, (int)n, p);
        p += n + (p[n] == '\n');
    }
}

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* Writes the Markdown; returns the number of highlights. */
int export_write(Doc *d, FILE *f, int *ncomments, const NoteFilter *only)
{
    int total = 0, comments = 0;
    bool *pass = NULL; /* per note of the page being written */
    for (int page = 0; page < d->npages; page++) {
        notes_page(d, page);
        PageSlot *s = &d->slots[page];
        for (int i = 0; i < s->nnotes; i++)
            total += note_filter_pass(only, s->notes[i].subtype, s->notes[i].color, s->notes[i].has_comment);
    }
    char which[160];
    note_filter_describe(only, which, sizeof which);
    char when[64];
    time_t now = time(NULL);
    strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime(&now));
    fprintf(f, EXPORT_TAG "\n# %s — highlights\n\n", base_name(d->path));
    fprintf(f, "Exported by Nomadys-PDF on %s from `%s`.\n\n", when, d->path);
    if (which[0])
        fprintf(f, "Only the highlights that are: %s.\n\n", which);
    for (int page = 0; page < d->npages; page++) {
        PageSlot *s = &d->slots[page];
        int shown = 0;
        pass = xrealloc(pass, (size_t)(s->nnotes ? s->nnotes : 1) * sizeof *pass);
        for (int i = 0; i < s->nnotes; i++)
            shown += pass[i] = note_filter_pass(only, s->notes[i].subtype, s->notes[i].color, s->notes[i].has_comment);
        if (!shown)
            continue;
        fprintf(f, "## Page %d\n\n", page + 1);
        Order *o = page_order(d, page);
        for (int i = 0; i < s->nnotes; i++) {
            if (!pass[o[i].note])
                continue;
            const Note *n = &s->notes[o[i].note];
            char hex[16];
            const char *color = color_name(n->color, hex, sizeof hex);
            char *text = note_text(d, page, n), *comment = note_comment(d, page, o[i].note);
            if (n->subtype == FPDF_ANNOT_STAMP)
                fprintf(f, "- *(image)* placed on the page\n\n");
            else if (n->subtype == FPDF_ANNOT_TEXT)
                fprintf(f, "- *(note)*\n\n");
            else if (n->subtype == FPDF_ANNOT_INK)
                fprintf(f, "- *(%s, drawing)* drawn by hand\n\n", color);
            else if (text)
                fprintf(f, "- *(%s)* “%s”\n\n", color, text); /* blank lines: easier to read */
            else
                fprintf(f, "- *(%s, area)* highlighted region — figure, table or equation\n\n", color);
            if (comment && *comment) {
                fprintf(f, "  **Comment:** ");
                write_indented(f, comment, "  ");
                fprintf(f, "\n");
                comments++;
            }
            free(text);
            free(comment);
        }
        free(o);
    }
    int nmarks = 0;
    for (int m = 0; m < NMARKS; m++)
        nmarks += d->marks[m].set;
    if (nmarks) {
        fprintf(f, "## Marks\n\n");
        for (int m = 0; m < NMARKS; m++)
            if (d->marks[m].set) {
                char name[sizeof d->marks[m].name], *o = name;
                for (const char *c = d->marks[m].name; *c; c++) /* one line, single spaces */
                    if (!((*c == ' ' || *c == '\n' || *c == '\t') && (o == name || o[-1] == ' ')))
                        *o++ = *c == '\n' || *c == '\t' ? ' ' : *c;
                while (o > name && o[-1] == ' ')
                    o--;
                *o = 0;
                fprintf(f, "- `%c` %s — page %d\n", 'a' + m, name[0] ? name : "(no name)", d->marks[m].pos.page + 1);
            }
    }
    free(pass);
    if (!total && !nmarks)
        fprintf(f, which[0] ? "No highlights of that kind.\n" : "No highlights, comments or marks yet.\n");
    if (ncomments)
        *ncomments = comments;
    return total;
}

/* "paper.pdf" -> "paper-notes.md"; a relative name goes next to the PDF. */
static char *target_path(Doc *d, const char *arg)
{
    const char *home = getenv("HOME");
    const char *slash = strrchr(d->path, '/');
    int dirlen = slash ? (int)(slash - d->path) : 0;
    char *out = xmalloc(strlen(d->path) + strlen(arg) + (home ? strlen(home) : 0) + 16);
    if (!*arg) {
        size_t n = strlen(d->path);
        if (n > 4 && !strcasecmp(d->path + n - 4, ".pdf"))
            n -= 4;
        sprintf(out, "%.*s-notes.md", (int)n, d->path);
    } else if (arg[0] == '~' && home) {
        sprintf(out, "%s%s", home, arg + 1);
    } else if (arg[0] == '/') {
        strcpy(out, arg);
    } else {
        sprintf(out, "%.*s/%s", dirlen, slash ? d->path : ".", arg);
    }
    return out;
}

/* True if the file is missing or was written by an earlier export. */
static bool ours(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return true;
    char line[64] = "";
    bool yes = fgets(line, sizeof line, f) && !strncmp(line, EXPORT_TAG, strlen(EXPORT_TAG));
    fclose(f);
    return yes;
}

/* :export [file.md] [colours] [commented]: the words that name colours or
 * "commented" choose the notes, the other one is the file. */
void export_notes(Window *w, View *v, const char *arg, bool force, const NoteFilter *base)
{
    NoteFilter only = base ? *base : (NoteFilter){0};
    char file[1024] = "", word[1024];
    for (const char *p = arg; *p;) {
        p += strspn(p, " ");
        size_t n = strcspn(p, " ");
        if (!n)
            break;
        snprintf(word, sizeof word, "%.*s", (int)SDL_min(n, sizeof word - 1), p);
        p += n;
        if (!note_filter_word(word, &only) && !file[0])
            snprintf(file, sizeof file, "%s", word);
    }
    char *path = target_path(v->doc, file);
    if (!force && !ours(path)) {
        ui_message_long(w, 6000, "%s exists and was not made by :export — use :export! to overwrite", base_name(path));
        free(path);
        return;
    }
    char *tmp = xmalloc(strlen(path) + 16);
    sprintf(tmp, "%s.tmp%d", path, (int)getpid());
    FILE *f = fopen(tmp, "w");
    if (!f) {
        ui_message_long(w, 6000, "cannot write %s", path);
        free(tmp);
        free(path);
        return;
    }
    int comments = 0, n = export_write(v->doc, f, &comments, &only);
    bool ok = !ferror(f);
    ok &= fclose(f) == 0;
    if (!ok || rename(tmp, path)) {
        unlink(tmp);
        ui_message_long(w, 6000, "cannot write %s", path);
    } else {
        char which[160];
        note_filter_describe(&only, which, sizeof which);
        ui_message_long(w, 5000, "%d highlight%s, %d comment%s%s%s → %s", n, n == 1 ? "" : "s", comments,
                        comments == 1 ? "" : "s", which[0] ? " — only " : "", which, base_name(path));
    }
    free(tmp);
    free(path);
}
