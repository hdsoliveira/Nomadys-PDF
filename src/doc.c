/* Document: PDFium handles, page cache, form environment, saving. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fpdf_signature.h"
#include "fpdf_transformpage.h"
#include "nomadys.h"

Doc *docs[MAX_DOCS];
int ndocs;
bool need_redraw_all;

static uint64_t use_clock;

static Doc *doc_of(FPDF_FORMFILLINFO *info) { return ((FormInfo *)info)->doc; }

static int page_index(Doc *d, FPDF_PAGE page)
{
    for (int i = 0; i < d->npages; i++)
        if (d->slots[i].page == page)
            return i;
    return -1;
}

/* ---------- form fill callbacks ---------- */
static void ffi_invalidate(FPDF_FORMFILLINFO *info, FPDF_PAGE page,
                           double l, double t, double r, double b)
{
    (void)l, (void)t, (void)r, (void)b;
    Doc *d = doc_of(info);
    int i = page_index(d, page);
    if (i >= 0)
        d->slots[i].version++;
    need_redraw_all = true;
}

static int ffi_set_timer(FPDF_FORMFILLINFO *info, int elapse, TimerCallback fn)
{
    Doc *d = doc_of(info);
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!d->timers[i].id) {
            d->timers[i] = (FormTimer){++d->next_timer_id, elapse, SDL_GetTicks() + elapse, fn};
            return d->timers[i].id;
        }
    }
    return 0;
}

static void ffi_kill_timer(FPDF_FORMFILLINFO *info, int id)
{
    Doc *d = doc_of(info);
    for (int i = 0; i < MAX_TIMERS; i++)
        if (d->timers[i].id == id)
            d->timers[i].id = 0;
}

static void ffi_on_change(FPDF_FORMFILLINFO *info)
{
    Doc *d = doc_of(info);
    d->modified = true;
    d->save_failed = false;
    d->changed_at = SDL_GetTicks();
}

static FPDF_PAGE ffi_get_page(FPDF_FORMFILLINFO *info, FPDF_DOCUMENT doc, int i)
{
    (void)doc;
    Doc *d = doc_of(info);
    return (i >= 0 && i < d->npages) ? doc_page(d, i) : NULL;
}

static FPDF_PAGE ffi_current_page(FPDF_FORMFILLINFO *info, FPDF_DOCUMENT doc)
{
    Doc *d = doc_of(info);
    return ffi_get_page(info, doc, d->focus_page >= 0 ? d->focus_page : 0);
}

static int ffi_rotation(FPDF_FORMFILLINFO *info, FPDF_PAGE page)
{
    (void)info, (void)page;
    return 0;
}

static void ffi_named_action(FPDF_FORMFILLINFO *info, FPDF_BYTESTRING name)
{
    (void)info, (void)name;
}

static void ffi_text_focus(FPDF_FORMFILLINFO *info, FPDF_WIDESTRING v, FPDF_DWORD n, FPDF_BOOL f)
{
    (void)info, (void)v, (void)n, (void)f;
}

static void ffi_uri(FPDF_FORMFILLINFO *info, FPDF_BYTESTRING uri)
{
    (void)info, (void)uri;
}

static void ffi_goto(FPDF_FORMFILLINFO *info, int page, int zoom, float *pos, int n)
{
    (void)info, (void)page, (void)zoom, (void)pos, (void)n;
}

