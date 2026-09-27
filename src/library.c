/* Searching the whole library (\/ and \n, :library): the text of every PDF
 * in pdf_dirs, or every comment and highlight in them.
 *
 * index:  worker processes ("npdf --index-worker DIR K"), apart so that a PDF
 *         that crashes PDFium cannot take the reader down, niced, each doing
 *         the files whose path hash falls to it. Per PDF one gzip file:
 *         a line of text per page, then a line "\1", then one line per note
 *         (page, kind, colour, highlighted text, comment). A manifest per
 *         worker lists path, date and size, so only changed files are read
 *         again. It lives in $XDG_CACHE_HOME/nomadys-pdf/index.
 * search: a few threads read the index while you type; a newer query stops
 *         an older search. Words match anywhere on the page, in any order
 *         (ASCII case ignored); pages with the words together come first. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "fpdf_annot.h"
#include "fpdf_text.h"
#include "nomadys.h"

#define WORKERS 3            /* index processes; the partition of the files */
#define SEARCH_THREADS 4
#define MAX_HITS 2000
#define HITS_PER_FILE 30
#define MAX_FILE_BYTES (400ll << 20)
#define MAX_PAGES 5000

static uint64_t fnv1a(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 1099511628211ull;
    return h;
}

static void index_dir(char *out, size_t cap)
{
    const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(out, cap, "%s/nomadys-pdf/index", xdg);
    else
        snprintf(out, cap, "%s/.cache/nomadys-pdf/index", home ? home : "/tmp");
}

static void mkdirs(const char *path)
{
    char p[4096];
    snprintf(p, sizeof p, "%s", path);
    for (char *c = p + 1; *c; c++)
        if (*c == '/') {
            *c = 0;
            mkdir(p, 0700);
            *c = '/';
        }
    mkdir(p, 0700);
}

/* One line: tabs and line breaks become spaces, runs of spaces one. */
static void flatten(char *s)
{
    char *o = s;
    bool space = true;
    for (char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c == 0x7F) /* also PDFium's U+0002 soft hyphen marks, already joined */
            c = ' ';
        if (c == ' ') {
            if (!space)
                *o++ = ' ';
            space = true;
        } else {
            *o++ = (char)c;
            space = false;
        }
    }
    while (o > s && o[-1] == ' ')
        o--;
    *o = 0;
}

/* ---------- the worker (a separate process) ---------- */
typedef struct {
    char *path;
    long long mtime, size;
    int pages;
} Known;

static int known_cmp(const void *a, const void *b) { return strcmp(((const Known *)a)->path, ((const Known *)b)->path); }

typedef struct {
    Known k;
    int seq;
} Line;

static int line_cmp(const void *a, const void *b)
{
    const Line *x = a, *y = b;
    int c = strcmp(x->k.path, y->k.path);
    return c ? c : x->seq - y->seq;
}

/* A manifest, sorted by path; for a file listed twice the later line wins
 * (the manifest is only appended to). */
static Known *read_manifest(const char *file, int *n)
{
    Line *l = NULL;
    int cap = 0, m = 0;
    FILE *f = fopen(file, "r");
    char line[4400];
    while (f && fgets(line, sizeof line, f)) {
        if (!strchr(line, '\n'))
            continue; /* cut short by a crash */
        line[strcspn(line, "\n")] = 0;
        long long mtime, size;
        int pages, off = 0;
        if (sscanf(line, "%*16s\t%lld\t%lld\t%d\t%n", &mtime, &size, &pages, &off) < 3 || !off || line[off] != '/')
            continue;
        if (m == cap)
            l = xrealloc(l, (size_t)(cap = cap ? cap * 2 : 256) * sizeof *l);
        l[m] = (Line){{xstrdup(line + off), mtime, size, pages}, m};
        m++;
    }
    if (f)
        fclose(f);
    if (m)
        qsort(l, (size_t)m, sizeof *l, line_cmp);
    Known *out = xmalloc((size_t)(m ? m : 1) * sizeof *out);
    int w = 0;
    for (int i = 0; i < m; i++) {
        if (i + 1 < m && !strcmp(l[i].k.path, l[i + 1].k.path)) { /* a later line follows */
            free(l[i].k.path);
            continue;
        }
        out[w++] = l[i].k;
    }
    free(l);
    *n = w;
    return out;
}

static void known_free(Known *k, int n)
{
    for (int i = 0; i < n; i++)
        free(k[i].path);
    free(k);
}

static Known *known_find(Known *k, int n, const char *path)
{
    Known key = {(char *)path, 0, 0, 0};
    return bsearch(&key, k, (size_t)n, sizeof *k, known_cmp);
}

static uint32_t annot_color(FPDF_ANNOTATION a)
{
    unsigned int r, g, b, al;
    if (FPDFAnnot_GetColor(a, FPDFANNOT_COLORTYPE_Color, &r, &g, &b, &al))
        return r << 16 | g << 8 | b;
    for (int i = 0; i < FPDFAnnot_GetObjectCount(a); i++) {
        FPDF_PAGEOBJECT o = FPDFAnnot_GetObject(a, i);
        int fill;
        FPDF_BOOL stroke;
        if (o && FPDFPageObj_GetType(o) == FPDF_PAGEOBJ_PATH && FPDFPath_GetDrawMode(o, &fill, &stroke) &&
            (fill ? FPDFPageObj_GetFillColor(o, &r, &g, &b, &al) : stroke && FPDFPageObj_GetStrokeColor(o, &r, &g, &b, &al)))
            return r << 16 | g << 8 | b;
    }
    return cfg.highlights[0];
}

