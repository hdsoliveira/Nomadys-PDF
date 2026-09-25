/* Keyboard input: every key press becomes a token ("j", "G", "<C-j>", "<Esc>"),
 * and tokens are matched against the keymap of the current mode.
 *
 * To add or change a key, edit the tables at the end of this file. Patterns:
 *   "gg"          a sequence of keys
 *   "<C-j>"       Ctrl + j;  other names: <Esc> <Enter> <Tab> <S-Tab> <BS> <Del>
 *                 <Up> <Down> <Left> <Right> <PageUp> <PageDown> <Home> <End> <lt>
 *   "<leader>"    the leader key from the config (default "\")
 *   "*"           any single character, passed to the action as `arg`
 * A number typed before a command is passed as `count` (0 when absent).
 */
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

typedef void (*ActionFn)(Window *w, View *v, int count, const char *arg);
typedef struct { const char *keys; ActionFn fn; } Binding;

#define TOKENS_MAX 8
#define TOKEN_LEN 16

static char pend[TOKENS_MAX][TOKEN_LEN];
static int npend;

/* ---------- small helpers used by actions ---------- */
static double line_px(Window *w) { return 60 * w->density; }
static double page_px(Window *w) { return w->H - top_of(w) - 40 * w->density; }
static int n1(int count) { return count ? count : 1; }

void scroll(Window *w, double dx, double dy)
{
    View *v = cur_view(w);
    v->sx += dx;
    v->sy += dy;
    view_clamp(v, w->W, w->H, top_of(w));
    w->redraw = true;
}

static void goto_page(Window *w, View *v, int page)
{
    page = SDL_clamp(page, 0, v->doc->npages - 1);
    jump_to(w, v, (Pos){page, 0});
    ui_message(w, "%d / %d", page + 1, v->doc->npages);
}

static void zoom_by(Window *w, View *v, double f)
{
    int top = top_of(w);
    double cx = (v->sx + w->W / 2.0) / SDL_max(1, v->content_w);
    v->fit_width = false;
    v->zoom = SDL_clamp(v->zoom * f, 0.05, 40.0);
    view_layout(v, w->W, w->H, top);
    v->sx = cx * v->content_w - w->W / 2.0;
    view_clamp(v, w->W, w->H, top);
    ui_message(w, "%.0f%%", v->zoom / w->density * 72.0 / 96.0 * 100);
}

/* ---------- normal mode actions ---------- */
static void a_down(Window *w, View *v, int c, const char *a) { (void)v, (void)a; scroll(w, 0, line_px(w) * n1(c)); }
static void a_up(Window *w, View *v, int c, const char *a) { (void)v, (void)a; scroll(w, 0, -line_px(w) * n1(c)); }
static void a_left(Window *w, View *v, int c, const char *a) { (void)v, (void)a; scroll(w, -line_px(w) * n1(c), 0); }
static void a_right(Window *w, View *v, int c, const char *a) { (void)v, (void)a; scroll(w, line_px(w) * n1(c), 0); }
static void a_half_down(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; scroll(w, 0, page_px(w) / 2); }
static void a_half_up(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; scroll(w, 0, -page_px(w) / 2); }
static void a_page_down(Window *w, View *v, int c, const char *a) { (void)v, (void)a; scroll(w, 0, page_px(w) * n1(c)); }
static void a_page_up(Window *w, View *v, int c, const char *a) { (void)v, (void)a; scroll(w, 0, -page_px(w) * n1(c)); }

static void a_next_page(Window *w, View *v, int c, const char *a)
{
    (void)a;
    goto_page(w, v, view_top_page(v) + n1(c) * (v->dual ? 2 : 1));
}

static void a_prev_page(Window *w, View *v, int c, const char *a)
{
    (void)a;
    goto_page(w, v, view_top_page(v) - n1(c) * (v->dual ? 2 : 1));
}

static void a_first(Window *w, View *v, int c, const char *a) { (void)a; goto_page(w, v, c ? c - 1 : 0); }

