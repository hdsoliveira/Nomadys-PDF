/* Recent files and the session.
 *
 * recent:  every PDF opened, most recent first ($XDG_DATA_HOME/nomadys-pdf/recent)
 * session: the tabs open now, rewritten as they change, so "npdf" alone or
 *          :session brings them back, even after a crash
 * picker:  \o or :recent, a list of recent files filtered as you type
 *          (fuzzy: the letters in order, not necessarily together)
 * finder:  \f or :find, the same list over every PDF in pdf_dirs (the home
 *          folder by default), found by a thread and kept for next time */
#include <ctype.h>
#include <dirent.h>
#include <sys/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "nomadys.h"

#define RECENT_MAX 300

typedef struct {
    char *path;
    long long when;          /* seconds since 1970 */
} Recent;

static Recent *recent;
static int nrecent;
static bool loaded;

typedef struct {
    int index, score;
    long long when;
} Hit;

static Hit *hits;            /* the picker's list: recent files that match */
static int nhits;

static void data_path(const char *name, char *out, size_t cap)
{
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    char dir[4096];
    if (xdg && *xdg)
        snprintf(dir, sizeof dir, "%s/nomadys-pdf", xdg);
    else
        snprintf(dir, sizeof dir, "%s/.local/share/nomadys-pdf", home ? home : "/tmp");
    for (char *p = dir + 1; *p; p++) /* mkdir -p */
        if (*p == '/') {
            *p = 0;
            mkdir(dir, 0700);
            *p = '/';
        }
    mkdir(dir, 0700);
    snprintf(out, cap, "%s/%s", dir, name);
}

/* Writes through a temporary file, so a crash never leaves half a list. */
static FILE *open_new(const char *path, char *tmp, size_t cap)
{
    snprintf(tmp, cap, "%s.tmp%d", path, (int)getpid());
    return fopen(tmp, "w");
}

static void finish(FILE *f, const char *tmp, const char *path)
{
    bool ok = !ferror(f);
    ok &= fclose(f) == 0;
    if (!ok || rename(tmp, path))
        unlink(tmp);
}

static void load(void)
{
    if (loaded)
        return;
    loaded = true;
    char path[4200], line[4200];
    data_path("recent", path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    while (nrecent < RECENT_MAX && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        char *tab = strchr(line, '\t');
        if (!tab || tab[1] != '/')
            continue;
        recent = xrealloc(recent, (size_t)(nrecent + 1) * sizeof *recent);
        recent[nrecent++] = (Recent){xstrdup(tab + 1), atoll(line)};
    }
    fclose(f);
}

static void recent_write(void)
{
    char path[4200], tmp[4300];
    data_path("recent", path, sizeof path);
    FILE *f = open_new(path, tmp, sizeof tmp);
    if (!f)
        return;
    for (int i = 0; i < nrecent; i++)
        fprintf(f, "%lld\t%s\n", recent[i].when, recent[i].path);
    finish(f, tmp, path);
}

void recent_add(const char *path)
{
    char *real = realpath(path, NULL);
    if (!real)
        return;
    load();
    int at = -1;
    for (int i = 0; i < nrecent && at < 0; i++)
        if (!strcmp(recent[i].path, real))
            at = i;
    char *keep;
    if (at >= 0) { /* known: moves to the top */
        keep = recent[at].path;
        free(real);
    } else { /* new: the oldest goes when the list is full */
        if (nrecent == RECENT_MAX)
            free(recent[--nrecent].path);
        recent = xrealloc(recent, (size_t)(nrecent + 1) * sizeof *recent);
        at = nrecent++;
        keep = real;
    }
    memmove(recent + 1, recent, (size_t)at * sizeof *recent);
    recent[0] = (Recent){keep, (long long)time(NULL)};
    recent_write();
}

static void finder_shutdown(void);

void recent_shutdown(void)
{
    finder_shutdown();
    for (int i = 0; i < nrecent; i++)
        free(recent[i].path);
    free(recent);
    free(hits);
    recent = NULL;
    hits = NULL;
    nrecent = nhits = 0;
    loaded = false;
}

/* ---------- session ---------- */
static bool restoring;

void session_save(void)
{
    if (restoring)
        return;
    char path[4200], tmp[4300];
    data_path("session", path, sizeof path);
    FILE *f = open_new(path, tmp, sizeof tmp);
    if (!f)
        return;
    fprintf(f, "# nomadys-pdf session: the tabs open, * the one shown\n");
    for (int k = 0; k < nwindows; k++)
        for (int t = 0; t < windows[k]->ntabs; t++)
            fprintf(f, "%s%s\n", t == windows[k]->cur && k == 0 ? "*" : " ", windows[k]->tabs[t]->doc->path);
    finish(f, tmp, path);
}

/* The files of the last session that still exist; returns how many. */
int session_files(char ***out, int *current)
{
    char path[4200], line[4200];
    data_path("session", path, sizeof path);
    *out = NULL;
    *current = 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        if ((line[0] != '*' && line[0] != ' ') || line[1] != '/' || access(line + 1, R_OK))
            continue;
        if (line[0] == '*')
            *current = n;
        *out = xrealloc(*out, (size_t)(n + 1) * sizeof **out);
        (*out)[n++] = xstrdup(line + 1);
    }
    fclose(f);
    return n;
}