/* ---------- open / close ---------- */
long long file_mtime_ns(const char *path)
{
    struct stat st;
    return stat(path, &st) ? -1 : (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
}

/* Page sizes, page slots and the form environment of a freshly loaded PDF. */
static void attach(Doc *d, FPDF_DOCUMENT pdf)
{
    d->pdf = pdf;
    d->focus_page = -1;
    d->nloaded = 0;
    d->npages = FPDF_GetPageCount(pdf);
    d->pw = xcalloc(d->npages ? d->npages : 1, sizeof *d->pw);
    d->ph = xcalloc(d->npages ? d->npages : 1, sizeof *d->ph);
    d->slots = xcalloc(d->npages ? d->npages : 1, sizeof *d->slots);
    for (int i = 0; i < d->npages; i++) {
        FS_SIZEF s = {612, 792};
        FPDF_GetPageSizeByIndexF(pdf, i, &s);
        d->pw[i] = s.width > 1 ? s.width : 612;
        d->ph[i] = s.height > 1 ? s.height : 792;
    }
    FPDF_FORMFILLINFO *fi = &d->ffi.info;
    memset(fi, 0, sizeof *fi);
    d->ffi.doc = d;
    fi->version = 2;
    fi->FFI_Invalidate = ffi_invalidate;
    fi->FFI_SetTimer = ffi_set_timer;
    fi->FFI_KillTimer = ffi_kill_timer;
    fi->FFI_OnChange = ffi_on_change;
    fi->FFI_GetPage = ffi_get_page;
    fi->FFI_GetCurrentPage = ffi_current_page;
    fi->FFI_GetRotation = ffi_rotation;
    fi->FFI_ExecuteNamedAction = ffi_named_action;
    fi->FFI_SetTextFieldFocus = ffi_text_focus;
    fi->FFI_DoURIAction = ffi_uri;
    fi->FFI_DoGoToAction = ffi_goto;
    fi->xfa_disabled = 1;
    d->form = FPDFDOC_InitFormFillEnvironment(pdf, fi);
    if (d->form) {
        FPDF_SetFormFieldHighlightColor(d->form, 0, 0xFFE8DD); /* 0xBBGGRR */
        FPDF_SetFormFieldHighlightAlpha(d->form, 110);
    }
    d->saved_mtime_ns = file_mtime_ns(d->path);
}

static void close_slot(Doc *d, int i);
static void detach_doc(Doc *d, bool close);
static void detach(Doc *d) { detach_doc(d, true); }

static void free_images(PageSlot *s)
{
    for (int k = 0; s->alphas && k < s->nimages; k++)
        free(s->alphas[k].a), free(s->alphas[k].rgb);
    free(s->alphas);
    free(s->images);
    s->alphas = NULL;
    s->images = NULL;
    s->nimages = s->placed_from = 0;
    s->images_ready = false;
}

/* Pages, forms and (with close) the PDFium document; the Doc itself (path,
 * marks) stays. */
static void detach_doc(Doc *d, bool close)
{
    doc_kill_focus(d);
    search_forget(d);
    for (int i = 0; i < d->npages; i++) {
        close_slot(d, i);
        free_images(&d->slots[i]);
        free(d->slots[i].widgets);
        for (int k = 0; k < d->slots[i].nnotes; k++)
            free(d->slots[i].notes[k].quads);
        free(d->slots[i].notes);
    }
    for (int i = 0; i < MAX_TIMERS; i++)
        d->timers[i].id = 0;
    if (d->form)
        FPDFDOC_ExitFormFillEnvironment(d->form);
    d->form = NULL;
    if (close)
        FPDF_CloseDocument(d->pdf);
    free(d->pw);
    free(d->ph);
    free(d->slots);
}

/* Replaces the document with the new version on disk. Unsaved edits are
 * dropped: the file was rewritten by someone else (e.g. LaTeX). */
bool doc_reload(Doc *d)
{
    FPDF_DOCUMENT pdf = FPDF_LoadDocument(d->path, d->password);
    if (pdf && FPDF_GetPageCount(pdf) < 1) {
        FPDF_CloseDocument(pdf); /* half written, or broken: keep the old one */
        return false;
    }
    if (!pdf)
        return false; /* still being written */
    detach(d);
    attach(d, pdf);
    d->modified = false;
    doc_undo_clear(d); /* another version of the file: its annotations may differ */
    need_redraw_all = true;
    return true;
}

/* Page numbers mean other pages now: every cache keyed by (page, version)
 * must miss, so the versions start above any used before. */
static void new_versions(Doc *d)
{
    static unsigned stamp = 1u << 24;
    stamp += 1u << 16;
    for (int i = 0; i < d->npages; i++)
        d->slots[i].version = stamp;
}

/* Pages were deleted, moved, turned or added in d->pdf: sizes and page
 * slots again (pages.c). */
void doc_pages_changed(Doc *d)
{
    render_cancel(d, -1);
    detach_doc(d, false);
    attach(d, d->pdf);
    new_versions(d);
    doc_mark_changed(d, -1);
}

/* Another state of the document (an undo of a page change): it replaces
 * this one, which is unsaved until written. */
bool doc_replace_pdf(Doc *d, const char *path)
{
    FPDF_DOCUMENT pdf = FPDF_LoadDocument(path, d->password);
    if (!pdf)
        return false;
    render_cancel(d, -1);
    detach(d);
    attach(d, pdf);
    new_versions(d);
    doc_mark_changed(d, -1);
    return true;
}

/* PDFium keeps every object it has parsed until the document is closed:
 * after reading a big paper through, tens of MB. Opening the file again
 * drops that. Only for an unmodified document whose file is still the one
 * we have; page versions are kept, so textures on screen stay valid and
 * nothing visible happens. */
bool doc_compact(Doc *d)
{
    if (d->modified || d->reload_at || d->focus_page >= 0 || file_mtime_ns(d->path) != d->saved_mtime_ns)
        return false;
    FPDF_DOCUMENT pdf = FPDF_LoadDocument(d->path, d->password);
    if (!pdf)
        return false;
    if (FPDF_GetPageCount(pdf) != d->npages) {
        FPDF_CloseDocument(pdf);
        return false;
    }
    int n = d->npages;
    unsigned *versions = xmalloc((size_t)(n ? n : 1) * sizeof *versions);
    for (int i = 0; i < n; i++)
        versions[i] = d->slots[i].version;
    detach(d);
    attach(d, pdf);
    for (int i = 0; i < n; i++)
        d->slots[i].version = versions[i];
    free(versions);
    d->pages_opened = 0;
    memory_freed = true;
    return true;
}

/* Opens a PDF, or finds it open already. A password-protected one needs
 * the password (else *locked is set); it is kept in memory only, to reload
 * and save, and wiped when the document closes. */
Doc *doc_open_locked(const char *path, const char *password, bool *locked, char *err, size_t errlen)
{
    if (locked)
        *locked = false;
    char *real = realpath(path, NULL);
    if (!real) {
        snprintf(err, errlen, "%s: %s", path, strerror(errno));
        return NULL;
    }
    for (int i = 0; i < ndocs; i++) {
        if (!strcmp(docs[i]->path, real)) {
            free(real);
            docs[i]->refs++;
            return docs[i];
        }
    }
    if (ndocs == MAX_DOCS) {
        snprintf(err, errlen, "too many open documents");
        free(real);
        return NULL;
    }
    FPDF_DOCUMENT pdf = FPDF_LoadDocument(real, password);
    if (pdf && FPDF_GetPageCount(pdf) < 1) {
        FPDF_CloseDocument(pdf);
        snprintf(err, errlen, "%s: the PDF has no pages", path);
        free(real);
        return NULL;
    }
    if (!pdf) {
        static const char *why[] = {"ok", "unknown error", "cannot open file", "not a PDF or damaged",
                                    "password protected", "unsupported security", "page not found"};
        unsigned long e = FPDF_GetLastError();
        if (e == FPDF_ERR_PASSWORD && locked)
            *locked = true;
        snprintf(err, errlen, "%s: %s", path, e == FPDF_ERR_PASSWORD && password ? "wrong password" : e < 7 ? why[e] : "error");
        free(real);
        return NULL;
    }
    Doc *d = xcalloc(1, sizeof *d);
    d->path = real;
    d->password = password ? xstrdup(password) : NULL;
    d->refs = 1;
    attach(d, pdf);
    d->last.page = -1;
    marks_load(d);
    ipc_watch(d);
    docs[ndocs++] = d;
    return d;
}

Doc *doc_open(const char *path, char *err, size_t errlen) { return doc_open_locked(path, NULL, NULL, err, errlen); }

void doc_ref(Doc *d) { d->refs++; }

static void close_slot(Doc *d, int i)
{
    PageSlot *s = &d->slots[i];
    if (s->page)
        render_cancel(d, i); /* a half-rendered page must not be closed under it */
    if (s->text)
        FPDFText_ClosePage(s->text);
    if (s->page) {
        if (d->form) {
            FORM_DoPageAAction(s->page, d->form, FPDFPAGE_AACTION_CLOSE);
            FORM_OnBeforeClosePage(s->page, d->form);
        }
        FPDF_ClosePage(s->page);
        d->nloaded--;
    }
    s->page = NULL;
    s->text = NULL;
    free(s->boxes);
    free(s->line);
    s->boxes = NULL;
    s->line = NULL;
    s->chars_ready = false;
}

void doc_unref(Doc *d)
{
    if (--d->refs > 0)
        return;
    marks_save(d);
    doc_kill_focus(d); /* commits a field being edited */
    bool was_modified = d->modified;
    doc_purge_deleted(d); /* deletions were hidden for undo: now they go */
    if (!cfg.autosave)
        d->modified = was_modified; /* manual saving: purging alone is not an unsaved change */
    if (d->modified && cfg.autosave) {
        char err[256];
        if (!doc_save(d, err, sizeof err))
            fprintf(stderr, "npdf: %s\n", err);
    }
    detach(d);
    for (int i = 0; i < ndocs; i++) {
        if (docs[i] == d) {
            docs[i] = docs[--ndocs];
            break;
        }
    }
    free(d->marks_path);
    free(d->replies);
    free(d->path);
    if (d->password) { /* not left in freed memory */
        explicit_bzero(d->password, strlen(d->password));
        free(d->password);
    }
    free(d);
    memory_freed = true;
}

/* Loads a page again from the document: PDFium keeps the parsed
 * appearance of annotations, which goes stale when one is resized. */
void doc_reload_page(Doc *d, int i)
{
    if (!doc_page_ok(d, i) || !d->slots[i].page)
        return;
    if (d->focus_page == i)
        doc_kill_focus(d);
    close_slot(d, i);
}

/* Least-recently-used pages are closed once too many are loaded, across
 * every open document. */
static void evict(void)
{
    for (;;) {
        int total = 0;
        Doc *vd = NULL;
        int victim = -1;
        for (int k = 0; k < ndocs; k++) {
            Doc *d = docs[k];
            total += d->nloaded;
            for (int i = 0; i < d->npages && d->nloaded; i++) {
                if (!d->slots[i].page || i == d->focus_page)
                    continue;
                if (victim < 0 || d->slots[i].used < vd->slots[victim].used)
                    vd = d, victim = i;
            }
        }
        if (total <= LOADED_PAGE_CAP || victim < 0)
            return;
        close_slot(vd, victim);
        memory_freed = true;
    }
}

FPDF_PAGE doc_page(Doc *d, int i)
{
    if (!doc_page_ok(d, i))
        return NULL;
    PageSlot *s = &d->slots[i];
    s->used = ++use_clock;
    if (s->page)
        return s->page;
    s->page = FPDF_LoadPage(d->pdf, i);
    if (!s->page)
        return NULL;
    d->nloaded++;
    d->pages_opened++;
    if (d->form) {
        FORM_OnAfterLoadPage(s->page, d->form);
        FORM_DoPageAAction(s->page, d->form, FPDFPAGE_AACTION_OPEN);
    }
    evict();
    return s->page;
}

FPDF_TEXTPAGE doc_text(Doc *d, int i)
{
    if (!doc_page_ok(d, i))
        return NULL;
    FPDF_PAGE p = doc_page(d, i);
    if (!p)
        return NULL;
    if (!d->slots[i].text)
        d->slots[i].text = FPDFText_LoadPage(p);
    return d->slots[i].text;
}

void doc_mark_changed(Doc *d, int page)
{
    render_cancel(d, page);
    if (page >= 0) {
        PageSlot *s = &d->slots[page];
        s->version++;
        free_images(s); /* an image may have been added or moved */
    }
    d->modified = true;
    d->save_failed = false;
    d->changed_at = SDL_GetTicks();
    need_redraw_all = true;
}

int doc_run_timers(Doc *d, uint64_t now)
{
    int wait = -1;
    for (int i = 0; i < MAX_TIMERS; i++) {
        FormTimer *t = &d->timers[i];
        if (!t->id)
            continue;
        if (now >= t->next) {
            t->next = now + (t->interval > 0 ? t->interval : 1);
            t->fn(t->id);
        }
        if (t->id) {
            int left = (int)(t->next - now);
            if (wait < 0 || left < wait)
                wait = left;
        }
    }
    return wait;
}

/* ---------- saving ---------- */
typedef struct {
    FPDF_FILEWRITE base;
    int fd;
    bool failed;
} FdWriter;

static int write_block(FPDF_FILEWRITE *self, const void *data, unsigned long size)
{
    FdWriter *w = (FdWriter *)self;
    const char *p = data;
    while (size && !w->failed) {
        ssize_t n = write(w->fd, p, size);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            w->failed = true;
            break;
        }
        p += n;
        size -= (unsigned long)n;
    }
    return !w->failed;
}