static void a_last(Window *w, View *v, int c, const char *a)
{
    (void)a;
    if (c)
        goto_page(w, v, c - 1);
    else
        jump_to(w, v, (Pos){v->doc->npages - 1, 1});
}

static void a_zoom_in(Window *w, View *v, int c, const char *a) { (void)a; zoom_by(w, v, SDL_pow(1.1, n1(c))); }
static void a_zoom_out(Window *w, View *v, int c, const char *a) { (void)a; zoom_by(w, v, SDL_pow(1 / 1.1, n1(c))); }

static void a_fit_width(Window *w, View *v, int c, const char *a)
{
    (void)c, (void)a;
    v->fit_width = true;
    ui_message(w, "ajustar à largura");
}

static void a_fit_page(Window *w, View *v, int c, const char *a)
{
    (void)c, (void)a;
    int top = top_of(w), p = view_top_page(v);
    float pw = v->doc->pw[p] + (v->dual && p + 1 < v->doc->npages ? v->doc->pw[p + 1] : 0);
    v->fit_width = false;
    v->zoom = SDL_min((w->W - 30.0) / pw, (w->H - top - 20.0) / v->doc->ph[p]);
    view_layout(v, w->W, w->H, top);
    view_goto_page(v, p);
    view_clamp(v, w->W, w->H, top);
}

static void a_dual(Window *w, View *v, int c, const char *a)
{
    (void)c, (void)a;
    int p = view_top_page(v);
    v->dual = !v->dual;
    view_layout(v, w->W, w->H, top_of(w));
    view_goto_page(v, p);
    view_clamp(v, w->W, w->H, top_of(w));
    ui_message(w, v->dual ? "duas páginas" : "uma página");
}

static void a_dark(Window *w, View *v, int c, const char *a) { (void)w, (void)c, (void)a; v->dark = !v->dark; }
static void a_tabs(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; w->show_tabs = !w->show_tabs; }
static void a_caret(Window *w, View *v, int c, const char *a) { (void)c, (void)a; caret_enter(w, v); }
static void a_area(Window *w, View *v, int c, const char *a) { (void)c, (void)a; area_enter(w, v); }

static void a_clear(Window *w, View *v, int c, const char *a)
{
    (void)w, (void)c, (void)a;
    v->match_page = -1;
}

static void a_search_next(Window *w, View *v, int c, const char *a)
{
    (void)a;
    if (!v->query[0])
        ui_message(w, "nenhuma busca: use /");
    for (int i = 0; i < n1(c) && v->query[0]; i++)
        search_run(w, v, 1, false);
}

static void a_search_prev(Window *w, View *v, int c, const char *a)
{
    (void)a;
    for (int i = 0; i < n1(c) && v->query[0]; i++)
        search_run(w, v, -1, false);
}

static void a_quit(Window *w, View *v, int c, const char *a)
{
    (void)v, (void)c, (void)a;
    if (confirm_close(w, NULL))
        close_window(w);
}

static void a_form_next(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; form_step(w, 1); }
static void a_form_prev(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; form_step(w, -1); }
static void a_new_window(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; window_new_from(w); }
static void a_save(Window *w, View *v, int c, const char *a) { (void)c, (void)a; save(w, v); }

static void switch_tab(Window *w, int dir)
{
    if (w->ntabs > 1) {
        if (cur_view(w)->doc->focus_page >= 0)
            form_leave(w);
        w->cur = (w->cur + dir + w->ntabs) % w->ntabs;
        w->mode = MODE_NORMAL;
        update_title(w);
    }
    w->tabs_flash_until = SDL_GetTicks() + 1000;
}

static void a_tab_prev(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; switch_tab(w, -1); }
static void a_tab_next(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; switch_tab(w, 1); }
static void a_jump_older(Window *w, View *v, int c, const char *a) { (void)c, (void)a; jump_history(w, v, -1); }
static void a_jump_newer(Window *w, View *v, int c, const char *a) { (void)c, (void)a; jump_history(w, v, 1); }
static void a_jump_back(Window *w, View *v, int c, const char *a) { (void)c, (void)a; jump_back(w, v); }
static void a_panel(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_open(w); }

