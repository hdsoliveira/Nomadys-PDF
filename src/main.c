/* Nomadys-PDF: windows, tabs, input handling and the event loop. */
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpdf_fwlevent.h"
#include "nomadys.h"

#define VERSION "0.1.0"
#define BLINK_MS 530

Window *windows[MAX_WINDOWS];
int nwindows;
static bool blink_on = true;
static const char *capture_path;  /* test mode: save the next frame here */
static uint64_t blink_at;

static View *cur_view(Window *w) { return w->ntabs ? w->tabs[w->cur] : NULL; }
static int top_of(Window *w) { return ui_tabbar_height(w); }

static void update_title(Window *w)
{
    View *v = cur_view(w);
    if (!v)
        return;
    const char *name = strrchr(v->doc->path, '/');
    char title[512];
    snprintf(title, sizeof title, "%s - Nomadys-PDF", name ? name + 1 : v->doc->path);
    SDL_SetWindowTitle(w->win, title);
}

static void update_size(Window *w)
{
    int pw, ph, ww, wh;
    SDL_GetWindowSizeInPixels(w->win, &pw, &ph);
    SDL_GetWindowSize(w->win, &ww, &wh);
    w->W = pw;
    w->H = ph;
    w->density = ww > 0 ? (float)pw / ww : 1;
}

static void init_zoom(Window *w, View *v)
{
    if (!v->fit_width && v->zoom <= 0)
        v->zoom = cfg.zoom * 96.0 / 72.0 * w->density;
}

static Window *window_new(View *v)
{
    if (nwindows == MAX_WINDOWS) {
        view_free(v);
        return NULL;
    }
    Window *w = calloc(1, sizeof *w);
    w->win = SDL_CreateWindow("Nomadys-PDF", 900, 1100, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!w->win) {
        fprintf(stderr, "nomadys-pdf: %s\n", SDL_GetError());
        free(w);
        view_free(v);
        return NULL;
    }
    w->ren = SDL_CreateRenderer(w->win, NULL);
    SDL_SetRenderVSync(w->ren, 1);
    ui_init(w);
    update_size(w);
    w->tabs[w->ntabs++] = v;
    init_zoom(w, v);
    w->show_tabs = cfg.show_tabs;
    w->redraw = true;
    SDL_StartTextInput(w->win);
    windows[nwindows++] = w;
    update_title(w);
    return w;
}

static void window_close(Window *w)
{
    for (int i = 0; i < w->ntabs; i++) {
        /* textures belong to this renderer: free them before destroying it */
        view_free(w->tabs[i]);
    }
    SDL_DestroyTexture(w->font);
    SDL_DestroyRenderer(w->ren);
    SDL_DestroyWindow(w->win);
    for (int i = 0; i < nwindows; i++) {
        if (windows[i] == w) {
            windows[i] = windows[--nwindows];
            break;
        }
    }
    free(w);
}

static Window *window_by_id(SDL_WindowID id)
{
    for (int i = 0; i < nwindows; i++)
        if (SDL_GetWindowID(windows[i]->win) == id)
            return windows[i];
    return NULL;
}

/* Views of a document outside `except` (to know whether closing loses edits). */
static int other_views(Doc *d, View *except)
{
    int n = 0;
    for (int i = 0; i < nwindows; i++)
        for (int k = 0; k < windows[i]->ntabs; k++)
            n += windows[i]->tabs[k] != except && windows[i]->tabs[k]->doc == d;
    return n;
}

static bool unsaved_would_be_lost(Window *w, View *only)
{
    if (cfg.autosave)
        return false;
    for (int k = 0; k < w->ntabs; k++) {
        View *v = w->tabs[k];
        if (only && v != only)
            continue;
        if (v->doc->modified && other_views(v->doc, NULL) - 1 == 0)
            return true;
    }
    return false;
}

/* Returns false when the user must confirm first. */
static bool confirm_close(Window *w, View *only)
{
    uint64_t now = SDL_GetTicks();
    if (!unsaved_would_be_lost(w, only) || now < w->quit_warned)
        return true;
    w->quit_warned = now + 4000;
    ui_message(w, "alterações não salvas: Ctrl-s salva, repita para descartar");
    return false;
}

static void discard_changes_if_manual(View *v)
{
    if (!cfg.autosave && other_views(v->doc, v) == 0)
        v->doc->modified = false;
}