/* A whole PDF to path, through a temporary file (snapshots, extracted pages). */
bool pdf_write(FPDF_DOCUMENT pdf, const char *path, char *err, size_t errlen)
{
    size_t n = strlen(path) + 32;
    char *tmp = xmalloc(n);
    snprintf(tmp, n, "%s.nomadys-XXXXXX", path);
    int fd = mkstemp(tmp);
    if (fd < 0) {
        snprintf(err, errlen, "cannot write %s: %s", path, strerror(errno));
        free(tmp);
        return false;
    }
    FdWriter w = {{1, write_block}, fd, false};
    bool ok = FPDF_SaveAsCopy(pdf, &w.base, FPDF_NO_INCREMENTAL) && !w.failed;
    ok = (close(fd) == 0) && ok;
    if (ok && rename(tmp, path) != 0)
        ok = false;
    if (!ok) {
        snprintf(err, errlen, "cannot write %s: %s", path, strerror(errno ? errno : EIO));
        unlink(tmp);
    }
    free(tmp);
    return ok;
}

/* Save to a temporary file, then atomic rename over the original. */
/* The document as it is now, written to a file (atomically: a temporary
 * file next to it, renamed over it at the end). */
static bool write_to(Doc *d, const char *path, char *err, size_t errlen)
{
    size_t n = strlen(path) + 32;
    char *tmp = xmalloc(n);
    snprintf(tmp, n, "%s.nomadys-XXXXXX", path);
    int fd = mkstemp(tmp);
    if (fd < 0) {
        snprintf(err, errlen, "save failed: %s", strerror(errno));
        free(tmp);
        return false;
    }
    struct stat st;
    if (stat(d->path, &st) == 0)
        fchmod(fd, st.st_mode & 07777);
    FdWriter w = {{1, write_block}, fd, false};
    /* A full rewrite keeps the file compact: an incremental save appends
     * every object PDFium has loaded (the page's photos included), so each
     * save made big papers tens of MB larger. Signed PDFs stay incremental,
     * since rewriting them would break the signature. */
    int mode = FPDF_GetSignatureCount(d->pdf) > 0 ? FPDF_INCREMENTAL : FPDF_NO_INCREMENTAL;
    bool ok = FPDF_SaveAsCopy(d->pdf, &w.base, mode) && !w.failed;
    ok = ok && pdf_link_replies(fd, d->replies, d->nreplies); /* the /IRT PDFium cannot write */
    ok = ok && fsync(fd) == 0;
    ok = (close(fd) == 0) && ok;
    if (ok && rename(tmp, path) != 0)
        ok = false;
    if (!ok) {
        snprintf(err, errlen, "save failed: %s", strerror(errno ? errno : EIO));
        unlink(tmp);
        free(tmp);
        return false;
    }
    free(tmp);
    return true;
}