static bool mark_letter(Window *w, const char *a)
{
    if (a[0] >= 'a' && a[0] <= 'z' && !a[1])
        return true;
    ui_message(w, "marcas usam letras de a a z");
    return false;
}

static void a_mark_jump(Window *w, View *v, int c, const char *a)
{
    (void)c;
    if (mark_letter(w, a))
        mark_jump(w, v, a[0] - 'a');
}

static void a_mark_set(Window *w, View *v, int c, const char *a)
{
    (void)c;
    if (mark_letter(w, a))
        mark_set(w, v, a[0] - 'a');
}

/* ---------- prompt line: commands, search, comments ---------- */
static void complete_path(Window *w)
{
    if (strncmp(w->cmd, "e ", 2))
        return;
    char pattern[CMD_MAX + 8];
    const char *arg = w->cmd + 2, *home = getenv("HOME");
    if (arg[0] == '~' && home)
        snprintf(pattern, sizeof pattern, "%s%s*", home, arg + 1);
    else
        snprintf(pattern, sizeof pattern, "%s*", arg);
    glob_t g;
    if (glob(pattern, GLOB_MARK, NULL, &g) || !g.gl_pathc) {
        globfree(&g);
        return;
    }
    size_t common = strlen(g.gl_pathv[0]);
    for (size_t i = 1; i < g.gl_pathc; i++) {
        size_t k = 0;
        while (k < common && g.gl_pathv[i][k] == g.gl_pathv[0][k])
            k++;
        common = k;
    }
    if (common + 3 < CMD_MAX) {
        snprintf(w->cmd, CMD_MAX, "e %.*s", (int)common, g.gl_pathv[0]);
        w->cmdlen = (int)strlen(w->cmd);
    }
    if (g.gl_pathc > 1)
        ui_message(w, "%zu opções", g.gl_pathc);
    globfree(&g);
}

static void run_command(Window *w, const char *line)
{
    View *v = cur_view(w);
    while (*line == ' ')
        line++;
    char c[CMD_MAX];
    snprintf(c, sizeof c, "%s", line);
    if (!strcmp(c, "w")) {
        save(w, v);
    } else if (!strcmp(c, "q") || !strcmp(c, "q!") || !strcmp(c, "wq") || !strcmp(c, "x")) {
        if (c[0] == 'w' || c[0] == 'x')
            save(w, v);
        if (c[1] == '!' || c[0] == 'w' || c[0] == 'x' || confirm_close(w, v))
            close_tab(w, w->cur);
    } else if (!strncmp(c, "e ", 2)) {
        char path[CMD_MAX + 256];
        const char *arg = c + 2, *home = getenv("HOME");
        if (arg[0] == '~' && home)
            snprintf(path, sizeof path, "%s%s", home, arg + 1);
        else
            snprintf(path, sizeof path, "%s", arg);
        open_tab(w, path);
    } else if (!strcmp(c, "marks") || !strcmp(c, "notes")) {
        panel_open(w);
    } else if (!strcmp(c, "noh")) {
        v->match_page = -1;
    } else if (c[0] >= '0' && c[0] <= '9') {
        goto_page(w, v, atoi(c) - 1);
    } else if (*c) {
        ui_message(w, "comando desconhecido: %s", c);
    }
}

static void run_search(Window *w, const char *text)
{
    View *v = cur_view(w);
    snprintf(v->query, sizeof v->query, "%s", text);
    search_run(w, v, 1, true);
}

static void a_command(Window *w, View *v, int c, const char *a)
{
    (void)v, (void)c, (void)a;
    prompt_open(w, ":", "", run_command, 0);
}

static void a_search(Window *w, View *v, int c, const char *a)
{
    (void)v, (void)c, (void)a;
    prompt_open(w, "/", "", run_search, 0);
}

void prompt_open(Window *w, const char *label, const char *initial, PromptFn done, int arg)
{
    w->prompt_return = w->mode;
    w->mode = MODE_PROMPT;
    snprintf(w->prompt_label, sizeof w->prompt_label, "%s", label);
    snprintf(w->cmd, sizeof w->cmd, "%s", initial);
    w->cmdlen = (int)strlen(w->cmd);
    w->prompt_done = done;
    w->prompt_arg = arg;
    w->redraw = true;
}

