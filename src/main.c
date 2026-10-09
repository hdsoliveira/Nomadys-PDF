/* Nomadys-PDF: windows, tabs, drawing and the event loop. */
#include <malloc.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

#define VERSION NOMADYS_VERSION

Window *windows[MAX_WINDOWS];
int nwindows;
static uint64_t last_input; /* any event: the reader is active */
static const char *capture_path; /* test mode: save the next frame here */

View *cur_view(Window *w)
{
    if (!w->ntabs)
        return NULL;
    return w->split && w->split_focus ? w->split : w->tabs[w->cur];
}

/* In a pane (split.c) the pages start at its top; the tab bar is above it. */
int top_of(Window *w) { return w->in_pane ? 0 : ui_tabbar_height(w); }

/* The colours of the menus and bars: always the dark mode of the theme, whatever
 * the pages are showing. */
const Theme *uitheme(Window *w)
{
    (void)w;
    return theme_of(true);
}

/* The colours: of the pane being drawn, or of the focused one. */
const Theme *wtheme(Window *w)
{
    if (!w->ntabs)
        return theme_of(false);
    return theme_of((w->in_pane ? pane_view(w, w->pane_second) : cur_view(w))->dark);
}

void update_title(Window *w)
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
    float density = ww > 0 ? (float)pw / ww : 1;
    w->W = pw;
    w->H = ph;
    if (density != w->density || !w->font) {
        w->density = density;
        font_close(w->font);
        w->font = font_open(w->ren, cfg.font, cfg.font_size, density);
        static bool warned;
        if (font_fallback[0] && !warned) { /* a typo in the config's font: say so once */
            warned = true;
            fprintf(stderr, "npdf: %s (fc-list : family lists the fonts)\n", font_fallback);
            ui_message_long(w, 8000, "%s (fc-list : family lists the fonts)", font_fallback);
        }
    }
}

static void init_zoom(Window *w, View *v)
{
    if (!v->fit_width && v->zoom <= 0) {
        double z = cfg.remember_view && v->doc->prefs.set ? v->doc->prefs.zoom : cfg.zoom;
        v->zoom = z * 96.0 / 72.0 * w->density;
    }
    v->density = w->density;
}

static int test_w, test_h; /* --test-size: the window's size in test mode */