/* :session: the last session's tabs, in this window. */
void session_restore(Window *w)
{
    char **files;
    int current, n = session_files(&files, &current), opened = 0, show = -1;
    restoring = true;
    for (int i = 0; i < n; i++) {
        int tab = -1;
        for (int t = 0; t < w->ntabs; t++)
            if (!strcmp(w->tabs[t]->doc->path, files[i]))
                tab = t;
        if (tab < 0 && open_tab(w, files[i]))
            tab = w->ntabs - 1, opened++;
        if (i == current)
            show = tab;
        free(files[i]);
    }
    restoring = false;
    free(files);
    if (show >= 0)
        w->cur = show;
    session_save();
    ui_message(w, n ? "session: %d tab%s opened" : "no session saved yet", opened, opened == 1 ? "" : "s");
    w->redraw = true;
}

/* ---------- the finder: every PDF in pdf_dirs ---------- */
#define FIND_DEPTH 12
#define FIND_MAX 200000

typedef struct {
    char **path;
    long long *when;         /* modified, seconds since 1970 */
    int n, cap;
} Found;

static Found found;          /* shown by the picker; guarded by found_lock */
static SDL_Mutex *found_lock;
static SDL_Thread *finder;
static SDL_AtomicInt finder_busy, finder_stop;

static void found_add(Found *f, const char *path, long long when)
{
    if (f->n == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 256;
        f->path = xrealloc(f->path, (size_t)f->cap * sizeof *f->path);
        f->when = xrealloc(f->when, (size_t)f->cap * sizeof *f->when);
    }
    f->path[f->n] = xstrdup(path);
    f->when[f->n++] = when;
}

static void found_free(Found *f)
{
    for (int i = 0; i < f->n; i++)
        free(f->path[i]);
    free(f->path);
    free(f->when);
    *f = (Found){0};
}

static bool is_pdf(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".pdf");
}

/* Folders never worth a look: hidden ones, caches, code dependencies. */
static bool skip_dir(const char *name)
{
    static const char *const skip[] = {"node_modules", "__pycache__", "site-packages", "venv", "snap", "build",
                                       "target"};
    if (name[0] == '.')
        return true;
    for (size_t i = 0; i < SDL_arraysize(skip); i++)
        if (!strcmp(name, skip[i]))
            return true;
    return false;
}

/* A network or FUSE mount (rclone, sshfs, a cloud drive): reading it could
 * download every file, so it is left out. */
static bool remote(const char *dir)
{
    struct statfs fs;
    if (statfs(dir, &fs) != 0)
        return true;
    switch ((unsigned long)fs.f_type) {
    case 0x65735546: /* fuse */
    case 0x6969:     /* nfs */
    case 0xFF534D42: /* cifs */
    case 0xFE534D42: /* smb2 */
    case 0x517B:     /* smb */
        return true;
    default:
        return false;
    }
}

