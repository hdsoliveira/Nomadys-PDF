/* Nomadys-PDF: windows, tabs, drawing and the event loop. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

#define VERSION "0.2.0"
#define BLINK_MS 530

Window *windows[MAX_WINDOWS];
int nwindows;
static bool blink_on = true;
static uint64_t blink_at;
static const char *capture_path; /* test mode: save the next frame here */

View *cur_view(Window *w) { return w->ntabs ? w->tabs[w->cur] : NULL; }
int top_of(Window *w) { return ui_tabbar_height(w); }
const Theme *wtheme(Window *w) { return theme_of(w->ntabs && cur_view(w)->dark); }

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
    }
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
    font_close(w->font);
    free(w->items);
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
    ui_message(w, "alterações não salvas: Ctrl-s salva, repita para descartar");
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
        window_destroy(w);
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

void close_window(Window *w)
{
    for (int i = 0; i < w->ntabs; i++)
        discard_if_manual(w->tabs[i]);
    window_destroy(w);
}

bool open_tab(Window *w, const char *path)
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

void save(Window *w, View *v)
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

/* ---------- drawing ---------- */
static void draw(Window *w, uint64_t now)
{
    View *v = cur_view(w);
    update_size(w);
    int top = top_of(w);
    view_layout(v, w->W, w->H, top);
    view_clamp(v, w->W, w->H, top);
    uint32_t bg = wtheme(w)->background;
    SDL_SetRenderDrawColor(w->ren, bg >> 16, (bg >> 8) & 0xFF, bg & 0xFF, 255);
    SDL_RenderClear(w->ren);
    view_draw(w, v, top);
    ui_draw_notes(w, v, top);
    search_draw(w, v, top);
    cursor_draw(w, v, top, blink_on);
    if (w->mode == MODE_FORM)
        form_draw_focus(w, v, top);
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
static bool alive(Window *w)
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
            input_key_event(w, &e->key);
        return;
    case SDL_EVENT_TEXT_INPUT:
        if ((w = window_by_id(e->text.windowID)))
            input_text_event(w, e->text.text);
        return;
    case SDL_EVENT_MOUSE_WHEEL:
        if ((w = window_by_id(e->wheel.windowID))) {
            if (SDL_GetModState() & SDL_KMOD_CTRL)
                input_token(w, e->wheel.y > 0 ? "<C-=>" : "<C-->");
            else
                scroll(w, -e->wheel.x * 60 * w->density, -e->wheel.y * 60 * w->density);
        }
        return;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if ((w = window_by_id(e->button.windowID)) && e->button.button == SDL_BUTTON_LEFT &&
            (w->mode == MODE_NORMAL || w->mode == MODE_FORM))
            form_click(w, e->button.x * w->density, e->button.y * w->density,
                       e->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        if (w && alive(w))
            w->redraw = true;
        return;
    default:
        return;
    }
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

/* Background work while nothing happens; returns true if there is more. */
static bool idle_work(void)
{
    for (int i = 0; i < nwindows; i++)
        if (view_prefetch(windows[i], cur_view(windows[i]), top_of(windows[i])))
            return true;
    for (int i = 0; i < ndocs; i++)
        if (notes_scan(docs[i], 4)) /* list annotations for the panel */
            return true;
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
        capture_path = path;
        draw(w, SDL_GetTicks());
    } else if (!strncmp(tok, "<wait:", 6)) {
        uint64_t until = SDL_GetTicks() + (uint64_t)atoi(tok + 6);
        while (SDL_GetTicks() < until) {
            input_expire(SDL_GetTicks());
            if (!idle_work())
                SDL_Delay(5);
        }
    } else {
        input_token(w, tok);
    }
    return true;
}

static void usage(void)
{
    printf("uso: nomadys-pdf arquivo.pdf [arquivo2.pdf ...]\n"
           "\n"
           "  j/k h/l      rolar           gg / G / 42G   início / fim / página 42\n"
           "  Ctrl-d/u     meia tela       Ctrl-f/b       tela inteira\n"
           "  J/K          página seguinte/anterior\n"
           "  + / - / =    zoom / ajustar à largura       z   página inteira\n"
           "  d            duas páginas    i              modo escuro\n"
           "  %sma  ma      grava / salta para a marca a   mm  volta ao ponto anterior\n"
           "  Ctrl-o/i     histórico de saltos            %sl  painel de marcas e notas\n"
           "  v            cursor de texto: hjkl w b 0 $, v seleciona, H destaca,\n"
           "               c comenta, y copia, x apaga destaque\n"
           "  Ctrl-v       cursor livre (áreas e figuras): hjkl, v, H, c\n"
           "  Ctrl-j/k     próximo/anterior campo de formulário (Esc sai)\n"
           "  / n N        buscar          :e arquivo     abrir em nova aba\n"
           "  t            mostrar abas    Ctrl-, Ctrl-.  aba anterior/seguinte\n"
           "  Ctrl-n       nova janela     Ctrl-s / :w    salvar\n"
           "  :q           fechar aba      q              fechar janela\n"
           "\n"
           "config: ~/.config/nomadys-pdf/config\n",
           leader_label(), leader_label());
}

int main(int argc, char **argv)
{
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
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        usage();
        return argc < 2;
    }
    if (!strcmp(argv[1], "--version")) {
        puts("nomadys-pdf " VERSION);
        return 0;
    }
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
        bool blinking = false;
        for (int i = 0; i < nwindows; i++) {
            Window *x = windows[i];
            Mode m = x->mode == MODE_PROMPT ? x->prompt_return : x->mode;
            blinking |= m == MODE_CARET || m == MODE_AREA;
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
        if (blinking) {
            if (now >= blink_at) {
                blink_on = !blink_on;
                blink_at = now + BLINK_MS;
                for (int i = 0; i < nwindows; i++)
                    windows[i]->redraw |= windows[i]->mode == MODE_CARET || windows[i]->mode == MODE_AREA;
            }
            int left = (int)(blink_at - now);
            wait = wait < 0 ? left : SDL_min(wait, left);
        } else {
            blink_on = true;
        }
        autosave(now, &wait);
        input_expire(now);
        int keys = input_wait_ms(now);
        if (keys >= 0)
            wait = wait < 0 ? keys : SDL_min(wait, keys);
        for (int i = 0; i < nwindows; i++)
            if (windows[i]->redraw || need_redraw_all)
                draw(windows[i], now);
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
        prefetching = idle_work();
    }
    while (ndocs)
        doc_unref(docs[0]);
    SDL_Quit();
    FPDF_DestroyLibrary();
    return 0;
}