/* :w file.pdf: a copy under another name (the document stays the one open). */
bool doc_save_copy(Doc *d, const char *path, bool overwrite, char *err, size_t errlen)
{
    render_cancel(d, -1);
    struct stat a, b;
    if (stat(path, &a) == 0 && stat(d->path, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino)
        return doc_save(d, err, errlen); /* the same file: an ordinary save */
    if (!overwrite && access(path, F_OK) == 0) {
        snprintf(err, errlen, "%s exists: :w! %s replaces it", strrchr(path, '/') ? strrchr(path, '/') + 1 : path,
                 strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
        return false;
    }
    return write_to(d, path, err, errlen);
}

/* Keeps the file as it is on disk, as name.pdf.bak, before this session's first
 * save writes over it. A hard link: nothing is copied (a save puts a new file in
 * its place), so it costs nothing even for a huge PDF; a copy where links are not
 * possible. A later session replaces the .bak. Silent when it cannot be made. */
static void make_backup(Doc *d)
{
    if (!cfg.backup || d->backed_up || access(d->path, F_OK) != 0)
        return;
    d->backed_up = true;
    size_t n = strlen(d->path) + 8;
    char *bak = xmalloc(n);
    snprintf(bak, n, "%s.bak", d->path);
    unlink(bak);
    bool ok = link(d->path, bak) == 0;
    if (!ok) { /* another kind of file system: a copy */
        FILE *in = fopen(d->path, "rb"), *out = in ? fopen(bak, "wb") : NULL;
        char buf[1 << 16];
        size_t got;
        ok = out != NULL;
        while (ok && (got = fread(buf, 1, sizeof buf, in)) > 0)
            ok = fwrite(buf, 1, got, out) == got;
        if (out)
            ok = fclose(out) == 0 && ok;
        if (in)
            fclose(in);
        if (!ok)
            unlink(bak);
    }
    d->backup_new = ok;
    free(bak);
}

bool doc_save(Doc *d, char *err, size_t errlen)
{
    render_cancel(d, -1);
    /* the atomic rename below would replace even a read-only file */
    if (access(d->path, W_OK) != 0 && errno != ENOENT) {
        snprintf(err, errlen, "save failed: %s is read-only", strrchr(d->path, '/') + 1);
        return false;
    }
    uint64_t t0 = SDL_GetTicks();
    make_backup(d);
    if (!write_to(d, d->path, err, errlen))
        return false;
    d->modified = false;
    d->save_ms = (int)(SDL_GetTicks() - t0);
    d->saved_mtime_ns = file_mtime_ns(d->path); /* our write: do not reload */
    return true;
}

/* ---------- page analysis ---------- */
typedef struct { float a, b, c, d, e, f; } Mat;

static Mat mat_mul(Mat m, Mat n) /* apply m, then n */
{
    return (Mat){m.a * n.a + m.b * n.c, m.a * n.b + m.b * n.d,
                 m.c * n.a + m.d * n.c, m.c * n.b + m.d * n.d,
                 m.e * n.a + m.f * n.c + n.e, m.e * n.b + m.f * n.d + n.f};
}

static void add_rect(PageSlot *s, int *cap, PRect r, Alpha al)
{
    if (s->nimages == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        s->images = xrealloc(s->images, *cap * sizeof *s->images);
        s->alphas = xrealloc(s->alphas, *cap * sizeof *s->alphas);
    }
    s->alphas[s->nimages] = al;
    s->images[s->nimages++] = r;
}

static Alpha image_alpha(Doc *d, FPDF_PAGE p, FPDF_PAGEOBJECT o);


static PRect transformed(Mat m, float l, float b, float r, float t)
{
    float xs[4] = {l, r, l, r}, ys[4] = {b, b, t, t};
    PRect out = {1e30f, -1e30f, -1e30f, 1e30f};
    for (int i = 0; i < 4; i++) {
        float x = xs[i] * m.a + ys[i] * m.c + m.e, y = xs[i] * m.b + ys[i] * m.d + m.f;
        out.l = SDL_min(out.l, x), out.r = SDL_max(out.r, x);
        out.b = SDL_min(out.b, y), out.t = SDL_max(out.t, y);
    }
    return out;
}

static void collect(Doc *d, FPDF_PAGE p, PageSlot *s, int *cap, FPDF_PAGEOBJECT o, Mat m, int depth)
{
    int type = FPDFPageObj_GetType(o);
    if (type == FPDF_PAGEOBJ_FORM && depth < 8) {
        FS_MATRIX fm;
        Mat inner = m;
        if (FPDFPageObj_GetMatrix(o, &fm))
            inner = mat_mul((Mat){fm.a, fm.b, fm.c, fm.d, fm.e, fm.f}, m);
        int n = FPDFFormObj_CountObjects(o);
        for (int i = 0; i < n; i++)
            collect(d, p, s, cap, FPDFFormObj_GetObject(o, (unsigned long)i), inner, depth + 1);
        return;
    }
    if (type != FPDF_PAGEOBJ_IMAGE && type != FPDF_PAGEOBJ_SHADING)
        return;
    float l, b, r, t;
    if (!FPDFPageObj_GetBounds(o, &l, &b, &r, &t))
        return;
    PRect full = transformed(m, l, b, r, t);
    /* only what the clip path shows (a figure trimmed in LaTeX): each of
     * its paths bounds the image, in the same space as its bounds */
    FPDF_CLIPPATH clip = FPDFPageObj_GetClipPath(o);
    for (int i = 0, np = clip ? FPDFClipPath_CountPaths(clip) : 0; i < np; i++) {
        float cl = 1e30f, cb = 1e30f, cr = -1e30f, ct = -1e30f, x, y;
        int ns = FPDFClipPath_CountPathSegments(clip, i);
        for (int j = 0; j < ns; j++) {
            FPDF_PATHSEGMENT seg = FPDFClipPath_GetPathSegment(clip, i, j);
            if (seg && FPDFPathSegment_GetPoint(seg, &x, &y))
                cl = SDL_min(cl, x), cr = SDL_max(cr, x), cb = SDL_min(cb, y), ct = SDL_max(ct, y);
        }
        if (cl <= cr) /* a path with points (text clips have none) */
            l = SDL_max(l, cl), r = SDL_min(r, cr), b = SDL_max(b, cb), t = SDL_min(t, ct);
    }
    if (l >= r || b >= t)
        return; /* clipped away */
    Alpha al = type == FPDF_PAGEOBJ_IMAGE ? image_alpha(d, p, o) : (Alpha){0};
    al.full = full;
    /* its opacity is drawn with the image's own matrix, not the forms'
     * around it: a form that mirrors it (Inkscape's y axis) mirrors that
     * too; one that turns it leaves no simple way to line it up */
    if (SDL_fabsf(m.b) > 1e-4f || SDL_fabsf(m.c) > 1e-4f) {
        free(al.a), free(al.rgb);
        al.a = al.rgb = NULL;
    }
    al.flip_x = m.a < 0, al.flip_y = m.d < 0;
    add_rect(s, cap, transformed(m, l, b, r, t), al);
}

/* Where a placed picture (a stamp) is see-through: its opacity, at most
 * ALPHA_SIDE pixels a side; a = NULL when it is opaque all over, so that
 * dark mode keeps its whole rectangle. (FPDFPageObj_HasTransparency does
 * not see an image's soft mask: the pixels tell.) */
#define ALPHA_SIDE 1024
static bool is_jpeg(FPDF_PAGEOBJECT o)
{
    char f[32];
    int n = FPDFImageObj_GetImageFilterCount(o);
    return n > 0 && FPDFImageObj_GetImageFilter(o, n - 1, f, sizeof f) > 0 && !strcmp(f, "DCTDecode");
}

/* An image's opacity, or a = NULL when it is opaque all over. A JPEG has
 * none (and image.c keeps placed PNGs lossless). A see-through one keeps
 * its colours too (rgb), to tell where the page really shows it: its clip
 * may cut it to any shape. */
static Alpha image_alpha(Doc *d, FPDF_PAGE p, FPDF_PAGEOBJECT o)
{
    Alpha out = {0};
    if (!o || FPDFPageObj_GetType(o) != FPDF_PAGEOBJ_IMAGE || is_jpeg(o))
        return out;
    /* drawn at the size of its matrix, one pixel per point: made larger for
     * the time of the call, so its edges do not come out in steps */
    FS_MATRIX mx;
    unsigned iw = 0, ih = 0;
    bool scaled = false;
    if (FPDFPageObj_GetMatrix(o, &mx) && FPDFImageObj_GetImagePixelSize(o, &iw, &ih)) {
        float side = SDL_max(SDL_sqrtf(mx.a * mx.a + mx.b * mx.b), SDL_sqrtf(mx.c * mx.c + mx.d * mx.d));
        float k = side > 0 ? SDL_min((float)SDL_max(iw, ih), (float)ALPHA_SIDE) / side : 1;
        if (k > 1.01f) {
            FS_MATRIX big = {mx.a * k, mx.b * k, mx.c * k, mx.d * k, mx.e, mx.f};
            scaled = FPDFPageObj_SetMatrix(o, &big);
        }
    }
    FPDF_BITMAP bmp = FPDFImageObj_GetRenderedBitmap(d->pdf, p, o);
    if (scaled)
        FPDFPageObj_SetMatrix(o, &mx);
    if (!bmp)
        return out;
    int bw = FPDFBitmap_GetWidth(bmp), bh = FPDFBitmap_GetHeight(bmp), stride = FPDFBitmap_GetStride(bmp);
    const uint8_t *px = FPDFBitmap_GetBuffer(bmp);
    bool see_through = false;
    for (int y = 0; px && FPDFBitmap_GetFormat(bmp) == FPDFBitmap_BGRA && y < bh && !see_through; y++)
        for (int x = 0; x < bw && !see_through; x++)
            see_through = px[(size_t)y * stride + 4 * x + 3] < 250;
    if (see_through) {
        float k = SDL_min(1.0f, (float)ALPHA_SIDE / (float)SDL_max(bw, bh));
        out.w = SDL_max(1, (int)(bw * k)), out.h = SDL_max(1, (int)(bh * k));
        out.a = xmalloc((size_t)out.w * out.h);
        out.rgb = xmalloc((size_t)out.w * out.h * 3);
        for (int y = 0; y < out.h; y++)
            for (int x = 0; x < out.w; x++) {
                const uint8_t *q = px + (size_t)(y * bh / out.h) * stride + 4 * (x * bw / out.w);
                uint8_t *c = out.rgb + ((size_t)y * out.w + x) * 3;
                out.a[(size_t)y * out.w + x] = q[3];
                c[0] = q[0], c[1] = q[1], c[2] = q[2]; /* B G R */
            }
    }
    FPDFBitmap_Destroy(bmp);
    return out;
}

static Alpha picture_alpha(Doc *d, FPDF_ANNOTATION a)
{
    Alpha out = {0};
    for (int j = 0; j < FPDFAnnot_GetObjectCount(a) && !out.a; j++)
        out = image_alpha(d, NULL, FPDFAnnot_GetObject(a, j));
    return out;
}

/* Image/shading regions in page space. A single image covering most of the
 * page is a scan: it is dropped so that dark mode still applies. */
void doc_page_images(Doc *d, int i)
{
    if (!doc_page_ok(d, i))
        return;
    PageSlot *s = &d->slots[i];
    if (s->images_ready)
        return;
    FPDF_PAGE p = doc_page(d, i);
    if (!p)
        return;
    int cap = 0;
    Mat id = {1, 0, 0, 1, 0, 0};
    int n = FPDFPage_CountObjects(p);
    for (int k = 0; k < n; k++)
        collect(d, p, s, &cap, FPDFPage_GetObject(p, k), id, 0);
    FS_RECTF box;
    float area = d->pw[i] * d->ph[i];
    if (FPDF_GetPageBoundingBox(p, &box))
        area = (box.right - box.left) * (box.top - box.bottom);
    int kept = 0;
    for (int k = 0; k < s->nimages; k++) {
        PRect r = s->images[k];
        if ((r.r - r.l) * (r.t - r.b) < 0.8f * area) {
            s->alphas[kept] = s->alphas[k];
            s->images[kept++] = r;
        } else {
            free(s->alphas[k].a), free(s->alphas[k].rgb);
        }
    }
    s->nimages = kept;
    /* pictures placed as stamps (image.c: :image, Ctrl-v): you chose them,
     * so they keep their own colours, whatever they look like */
    s->placed_from = kept;
    for (int k = 0; k < FPDFPage_GetAnnotCount(p); k++) {
        FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, k);
        if (!a)
            continue;
        bool picture = false;
        for (int j = 0; FPDFAnnot_GetSubtype(a) == FPDF_ANNOT_STAMP && j < FPDFAnnot_GetObjectCount(a); j++) {
            FPDF_PAGEOBJECT o = FPDFAnnot_GetObject(a, j);
            picture |= o && FPDFPageObj_GetType(o) == FPDF_PAGEOBJ_IMAGE;
        }
        FS_RECTF r;
        if (picture && FPDFAnnot_GetRect(a, &r)) {
            PRect pr = {SDL_min(r.left, r.right), SDL_max(r.top, r.bottom), SDL_max(r.left, r.right),
                        SDL_min(r.top, r.bottom)};
            Alpha al = picture_alpha(d, a);
            al.full = pr;
            add_rect(s, &cap, pr, al);
        }
        FPDFPage_CloseAnnot(a);
    }
    s->images_ready = true;
}

static int widget_cmp(const void *a, const void *b)
{
    const Widget *x = a, *y = b;
    if (SDL_fabsf(x->r.t - y->r.t) > 3)
        return x->r.t > y->r.t ? -1 : 1;
    return (x->r.l > y->r.l) - (x->r.l < y->r.l);
}

/* Fillable widgets of a page in reading order (top to bottom, left to right). */
void doc_page_widgets(Doc *d, int i)
{
    if (!doc_page_ok(d, i))
        return;
    PageSlot *s = &d->slots[i];
    if (s->widgets_ready || !d->form)
        return;
    FPDF_PAGE p = doc_page(d, i);
    if (!p)
        return;
    int n = FPDFPage_GetAnnotCount(p);
    s->widgets = xcalloc(n ? n : 1, sizeof *s->widgets);
    for (int k = 0; k < n; k++) {
        FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, k);
        if (!a)
            continue;
        FS_RECTF r;
        int type = FPDFAnnot_GetFormFieldType(d->form, a);
        bool ok = FPDFAnnot_GetSubtype(a) == FPDF_ANNOT_WIDGET && type > 0 &&
                  type != FPDF_FORMFIELD_PUSHBUTTON && type != FPDF_FORMFIELD_SIGNATURE &&
                  !(FPDFAnnot_GetFormFieldFlags(d->form, a) & FPDF_FORMFLAG_READONLY) &&
                  !(FPDFAnnot_GetFlags(a) & FPDF_ANNOT_FLAG_HIDDEN) && FPDFAnnot_GetRect(a, &r);
        if (ok)
            s->widgets[s->nwidgets++] = (Widget){k, {SDL_min(r.left, r.right), SDL_max(r.top, r.bottom),
                                                     SDL_max(r.left, r.right), SDL_min(r.top, r.bottom)}};
        FPDFPage_CloseAnnot(a);
    }
    qsort(s->widgets, s->nwidgets, sizeof *s->widgets, widget_cmp);
    s->widgets_ready = true;
}

