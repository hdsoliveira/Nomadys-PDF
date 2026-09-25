/* The ':' command line: the command table, running a line, and Tab
 * completion of command names and file paths (vim-like wildmenu).
 *
 * To add a command, add a line to `commands` with its action and the
 * description shown on the help screen and in the completion list. */
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "nomadys.h"

typedef void (*CommandFn)(Window *w, View *v, const char *arg);

enum { ARG_NONE, ARG_FILE, ARG_NUMBER };

typedef struct {
    const char *name;
    int arg;
    CommandFn fn;
    const char *help;
} Command;

static void c_write(Window *w, View *v, const char *a) { (void)a; save(w, v); }

static void c_quit(Window *w, View *v, const char *a)
{
    (void)a;
    if (confirm_close(w, v))
        close_tab(w, w->cur);
}

static void c_quit_force(Window *w, View *v, const char *a) { (void)v, (void)a; close_tab(w, w->cur); }

static void c_write_quit(Window *w, View *v, const char *a)
{
    (void)a;
    save(w, v);
    close_tab(w, w->cur);
}

static char *expand_home(const char *path)
{
    const char *home = getenv("HOME");
    char *out = malloc(strlen(path) + (home ? strlen(home) : 0) + 1);
    if (path[0] == '~' && home)
        sprintf(out, "%s%s", home, path + 1);
    else
        strcpy(out, path);
    return out;
}

static void c_edit(Window *w, View *v, const char *a)
{
    (void)v;
    if (!*a) {
        ui_message(w, "uso: :e arquivo.pdf");
        return;
    }
    char *path = expand_home(a);
    open_tab(w, path);
    free(path);
}

static void c_panel(Window *w, View *v, const char *a) { (void)v, (void)a; panel_open(w); }
static void c_nohl(Window *w, View *v, const char *a) { (void)w, (void)a; v->match_page = -1; }

static void c_reload(Window *w, View *v, const char *a)
{
    (void)a;
    v->doc->saved_mtime_ns = -2; /* differs from any real mtime: reload for sure */
    v->doc->reload_at = SDL_GetTicks();
    ui_message(w, "recarregando");
}

static void c_zoom(Window *w, View *v, const char *a)
{
    double pct = atof(a);
    if (pct < 5 || pct > 4000) {
        ui_message(w, "uso: :zoom 150  (porcentagem)");
        return;
    }
    v->fit_width = false;
    v->zoom = pct / 100.0 * 96.0 / 72.0 * w->density;
    view_layout(v, w->W, w->H, top_of(w));
    view_clamp(v, w->W, w->H, top_of(w));
}

static void c_dark(Window *w, View *v, const char *a) { (void)w, (void)a; v->dark = true; }
static void c_light(Window *w, View *v, const char *a) { (void)w, (void)a; v->dark = false; }

static void c_help(Window *w, View *v, const char *a)
{
    (void)v, (void)a;
    w->help_return = w->mode;
    w->help_first = 0;
    w->mode = MODE_HELP;
}

static void c_page(Window *w, View *v, const char *a) { goto_page(w, v, atoi(a) - 1); }

static const Command commands[] = {
    {"w", ARG_NONE, c_write, "salvar"},
    {"q", ARG_NONE, c_quit, "fechar a aba"},
    {"q!", ARG_NONE, c_quit_force, "fechar a aba descartando alterações"},
    {"wq", ARG_NONE, c_write_quit, "salvar e fechar a aba"},
    {"x", ARG_NONE, c_write_quit, "salvar e fechar a aba"},
    {"e", ARG_FILE, c_edit, "abrir um PDF em nova aba"},
    {"page", ARG_NUMBER, c_page, "ir para a página (também :42)"},
    {"zoom", ARG_NUMBER, c_zoom, "zoom em porcentagem, ex. :zoom 150"},
    {"dark", ARG_NONE, c_dark, "modo escuro"},
    {"light", ARG_NONE, c_light, "modo claro"},
    {"marks", ARG_NONE, c_panel, "painel de marcas, destaques e sumário"},
    {"notes", ARG_NONE, c_panel, "painel de marcas, destaques e sumário"},
    {"noh", ARG_NONE, c_nohl, "limpar o destaque da busca"},
    {"reload", ARG_NONE, c_reload, "recarregar o PDF do disco"},
    {"help", ARG_NONE, c_help, "todos os atalhos"},
};

int command_count(void) { return (int)SDL_arraysize(commands); }

