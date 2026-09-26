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
/* keys, action, and the description shown on the help screen (\c); NULL hides it */
typedef struct { const char *keys; ActionFn fn; const char *help; } Binding;

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

void goto_page(Window *w, View *v, int page)
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
    ui_message(w, "fit width");
}

static void a_fit_page(Window *w, View *v, int c, const char *a)
{
    (void)c, (void)a;
    int top = top_of(w), p = view_top_page(v);
    float pw = view_page_w(v, p) + (v->dual && p + 1 < v->doc->npages ? view_page_w(v, p + 1) : 0);
    v->fit_width = false;
    v->zoom = SDL_min((w->W - 30.0) / pw, (w->H - top - 20.0) / view_page_h(v, p));
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
    ui_message(w, v->dual ? "two pages" : "one page");
}

static void a_rotate_cw(Window *w, View *v, int c, const char *a) { (void)a; view_rotate(v, n1(c)); ui_message(w, "rotation: %d°", v->rotation * 90); }
static void a_rotate_ccw(Window *w, View *v, int c, const char *a) { (void)a; view_rotate(v, -n1(c)); ui_message(w, "rotation: %d°", v->rotation * 90); }
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
        ui_message(w, "no search yet: use /");
    /* a count repeats the search, bounded and stopping at the first miss */
    for (int i = 0; i < SDL_min(n1(c), 500) && v->query[0]; i++)
        if (!search_run(w, v, 1, false))
            break;
}