static void scan(const char *dir, int depth, Found *out, bool publish)
{
    if (depth > FIND_DEPTH || out->n >= FIND_MAX || SDL_GetAtomicInt(&finder_stop) || remote(dir))
        return;
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    char path[4200];
    while ((e = readdir(d)) && !SDL_GetAtomicInt(&finder_stop)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (e->d_type == DT_DIR) { /* links to folders are not followed: no loops */
            if (!skip_dir(e->d_name))
                scan(path, depth + 1, out, publish);
            continue;
        }
        struct stat st;
        if ((e->d_type == DT_REG || e->d_type == DT_LNK || e->d_type == DT_UNKNOWN) && is_pdf(e->d_name) &&
            stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            if (publish)
                SDL_LockMutex(found_lock);
            found_add(out, path, (long long)st.st_mtime);
            if (publish)
                SDL_UnlockMutex(found_lock);
        } else if (e->d_type == DT_UNKNOWN && stat(path, &st) == 0 && S_ISDIR(st.st_mode) && !skip_dir(e->d_name)) {
            scan(path, depth + 1, out, publish);
        }
    }
    closedir(d);
}

static void finder_post(void)
{
    if (!ipc_event)
        return; /* the index worker: no window to tell */
    SDL_Event e = {0};
    e.type = ipc_event;
    e.user.code = IPC_FINDER;
    SDL_PushEvent(&e);
}

static void found_write(const Found *f)
{
    char path[4200], tmp[4300];
    data_path("files", path, sizeof path);
    FILE *o = open_new(path, tmp, sizeof tmp);
    if (!o)
        return;
    for (int i = 0; i < f->n; i++)
        fprintf(o, "%lld\t%s\n", f->when[i], f->path[i]);
    finish(o, tmp, path);
}

/* Every folder of a pdf_dirs list ("~/Papers, ~/Books"). */
static void scan_dirs(const char *list, Found *out, bool publish)
{
    char dirs[sizeof cfg.pdf_dirs];
    snprintf(dirs, sizeof dirs, "%s", list);
    for (char *save = NULL, *d = strtok_r(dirs, ",:", &save); d; d = strtok_r(NULL, ",:", &save)) {
        while (*d == ' ')
            d++;
        for (char *end = d + strlen(d); end > d && end[-1] == ' ';)
            *--end = 0;
        if (!*d)
            continue;
        char *dir = expand_home(d);
        scan(dir, 0, out, publish);
        free(dir);
        if (publish)
            finder_post();
    }
}

/* The PDFs of pdf_dirs, for the index (library.c); returns how many. */
int pdf_scan(const char *dirs, char ***paths, long long **when)
{
    Found f = {0};
    scan_dirs(dirs, &f, false);
    *paths = f.path;
    *when = f.when;
    return f.n;
}

static int finder_run(void *arg)
{
    (void)arg;
    SDL_LockMutex(found_lock);
    bool publish = found.n == 0; /* nothing to show yet: show them as they come */
    SDL_UnlockMutex(found_lock);
    Found fresh = {0};
    scan_dirs(cfg.pdf_dirs, publish ? &found : &fresh, publish);
    if (!SDL_GetAtomicInt(&finder_stop)) {
        SDL_LockMutex(found_lock);
        if (!publish) {
            found_free(&found);
            found = fresh;
            fresh = (Found){0};
        }
        found_write(&found);
        SDL_UnlockMutex(found_lock);
    }
    found_free(&fresh);
    SDL_SetAtomicInt(&finder_busy, 0);
    finder_post();
    return 0;
}

/* The list of the last search, at once; then a new search in the background. */
static void finder_start(void)
{
    if (!found_lock)
        found_lock = SDL_CreateMutex();
    SDL_LockMutex(found_lock);
    if (!found.n) {
        char path[4200], line[4300];
        data_path("files", path, sizeof path);
        FILE *f = fopen(path, "r");
        while (f && found.n < FIND_MAX && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\n")] = 0;
            char *tab = strchr(line, '\t');
            if (tab && tab[1] == '/')
                found_add(&found, tab + 1, atoll(line));
        }
        if (f)
            fclose(f);
    }
    SDL_UnlockMutex(found_lock);
    if (SDL_GetAtomicInt(&finder_busy))
        return;
    if (finder) {
        SDL_WaitThread(finder, NULL); /* the last one has finished */
        finder = NULL;
    }
    SDL_SetAtomicInt(&finder_busy, 1);
    SDL_SetAtomicInt(&finder_stop, 0);
    finder = SDL_CreateThread(finder_run, "finder", NULL);
    if (!finder)
        SDL_SetAtomicInt(&finder_busy, 0);
}