static void close_tab(Window *w, int i)
{
    View *v = w->tabs[i];
    discard_changes_if_manual(v);
    if (w->ntabs == 1) {
        window_close(w);
        return;
    }
    view_free(v);
    memmove(&w->tabs[i], &w->tabs[i + 1], (w->ntabs - i - 1) * sizeof *w->tabs);
    w->ntabs--;
    if (w->cur >= w->ntabs)
        w->cur = w->ntabs - 1;
    w->redraw = true;
    update_title(w);
}

static void close_window(Window *w)
{
    for (int i = 0; i < w->ntabs; i++)
        discard_changes_if_manual(w->tabs[i]);
    window_close(w);
}

static bool open_tab(Window *w, const char *path)
{
    char err[512];
    Doc *d = doc_open(path, err, sizeof err);
    if (!d) {
        ui_message(w, "%s", err);
        return false;
    }
    if (w->ntabs == MAX_TABS) {
        doc_unref(d);
        ui_message(w, "abas demais");
        return false;
    }
    View *v = view_new(d);
    init_zoom(w, v);
    w->tabs[w->ntabs++] = v;
    w->cur = w->ntabs - 1;
    w->tabs_flash_until = SDL_GetTicks() + 1000;
    update_title(w);
    w->redraw = true;
    return true;
}

static void save(Window *w, View *v)
{
    char err[512];
    doc_kill_focus(v->doc);
    if (w->mode == MODE_FORM)
        w->mode = MODE_NORMAL;
    if (doc_save(v->doc, err, sizeof err))
        ui_message(w, "salvo");
    else
        ui_message(w, "%s", err);
    w->quit_warned = 0;
}

/* ---------- scrolling and zoom ---------- */
static void scroll(Window *w, double dx, double dy)
{
    View *v = cur_view(w);
    v->sx += dx;
    v->sy += dy;
    view_clamp(v, w->W, w->H, top_of(w));
    w->redraw = true;
}

static void zoom_by(Window *w, double f)
{
    View *v = cur_view(w);
    int top = top_of(w);
    double cx = (v->sx + w->W / 2.0) / SDL_max(1, v->content_w);
    v->fit_width = false;
    v->zoom = SDL_clamp(v->zoom * f, 0.05, 40.0);
    view_layout(v, w->W, w->H, top);
    v->sx = cx * v->content_w - w->W / 2.0;
    view_clamp(v, w->W, w->H, top);
    ui_message(w, "%.0f%%", v->zoom / w->density * 72.0 / 96.0 * 100);
    w->redraw = true;
}

static void fit_page(Window *w)
{
    View *v = cur_view(w);
    int top = top_of(w), p = view_top_page(v);
    float pw = v->doc->pw[p] + (v->dual && p + 1 < v->doc->npages ? v->doc->pw[p + 1] : 0);
    v->fit_width = false;
    v->zoom = SDL_min((w->W - 30.0) / pw, (w->H - top - 20.0) / v->doc->ph[p]);
    view_layout(v, w->W, w->H, top);
    view_goto_page(v, p);
    view_clamp(v, w->W, w->H, top);
    w->redraw = true;
}

static void goto_page(Window *w, int page)
{
    View *v = cur_view(w);
    view_goto_page(v, page);
    view_clamp(v, w->W, w->H, top_of(w));
    ui_message(w, "%d / %d", SDL_clamp(page, 0, v->doc->npages - 1) + 1, v->doc->npages);
    w->redraw = true;
}

/* ---------- forms ---------- */
static void form_step(Window *w, int dir)
{
    View *v = cur_view(w);
    Doc *d = v->doc;
    if (!doc_has_forms(d)) {
        ui_message(w, "este PDF não tem formulário");
        return;
    }
    int page, idx;
    if (d->focus_page >= 0) {
        page = d->focus_page;
        idx = d->focus_widget;
    } else {
        page = view_top_page(v);
        doc_page_widgets(d, page);
        idx = dir > 0 ? -1 : d->slots[page].nwidgets;
    }
    for (int visited = 0; visited <= d->npages;) {
        idx += dir;
        doc_page_widgets(d, page);
        if (idx < 0 || idx >= d->slots[page].nwidgets) {
            page = (page + dir + d->npages) % d->npages;
            doc_page_widgets(d, page);
            idx = dir > 0 ? -1 : d->slots[page].nwidgets;
            visited++;
            continue;
        }
        if (doc_focus_widget(d, page, idx)) {
            /* start at the end of existing text, ready to append or erase */
            FORM_OnKeyDown(d->form, doc_page(d, page), FWL_VKEY_End, 0);
            FORM_OnKeyUp(d->form, doc_page(d, page), FWL_VKEY_End, 0);
            w->mode = MODE_FORM;
            view_show_rect(v, page, d->slots[page].widgets[idx].r, w->W, w->H, top_of(w));
            w->redraw = true;
            return;
        }
    }
    ui_message(w, "nenhum campo preenchível");
}