static void a_search_prev(Window *w, View *v, int c, const char *a)
{
    (void)a;
    for (int i = 0; i < SDL_min(n1(c), 500) && v->query[0]; i++)
        if (!search_run(w, v, -1, false))
            break;
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
    ui_message(w, "marks are letters a to z");
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
static void run_search(Window *w, const char *text)
{
    View *v = cur_view(w);
    snprintf(v->query, sizeof v->query, "%s", text);
    v->query_absent = false;
    search_run(w, v, 1, true);
}

static void a_print(Window *w, View *v, int c, const char *a)
{
    (void)v, (void)c, (void)a;
    prompt_open(w, ":", "print ", command_run, 0);
}

static void a_command(Window *w, View *v, int c, const char *a)
{
    (void)v, (void)c, (void)a;
    prompt_open(w, ":", "", command_run, 0);
}

static void a_search(Window *w, View *v, int c, const char *a)
{
    (void)v, (void)c, (void)a;
    prompt_open(w, "/", "", run_search, 0);
}

/* Whether the open prompt's action refers to the document (a comment, a mark
 * name): such a prompt is cancelled when the document is reloaded. */
const char *prompt_kind(Window *w)
{
    if (w->prompt_done == command_run)
        return "COMMAND";
    if (w->prompt_done == run_search)
        return "SEARCH";
    return !strncmp(w->prompt_label, "comment", 7) ? "COMMENT" : "EDIT";
}

bool prompt_uses_document(Window *w)
{
    return w->mode == MODE_PROMPT && w->prompt_done != command_run && w->prompt_done != run_search;
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
    w->prompt_box = false;
    w->redraw = true;
}

static void prompt_insert(Window *w, const char *t)
{
    command_complete_reset(w);
    size_t n = strlen(t);
    if (w->cmdlen + n < CMD_MAX - 1) {
        memcpy(w->cmd + w->cmdlen, t, n + 1);
        w->cmdlen += (int)n;
    }
}

static void prompt_special(Window *w, const char *tok)
{
    bool completing = w->prompt_done == command_run && (!strcmp(tok, "<Tab>") || !strcmp(tok, "<S-Tab>"));
    if (completing) {
        command_complete(w, tok[1] == 'S' ? -1 : 1);
        return;
    }
    command_complete_reset(w);
    if (!strcmp(tok, "<Esc>")) {
        w->mode = w->prompt_return;
    } else if (!strcmp(tok, "<Enter>") || !strcmp(tok, "<S-Enter>")) {
        w->mode = w->prompt_return;
        char text[CMD_MAX];
        snprintf(text, sizeof text, "%s", w->cmd);
        w->prompt_done(w, text);
    } else if (!strcmp(tok, "<BS>")) {
        if (w->cmdlen == 0 && (w->prompt_done == command_run || w->prompt_done == run_search)) {
            w->mode = w->prompt_return; /* backspace on an empty ':' or '/' line */
            return;
        }
        while (w->cmdlen > 0 && ((unsigned char)w->cmd[--w->cmdlen] & 0xC0) == 0x80)
            ;
        w->cmd[w->cmdlen] = 0;
    } else if (!strcmp(tok, "<C-u>")) {
        w->cmd[w->cmdlen = 0] = 0;
    }
}

/* ---------- caret and free cursor actions ---------- */
static void a_caret_move(Window *w, View *v, int c, const char *a)
{
    /* the pattern character is the motion itself */
    caret_move(w, v, a[0], n1(c));
}

static void a_caret_select(Window *w, View *v, int c, const char *a) { (void)c, (void)a; caret_toggle_select(w, v); }
/* C V B N M: the five highlight colours of the config, in that order. */
static uint32_t mark_color(const char *key)
{
    const char *at = strchr("CVBNM", key[0]);
    return cfg.highlights[at && key[0] ? at - "CVBNM" : 0];
}

static void a_caret_mark(Window *w, View *v, int c, const char *a) { (void)c; caret_mark(w, v, false, mark_color(a)); }
static void a_caret_comment(Window *w, View *v, int c, const char *a) { (void)c, (void)a; caret_mark(w, v, true, cfg.highlights[0]); }
static void a_caret_copy(Window *w, View *v, int c, const char *a) { (void)c, (void)a; caret_copy(w, v); }

static void a_note_delete(Window *w, View *v, int c, const char *a)
{
    (void)c, (void)a;
    int page, note;
    if (cursor_note(w, v, &page, &note) && note_delete(v->doc, page, note))
        ui_message(w, "highlight deleted");
    else
        ui_message(w, "no highlight under the cursor");
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
    if (!strncmp(a, "<C-", 3)) { /* Ctrl-h/j/k/l: fast */
        n = big;
        a += 3;
    }
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
static void a_area_mark(Window *w, View *v, int c, const char *a) { (void)c; area_mark(w, v, false, mark_color(a)); }
static void a_area_comment(Window *w, View *v, int c, const char *a) { (void)c, (void)a; area_mark(w, v, true, cfg.highlights[0]); }

static void a_source(Window *w, View *v, int c, const char *a)
{
    (void)c, (void)a;
    int page;
    float x, y;
    if (w->mode == MODE_AREA) {
        page = v->area_page, x = v->area_x, y = v->area_y;
    } else if (v->caret.page >= 0 && v->doc->slots[v->caret.page].chars_ready) {
        PRect r = v->doc->slots[v->caret.page].boxes[v->caret.ch];
        page = v->caret.page, x = (r.l + r.r) / 2, y = (r.t + r.b) / 2;
    } else {
        return;
    }
    synctex_inverse(w, v, page, x, y);
}

/* ---------- panel actions ---------- */
static void a_panel_down(Window *w, View *v, int c, const char *a) { (void)v, (void)a; panel_move(w, n1(c)); }
static void a_panel_up(Window *w, View *v, int c, const char *a) { (void)v, (void)a; panel_move(w, -n1(c)); }
static void a_panel_top(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; w->sel = 0; panel_move(w, 1); }
static void a_panel_bottom(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_move(w, w->nitems); }
static void a_panel_go(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_activate(w); }
static void a_panel_delete(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_delete(w); }
static void a_panel_uncomment(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_delete_comment(w); }
static void a_panel_rename(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_rename(w); }
static void a_panel_close(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_close(w); }
static void a_panel_next_tab(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_switch(w, -2); }
static void a_panel_prev_tab(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_switch(w, -1); }
static void a_panel_tab(Window *w, View *v, int c, const char *a) { (void)v, (void)c; panel_switch(w, a[0] - '1'); }

/* ---------- help screen actions ---------- */
static void a_help(Window *w, View *v, int c, const char *a)
{
    (void)v, (void)c, (void)a;
    w->help_return = w->mode;
    w->help_first = 0;
    w->mode = MODE_HELP;
}

static void a_help_close(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; w->mode = w->help_return; }
static void a_help_down(Window *w, View *v, int c, const char *a) { (void)v, (void)a; w->help_first += n1(c); }
static void a_help_up(Window *w, View *v, int c, const char *a) { (void)v, (void)a; w->help_first = SDL_max(0, w->help_first - n1(c)); }
static void a_help_page_down(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; w->help_first += 15; }
static void a_help_page_up(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; w->help_first = SDL_max(0, w->help_first - 15); }
static void a_help_top(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; w->help_first = 0; }
static void a_help_bottom(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; w->help_first = 1 << 20; }

/* ---------- links and reference preview ---------- */
static void a_follow(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; links_start(w, false); }
static void a_peek(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; links_start(w, true); }
static void a_preview_down(Window *w, View *v, int c, const char *a) { (void)v, (void)a; preview_scroll(w, (float)n1(c)); }
static void a_preview_up(Window *w, View *v, int c, const char *a) { (void)v, (void)a; preview_scroll(w, -(float)n1(c)); }
static void a_preview_page_down(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; preview_scroll(w, 6); }
static void a_preview_page_up(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; preview_scroll(w, -6); }
static void a_preview_go(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; preview_go(w); }
static void a_preview_close(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; preview_close(w); }

/* ---------- keymaps ---------- */
#define SCROLL_KEYS                                                                              \
    {"<C-d>", a_half_down, "half a screen down"}, {"<C-u>", a_half_up, "half a screen up"},          \
    {"<C-f>", a_page_down, "a screen down"}, {"<C-b>", a_page_up, "a screen up"},     \
    {"<PageDown>", a_page_down, NULL}, {"<PageUp>", a_page_up, NULL}, {"<Down>", a_down, NULL},    \
    {"<Up>", a_up, NULL}, {"<Left>", a_left, NULL}, {"<Right>", a_right, NULL}

/* Marks, jumps, the panel and this help work in every reading mode. */
#define MARK_KEYS                                                                                \
    {"<leader>m*", a_mark_set, "set a mark here (a-z)"},                                \
    {"m*", a_mark_jump, "jump to a mark"}, {"mm", a_jump_back, "back to where you were"},   \
    {"<C-o>", a_jump_older, "older position in the jump list"},                                      \
    {"<C-i>", a_jump_newer, "newer position in the jump list"},                                       \
    {"<leader>l", a_panel, "panel: tabs, summary, marks, highlights"},                                 \
    {"<leader>c", a_help, "this help (again closes it)"}

static const Binding normal_map[] = {
    {"j", a_down, "scroll"}, {"k", a_up, "scroll"}, {"h", a_left, "scroll"}, {"l", a_right, "scroll"},
    {" ", a_page_down, "a screen down"}, SCROLL_KEYS,
    {"<Home>", a_first, NULL}, {"<End>", a_last, NULL},
    {"J", a_next_page, "next page"}, {"K", a_prev_page, "previous page"},
    {"gg", a_first, "first page (42gg: page 42)"}, {"G", a_last, "last page (42G: page 42)"},
    {"+", a_zoom_in, "zoom"}, {"-", a_zoom_out, "zoom"}, {"=", a_fit_width, "fit width"},
    {"z", a_fit_page, "fit page"}, {"<C-=>", a_zoom_in, NULL}, {"<C-->", a_zoom_out, NULL},
    {"d", a_dual, "two pages side by side"}, {"i", a_dark, "dark mode"},
    {"r", a_rotate_cw, "rotate 90° clockwise"}, {"R", a_rotate_ccw, "rotate 90° counter-clockwise"},
    {"t", a_tabs, "show / hide tabs"},
    {"v", a_caret, "text cursor: highlight, comment, copy"},
    {"<C-v>", a_area, "free cursor: mark figures and areas"},
    {"/", a_search, "search"}, {"n", a_search_next, "next match"},
    {"N", a_search_prev, "previous match"},
    {"f", a_follow, "follow a link: type its letters (Ctrl-o returns)"},
    {"F", a_peek, "preview a reference (figure, equation, citation) in place"},
    {":", a_command, "command line: :w save, :q close tab, :e file, :42 page"},
    {"<C-j>", a_form_next, "next form field"}, {"<C-k>", a_form_prev, "previous form field"},
    {"<Tab>", a_form_next, NULL}, {"<S-Tab>", a_form_prev, NULL},
    {"<C-,>", a_tab_prev, "previous tab"}, {"<C-.>", a_tab_next, "next tab"},
    {"<C-n>", a_new_window, "new window on the same PDF"}, {"<C-s>", a_save, "save"},
    {"<C-p>", a_print, "print (opens :print; Enter prints everything)"},
    MARK_KEYS, {"<Esc>", a_clear, "clear the search"}, {"q", a_quit, "close the window"},
};

static const Binding caret_map[] = {
    {"h", a_caret_move, "move"}, {"j", a_caret_move, "move"}, {"k", a_caret_move, "move"},
    {"l", a_caret_move, "move"}, {"w", a_caret_move, "next / previous word"},
    {"b", a_caret_move, "next / previous word"}, {"0", a_caret_move, "start / end of line"},
    {"$", a_caret_move, "start / end of line"}, {"v", a_caret_select, "start / stop selecting"},
    {"C", a_caret_mark, "highlight: pastel yellow"}, {"V", a_caret_mark, "highlight: pastel red"},
    {"B", a_caret_mark, "highlight: pastel green"}, {"N", a_caret_mark, "highlight: pastel pink"},
    {"M", a_caret_mark, "highlight: pastel blue"},
    {"c", a_caret_comment, "highlight with a comment, or edit the comment"},
    {"y", a_caret_copy, "copy the selection"}, {"x", a_note_delete, "delete the highlight under the cursor"},
    {"<C-v>", a_area, "switch to the free cursor"}, {"<Esc>", a_leave, "cancel the selection / leave"},
    {"<Enter>", a_source, "LaTeX: open this spot in the editor (SyncTeX)"},
    SCROLL_KEYS, MARK_KEYS,
};

static const Binding area_map[] = {
    {"h", a_area_move, "move"}, {"j", a_area_move, "move"}, {"k", a_area_move, "move"},
    {"l", a_area_move, "move"}, {"w", a_area_move, "move faster"}, {"b", a_area_move, "move faster"},
    {"J", a_area_move, "move faster"}, {"K", a_area_move, "move faster"},
    {"<C-h>", a_area_move, "move faster"}, {"<C-j>", a_area_move, "move faster"},
    {"<C-k>", a_area_move, "move faster"}, {"<C-l>", a_area_move, "move faster"},
    {"v", a_area_select, "start / stop the rectangle"},
    {"C", a_area_mark, "highlight the area: pastel yellow"}, {"V", a_area_mark, "highlight the area: pastel red"},
    {"B", a_area_mark, "highlight the area: pastel green"}, {"N", a_area_mark, "highlight the area: pastel pink"},
    {"M", a_area_mark, "highlight the area: pastel blue"},
    {"c", a_area_comment, "highlight with a comment, or edit the comment"},
    {"x", a_note_delete, "delete the highlight under the cursor"},
    {"<C-v>", a_to_caret, "back to the text cursor"}, {"<Esc>", a_leave, "cancel / leave"},
    {"<Enter>", a_source, "LaTeX: open this spot in the editor (SyncTeX)"},
    SCROLL_KEYS, MARK_KEYS,
};

static const Binding panel_map[] = {
    {"j", a_panel_down, "move"}, {"k", a_panel_up, "move"}, {"<Down>", a_panel_down, NULL},
    {"<Up>", a_panel_up, NULL}, {"gg", a_panel_top, "first / last"},
    {"G", a_panel_bottom, "first / last"}, {"<Enter>", a_panel_go, "go to the entry"},
    {"r", a_panel_rename, "rename the mark / edit the comment"},
    {"d", a_panel_uncomment, "delete only the comment"},
    {"x", a_panel_delete, "delete the highlight (with its comment) or the mark"},
    {"<Tab>", a_panel_next_tab, "next / previous panel tab"}, {"l", a_panel_next_tab, "next / previous panel tab"},
    {"<S-Tab>", a_panel_prev_tab, "next / previous panel tab"}, {"h", a_panel_prev_tab, "next / previous panel tab"},
    {"1", a_panel_tab, "go to panel tab 1-4"}, {"2", a_panel_tab, "go to panel tab 1-4"},
    {"3", a_panel_tab, "go to panel tab 1-4"}, {"4", a_panel_tab, "go to panel tab 1-4"},
    {"<leader>c", a_help, NULL}, {"<Esc>", a_panel_close, "close"}, {"q", a_panel_close, "close"},
};

static const Binding help_map[] = {
    {"j", a_help_down, NULL}, {"k", a_help_up, NULL}, {"<Down>", a_help_down, NULL},
    {"<Up>", a_help_up, NULL}, {"<C-d>", a_help_page_down, NULL}, {"<C-u>", a_help_page_up, NULL},
    {"<PageDown>", a_help_page_down, NULL}, {"<PageUp>", a_help_page_up, NULL},
    {"gg", a_help_top, NULL}, {"G", a_help_bottom, NULL},
    {"<leader>c", a_help_close, NULL}, {"<Esc>", a_help_close, NULL}, {"q", a_help_close, NULL},
};

static const Binding preview_map[] = {
    {"j", a_preview_down, "scroll the preview"}, {"k", a_preview_up, "scroll the preview"},
    {"<Down>", a_preview_down, NULL}, {"<Up>", a_preview_up, NULL},
    {"<C-d>", a_preview_page_down, "scroll the preview faster"}, {"<C-u>", a_preview_page_up, "scroll the preview faster"},
    {"<Enter>", a_preview_go, "go there (Ctrl-o returns)"},
    {"<Esc>", a_preview_close, "close"}, {"q", a_preview_close, "close"}, {"F", a_preview_close, NULL},
};

static const Binding *keymap(Mode m, int *n)
{
    switch (m) {
    case MODE_NORMAL: *n = SDL_arraysize(normal_map); return normal_map;
    case MODE_CARET: *n = SDL_arraysize(caret_map); return caret_map;
    case MODE_AREA: *n = SDL_arraysize(area_map); return area_map;
    case MODE_PANEL: *n = SDL_arraysize(panel_map); return panel_map;
    case MODE_HELP: *n = SDL_arraysize(help_map); return help_map;
    case MODE_PREVIEW: *n = SDL_arraysize(preview_map); return preview_map;
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
    if (w->mode == MODE_PROMPT && w->prompt_box) { /* the comment box */
        comment_editor_key(w, tok);
        return;
    }
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
    if (w->mode == MODE_HINTS) { /* letters pick a link */
        npend = 0;
        hints_key(w, tok);
        return;
    }
    /* a count, unless a sequence is under way ("m1" is not a count); in the
     * panel digits pick a tab */
    if (!npend && w->mode != MODE_PANEL && ((tok[0] >= '1' && tok[0] <= '9') || (tok[0] == '0' && w->count)) &&
        !tok[1]) {
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

/* ---------- help screen content, generated from the keymaps ---------- */

/* "<leader>m*" -> "\\m<letter>", "<C-j>" -> "Ctrl-j", " " -> "Space". */
static void key_label(const char *pattern, char *out, size_t cap)
{
    char t[TOKENS_MAX][TOKEN_LEN];
    int n = pattern_tokens(pattern, t);
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        char part[40];
        if (!strcmp(t[i], "*"))
            snprintf(part, sizeof part, "<letter>");
        else if (!strcmp(t[i], " ") && i == 0 && !strncmp(pattern, "<leader>", 8))
            snprintf(part, sizeof part, "space ");
        else if (!strcmp(t[i], " "))
            snprintf(part, sizeof part, "Space");
        else if (!strncmp(t[i], "<C-", 3))
            snprintf(part, sizeof part, "Ctrl-%.*s", (int)strlen(t[i]) - 4, t[i] + 3);
        else if (t[i][0] == '<' && t[i][1])
            snprintf(part, sizeof part, "%.*s", (int)strlen(t[i]) - 2, t[i] + 1);
        else
            snprintf(part, sizeof part, "%s", t[i]);
        SDL_strlcat(out, part, cap);
    }
}

static HelpLine *help_add(HelpLine **lines, int *n, int *cap)
{
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *lines = xrealloc(*lines, *cap * sizeof **lines);
    }
    HelpLine *l = &(*lines)[(*n)++];
    memset(l, 0, sizeof *l);
    return l;
}

static bool in_normal_map(const Binding *b)
{
    for (size_t i = 0; i < SDL_arraysize(normal_map); i++)
        if (!strcmp(normal_map[i].keys, b->keys) && normal_map[i].help && b->help &&
            !strcmp(normal_map[i].help, b->help))
            return true;
    return false;
}

static void help_section(HelpLine **lines, int *n, int *cap, const char *title, const Binding *map, size_t count,
                         bool skip_shared)
{
    snprintf(help_add(lines, n, cap)->keys, sizeof (*lines)->keys, "%s", title);
    (*lines)[*n - 1].header = true;
    for (size_t i = 0; i < count; i++) {
        const Binding *b = &map[i];
        if (!b->help || (skip_shared && in_normal_map(b)))
            continue;
        char label[48];
        key_label(b->keys, label, sizeof label);
        HelpLine *prev = *n ? &(*lines)[*n - 1] : NULL;
        if (prev && !prev->header && prev->text && !strcmp(prev->text, b->help)) {
            /* same description as the previous key: list them together */
            SDL_strlcat(prev->keys, " ", sizeof prev->keys);
            SDL_strlcat(prev->keys, label, sizeof prev->keys);
            continue;
        }
        HelpLine *l = help_add(lines, n, cap);
        snprintf(l->keys, sizeof l->keys, "%s", label);
        l->text = b->help;
    }
}

int help_lines(HelpLine **out)
{
    static const struct { const char *keys, *text; } form[] = {
        {"type", "fill in the field"}, {"Space", "tick / untick a checkbox"},
        {"Ctrl-j Tab", "next field"}, {"Ctrl-k", "previous field"},
        {"Ctrl-a Ctrl-z Ctrl-v", "select all / undo / paste"}, {"Esc", "leave the field"},
    };
    HelpLine *lines = NULL;
    int n = 0, cap = 0;
    help_section(&lines, &n, &cap, "Reading", normal_map, SDL_arraysize(normal_map), false);
    help_section(&lines, &n, &cap, "Text cursor (v)", caret_map, SDL_arraysize(caret_map), true);
    help_section(&lines, &n, &cap, "Free cursor (Ctrl-v)", area_map, SDL_arraysize(area_map), true);
    char panel_title[48];
    snprintf(panel_title, sizeof panel_title, "Panel (%sl)", leader_label());
    help_section(&lines, &n, &cap, panel_title, panel_map, SDL_arraysize(panel_map), false);
    help_section(&lines, &n, &cap, "Reference preview (F)", preview_map, SDL_arraysize(preview_map), false);
    snprintf(help_add(&lines, &n, &cap)->keys, sizeof lines->keys, "Commands (:, Tab completes)");
    lines[n - 1].header = true;
    for (int i = 0; i < command_count(); i++) {
        HelpLine *l = help_add(&lines, &n, &cap);
        command_describe(i, l->keys, sizeof l->keys, &l->text);
    }
    static const struct { const char *keys, *text; } box[] = {
        {"$...$", "an equation, typeset by LaTeX as you write"}, {"$$...$$", "an equation on its own line"},
        {"Enter", "save"}, {"Shift-Enter", "new line"}, {"Esc", "cancel"},
        {"arrows Home End", "move (a formula shows its source with the cursor inside)"},
        {"Ctrl-w Ctrl-u Ctrl-v", "delete a word / delete everything / paste"},
    };
    snprintf(help_add(&lines, &n, &cap)->keys, sizeof lines->keys, "Comment box (c)");
    lines[n - 1].header = true;
    for (size_t i = 0; i < SDL_arraysize(box); i++) {
        HelpLine *l = help_add(&lines, &n, &cap);
        snprintf(l->keys, sizeof l->keys, "%s", box[i].keys);
        l->text = box[i].text;
    }
    snprintf(help_add(&lines, &n, &cap)->keys, sizeof lines->keys, "Forms (inside a field)");
    lines[n - 1].header = true;
    for (size_t i = 0; i < SDL_arraysize(form); i++) {
        HelpLine *l = help_add(&lines, &n, &cap);
        snprintf(l->keys, sizeof l->keys, "%s", form[i].keys);
        l->text = form[i].text;
    }
    *out = lines;
    return n;
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
    } else if ((e->key == SDLK_RETURN || e->key == SDLK_KP_ENTER) && (e->mod & SDL_KMOD_SHIFT)) {
        snprintf(tok, sizeof tok, "<S-Enter>");
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
    if (w->mode == MODE_PROMPT && w->prompt_box) {
        comment_editor_insert(w, text);
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