static void finder_shutdown(void)
{
    SDL_SetAtomicInt(&finder_stop, 1);
    if (finder)
        SDL_WaitThread(finder, NULL);
    finder = NULL;
    found_free(&found);
    if (found_lock)
        SDL_DestroyMutex(found_lock);
    found_lock = NULL;
}

/* ---------- the picker ---------- */
/* Score of one word in text (letters in order, not necessarily together),
 * -1 if absent. Every place the first letter occurs is tried; letters
 * together and at word starts score more. */
static int word_score(const char *word, const char *text)
{
    int best = -1;
    for (const char *start = text; *start; start++) {
        if (tolower((unsigned char)*start) != tolower((unsigned char)word[0]))
            continue;
        int score = 0, last = -2;
        const char *p = start;
        bool ok = true;
        for (const char *c = word; *c && ok; c++) {
            while (*p && tolower((unsigned char)*p) != tolower((unsigned char)*c))
                p++;
            if (!*p) {
                ok = false;
                break;
            }
            int at = (int)(p - text);
            score += 10;
            if (at == last + 1)
                score += 15; /* together */
            if (p == text || strchr("/_-. ", p[-1]))
                score += 12; /* start of a word */
            last = at;
            p++;
        }
        if (ok && score > best)
            best = score;
    }
    return best;
}

/* Fuzzy score of a path for a query, -1 if a word is missing. Spaces
 * separate words, in any order; a word found in the file name counts more
 * than one found only in the folders. */
static int fuzzy(const char *query, const char *path)
{
    const char *base = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    int total = 0;
    char word[256];
    for (const char *q = query; *q;) {
        while (*q == ' ')
            q++;
        size_t n = strcspn(q, " ");
        if (!n)
            break;
        snprintf(word, sizeof word, "%.*s", (int)SDL_min(n, sizeof word - 1), q);
        q += n;
        int in_name = word_score(word, base), anywhere = word_score(word, path);
        if (in_name < 0 && anywhere < 0)
            return -1;
        total += in_name >= 0 ? in_name + 40 : anywhere;
    }
    return total;
}

static int hit_cmp(const void *a, const void *b)
{
    const Hit *x = a, *y = b;
    if (x->score != y->score)
        return y->score - x->score;
    if (x->when != y->when) /* then the most recent */
        return x->when < y->when ? 1 : -1;
    return x->index - y->index;
}

/* The picker's list: the recent files, or (the finder) every PDF found. */
static void list_lock(Window *w)
{
    if (w->picker_files)
        SDL_LockMutex(found_lock);
}

static void list_unlock(Window *w)
{
    if (w->picker_files)
        SDL_UnlockMutex(found_lock);
}

static int list_count(Window *w) { return w->picker_files ? found.n : nrecent; }
static const char *list_path(Window *w, int i) { return w->picker_files ? found.path[i] : recent[i].path; }
static long long list_when(Window *w, int i) { return w->picker_files ? found.when[i] : recent[i].when; }

/* Every letter of the query, in order, somewhere in s (spaces aside): a
 * quick test before scoring, for long lists. */
static bool letters_in(const char *query, const char *s)
{
    for (const char *q = query; *q; q++) {
        if (*q == ' ')
            continue;
        while (*s && tolower((unsigned char)*s) != tolower((unsigned char)*q))
            s++;
        if (!*s)
            return false;
        s++;
    }
    return true;
}

static void filter(Window *w)
{
    load();
    list_lock(w);
    int n = list_count(w);
    free(hits);
    hits = xmalloc((size_t)(n ? n : 1) * sizeof *hits);
    nhits = 0;
    for (int i = 0; i < n; i++) {
        const char *path = list_path(w, i);
        if (!w->picker_files && access(path, R_OK))
            continue; /* moved or deleted */
        if (w->picker_query[0] && w->picker_files && !letters_in(w->picker_query, path))
            continue;
        int s = w->picker_query[0] ? fuzzy(w->picker_query, path) : 0;
        if (s >= 0) /* recent files stay in their order; found ones newest first */
            hits[nhits++] = (Hit){i, s, w->picker_files ? list_when(w, i) : 0};
    }
    list_unlock(w);
    qsort(hits, (size_t)nhits, sizeof *hits, hit_cmp);
    w->picker_sel = SDL_clamp(w->picker_sel, 0, SDL_max(0, nhits - 1));
}

