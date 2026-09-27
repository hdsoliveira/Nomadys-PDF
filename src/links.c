/* Links by keyboard: 'f' labels every visible link with letters (citations,
 * figure and equation references, outline entries, URLs in annotations or
 * in the text) and follows the one typed; 'F' previews an internal link's
 * target (a figure, an equation, a bibliography entry) in a small box
 * without leaving the page. */
#include <ctype.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fpdf_doc.h"
#include "nomadys.h"

#define HINT_CHARS "sadfjklewcmpgh" /* home row first, like vimium */
#define PREVIEW_SCROLL 40.0f         /* points per j/k in the preview */

extern char **environ;

/* ---------- collecting links ---------- */
static Pos dest_pos(Doc *d, FPDF_DEST dest, bool *ok)
{
    Pos p = {FPDFDest_GetDestPageIndex(d->pdf, dest), 0};
    *ok = doc_page_ok(d, p.page);
    FPDF_BOOL hx, hy, hz;
    FS_FLOAT x, y, z;
    if (*ok && FPDFDest_GetLocationInPage(dest, &hx, &hy, &hz, &x, &y, &z) && hy)
        p.y = SDL_clamp(1 - y / d->ph[p.page], 0.0f, 1.0f);
    return p;
}

static void add_link(Window *w, int *cap, Link l)
{
    if (w->nlinks == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        w->links = xrealloc(w->links, *cap * sizeof *w->links);
    }
    w->links[w->nlinks++] = l;
}

static bool on_screen(Window *w, View *v, int page, PRect r)
{
    float x0, y0, x1, y1;
    int top = top_of(w);
    page_to_screen(v, page, r.l, r.t, &x0, &y0, top);
    page_to_screen(v, page, r.r, r.b, &x1, &y1, top);
    return SDL_max(x0, x1) > 0 && SDL_min(x0, x1) < w->W && SDL_max(y0, y1) > top && SDL_min(y0, y1) < w->H;
}

static bool covered(Window *w, int page, PRect r)
{
    float cx = (r.l + r.r) / 2, cy = (r.t + r.b) / 2;
    for (int i = 0; i < w->nlinks; i++) {
        Link *o = &w->links[i];
        if (o->page == page && cx >= o->r.l && cx <= o->r.r && cy <= o->r.t && cy >= o->r.b)
            return true;
    }
    return false;
}

static void page_links(Window *w, View *v, int page, int *cap)
{
    Doc *d = v->doc;
    FPDF_PAGE p = doc_page(d, page);
    if (!p)
        return;
    int pos = 0;
    FPDF_LINK link;
    while (FPDFLink_Enumerate(p, &pos, &link)) {
        FS_RECTF fr;
        if (!FPDFLink_GetAnnotRect(link, &fr))
            continue;
        Link l = {.page = page, .r = {SDL_min(fr.left, fr.right), SDL_max(fr.top, fr.bottom),
                                      SDL_max(fr.left, fr.right), SDL_min(fr.top, fr.bottom)}};
        FPDF_DEST dest = FPDFLink_GetDest(d->pdf, link);
        FPDF_ACTION action = dest ? NULL : FPDFLink_GetAction(link);
        unsigned long type = action ? FPDFAction_GetType(action) : 0;
        if (!dest && type == PDFACTION_GOTO)
            dest = FPDFAction_GetDest(d->pdf, action);
        if (dest) {
            l.target = dest_pos(d, dest, &l.internal);
        } else if (type == PDFACTION_URI) {
            unsigned long n = FPDFAction_GetURIPath(d->pdf, action, l.uri, sizeof l.uri);
            if (n == 0 || n > sizeof l.uri)
                l.uri[0] = 0;
        } else if (type == PDFACTION_REMOTEGOTO || type == PDFACTION_LAUNCH) {
            char file[900];
            unsigned long n = FPDFAction_GetFilePath(action, file, sizeof file);
            if (n > 0 && n <= sizeof file)
                snprintf(l.uri, sizeof l.uri, "file:%s", file);
        }
        if ((l.internal || l.uri[0]) && on_screen(w, v, page, l.r))
            add_link(w, cap, l);
    }
    /* URLs written in the text, not made into links by the author */
    FPDF_TEXTPAGE tp = doc_text(d, page);
    FPDF_PAGELINK pl = tp ? FPDFLink_LoadWebLinks(tp) : NULL;
    for (int i = 0; pl && i < FPDFLink_CountWebLinks(pl); i++) {
        double l, t, r, b;
        if (FPDFLink_CountRects(pl, i) < 1 || !FPDFLink_GetRect(pl, i, 0, &l, &t, &r, &b))
            continue;
        Link lk = {.page = page, .r = {(float)l, (float)t, (float)r, (float)b}};
        unsigned short u[1024];
        int n = FPDFLink_GetURL(pl, i, u, 1023);
        if (n <= 1 || covered(w, page, lk.r) || !on_screen(w, v, page, lk.r))
            continue;
        char *s = utf16_to_utf8(u, n - 1);
        snprintf(lk.uri, sizeof lk.uri, "%s", s);
        free(s);
        add_link(w, cap, lk);
    }
    if (pl)
        FPDFLink_CloseWebLinks(pl);
}

