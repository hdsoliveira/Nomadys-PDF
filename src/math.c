/* Equations in comments: "$...$" and "$$...$$" typeset by the real LaTeX
 * (pdflatex, in the background) and drawn with PDFium, so they look like
 * the paper. Results are cached in ~/.cache/nomadys-pdf/math/, keyed by the
 * formula, so a formula is only typeset once, ever.
 *
 * Macros of your own go in ~/.config/nomadys-pdf/math.tex (the preamble). */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "nomadys.h"

#define MATH_JOBS 2          /* pdflatex processes at once */
#define MATH_TIMEOUT_MS 8000 /* a formula that loops forever is given up */
#define MATH_MAX 512         /* entries kept in memory */

extern char **environ;

typedef enum { M_QUEUED, M_RUNNING, M_READY, M_FAILED } MathState;

typedef struct {
    uint64_t key;
    char *src;
    bool display;
    MathState state;
    pid_t pid;
    uint64_t started;
    float ht, dp;            /* baseline split of the box, in TeX points */
    char err[160];
    /* one texture, for the renderer and scale it was last drawn at */
    SDL_Texture *tex;
    SDL_Renderer *ren;
    float scale;
    int tw, th;
    uint64_t used;
} MathEntry;

static MathEntry *entries;
static int nentries, running;
static uint64_t clock_used;
static char *cache_dir, *preamble;

/* ---------- files ---------- */
static uint64_t fnv(uint64_t h, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        h = (h ^ (unsigned char)s[i]) * 0x100000001b3ULL;
    return h;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    size_t len = 0, cap = 4096;
    char *s = xmalloc(cap);
    size_t r;
    while ((r = fread(s + len, 1, cap - len - 1, f)) > 0) {
        len += r;
        if (len + 1 == cap)
            s = xrealloc(s, cap *= 2);
    }
    s[len] = 0;
    fclose(f);
    return s;
}

static void mkdirs(char *path)
{
    for (char *p = path + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = 0;
        mkdir(path, 0700);
        *p = '/';
    }
    mkdir(path, 0700);
}

static void init(void)
{
    if (cache_dir)
        return;
    /* the formulas come from the PDF, which may not be yours: pdflatex must not read or write
     * files outside the TeX trees and its own folder (TeX Live's "paranoid" setting) */
    setenv("openin_any", "p", 1);
    setenv("openout_any", "p", 1);
    setenv("shell_escape", "f", 1);
    const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    char buf[4096];
    if (xdg && *xdg)
        snprintf(buf, sizeof buf, "%s/nomadys-pdf/math", xdg);
    else
        snprintf(buf, sizeof buf, "%s/.cache/nomadys-pdf/math", home ? home : "/tmp");
    cache_dir = xstrdup(buf);
    mkdirs(cache_dir);
    const char *cfgdir = getenv("XDG_CONFIG_HOME");
    if (cfgdir && *cfgdir)
        snprintf(buf, sizeof buf, "%s/nomadys-pdf/math.tex", cfgdir);
    else
        snprintf(buf, sizeof buf, "%s/.config/nomadys-pdf/math.tex", home ? home : "/tmp");
    preamble = read_file(buf);
    if (!preamble)
        preamble = xstrdup("");
}

static void entry_path(const MathEntry *e, const char *ext, char *out, size_t cap)
{
    snprintf(out, cap, "%s/%016llx%s", cache_dir, (unsigned long long)e->key, ext);
}

/* The scratch folder of a job; the pid keeps two npdf typesetting the same
 * formula from deleting each other's files. */
static void job_dir(const MathEntry *e, char *out, size_t cap)
{
    snprintf(out, cap, "%s/%016llx.%d.d", cache_dir, (unsigned long long)e->key, (int)getpid());
}