void picker_open(Window *w, bool files)
{
    w->picker_return = w->mode == MODE_PICKER ? w->picker_return : w->mode;
    w->picker_query[0] = 0;
    w->picker_sel = w->picker_first = 0;
    w->picker_files = files;
    w->mode = MODE_PICKER;
    if (files)
        finder_start();
    filter(w);
    w->redraw = true;
}

/* The finder found more (or finished): the list again, same query. */
void picker_refresh(void)
{
    for (int k = 0; k < nwindows; k++)
        if (windows[k]->mode == MODE_PICKER && windows[k]->picker_files) {
            filter(windows[k]);
            windows[k]->redraw = true;
        }
}

static void open_hit(Window *w)
{
    if (!nhits)
        return;
    list_lock(w);
    char *path = xstrdup(list_path(w, hits[w->picker_sel].index));
    list_unlock(w);
    w->mode = w->picker_return == MODE_PICKER ? MODE_NORMAL : w->picker_return;
    for (int t = 0; t < w->ntabs; t++)
        if (!strcmp(w->tabs[t]->doc->path, path)) { /* already open: go there */
            w->cur = t;
            update_title(w);
            w->redraw = true;
            free(path);
            return;
        }
    open_tab(w, path); /* a copy: open_tab updates the list */
    free(path);
}

void picker_key(Window *w, const char *tok)
{
    size_t n = strlen(w->picker_query);
    w->redraw = true;
    if (!strcmp(tok, "<Esc>")) {
        w->mode = w->picker_return;
    } else if (!strcmp(tok, "<Enter>")) {
        open_hit(w);
    } else if (!strcmp(tok, "<Down>") || !strcmp(tok, "<C-j>") || !strcmp(tok, "<C-n>") || !strcmp(tok, "<Tab>")) {
        w->picker_sel = SDL_min(w->picker_sel + 1, SDL_max(0, nhits - 1));
    } else if (!strcmp(tok, "<Up>") || !strcmp(tok, "<C-k>") || !strcmp(tok, "<C-p>") || !strcmp(tok, "<S-Tab>")) {
        w->picker_sel = SDL_max(w->picker_sel - 1, 0);
    } else if (!strcmp(tok, "<BS>")) {
        while (n > 0 && ((unsigned char)w->picker_query[--n] & 0xC0) == 0x80)
            ;
        w->picker_query[n] = 0;
        w->picker_sel = 0;
        filter(w);
    } else if (!strcmp(tok, "<C-u>")) {
        w->picker_query[0] = 0;
        w->picker_sel = 0;
        filter(w);
    } else {
        picker_text(w, !strcmp(tok, "<lt>") ? "<" : tok);
    }
}

void picker_text(Window *w, const char *text)
{
    if (text[0] == '<' && text[1])
        return; /* another special key */
    size_t n = strlen(w->picker_query), t = strlen(text);
    if (n + t >= sizeof w->picker_query)
        return;
    memcpy(w->picker_query + n, text, t + 1);
    w->picker_sel = 0;
    filter(w);
    w->redraw = true;
}

static void ago(long long when, char *out, size_t cap)
{
    long long s = (long long)time(NULL) - when;
    if (s < 60)
        snprintf(out, cap, "now");
    else if (s < 3600)
        snprintf(out, cap, "%lld min", s / 60);
    else if (s < 86400)
        snprintf(out, cap, "%lld h", s / 3600);
    else
        snprintf(out, cap, "%lld d", s / 86400);
}