static char *annot_string(FPDF_ANNOTATION a, const char *key)
{
    unsigned long bytes = FPDFAnnot_GetStringValue(a, key, NULL, 0);
    if (bytes <= 2 || bytes > (1u << 20))
        return xstrdup("");
    unsigned short *u = xmalloc(bytes);
    FPDFAnnot_GetStringValue(a, key, u, bytes);
    char *s = utf16_to_utf8(u, (int)(bytes / 2) - 1);
    free(u);
    return s;
}

/* The text under a highlight's boxes. */
static char *covered(FPDF_TEXTPAGE tp, FPDF_ANNOTATION a)
{
    size_t nq = FPDFAnnot_CountAttachmentPoints(a);
    char *all = xstrdup("");
    for (size_t q = 0; tp && q < nq && q < 200; q++) {
        FS_QUADPOINTSF p;
        if (!FPDFAnnot_GetAttachmentPoints(a, q, &p))
            continue;
        double l = SDL_min(SDL_min(p.x1, p.x2), SDL_min(p.x3, p.x4)), r = SDL_max(SDL_max(p.x1, p.x2), SDL_max(p.x3, p.x4));
        double b = SDL_min(SDL_min(p.y1, p.y2), SDL_min(p.y3, p.y4)), t = SDL_max(SDL_max(p.y1, p.y2), SDL_max(p.y3, p.y4));
        int n = FPDFText_GetBoundedText(tp, l, t, r, b, NULL, 0);
        if (n <= 0 || n > 100000)
            continue;
        unsigned short *u = xmalloc(((size_t)n + 1) * sizeof *u);
        n = FPDFText_GetBoundedText(tp, l, t, r, b, u, n);
        char *s = utf16_to_utf8(u, n);
        all = xrealloc(all, strlen(all) + strlen(s) + 2);
        if (all[0])
            strcat(all, " ");
        strcat(all, s);
        free(s);
        free(u);
    }
    return all;
}

static bool is_note(int sub)
{
    switch (sub) {
    case FPDF_ANNOT_TEXT: case FPDF_ANNOT_FREETEXT: case FPDF_ANNOT_SQUARE: case FPDF_ANNOT_CIRCLE:
    case FPDF_ANNOT_HIGHLIGHT: case FPDF_ANNOT_UNDERLINE: case FPDF_ANNOT_SQUIGGLY: case FPDF_ANNOT_STRIKEOUT:
    case FPDF_ANNOT_INK:
        return true;
    default:
        return false;
    }
}

/* The text and notes of one PDF into gz; its page count, -1 if unreadable. */
static int extract(const char *path, const char *gz)
{
    FPDF_DOCUMENT doc = FPDF_LoadDocument(path, NULL);
    if (!doc)
        return -1;
    int n = SDL_min(FPDF_GetPageCount(doc), MAX_PAGES);
    char tmp[4300];
    snprintf(tmp, sizeof tmp, "%s.tmp%d", gz, (int)getpid());
    gzFile g = gzopen(tmp, "wb6");
    if (!g) {
        FPDF_CloseDocument(doc);
        return -1;
    }
    char *notes = xstrdup("");
    size_t notes_len = 0;
    for (int i = 0; i < n; i++) {
        FPDF_PAGE p = FPDF_LoadPage(doc, i);
        FPDF_TEXTPAGE tp = p ? FPDFText_LoadPage(p) : NULL;
        int count = tp ? FPDFText_CountChars(tp) : 0;
        if (count > 0 && count < 2000000) {
            unsigned short *u = xmalloc(((size_t)count + 1) * sizeof *u);
            int got = FPDFText_GetText(tp, 0, count, u);
            char *s = utf16_to_utf8(u, got > 0 ? got - 1 : 0);
            text_join_hyphens(s);
            flatten(s);
            gzputs(g, s);
            free(s);
            free(u);
        }
        gzputc(g, '\n');
        for (int k = 0; p && k < FPDFPage_GetAnnotCount(p); k++) {
            FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, k);
            if (!a)
                continue;
            int sub = FPDFAnnot_GetSubtype(a);
            if (is_note(sub) && !(FPDFAnnot_GetFlags(a) & FPDF_ANNOT_FLAG_HIDDEN)) {
                char *comment = annot_string(a, "Contents");
                char *quote = sub == FPDF_ANNOT_HIGHLIGHT || sub == FPDF_ANNOT_UNDERLINE ||
                                      sub == FPDF_ANNOT_SQUIGGLY || sub == FPDF_ANNOT_STRIKEOUT
                                  ? covered(tp, a)
                                  : xstrdup("");
                text_join_hyphens(quote);
                flatten(comment);
                flatten(quote);
                char head[64];
                int hl = snprintf(head, sizeof head, "%d\t%d\t%06x\t", i + 1, sub, annot_color(a) & 0xFFFFFF);
                size_t add = (size_t)hl + strlen(quote) + strlen(comment) + 3;
                notes = xrealloc(notes, notes_len + add);
                notes_len += (size_t)snprintf(notes + notes_len, add, "%s%s\t%s\n", head, quote, comment);
                free(comment);
                free(quote);
            }
            FPDFPage_CloseAnnot(a);
        }
        if (tp)
            FPDFText_ClosePage(tp);
        if (p)
            FPDF_ClosePage(p);
    }
    gzputs(g, "\x01\n");
    gzputs(g, notes);
    free(notes);
    bool ok = gzclose(g) == Z_OK;
    FPDF_CloseDocument(doc);
    if (!ok || rename(tmp, gz)) {
        unlink(tmp);
        return -1;
    }
    return n;
}