static Window *window_new(View *v)
{
    if (nwindows == MAX_WINDOWS) {
        view_free(v);
        return NULL;
    }
    Window *w = xcalloc(1, sizeof *w);
    w->win = SDL_CreateWindow("Nomadys-PDF", test_w > 0 ? test_w : 900, test_h > 0 ? test_h : 1100, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Surface *icon = w->win ? about_icon() : NULL; /* X11 shows it; Wayland uses the app id (npdf.desktop) */
    if (icon) {
        SDL_SetWindowIcon(w->win, icon);
        SDL_DestroySurface(icon);
    }
    if (!w->win) {
        fprintf(stderr, "npdf: %s\n", SDL_GetError());
        free(w);
        view_free(v);
        return NULL;
    }
    w->ren = SDL_CreateRenderer(w->win, NULL);
    SDL_SetRenderVSync(w->ren, 1);
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

void window_new_from(Window *w)
{
    window_new(view_clone(cur_view(w)));
}

static void window_destroy(Window *w)
{
    for (int i = 0; i < w->ntabs; i++)
        view_free(w->tabs[i]); /* textures first: they belong to the renderer */
    if (w->split)
        view_free(w->split);
    font_close(w->font);
    if (w->shadow)
        SDL_DestroyTexture(w->shadow);
    free(w->items);
    free(w->links);
    grid_free(w);
    if (w->about_tex)
        SDL_DestroyTexture(w->about_tex);
    present_free(w);
    if (w->preview.tex)
        SDL_DestroyTexture(w->preview.tex);
    math_forget_renderer(w->ren);
    textbox_forget_renderer(w->ren);
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

/* ---------- closing with unsaved changes (autosave = false) ---------- */
static int views_of(Doc *d)
{
    int n = 0;
    for (int i = 0; i < nwindows; i++)
        for (int k = 0; k < windows[i]->ntabs; k++)
            n += windows[i]->tabs[k]->doc == d;
    for (int i = 0; i < nwindows; i++)
        n += windows[i]->split && windows[i]->split->doc == d;
    return n;
}

static bool unsaved_would_be_lost(Window *w, View *only)
{
    if (cfg.autosave)
        return false;
    for (int k = 0; k < w->ntabs; k++) {
        View *v = w->tabs[k];
        if ((!only || v == only) && v->doc->modified && views_of(v->doc) == 1)
            return true;
    }
    return false;
}

bool confirm_close(Window *w, View *only)
{
    uint64_t now = SDL_GetTicks();
    if (!unsaved_would_be_lost(w, only) || now < w->quit_warned)
        return true;
    w->quit_warned = now + 4000;
    ui_message(w, "unsaved changes: Ctrl-s saves, repeat to discard");
    return false;
}

static void discard_if_manual(View *v)
{
    if (!cfg.autosave && views_of(v->doc) == 1)
        v->doc->modified = false;
}

void close_tab(Window *w, int i)
{
    View *v = w->tabs[i];
    discard_if_manual(v);
    if (w->ntabs == 1) {
        /* the last tab of the last window: the session keeps it for next time */
        bool last = nwindows == 1;
        if (last)
            session_save();
        window_destroy(w);
        if (!last)
            session_save();
        return;
    }
    view_free(v);
    memmove(&w->tabs[i], &w->tabs[i + 1], (w->ntabs - i - 1) * sizeof *w->tabs);
    w->ntabs--;
    if (w->cur >= w->ntabs)
        w->cur = w->ntabs - 1;
    w->redraw = true;
    update_title(w);
    session_save();
}

void close_window(Window *w)
{
    textbox_abort(w); /* a text box hidden while it is written must show again */
    for (int i = 0; i < w->ntabs; i++)
        discard_if_manual(w->tabs[i]);
    bool last = nwindows == 1;
    if (last) /* quitting: the session keeps these tabs */
        session_save();
    window_destroy(w);
    if (!last)
        session_save();
}

static bool add_tab(Window *w, Doc *d, const char *path);

/* The password typed for w->pending_path: open it, or ask again. */
static void password_done(Window *w, const char *text)
{
    char err[512], path[sizeof w->pending_path];
    bool locked;
    snprintf(path, sizeof path, "%s", w->pending_path);
    Doc *d = doc_open_locked(path, text, &locked, err, sizeof err);
    explicit_bzero(w->cmd, sizeof w->cmd); /* the typed password */
    if (!d && locked) {
        ask_password(w, path, true);
        return;
    }
    if (!d) {
        ui_message(w, "%s", err);
        return;
    }
    w->pending_path[0] = 0;
    add_tab(w, d, path);
}

/* Asks for the password of a protected PDF in the prompt, typed hidden. */
void ask_password(Window *w, const char *path, bool again)
{
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    char label[64];
    snprintf(label, sizeof label, "%s%.36s password: ", again ? "wrong, again: " : "", name);
    snprintf(w->pending_path, sizeof w->pending_path, "%s", path);
    prompt_open(w, label, "", password_done, 0);
    w->prompt_secret = true;
}

bool open_tab(Window *w, const char *path)
{
    char err[512];
    bool locked;
    Doc *d = doc_open_locked(path, NULL, &locked, err, sizeof err);
    if (!d && locked) {
        ask_password(w, path, false);
        return false;
    }
    if (!d) {
        ui_message(w, "%s", err);
        return false;
    }
    return add_tab(w, d, path);
}

static bool add_tab(Window *w, Doc *d, const char *path)
{
    if (w->ntabs == MAX_TABS) {
        doc_unref(d);
        ui_message(w, "too many tabs");
        return false;
    }
    View *v = view_new(d);
    init_zoom(w, v);
    w->tabs[w->ntabs++] = v;
    w->cur = w->ntabs - 1;
    w->tabs_flash_until = SDL_GetTicks() + 1000;
    update_title(w);
    w->redraw = true;
    recent_add(path);
    session_save();
    return true;
}

void save(Window *w, View *v)
{
    char err[512];
    doc_kill_focus(v->doc);
    if (w->mode == MODE_FORM)
        w->mode = MODE_NORMAL;
    if (doc_save(v->doc, err, sizeof err))
        ui_message(w, "saved");
    else
        ui_message(w, "%s", err);
    w->quit_warned = 0;
}

/* ---------- drawing ---------- */
/* The pages of one view and what is drawn on them, in the window or in a pane. */
static void draw_pages(Window *w, View *v, uint64_t now)
{
    int top = top_of(w);
    v->density = w->density;
    view_layout(v, w->W, w->H, top);
    view_clamp(v, w->W, w->H, top);
    if (w->in_pane) {
        uint32_t bg = wtheme(w)->background;
        ui_fill(w, 0, 0, (float)w->W, (float)w->H, bg, 255);
    }
    view_draw(w, v, top);
    ui_draw_notes(w, v, top);
    search_draw(w, v, top);
    diff_draw(w, v, top);
    if (now < v->flash_until) {
        float x0, y0, x1, y1;
        page_to_screen(v, v->flash_page, v->flash_rect.l, v->flash_rect.t, &x0, &y0, top);
        page_to_screen(v, v->flash_page, v->flash_rect.r, v->flash_rect.b, &x1, &y1, top);
        ui_fill(w, SDL_min(x0, x1), SDL_min(y0, y1), SDL_fabsf(x1 - x0), SDL_fabsf(y1 - y0), wtheme(w)->caret, 70);
    }
    if (v != cur_view(w)) /* the cursors and edits belong to the focused pane */
        return;
    cursor_draw(w, v, top);
    object_draw(w, v, top);
    textbox_draw(w, v, top);
    if (w->mode == MODE_FORM)
        form_draw_focus(w, v, top);
}

static void draw(Window *w, uint64_t now)
{
    View *v = cur_view(w);
    update_size(w);
    uint32_t bg = (w->mode == MODE_GRID ? uitheme(w) : wtheme(w))->background; /* the page grid is a menu */
    SDL_SetRenderDrawColor(w->ren, bg >> 16, (bg >> 8) & 0xFF, bg & 0xFF, 255);
    SDL_RenderClear(w->ren);
    if (w->mode == MODE_PRESENT) { /* a slide on black, nothing else */
        view_layout(v, w->W, w->H, top_of(w));
        present_draw(w);
        goto shot;
    }
    if (w->mode == MODE_GRID) { /* the grid replaces the pages */
        view_layout(v, w->W, w->H, top_of(w));
        grid_draw(w);
        goto overlays;
    }
    if (!w->split) {
        draw_pages(w, v, now);
    } else {
        for (int p = 0; p < 2; p++)
            if (pane_enter(w, p)) {
                draw_pages(w, pane_view(w, p), now);
                pane_leave(w);
            }
        split_draw_frame(w);
    }
overlays:
    ui_draw_overlays(w, now);
shot:
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
static bool alive(Window *w)
{
    for (int i = 0; i < nwindows; i++)
        if (windows[i] == w)
            return true;
    return false;
}

bool window_alive(Window *w) { return alive(w); }

/* ---------- LaTeX: requests from VimTeX and reloads ---------- */

/* "forward\t<pdf>\t<line>\t<col>\t<tex>": show that source line in the PDF. */
static void handle_request(char *msg)
{
    char *f[5];
    int n = 0;
    for (char *p = strtok(msg, "\t\n"); p && n < 5; p = strtok(NULL, "\t\n"))
        f[n++] = p;
    if (n < 5 || strcmp(f[0], "forward") || !nwindows)
        return;
    char *real = realpath(f[1], NULL);
    Window *w = NULL;
    int tab = -1;
    for (int i = 0; i < nwindows && tab < 0; i++) {
        Window *x = windows[i];
        if (x->ntabs && real && !strcmp(cur_view(x)->doc->path, real)) {
            w = x, tab = x->cur;
            break;
        }
        for (int k = 0; k < x->ntabs; k++)
            if (real && !strcmp(x->tabs[k]->doc->path, real)) {
                w = x, tab = k;
                break;
            }
    }
    if (tab < 0) {
        w = windows[0];
        if (!open_tab(w, f[1])) {
            free(real);
            return;
        }
        tab = w->cur;
    }
    free(real);
    w->cur = tab;
    w->mode = MODE_NORMAL;
    w->split_focus = false; /* the tab's pane */
    update_title(w);
    View *v = cur_view(w);
    update_size(w);
    bool pane = pane_enter(w, false);
    view_layout(v, w->W, w->H, top_of(w));
    view_clamp(v, w->W, w->H, top_of(w));
    synctex_forward(w, v, f[4], atoi(f[2]), atoi(f[3]));
    if (pane)
        pane_leave(w);
    /* the window may be on another workspace, or hidden: ask for it. How
     * far the window manager goes is its call (sway: focus_on_window_activation) */
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
    SDL_RestoreWindow(w->win);
    SDL_RaiseWindow(w->win);
}

static void file_changed(int wd, const char *name)
{
    if (theme_file_changed(wd, name)) /* the theme being used was saved */
        return;
    for (int i = 0; i < ndocs; i++) {
        Doc *d = docs[i];
        const char *base = strrchr(d->path, '/');
        if (d->watch_wd == wd && base && !strcmp(base + 1, name)) {
            d->reload_at = SDL_GetTicks() + 250; /* let the writer finish */
            d->reload_tries = 0;
        }
    }
}

static void reload_due(uint64_t now, int *wait)
{
    for (int i = 0; i < ndocs; i++) {
        Doc *d = docs[i];
        if (!d->reload_at)
            continue;
        if (now < d->reload_at) {
            int left = (int)(d->reload_at - now);
            *wait = *wait < 0 ? left : SDL_min(*wait, left);
            continue;
        }
        long long mtime = file_mtime_ns(d->path);
        if (mtime == d->saved_mtime_ns || mtime < 0) {
            d->reload_at = 0; /* our own save, or the file is gone for now */
            continue;
        }
        bool dropped = d->modified; /* edits not yet saved are lost with the old version */
        if (!doc_reload(d)) {
            d->reload_at = ++d->reload_tries < 40 ? now + 250 : 0;
            continue;
        }
        d->reload_at = 0;
        for (int k = 0; k < nwindows; k++) {
            Window *w = windows[k];
            if (w->split && w->split->doc == d) {
                view_doc_reloaded(w->split);
                w->redraw = true;
            }
            for (int t = 0; t < w->ntabs; t++) {
                if (w->tabs[t]->doc != d)
                    continue;
                view_doc_reloaded(w->tabs[t]);
                if (t != w->cur)
                    continue;
                if (prompt_uses_document(w)) {
                    command_complete_reset(w);
                    ui_message(w, "PDF reloaded: edit cancelled");
                    w->mode = MODE_NORMAL;
                } else if (w->mode == MODE_PREVIEW) {
                    preview_close(w); /* the target may have moved */
                } else if (w->mode != MODE_HELP && w->mode != MODE_PROMPT) {
                    w->mode = MODE_NORMAL;
                } else if (w->mode == MODE_PROMPT) {
                    w->prompt_return = MODE_NORMAL; /* e.g. ':' typed from the panel */
                }
                if (dropped)
                    ui_message_long(w, 6000, "PDF changed on disk: your unsaved edits were dropped");
            }
            w->redraw = true;
        }
    }
}

static void handle_event(SDL_Event *e)
{
    Window *w = NULL;
    last_input = SDL_GetTicks();
    if (e->type == ipc_event) {
        if (e->user.code == IPC_REQUEST)
            handle_request(e->user.data1);
        else if (e->user.code == IPC_FINDER)
            picker_refresh();
        else if (e->user.code == IPC_INDEX || e->user.code == IPC_LIBRARY)
            library_event(e->user.code);
        else if (e->user.code == IPC_BIBTEX)
            cite_result(window_by_id((SDL_WindowID)(uintptr_t)e->user.data2), e->user.data1);
        else if (e->user.code == IPC_FILE)
            file_changed((int)(intptr_t)e->user.data2, e->user.data1);
        free(e->user.data1);
        return;
    }
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
            input_key_event(w, &e->key);
        return;
    case SDL_EVENT_TEXT_INPUT:
        if ((w = window_by_id(e->text.windowID)))
            input_text_event(w, e->text.text);
        return;
    case SDL_EVENT_MOUSE_WHEEL:
        if ((w = window_by_id(e->wheel.windowID))) {
            float mx = e->wheel.mouse_x * w->density, my = e->wheel.mouse_y * w->density;
            bool pane = pane_enter_at(w, &mx, &my); /* the pane under the mouse */
            if (pane)
                pane_leave(w); /* input_token enters it again */
            if (SDL_GetModState() & SDL_KMOD_CTRL) {
                input_token(w, e->wheel.y > 0 ? "<C-=>" : "<C-->");
            } else {
                pane = pane_enter_focused(w);
                scroll(w, -e->wheel.x * 60 * w->density, -e->wheel.y * 60 * w->density);
                if (pane)
                    pane_leave(w);
            }
        }
        return;
    case SDL_EVENT_MOUSE_MOTION:
        if ((w = window_by_id(e->motion.windowID)) && w->ntabs &&
            (w->mode == MODE_OBJECT || w->mode == MODE_ROTATE || object_mouse_active(w))) {
            float x = e->motion.x * w->density, y = e->motion.y * w->density;
            bool pane = pane_enter_focused(w); /* the pane where the picture is */
            if (pane)
                x -= w->pane_x, y -= w->pane_y;
            object_mouse_motion(w, x, y);
            if (pane && alive(w))
                pane_leave(w);
            return;
        }
        if ((e->motion.state & SDL_BUTTON_LMASK) && (w = window_by_id(e->motion.windowID)) && w->ntabs &&
            (w->mode == MODE_NORMAL || w->mode == MODE_CARET)) {
            float x = e->motion.x * w->density, y = e->motion.y * w->density;
            bool pane = pane_enter_focused(w); /* the pane where the drag began */
            if (pane)
                x -= w->pane_x, y -= w->pane_y;
            caret_mouse_drag(w, cur_view(w), x, y);
            if (pane && alive(w))
                pane_leave(w);
        }
        return;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        /* presenting: left click forward, right click back */
        if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && (w = window_by_id(e->button.windowID)) && w->mode == MODE_PRESENT) {
            present_step(w, e->button.button == SDL_BUTTON_RIGHT ? -1 : 1);
            return;
        }
        /* pictures and areas: a press selects, dragging moves, resizes or turns */
        if ((w = window_by_id(e->button.windowID)) && w->ntabs && e->button.button == SDL_BUTTON_LEFT &&
            (w->mode == MODE_NORMAL || w->mode == MODE_AREA || w->mode == MODE_OBJECT || w->mode == MODE_ROTATE)) {
            float x = e->button.x * w->density, y = e->button.y * w->density;
            bool down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN, pane, used;
            if (down) {
                pane = pane_enter_at(w, &x, &y);
                used = object_mouse_down(w, x, y, e->button.clicks);
            } else {
                pane = object_mouse_active(w) && pane_enter_focused(w); /* where the drag began */
                if (pane)
                    x -= w->pane_x, y -= w->pane_y;
                used = object_mouse_up(w, x, y);
            }
            if (pane && alive(w))
                pane_leave(w);
            if (used) {
                if (alive(w))
                    w->redraw = true;
                return;
            }
        }
        /* double click or Ctrl+click: go to the LaTeX source (SyncTeX) */
        if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && (w = window_by_id(e->button.windowID)) &&
            e->button.button == SDL_BUTTON_LEFT &&
            (e->button.clicks >= 2 || (SDL_GetModState() & SDL_KMOD_CTRL))) {
            int page;
            double px, py;
            float x = e->button.x * w->density, y = e->button.y * w->density;
            bool pane = pane_enter_at(w, &x, &y);
            if (screen_to_page(cur_view(w), x, y, top_of(w), &page, &px, &py))
                synctex_inverse(w, cur_view(w), page, px, py);
            if (pane && alive(w))
                pane_leave(w);
            if (alive(w))
                w->redraw = true;
            return;
        }
        if ((w = window_by_id(e->button.windowID)) && e->button.button == SDL_BUTTON_LEFT &&
            (w->mode == MODE_NORMAL || w->mode == MODE_FORM || w->mode == MODE_CARET)) {
            static float down_x, down_y;
            float x = e->button.x * w->density, y = e->button.y * w->density;
            bool down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            bool pane = pane_enter_at(w, &x, &y); /* a click focuses the pane */
            if (w->mode != MODE_CARET)
                form_click(w, x, y, down);
            if (down) {
                down_x = x, down_y = y;
                caret_mouse_down(w, cur_view(w), x, y); /* dragging over text selects it */
            } else if (w->mode == MODE_CARET) {
                caret_mouse_up(w, cur_view(w));
            } else if (w->mode == MODE_NORMAL && SDL_fabsf(x - down_x) + SDL_fabsf(y - down_y) < 6 * w->density) {
                caret_mouse_up(w, cur_view(w));
                links_click(w, x, y); /* a plain click on a link follows it */
            }
            if (pane && alive(w))
                pane_leave(w);
        }
        if (w && alive(w))
            w->redraw = true;
        return;
    default:
        return;
    }
}

static void mouse_flush(Window *w)
{
    if (!w->obj_drag.pending)
        return;
    bool pane = pane_enter_focused(w);
    object_mouse_flush(w);
    if (pane && alive(w))
        pane_leave(w);
}

static void autosave(uint64_t now, int *wait)
{
    if (!cfg.autosave)
        return;
    for (int i = 0; i < ndocs; i++) {
        Doc *d = docs[i];
        if (!d->modified || d->save_failed || d->focus_page >= 0)
            continue;
        bool dragging = false; /* a picture hidden for the drag must not be saved so */
        for (int k = 0; k < nwindows; k++)
            dragging |= (windows[k]->obj_drag.live || windows[k]->text_hidden) && windows[k]->ntabs &&
                        cur_view(windows[k])->doc == d;
        if (dragging)
            continue;
        /* a big file waits a little longer, so saving never gets in the way */
        uint64_t due = d->changed_at + (uint64_t)SDL_max(cfg.autosave_delay_ms, 4 * d->save_ms);
        if (now < due) {
            int left = (int)(due - now);
            *wait = *wait < 0 ? left : SDL_min(*wait, left);
            continue;
        }
        char err[512];
        if (!doc_save(d, err, sizeof err)) {
            d->save_failed = true; /* say it once; try again after the next change */
            for (int k = 0; k < nwindows; k++)
                ui_message_long(windows[k], 5000, "%s", err);
        }
    }
}

/* Background work while nothing happens; returns true if there is more. */
bool memory_freed;

/* Reopens documents to free what PDFium kept of pages seen long ago: those
 * out of sight after a few seconds, the one being read after a pause. */
static bool compact_docs(uint64_t now)
{
    for (int i = 0; i < ndocs; i++) {
        Doc *d = docs[i];
        bool shown = false;
        for (int k = 0; k < nwindows; k++)
            shown |= windows[k]->ntabs && (windows[k]->tabs[windows[k]->cur]->doc == d ||
                                           (windows[k]->split && windows[k]->split->doc == d));
        if (shown)
            d->hidden_since = 0;
        else if (!d->hidden_since)
            d->hidden_since = now ? now : 1;
        bool due = shown ? now - last_input > 15000 && d->pages_opened >= 2 * LOADED_PAGE_CAP
                         : now - d->hidden_since > 3000 && d->pages_opened >= 4;
        if (due && doc_compact(d))
            return true; /* one at a time */
    }
    return false;
}

static bool maintenance_pending(void)
{
    if (memory_freed)
        return true;
    for (int i = 0; i < ndocs; i++) { /* the same rules as compact_docs */
        Doc *d = docs[i];
        if (!d->modified && (d->hidden_since ? d->pages_opened >= 4 : d->pages_opened >= 2 * LOADED_PAGE_CAP))
            return true;
    }
    return false;
}

static bool idle_work(void)
{
    if (render_work()) /* a heavy page on screen, a slice at a time */
        return true;
    for (int i = 0; i < nwindows; i++) {
        Window *w = windows[i];
        if (!w->split) {
            if (view_prefetch(w, cur_view(w), top_of(w)))
                return true;
            continue;
        }
        for (int p = 0; p < 2; p++) { /* each pane at its own size */
            bool more = false;
            if (pane_enter(w, p)) {
                more = view_prefetch(w, pane_view(w, p), top_of(w));
                pane_leave(w);
            }
            if (more)
                return true;
        }
    }
    /* annotations are listed when the panel needs them, not in advance:
     * opening every page keeps its parsed objects in PDFium (tens of MB
     * per big document) until the document is closed */
    if (grid_work()) /* thumbnails of the page grid, one at a time */
        return true;
    if (present_work()) /* the next slide, ready before it is asked for */
        return true;
    if (compact_docs(SDL_GetTicks()))
        return true;
    if (memory_freed) { /* freed pages and textures: back to the system, not kept by malloc */
        malloc_trim(0);
        memory_freed = false;
    }
    return false;
}

/* ---------- test mode: scripted input and screenshots ---------- */
static const char *script, *script_dir;

/* Runs one script token; returns false when the script is finished. */
static bool script_step(void)
{
    if (!script || !*script || !nwindows)
        return false;
    Window *w = windows[nwindows - 1];
    if (*script != '<' || !strchr(script, '>')) {
        const char *s = script;
        utf8_next(&script);
        char t[8];
        snprintf(t, sizeof t, "%.*s", (int)(script - s), s);
        input_text_event(w, t);
        return true;
    }
    const char *end = strchr(script, '>');
    char tok[64];
    snprintf(tok, sizeof tok, "%.*s", (int)(end - script + 1), script);
    script = end + 1;
    static char path[1024];
    if (!strncmp(tok, "<shot:", 6)) {
        snprintf(path, sizeof path, "%s/%.*s.bmp", script_dir, (int)strlen(tok) - 7, tok + 6);
        for (int k = 0; k < 20; k++) { /* heavy pages finished first */
            render_incomplete = false;
            draw(w, SDL_GetTicks());
            if (!render_incomplete)
                break;
            render_finish();
        }
        capture_path = path;
        draw(w, SDL_GetTicks());
    } else if (!strncmp(tok, "<resize:", 8)) {
        int rw = 0, rh = 0;
        if (sscanf(tok + 8, "%d,%d", &rw, &rh) == 2)
            SDL_SetWindowSize(w->win, SDL_max(1, rw), SDL_max(1, rh));
        w->redraw = true;
    } else if (!strncmp(tok, "<drag:", 6)) { /* the left button, from one point to another (window points) */
        float x0, y0, x1, y1;
        int frames = 0; /* with a fifth number: that many frames, each drawn, and the time they took */
        int got = sscanf(tok + 6, "%f,%f,%f,%f,%d", &x0, &y0, &x1, &y1, &frames);
        if (got >= 4) {
            SDL_WindowID id = SDL_GetWindowID(w->win);
            SDL_Event e = {0};
            e.button = (SDL_MouseButtonEvent){.type = SDL_EVENT_MOUSE_BUTTON_DOWN, .windowID = id,
                                              .button = SDL_BUTTON_LEFT, .down = true, .clicks = 1, .x = x0, .y = y0};
            handle_event(&e);
            int steps = got == 5 && frames > 0 ? frames : 8;
            uint64_t t0 = SDL_GetPerformanceCounter(), worst = 0, flush_ticks = 0;
            for (int k = 1; k <= steps && alive(w); k++) {
                e.motion = (SDL_MouseMotionEvent){.type = SDL_EVENT_MOUSE_MOTION, .windowID = id, .state = SDL_BUTTON_LMASK,
                                                  .x = x0 + (x1 - x0) * k / steps, .y = y0 + (y1 - y0) * k / steps};
                handle_event(&e);
                if (got == 5 && alive(w)) { /* what a frame does: apply the pointer, then draw */
                    uint64_t f0 = SDL_GetPerformanceCounter();
                    mouse_flush(w);
                    uint64_t f1 = SDL_GetPerformanceCounter();
                    draw(w, SDL_GetTicks());
                    uint64_t f2 = SDL_GetPerformanceCounter();
                    worst = SDL_max(worst, f2 - f0);
                    flush_ticks += f1 - f0;
                }
            }
            if (got == 5) {
                double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
                fprintf(stderr, "drag: %d frames, %.1f ms each on average (%.1f applying), worst %.1f ms\n", steps,
                        (double)(SDL_GetPerformanceCounter() - t0) * ms / steps, (double)flush_ticks * ms / steps,
                        (double)worst * ms);
            }
            e.button = (SDL_MouseButtonEvent){.type = SDL_EVENT_MOUSE_BUTTON_UP, .windowID = id,
                                              .button = SDL_BUTTON_LEFT, .clicks = 1, .x = x1, .y = y1};
            if (alive(w))
                handle_event(&e);
        }
    } else if (!strncmp(tok, "<down:", 6) || !strncmp(tok, "<move:", 6) || !strncmp(tok, "<up:", 4)) {
        /* the left button pressed, moved (a frame is drawn) and released, one step at a time */
        float x, y;
        bool up = tok[1] == 'u';
        if (sscanf(tok + (up ? 4 : 6), "%f,%f", &x, &y) == 2) {
            SDL_WindowID id = SDL_GetWindowID(w->win);
            SDL_Event e = {0};
            if (tok[1] == 'd')
                e.button = (SDL_MouseButtonEvent){.type = SDL_EVENT_MOUSE_BUTTON_DOWN, .windowID = id,
                                                  .button = SDL_BUTTON_LEFT, .down = true, .clicks = 1, .x = x, .y = y};
            else if (up)
                e.button = (SDL_MouseButtonEvent){.type = SDL_EVENT_MOUSE_BUTTON_UP, .windowID = id,
                                                  .button = SDL_BUTTON_LEFT, .clicks = 1, .x = x, .y = y};
            else
                e.motion = (SDL_MouseMotionEvent){.type = SDL_EVENT_MOUSE_MOTION, .windowID = id,
                                                  .state = SDL_BUTTON_LMASK, .x = x, .y = y};
            handle_event(&e);
            if (alive(w))
                mouse_flush(w);
        }
    } else if (!strncmp(tok, "<wait:", 6)) {
        uint64_t until = SDL_GetTicks() + (uint64_t)atoi(tok + 6);
        while (SDL_GetTicks() < until) {
            input_expire(SDL_GetTicks());
            if (math_poll())
                need_redraw_all = true;
            if (!idle_work())
                SDL_Delay(5);
        }
    } else {
        input_token(w, tok);
    }
    return true;
}

/* A password from the terminal, not echoed. */
static bool read_password(const char *path, char *out, size_t cap)
{
    struct termios old, quiet;
    fprintf(stderr, "Password for %s: ", path);
    fflush(stderr);
    bool echo_off = tcgetattr(STDIN_FILENO, &old) == 0;
    if (echo_off) {
        quiet = old;
        quiet.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet);
    }
    bool ok = fgets(out, (int)cap, stdin) != NULL;
    if (echo_off)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
    fputc('\n', stderr);
    if (ok)
        out[strcspn(out, "\r\n")] = 0;
    return ok && *out;
}