static void form_leave(Window *w)
{
    doc_kill_focus(cur_view(w)->doc);
    w->mode = MODE_NORMAL;
    w->redraw = true;
}

static int vkey(SDL_Keycode k)
{
    switch (k) {
    case SDLK_BACKSPACE: return FWL_VKEY_Back;
    case SDLK_DELETE: return FWL_VKEY_Delete;
    case SDLK_LEFT: return FWL_VKEY_Left;
    case SDLK_RIGHT: return FWL_VKEY_Right;
    case SDLK_UP: return FWL_VKEY_Up;
    case SDLK_DOWN: return FWL_VKEY_Down;
    case SDLK_HOME: return FWL_VKEY_Home;
    case SDLK_END: return FWL_VKEY_End;
    case SDLK_RETURN: case SDLK_KP_ENTER: return FWL_VKEY_Return;
    default: return 0;
    }
}

static int fwl_mods(SDL_Keymod m)
{
    return ((m & SDL_KMOD_SHIFT) ? FWL_EVENTFLAG_ShiftKey : 0) |
           ((m & SDL_KMOD_CTRL) ? FWL_EVENTFLAG_ControlKey : 0) |
           ((m & SDL_KMOD_ALT) ? FWL_EVENTFLAG_AltKey : 0);
}

static bool form_key(Window *w, SDL_Keycode k, SDL_Keymod m)
{
    Doc *d = cur_view(w)->doc;
    FPDF_PAGE p = d->focus_page >= 0 ? doc_page(d, d->focus_page) : NULL;
    if (!p) {
        w->mode = MODE_NORMAL;
        return false;
    }
    bool ctrl = m & SDL_KMOD_CTRL;
    if (k == SDLK_ESCAPE) {
        form_leave(w);
        return true;
    }
    if (k == SDLK_TAB) {
        form_step(w, (m & SDL_KMOD_SHIFT) ? -1 : 1);
        return true;
    }
    if (ctrl && k == SDLK_A) {
        FORM_SelectAllText(d->form, p);
        return true;
    }
    if (ctrl && k == SDLK_Z) {
        FORM_Undo(d->form, p);
        return true;
    }
    if (ctrl && k == SDLK_V) {
        char *clip = SDL_GetClipboardText();
        if (clip) {
            static unsigned short u[8192];
            utf8_to_utf16(clip, u, 8192);
            FORM_ReplaceSelection(d->form, p, u);
            SDL_free(clip);
        }
        return true;
    }
    if (k == SDLK_BACKSPACE) {
        /* PDFium edits delete backwards on the Backspace character, not the key */
        FORM_OnChar(d->form, p, FWL_VKEY_Back, fwl_mods(m));
        return true;
    }
    int vk = vkey(k);
    if (!vk)
        return false;
    FORM_OnKeyDown(d->form, p, vk, fwl_mods(m));
    if (vk == FWL_VKEY_Return)
        FORM_OnChar(d->form, p, '\r', fwl_mods(m));
    FORM_OnKeyUp(d->form, p, vk, fwl_mods(m));
    return true;
}

static void form_text(Window *w, const char *text)
{
    Doc *d = cur_view(w)->doc;
    FPDF_PAGE p = d->focus_page >= 0 ? doc_page(d, d->focus_page) : NULL;
    if (!p)
        return;
    unsigned short u[64];
    int n = utf8_to_utf16(text, u, 64);
    for (int i = 0; i < n; i++)
        FORM_OnChar(d->form, p, u[i], 0);
}