void command_describe(int i, char *keys, size_t cap, const char **text)
{
    const Command *c = &commands[i];
    snprintf(keys, cap, ":%s%s", c->name, c->arg == ARG_FILE ? " arquivo" : c->arg == ARG_NUMBER ? " N" : "");
    *text = c->help;
}

void command_run(Window *w, const char *line)
{
    View *v = cur_view(w);
    while (*line == ' ')
        line++;
    size_t n = strcspn(line, " ");
    const char *arg = line + n + strspn(line + n, " ");
    if (!n)
        return;
    if (line[0] >= '0' && line[0] <= '9') {
        goto_page(w, v, atoi(line) - 1);
        return;
    }
    for (size_t i = 0; i < SDL_arraysize(commands); i++) {
        if (strlen(commands[i].name) == n && !strncmp(commands[i].name, line, n)) {
            commands[i].fn(w, v, arg);
            return;
        }
    }
    ui_message(w, "comando desconhecido: %.*s  (Tab lista os comandos)", (int)n, line);
}

/* ---------- completion ---------- */
void command_complete_reset(Window *w)
{
    for (int i = 0; i < w->ncomp; i++)
        free(w->comp[i]);
    free(w->comp);
    w->comp = NULL;
    w->ncomp = 0;
}

static void add_candidate(Window *w, const char *s)
{
    for (int i = 0; i < w->ncomp; i++)
        if (!strcmp(w->comp[i], s))
            return;
    w->comp = realloc(w->comp, (w->ncomp + 1) * sizeof *w->comp);
    w->comp[w->ncomp++] = strdup(s);
}

static bool is_pdf_or_dir(const char *p)
{
    size_t n = strlen(p);
    return (n && p[n - 1] == '/') || (n > 4 && !strcasecmp(p + n - 4, ".pdf"));
}

static void build_candidates(Window *w)
{
    const char *line = w->cmd;
    const char *space = strchr(line, ' ');
    if (!space) {
        w->comp_start = 0;
        for (size_t i = 0; i < SDL_arraysize(commands); i++)
            if (!strncmp(commands[i].name, line, strlen(line)))
                add_candidate(w, commands[i].name);
        return;
    }
    size_t n = (size_t)(space - line);
    const Command *c = NULL;
    for (size_t i = 0; i < SDL_arraysize(commands); i++)
        if (strlen(commands[i].name) == n && !strncmp(commands[i].name, line, n))
            c = &commands[i];
    if (!c || c->arg != ARG_FILE)
        return;
    const char *arg = space + strspn(space, " ");
    w->comp_start = (int)(arg - line);
    char *prefix = expand_home(arg);
    char *pattern = malloc(strlen(prefix) + 2);
    sprintf(pattern, "%s*", prefix);
    glob_t g;
    if (glob(pattern, GLOB_MARK, NULL, &g) == 0)
        for (size_t i = 0; i < g.gl_pathc; i++)
            if (is_pdf_or_dir(g.gl_pathv[i]))
                add_candidate(w, g.gl_pathv[i]);
    globfree(&g);
    free(pattern);
    free(prefix);
}

static void put_candidate(Window *w, const char *s)
{
    snprintf(w->cmd + w->comp_start, CMD_MAX - (size_t)w->comp_start, "%s", s);
    w->cmdlen = (int)strlen(w->cmd);
}

/* Tab / Shift-Tab: complete the word being typed, then cycle through the options. */
void command_complete(Window *w, int dir)
{
    if (!w->ncomp) {
        build_candidates(w);
        if (!w->ncomp) {
            ui_message(w, "nenhuma opção");
            return;
        }
        if (w->ncomp == 1) {
            put_candidate(w, w->comp[0]);
            bool wants_arg = false;
            for (size_t i = 0; i < SDL_arraysize(commands) && w->comp_start == 0; i++)
                if (!strcmp(commands[i].name, w->comp[0]))
                    wants_arg = commands[i].arg != ARG_NONE;
            if (wants_arg && w->cmdlen + 1 < CMD_MAX)
                strcat(w->cmd, " "), w->cmdlen++;
            command_complete_reset(w); /* a directory: the next Tab looks inside */
            return;
        }
        w->comp_at = dir > 0 ? -1 : 0;
    }
    w->comp_at = (w->comp_at + dir + w->ncomp) % w->ncomp;
    put_candidate(w, w->comp[w->comp_at]);
}

/* Description of a candidate, for the completion list. */
const char *command_help_for(const char *name)
{
    for (size_t i = 0; i < SDL_arraysize(commands); i++)
        if (!strcmp(commands[i].name, name))
            return commands[i].help;
    return NULL;
}