/* Labels of equal length from HINT_CHARS: a, s, d... or sa, ss, sd... */
static void label_links(Window *w)
{
    int k = (int)strlen(HINT_CHARS), len = 1;
    for (int cap = k; cap < w->nlinks && len < 3; cap *= k)
        len++;
    for (int i = 0; i < w->nlinks; i++) {
        int n = i;
        for (int c = len - 1; c >= 0; c--, n /= k)
            w->links[i].label[c] = HINT_CHARS[n % k];
        w->links[i].label[len] = 0;
    }
}

void links_start(Window *w, bool preview)
{
    View *v = cur_view(w);
    int cap = 0, top = top_of(w);
    w->nlinks = 0;
    for (int page = 0; page < v->doc->npages; page++) {
        double sy = v->py[page] - v->sy + top;
        if (sy >= w->H)
            break;
        if (sy + v->phpx[page] > top)
            page_links(w, v, page, &cap);
    }
    if (!w->nlinks) {
        ui_message(w, "no links on screen");
        return;
    }
    label_links(w);
    w->hint_typed[0] = 0;
    w->hint_preview = preview;
    w->hint_return = w->mode;
    w->mode = MODE_HINTS;
    w->redraw = true;
}

/* ---------- following ---------- */
static bool safe_uri(const char *u)
{
    return !strncmp(u, "http://", 7) || !strncmp(u, "https://", 8) || !strncmp(u, "mailto:", 7) ||
           !strncmp(u, "doi:", 4) || !strncmp(u, "www.", 4);
}

static void open_uri(Window *w, const char *uri)
{
    char url[1100];
    if (!strncmp(uri, "doi:", 4))
        snprintf(url, sizeof url, "https://doi.org/%s", uri + 4);
    else if (!strncmp(uri, "www.", 4))
        snprintf(url, sizeof url, "https://%s", uri);
    else
        snprintf(url, sizeof url, "%s", uri);
    if (!safe_uri(uri)) {
        /* file: or program links could run anything: only show and copy them */
        SDL_SetClipboardText(uri);
        ui_message_long(w, 5000, "not opened (copied): %s", uri);
        return;
    }
    char *argv[] = {"xdg-open", url, NULL};
    pid_t pid;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", 1, 0);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", 1, 0);
    if (posix_spawnp(&pid, "xdg-open", &fa, NULL, argv, environ) == 0) {
        /* xdg-open hands the URL to the browser and exits quickly */
        waitpid(pid, NULL, 0);
        ui_message(w, "opening %s", url);
    } else {
        SDL_SetClipboardText(url);
        ui_message_long(w, 5000, "xdg-open not found; link copied: %s", url);
    }
    posix_spawn_file_actions_destroy(&fa);
}

static void follow(Window *w, View *v, Link *l)
{
    if (!l->internal) {
        open_uri(w, l->uri);
        return;
    }
    jump_to(w, v, l->target);
    /* flash a band where the link points, to find the spot at a glance */
    Doc *d = v->doc;
    float y = d->ph[l->target.page] * (1 - l->target.y);
    v->flash_page = l->target.page;
    v->flash_rect = (PRect){0, y, d->pw[l->target.page], y - 14};
    v->flash_until = SDL_GetTicks() + 1200;
    ui_message(w, "p. %d  (Ctrl-o returns)", l->target.page + 1);
}