static void click(Window *w, float x, float y, bool down)
{
    View *v = cur_view(w);
    Doc *d = v->doc;
    int page;
    double px, py;
    if (!d->form || !screen_to_page(v, x, y, top_of(w), &page, &px, &py))
        return;
    FPDF_PAGE p = doc_page(d, page);
    if (down) {
        FORM_OnLButtonDown(d->form, p, 0, px, py);
        return;
    }
    FORM_OnLButtonUp(d->form, p, 0, px, py);
    int fpage = -1;
    FPDF_ANNOTATION a = NULL;
    if (FORM_GetFocusedAnnot(d->form, &fpage, &a) && a) {
        FS_RECTF r;
        FPDFAnnot_GetRect(a, &r);
        FPDFPage_CloseAnnot(a);
        doc_page_widgets(d, fpage);
        PageSlot *s = &d->slots[fpage];
        for (int i = 0; i < s->nwidgets; i++) {
            if (SDL_fabsf(s->widgets[i].r.l - SDL_min(r.left, r.right)) < 0.5f &&
                SDL_fabsf(s->widgets[i].r.t - SDL_max(r.top, r.bottom)) < 0.5f) {
                d->focus_page = fpage;
                d->focus_widget = i;
                w->mode = MODE_FORM;
            }
        }
    } else if (w->mode == MODE_FORM) {
        d->focus_page = -1;
        w->mode = MODE_NORMAL;
    }
    w->redraw = true;
}

/* ---------- command line ---------- */
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

/* Returns false if the window was closed. */
static bool run_command(Window *w)
{
    View *v = cur_view(w);
    char *c = w->cmd;
    while (*c == ' ')
        c++;
    if (!strcmp(c, "w")) {
        save(w, v);
    } else if (!strcmp(c, "q") || !strcmp(c, "q!") || !strcmp(c, "wq") || !strcmp(c, "x")) {
        if (c[0] == 'w' || c[0] == 'x')
            save(w, v);
        if (c[1] == '!' || c[0] == 'w' || c[0] == 'x' || confirm_close(w, v)) {
            bool last = w->ntabs == 1;
            close_tab(w, w->cur);
            return !last;
        }
    } else if (!strncmp(c, "e ", 2)) {
        char path[CMD_MAX + 256];
        const char *arg = c + 2, *home = getenv("HOME");
        if (arg[0] == '~' && home)
            snprintf(path, sizeof path, "%s%s", home, arg + 1);
        else
            snprintf(path, sizeof path, "%s", arg);
        open_tab(w, path);
    } else if (!strcmp(c, "noh")) {
        v->match_page = -1;
    } else if (c[0] >= '0' && c[0] <= '9') {
        goto_page(w, atoi(c) - 1);
    } else if (*c) {
        ui_message(w, "comando desconhecido: %s", c);
    }
    return true;
}

static void cmdline_key(Window *w, SDL_Keycode k)
{
    if (k == SDLK_ESCAPE) {
        w->mode = MODE_NORMAL;
    } else if (k == SDLK_BACKSPACE) {
        if (w->cmdlen == 0) {
            w->mode = MODE_NORMAL;
        } else {
            do
                w->cmdlen--;
            while (w->cmdlen > 0 && ((unsigned char)w->cmd[w->cmdlen] & 0xC0) == 0x80);
            w->cmd[w->cmdlen] = 0;
        }
    } else if (k == SDLK_TAB && w->mode == MODE_COMMAND) {
        complete_path(w);
    } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        Mode m = w->mode;
        w->mode = MODE_NORMAL;
        if (m == MODE_SEARCH) {
            View *v = cur_view(w);
            snprintf(v->query, sizeof v->query, "%s", w->cmd);
            search_run(w, v, 1, true);
        } else {
            run_command(w);
            return;
        }
    }
    w->redraw = true;
}

static void cmdline_text(Window *w, const char *t)
{
    size_t n = strlen(t);
    if (w->cmdlen + n < CMD_MAX - 1) {
        memcpy(w->cmd + w->cmdlen, t, n + 1);
        w->cmdlen += (int)n;
    }
    w->redraw = true;
}

/* ---------- normal mode ---------- */
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
    w->redraw = true;
}

static void new_window(Window *w)
{
    View *v = view_clone(cur_view(w));
    Window *n = window_new(v);
    if (n)
        n->redraw = true;
}