/* npdf --index-worker DIR K: the files whose path hash % WORKERS is K. */
int library_worker(const char *dir, int part)
{
    errno = 0;
    (void)!nice(10); /* the reader comes first; failing only means full speed */
    mkdirs(dir);
    char manifest[4200], current[4200];
    snprintf(manifest, sizeof manifest, "%s/manifest.%d", dir, part);
    snprintf(current, sizeof current, "%s/current.%d", dir, part);
    int nknown;
    Known *known = read_manifest(manifest, &nknown);
    /* a PDF that crashed the last run is left out until it changes */
    char bad[4200] = "";
    FILE *cf = fopen(current, "r");
    if (cf) {
        if (fgets(bad, sizeof bad, cf))
            bad[strcspn(bad, "\n")] = 0;
        fclose(cf);
    }
    char **paths;
    long long *when;
    int n = pdf_scan(cfg.pdf_dirs, &paths, &when), mine = 0;
    for (int i = 0; i < n; i++)
        mine += (int)(fnv1a(paths[i]) % WORKERS) == part;
    FILE *out = fopen(manifest, "a");
    if (!out) {
        known_free(known, nknown);
        return 1;
    }
    int done = 0;
    uint64_t last_report = 0;
    for (int i = 0; i < n; i++) {
        if ((int)(fnv1a(paths[i]) % WORKERS) != part)
            continue;
        struct stat st;
        if (stat(paths[i], &st) != 0)
            continue;
        Known *k = known_find(known, nknown, paths[i]);
        char gz[4200];
        uint64_t h = fnv1a(paths[i]);
        snprintf(gz, sizeof gz, "%s/%016llx.gz", dir, (unsigned long long)h);
        bool fresh = k && k->mtime == (long long)st.st_mtime && k->size == (long long)st.st_size &&
                     (k->pages < 0 || access(gz, R_OK) == 0);
        if (!fresh) {
            int pages = -2;
            if (!strcmp(paths[i], bad) || st.st_size > MAX_FILE_BYTES) {
                pages = -2; /* crashed before, or too big: skipped until it changes */
            } else {
                FILE *c = fopen(current, "w");
                if (c) {
                    fprintf(c, "%s\n", paths[i]);
                    fclose(c);
                }
                pages = extract(paths[i], gz);
            }
            fprintf(out, "%016llx\t%lld\t%lld\t%d\t%s\n", (unsigned long long)h, (long long)st.st_mtime,
                    (long long)st.st_size, pages, paths[i]);
            fflush(out);
        }
        done++;
        uint64_t now = SDL_GetTicks();
        if (now - last_report > 200 || done == mine) {
            printf("P %d %d\n", done, mine);
            fflush(stdout);
            last_report = now;
        }
    }
    fclose(out);
    unlink(current);
    /* the manifest again, with only the files that still exist; orphans go */
    int nnow;
    Known *now = read_manifest(manifest, &nnow);
    char tmp[4300];
    snprintf(tmp, sizeof tmp, "%s.tmp", manifest);
    FILE *t = fopen(tmp, "w");
    for (int i = 0; t && i < nnow; i++) {
        bool alive = false;
        for (int j = 0; j < n && !alive; j++) /* n is small enough: this runs once per indexing */
            alive = paths[j][1] == now[i].path[1] && !strcmp(paths[j], now[i].path);
        uint64_t h = fnv1a(now[i].path);
        if (alive) {
            fprintf(t, "%016llx\t%lld\t%lld\t%d\t%s\n", (unsigned long long)h, now[i].mtime, now[i].size,
                    now[i].pages, now[i].path);
        } else {
            char gz[4200];
            snprintf(gz, sizeof gz, "%s/%016llx.gz", dir, (unsigned long long)h);
            unlink(gz);
        }
    }
    if (t && fclose(t) == 0)
        rename(tmp, manifest);
    known_free(known, nknown);
    known_free(now, nnow);
    for (int i = 0; i < n; i++)
        free(paths[i]);
    free(paths);
    free(when);
    printf("D\n");
    fflush(stdout);
    return 0;
}

/* ---------- the index, as the reader sees it ---------- */
typedef struct {
    char *path;
    long long mtime;
    char gz[4200];
} Entry;

typedef struct {
    Entry *e;
    int n;
    SDL_AtomicInt refs;
} Snapshot;

static SDL_Mutex *lock;      /* snapshot, shown results */
static Snapshot *snap;       /* the index files that can be searched */

static void snap_unref(Snapshot *s)
{
    if (!s || SDL_AddAtomicInt(&s->refs, -1) != 1)
        return;
    for (int i = 0; i < s->n; i++)
        free(s->e[i].path);
    free(s->e);
    free(s);
}

static void load_snapshot(void)
{
    char dir[4096];
    index_dir(dir, sizeof dir);
    Snapshot *s = xcalloc(1, sizeof *s);
    SDL_SetAtomicInt(&s->refs, 1);
    int cap = 0;
    for (int part = 0; part < WORKERS; part++) {
        char manifest[4200];
        snprintf(manifest, sizeof manifest, "%s/manifest.%d", dir, part);
        int n;
        Known *k = read_manifest(manifest, &n);
        for (int i = 0; i < n; i++) {
            if (k[i].pages <= 0) {
                free(k[i].path);
                continue;
            }
            if (s->n == cap)
                s->e = xrealloc(s->e, (size_t)(cap = cap ? cap * 2 : 1024) * sizeof *s->e);
            Entry *e = &s->e[s->n++];
            e->path = k[i].path;
            e->mtime = k[i].mtime;
            snprintf(e->gz, sizeof e->gz, "%s/%016llx.gz", dir, (unsigned long long)fnv1a(k[i].path));
        }
        free(k);
    }
    SDL_LockMutex(lock);
    Snapshot *old = snap;
    snap = s;
    SDL_UnlockMutex(lock);
    snap_unref(old);
}