static void prompt_insert(Window *w, const char *t)
{
    size_t n = strlen(t);
    if (w->cmdlen + n < CMD_MAX - 1) {
        memcpy(w->cmd + w->cmdlen, t, n + 1);
        w->cmdlen += (int)n;
    }
}

static void prompt_special(Window *w, const char *tok)
{
    if (!strcmp(tok, "<Esc>")) {
        w->mode = w->prompt_return;
    } else if (!strcmp(tok, "<Enter>")) {
        w->mode = w->prompt_return;
        char text[CMD_MAX];
        snprintf(text, sizeof text, "%s", w->cmd);
        w->prompt_done(w, text);
    } else if (!strcmp(tok, "<BS>")) {
        if (w->cmdlen == 0 && (w->prompt_done == run_command || w->prompt_done == run_search)) {
            w->mode = w->prompt_return; /* backspace on an empty ':' or '/' line */
            return;
        }
        while (w->cmdlen > 0 && ((unsigned char)w->cmd[--w->cmdlen] & 0xC0) == 0x80)
            ;
        w->cmd[w->cmdlen] = 0;
    } else if (!strcmp(tok, "<C-u>")) {
        w->cmd[w->cmdlen = 0] = 0;
    } else if (!strcmp(tok, "<Tab>") && w->prompt_done == run_command) {
        complete_path(w);
    }
}

/* ---------- caret and free cursor actions ---------- */
static void a_caret_move(Window *w, View *v, int c, const char *a)
{
    /* the pattern character is the motion itself */
    caret_move(w, v, a[0], n1(c));
}

static void a_caret_select(Window *w, View *v, int c, const char *a) { (void)c, (void)a; caret_toggle_select(w, v); }
static void a_caret_mark(Window *w, View *v, int c, const char *a) { (void)c, (void)a; caret_mark(w, v, false); }
static void a_caret_comment(Window *w, View *v, int c, const char *a) { (void)c, (void)a; caret_mark(w, v, true); }
static void a_caret_copy(Window *w, View *v, int c, const char *a) { (void)c, (void)a; caret_copy(w, v); }

static void a_note_delete(Window *w, View *v, int c, const char *a)
{
    (void)c, (void)a;
    int page, note;
    if (cursor_note(w, v, &page, &note) && note_delete(v->doc, page, note))
        ui_message(w, "destaque apagado");
    else
        ui_message(w, "nenhum destaque sob o cursor");
}

static void a_leave(Window *w, View *v, int c, const char *a)
{
    (void)c, (void)a;
    if (w->mode == MODE_CARET && v->selecting)
        v->selecting = false;
    else if (w->mode == MODE_AREA && v->area_selecting)
        v->area_selecting = false;
    else
        w->mode = MODE_NORMAL;
}

static void a_to_caret(Window *w, View *v, int c, const char *a) { (void)c, (void)a; caret_enter(w, v); }

static void a_area_move(Window *w, View *v, int c, const char *a)
{
    float n = (float)n1(c), big = 6 * n;
    switch (a[0]) {
    case 'h': area_move(w, v, -n, 0); break;
    case 'l': area_move(w, v, n, 0); break;
    case 'j': area_move(w, v, 0, n); break;
    case 'k': area_move(w, v, 0, -n); break;
    case 'b': area_move(w, v, -big, 0); break;
    case 'w': area_move(w, v, big, 0); break;
    case 'J': area_move(w, v, 0, big); break;
    case 'K': area_move(w, v, 0, -big); break;
    }
}

static void a_area_select(Window *w, View *v, int c, const char *a) { (void)c, (void)a; area_toggle_select(w, v); }
static void a_area_mark(Window *w, View *v, int c, const char *a) { (void)c, (void)a; area_mark(w, v, false); }
static void a_area_comment(Window *w, View *v, int c, const char *a) { (void)c, (void)a; area_mark(w, v, true); }