static bool normal_key(Window *w, SDL_Keycode k, SDL_Keymod m)
{
    View *v = cur_view(w);
    double line = 60 * w->density, page = w->H - top_of(w) - 40 * w->density;
    bool ctrl = m & SDL_KMOD_CTRL;
    if (ctrl) {
        switch (k) {
        case SDLK_J: form_step(w, 1); return true;
        case SDLK_K: form_step(w, -1); return true;
        case SDLK_D: scroll(w, 0, page / 2); return true;
        case SDLK_U: scroll(w, 0, -page / 2); return true;
        case SDLK_F: scroll(w, 0, page); return true;
        case SDLK_B: scroll(w, 0, -page); return true;
        case SDLK_N: new_window(w); return true;
        case SDLK_S: save(w, v); return true;
        case SDLK_COMMA: switch_tab(w, -1); return true;
        case SDLK_PERIOD: switch_tab(w, 1); return true;
        case SDLK_EQUALS: case SDLK_PLUS: zoom_by(w, 1.1); return true;
        case SDLK_MINUS: zoom_by(w, 1 / 1.1); return true;
        default: return false;
        }
    }
    switch (k) {
    case SDLK_ESCAPE:
        w->count = 0;
        w->pending_g = false;
        v->match_page = -1;
        w->redraw = true;
        return true;
    case SDLK_DOWN: scroll(w, 0, line); return true;
    case SDLK_UP: scroll(w, 0, -line); return true;
    case SDLK_LEFT: scroll(w, -line, 0); return true;
    case SDLK_RIGHT: scroll(w, line, 0); return true;
    case SDLK_PAGEDOWN: scroll(w, 0, page); return true;
    case SDLK_PAGEUP: scroll(w, 0, -page); return true;
    case SDLK_HOME: goto_page(w, 0); return true;
    case SDLK_END: scroll(w, 0, 1e12); return true;
    default: return false;
    }
}

static void normal_text(Window *w, const char *t)
{
    View *v = cur_view(w);
    double line = 60 * w->density, page = w->H - top_of(w) - 40 * w->density;
    char c = t[1] ? 0 : t[0];
    int n = w->count ? w->count : 1;
    if ((c >= '1' && c <= '9') || (c == '0' && w->count)) {
        w->count = SDL_min(w->count * 10 + (c - '0'), 1000000);
        return;
    }
    if (w->pending_g) {
        w->pending_g = false;
        if (c == 'g')
            goto_page(w, w->count ? w->count - 1 : 0);
        w->count = 0;
        return;
    }
    switch (c) {
    case 'j': scroll(w, 0, line * n); break;
    case 'k': scroll(w, 0, -line * n); break;
    case 'h': scroll(w, -line * n, 0); break;
    case 'l': scroll(w, line * n, 0); break;
    case ' ': scroll(w, 0, page); break;
    case 'J': goto_page(w, view_top_page(v) + n * (v->dual ? 2 : 1)); break;
    case 'K': goto_page(w, view_top_page(v) - n * (v->dual ? 2 : 1)); break;
    case 'g': w->pending_g = true; return;
    case 'G':
        if (w->count)
            goto_page(w, w->count - 1);
        else
            scroll(w, 0, 1e12);
        break;
    case '+': zoom_by(w, SDL_pow(1.1, n)); break;
    case '-': zoom_by(w, SDL_pow(1 / 1.1, n)); break;
    case '=':
        v->fit_width = true;
        ui_message(w, "ajustar à largura");
        w->redraw = true;
        break;
    case 'z': fit_page(w); break;
    case 'd': {
        int p = view_top_page(v);
        v->dual = !v->dual;
        view_layout(v, w->W, w->H, top_of(w));
        view_goto_page(v, p);
        view_clamp(v, w->W, w->H, top_of(w));
        ui_message(w, v->dual ? "duas páginas" : "uma página");
        w->redraw = true;
        break;
    }
    case 'i':
        v->dark = !v->dark;
        w->redraw = true;
        break;
    case 't':
        w->show_tabs = !w->show_tabs;
        w->redraw = true;
        break;
    case 'v':
        caret_enter(w, v);
        break;
    case ':': case '/':
        w->mode = c == ':' ? MODE_COMMAND : MODE_SEARCH;
        w->cmd[0] = 0;
        w->cmdlen = 0;
        w->redraw = true;
        break;
    case 'n': case 'N':
        if (!v->query[0])
            ui_message(w, "nenhuma busca: use /");
        for (int i = 0; i < n && v->query[0]; i++)
            search_run(w, v, c == 'n' ? 1 : -1, false);
        break;
    case 'q':
        if (confirm_close(w, NULL))
            close_window(w);
        return;
    default:
        break;
    }
    w->count = 0;
}

