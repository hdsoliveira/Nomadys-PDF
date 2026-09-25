/* SyncTeX (LaTeX <-> PDF) through TeX Live's `synctex` command:
 * forward search (tex line -> PDF spot) and inverse search (PDF spot -> editor).
 * SyncTeX coordinates are points from the top-left corner of the page. */
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "nomadys.h"

extern char **environ;

bool synctex_available(Doc *d)
{
    size_t n = strlen(d->path);
    if (n < 4 || strcmp(d->path + n - 4, ".pdf"))
        return false;
    char path[4200];
    struct stat st;
    snprintf(path, sizeof path, "%.*s.synctex.gz", (int)(n - 4), d->path);
    if (stat(path, &st) == 0)
        return true;
    snprintf(path, sizeof path, "%.*s.synctex", (int)(n - 4), d->path);
    return stat(path, &st) == 0;
}

/* Runs `synctex` with two arguments and returns its standard output. */
static char *run_synctex(const char *mode, const char *flag1, const char *arg1, const char *flag2,
                         const char *arg2)
{
    int fd[2];
    if (pipe(fd))
        return NULL;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, fd[0]);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    char *argv[] = {"synctex", (char *)mode, (char *)flag1, (char *)arg1, (char *)flag2, (char *)arg2, NULL};
    if (!flag2)
        argv[4] = NULL;
    pid_t pid;
    int err = posix_spawnp(&pid, "synctex", &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fd[1]);
    if (err) {
        close(fd[0]);
        return NULL;
    }
    size_t len = 0, cap = 4096;
    char *out = malloc(cap);
    for (;;) {
        if (len + 1024 > cap)
            out = realloc(out, cap *= 2);
        ssize_t r = read(fd[0], out + len, cap - len - 1);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        len += (size_t)r;
    }
    out[len] = 0;
    close(fd[0]);
    waitpid(pid, NULL, 0);
    return out;
}

/* Value after "Key:" at the start of a line of synctex output; NULL if absent. */
static const char *field(const char *text, const char *key)
{
    size_t n = strlen(key);
    for (const char *p = text; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL)
        if (!strncmp(p, key, n) && p[n] == ':')
            return p + n + 1;
    return NULL;
}

/* Runs the editor command in the background, detached from us. */
static void spawn_editor(const char *cmd)
{
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() == 0) {
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0)
        waitpid(pid, NULL, 0);
}

/* %l, %c, %f in the configured editor command; the file keeps its quoting. */
static void editor_command(char *out, size_t cap, const char *file, int line, int col)
{
    const char *t = cfg.synctex_editor;
    size_t n = 0;
    while (*t && n + 1 < cap) {
        char buf[32];
        const char *rep = NULL;
        if (t[0] == '%' && (t[1] == 'l' || t[1] == 'c' || t[1] == 'f')) {
            if (t[1] == 'f') {
                rep = file;
            } else {
                snprintf(buf, sizeof buf, "%d", t[1] == 'l' ? line : SDL_max(col, 0));
                rep = buf;
            }
            t += 2;
            size_t k = strlen(rep);
            if (n + k >= cap)
                break;
            memcpy(out + n, rep, k);
            n += k;
        } else {
            out[n++] = *t++;
        }
    }
    out[n] = 0;
}

bool synctex_inverse(Window *w, View *v, int page, double x, double y)
{
    Doc *d = v->doc;
    if (!synctex_available(d)) {
        ui_message(w, "sem SyncTeX: compile com -synctex=1");
        return false;
    }
    FPDF_PAGE p = doc_page(d, page);
    if (!p)
        return false;
    /* page space -> points from the top-left corner, as SyncTeX expects */
    int dx, dy, pw = (int)(d->pw[page] * 8), ph = (int)(d->ph[page] * 8);
    FPDF_PageToDevice(p, 0, 0, pw, ph, 0, x, y, &dx, &dy);
    char spot[4300];
    snprintf(spot, sizeof spot, "%d:%.2f:%.2f:%s", page + 1, dx / 8.0, dy / 8.0, d->path);
    char *out = run_synctex("edit", "-o", spot, NULL, NULL);
    const char *input = out ? field(out, "Input") : NULL, *line = out ? field(out, "Line") : NULL;
    if (!input || !line) {
        ui_message(w, out ? "SyncTeX não achou este ponto no código" : "comando synctex não encontrado");
        free(out);
        return false;
    }
    char file[4096];
    snprintf(file, sizeof file, "%.*s", (int)strcspn(input, "\n"), input);
    const char *colp = field(out, "Column");
    int ln = atoi(line), col = colp ? atoi(colp) : 0;
    free(out);
    char cmd[8192];
    editor_command(cmd, sizeof cmd, file, ln, col);
    spawn_editor(cmd);
    const char *base = strrchr(file, '/');
    ui_message(w, "%s:%d", base ? base + 1 : file, ln);
    return true;
}

bool synctex_forward(Window *w, View *v, const char *tex, int line, int col)
{
    Doc *d = v->doc;
    char input[4200];
    snprintf(input, sizeof input, "%d:%d:%s", line, SDL_max(col, 0), tex);
    char *out = run_synctex("view", "-i", input, "-o", d->path);
    const char *page = out ? field(out, "Page") : NULL;
    if (!page) {
        ui_message(w, out ? "SyncTeX: linha %d não encontrada no PDF" : "comando synctex não encontrado", line);
        free(out);
        return false;
    }
    int pg = atoi(page) - 1;
    const char *h = field(out, "h"), *vv = field(out, "v"), *W = field(out, "W"), *H = field(out, "H");
    double left = h ? atof(h) : 0, base = vv ? atof(vv) : 0, wd = W ? atof(W) : 0, ht = H ? atof(H) : 12;
    free(out);
    if (pg < 0 || pg >= d->npages)
        return false;
    FPDF_PAGE p = doc_page(d, pg);
    if (!p)
        return false;
    /* points from the top-left corner -> page space */
    int pw = (int)(d->pw[pg] * 8), ph = (int)(d->ph[pg] * 8);
    double x0, y0, x1, y1;
    FPDF_DeviceToPage(p, 0, 0, pw, ph, 0, (int)(left * 8), (int)((base - ht) * 8), &x0, &y0);
    FPDF_DeviceToPage(p, 0, 0, pw, ph, 0, (int)((left + SDL_max(wd, 20)) * 8), (int)(base * 8 + 16), &x1, &y1);
    PRect r = {(float)SDL_min(x0, x1), (float)SDL_max(y0, y1), (float)SDL_max(x0, x1), (float)SDL_min(y0, y1)};
    Pos before = view_pos(v);
    view_show_rect(v, pg, r, w->W, w->H, top_of(w));
    Pos after = view_pos(v);
    view_set_pos(v, before);
    jump_to(w, v, after);
    v->flash_page = pg;
    v->flash_rect = r;
    v->flash_until = SDL_GetTicks() + 1500;
    w->redraw = true;
    return true;
}