/* ---------- the index workers, run from a thread ---------- */
static SDL_Thread *indexer;
static SDL_AtomicInt idx_running, idx_stop, idx_done[WORKERS], idx_total[WORKERS];
static SDL_Process *procs[WORKERS];
static SDL_Mutex *procs_lock;     /* procs: the thread starts and ends them, quitting kills them */
static uint64_t idx_finished_at;

static void post(int code)
{
    SDL_Event e = {0};
    e.type = ipc_event;
    e.user.code = code;
    SDL_PushEvent(&e);
}

static int indexer_run(void *arg)
{
    (void)arg;
    char self[4096], dir[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) {
        SDL_SetAtomicInt(&idx_running, 0);
        return 0;
    }
    self[n] = 0;
    index_dir(dir, sizeof dir);
    mkdirs(dir);
    int restarts[WORKERS] = {0};
    bool running[WORKERS] = {0};
    char lines[WORKERS][256];
    size_t len[WORKERS] = {0};
    SDL_LockMutex(procs_lock);
    for (int k = 0; k < WORKERS && !SDL_GetAtomicInt(&idx_stop); k++) {
        char part[12];
        snprintf(part, sizeof part, "%d", k);
        const char *args[] = {self, "--index-worker", dir, part, NULL};
        procs[k] = SDL_CreateProcess(args, true);
        running[k] = procs[k] != NULL;
    }
    SDL_UnlockMutex(procs_lock);
    uint64_t last_post = 0;
    for (;;) {
        bool any = false, heard = false;
        for (int k = 0; k < WORKERS; k++) {
            if (!running[k])
                continue;
            any = true;
            SDL_IOStream *io = SDL_GetProcessOutput(procs[k]);
            char buf[256];
            size_t got = io ? SDL_ReadIO(io, buf, sizeof buf) : 0;
            heard |= got > 0;
            for (size_t i = 0; i < got; i++) {
                if (buf[i] != '\n') {
                    if (len[k] < sizeof lines[k] - 1)
                        lines[k][len[k]++] = buf[i];
                    continue;
                }
                lines[k][len[k]] = 0;
                int d, t;
                if (sscanf(lines[k], "P %d %d", &d, &t) == 2) {
                    SDL_SetAtomicInt(&idx_done[k], d);
                    SDL_SetAtomicInt(&idx_total[k], t);
                }
                len[k] = 0;
            }
            if (got == 0 && io && SDL_GetIOStatus(io) == SDL_IO_STATUS_NOT_READY)
                continue;
            if (got == 0) { /* the worker has finished, or crashed on a PDF */
                int code = 0;
                SDL_LockMutex(procs_lock);
                SDL_WaitProcess(procs[k], true, &code);
                SDL_DestroyProcess(procs[k]);
                procs[k] = NULL;
                running[k] = false;
                if (code != 0 && !SDL_GetAtomicInt(&idx_stop) && ++restarts[k] < 20) { /* skips the bad PDF */
                    char part[12];
                    snprintf(part, sizeof part, "%d", k);
                    const char *args[] = {self, "--index-worker", dir, part, NULL};
                    procs[k] = SDL_CreateProcess(args, true);
                    running[k] = procs[k] != NULL;
                }
                SDL_UnlockMutex(procs_lock);
            }
        }
        uint64_t now = SDL_GetTicks();
        if (now - last_post > 1500) {
            post(IPC_INDEX);
            last_post = now;
        }
        if (!any)
            break;
        if (!heard)
            SDL_Delay(50); /* the workers' output does not block: wait a little */
    }
    SDL_SetAtomicInt(&idx_running, 0);
    post(IPC_INDEX);
    return 0;
}

static void index_start(void)
{
    if (!procs_lock)
        procs_lock = SDL_CreateMutex();
    if (SDL_GetAtomicInt(&idx_running))
        return;
    if (idx_finished_at && SDL_GetTicks() - idx_finished_at < 60000)
        return; /* just done: nothing has changed since */
    if (indexer)
        SDL_WaitThread(indexer, NULL);
    for (int k = 0; k < WORKERS; k++)
        SDL_SetAtomicInt(&idx_done[k], 0), SDL_SetAtomicInt(&idx_total[k], 0);
    SDL_SetAtomicInt(&idx_running, 1);
    SDL_SetAtomicInt(&idx_stop, 0);
    indexer = SDL_CreateThread(indexer_run, "indexer", NULL);
    if (!indexer)
        SDL_SetAtomicInt(&idx_running, 0);
}

/* ---------- searching ---------- */
typedef struct {
    int file, page, score;
    bool note;
    uint32_t color;
    char snip[240];
    unsigned char spans[6][2]; /* matched words in snip: start, length */
    int nspans;
} Hit;

typedef struct {
    Snapshot *snap;
    Hit *hits;
    int n;
    char query[256];
    bool notes, done;
    unsigned gen;
} Results;

static Results shown;        /* guarded by lock */
static SDL_AtomicInt gen;    /* the newest search */
static SDL_AtomicInt active; /* search threads running */

typedef struct {
    Snapshot *snap;
    char query[256];         /* lower case */
    char words[8][64];
    int nwords;
    bool notes;
    unsigned gen;
    SDL_AtomicInt left;      /* threads still running */
    SDL_Mutex *m;
    Hit *hits;
    int n, cap;
} Search;