/* ---------- panel actions ---------- */
static void a_panel_down(Window *w, View *v, int c, const char *a) { (void)v, (void)a; panel_move(w, n1(c)); }
static void a_panel_up(Window *w, View *v, int c, const char *a) { (void)v, (void)a; panel_move(w, -n1(c)); }
static void a_panel_top(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; w->sel = 0; panel_move(w, 1); }
static void a_panel_bottom(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_move(w, w->nitems); }
static void a_panel_go(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_activate(w); }
static void a_panel_delete(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_delete(w); }
static void a_panel_rename(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_rename(w); }
static void a_panel_close(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_close(w); }

/* ---------- keymaps ---------- */
#define SCROLL_KEYS                                                                          \
    {"<C-d>", a_half_down}, {"<C-u>", a_half_up}, {"<C-f>", a_page_down}, {"<C-b>", a_page_up}, \
    {"<PageDown>", a_page_down}, {"<PageUp>", a_page_up}, {"<Down>", a_down}, {"<Up>", a_up},  \
    {"<Left>", a_left}, {"<Right>", a_right}

/* Marks, jumps and the panel work in every reading mode. */
#define MARK_KEYS                                                                             \
    {"mm", a_jump_back}, {"m*", a_mark_jump}, {"<leader>m*", a_mark_set}, {"<leader>l", a_panel}, \
    {"<C-o>", a_jump_older}, {"<C-i>", a_jump_newer}

static const Binding normal_map[] = {
    {"j", a_down}, {"k", a_up}, {"h", a_left}, {"l", a_right}, {" ", a_page_down},
    SCROLL_KEYS, {"<Home>", a_first}, {"<End>", a_last},
    {"J", a_next_page}, {"K", a_prev_page}, {"gg", a_first}, {"G", a_last},
    {"+", a_zoom_in}, {"-", a_zoom_out}, {"=", a_fit_width}, {"z", a_fit_page},
    {"<C-=>", a_zoom_in}, {"<C-->", a_zoom_out},
    {"d", a_dual}, {"i", a_dark}, {"t", a_tabs},
    {"v", a_caret}, {"<C-v>", a_area},
    {":", a_command}, {"/", a_search}, {"n", a_search_next}, {"N", a_search_prev},
    {"<C-j>", a_form_next}, {"<C-k>", a_form_prev}, {"<Tab>", a_form_next}, {"<S-Tab>", a_form_prev},
    {"<C-n>", a_new_window}, {"<C-s>", a_save},
    {"<C-,>", a_tab_prev}, {"<C-.>", a_tab_next},
    MARK_KEYS, {"<Esc>", a_clear}, {"q", a_quit},
};

static const Binding caret_map[] = {
    {"h", a_caret_move}, {"j", a_caret_move}, {"k", a_caret_move}, {"l", a_caret_move},
    {"w", a_caret_move}, {"b", a_caret_move}, {"0", a_caret_move}, {"$", a_caret_move},
    {"v", a_caret_select}, {"H", a_caret_mark}, {"c", a_caret_comment}, {"y", a_caret_copy},
    {"x", a_note_delete}, {"<C-v>", a_area}, {"<Esc>", a_leave}, SCROLL_KEYS, MARK_KEYS,
};

static const Binding area_map[] = {
    {"h", a_area_move}, {"j", a_area_move}, {"k", a_area_move}, {"l", a_area_move},
    {"w", a_area_move}, {"b", a_area_move}, {"J", a_area_move}, {"K", a_area_move},
    {"v", a_area_select}, {"H", a_area_mark}, {"c", a_area_comment}, {"x", a_note_delete},
    {"<C-v>", a_to_caret}, {"<Esc>", a_leave}, SCROLL_KEYS, MARK_KEYS,
};

static const Binding panel_map[] = {
    {"j", a_panel_down}, {"k", a_panel_up}, {"<Down>", a_panel_down}, {"<Up>", a_panel_up},
    {"gg", a_panel_top}, {"G", a_panel_bottom}, {"<Enter>", a_panel_go}, {"x", a_panel_delete},
    {"r", a_panel_rename}, {"<Esc>", a_panel_close}, {"q", a_panel_close},
};