bool doc_has_forms(Doc *d)
{
    return d->form && FPDF_GetFormType(d->pdf) != FORMTYPE_NONE;
}

bool doc_focus_widget(Doc *d, int page, int w)
{
    if (!doc_page_ok(d, page) || w < 0 || w >= d->slots[page].nwidgets)
        return false;
    FPDF_PAGE p = doc_page(d, page);
    if (!p)
        return false;
    FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, d->slots[page].widgets[w].idx);
    if (!a)
        return false;
    bool ok = FORM_SetFocusedAnnot(d->form, a);
    FPDFPage_CloseAnnot(a);
    if (ok) {
        d->focus_page = page;
        d->focus_widget = w;
        d->slots[page].version++;
        need_redraw_all = true;
    }
    return ok;
}

void doc_kill_focus(Doc *d)
{
    if (d->form && d->focus_page >= 0) {
        FORM_ForceToKillFocus(d->form);
        d->slots[d->focus_page].version++;
        need_redraw_all = true;
    }
    d->focus_page = -1;
}

/* Selectable characters and their line numbers, for caret mode. */
void doc_page_chars(Doc *d, int i)
{
    if (!doc_page_ok(d, i))
        return;
    PageSlot *s = &d->slots[i];
    if (s->chars_ready)
        return;
    FPDF_TEXTPAGE tp = doc_text(d, i);
    if (!tp)
        return;
    int n = FPDFText_CountChars(tp);
    if (n < 0)
        n = 0;
    s->nchars = n;
    s->boxes = xcalloc(n ? n : 1, sizeof *s->boxes);
    s->line = xcalloc(n ? n : 1, sizeof *s->line);
    int line = -1;
    float lt = 0, lb = 0;
    for (int k = 0; k < n; k++) {
        FS_RECTF r;
        unsigned int u = FPDFText_GetUnicode(tp, k);
        bool gen = FPDFText_IsGenerated(tp, k) == 1;
        if (gen || u == '\r' || u == '\n' || !FPDFText_GetLooseCharBox(tp, k, &r) ||
            r.right - r.left <= 0 || r.top - r.bottom <= 0) {
            s->line[k] = -1;
            continue;
        }
        s->boxes[k] = (PRect){r.left, r.top, r.right, r.bottom};
        float mid = (r.top + r.bottom) / 2;
        if (line < 0 || mid > lt || mid < lb) {
            line++;
            lt = r.top;
            lb = r.bottom;
        }
        s->line[k] = line;
    }
    s->nlines = line + 1;
    s->chars_ready = true;
}