static void lower(char *s)
{
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

/* A piece of text around pos, with the words marked. */
static void make_snippet(Hit *h, const Search *s, const char *text, const char *low, size_t pos, size_t len)
{
    size_t start = pos > 70 ? pos - 70 : 0;
    while (start > 0 && ((unsigned char)text[start] & 0xC0) == 0x80)
        start--;
    size_t end = SDL_min(len, start + sizeof h->snip - 8);
    while (end > start && end < len && ((unsigned char)text[end] & 0xC0) == 0x80)
        end--;
    int o = 0;
    if (start > 0)
        o = snprintf(h->snip, sizeof h->snip, "…");
    int base = o;
    memcpy(h->snip + o, text + start, end - start);
    o += (int)(end - start);
    h->snip[o] = 0;
    if (end < len && o + 4 < (int)sizeof h->snip)
        strcat(h->snip, "…");
    h->nspans = 0;
    for (int w = 0; w < s->nwords; w++) {
        size_t wl = strlen(s->words[w]);
        const char *p = strstr(low + start, s->words[w]); /* the first of each word is enough */
        int at = p ? base + (int)(p - low - start) : 0;
        if (p && (size_t)(p - low) + wl <= end && h->nspans < 6 && at + wl < 250) {
            h->spans[h->nspans][0] = (unsigned char)at;
            h->spans[h->nspans][1] = (unsigned char)wl;
            h->nspans++;
        }
    }
}

static bool all_words(const Search *s, const char *low)
{
    for (int w = 0; w < s->nwords; w++)
        if (!strstr(low, s->words[w]))
            return false;
    return true;
}

static void add_hit(Hit *h, Hit **local, int *n, int *cap)
{
    if (*n == *cap)
        *local = xrealloc(*local, (size_t)(*cap = *cap ? *cap * 2 : 64) * sizeof **local);
    (*local)[(*n)++] = *h;
}

/* One index file: the pages (or notes) where every word is. */
static void search_file(Search *s, int file, Hit **local, int *n, int *cap)
{
    gzFile g = gzopen(s->snap->e[file].gz, "rb");
    if (!g)
        return;
    size_t len = 0, room = 1 << 16;
    char *buf = xmalloc(room);
    for (;;) {
        if (room - len < 1 << 15)
            buf = xrealloc(buf, room *= 2);
        int got = gzread(g, buf + len, (unsigned)(room - len - 1));
        if (got <= 0)
            break;
        len += (size_t)got;
    }
    gzclose(g);
    buf[len] = 0;
    char *low = xmalloc(len + 1);
    memcpy(low, buf, len + 1);
    lower(low);
    int page = 1, found = 0;
    bool in_notes = false;
    for (size_t at = 0; at < len && found < HITS_PER_FILE;) {
        size_t end = at + strcspn(buf + at, "\n");
        buf[end] = low[end] = 0;
        char *line = buf + at, *lline = low + at;
        if (line[0] == '\x01' && !line[1]) {
            in_notes = true;
        } else if (!in_notes && !s->notes) {
            if (s->nwords && all_words(s, lline)) {
                Hit h = {.file = file, .page = page};
                char *phrase = strstr(lline, s->query);
                h.score = phrase ? 2 : 1;
                make_snippet(&h, s, line, lline, phrase ? (size_t)(phrase - lline) : (size_t)(strstr(lline, s->words[0]) - lline),
                             end - at);
                add_hit(&h, local, n, cap);
                found++;
            }
            page++;
        } else if (in_notes && s->notes) {
            /* page \t kind \t colour \t quote \t comment */
            char *f[5] = {line};
            int nf = 1;
            for (char *c = line; *c && nf < 5; c++)
                if (*c == '\t')
                    *c = 0, f[nf++] = c + 1;
            if (nf == 5) {
                char both[2048];
                snprintf(both, sizeof both, "%s%s%s%s%s", f[4], f[4][0] && f[3][0] ? " — " : "",
                         f[3][0] ? "“" : "", f[3], f[3][0] ? "”" : "");
                char lboth[2048];
                memcpy(lboth, both, sizeof both);
                lower(lboth);
                bool match = s->nwords ? all_words(s, lboth) : f[4][0] != 0; /* no words: every comment */
                if (match) {
                    Hit h = {.file = file, .page = atoi(f[0]) , .note = true,
                             .color = (uint32_t)strtoul(f[2], NULL, 16)};
                    char *phrase = s->nwords ? strstr(lboth, s->query) : NULL;
                    h.score = phrase ? 2 : 1;
                    size_t pos = s->nwords ? (size_t)((phrase ? phrase : strstr(lboth, s->words[0])) - lboth) : 0;
                    make_snippet(&h, s, both, lboth, pos, strlen(both));
                    add_hit(&h, local, n, cap);
                    found++;
                }
            }
        }
        at = end + 1;
    }
    free(low);
    free(buf);
}

static Snapshot *sort_snap; /* for qsort, under lock */

static int hit_cmp(const void *a, const void *b)
{
    const Hit *x = a, *y = b;
    const Snapshot *s = sort_snap;
    if (x->score != y->score)
        return y->score - x->score;
    if (x->file != y->file) {
        long long mx = s->e[x->file].mtime, my = s->e[y->file].mtime;
        if (mx != my)
            return mx < my ? 1 : -1; /* the newest files first */
        return x->file - y->file;
    }
    return x->page - y->page;
}


typedef struct {
    Search *s;
    int part;
} Job;

static void lib_search_free(Search *s)
{
    snap_unref(s->snap);
    free(s->hits);
    SDL_DestroyMutex(s->m);
    free(s);
}

static int lib_search_thread(void *arg)
{
    Job *j = arg; /* active was counted when it was started */
    Search *s = j->s;
    Hit *local = NULL;
    int n = 0, cap = 0;
    for (int i = j->part; i < s->snap->n && n < MAX_HITS; i += SEARCH_THREADS) {
        if ((unsigned)SDL_GetAtomicInt(&gen) != s->gen)
            break; /* a newer query */
        search_file(s, i, &local, &n, &cap);
    }
    SDL_LockMutex(s->m);
    if (n) {
        s->hits = xrealloc(s->hits, (size_t)(s->n + n) * sizeof *s->hits);
        memcpy(s->hits + s->n, local, (size_t)n * sizeof *local);
        s->n += n;
    }
    SDL_UnlockMutex(s->m);
    free(local);
    free(j);
    if (SDL_AddAtomicInt(&s->left, -1) != 1) {
        SDL_AddAtomicInt(&active, -1);
        return 0;
    }
    /* the last thread: sort and show, unless a newer search began */
    if ((unsigned)SDL_GetAtomicInt(&gen) == s->gen) {
        SDL_LockMutex(lock);
        sort_snap = s->snap;
        if (s->n)
            qsort(s->hits, (size_t)s->n, sizeof *s->hits, hit_cmp);
        free(shown.hits);
        snap_unref(shown.snap);
        shown = (Results){s->snap, s->hits, SDL_min(s->n, MAX_HITS), "", s->notes, true, s->gen};
        snprintf(shown.query, sizeof shown.query, "%s", s->query);
        SDL_AddAtomicInt(&s->snap->refs, 1);
        s->hits = NULL;
        SDL_UnlockMutex(lock);
        post(IPC_LIBRARY);
    }
    lib_search_free(s);
    SDL_AddAtomicInt(&active, -1);
    return 0;
}

static void lib_search_start(Window *w)
{
    unsigned g = (unsigned)SDL_AddAtomicInt(&gen, 1) + 1;
    SDL_LockMutex(lock);
    Snapshot *sn = snap;
    if (sn)
        SDL_AddAtomicInt(&sn->refs, 1);
    shown.done = false;
    SDL_UnlockMutex(lock);
    if (!sn)
        return;
    Search *s = xcalloc(1, sizeof *s);
    s->snap = sn;
    s->gen = g;
    s->notes = w->lib_notes;
    s->m = SDL_CreateMutex();
    snprintf(s->query, sizeof s->query, "%s", w->lib_query);
    flatten(s->query);
    lower(s->query);
    char q[256];
    snprintf(q, sizeof q, "%s", s->query);
    for (char *save = NULL, *t = strtok_r(q, " ", &save); t && s->nwords < 8; t = strtok_r(NULL, " ", &save))
        snprintf(s->words[s->nwords++], sizeof s->words[0], "%s", t);
    if (!s->notes && (strlen(s->query) < 2 || !s->nwords)) { /* too short to search the text */
        SDL_LockMutex(lock);
        free(shown.hits);
        snap_unref(shown.snap);
        shown = (Results){.gen = g, .done = true};
        SDL_UnlockMutex(lock);
        lib_search_free(s);
        return;
    }
    SDL_SetAtomicInt(&s->left, SEARCH_THREADS);
    for (int i = 0; i < SEARCH_THREADS; i++) {
        Job *j = xmalloc(sizeof *j);
        *j = (Job){s, i};
        SDL_AddAtomicInt(&active, 1);
        SDL_Thread *t = SDL_CreateThread(lib_search_thread, "search", j);
        if (t)
            SDL_DetachThread(t);
        else
            lib_search_thread(j); /* no thread: here, slowly */
    }
}

/* ---------- the window ---------- */
void library_open(Window *w, bool notes)
{
    if (!lock)
        lock = SDL_CreateMutex();
    if (!snap)
        load_snapshot();
    index_start();
    w->lib_return = w->mode == MODE_LIBRARY ? w->lib_return : w->mode;
    w->mode = MODE_LIBRARY;
    w->lib_notes = notes;
    w->lib_query[0] = 0;
    w->lib_sel = w->lib_first = 0;
    lib_search_start(w);
    w->redraw = true;
}

/* The workers reported, or a search finished. */
void library_event(int code)
{
    static uint64_t last_load;
    if (code == IPC_INDEX) {
        bool running = SDL_GetAtomicInt(&idx_running);
        uint64_t now = SDL_GetTicks();
        if (!running)
            idx_finished_at = now;
        if (!running || now - last_load > 4000) { /* newly indexed files become searchable */
            last_load = now;
            load_snapshot();
            for (int k = 0; k < nwindows; k++)
                if (windows[k]->mode == MODE_LIBRARY)
                    lib_search_start(windows[k]);
        }
    }
    for (int k = 0; k < nwindows; k++)
        if (windows[k]->mode == MODE_LIBRARY)
            windows[k]->redraw = true;
}

static void open_hit(Window *w)
{
    SDL_LockMutex(lock);
    if (!shown.done || w->lib_sel >= shown.n || !shown.snap) {
        SDL_UnlockMutex(lock);
        return;
    }
    Hit h = shown.hits[w->lib_sel];
    char *path = xstrdup(shown.snap->e[h.file].path);
    SDL_UnlockMutex(lock);
    w->mode = w->lib_return == MODE_LIBRARY ? MODE_NORMAL : w->lib_return;
    w->split_focus = false; /* the tab's pane shows it */
    int tab = -1;
    for (int t = 0; t < w->ntabs; t++)
        if (!strcmp(w->tabs[t]->doc->path, path))
            tab = t;
    if (tab >= 0) {
        w->cur = tab;
        update_title(w);
    } else if (!open_tab(w, path)) {
        free(path);
        return;
    }
    free(path);
    View *v = cur_view(w);
    int page = SDL_clamp(h.page - 1, 0, v->doc->npages - 1);
    jump_to(w, v, (Pos){page, 0});
    if (!h.note && w->lib_query[0]) { /* its words, highlighted on the pages */
        char q[256], best[256] = "";
        snprintf(q, sizeof q, "%s", w->lib_query);
        flatten(q);
        if (h.score == 2) {
            snprintf(best, sizeof best, "%s", q);
        } else {
            for (char *save = NULL, *t = strtok_r(q, " ", &save); t; t = strtok_r(NULL, " ", &save))
                if (strlen(t) > strlen(best))
                    snprintf(best, sizeof best, "%s", t);
        }
        snprintf(v->query, sizeof v->query, "%s", best);
        v->query_absent = false;
        v->match_page = -1;
        v->highlight_all = true;
    }
    ui_message(w, "p. %d   n N: the next / previous match", page + 1);
    w->redraw = true;
}

void library_key(Window *w, const char *tok)
{
    size_t n = strlen(w->lib_query);
    int count;
    SDL_LockMutex(lock);
    count = shown.done ? shown.n : 0;
    SDL_UnlockMutex(lock);
    w->redraw = true;
    if (!strcmp(tok, "<Esc>")) {
        w->mode = w->lib_return == MODE_LIBRARY ? MODE_NORMAL : w->lib_return;
    } else if (!strcmp(tok, "<Enter>")) {
        open_hit(w);
    } else if (!strcmp(tok, "<Tab>") || !strcmp(tok, "<S-Tab>")) {
        w->lib_notes = !w->lib_notes;
        w->lib_sel = w->lib_first = 0;
        lib_search_start(w);
    } else if (!strcmp(tok, "<Down>") || !strcmp(tok, "<C-j>") || !strcmp(tok, "<C-n>")) {
        w->lib_sel = SDL_min(w->lib_sel + 1, SDL_max(0, count - 1));
    } else if (!strcmp(tok, "<Up>") || !strcmp(tok, "<C-k>") || !strcmp(tok, "<C-p>")) {
        w->lib_sel = SDL_max(w->lib_sel - 1, 0);
    } else if (!strcmp(tok, "<C-d>") || !strcmp(tok, "<PageDown>")) {
        w->lib_sel = SDL_min(w->lib_sel + 10, SDL_max(0, count - 1));
    } else if (!strcmp(tok, "<C-u>")) {
        w->lib_query[0] = 0;
        w->lib_sel = w->lib_first = 0;
        lib_search_start(w);
    } else if (!strcmp(tok, "<BS>")) {
        while (n > 0 && ((unsigned char)w->lib_query[--n] & 0xC0) == 0x80)
            ;
        w->lib_query[n] = 0;
        w->lib_sel = w->lib_first = 0;
        lib_search_start(w);
    } else {
        library_text(w, !strcmp(tok, "<lt>") ? "<" : tok);
    }
}

void library_text(Window *w, const char *text)
{
    if (text[0] == '<' && text[1])
        return; /* another special key */
    size_t n = strlen(w->lib_query), t = strlen(text);
    if (n + t >= sizeof w->lib_query)
        return;
    memcpy(w->lib_query + n, text, t + 1);
    w->lib_sel = w->lib_first = 0;
    lib_search_start(w);
    w->redraw = true;
}

/* A snippet with its matched words in the accent colour, cut to width. */
static void draw_snippet(Window *w, float x, float y, float maxw, const Hit *h, uint32_t fg, uint32_t accent,
                         bool underline)
{
    float under = y + font_height(w->font) - SDL_max(1.0f, w->density), thick = SDL_max(1.0f, w->density);
    int marks[256] = {0}; /* 1 inside a match */
    for (int i = 0; i < h->nspans; i++)
        for (int k = h->spans[i][0]; k < h->spans[i][0] + h->spans[i][1] && k < 255; k++)
            marks[k] = 1;
    const char *s = h->snip;
    float cx = x;
    int at = 0, len = (int)strlen(s);
    while (at < len) {
        int kind = at < 255 ? marks[at] : 0, end = at;
        while (end < len && (end < 255 ? marks[end] : 0) == kind)
            end++;
        char piece[256];
        snprintf(piece, sizeof piece, "%.*s", end - at, s + at);
        float pw = (float)font_width(w->font, piece);
        if (cx + pw > x + maxw) { /* the rest does not fit */
            char cut[260];
            snprintf(cut, sizeof cut - 4, "%s", piece);
            font_fit(w->font, cut, (int)(x + maxw - cx));
            font_draw(w->font, cx, y, cut, kind ? accent : fg);
            return;
        }
        font_draw(w->font, cx, y, piece, kind ? accent : fg);
        if (kind && underline) /* on the selected row the colours are the selection's */
            ui_fill(w, cx, under, pw, thick, accent, 255);
        cx += pw;
        at = end;
    }
}

void library_draw(Window *w)
{
    const Theme *t = wtheme(w);
    int pad = (int)(12 * w->density), lh = font_height(w->font), row = lh * 2 + (int)(8 * w->density);
    float bw = (float)SDL_min(w->W - 2 * pad, (int)(900 * w->density));
    float bh = (w->H - top_of(w) - ui_bar_height(w)) * 0.85f;
    float x = (w->W - bw) / 2, y = top_of(w) + (w->H - top_of(w) - ui_bar_height(w) - bh) / 2;
    ui_fill(w, x, y, bw, bh, t->panel_bg, 255);
    ui_frame(w, x, y, bw, bh, SDL_max(1.0f, w->density), t->panel_header);
    char head[400];
    snprintf(head, sizeof head, "%s: %s", w->lib_notes ? "Search comments and highlights" : "Search all PDFs",
             w->lib_query);
    font_draw(w->font, x + pad, y + pad / 2.0f, head, t->panel_fg);
    ui_fill(w, x + pad + font_width(w->font, head) + 1, y + pad / 2.0f, SDL_max(2.0f, 2 * w->density), (float)lh,
            t->bar_cursor, 255);
    SDL_LockMutex(lock);
    int files = snap ? snap->n : 0;
    bool done = shown.done && shown.gen == (unsigned)SDL_GetAtomicInt(&gen);
    int count = done ? shown.n : 0;
    int idone = 0, itotal = 0;
    for (int k = 0; k < WORKERS; k++)
        idone += SDL_GetAtomicInt(&idx_done[k]), itotal += SDL_GetAtomicInt(&idx_total[k]);
    bool indexing = SDL_GetAtomicInt(&idx_running);
    char status[160];
    if (indexing && itotal)
        snprintf(status, sizeof status, "%s%d result%s   indexing %d / %d", done ? "" : "searching…  ", count,
                 count == 1 ? "" : "s", idone, itotal);
    else if (indexing)
        snprintf(status, sizeof status, "looking for PDFs…");
    else
        snprintf(status, sizeof status, "%s%d result%s in %d PDFs", done ? "" : "searching…  ", count,
                 count == 1 ? "" : "s", files);
    font_draw(w->font, x + bw - pad - font_width(w->font, status), y + pad / 2.0f, status, t->panel_header);
    float list_top = y + lh + pad * 1.5f, list_bottom = y + bh - lh - pad;
    ui_fill(w, x, list_top - pad / 2.0f, bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    ui_fill(w, x, list_bottom, bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    char foot[200];
    snprintf(foot, sizeof foot - 4, "type words (any order)   Tab %s   Ctrl-j Ctrl-k move   Enter open at the page   Esc close",
             w->lib_notes ? "search the text" : "search comments and highlights");
    font_fit(w->font, foot, (int)bw - 2 * pad);
    font_draw(w->font, x + pad, list_bottom + pad / 2.0f, foot, t->panel_header);
    if (!count) {
        const char *msg = !files && indexing ? "Reading your PDFs for the first time (a few minutes; you can keep searching)…"
                          : !files          ? "No PDF indexed: pdf_dirs in the config says where to look."
                          : !w->lib_notes && strlen(w->lib_query) < 2 ? "Type at least two letters."
                          : !done           ? "searching…"
                          : w->lib_notes && !w->lib_query[0] ? "No comments in your PDFs yet."
                                                             : "Nothing found.";
        font_draw(w->font, x + pad, list_top + pad / 2.0f, msg, t->panel_header);
        SDL_UnlockMutex(lock);
        return;
    }
    int rows = SDL_max(1, (int)((list_bottom - list_top) / row));
    w->lib_sel = SDL_clamp(w->lib_sel, 0, count - 1);
    if (w->lib_sel < w->lib_first)
        w->lib_first = w->lib_sel;
    if (w->lib_sel >= w->lib_first + rows)
        w->lib_first = w->lib_sel - rows + 1;
    for (int i = w->lib_first; i < count && i < w->lib_first + rows; i++) {
        const Hit *h = &shown.hits[i];
        float ry = list_top + (i - w->lib_first) * row;
        bool sel = i == w->lib_sel;
        if (sel)
            ui_fill(w, x + 2, ry, bw - 4, (float)row, t->panel_selected_bg, 255);
        uint32_t fg = sel ? t->panel_selected_fg : t->panel_fg, dim = sel ? t->panel_selected_fg : t->panel_header;
        const char *path = shown.snap->e[h->file].path, *slash = strrchr(path, '/');
        char name[300], page[32];
        snprintf(page, sizeof page, "p. %d", h->page);
        snprintf(name, sizeof name - 4, "%s", slash ? slash + 1 : path);
        int pw = font_width(w->font, page);
        float nx = x + pad;
        if (h->note) { /* the highlight's colour */
            float sq = lh * 0.6f;
            ui_fill(w, nx, ry + 3 * w->density + (lh - sq) / 2, sq, sq, h->color, 255);
            nx += sq + pad / 2.0f;
        }
        font_fit(w->font, name, (int)(x + bw - 2 * pad - pw - nx));
        font_draw(w->font, nx, ry + 3 * w->density, name, fg);
        font_draw(w->font, x + bw - pad - pw, ry + 3 * w->density, page, dim);
        draw_snippet(w, x + pad, ry + 3 * w->density + lh, bw - 2 * pad, h, dim, sel ? t->panel_selected_fg : t->tab_accent, sel);
    }
    SDL_UnlockMutex(lock);
}

void library_shutdown(void)
{
    SDL_SetAtomicInt(&idx_stop, 1);
    if (procs_lock) {
        SDL_LockMutex(procs_lock);
        for (int k = 0; k < WORKERS; k++)
            if (procs[k])
                SDL_KillProcess(procs[k], false); /* what is written stays; the rest next time */
        SDL_UnlockMutex(procs_lock);
    }
    if (indexer)
        SDL_WaitThread(indexer, NULL);
    indexer = NULL;
    SDL_AddAtomicInt(&gen, 1); /* searches stop at the next file */
    while (SDL_GetAtomicInt(&active))
        SDL_Delay(5);
    if (lock) {
        SDL_LockMutex(lock);
        free(shown.hits);
        snap_unref(shown.snap);
        shown = (Results){0};
        snap_unref(snap);
        snap = NULL;
        SDL_UnlockMutex(lock);
    }
}