static const Binding *keymap(Mode m, int *n)
{
    switch (m) {
    case MODE_NORMAL: *n = SDL_arraysize(normal_map); return normal_map;
    case MODE_CARET: *n = SDL_arraysize(caret_map); return caret_map;
    case MODE_AREA: *n = SDL_arraysize(area_map); return area_map;
    case MODE_PANEL: *n = SDL_arraysize(panel_map); return panel_map;
    default: *n = 0; return NULL;
    }
}

/* ---------- matching ---------- */

/* Splits a pattern into tokens; returns their count. */
static int pattern_tokens(const char *p, char out[][TOKEN_LEN])
{
    int n = 0;
    while (*p && n < TOKENS_MAX) {
        if (!strncmp(p, "<leader>", 8)) {
            snprintf(out[n++], TOKEN_LEN, "%s", cfg.leader);
            p += 8;
        } else if (*p == '<' && strchr(p, '>')) {
            size_t len = (size_t)(strchr(p, '>') - p + 1);
            snprintf(out[n++], TOKEN_LEN, "%.*s", (int)len, p);
            p += len;
        } else {
            const char *s = p;
            utf8_next(&p);
            snprintf(out[n++], TOKEN_LEN, "%.*s", (int)(p - s), s);
        }
    }
    return n;
}

static bool printable(const char *tok) { return tok[0] != '<' || !strcmp(tok, "<lt>"); }

/* 2 = exact match, 1 = prefix of the pattern, 0 = no match. */
static int match(const Binding *b, const char **arg)
{
    char pt[TOKENS_MAX][TOKEN_LEN];
    int np = pattern_tokens(b->keys, pt);
    if (npend > np)
        return 0;
    for (int i = 0; i < npend; i++) {
        if (!strcmp(pt[i], "*") && printable(pend[i]))
            *arg = pend[i];
        else if (strcmp(pt[i], pend[i]))
            return 0;
    }
    return npend == np ? 2 : 1;
}

/* When a sequence is both complete and the start of a longer one (a space
 * leader vs. Space), the shorter binding is held until the next key or a
 * timeout decides, like vim's 'timeoutlen'. */
#define AMBIGUOUS_MS 700
static struct {
    const Binding *b;
    char arg[TOKEN_LEN];
    int ntok;                /* tokens consumed by the held binding */
    Window *w;
    uint64_t at;
} held;

static void run(Window *w, const Binding *b, const char *arg)
{
    int count = w->count;
    w->count = 0;
    w->redraw = true; /* before the action: it may close the window */
    b->fn(w, cur_view(w), count, arg);
}

/* Runs the held binding, then replays the keys typed after it. */
static void release_held(void)
{
    char rest[TOKENS_MAX][TOKEN_LEN];
    int nrest = npend - held.ntok;
    memcpy(rest, pend + held.ntok, sizeof rest[0] * (size_t)SDL_max(0, nrest));
    const Binding *b = held.b;
    Window *w = held.w;
    char arg[TOKEN_LEN];
    SDL_strlcpy(arg, held.arg, sizeof arg);
    held.b = NULL;
    npend = 0;
    run(w, b, arg);
    for (int i = 0; i < nrest; i++) {
        bool alive = false;
        for (int k = 0; k < nwindows; k++)
            alive |= windows[k] == w;
        if (alive)
            input_token(w, rest[i]);
    }
}

int input_wait_ms(uint64_t now)
{
    return held.b ? (int)SDL_max(0, (int64_t)(held.at + AMBIGUOUS_MS) - (int64_t)now) : -1;
}

void input_expire(uint64_t now)
{
    if (held.b && now >= held.at + AMBIGUOUS_MS)
        release_held();
}