static void usage(void)
{
    printf("usage: npdf file.pdf [other.pdf ...]\n"
           "\n"
           "  j/k h/l      scroll          gg / G / 42G   first / last / page 42\n"
           "  Ctrl-d/u     half screen     Ctrl-f/b       full screen\n"
           "  J/K          next / previous page\n"
           "  + / - / =    zoom / fit width              z   fit page\n"
           "  d            two pages       i              dark mode      r / R  rotate\n"
           "  %sma  ma      set / jump to mark a           mm  back to where you were\n"
           "  Ctrl-o/i     jump list                      %sl  panel: tabs, summary, marks, highlights\n"
           "  v            text cursor: hjkl w b 0 $, v selects, C V B N M highlight,\n"
           "               c comments, y copies, x deletes a highlight\n"
           "  Ctrl-v       free cursor (areas, figures): hjkl, J K faster, v, C V B N M, c\n"
           "  Ctrl-j/k     next / previous form field (Esc leaves)\n"
           "  / n N        search          :e file        open in a new tab\n"
           "  t            show tabs       Ctrl-, Ctrl-.  previous / next tab\n"
           "  Ctrl-n       new window      Ctrl-s / :w    save\n"
           "  :q           close tab       q              close window\n"
           "  %sc          every key (again closes it)\n"
           "\n"
           "LaTeX: --synctex-forward LINE:COL:FILE.tex file.pdf shows that line;\n"
           "       double click, Ctrl+click or Enter in a cursor opens the source (-x COMMAND)\n"
           "\n"
           "--export file.pdf   print its highlights and comments as Markdown\n"
           "\n"
           "config: ~/.config/nomadys-pdf/config\n",
           leader_label(), leader_label(), leader_label());
}