/* ---------- preview ---------- */
static void preview_render(Window *w)
{
    View *v = cur_view(w);
    Doc *d = v->doc;
    int page = w->preview.page;
    if (w->preview.tex)
        SDL_DestroyTexture(w->preview.tex);
    w->preview.tex = NULL;
    float zoom = w->preview.w / d->pw[page];
    int pw = (int)(d->pw[page] * zoom), ph = (int)(d->ph[page] * zoom);
    float maxtop = SDL_max(0.0f, d->ph[page] - w->preview.h / zoom);
    w->preview.top = SDL_clamp(w->preview.top, 0.0f, maxtop);
    int cy = (int)(w->preview.top * zoom), h = SDL_min((int)w->preview.h, ph - cy);
    w->preview.tex = view_render_area(w, v, page, pw, ph, 0, 0, cy, pw, h);
    w->preview.th = h;
}

static void preview_open(Window *w, View *v, Link *l)
{
    if (!l->internal) {
        ui_message_long(w, 5000, "%s", l->uri); /* an external link: show where it goes */
        return;
    }
    Doc *d = v->doc;
    int pad = (int)(12 * w->density);
    w->preview.link = *l;
    w->preview.page = l->target.page;
    w->preview.w = (float)SDL_min(w->W - 4 * pad, (int)(900 * w->density));
    w->preview.h = (w->H - top_of(w)) * 0.42f;
    /* show a little above the anchor: captions and equations sit below it */
    float zoom = w->preview.w / d->pw[l->target.page];
    w->preview.top = d->ph[l->target.page] * l->target.y - 0.3f * w->preview.h / zoom;
    float y0, y1, x;
    page_to_screen(v, l->page, l->r.l, l->r.t, &x, &y0, top_of(w));
    page_to_screen(v, l->page, l->r.l, l->r.b, &x, &y1, top_of(w));
    w->preview.at_top = (y0 + y1) / 2 > w->H / 2; /* away from the link */
    preview_render(w);
    w->mode = MODE_PREVIEW;
    w->redraw = true;
}

void preview_scroll(Window *w, float lines)
{
    w->preview.top += lines * PREVIEW_SCROLL;
    preview_render(w);
    w->redraw = true;
}

void preview_close(Window *w)
{
    if (w->preview.tex)
        SDL_DestroyTexture(w->preview.tex);
    w->preview.tex = NULL;
    w->mode = MODE_NORMAL;
    w->redraw = true;
}

void preview_go(Window *w)
{
    View *v = cur_view(w);
    Link l = w->preview.link;
    preview_close(w);
    if (doc_page_ok(v->doc, l.target.page))
        follow(w, v, &l);
}

void preview_draw(Window *w)
{
    if (!w->preview.tex)
        return;
    const Theme *t = wtheme(w);
    int pad = (int)(12 * w->density), bar = ui_bar_height(w);
    float bw = w->preview.w, bh = (float)w->preview.th + bar;
    float x = (w->W - bw) / 2, y = w->preview.at_top ? top_of(w) + pad : w->H - bh - bar - 2 * pad;
    ui_fill(w, x - 3, y - 3, bw + 6, bh + 6, t->panel_header, 255);
    SDL_FRect dst = {x, y, bw, (float)w->preview.th};
    SDL_RenderTexture(w->ren, w->preview.tex, NULL, &dst);
    /* a mark in the margin where the link points */
    const Doc *d = cur_view(w)->doc;
    if (doc_page_ok(d, w->preview.page)) {
        float zoom = w->preview.w / d->pw[w->preview.page];
        float ty = (d->ph[w->preview.page] * w->preview.link.target.y - w->preview.top) * zoom;
        if (ty >= 0 && ty < w->preview.th)
            ui_fill(w, x, y + ty, 4 * w->density, font_height(w->font), t->search_match, 255);
    }
    ui_fill(w, x, y + w->preview.th, bw, (float)bar, t->panel_bg, 255);
    char info[128];
    snprintf(info, sizeof info - 4, "p. %d    Enter go there   j/k scroll   Esc close", w->preview.page + 1);
    font_fit(w->font, info, (int)bw - 2 * pad);
    font_draw(w->font, x + pad, y + w->preview.th + pad / 4.0f, info, t->panel_fg);
}

