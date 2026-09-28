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
    v->highlight_all = false;
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
static void run_search(Window *w, const char *text) { search_commit(w, cur_view(w), text); }

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
    search_begin(w, v);
    prompt_open(w, v->match_case ? "/Aa " : "/", "", run_search, 0);
}

/* Whether the open prompt's action refers to the document (a comment, a mark
 * name): such a prompt is cancelled when the document is reloaded. */
const char *prompt_kind(Window *w)
{
    if (w->prompt_done == command_run)
        return "COMMAND";
    if (w->prompt_done == run_search)
        return "SEARCH";
    if (w->prompt_secret)
        return "PASSWORD";
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
    w->prompt_secret = false;
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
    if (w->prompt_done == run_search) /* incremental search */
        search_live(w, cur_view(w), w->cmd);
}

static void prompt_special(Window *w, const char *tok)
{
    bool completing = w->prompt_done == command_run && (!strcmp(tok, "<Tab>") || !strcmp(tok, "<S-Tab>"));
    if (completing) {
        command_complete(w, tok[1] == 'S' ? -1 : 1);
        return;
    }
    command_complete_reset(w);
    bool searching = w->prompt_done == run_search;
    if (!strcmp(tok, "<Esc>")) {
        w->mode = w->prompt_return;
        if (searching)
            search_cancel(w, cur_view(w));
        if (w->prompt_secret) /* a password not used: wiped */
            explicit_bzero(w->cmd, sizeof w->cmd), w->cmdlen = 0;
    } else if (searching && !strcmp(tok, "<C-t>")) { /* match case, or smart case */
        View *v = cur_view(w);
        v->match_case = !v->match_case;
        snprintf(w->prompt_label, sizeof w->prompt_label, "%s", v->match_case ? "/Aa " : "/");
        search_live(w, v, w->cmd);
    } else if (!strcmp(tok, "<Enter>") || !strcmp(tok, "<S-Enter>")) {
        w->mode = w->prompt_return;
        char text[CMD_MAX];
        snprintf(text, sizeof text, "%s", w->cmd);
        w->prompt_done(w, text);
    } else if (!strcmp(tok, "<BS>")) {
        if (w->cmdlen == 0 && (w->prompt_done == command_run || searching)) {
            w->mode = w->prompt_return; /* backspace on an empty ':' or '/' line */
            if (searching)
                search_cancel(w, cur_view(w));
            return;
        }
        while (w->cmdlen > 0 && ((unsigned char)w->cmd[--w->cmdlen] & 0xC0) == 0x80)
            ;
        w->cmd[w->cmdlen] = 0;
        if (searching)
            search_live(w, cur_view(w), w->cmd);
    } else if (!strcmp(tok, "<C-u>")) {
        w->cmd[w->cmdlen = 0] = 0;
        if (searching)
            search_live(w, cur_view(w), w->cmd);
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
static void a_caret_copy(Window *w, View *v, int c, const char *a) { (void)c, (void)a; caret_copy(w, v, false); }
static void a_caret_cite(Window *w, View *v, int c, const char *a) { (void)c, (void)a; caret_copy(w, v, true); }

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
static void a_panel_filter(Window *w, View *v, int c, const char *a) { (void)v, (void)c; panel_filter(w, a[0]); }
static void a_panel_close(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_close(w); }
static void a_panel_next_tab(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_switch(w, -2); }
static void a_panel_prev_tab(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; panel_switch(w, -1); }
static void a_panel_tab(Window *w, View *v, int c, const char *a) { (void)v, (void)c; panel_switch(w, a[0] - '1'); }

/* ---------- help screen actions ---------- */
static void a_help(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; help_open(w); }
static void a_help_close(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; w->mode = w->help_return; }
static void a_help_escape(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; help_escape(w); }
static void a_help_down(Window *w, View *v, int c, const char *a) { (void)v, (void)a; help_move(w, n1(c)); }
static void a_help_up(Window *w, View *v, int c, const char *a) { (void)v, (void)a; help_move(w, -n1(c)); }
static void a_help_page_down(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; help_move(w, 10); }
static void a_help_page_up(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; help_move(w, -10); }
static void a_help_top(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; help_edge(w, false); }
static void a_help_bottom(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; help_edge(w, true); }
static void a_help_next_tab(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; help_switch(w, -2); }
static void a_help_prev_tab(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; help_switch(w, -1); }
static void a_help_tab(Window *w, View *v, int c, const char *a) { (void)v, (void)c; help_switch(w, a[0] - '1'); }
static void a_help_search(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; help_search(w); }

/* ---------- links and reference preview ---------- */
static void a_follow(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; links_start(w, false); }
static void a_peek(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; links_start(w, true); }
static void a_preview_down(Window *w, View *v, int c, const char *a) { (void)v, (void)a; preview_scroll(w, (float)n1(c)); }
static void a_preview_up(Window *w, View *v, int c, const char *a) { (void)v, (void)a; preview_scroll(w, -(float)n1(c)); }
static void a_preview_page_down(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; preview_scroll(w, 6); }
static void a_preview_page_up(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; preview_scroll(w, -6); }
static void a_preview_go(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; preview_go(w); }
static void a_preview_close(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; preview_close(w); }

/* ---------- images and moving / resizing ---------- */
#define MOVE_STEP 2.0f  /* points per h j k l */
#define MOVE_FAST 12.0f /* Ctrl-h j k l */
static void a_recent(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; picker_open(w, false); }
static void a_split(Window *w, View *v, int c, const char *a)
{
    (void)v, (void)c;
    char k = a[0] == '<' ? a[3] : a[0]; /* "<C-w>" as the second key: Ctrl-w Ctrl-w */
    if (k == 'v' || k == 's')
        split_open(w, k == 'v', NULL);
    else if (k == 'q' || k == 'c')
        split_close(w, true);
    else if (k == 'o')
        split_close(w, false);
    else if (k == 'x')
        split_swap(w);
    else if (strchr("hjkl", k))
        split_focus_dir(w, k);
    else
        split_focus_other(w);
}

static void a_library(Window *w, View *v, int c, const char *a)
{
    (void)v, (void)c;
    library_open(w, a[0] == 'n');
}

static void a_find(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; picker_open(w, true); }
static void a_grid(Window *w, View *v, int c, const char *a) { (void)c, (void)a; grid_open(w, v); }
static void a_grid_h(Window *w, View *v, int c, const char *a) { (void)v, (void)a; grid_move(w, -n1(c), 0); }
static void a_grid_l(Window *w, View *v, int c, const char *a) { (void)v, (void)a; grid_move(w, n1(c), 0); }
static void a_grid_j(Window *w, View *v, int c, const char *a) { (void)v, (void)a; grid_move(w, 0, n1(c)); }
static void a_grid_k(Window *w, View *v, int c, const char *a) { (void)v, (void)a; grid_move(w, 0, -n1(c)); }
static void a_grid_down(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; grid_screen(w, 1); }
static void a_grid_up(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; grid_screen(w, -1); }
static void a_grid_first(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; grid_edge(w, false); }
static void a_grid_last(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; grid_edge(w, true); }
static void a_grid_bigger(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; grid_zoom(w, 1); }
static void a_grid_smaller(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; grid_zoom(w, -1); }
static void a_grid_go(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; grid_go(w); }

static void a_grid_choose(Window *w, View *v, int c, const char *a)
{
    (void)v, (void)c, (void)a;
    w->grid_anchor = w->grid_anchor >= 0 ? -1 : w->grid_sel;
    w->redraw = true;
}

static void a_grid_mark(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; grid_mark(w); }

static void a_grid_delete(Window *w, View *v, int c, const char *a)
{
    (void)c, (void)a;
    bool *pick = grid_pick(w, v);
    int first = w->grid_sel;
    for (int i = v->doc->npages - 1; i >= 0; i--)
        first = pick[i] ? i : first;
    pages_delete(w, v->doc, pick);
    free(pick);
    w->grid_sel = SDL_clamp(first, 0, v->doc->npages - 1);
}

static void a_grid_rotate(Window *w, View *v, int c, const char *a)
{
    (void)c;
    int anchor = w->grid_anchor;
    bool *pick = grid_pick(w, v);
    pages_rotate(w, v->doc, pick, a[0] == 'R' ? -1 : 1);
    free(pick);
    w->grid_anchor = anchor; /* still chosen: turn again, or move them */
}

/* < > and the arrows: the pages chosen, side by side, a place (a row for
 * up / down) earlier or later. */
static void a_grid_shift(Window *w, View *v, int c, const char *a)
{
    bool back = !strcmp(a, "<lt>") || !strcmp(a, "<Left>") || !strcmp(a, "<Up>");
    bool row = !strcmp(a, "<Up>") || !strcmp(a, "<Down>");
    int first, last, step = (back ? -1 : 1) * (row ? grid_cols(w) : 1) * n1(c);
    bool *pick = grid_pick(w, v);
    bool span = pages_pick_span(pick, v->doc->npages, &first, &last);
    free(pick);
    if (!span) {
        ui_message(w, "only pages side by side move together (x r y work on these)");
        return;
    }
    bool chosen = w->grid_anchor >= 0;
    int anchor_off = chosen ? w->grid_anchor - first : 0, sel_off = w->grid_sel - first;
    if (sel_off < 0 || sel_off > last - first)
        sel_off = 0; /* chosen with Space, the frame elsewhere: it goes with them */
    int to = pages_move(w, v->doc, first, last, first + step);
    if (to < 0)
        return;
    w->grid_sel = to + sel_off; /* the pages stay chosen where they went */
    w->grid_anchor = chosen ? to + anchor_off : -1;
    grid_move(w, 0, 0);         /* in sight */
    ui_message(w, "moved to p. %d   u undoes it", to + 1);
}

static void extract_done(Window *w, const char *text)
{
    View *v = cur_view(w);
    if (*text && v && w->grid_extract && w->grid_extract_n == v->doc->npages) {
        char *path = expand_home(text);
        pages_extract(w, v->doc, w->grid_extract, path);
        free(path);
    }
    free(w->grid_extract);
    w->grid_extract = NULL;
}

static void a_grid_extract(Window *w, View *v, int c, const char *a)
{
    (void)c, (void)a;
    free(w->grid_extract);
    w->grid_extract = grid_pick(w, v); /* kept until the name is typed */
    w->grid_extract_n = v->doc->npages;
    char name[CMD_MAX - 8];
    pages_default_name(v->doc, w->grid_extract, name, sizeof name);
    prompt_open(w, "save the pages as: ", name, extract_done, 0);
}

static void a_grid_insert(Window *w, View *v, int c, const char *a)
{
    (void)v, (void)c, (void)a;
    prompt_open(w, ":", "insert ", command_run, 0); /* :insert after the page selected; Tab completes */
}
static void a_grid_close(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; grid_close(w); }
static void a_present(Window *w, View *v, int c, const char *a) { (void)c, (void)a; present_start(w, v); }
static void a_present_next(Window *w, View *v, int c, const char *a) { (void)v, (void)a; present_step(w, n1(c)); }
static void a_present_prev(Window *w, View *v, int c, const char *a) { (void)v, (void)a; present_step(w, -n1(c)); }
static void a_present_first(Window *w, View *v, int c, const char *a) { (void)v, (void)a; present_go(w, c ? c - 1 : 0); }
static void a_present_last(Window *w, View *v, int c, const char *a) { (void)a; present_go(w, c ? c - 1 : v->doc->npages - 1); }
static void a_present_blank(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; present_blank(w); }
static void a_present_stop(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; present_stop(w); }
static void a_diff_next(Window *w, View *v, int c, const char *a) { (void)a; for (int i = 0; i < SDL_min(n1(c), 500); i++) diff_step(w, v, 1); }
static void a_diff_prev(Window *w, View *v, int c, const char *a) { (void)a; for (int i = 0; i < SDL_min(n1(c), 500); i++) diff_step(w, v, -1); }
static void a_copy_region(Window *w, View *v, int c, const char *a) { (void)c; image_copy_region(w, v, a[0] == 'Y'); }
/* u / Ctrl-r: undo or redo the last change to highlights, comments, images. */
static void a_undo(Window *w, View *v, int c, const char *a)
{
    bool redo = !strcmp(a, "<C-r>");
    const char *what = NULL;
    int page = -1;
    for (int i = 0; i < SDL_min(n1(c), 200); i++) {
        const char *got = doc_undo(v->doc, redo, &page);
        if (!got)
            break;
        what = got;
    }
    if (!what) {
        ui_message(w, redo ? "nothing to redo" : "nothing to undo");
        return;
    }
    if (page >= 0 && (page < view_top_page(v) || page > view_top_page(v) + 2))
        jump_to(w, v, (Pos){page, 0}); /* show where it happened */
    ui_message(w, "%s: %s (p. %d)   %s", redo ? "redone" : "undone", what, page + 1,
               redo ? "u undoes it" : "Ctrl-r redoes it");
    w->redraw = true;
}

static void a_about(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; about_open(w); }
static void a_themes(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; theme_open(w); }
static void a_image_file(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; prompt_open(w, ":", "image ", command_run, 0); }
static void a_image_paste(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; image_paste(w); }
static void a_object_edit(Window *w, View *v, int c, const char *a) { (void)c, (void)a; object_begin_at_cursor(w, v); }

static void a_object_move(Window *w, View *v, int c, const char *a)
{
    (void)v;
    float s = !strncmp(a, "<C-", 3) ? MOVE_FAST : MOVE_STEP, n = (float)n1(c);
    char k = !strncmp(a, "<C-", 3) ? a[3] : a[0];
    if (!strcmp(a, "<Left>")) k = 'h';
    if (!strcmp(a, "<Right>")) k = 'l';
    if (!strcmp(a, "<Up>")) k = 'k';
    if (!strcmp(a, "<Down>")) k = 'j';
    object_move(w, (k == 'l' ? s : k == 'h' ? -s : 0) * n, (k == 'k' ? s : k == 'j' ? -s : 0) * n);
}

/* H L narrower / wider, K J shorter / taller: 4% of the size a step. */
static void a_object_resize(Window *w, View *v, int c, const char *a)
{
    (void)v;
    PRect r = w->obj_rect;
    float n = (float)n1(c), sw = SDL_max(2.0f, (r.r - r.l) * 0.04f) * n, sh = SDL_max(2.0f, (r.t - r.b) * 0.04f) * n;
    object_resize(w, a[0] == 'L' ? sw : a[0] == 'H' ? -sw : 0, a[0] == 'J' ? sh : a[0] == 'K' ? -sh : 0);
}

static void a_object_bigger(Window *w, View *v, int c, const char *a) { (void)v, (void)a; object_scale(w, SDL_powf(1.05f, (float)n1(c))); }
static void a_object_smaller(Window *w, View *v, int c, const char *a) { (void)v, (void)a; object_scale(w, SDL_powf(1 / 1.05f, (float)n1(c))); }
static void a_object_ratio(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; object_proportions(w); }
static void a_object_done(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; object_done(w, true); }
static void a_object_cancel(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; object_done(w, false); }
static void a_object_delete(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; object_delete(w); }
static void a_object_turn(Window *w, View *v, int c, const char *a) { (void)v; for (int i = 0; i < n1(c) % 4; i++) object_rotate(w, a[0] == 'r' ? 1 : -1); }
static void a_crop_begin(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; object_crop_begin(w); }
static void a_crop_reset(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; object_crop_reset(w); }
static void a_crop_done(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; object_crop_done(w, true); }
static void a_crop_cancel(Window *w, View *v, int c, const char *a) { (void)v, (void)c, (void)a; object_crop_done(w, false); }

/* h j k l cut that side (Ctrl: more), H J K L give it back. */
static void a_crop_side(Window *w, View *v, int c, const char *a)
{
    (void)v;
    bool fast = !strncmp(a, "<C-", 3);
    char k = fast ? a[3] : a[0];
    bool back = k >= 'A' && k <= 'Z';
    int side = strchr("hH", k) ? 0 : strchr("lL", k) ? 1 : strchr("kK", k) ? 2 : 3;
    object_crop(w, side, (fast ? 0.1f : 0.02f) * (float)n1(c) * (back ? -1 : 1));
}

/* ---------- keymaps ---------- */
/* A titled group on the help screen; "<#>" is never typed, so it never runs. */
#define GROUP(title) {"<#>", NULL, title}

#define SCROLL_KEYS                                                                              \
    {"<C-d>", a_half_down, "half a screen down"}, {"<C-u>", a_half_up, "half a screen up"},          \
    {"<C-f>", a_page_down, "a screen down"}, {"<C-b>", a_page_up, "a screen up"},     \
    {"<PageDown>", a_page_down, NULL}, {"<PageUp>", a_page_up, NULL}, {"<Down>", a_down, NULL},    \
    {"<Up>", a_up, NULL}, {"<Left>", a_left, NULL}, {"<Right>", a_right, NULL}

/* Marks, jumps, the panel and this help work in every reading mode. */
#define JUMP_KEYS                                                                                \
    {"<leader>m*", a_mark_set, "set a mark here (a-z)"},                                            \
    {"m*", a_mark_jump, "jump to a mark"}, {"mm", a_jump_back, "back to where you were"},            \
    {"<C-o>", a_jump_older, "older position in the jump list"},                                     \
    {"<C-i>", a_jump_newer, "newer position in the jump list"}
#define OVERLAY_KEYS                                                                             \
    {"<leader>l", a_panel, "panel: tabs, summary, marks, highlights"},                              \
    {"<leader>c", a_help, "this help (again closes it)"}
#define UNDO_KEYS                                                                                \
    {"u", a_undo, "undo the last change to highlights, comments, images"},                          \
    {"<C-r>", a_undo, "redo"}
#define MARK_KEYS JUMP_KEYS, OVERLAY_KEYS, UNDO_KEYS

static const Binding normal_map[] = {
    GROUP("Moving"),
    {"j", a_down, "scroll"}, {"k", a_up, "scroll"}, {"h", a_left, "scroll"}, {"l", a_right, "scroll"},
    {" ", a_page_down, "a screen down"}, SCROLL_KEYS,
    {"<Home>", a_first, NULL}, {"<End>", a_last, NULL},
    {"J", a_next_page, "next page"}, {"K", a_prev_page, "previous page"},
    {"gg", a_first, "first page (42gg: page 42)"}, {"G", a_last, "last page (42G: page 42)"},
    GROUP("View"),
    {"+", a_zoom_in, "zoom"}, {"-", a_zoom_out, "zoom"}, {"=", a_fit_width, "fit width"},
    {"z", a_fit_page, "fit page"}, {"<C-=>", a_zoom_in, NULL}, {"<C-->", a_zoom_out, NULL},
    {"d", a_dual, "two pages side by side"}, {"i", a_dark, "dark mode"},
    {"r", a_rotate_cw, "rotate 90° clockwise"}, {"R", a_rotate_ccw, "rotate 90° counter-clockwise"},
    GROUP("Search and links"),
    {"/", a_search, "search"}, {"n", a_search_next, "next match"},
    {"N", a_search_prev, "previous match"}, {"<Esc>", a_clear, "clear the search"},
    {"]c", a_diff_next, "next / previous change (after :diff old.pdf)"},
    {"[c", a_diff_prev, "next / previous change (after :diff old.pdf)"},
    {"f", a_follow, "follow a link: type its letters (Ctrl-o returns)"},
    {"F", a_peek, "preview a reference (figure, equation, citation) in place"},
    GROUP("Highlights and comments"),
    {"v", a_caret, "text cursor: highlight, comment, copy"},
    UNDO_KEYS,
    {"<C-v>", a_area, "free cursor: mark figures and areas"},
    GROUP("Marks and jumps"),
    JUMP_KEYS,
    GROUP("Forms"),
    {"<C-j>", a_form_next, "next form field"}, {"<C-k>", a_form_prev, "previous form field"},
    {"<Tab>", a_form_next, NULL}, {"<S-Tab>", a_form_prev, NULL},
    GROUP("Files, tabs and windows"),
    {":", a_command, "command line: :w save, :q close tab, :e file, :42 page"},
    {"<leader>o", a_recent, "open a recent file (type to filter)"},
    {"<leader>f", a_find, "find any PDF in your folders (type to filter)"},
    {"<leader>/", a_library, "search the text of all your PDFs"},
    {"<leader>n", a_library, "search the comments and highlights of all your PDFs"},
    {"<leader>g", a_grid, "all pages as thumbnails"},
    {"<leader>T", a_themes, "choose the colour theme (tried as you move)"},
    {"<leader>a", a_about, "about Nomadys-PDF: version, author, contact"},
    {"<F5>", a_present, "present: fullscreen, one page at a time"},
    {"<C-s>", a_save, "save"}, {"<C-p>", a_print, "print (opens :print; Enter prints everything)"},
    {"t", a_tabs, "show / hide tabs"},
    {"<C-,>", a_tab_prev, "previous tab"}, {"<C-.>", a_tab_next, "next tab"},
    {"<C-n>", a_new_window, "new window on the same PDF"},
    GROUP("Split view"),
    {"<C-w>v", a_split, "split: side by side (the same PDF; :vsplit other.pdf)"},
    {"<C-w>s", a_split, "split: one above the other (:split other.pdf)"},
    {"<C-w>w", a_split, "the other pane (also Ctrl-w Ctrl-w)"}, {"<C-w><C-w>", a_split, NULL},
    {"<C-w>h", a_split, "the pane to the left; j below, k above, l right"}, {"<C-w>j", a_split, NULL},
    {"<C-w>k", a_split, NULL}, {"<C-w>l", a_split, NULL},
    {"<C-w>x", a_split, "the panes change places"},
    {"<C-w>q", a_split, "close this pane (also Ctrl-w c, :close)"}, {"<C-w>c", a_split, NULL},
    {"<C-w>o", a_split, "keep only this pane (:only)"},
    OVERLAY_KEYS, {"q", a_quit, "close the window"},
};

static const Binding caret_map[] = {
    GROUP("Moving"),
    {"h", a_caret_move, "move"}, {"j", a_caret_move, "move"}, {"k", a_caret_move, "move"},
    {"l", a_caret_move, "move"}, {"w", a_caret_move, "next / previous word"},
    {"b", a_caret_move, "next / previous word"}, {"e", a_caret_move, "end of the word"},
    {"0", a_caret_move, "start / end of line"},
    {"$", a_caret_move, "start / end of line"},
    GROUP("Selecting and highlighting"),
    {"v", a_caret_select, "start / stop selecting"},
    {"C", a_caret_mark, "highlight: pastel yellow"}, {"V", a_caret_mark, "highlight: pastel red"},
    {"B", a_caret_mark, "highlight: pastel green"}, {"N", a_caret_mark, "highlight: pastel pink"},
    {"M", a_caret_mark, "highlight: pastel blue"},
    {"c", a_caret_comment, "highlight with a comment, or edit the comment"},
    {"y", a_caret_copy, "copy the selection"},
    {"Y", a_caret_cite, "copy it as a quotation: “…” (p. 5)"}, {"x", a_note_delete, "delete the highlight under the cursor"},
    GROUP("Other"),
    {"<C-v>", a_area, "switch to the free cursor"}, {"<Esc>", a_leave, "cancel the selection / leave"},
    {"<Enter>", a_source, "LaTeX: open this spot in the editor (SyncTeX)"},
    SCROLL_KEYS, MARK_KEYS,
};

static const Binding area_map[] = {
    GROUP("Moving"),
    {"h", a_area_move, "move"}, {"j", a_area_move, "move"}, {"k", a_area_move, "move"},
    {"l", a_area_move, "move"}, {"w", a_area_move, "move faster"}, {"b", a_area_move, "move faster"},
    {"J", a_area_move, "move faster"}, {"K", a_area_move, "move faster"},
    {"<C-h>", a_area_move, "move faster"}, {"<C-j>", a_area_move, "move faster"},
    {"<C-k>", a_area_move, "move faster"}, {"<C-l>", a_area_move, "move faster"},
    GROUP("Marking an area"),
    {"v", a_area_select, "start / stop the rectangle"},
    {"y", a_copy_region, "copy the rectangle as an image (for slides)"},
    {"Y", a_copy_region, "save the rectangle as a PNG next to the PDF"},
    {"C", a_area_mark, "highlight the area: pastel yellow"}, {"V", a_area_mark, "highlight the area: pastel red"},
    {"B", a_area_mark, "highlight the area: pastel green"}, {"N", a_area_mark, "highlight the area: pastel pink"},
    {"M", a_area_mark, "highlight the area: pastel blue"},
    {"c", a_area_comment, "highlight with a comment, or edit the comment"},
    {"x", a_note_delete, "delete the highlight under the cursor"},
    GROUP("Images, moving and resizing"),
    {"I", a_image_file, "put an image here (PNG, JPEG): inside the rectangle, or at the cross"},
    {"P", a_image_paste, "paste an image from the clipboard (a screenshot) here"},
    {"e", a_object_edit, "move / resize the image or highlighted area under the cross"},
    GROUP("Other"),
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
    {"C", a_panel_filter, "Highlights: show / hide a colour"}, {"V", a_panel_filter, "Highlights: show / hide a colour"},
    {"B", a_panel_filter, "Highlights: show / hide a colour"}, {"N", a_panel_filter, "Highlights: show / hide a colour"},
    {"M", a_panel_filter, "Highlights: show / hide a colour"},
    {"c", a_panel_filter, "Highlights: only those with a comment"}, {"a", a_panel_filter, "Highlights: all again"},
    {"e", a_panel_filter, "Highlights: export what is shown to Markdown"},
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
    {"<Tab>", a_help_next_tab, NULL}, {"l", a_help_next_tab, NULL}, {"<Right>", a_help_next_tab, NULL},
    {"<S-Tab>", a_help_prev_tab, NULL}, {"h", a_help_prev_tab, NULL}, {"<Left>", a_help_prev_tab, NULL},
    {"1", a_help_tab, NULL}, {"2", a_help_tab, NULL}, {"3", a_help_tab, NULL}, {"4", a_help_tab, NULL},
    {"5", a_help_tab, NULL}, {"6", a_help_tab, NULL}, {"7", a_help_tab, NULL}, {"8", a_help_tab, NULL},
    {"9", a_help_tab, NULL},
    {"/", a_help_search, NULL},
    {"<leader>c", a_help_close, NULL}, {"<Esc>", a_help_escape, NULL}, {"q", a_help_close, NULL},
};

static const Binding object_map[] = {
    GROUP("Moving"),
    {"h", a_object_move, "move"}, {"j", a_object_move, "move"}, {"k", a_object_move, "move"},
    {"l", a_object_move, "move"}, {"<Left>", a_object_move, NULL}, {"<Down>", a_object_move, NULL},
    {"<Up>", a_object_move, NULL}, {"<Right>", a_object_move, NULL},
    {"<C-h>", a_object_move, "move faster"}, {"<C-j>", a_object_move, "move faster"},
    {"<C-k>", a_object_move, "move faster"}, {"<C-l>", a_object_move, "move faster"},
    GROUP("Resizing"),
    {"+", a_object_bigger, "bigger / smaller, keeping the proportions"},
    {"-", a_object_smaller, "bigger / smaller, keeping the proportions"},
    {"=", a_object_ratio, "image: back to its own proportions"},
    {"H", a_object_resize, "narrower / wider"}, {"L", a_object_resize, "narrower / wider"},
    {"K", a_object_resize, "shorter / taller"}, {"J", a_object_resize, "shorter / taller"},
    GROUP("Images"),
    {"r", a_object_turn, "turn 90° clockwise / counter-clockwise"},
    {"R", a_object_turn, "turn 90° clockwise / counter-clockwise"},
    {"c", a_crop_begin, "crop (the picture is kept whole: cropping can be undone any time)"},
    GROUP("Finishing"),
    {"<Enter>", a_object_done, "done"}, {"<Esc>", a_object_cancel, "undo: put it back where it was"},
    {"x", a_object_delete, "delete it"},
};

static const Binding crop_map[] = {
    GROUP("Cutting"),
    {"h", a_crop_side, "cut the left / bottom / top / right side"}, {"j", a_crop_side, "cut the left / bottom / top / right side"},
    {"k", a_crop_side, "cut the left / bottom / top / right side"}, {"l", a_crop_side, "cut the left / bottom / top / right side"},
    {"<C-h>", a_crop_side, "cut faster"}, {"<C-j>", a_crop_side, "cut faster"},
    {"<C-k>", a_crop_side, "cut faster"}, {"<C-l>", a_crop_side, "cut faster"},
    {"H", a_crop_side, "give that side back"}, {"J", a_crop_side, "give that side back"},
    {"K", a_crop_side, "give that side back"}, {"L", a_crop_side, "give that side back"},
    {"u", a_crop_reset, "uncrop: the whole picture again"},
    GROUP("Finishing"),
    {"<Enter>", a_crop_done, "done (back to moving and resizing)"}, {"<Esc>", a_crop_cancel, "cancel this cropping"},
};

static const Binding grid_map[] = {
    GROUP("Choosing a page"),
    {"h", a_grid_h, "move"}, {"j", a_grid_j, "move"}, {"k", a_grid_k, "move"}, {"l", a_grid_l, "move"},
    {"J", a_grid_down, "a screen down / up"}, {"K", a_grid_up, "a screen down / up"},
    {"<C-d>", a_grid_down, NULL}, {"<C-u>", a_grid_up, NULL}, {"<PageDown>", a_grid_down, NULL}, {"<PageUp>", a_grid_up, NULL},
    {"gg", a_grid_first, "first / last page"}, {"G", a_grid_last, "first / last page"},
    {"+", a_grid_bigger, "bigger / smaller thumbnails"}, {"-", a_grid_smaller, "bigger / smaller thumbnails"},
    GROUP("Changing the pages (u undoes)"),
    {" ", a_grid_mark, "choose / unchoose this page (several, apart)"},
    {"v", a_grid_choose, "choose several pages (then move to the other end)"},
    {"x", a_grid_delete, "delete the page(s)"}, {"d", a_grid_delete, NULL},
    {"r", a_grid_rotate, "turn right / left, in the file"}, {"R", a_grid_rotate, "turn right / left, in the file"},
    {"<Left>", a_grid_shift, "move the page(s) one place earlier / later"},
    {"<Right>", a_grid_shift, "move the page(s) one place earlier / later"},
    {"<Up>", a_grid_shift, "move the page(s) a row up / down"},
    {"<Down>", a_grid_shift, "move the page(s) a row up / down"},
    {"<lt>", a_grid_shift, NULL}, {">", a_grid_shift, NULL},
    {"y", a_grid_extract, "save the page(s) as a new PDF"},
    {"i", a_grid_insert, "insert another PDF after this page"},
    {"u", a_undo, "undo / redo"}, {"<C-r>", a_undo, "undo / redo"},
    GROUP("Finishing"),
    {"<Enter>", a_grid_go, "go to that page (Ctrl-o returns)"},
    {"<Esc>", a_grid_close, "close"}, {"q", a_grid_close, "close"}, {"<leader>g", a_grid_close, NULL},
};

static const Binding present_map[] = {
    GROUP("Slides"),
    {"j", a_present_next, "next slide"}, {"l", a_present_next, "next slide"}, {" ", a_present_next, "next slide"},
    {"<Right>", a_present_next, "next slide"}, {"<Down>", a_present_next, "next slide"},
    {"<PageDown>", a_present_next, "next slide"}, {"<Enter>", a_present_next, "next slide"},
    {"k", a_present_prev, "previous slide"}, {"h", a_present_prev, "previous slide"},
    {"<Left>", a_present_prev, "previous slide"}, {"<Up>", a_present_prev, "previous slide"},
    {"<PageUp>", a_present_prev, "previous slide"}, {"<BS>", a_present_prev, "previous slide"},
    {"gg", a_present_first, "first / last slide (12G: slide 12)"}, {"G", a_present_last, "first / last slide (12G: slide 12)"},
    {".", a_present_blank, "black screen, and back"}, {"b", a_present_blank, "black screen, and back"},
    GROUP("Finishing"),
    {"<Esc>", a_present_stop, "stop (reading goes on at this slide)"}, {"q", a_present_stop, "stop (reading goes on at this slide)"},
    {"<F5>", a_present_stop, "stop (reading goes on at this slide)"},
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
    case MODE_OBJECT: *n = SDL_arraysize(object_map); return object_map;
    case MODE_CROP: *n = SDL_arraysize(crop_map); return crop_map;
    case MODE_GRID: *n = SDL_arraysize(grid_map); return grid_map;
    case MODE_PRESENT: *n = SDL_arraysize(present_map); return present_map;
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

static void token_run(Window *w, const char *tok);

/* Modes that work on the pages: in a split, on the focused pane's. */
static bool on_pages(Mode m)
{
    return m != MODE_GRID && m != MODE_PRESENT && m != MODE_HELP && m != MODE_PANEL && m != MODE_PICKER && m != MODE_LIBRARY &&
           m != MODE_THEMES && m != MODE_ABOUT;
}

void input_token(Window *w, const char *tok)
{
    bool pane = on_pages(w->mode) && pane_enter_focused(w);
    token_run(w, tok);
    if (pane && window_alive(w))
        pane_leave(w);
}

static void token_run(Window *w, const char *tok)
{
    if (w->mode == MODE_ABOUT) { /* any key closes it */
        about_key(w, tok);
        return;
    }
    if (w->mode == MODE_THEMES) { /* moving tries each theme */
        theme_key(w, tok);
        return;
    }
    if (w->mode == MODE_PICKER) { /* typing filters the recent files */
        picker_key(w, tok);
        return;
    }
    if (w->mode == MODE_LIBRARY) { /* typing searches every PDF */
        library_key(w, tok);
        return;
    }
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
    if (!npend && w->mode != MODE_PANEL && w->mode != MODE_HELP && ((tok[0] >= '1' && tok[0] <= '9') || (tok[0] == '0' && w->count)) &&
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
        if (i > 0 && t[i - 1][0] == '<' && t[i - 1][1]) /* "Ctrl-w v", not "Ctrl-wv" */
            SDL_strlcat(out, " ", cap);
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
        if (normal_map[i].fn && !strcmp(normal_map[i].keys, b->keys) && normal_map[i].help && b->help &&
            !strcmp(normal_map[i].help, b->help))
            return true;
    return false;
}

static void help_title(HelpLine **lines, int *n, int *cap, const char *title, bool tab)
{
    HelpLine *l = help_add(lines, n, cap);
    snprintf(l->keys, sizeof l->keys, "%s", title);
    l->header = tab;
    l->group = !tab;
}

static void help_section(HelpLine **lines, int *n, int *cap, const char *title, const Binding *map, size_t count,
                         bool skip_shared)
{
    help_title(lines, n, cap, title, true);
    for (size_t i = 0; i < count; i++) {
        const Binding *b = &map[i];
        if (!b->fn) { /* GROUP("...") */
            help_title(lines, n, cap, b->help, false);
            continue;
        }
        if (!b->help || (skip_shared && in_normal_map(b)))
            continue;
        char label[48];
        key_label(b->keys, label, sizeof label);
        HelpLine *prev = *n ? &(*lines)[*n - 1] : NULL;
        if (prev && !prev->header && !prev->group && prev->text && !strcmp(prev->text, b->help)) {
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

typedef struct { const char *keys, *text; } HelpPair;

static void help_static(HelpLine **lines, int *n, int *cap, const HelpPair *pairs, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!pairs[i].text) {
            help_title(lines, n, cap, pairs[i].keys, false);
            continue;
        }
        HelpLine *l = help_add(lines, n, cap);
        snprintf(l->keys, sizeof l->keys, "%s", pairs[i].keys);
        l->text = pairs[i].text;
    }
}

/* Every key, as tabs (header) holding titled groups (group) of entries.
 * Generated from the keymaps, so it never goes out of date. */
int help_lines(HelpLine **out)
{
    static const HelpPair box[] = {
        {"Equations", NULL},
        {"$...$", "an equation, typeset by LaTeX as you write"}, {"$$...$$", "an equation on its own line"},
        {"\\$", "a dollar sign"},
        {"Writing", NULL},
        {"Enter", "save"}, {"Shift-Enter", "new line"}, {"Esc", "cancel"},
        {"arrows Home End", "move (a formula shows its source with the cursor inside)"},
        {"BS Del", "delete a character"}, {"Ctrl-w", "delete the word before the cursor"},
        {"Ctrl-u", "delete everything"}, {"Ctrl-v", "paste"},
    };
    static const HelpPair form[] = {
        {"Inside a field", NULL},
        {"type", "fill in the field"}, {"Space", "tick / untick a checkbox"},
        {"Ctrl-j Tab", "next field"}, {"Ctrl-k S-Tab", "previous field"},
        {"Ctrl-a", "select all"}, {"Ctrl-z", "undo"}, {"Ctrl-v", "paste"}, {"Esc", "leave the field"},
    };
    HelpLine *lines = NULL;
    int n = 0, cap = 0;
    help_section(&lines, &n, &cap, "Reading", normal_map, SDL_arraysize(normal_map), false);
    help_section(&lines, &n, &cap, "Text cursor", caret_map, SDL_arraysize(caret_map), true);
    help_section(&lines, &n, &cap, "Free cursor", area_map, SDL_arraysize(area_map), true);
    help_title(&lines, &n, &cap, "Comments", true);
    help_static(&lines, &n, &cap, box, SDL_arraysize(box));
    help_section(&lines, &n, &cap, "Panel", panel_map, SDL_arraysize(panel_map), false);
    help_section(&lines, &n, &cap, "Preview", preview_map, SDL_arraysize(preview_map), false);
    help_title(&lines, &n, &cap, "Commands", true);
    help_title(&lines, &n, &cap, "Type : then the command (Tab completes)", false);
    for (int i = 0; i < command_count(); i++) {
        HelpLine *l = help_add(&lines, &n, &cap);
        command_describe(i, l->keys, sizeof l->keys, &l->text);
        l->command = true;
    }
    help_title(&lines, &n, &cap, "Forms", true);
    help_static(&lines, &n, &cap, form, SDL_arraysize(form));
    help_section(&lines, &n, &cap, "Move/resize", object_map, SDL_arraysize(object_map), false);
    help_section(&lines, &n, &cap, "Crop", crop_map, SDL_arraysize(crop_map), false);
    help_section(&lines, &n, &cap, "Pages", grid_map, SDL_arraysize(grid_map), false);
    help_section(&lines, &n, &cap, "Present", present_map, SDL_arraysize(present_map), false);
    /* "How to get here" at the top of every tab but Reading */
    static const struct { const char *tab, *keys, *text; } enter[] = {
        {"Text cursor", "v", "from reading: the text cursor"},
        {"Free cursor", "<C-v>", "from reading or the text cursor: the free cursor"},
        {"Comments", "c", "on a selection or a highlight (text or free cursor)"},
        {"Panel", "<leader>l :marks", "from reading or a cursor"},
        {"Preview", "F", "then the letters of a link"},
        {"Commands", ":", "from reading or a cursor"},
        {"Forms", "<C-j> <Tab>", "from reading: the next field (or click a field)"},
        {"Move/resize", "e", "on an image or highlighted area, in the free cursor (a new image starts here)"},
        {"Crop", "c", "on an image, while moving / resizing it"},
        {"Pages", "<leader>g :pages", "from reading"},
        {"Present", "<F5> :present", "from reading"},
    };
    HelpLine *with = xmalloc((size_t)(n + 2 * SDL_arraysize(enter) + 1) * sizeof *with);
    int m = 0;
    for (int i = 0; i < n; i++) {
        with[m++] = lines[i];
        for (size_t e = 0; lines[i].header && e < SDL_arraysize(enter); e++) {
            if (strcmp(lines[i].keys, enter[e].tab))
                continue;
            memset(&with[m], 0, sizeof with[m]);
            snprintf(with[m].keys, sizeof with[m].keys, "How to get here");
            with[m++].group = true;
            memset(&with[m], 0, sizeof with[m]);
            char buf[64], part[48];
            snprintf(buf, sizeof buf, "%s", enter[e].keys);
            for (char *save = NULL, *k = strtok_r(buf, " ", &save); k; k = strtok_r(NULL, " ", &save)) {
                if (k[0] == ':')
                    snprintf(part, sizeof part, "%s", k); /* a command, as typed */
                else
                    key_label(k, part, sizeof part);
                if (with[m].keys[0])
                    SDL_strlcat(with[m].keys, " ", sizeof with[m].keys);
                SDL_strlcat(with[m].keys, part, sizeof with[m].keys);
            }
            with[m++].text = enter[e].text;
        }
    }
    free(lines);
    lines = with;
    n = m;
    /* drop titles with nothing under them (groups of keys shared with Reading) */
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (lines[i].group && (i + 1 == n || lines[i + 1].group || lines[i + 1].header))
            continue;
        lines[k++] = lines[i];
    }
    *out = lines;
    return k;
}

/* ---------- SDL events to tokens ---------- */
bool input_key_event(Window *w, const SDL_KeyboardEvent *e)
{
    static const struct { SDL_Keycode key; const char *name; } names[] = {
        {SDLK_ESCAPE, "<Esc>"}, {SDLK_RETURN, "<Enter>"}, {SDLK_KP_ENTER, "<Enter>"},
        {SDLK_BACKSPACE, "<BS>"}, {SDLK_DELETE, "<Del>"}, {SDLK_UP, "<Up>"}, {SDLK_DOWN, "<Down>"},
        {SDLK_LEFT, "<Left>"}, {SDLK_RIGHT, "<Right>"}, {SDLK_PAGEUP, "<PageUp>"},
        {SDLK_PAGEDOWN, "<PageDown>"}, {SDLK_HOME, "<Home>"}, {SDLK_END, "<End>"}, {SDLK_F5, "<F5>"},
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
        bool pane = pane_enter_focused(w);
        form_text(w, text);
        if (pane)
            pane_leave(w);
        w->redraw = true;
        return;
    }
    if (w->mode == MODE_PICKER) {
        picker_text(w, text);
        return;
    }
    if (w->mode == MODE_LIBRARY) {
        library_text(w, text);
        return;
    }
    if (w->mode == MODE_THEMES) {
        theme_key(w, text);
        return;
    }
    if (w->mode == MODE_ABOUT) {
        about_key(w, text);
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