int main(int argc, char **argv)
{
    const char *forward = NULL, *editor = NULL;
    if (argc == 4 && !strcmp(argv[1], "--index-worker")) { /* the library index, a process apart (library.c) */
        config_load();
        FPDF_LIBRARY_CONFIG lib = {.version = 2};
        FPDF_InitLibraryWithConfig(&lib);
        int code = library_worker(argv[2], atoi(argv[3]));
        FPDF_DestroyLibrary();
        return code;
    }
    while (argc > 2 && argv[1][0] == '-' && strcmp(argv[1], "--help") && strcmp(argv[1], "--version")) {
        if (!strcmp(argv[1], "--test-script"))
            script = argv[2];
        else if (!strcmp(argv[1], "--test-out"))
            script_dir = argv[2];
        else if (!strcmp(argv[1], "--test-size"))
            sscanf(argv[2], "%dx%d", &test_w, &test_h);
        else if (!strcmp(argv[1], "--synctex-forward"))
            forward = argv[2];
        else if (!strcmp(argv[1], "--synctex-editor-command") || !strcmp(argv[1], "-x"))
            editor = argv[2];
        else
            break;
        argv += 2;
        argc -= 2;
    }
    if (script && !script_dir)
        script_dir = ".";
    config_load();
    if (editor)
        snprintf(cfg.synctex_editor, sizeof cfg.synctex_editor, "%s", editor);
    /* VimTeX forward search: "--synctex-forward LINE:COL:TEX file.pdf" */
    char request[8192] = "";
    if (forward && argc >= 2) {
        int line = 0, col = 0, used = 0;
        char *pdf = realpath(argv[argc - 1], NULL);
        if (sscanf(forward, "%d:%d:%n", &line, &col, &used) >= 2 && used && pdf)
            snprintf(request, sizeof request, "forward\t%s\t%d\t%d\t%s\n", pdf, line, col, forward + used);
        free(pdf);
        if (request[0] && !script && ipc_send(request))
            return 0; /* a running instance shows it */
    }
    /* npdf alone: the tabs of the last session */
    char **files = argv + 1, **session = NULL;
    int nfiles = argc - 1, session_cur = 0;
    if (argc < 2) {
        nfiles = session_files(&session, &session_cur);
        files = session;
    }
    if (!nfiles || (argc >= 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))) {
        usage();
        return argc < 2;
    }
    if (argc >= 2 && !strcmp(argv[1], "--version")) {
        puts("Nomadys-PDF (npdf) " VERSION);
        return 0;
    }
    FPDF_LIBRARY_CONFIG lib = {.version = 2};
    FPDF_InitLibraryWithConfig(&lib);
    if (argc >= 2 && !strcmp(argv[1], "--export")) { /* highlights as Markdown, to standard output */
        char err[512];
        Doc *d = argc == 3 ? doc_open(argv[2], err, sizeof err) : NULL;
        if (!d)
            fprintf(stderr, "npdf: %s\n", argc == 3 ? err : "usage: npdf --export file.pdf");
        else
            export_write(d, stdout, NULL, NULL);
        FPDF_DestroyLibrary();
        return d ? 0 : 1;
    }
    SDL_SetAppMetadata("Nomadys-PDF", VERSION, "npdf"); /* app id: npdf.desktop gives the icon */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "npdf: %s\n", SDL_GetError());
        return 1;
    }
    ipc_start();
    theme_watch(); /* saving your theme shows it at once */
    Window *w = NULL;
    const char *ask_later = NULL; /* protected, no terminal: asked in the window */
    for (int i = 0; i < nfiles; i++) {
        char err[512];
        bool locked;
        Doc *d = doc_open_locked(files[i], NULL, &locked, err, sizeof err);
        for (int tries = 0; !d && locked && isatty(STDIN_FILENO) && tries < 3; tries++) {
            char pw[256];
            if (!read_password(files[i], pw, sizeof pw))
                break;
            d = doc_open_locked(files[i], pw, &locked, err, sizeof err);
            explicit_bzero(pw, sizeof pw);
        }
        if (!d && locked && !isatty(STDIN_FILENO) && !ask_later) {
            ask_later = files[i];
            continue;
        }
        if (!d) {
            fprintf(stderr, "npdf: %s\n", err);
            continue;
        }
        recent_add(files[i]);
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
    if (!w && ask_later)
        fprintf(stderr, "npdf: %s is password protected: run npdf in a terminal, or open it with :e\n", ask_later);
    if (!w) {
        for (int i = 0; session && i < nfiles; i++)
            free(session[i]);
        free(session);
        SDL_Quit();
        FPDF_DestroyLibrary();
        return 1;
    }
    if (session && session_cur < w->ntabs)
        w->cur = session_cur;
    if (ask_later)
        ask_password(w, ask_later, false);
    for (int i = 0; session && i < nfiles; i++)
        free(session[i]);
    free(session);
    session_save();
    if (request[0]) {
        SDL_Event e = {0};
        e.type = ipc_event;
        e.user.code = IPC_REQUEST;
        e.user.data1 = xstrdup(request);
        SDL_PushEvent(&e);
    }
    bool prefetching = false;
    while (nwindows) {
        uint64_t now = SDL_GetTicks();
        int wait = -1;
        for (int i = 0; i < ndocs; i++) {
            int t = doc_run_timers(docs[i], now);
            if (t >= 0)
                wait = wait < 0 ? t : SDL_min(wait, t);
        }
        for (int i = 0; i < nwindows; i++) {
            Window *x = windows[i];
            if (cursor_locating(x, now) || (x->locate_at && now - x->locate_at < LOCATE_MS + 100)) {
                x->redraw = true; /* animation frames, then one to clear it */
                wait = wait < 0 ? 16 : SDL_min(wait, 16);
                if (!cursor_locating(x, now))
                    x->locate_at = 0;
            }
            uint64_t until[3] = {x->msg_until, x->tabs_flash_until, x->ntabs ? cur_view(x)->flash_until : 0};
            for (int k = 0; k < 3; k++) {
                if (until[k] > now) {
                    int left = (int)(until[k] - now) + 1;
                    wait = wait < 0 ? left : SDL_min(wait, left);
                } else if (until[k] && now - until[k] < 100) {
                    x->redraw = true; /* overlay just expired */
                }
            }
        }
        if (math_poll()) /* a formula was typeset */
            need_redraw_all = true;
        if (math_busy())
            wait = wait < 0 ? 40 : SDL_min(wait, 40);
        autosave(now, &wait);
        reload_due(now, &wait);
        input_expire(now);
        int keys = input_wait_ms(now);
        if (keys >= 0)
            wait = wait < 0 ? keys : SDL_min(wait, keys);
        for (int i = 0; i < nwindows; i++) /* the mouse moved a picture: once per frame, not once per event */
            mouse_flush(windows[i]);
        for (int i = 0; i < nwindows; i++)
            if (windows[i]->redraw || need_redraw_all) {
                render_incomplete = false;
                draw(windows[i], now);
                if (render_incomplete) { /* a heavy page: again soon, with more of it */
                    windows[i]->redraw = true;
                    wait = render_busy() ? 0 : (wait < 0 ? 16 : SDL_min(wait, 16));
                }
            }
        need_redraw_all = false;
        if (prefetching || script)
            wait = 0;
        SDL_Event e;
        /* nothing scheduled: sleep until something happens, unless memory
         * can be given back soon (compact_docs, malloc_trim) */
        if (wait < 0 && maintenance_pending())
            wait = 1000;
        if (SDL_WaitEventTimeout(&e, wait)) {
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
        prefetching = idle_work();
    }
    while (ndocs)
        doc_unref(docs[0]);
    ipc_stop();
    library_shutdown();
    recent_shutdown();
    math_shutdown();
    textbox_shutdown();
    font_shutdown();
    SDL_Quit();
    FPDF_DestroyLibrary();
    return 0;
}