/* ---------- drawing ---------- */
static void draw_focus(Window *w, View *v, int top)
{
    Doc *d = v->doc;
    if (d->focus_page < 0 || !d->slots[d->focus_page].widgets_ready)
        return;
    PRect r = d->slots[d->focus_page].widgets[d->focus_widget].r;
    float x0, y0, x1, y1, t = SDL_max(2.0f, w->density * 1.5f);
    page_to_screen(v, d->focus_page, r.l, r.t, &x0, &y0, top);
    page_to_screen(v, d->focus_page, r.r, r.b, &x1, &y1, top);
    float l = SDL_min(x0, x1) - t, u = SDL_min(y0, y1) - t, wd = SDL_fabsf(x1 - x0) + 2 * t,
          ht = SDL_fabsf(y1 - y0) + 2 * t;
    uint32_t c = 0x4DBBD5;
    ui_fill(w, l, u, wd, t, c, 255);
    ui_fill(w, l, u + ht - t, wd, t, c, 255);
    ui_fill(w, l, u, t, ht, c, 255);
    ui_fill(w, l + wd - t, u, t, ht, c, 255);
}

static void draw(Window *w, uint64_t now)
{
    View *v = cur_view(w);
    update_size(w);
    int top = top_of(w);
    view_layout(v, w->W, w->H, top);
    view_clamp(v, w->W, w->H, top);
    uint32_t bg = v->dark ? cfg.bg_dark : cfg.bg_light;
    SDL_SetRenderDrawColor(w->ren, bg >> 16, (bg >> 8) & 0xFF, bg & 0xFF, 255);
    SDL_RenderClear(w->ren);
    view_draw(w, v, top);
    search_draw(w, v, top);
    if (w->mode == MODE_CARET)
        caret_draw(w, v, top, blink_on);
    if (w->mode == MODE_FORM)
        draw_focus(w, v, top);
    ui_draw_overlays(w, now);
    if (capture_path) {
        SDL_Surface *shot = SDL_RenderReadPixels(w->ren, NULL);
        if (shot) {
            SDL_SaveBMP(shot, capture_path);
            SDL_DestroySurface(shot);
        }
        capture_path = NULL;
    }
    SDL_RenderPresent(w->ren);
    w->redraw = false;
}

/* ---------- events ---------- */
static void handle_key(Window *w, SDL_KeyboardEvent *e)
{
    SDL_Keycode k = e->key;
    SDL_Keymod m = e->mod;
    switch (w->mode) {
    case MODE_COMMAND:
    case MODE_SEARCH:
        cmdline_key(w, k);
        return;
    case MODE_FORM:
        if ((m & SDL_KMOD_CTRL) && (k == SDLK_J || k == SDLK_K)) {
            form_step(w, k == SDLK_J ? 1 : -1);
            return;
        }
        if ((m & SDL_KMOD_CTRL) && k == SDLK_S) {
            save(w, cur_view(w));
            return;
        }
        form_key(w, k, m);
        return;
    case MODE_CARET:
        if (k == SDLK_ESCAPE) {
            caret_key(w, cur_view(w), k, m, NULL);
            return;
        }
        normal_key(w, k, m);
        return;
    case MODE_NORMAL:
        normal_key(w, k, m);
        return;
    }
}

static void handle_text(Window *w, const char *t)
{
    switch (w->mode) {
    case MODE_COMMAND:
    case MODE_SEARCH:
        cmdline_text(w, t);
        break;
    case MODE_FORM:
        form_text(w, t);
        break;
    case MODE_CARET:
        if (t[0] >= '1' && t[0] <= '9' && !t[1]) {
            w->count = w->count * 10 + (t[0] - '0');
            break;
        }
        if (t[0] == '0' && !t[1] && w->count) {
            w->count *= 10;
            break;
        }
        caret_key(w, cur_view(w), 0, 0, t);
        break;
    case MODE_NORMAL:
        normal_text(w, t);
        break;
    }
}

static bool window_alive(Window *w)
{
    for (int i = 0; i < nwindows; i++)
        if (windows[i] == w)
            return true;
    return false;
}