void input_token(Window *w, const char *tok)
{
    if (w->mode == MODE_FORM || w->mode == MODE_PROMPT) {
        const char *text = !strcmp(tok, "<lt>") ? "<" : tok;
        w->redraw = true;
        if (w->mode == MODE_FORM && printable(tok))
            form_text(w, text);
        else if (w->mode == MODE_FORM)
            form_special(w, tok);
        else if (printable(tok))
            prompt_insert(w, text);
        else
            prompt_special(w, tok);
        return;
    }
    /* a count, unless a sequence is under way ("m1" is not a count) */
    if (!npend && ((tok[0] >= '1' && tok[0] <= '9') || (tok[0] == '0' && w->count)) && !tok[1]) {
        w->count = SDL_min(w->count * 10 + (tok[0] - '0'), 1000000);
        return;
    }
    if (npend == TOKENS_MAX)
        npend = 0;
    snprintf(pend[npend++], TOKEN_LEN, "%s", tok);
    int n;
    const Binding *map = keymap(w->mode, &n);
    const Binding *exact = NULL;
    char exact_arg[TOKEN_LEN] = "";
    bool prefix = false;
    for (int i = 0; i < n; i++) {
        const char *arg = "";
        int m = match(&map[i], &arg);
        if (m == 2 && !exact) {
            exact = &map[i];
            SDL_strlcpy(exact_arg, arg[0] ? arg : pend[npend - 1], sizeof exact_arg);
        }
        prefix |= m == 1;
    }
    if (exact && !prefix) {
        held.b = NULL;
        npend = 0;
        run(w, exact, exact_arg);
    } else if (exact) {
        held.b = exact;
        SDL_strlcpy(held.arg, exact_arg, sizeof held.arg);
        held.ntok = npend;
        held.w = w;
        held.at = SDL_GetTicks();
    } else if (!prefix) {
        if (held.b) {
            release_held();
            return;
        }
        /* unknown sequence: drop it, but give its last key a second chance */
        bool retry = npend > 1;
        char last[TOKEN_LEN];
        SDL_strlcpy(last, pend[npend - 1], sizeof last);
        npend = 0;
        if (retry)
            input_token(w, last);
        else
            w->count = 0;
    }
}

/* ---------- SDL events to tokens ---------- */
bool input_key_event(Window *w, const SDL_KeyboardEvent *e)
{
    static const struct { SDL_Keycode key; const char *name; } names[] = {
        {SDLK_ESCAPE, "<Esc>"}, {SDLK_RETURN, "<Enter>"}, {SDLK_KP_ENTER, "<Enter>"},
        {SDLK_BACKSPACE, "<BS>"}, {SDLK_DELETE, "<Del>"}, {SDLK_UP, "<Up>"}, {SDLK_DOWN, "<Down>"},
        {SDLK_LEFT, "<Left>"}, {SDLK_RIGHT, "<Right>"}, {SDLK_PAGEUP, "<PageUp>"},
        {SDLK_PAGEDOWN, "<PageDown>"}, {SDLK_HOME, "<Home>"}, {SDLK_END, "<End>"},
    };
    char tok[TOKEN_LEN];
    if ((e->mod & SDL_KMOD_CTRL) && e->key >= 32 && e->key < 127) {
        snprintf(tok, sizeof tok, "<C-%c>", (char)e->key);
    } else if (e->key == SDLK_TAB) {
        snprintf(tok, sizeof tok, (e->mod & SDL_KMOD_SHIFT) ? "<S-Tab>" : "<Tab>");
    } else {
        tok[0] = 0;
        for (size_t i = 0; i < SDL_arraysize(names); i++)
            if (names[i].key == e->key)
                snprintf(tok, sizeof tok, "%s", names[i].name);
        if (!tok[0])
            return false; /* printable keys arrive as text input */
    }
    input_token(w, tok);
    return true;
}

void input_text_event(Window *w, const char *text)
{
    if (w->mode == MODE_FORM) {
        form_text(w, text);
        w->redraw = true;
        return;
    }
    if (w->mode == MODE_PROMPT) {
        prompt_insert(w, text);
        w->redraw = true;
        return;
    }
    while (*text) {
        const char *s = text;
        utf8_next(&text);
        char tok[TOKEN_LEN];
        snprintf(tok, sizeof tok, "%.*s", (int)(text - s), s);
        input_token(w, !strcmp(tok, "<") ? "<lt>" : tok);
    }
}
