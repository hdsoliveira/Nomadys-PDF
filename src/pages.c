/* Changing the pages of a PDF, from the page grid (\g) or the command line:
 * delete, turn, move, extract to a new PDF, insert another PDF. Each change
 * keeps the document as it was in a snapshot file, so u undoes it (and
 * Ctrl-r redoes it); marks follow their pages. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fpdf_ppo.h"
#include "fpdf_signature.h"
#include "nomadys.h"

static void snapshot_dir(char *out, size_t cap)
{
    const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(out, cap, "%s/nomadys-pdf/undo", xdg);
    else
        snprintf(out, cap, "%s/.cache/nomadys-pdf/undo", home ? home : "/tmp");
    char p[4096];
    snprintf(p, sizeof p, "%s", out);
    for (char *c = p + 1; *c; c++)
        if (*c == '/') {
            *c = 0;
            mkdir(p, 0700);
            *c = '/';
        }
    mkdir(p, 0700);
}

/* The document as it is now, in a temporary file; NULL on failure. */
char *pages_snapshot(Doc *d)
{
    static unsigned counter;
    char dir[4096], err[512];
    snapshot_dir(dir, sizeof dir);
    char *path = xmalloc(strlen(dir) + 64);
    sprintf(path, "%s/%d-%u.pdf", dir, (int)getpid(), ++counter);
    doc_kill_focus(d); /* a field being typed in is part of it */
    if (!pdf_write(d->pdf, path, err, sizeof err)) {
        free(path);
        return NULL;
    }
    return path;
}

/* Every view of d shows the new pages; old is the page each old page
 * became (-1: gone), so each view stays at what it was reading. */
static void refresh(Doc *d, const int *map, int old_n)
{
    for (int k = 0; k < nwindows; k++) {
        Window *w = windows[k];
        for (int t = 0; t <= w->ntabs; t++) {
            View *v = t < w->ntabs ? w->tabs[t] : w->split;
            if (!v || v->doc != d)
                continue;
            Pos p = v->lay_w ? view_pos(v) : v->pending_pos;
            view_doc_reloaded(v);
            if (map && p.page >= 0 && p.page < old_n) {
                int to = map[p.page];
                for (int q = p.page; to < 0 && q < old_n; q++) /* a deleted page: the next one left */
                    to = map[q];
                v->pending_pos = (Pos){SDL_clamp(to < 0 ? d->npages - 1 : to, 0, d->npages - 1), to == map[p.page] ? p.y : 0};
            }
            v->match_page = -1;
        }
        if (w->thumbs)
            grid_reset(w);
        present_reset(w);
        w->grid_sel = SDL_clamp(w->grid_sel, 0, SDL_max(0, d->npages - 1));
        w->grid_anchor = -1;
        w->redraw = true;
    }
    need_redraw_all = true;
}

static void move_marks(Doc *d, const int *map, int old_n)
{
    for (int m = 0; m < NMARKS; m++) {
        Mark *mk = &d->marks[m];
        if (!mk->set || mk->pos.page < 0 || mk->pos.page >= old_n)
            continue;
        int to = map[mk->pos.page];
        if (to < 0)
            mk->set = false; /* its page is gone */
        else
            mk->pos.page = to;
    }
    if (d->last.page >= 0 && d->last.page < old_n && map[d->last.page] >= 0)
        d->last.page = map[d->last.page];
    marks_save(d);
}

bool pages_restore(Doc *d, const char *snapshot, const Mark *marks)
{
    if (!doc_replace_pdf(d, snapshot))
        return false;
    memcpy(d->marks, marks, NMARKS * sizeof *marks);
    marks_save(d);
    refresh(d, NULL, 0);
    return true;
}

/* Before a change: false (with a message) if the pages cannot change. */
static bool can_change(Window *w, Doc *d)
{
    if (FPDF_GetSignatureCount(d->pdf) > 0) {
        ui_message_long(w, 5000, "this PDF is signed: changing its pages would break the signature");
        return false;
    }
    return true;
}

/* The snapshot and marks for undo, then the change, then everything that
 * shows the pages. map: old page -> new page (-1 deleted). */
typedef struct {
    char *snapshot;
    Mark marks[NMARKS];
    int old_n;
} Before;

