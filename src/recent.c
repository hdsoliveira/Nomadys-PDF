/* Recent files and the session.
 *
 * recent:  every PDF opened, most recent first ($XDG_DATA_HOME/nomadys-pdf/recent)
 * session: the tabs open now, rewritten as they change, so "npdf" alone or
 *          :session brings them back, even after a crash
 * picker:  \o or :recent, a list of recent files filtered as you type
 *          (fuzzy: the letters in order, not necessarily together) */
#include <ctype.h>
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

void recent_shutdown(void)
{
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
    return x->index - y->index; /* then the most recent */
}


static void filter(Window *w)
{
    load();
    free(hits);
    hits = xmalloc((size_t)(nrecent ? nrecent : 1) * sizeof *hits);
    nhits = 0;
    for (int i = 0; i < nrecent; i++) {
        if (access(recent[i].path, R_OK))
            continue; /* moved or deleted */
        int s = w->picker_query[0] ? fuzzy(w->picker_query, recent[i].path) : 0;
        if (s >= 0)
            hits[nhits++] = (Hit){i, s};
    }
    qsort(hits, (size_t)nhits, sizeof *hits, hit_cmp);
    w->picker_sel = SDL_clamp(w->picker_sel, 0, SDL_max(0, nhits - 1));
}

void picker_open(Window *w)
{
    w->picker_return = w->mode == MODE_PICKER ? w->picker_return : w->mode;
    w->picker_query[0] = 0;
    w->picker_sel = w->picker_first = 0;
    w->mode = MODE_PICKER;
    filter(w);
    w->redraw = true;
}

static void open_hit(Window *w)
{
    if (!nhits)
        return;
    const char *path = recent[hits[w->picker_sel].index].path;
    w->mode = w->picker_return == MODE_PICKER ? MODE_NORMAL : w->picker_return;
    for (int t = 0; t < w->ntabs; t++)
        if (!strcmp(w->tabs[t]->doc->path, path)) { /* already open: go there */
            w->cur = t;
            update_title(w);
            w->redraw = true;
            return;
        }
    char *copy = xstrdup(path); /* open_tab updates the list */
    open_tab(w, copy);
    free(copy);
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
    const Theme *t = wtheme(w);
    int pad = (int)(12 * w->density), lh = font_height(w->font), row = lh * 2 + (int)(8 * w->density);
    float bw = (float)SDL_min(w->W - 2 * pad, (int)(820 * w->density));
    float bh = (w->H - top_of(w) - ui_bar_height(w)) * 0.8f;
    float x = (w->W - bw) / 2, y = top_of(w) + (w->H - top_of(w) - ui_bar_height(w) - bh) / 2;
    ui_fill(w, x, y, bw, bh, t->panel_bg, 255);
    ui_frame(w, x, y, bw, bh, SDL_max(1.0f, w->density), t->panel_header);
    /* the query line */
    char head[300];
    snprintf(head, sizeof head, "Open recent: %s", w->picker_query);
    font_draw(w->font, x + pad, y + pad / 2.0f, head, t->panel_fg);
    ui_fill(w, x + pad + font_width(w->font, head) + 1, y + pad / 2.0f, SDL_max(2.0f, 2 * w->density), (float)lh,
            t->bar_cursor, 255);
    char count[48];
    snprintf(count, sizeof count, "%d file%s", nhits, nhits == 1 ? "" : "s");
    font_draw(w->font, x + bw - pad - font_width(w->font, count), y + pad / 2.0f, count, t->panel_header);
    float list_top = y + lh + pad * 1.5f, list_bottom = y + bh - lh - pad;
    ui_fill(w, x, list_top - pad / 2.0f, bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    ui_fill(w, x, list_bottom, bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    char foot[160];
    snprintf(foot, sizeof foot - 4, "type to filter   Ctrl-j Ctrl-k move   Enter open   Esc close");
    font_fit(w->font, foot, (int)bw - 2 * pad);
    font_draw(w->font, x + pad, list_bottom + pad / 2.0f, foot, t->panel_header);
    if (!nhits) {
        font_draw(w->font, x + pad, list_top + pad / 2.0f, nrecent ? "no file matches" : "no recent files yet",
                  t->panel_header);
        return;
    }
    int rows = SDL_max(1, (int)((list_bottom - list_top) / row));
    if (w->picker_sel < w->picker_first)
        w->picker_first = w->picker_sel;
    if (w->picker_sel >= w->picker_first + rows)
        w->picker_first = w->picker_sel - rows + 1;
    const char *home = getenv("HOME");
    size_t hl = home ? strlen(home) : 0;
    for (int i = w->picker_first; i < nhits && i < w->picker_first + rows; i++) {
        const Recent *r = &recent[hits[i].index];
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
}