void picker_draw(Window *w)
{
    const Theme *t = uitheme(w);
    int pad = (int)(12 * w->density), lh = font_height(w->font), row = lh * 2 + (int)(8 * w->density);
    float bw = (float)SDL_min(w->W - 2 * pad, (int)(820 * w->density));
    float bh = (w->H - top_of(w) - ui_bar_height(w)) * 0.8f;
    float x = (w->W - bw) / 2, y = top_of(w) + (w->H - top_of(w) - ui_bar_height(w) - bh) / 2;
    ui_fill(w, x, y, bw, bh, t->panel_bg, 255);
    ui_frame(w, x, y, bw, bh, SDL_max(1.0f, w->density), t->panel_header);
    /* the query line */
    char head[300];
    snprintf(head, sizeof head, "%s: %s", w->picker_files ? "Find a PDF" : "Open recent", w->picker_query);
    font_draw(w->font, x + pad, y + pad / 2.0f, head, t->panel_fg);
    ui_fill(w, x + pad + font_width(w->font, head) + 1, y + pad / 2.0f, SDL_max(2.0f, 2 * w->density), (float)lh,
            t->bar_cursor, 255);
    char count[64];
    bool busy = w->picker_files && SDL_GetAtomicInt(&finder_busy);
    snprintf(count, sizeof count, "%s%d file%s", busy ? "searching\u2026  " : "", nhits, nhits == 1 ? "" : "s");
    font_draw(w->font, x + bw - pad - font_width(w->font, count), y + pad / 2.0f, count, t->panel_header);
    float list_top = y + lh + pad * 1.5f, list_bottom = y + bh - lh - pad;
    ui_fill(w, x, list_top - pad / 2.0f, bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    ui_fill(w, x, list_bottom, bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    char foot[160];
    snprintf(foot, sizeof foot - 4, "type to filter   Ctrl-j Ctrl-k move   Enter open   Esc close");
    font_fit(w->font, foot, (int)bw - 2 * pad);
    font_draw(w->font, x + pad, list_bottom + pad / 2.0f, foot, t->panel_header);
    if (!nhits) {
        list_lock(w);
        const char *none = list_count(w) ? "no file matches"
                           : w->picker_files ? (busy ? "searching\u2026" : "no PDF found (pdf_dirs in the config)")
                                             : "no recent files yet";
        list_unlock(w);
        font_draw(w->font, x + pad, list_top + pad / 2.0f, none, t->panel_header);
        return;
    }
    int rows = SDL_max(1, (int)((list_bottom - list_top) / row));
    if (w->picker_sel < w->picker_first)
        w->picker_first = w->picker_sel;
    if (w->picker_sel >= w->picker_first + rows)
        w->picker_first = w->picker_sel - rows + 1;
    const char *home = getenv("HOME");
    size_t hl = home ? strlen(home) : 0;
    list_lock(w);
    for (int i = w->picker_first; i < nhits && i < w->picker_first + rows; i++) {
        if (hits[i].index >= list_count(w))
            continue;
        const Recent r_ = {(char *)list_path(w, hits[i].index), list_when(w, hits[i].index)}, *r = &r_;
        float ry = list_top + (i - w->picker_first) * row;
        bool sel = i == w->picker_sel;
        if (sel)
            ui_fill(w, x + 2, ry, bw - 4, (float)row, t->panel_selected_bg, 255);
        uint32_t fg = sel ? t->panel_selected_fg : t->panel_fg, dim = sel ? t->panel_selected_fg : t->panel_header;
        const char *slash = strrchr(r->path, '/');
        char name[300], dir[4200], when[32];
        snprintf(name, sizeof name - 4, "%s", slash ? slash + 1 : r->path);
        int dl = slash ? (int)(slash - r->path) : 0;
        if (hl && !strncmp(r->path, home, hl) && (size_t)dl >= hl)
            snprintf(dir, sizeof dir - 4, "~%.*s", dl - (int)hl, r->path + hl);
        else
            snprintf(dir, sizeof dir - 4, "%.*s", dl, r->path);
        ago(r->when, when, sizeof when);
        int ww = font_width(w->font, when);
        font_fit(w->font, name, (int)bw - 3 * pad - ww);
        font_fit(w->font, dir, (int)bw - 2 * pad);
        font_draw(w->font, x + pad, ry + 3 * w->density, name, fg);
        font_draw(w->font, x + bw - pad - ww, ry + 3 * w->density, when, dim);
        font_draw(w->font, x + pad, ry + 3 * w->density + lh, dir, dim);
    }
    list_unlock(w);
}