static bool begin(Window *w, Doc *d, Before *b)
{
    if (!can_change(w, d))
        return false;
    b->snapshot = pages_snapshot(d);
    if (!b->snapshot) {
        ui_message_long(w, 5000, "cannot keep a copy for undo (disk full?): nothing changed");
        return false;
    }
    memcpy(b->marks, d->marks, sizeof b->marks);
    b->old_n = d->npages;
    return true;
}

static void finish(Doc *d, Before *b, const int *map, int page)
{
    doc_pages_changed(d);
    move_marks(d, map, b->old_n);
    undo_pages(d, page, b->snapshot, b->marks);
    refresh(d, map, b->old_n);
}

static void range_label(int first, int last, char *out, size_t cap)
{
    if (first == last)
        snprintf(out, cap, "p. %d", first + 1);
    else
        snprintf(out, cap, "pp. %d–%d", first + 1, last + 1);
}

void pages_delete(Window *w, Doc *d, int first, int last)
{
    first = SDL_clamp(first, 0, d->npages - 1), last = SDL_clamp(last, first, d->npages - 1);
    if (last - first + 1 >= d->npages) {
        ui_message(w, "a PDF needs at least one page");
        return;
    }
    Before b;
    if (!begin(w, d, &b))
        return;
    doc_kill_focus(d);
    render_cancel(d, -1);
    for (int i = last; i >= first; i--) {
        doc_reload_page(d, i); /* closed before it goes */
        FPDFPage_Delete(d->pdf, i);
    }
    int *map = xmalloc((size_t)b.old_n * sizeof *map);
    for (int i = 0; i < b.old_n; i++)
        map[i] = i < first ? i : i > last ? i - (last - first + 1) : -1;
    finish(d, &b, map, SDL_min(first, b.old_n - (last - first + 1) - 1));
    free(map);
    char what[48];
    range_label(first, last, what, sizeof what);
    ui_message_long(w, 5000, "deleted %s   u undoes it", what);
}

void pages_rotate(Window *w, Doc *d, int first, int last, int quarters)
{
    first = SDL_clamp(first, 0, d->npages - 1), last = SDL_clamp(last, first, d->npages - 1);
    Before b;
    if (!begin(w, d, &b))
        return;
    for (int i = first; i <= last; i++) {
        FPDF_PAGE p = doc_page(d, i);
        if (p)
            FPDFPage_SetRotation(p, ((FPDFPage_GetRotation(p) + quarters) % 4 + 4) % 4);
    }
    int *map = xmalloc((size_t)b.old_n * sizeof *map);
    for (int i = 0; i < b.old_n; i++)
        map[i] = i;
    finish(d, &b, map, first);
    free(map);
    char what[48];
    range_label(first, last, what, sizeof what);
    ui_message(w, "%s turned %s (in the file)   u undoes it", what, quarters > 0 ? "right" : "left");
}

/* Moves pages first..last so that the first of them becomes page dest
 * (counted after taking them out). Returns dest, or -1. */
int pages_move(Window *w, Doc *d, int first, int last, int dest)
{
    int n = last - first + 1;
    dest = SDL_clamp(dest, 0, d->npages - n);
    if (dest == first || n <= 0)
        return -1;
    Before b;
    if (!begin(w, d, &b))
        return -1;
    int *idx = xmalloc((size_t)n * sizeof *idx);
    for (int i = 0; i < n; i++)
        idx[i] = first + i;
    for (int i = 0; i < b.old_n; i++)
        doc_reload_page(d, i); /* none loaded while PDFium reorders */
    bool ok = FPDF_MovePages(d->pdf, idx, (unsigned long)n, dest);
    free(idx);
    if (!ok) {
        unlink(b.snapshot);
        free(b.snapshot);
        ui_message(w, "cannot move those pages");
        return -1;
    }
    /* the new order: the others in order, the moved ones at dest */
    int *order = xmalloc((size_t)b.old_n * sizeof *order), k = 0;
    for (int i = 0; i < b.old_n; i++)
        if (i < first || i > last)
            order[k++] = i;
    memmove(order + dest + n, order + dest, (size_t)(b.old_n - n - dest) * sizeof *order);
    for (int i = 0; i < n; i++)
        order[dest + i] = first + i;
    int *map = xmalloc((size_t)b.old_n * sizeof *map);
    for (int i = 0; i < b.old_n; i++)
        map[order[i]] = i;
    finish(d, &b, map, dest);
    free(order);
    free(map);
    return dest;
}