/* A formula typeset in an earlier session. */
static bool load_cached(MathEntry *e)
{
    char pdf[4200], dim[4200];
    entry_path(e, ".pdf", pdf, sizeof pdf);
    entry_path(e, ".dim", dim, sizeof dim);
    FILE *f = fopen(dim, "r");
    if (!f)
        return false;
    bool ok = fscanf(f, "%f %f", &e->ht, &e->dp) == 2 && access(pdf, R_OK) == 0;
    fclose(f);
    return ok;
}

/* ---------- typesetting ---------- */
static void start_job(MathEntry *e)
{
    char dir[4200], tex[4300];
    job_dir(e, dir, sizeof dir);
    mkdir(dir, 0700);
    snprintf(tex, sizeof tex, "%s/job.tex", dir);
    FILE *f = fopen(tex, "w");
    if (!f) {
        e->state = M_FAILED;
        snprintf(e->err, sizeof e->err, "cannot write in %s", cache_dir);
        return;
    }
    /* the box is measured so the formula sits on the text's baseline */
    fprintf(f,
            "\\documentclass[border=0pt]{standalone}\n"
            "\\usepackage{amsmath,amssymb}\n%s\n"
            "\\newsavebox\\nomadysbox\n"
            "\\begin{document}\n"
            "\\sbox\\nomadysbox{$%s%s$}%%\n"
            "\\typeout{NOMADYS:\\the\\ht\\nomadysbox:\\the\\dp\\nomadysbox}%%\n"
            "\\usebox\\nomadysbox\n"
            "\\end{document}\n",
            preamble, e->display ? "\\displaystyle " : "", e->src);
    fclose(f);
    char outdir[4300];
    snprintf(outdir, sizeof outdir, "-output-directory=%s", dir);
    char *argv[] = {"pdflatex", "-interaction=nonstopmode", "-halt-on-error", "-no-shell-escape", outdir, tex, NULL};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    int err = posix_spawnp(&e->pid, "pdflatex", &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (err) {
        e->state = M_FAILED;
        snprintf(e->err, sizeof e->err, "pdflatex not found: install TeX Live to see equations");
        return;
    }
    e->state = M_RUNNING;
    e->started = SDL_GetTicks();
    running++;
}

static void remove_job_dir(const MathEntry *e)
{
    static const char *files[] = {"job.tex", "job.log", "job.aux", "job.pdf"};
    char dir[4200], path[4300];
    job_dir(e, dir, sizeof dir);
    for (size_t i = 0; i < SDL_arraysize(files); i++) {
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        unlink(path);
    }
    rmdir(dir);
}

/* The first error line of the log, e.g. "Undefined control sequence." */
static void log_error(const char *log, char *out, size_t cap)
{
    const char *bang = log ? strstr(log, "\n! ") : NULL;
    if (!bang) {
        snprintf(out, cap, "LaTeX error");
        return;
    }
    bang += 3;
    snprintf(out, cap, "%.*s", (int)strcspn(bang, "\n"), bang);
}

static void finish_job(MathEntry *e, int status)
{
    running--;
    e->pid = 0;
    char dir[4200], log_path[4300], pdf[4300], dest[4200], dim[4200];
    job_dir(e, dir, sizeof dir);
    snprintf(log_path, sizeof log_path, "%s/job.log", dir);
    snprintf(pdf, sizeof pdf, "%s/job.pdf", dir);
    char *log = read_file(log_path);
    const char *m = log ? strstr(log, "NOMADYS:") : NULL;
    if (status == 0 && m && sscanf(m, "NOMADYS:%fpt:%fpt", &e->ht, &e->dp) == 2 && access(pdf, R_OK) == 0) {
        entry_path(e, ".pdf", dest, sizeof dest);
        entry_path(e, ".dim", dim, sizeof dim);
        FILE *f = fopen(dim, "w");
        if (f) {
            fprintf(f, "%g %g\n", e->ht, e->dp);
            fclose(f);
        }
        rename(pdf, dest);
        e->state = M_READY;
    } else {
        e->state = M_FAILED;
        if (status < 0)
            snprintf(e->err, sizeof e->err, "LaTeX took too long");
        else
            log_error(log, e->err, sizeof e->err);
    }
    free(log);
    remove_job_dir(e);
}

bool math_poll(void)
{
    bool changed = false;
    uint64_t now = SDL_GetTicks();
    for (int i = 0; i < nentries; i++) {
        MathEntry *e = &entries[i];
        if (e->state != M_RUNNING)
            continue;
        int st;
        pid_t r = waitpid(e->pid, &st, WNOHANG);
        if (r == e->pid) {
            finish_job(e, WIFEXITED(st) ? WEXITSTATUS(st) : 1);
            changed = true;
        } else if (now - e->started > MATH_TIMEOUT_MS) {
            kill(e->pid, SIGKILL);
            waitpid(e->pid, NULL, 0);
            finish_job(e, -1);
            changed = true;
        }
    }
    for (int i = 0; i < nentries && running < MATH_JOBS; i++)
        if (entries[i].state == M_QUEUED)
            start_job(&entries[i]);
    return changed;
}

bool math_busy(void) { return running > 0; }

/* ---------- lookup and drawing ---------- */
static void drop_texture(MathEntry *e)
{
    if (e->tex)
        SDL_DestroyTexture(e->tex);
    e->tex = NULL;
    e->ren = NULL;
}

static MathEntry *lookup(const char *src, size_t len, bool display)
{
    init();
    uint64_t key = fnv(fnv(0xcbf29ce484222325ULL, display ? "D" : "I", 1), src, len);
    key = fnv(key, preamble, strlen(preamble));
    for (int i = 0; i < nentries; i++)
        if (entries[i].key == key) {
            entries[i].used = ++clock_used;
            return &entries[i];
        }
    if (nentries == MATH_MAX) { /* forget the least recently used finished one */
        int old = -1;
        for (int i = 0; i < nentries; i++)
            if (entries[i].state >= M_READY && (old < 0 || entries[i].used < entries[old].used))
                old = i;
        if (old < 0)
            return NULL;
        drop_texture(&entries[old]);
        free(entries[old].src);
        entries[old] = entries[--nentries];
    }
    entries = xrealloc(entries, (size_t)(nentries + 1) * sizeof *entries);
    MathEntry *e = &entries[nentries++];
    memset(e, 0, sizeof *e);
    e->key = key;
    e->src = xmalloc(len + 1);
    memcpy(e->src, src, len);
    e->src[len] = 0;
    e->display = display;
    e->used = ++clock_used;
    e->state = load_cached(e) ? M_READY : M_QUEUED;
    if (e->state == M_QUEUED)
        math_poll(); /* start it now if a slot is free */
    return e;
}

/* White glyphs with the typeset coverage as alpha: tinted when drawn. */
static bool make_texture(MathEntry *e, SDL_Renderer *ren, float scale)
{
    drop_texture(e);
    char pdf[4200];
    entry_path(e, ".pdf", pdf, sizeof pdf);
    FPDF_DOCUMENT doc = FPDF_LoadDocument(pdf, NULL);
    FPDF_PAGE page = doc ? FPDF_LoadPage(doc, 0) : NULL;
    bool ok = false;
    /* an empty box gives a page without a size, which PDFium makes Letter */
    float expect = (e->ht + e->dp) * 72.0f / 72.27f, ph = page ? FPDF_GetPageHeightF(page) : 0;
    if (page && e->ht + e->dp > 0 && SDL_fabsf(ph - expect) < 2 + expect * 0.2f) {
        int w = (int)(FPDF_GetPageWidthF(page) * scale + 0.999f), h = (int)(ph * scale + 0.999f);
        /* drawn SS times larger, then averaged: at the size of a screen's
         * text (1-2 px per point) PDFium alone drops thin strokes (the top
         * bar of an "=", a fraction's line) and leaves the rest grey */
        int ss = scale < 3 ? 4 : 2, W = w * ss, H = h * ss;
        if (w > 0 && h > 0 && W < 16384 && H < 16384) {
            FPDF_BITMAP bmp = FPDFBitmap_Create(W, H, 1);
            uint8_t *out = bmp ? malloc((size_t)w * h * 4) : NULL;
            if (out) {
                FPDFBitmap_FillRect(bmp, 0, 0, W, H, 0x00000000);
                FPDF_RenderPageBitmap(bmp, page, 0, 0, (int)(FPDF_GetPageWidthF(page) * scale * ss + 0.5f),
                                      (int)(ph * scale * ss + 0.5f), 0, FPDF_ANNOT);
                const uint8_t *px = FPDFBitmap_GetBuffer(bmp);
                int stride = FPDFBitmap_GetStride(bmp);
                /* small type gets a little more weight, as light letters on
                 * a screen look thinner than on paper */
                float weight = scale < 2 ? 1.5f : scale < 3 ? 1.25f : 1.0f;
                uint8_t curve[256];
                for (int a = 0; a < 256; a++)
                    curve[a] = (uint8_t)(255.0f * (1 - SDL_powf(1 - a / 255.0f, weight)) + 0.5f);
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        int sum = 0;
                        for (int j = 0; j < ss; j++) {
                            const uint8_t *row = px + (size_t)(y * ss + j) * stride + (size_t)x * ss * 4 + 3;
                            for (int i = 0; i < ss; i++)
                                sum += row[i * 4];
                        }
                        uint8_t *o = out + ((size_t)y * w + x) * 4;
                        o[0] = o[1] = o[2] = 0xFF; /* white, tinted when drawn */
                        o[3] = curve[sum / (ss * ss)];
                    }
                e->tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC, w, h);
                if (e->tex) {
                    SDL_UpdateTexture(e->tex, NULL, out, w * 4);
                    SDL_SetTextureBlendMode(e->tex, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(e->tex, SDL_SCALEMODE_NEAREST);
                    e->ren = ren;
                    e->scale = scale;
                    e->tw = w;
                    e->th = h;
                    ok = true;
                }
            }
            free(out);
            if (bmp)
                FPDFBitmap_Destroy(bmp);
        }
    }
    if (page)
        FPDF_ClosePage(page);
    if (doc)
        FPDF_CloseDocument(doc);
    if (!ok) {
        e->state = M_FAILED;
        snprintf(e->err, sizeof e->err, e->ht + e->dp > 0 ? "cannot draw the typeset formula" : "empty formula");
    }
    return ok;
}