static void handle_event(SDL_Event *e)
{
    Window *w = NULL;
    switch (e->type) {
    case SDL_EVENT_QUIT:
        while (nwindows)
            close_window(windows[0]);
        return;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if ((w = window_by_id(e->window.windowID)) && confirm_close(w, NULL))
            close_window(w);
        return;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        if ((w = window_by_id(e->window.windowID)))
            w->redraw = true;
        return;
    case SDL_EVENT_KEY_DOWN:
        if ((w = window_by_id(e->key.windowID)))
            handle_key(w, &e->key);
        break;
    case SDL_EVENT_TEXT_INPUT:
        if ((w = window_by_id(e->text.windowID)))
            handle_text(w, e->text.text);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if ((w = window_by_id(e->wheel.windowID))) {
            if (SDL_GetModState() & SDL_KMOD_CTRL)
                zoom_by(w, e->wheel.y > 0 ? 1.1 : 1 / 1.1);
            else
                scroll(w, -e->wheel.x * 60 * w->density, -e->wheel.y * 60 * w->density);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if ((w = window_by_id(e->button.windowID)) && e->button.button == SDL_BUTTON_LEFT &&
            w->mode != MODE_COMMAND && w->mode != MODE_SEARCH)
            click(w, e->button.x * w->density, e->button.y * w->density, e->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        break;
    default:
        return;
    }
    if (w && window_alive(w))
        w->redraw = true;
}

static void autosave(uint64_t now, int *wait)
{
    if (!cfg.autosave)
        return;
    for (int i = 0; i < ndocs; i++) {
        Doc *d = docs[i];
        if (!d->modified || d->focus_page >= 0)
            continue;
        uint64_t due = d->changed_at + (uint64_t)cfg.autosave_delay_ms;
        if (now < due) {
            int left = (int)(due - now);
            *wait = *wait < 0 ? left : SDL_min(*wait, left);
            continue;
        }
        char err[512];
        if (!doc_save(d, err, sizeof err)) {
            d->changed_at = now + 10000; /* retry later */
            for (int k = 0; k < nwindows; k++)
                ui_message(windows[k], "%s", err);
        }
    }
}

/* ---------- test mode: scripted input and screenshots ---------- */
static const char *script, *script_dir;

static SDL_Keycode named_key(const char *n, size_t len)
{
    static const struct { const char *name; SDL_Keycode key; } keys[] = {
        {"Esc", SDLK_ESCAPE}, {"Enter", SDLK_RETURN}, {"Tab", SDLK_TAB}, {"BS", SDLK_BACKSPACE},
        {"Del", SDLK_DELETE}, {"Home", SDLK_HOME}, {"End", SDLK_END}, {"Down", SDLK_DOWN}, {"Up", SDLK_UP}, {"Left", SDLK_LEFT}, {"Right", SDLK_RIGHT}};
    for (size_t i = 0; i < SDL_arraysize(keys); i++)
        if (strlen(keys[i].name) == len && !strncmp(n, keys[i].name, len))
            return keys[i].key;
    return len == 1 ? (SDL_Keycode)(unsigned char)n[0] : 0;
}

/* Runs one script token; returns false when the script is finished. */
static bool script_step(void)
{
    if (!script || !*script || !nwindows)
        return false;
    Window *w = windows[nwindows - 1];
    if (*script != '<') {
        char t[5] = {0};
        int n = 1;
        while (n < 4 && ((unsigned char)script[n] & 0xC0) == 0x80)
            n++;
        memcpy(t, script, n);
        script += n;
        handle_text(w, t);
        return true;
    }
    const char *end = strchr(script, '>');
    if (!end)
        return false;
    const char *tok = script + 1;
    size_t len = (size_t)(end - tok);
    script = end + 1;
    static char path[1024];
    if (!strncmp(tok, "shot:", 5)) {
        snprintf(path, sizeof path, "%s/%.*s.bmp", script_dir, (int)(len - 5), tok + 5);
        capture_path = path;
        draw(w, SDL_GetTicks());
    } else if (!strncmp(tok, "wait:", 5)) {
        SDL_Delay((Uint32)atoi(tok + 5));
    } else {
        SDL_KeyboardEvent e = {0};
        if (len > 2 && tok[1] == '-') {
            e.mod = tok[0] == 'C' ? SDL_KMOD_LCTRL : tok[0] == 'S' ? SDL_KMOD_LSHIFT : 0;
            tok += 2;
            len -= 2;
        }
        e.key = named_key(tok, len);
        e.windowID = SDL_GetWindowID(w->win);
        handle_key(w, &e);
    }
    return true;
}

static void usage(void)
{
    puts("uso: nomadys-pdf arquivo.pdf [arquivo2.pdf ...]\n"
         "\n"
         "  j/k h/l      rolar           gg / G / 42G   início / fim / página 42\n"
         "  Ctrl-d/u     meia tela       Ctrl-f/b       tela inteira\n"
         "  J/K          página seguinte/anterior\n"
         "  + / - / =    zoom / ajustar à largura       z   página inteira\n"
         "  d            duas páginas    i              modo escuro\n"
         "  Ctrl-j/k     próximo/anterior campo de formulário (Esc sai)\n"
         "  v            modo cursor: hjkl w b 0 $, v seleciona, H destaca, y copia\n"
         "  / n N        buscar          :e arquivo     abrir em nova aba\n"
         "  t            mostrar abas    Ctrl-, Ctrl-.  aba anterior/seguinte\n"
         "  Ctrl-n       nova janela     Ctrl-s / :w    salvar\n"
         "  :q           fechar aba      q              fechar janela\n"
         "\n"
         "config: ~/.config/nomadys-pdf/config (theme, autosave, highlight_color, zoom)");
}

int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        usage();
        return argc < 2;
    }
    if (!strcmp(argv[1], "--version")) {
        puts("nomadys-pdf " VERSION);
        return 0;
    }
    while (argc > 2 && (!strcmp(argv[1], "--test-script") || !strcmp(argv[1], "--test-out"))) {
        if (!strcmp(argv[1], "--test-script"))
            script = argv[2];
        else
            script_dir = argv[2];
        argv += 2;
        argc -= 2;
    }
    if (script && !script_dir)
        script_dir = ".";
    config_load();
    FPDF_LIBRARY_CONFIG lib = {.version = 2};
    FPDF_InitLibraryWithConfig(&lib);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "nomadys-pdf: %s\n", SDL_GetError());
        return 1;
    }
    Window *w = NULL;
    for (int i = 1; i < argc; i++) {
        char err[512];
        Doc *d = doc_open(argv[i], err, sizeof err);
        if (!d) {
            fprintf(stderr, "nomadys-pdf: %s\n", err);
            continue;
        }
        View *v = view_new(d);
        if (!w) {
            w = window_new(v);
        } else if (w->ntabs < MAX_TABS) {
            init_zoom(w, v);
            w->tabs[w->ntabs++] = v;
        } else {
            view_free(v);
        }
    }
    if (!w) {
        SDL_Quit();
        FPDF_DestroyLibrary();
        return 1;
    }
    blink_at = SDL_GetTicks() + BLINK_MS;
    bool prefetching = false;
    while (nwindows) {
        uint64_t now = SDL_GetTicks();
        int wait = -1;
        for (int i = 0; i < ndocs; i++) {
            int t = doc_run_timers(docs[i], now);
            if (t >= 0)
                wait = wait < 0 ? t : SDL_min(wait, t);
        }
        bool caret = false;
        for (int i = 0; i < nwindows; i++) {
            Window *x = windows[i];
            caret |= x->mode == MODE_CARET;
            uint64_t until[2] = {x->msg_until, x->tabs_flash_until};
            for (int k = 0; k < 2; k++) {
                if (until[k] > now) {
                    int left = (int)(until[k] - now) + 1;
                    wait = wait < 0 ? left : SDL_min(wait, left);
                } else if (until[k] && now - until[k] < 100) {
                    x->redraw = true; /* overlay just expired */
                }
            }
        }
        if (caret) {
            if (now >= blink_at) {
                blink_on = !blink_on;
                blink_at = now + BLINK_MS;
                for (int i = 0; i < nwindows; i++)
                    windows[i]->redraw |= windows[i]->mode == MODE_CARET;
            }
            int left = (int)(blink_at - now);
            wait = wait < 0 ? left : SDL_min(wait, left);
        } else {
            blink_on = true;
        }
        autosave(now, &wait);
        for (int i = 0; i < nwindows; i++) {
            if (windows[i]->redraw || need_redraw_all)
                draw(windows[i], now);
        }
        need_redraw_all = false;
        if (prefetching || script)
            wait = 0;
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, wait < 0 ? 1000 : wait)) {
            handle_event(&e);
            while (nwindows && SDL_PollEvent(&e))
                handle_event(&e);
            prefetching = true;
            continue;
        }
        prefetching = false;
        if (script) {
            if (!script_step()) {
                script = NULL;
                while (nwindows)
                    close_window(windows[0]);
            }
            continue;
        }
        for (int i = 0; i < nwindows && !prefetching; i++)
            prefetching = view_prefetch(windows[i], cur_view(windows[i]), top_of(windows[i]));
    }
    while (ndocs)
        doc_unref(docs[0]);
    SDL_Quit();
    FPDF_DestroyLibrary();
    return 0;
}