/* Pages first..last as a new PDF at path. */
bool pages_extract(Window *w, Doc *d, int first, int last, const char *path)
{
    first = SDL_clamp(first, 0, d->npages - 1), last = SDL_clamp(last, first, d->npages - 1);
    FPDF_DOCUMENT out = FPDF_CreateNewDocument();
    int n = last - first + 1;
    int *idx = xmalloc((size_t)n * sizeof *idx);
    for (int i = 0; i < n; i++)
        idx[i] = first + i;
    doc_kill_focus(d);
    bool ok = out && FPDF_ImportPagesByIndex(out, d->pdf, idx, (unsigned long)n, 0);
    free(idx);
    char err[600] = "cannot copy the pages";
    ok = ok && pdf_write(out, path, err, sizeof err);
    if (out)
        FPDF_CloseDocument(out);
    char what[48];
    range_label(first, last, what, sizeof what);
    if (ok)
        ui_message_long(w, 6000, "%s saved as %s   (:e opens it)", what, strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
    else
        ui_message_long(w, 6000, "%s", err);
    return ok;
}

/* Every page of another PDF, after page `after` (-1: at the start). */
void pages_insert(Window *w, Doc *d, const char *path, int after)
{
    FPDF_DOCUMENT src = FPDF_LoadDocument(path, NULL);
    if (!src) {
        ui_message_long(w, 5000, "cannot open %s (%s)", path,
                        FPDF_GetLastError() == FPDF_ERR_PASSWORD ? "it has a password" : "not a PDF?");
        return;
    }
    int count = FPDF_GetPageCount(src);
    Before b;
    if (count <= 0 || !begin(w, d, &b)) {
        FPDF_CloseDocument(src);
        return;
    }
    after = SDL_clamp(after, -1, d->npages - 1);
    for (int i = 0; i < b.old_n; i++)
        doc_reload_page(d, i);
    bool ok = FPDF_ImportPages(d->pdf, src, NULL, after + 1);
    FPDF_CloseDocument(src);
    if (!ok) {
        unlink(b.snapshot);
        free(b.snapshot);
        ui_message(w, "cannot insert the pages of %s", path);
        return;
    }
    int *map = xmalloc((size_t)b.old_n * sizeof *map);
    for (int i = 0; i < b.old_n; i++)
        map[i] = i <= after ? i : i + count;
    finish(d, &b, map, after + 1);
    free(map);
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    ui_message_long(w, 5000, "%d page%s of %s inserted after p. %d   u undoes it", count, count == 1 ? "" : "s", name,
                    after + 1);
}

/* "3-7", "5", "3-" (to the end); pages counted from 1. */
bool pages_parse_range(const char *s, int npages, int *first, int *last)
{
    int a = 0, b = 0, used = 0;
    while (*s == ' ')
        s++;
    if (sscanf(s, "%d-%d%n", &a, &b, &used) == 2) {
    } else if (sscanf(s, "%d-%n", &a, &used) == 1 && used) {
        b = npages;
    } else if (sscanf(s, "%d%n", &a, &used) == 1) {
        b = a;
    } else {
        return false;
    }
    if (a < 1 || b < a || a > npages)
        return false;
    *first = a - 1;
    *last = SDL_min(b, npages) - 1;
    return true;
}

/* "paper.pdf", pages 3-5 -> "paper-p3-5.pdf" next to it. */
void pages_default_name(Doc *d, int first, int last, char *out, size_t cap)
{
    size_t n = strlen(d->path);
    if (n > 4 && !strcasecmp(d->path + n - 4, ".pdf"))
        n -= 4;
    if (first == last)
        snprintf(out, cap, "%.*s-p%d.pdf", (int)n, d->path, first + 1);
    else
        snprintf(out, cap, "%.*s-p%d-%d.pdf", (int)n, d->path, first + 1, last + 1);
}
