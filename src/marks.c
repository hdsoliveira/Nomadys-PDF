/* Marks (a-z), the jump list and the last reading position.
 * Stored outside the PDF in $XDG_DATA_HOME/nomadys-pdf/marks/<key>, where the
 * key hashes the PDF's permanent file identifier (or its path). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fpdf_doc.h"
#include "nomadys.h"

static uint64_t fnv1a(const void *data, size_t n, uint64_t h)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

static void mkdirs(char *path)
{
    for (char *p = path + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(path, 0700);
            *p = '/';
        }
    }
    mkdir(path, 0700);
}

static char *marks_path_for(uint64_t h)
{
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    char dir[1024];
    if (xdg && *xdg)
        snprintf(dir, sizeof dir, "%s/nomadys-pdf/marks", xdg);
    else
        snprintf(dir, sizeof dir, "%s/.local/share/nomadys-pdf/marks", home ? home : "/tmp");
    char *path = xmalloc(strlen(dir) + 24);
    sprintf(path, "%s/%016llx", dir, (unsigned long long)h);
    return path;
}

/* Keyed by the file's path: the PDF's own /ID changes whenever LaTeX
 * recompiles it (and on some saves), which would lose the marks. */
static char *marks_file(Doc *d)
{
    return marks_path_for(fnv1a(d->path, strlen(d->path), 1469598103934665603ull));
}

/* Where version 0.2 kept them (the /ID key), to carry old marks over. */
static char *old_marks_file(Doc *d)
{
    char id[256];
    unsigned long n = FPDF_GetFileIdentifier(d->pdf, FILEIDTYPE_PERMANENT, id, sizeof id);
    if (n <= 1 || n > sizeof id)
        return NULL;
    return marks_path_for(fnv1a(id, n, fnv1a(&d->npages, sizeof d->npages, 1469598103934665603ull)));
}

void marks_load(Doc *d)
{
    d->marks_path = marks_file(d);
    if (access(d->marks_path, F_OK) != 0) {
        char *old = old_marks_file(d);
        if (old && access(old, F_OK) == 0)
            rename(old, d->marks_path);
        free(old);
    }
    FILE *f = fopen(d->marks_path, "r");
    if (!f)
        return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        char c;
        int page, off;
        float y;
        if (sscanf(line, "last %d %f", &page, &y) == 2 && page >= 0 && page < d->npages) {
            d->last = (Pos){page, y};
        } else if (sscanf(line, "mark %c %d %f %n", &c, &page, &y, &off) == 3 && c >= 'a' && c <= 'z' &&
                   page >= 0 && page < d->npages) {
            Mark *m = &d->marks[c - 'a'];
            m->set = true;
            m->pos = (Pos){page, y};
            snprintf(m->name, sizeof m->name, "%s", line + off);
        }
    }
    fclose(f);
}

void marks_save(Doc *d)
{
    bool any = d->last.page >= 0;
    for (int i = 0; i < NMARKS; i++)
        any |= d->marks[i].set;
    if (!any || !d->marks_path)
        return;
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", d->marks_path);
    *strrchr(dir, '/') = 0;
    mkdirs(dir);
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", d->marks_path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    fprintf(f, "# nomadys-pdf marks for %s\n", d->path);
    if (d->last.page >= 0)
        fprintf(f, "last %d %.5f\n", d->last.page, d->last.y);
    for (int i = 0; i < NMARKS; i++)
        if (d->marks[i].set)
            fprintf(f, "mark %c %d %.5f %s\n", 'a' + i, d->marks[i].pos.page, d->marks[i].pos.y, d->marks[i].name);
    if (fclose(f) == 0)
        rename(tmp, d->marks_path);
}

/* The first line of text at or below a position, as a default mark name. */
static void snippet(Doc *d, Pos p, char *out, size_t cap)
{
    snprintf(out, cap, "page %d", p.page + 1);
    doc_page_chars(d, p.page);
    PageSlot *s = &d->slots[p.page];
    FPDF_TEXTPAGE tp = doc_text(d, p.page);
    if (!tp || !s->chars_ready)
        return;
    float ytop = d->ph[p.page] * (1 - SDL_max(0.0f, p.y)); /* PDF y grows upwards */
    for (int i = 0; i < s->nchars; i++) {
        if (s->line[i] < 0 || s->boxes[i].t > ytop + 2)
            continue;
        int end = i;
        while (end + 1 < s->nchars && (s->line[end + 1] == s->line[i] || s->line[end + 1] < 0) && end - i < 80)
            end++;
        unsigned short u[96];
        int n = FPDFText_GetText(tp, i, end - i + 1, u);
        char *t = utf16_to_utf8(u, n > 0 ? n - 1 : 0);
        for (char *c = t; *c; c++)
            if (*c == '\r' || *c == '\n')
                *c = ' ';
        snprintf(out, cap, "%s", t);
        free(t);
        return;
    }
}

void mark_set(Window *w, View *v, int m)
{
    Doc *d = v->doc;
    Mark *mk = &d->marks[m];
    mk->set = true;
    mk->pos = view_pos(v);
    snippet(d, mk->pos, mk->name, sizeof mk->name);
    marks_save(d);
    ui_message(w, "mark %c: p. %d", 'a' + m, mk->pos.page + 1);
}

static void go(Window *w, View *v, Pos p)
{
    view_set_pos(v, p);
    view_clamp(v, w->W, w->H, top_of(w));
    w->redraw = true;
}

void jump_to(Window *w, View *v, Pos p)
{
    Pos here = view_pos(v);
    v->previous = here;
    if (v->jump_at < v->njumps)
        v->njumps = v->jump_at;
    if (v->njumps == JUMPS_MAX) {
        memmove(v->jumps, v->jumps + 1, (JUMPS_MAX - 1) * sizeof *v->jumps);
        v->njumps--;
    }
    v->jumps[v->njumps++] = here;
    v->jump_at = v->njumps;
    go(w, v, p);
}

void mark_jump(Window *w, View *v, int m)
{
    Mark *mk = &v->doc->marks[m];
    if (!mk->set) {
        ui_message(w, "no mark %c (set it with %sm%c)", 'a' + m, leader_label(), 'a' + m);
        return;
    }
    jump_to(w, v, mk->pos);
    ui_message(w, "%c: %s", 'a' + m, mk->name);
}

void jump_back(Window *w, View *v)
{
    if (v->previous.page < 0) {
        ui_message(w, "no previous jump");
        return;
    }
    Pos here = view_pos(v);
    go(w, v, v->previous);
    v->previous = here;
}

void jump_history(Window *w, View *v, int dir)
{
    if (dir < 0) {
        if (v->jump_at <= 0)
            return;
        if (v->jump_at == v->njumps && v->njumps < JUMPS_MAX)
            v->jumps[v->njumps++] = view_pos(v);
        v->jump_at--;
    } else {
        if (v->jump_at + 1 >= v->njumps)
            return;
        v->jump_at++;
    }
    go(w, v, v->jumps[v->jump_at]);
}