/* ---------- hint mode input and drawing ---------- */
void hints_key(Window *w, const char *tok)
{
    size_t n = strlen(w->hint_typed);
    if (!strcmp(tok, "<Esc>")) {
        w->mode = w->hint_return;
        w->redraw = true;
        return;
    }
    if (!strcmp(tok, "<BS>")) {
        if (n)
            w->hint_typed[n - 1] = 0;
        w->redraw = true;
        return;
    }
    if (tok[1] || !isalpha((unsigned char)tok[0]) || n + 1 >= sizeof w->hint_typed)
        return;
    w->hint_typed[n] = (char)tolower((unsigned char)tok[0]);
    w->hint_typed[n + 1] = 0;
    int match = -1, prefixes = 0;
    for (int i = 0; i < w->nlinks; i++) {
        if (strncmp(w->links[i].label, w->hint_typed, n + 1))
            continue;
        prefixes++;
        if (!w->links[i].label[n + 1])
            match = i;
    }
    w->redraw = true;
    if (!prefixes) {
        w->mode = w->hint_return;
        ui_message(w, "no link '%s'", w->hint_typed);
        return;
    }
    if (match < 0)
        return;
    View *v = cur_view(w);
    Link l = w->links[match];
    w->mode = w->hint_return == MODE_PREVIEW ? MODE_NORMAL : w->hint_return;
    if (w->hint_preview)
        preview_open(w, v, &l);
    else
        follow(w, v, &l);
}

void hints_draw(Window *w)
{
    View *v = cur_view(w);
    const Theme *t = wtheme(w);
    int top = top_of(w), p = (int)(3 * w->density), n = (int)strlen(w->hint_typed);
    /* letters in black or white, whichever reads better on the badge */
    uint32_t bg = t->search_match;
    int lum = (int)(((bg >> 16) & 0xFF) * 299 + ((bg >> 8) & 0xFF) * 587 + (bg & 0xFF) * 114) / 1000;
    uint32_t ink = lum > 140 ? 0x101010 : 0xFFFFFF;
    if (lum <= 140) /* mid-tone badges (gruvbox light): darken so white reads well */
        bg = rgb_mix(bg, 0x000000, 0.4f);
    uint32_t typed = rgb_mix(ink, bg, 0.55f);
    int fh = font_height(w->font);
    for (int i = 0; i < w->nlinks; i++) {
        Link *l = &w->links[i];
        if (strncmp(l->label, w->hint_typed, n))
            continue;
        float x0, y0, x1, y1;
        page_to_screen(v, l->page, l->r.l, l->r.t, &x0, &y0, top);
        page_to_screen(v, l->page, l->r.r, l->r.b, &x1, &y1, top);
        float x = SDL_min(x0, x1), y = SDL_min(y0, y1), by = y - fh * 0.6f;
        ui_fill(w, x, y, SDL_fabsf(x1 - x0), SDL_fabsf(y1 - y0), bg, 45);
        int tw = font_width(w->font, l->label);
        ui_fill(w, x - p - 1, by - 1, (float)(tw + 2 * p + 2), (float)fh + 2, ink, 255); /* thin outline */
        ui_fill(w, x - p, by, (float)(tw + 2 * p), (float)fh, bg, 255);
        char done[4];
        snprintf(done, sizeof done, "%.*s", n, l->label);
        font_draw(w->font, x, by, done, typed); /* typed part, dimmed */
        font_draw(w->font, x + font_width(w->font, done), by, l->label + n, ink);
    }
}

/* A plain click on a link follows it (Ctrl+click stays SyncTeX). */
bool links_click(Window *w, float sx, float sy)
{
    View *v = cur_view(w);
    int page;
    double px, py;
    if (!screen_to_page(v, sx, sy, top_of(w), &page, &px, &py))
        return false;
    int cap = 0;
    w->nlinks = 0;
    page_links(w, v, page, &cap);
    for (int i = 0; i < w->nlinks; i++) {
        Link *l = &w->links[i];
        if (px >= l->r.l && px <= l->r.r && py <= l->r.t && py >= l->r.b) {
            Link hit = *l;
            follow(w, v, &hit);
            return true;
        }
    }
    return false;
}
