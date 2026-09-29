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
    if (!strcasecmp(word, "tasks") || !strcasecmp(word, "todo") || !strcasecmp(word, "task")) {
        f->open_tasks = true;
        return true;
    }
    return false;
}

bool note_filter_pass(const NoteFilter *f, const Note *n)
{
    if (!f)
        return true;
    if (f->commented && !n->has_comment)
        return false;
    if (f->open_tasks && n->task != TASK_OPEN)
        return false;
    if (!f->colors)
        return true;
    if (n->subtype == FPDF_ANNOT_STAMP) /* an image has no colour */
        return false;
    return f->colors & (1u << note_color_index(n->color));
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
    if (f && f->open_tasks)
        snprintf(out + strlen(out), cap - strlen(out), "%s", out[0] ? ", tasks to do" : "tasks to do");
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
            total += note_filter_pass(only, &s->notes[i]);
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
            shown += pass[i] = note_filter_pass(only, &s->notes[i]);
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
            /* a task is a check box (Obsidian, GitHub): - [ ] to do, - [x] done */
            const char *item = n->task == TASK_DONE ? "- [x]" : n->task == TASK_OPEN ? "- [ ]" : "-";
            if (n->subtype == FPDF_ANNOT_STAMP)
                fprintf(f, "%s *(image)* placed on the page\n\n", item);
            else if (n->subtype == FPDF_ANNOT_TEXT)
                fprintf(f, "%s *(note)*\n\n", item);
            else if (n->subtype == FPDF_ANNOT_INK)
                fprintf(f, "%s *(%s, drawing)* drawn by hand\n\n", item, color);
            else if (text)
                fprintf(f, "%s *(%s)* “%s”\n\n", item, color, text); /* blank lines: easier to read */
            else
                fprintf(f, "%s *(%s, area)* highlighted region — figure, table or equation\n\n", item, color);
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
static char *target_path(Doc *d, const char *arg, const char *suffix)
{
    const char *home = getenv("HOME");
    const char *slash = strrchr(d->path, '/');
    int dirlen = slash ? (int)(slash - d->path) : 0;
    char *out = xmalloc(strlen(d->path) + strlen(arg) + (home ? strlen(home) : 0) + 16);
    if (!*arg) {
        size_t n = strlen(d->path);
        if (n > 4 && !strcasecmp(d->path + n - 4, ".pdf"))
            n -= 4;
        out = xrealloc(out, n + strlen(suffix) + 1);
        sprintf(out, "%.*s%s", (int)n, d->path, suffix);
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
static bool ours(const char *path, const char *tag)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return true;
    char line[64] = "";
    bool yes = fgets(line, sizeof line, f) && !strncmp(line, tag, strlen(tag));
    fclose(f);
    return yes;
}

/* :export [file.md] [colours] [commented] [tasks]: the words that name
 * colours, "commented" or "tasks" (to do) choose the notes, the other one
 * is the file. */
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
    char *path = target_path(v->doc, file, "-notes.md");
    if (!force && !ours(path, EXPORT_TAG)) {
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

/* ---------- :response — the answer to the reviewers ---------- */
#define RESPONSE_TAG "<!-- nomadys-pdf response -->"

/* A reviewer's point: a note someone else wrote (not a picture placed). */
static bool is_point(Doc *d, int page, int i, char *author, size_t cap)
{
    const Note *n = &d->slots[page].notes[i];
    if (n->subtype == FPDF_ANNOT_STAMP)
        return false;
    note_author(d, page, i, author, cap);
    return strcmp(author, note_user()) != 0;
}

/* Writes the letter, grouped by reviewer (the notes' author), each point
 * with its page, what it marks, the comment and our reply; returns the
 * number of points, *answered those with a reply of ours. */
int response_write(Doc *d, FILE *f, int *answered)
{
    char (*who)[128] = NULL; /* the reviewers, in the order they first appear */
    int nwho = 0, total = 0, done = 0;
    for (int page = 0; page < d->npages; page++) {
        notes_page(d, page);
        for (int i = 0; i < d->slots[page].nnotes; i++) {
            char a[128];
            if (!is_point(d, page, i, a, sizeof a))
                continue;
            total++;
            int k = 0;
            while (k < nwho && strcmp(who[k], a))
                k++;
            if (k == nwho) {
                who = xrealloc(who, (size_t)(nwho + 1) * sizeof *who);
                snprintf(who[nwho++], sizeof *who, "%s", a);
            }
        }
    }
    char when[64];
    time_t now = time(NULL);
    strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime(&now));
    fprintf(f, RESPONSE_TAG "\n# Response to the reviewers — %s\n\n", base_name(d->path));
    fprintf(f, "Written by Nomadys-PDF on %s from `%s`.\n\n", when, d->path);
    if (!total)
        fprintf(f, "No comments from anyone else in this PDF: the reviewers' notes come in with the PDF they "
                   "annotated, or with `:merge`.\n");
    Buf todo = {0};
    for (int k = 0; k < nwho; k++) {
        const char *name = who[k][0] ? who[k] : "(no name)";
        fprintf(f, "## %s\n\n", nwho == 1 && !who[k][0] ? "Reviewer" : name);
        int num = 0;
        for (int page = 0; page < d->npages; page++) {
            notes_page(d, page);
            PageSlot *s = &d->slots[page];
            if (!s->nnotes)
                continue;
            Order *o = page_order(d, page);
            for (int j = 0; j < s->nnotes; j++) {
                int i = o[j].note;
                char a[128];
                if (!is_point(d, page, i, a, sizeof a) || strcmp(a, who[k]))
                    continue;
                const Note *n = &s->notes[i];
                fprintf(f, "### %d. Page %d\n\n", ++num, page + 1);
                char *text = note_text(d, page, n), *comment = note_comment(d, page, i);
                if (text)
                    fprintf(f, "> “%s”\n\n", text);
                else if (n->subtype != FPDF_ANNOT_TEXT)
                    fprintf(f, "> *(a figure, table or equation marked on the page)*\n\n");
                if (comment && *comment) {
                    fprintf(f, "**Comment:** ");
                    write_indented(f, comment, "");
                    fprintf(f, "\n");
                }
                NoteReply *r;
                int nr = note_replies(d, page, i, &r);
                bool mine = false;
                for (int q = 0; q < nr; q++) {
                    if (!*r[q].text)
                        continue;
                    if (r[q].mine) {
                        fprintf(f, "**Response:** ");
                        mine = true;
                    } else {
                        fprintf(f, "**%s:** ", r[q].author[0] ? r[q].author : "Reply");
                    }
                    write_indented(f, r[q].text, "");
                    fprintf(f, "\n");
                }
                note_replies_free(r, nr);
                if (mine) {
                    done++;
                } else {
                    fprintf(f, "**Response:** *not written yet*\n\n");
                    char item[200];
                    int il = snprintf(item, sizeof item, "%s%s %d (page %d)", todo.len ? ", " : "", name, num, page + 1);
                    put(&todo, item, (size_t)SDL_min(il, (int)sizeof item - 1));
                }
                free(text);
                free(comment);
            }
            free(o);
        }
    }
    if (total) {
        fprintf(f, "---\n\n%d of %d point%s answered.", done, total, total == 1 ? "" : "s");
        if (todo.len)
            fprintf(f, " Still to answer: %s.", todo.s);
        fprintf(f, "\n");
    }
    free(todo.s);
    free(who);
    if (answered)
        *answered = done;
    return total;
}

/* :response [file.md] */
void response_letter(Window *w, View *v, const char *arg, bool force)
{
    while (*arg == ' ')
        arg++;
    char *path = target_path(v->doc, arg, "-response.md");
    if (!force && !ours(path, RESPONSE_TAG)) {
        ui_message_long(w, 6000, "%s exists and was not made by :response — use :response! to overwrite", base_name(path));
        free(path);
        return;
    }
    char *tmp = xmalloc(strlen(path) + 16);
    sprintf(tmp, "%s.tmp%d", path, (int)getpid());
    FILE *f = fopen(tmp, "w");
    int answered = 0, n = f ? response_write(v->doc, f, &answered) : 0;
    bool ok = f && !ferror(f);
    ok = f && fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path)) {
        unlink(tmp);
        ui_message_long(w, 6000, "cannot write %s", path);
    } else if (!n) {
        ui_message_long(w, 5000, "no reviewer comments in this PDF → %s", base_name(path));
    } else {
        ui_message_long(w, 5000, "%d of %d point%s answered → %s", answered, n, n == 1 ? "" : "s", base_name(path));
    }
    free(tmp);
    free(path);
}