MathStatus math_get(SDL_Renderer *ren, const char *src, size_t len, bool display, float scale, MathImage *out)
{
    MathEntry *e = lookup(src, len, display);
    memset(out, 0, sizeof *out);
    if (!e || e->state == M_QUEUED || e->state == M_RUNNING)
        return MATH_PENDING;
    if (e->state == M_READY && (e->ren != ren || e->scale != scale || !e->tex))
        make_texture(e, ren, scale);
    if (e->state == M_FAILED) {
        out->error = e->err;
        return MATH_FAILED;
    }
    out->tex = e->tex;
    out->w = e->tw;
    out->h = e->th;
    float total = e->ht + e->dp;
    out->baseline = total > 0 ? (int)(e->th * e->ht / total + 0.5f) : e->th;
    return MATH_READY;
}

/* Textures belong to a renderer: drop them before it goes away. */
void math_forget_renderer(SDL_Renderer *ren)
{
    for (int i = 0; i < nentries; i++)
        if (entries[i].ren == ren)
            drop_texture(&entries[i]);
}

void math_shutdown(void)
{
    for (int i = 0; i < nentries; i++) {
        MathEntry *e = &entries[i];
        if (e->state == M_RUNNING) {
            kill(e->pid, SIGKILL);
            waitpid(e->pid, NULL, 0);
            remove_job_dir(e);
        }
        drop_texture(e);
        free(e->src);
    }
    free(entries);
    entries = NULL;
    nentries = running = 0;
    free(cache_dir);
    free(preamble);
    cache_dir = preamble = NULL;
}
