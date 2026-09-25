/* Views: layout, page rendering, texture cache, dark mode. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

#define GAP 10
#define FULL_PAGE_MAX_PIXELS (12 * 1024 * 1024)
#define FULL_PAGE_MAX_SIDE 8192

static uint64_t cache_clock;

View *view_new(Doc *d)
{
    View *v = calloc(1, sizeof *v);
    v->doc = d;
    v->fit_width = cfg.zoom <= 0;
    v->dark = cfg.dark;
    v->match_page = -1;
    v->previous.page = -1;
    v->caret.page = -1;
    v->pending_pos = cfg.restore_position ? d->last : (Pos){-1, 0};
    int n = d->npages ? d->npages : 1;
    v->px = calloc(n, sizeof *v->px);
    v->py = calloc(n, sizeof *v->py);
    v->pwpx = calloc(n, sizeof *v->pwpx);
    v->phpx = calloc(n, sizeof *v->phpx);
    return v;
}

View *view_clone(View *src)
{
    View *v = view_new(src->doc);
    doc_ref(src->doc);
    v->zoom = src->zoom;
    v->fit_width = src->fit_width;
    v->dual = src->dual;
    v->dark = src->dark;
    v->rotation = src->rotation;
    v->sx = src->sx;
    v->sy = src->sy;
    v->pending_pos = (Pos){-1, 0};
    memcpy(v->query, src->query, sizeof v->query);
    return v;
}

void view_invalidate_cache(View *v)
{
    for (int i = 0; i < CACHE_SLOTS; i++) {
        if (v->cache[i].tex)
            SDL_DestroyTexture(v->cache[i].tex);
        v->cache[i] = (CacheEntry){0};
    }
}

/* After the document was reloaded: new page count and sizes, same place. */
void view_doc_reloaded(View *v)
{
    Pos p = v->lay_w ? view_pos(v) : v->pending_pos;
    int n = v->doc->npages ? v->doc->npages : 1;
    v->px = realloc(v->px, n * sizeof *v->px);
    v->py = realloc(v->py, n * sizeof *v->py);
    v->pwpx = realloc(v->pwpx, n * sizeof *v->pwpx);
    v->phpx = realloc(v->phpx, n * sizeof *v->phpx);
    view_invalidate_cache(v);
    v->lay_w = 0;
    v->content_h = 0;
    p.page = SDL_clamp(p.page, 0, n - 1);
    v->pending_pos = p;
    v->selecting = v->area_selecting = false;
    v->caret.page = -1;
    v->match_page = -1;
    v->flash_until = 0;
}

void view_free(View *v)
{
    if (v->lay_w)
        v->doc->last = view_pos(v);
    view_invalidate_cache(v);
    doc_unref(v->doc);
    free(v->px);
    free(v->py);
    free(v->pwpx);
    free(v->phpx);
    free(v);
}

float view_page_w(View *v, int page) { return v->rotation & 1 ? v->doc->ph[page] : v->doc->pw[page]; }
float view_page_h(View *v, int page) { return v->rotation & 1 ? v->doc->pw[page] : v->doc->ph[page]; }

/* Turns the view (not the file); keeps the reading position. */
void view_rotate(View *v, int quarter_turns)
{
    Pos p = v->lay_w ? view_pos(v) : v->pending_pos;
    v->rotation = ((v->rotation + quarter_turns) % 4 + 4) % 4;
    view_invalidate_cache(v);
    v->lay_w = 0;
    v->content_h = 0;
    v->pending_pos = p;
}

void view_layout(View *v, int W, int H, int top)
{
    (void)H, (void)top;
    Doc *d = v->doc;
    int step = v->dual ? 2 : 1;
    if (v->fit_width) {
        float widest = 1;
        for (int i = 0; i < d->npages; i += step) {
            float w = view_page_w(v, i) + (v->dual && i + 1 < d->npages ? view_page_w(v, i + 1) : 0);
            widest = SDL_max(widest, w);
        }
        int avail = W - 2 * GAP - (v->dual ? GAP : 0);
        v->zoom = SDL_max(0.05, avail / widest);
    }
    if (v->lay_w == W && v->lay_zoom == v->zoom && v->lay_dual == v->dual)
        return;
    double old_h = v->content_h;
    v->lay_w = W;
    v->lay_zoom = v->zoom;
    v->lay_dual = v->dual;
    int y = GAP, maxw = 0;
    for (int i = 0; i < d->npages; i += step) {
        int n = (v->dual && i + 1 < d->npages) ? 2 : 1, roww = 0, rowh = 0;
        for (int k = 0; k < n; k++) {
            v->pwpx[i + k] = (int)lround(view_page_w(v, i + k) * v->zoom);
            v->phpx[i + k] = (int)lround(view_page_h(v, i + k) * v->zoom);
            roww += v->pwpx[i + k] + (k ? GAP : 0);
            rowh = SDL_max(rowh, v->phpx[i + k]);
        }
        int x = 0;
        for (int k = 0; k < n; k++) {
            v->px[i + k] = x;
            v->py[i + k] = y;
            x += v->pwpx[i + k] + GAP;
        }
        maxw = SDL_max(maxw, roww);
        y += rowh + GAP;
    }
    v->content_w = maxw + 2 * GAP;
    v->content_h = y;
    /* centre each row horizontally inside the content width */
    for (int i = 0; i < d->npages; i += step) {
        int n = (v->dual && i + 1 < d->npages) ? 2 : 1;
        int roww = v->pwpx[i] + (n == 2 ? GAP + v->pwpx[i + 1] : 0);
        int off = (v->content_w - roww) / 2;
        for (int k = 0; k < n; k++)
            v->px[i + k] += off;
    }
    /* keep the relative scroll position across zoom/layout changes */
    if (old_h > 0)
        v->sy = v->sy * v->content_h / old_h;
    if (v->pending_pos.page >= 0) {
        view_set_pos(v, v->pending_pos);
        v->pending_pos.page = -1;
    }
}

Pos view_pos(View *v)
{
    int page = view_top_page(v);
    if (page < 0 || !v->phpx[page])
        return (Pos){0, 0};
    return (Pos){page, (float)((v->sy - v->py[page]) / v->phpx[page])};
}

void view_set_pos(View *v, Pos p)
{
    if (!v->lay_w) {
        v->pending_pos = p;
        return;
    }
    p.page = SDL_clamp(p.page, 0, v->doc->npages - 1);
    v->sy = v->py[p.page] + (double)p.y * v->phpx[p.page];
}

void view_clamp(View *v, int W, int H, int top)
{
    double maxy = SDL_max(0, v->content_h - (H - top));
    double maxx = SDL_max(0, v->content_w - W);
    v->sy = SDL_clamp(v->sy, 0, maxy);
    v->sx = SDL_clamp(v->sx, 0, maxx);
}

static double offset_x(View *v, int W)
{
    return v->content_w < W ? (W - v->content_w) / 2.0 : -v->sx;
}

int view_top_page(View *v)
{
    for (int i = 0; i < v->doc->npages; i++)
        if (v->py[i] + v->phpx[i] > v->sy + 1)
            return i;
    return v->doc->npages - 1;
}

void view_goto_page(View *v, int page)
{
    page = SDL_clamp(page, 0, v->doc->npages - 1);
    v->sy = v->py[page] - GAP;
}

static int layout_W(View *v) { return v->lay_w; }

void page_to_screen(View *v, int page, float x, float y, float *sx, float *sy, int top)
{
    int dx = 0, dy = 0;
    FPDF_PAGE p = doc_page(v->doc, page);
    if (p)
        FPDF_PageToDevice(p, 0, 0, v->pwpx[page], v->phpx[page], v->rotation, x, y, &dx, &dy);
    *sx = (float)(v->px[page] + dx + offset_x(v, layout_W(v)));
    *sy = (float)(v->py[page] + dy - v->sy + top);
}

bool screen_to_page(View *v, float sx, float sy, int top, int *page, double *x, double *y)
{
    double cx = sx - offset_x(v, layout_W(v)), cy = sy - top + v->sy;
    for (int i = 0; i < v->doc->npages; i++) {
        if (cx >= v->px[i] && cx < v->px[i] + v->pwpx[i] && cy >= v->py[i] && cy < v->py[i] + v->phpx[i]) {
            FPDF_PAGE p = doc_page(v->doc, i);
            if (!p)
                return false;
            *page = i;
            return FPDF_DeviceToPage(p, 0, 0, v->pwpx[i], v->phpx[i], v->rotation, (int)(cx - v->px[i]),
                                     (int)(cy - v->py[i]), x, y);
        }
    }
    return false;
}

void view_show_rect(View *v, int page, PRect r, int W, int H, int top)
{
    float x0, y0, x1, y1;
    page_to_screen(v, page, r.l, r.t, &x0, &y0, top);
    page_to_screen(v, page, r.r, r.b, &x1, &y1, top);
    float ymin = SDL_min(y0, y1), ymax = SDL_max(y0, y1);
    float xmin = SDL_min(x0, x1), xmax = SDL_max(x0, x1);
    if (ymin < top || ymax > H)
        v->sy += (ymin + ymax) / 2 - (top + (H - top) / 2.0f);
    if (xmin < 0 || xmax > W)
        v->sx += (xmin + xmax) / 2 - W / 2.0f;
    view_clamp(v, W, H, top);
}

/* ---------- dark mode ---------- */
static void darken(uint8_t *px, int w, int h, int stride, const uint8_t *keep)
{
    /* after the inversion, 0 (black paper) maps to page_bg and 255 (white
     * ink) to page_fg, per channel: themed paper, e.g. gruvbox brown */
    const Theme *t = theme_of(true);
    uint8_t lut[3][256];
    for (int c = 0; c < 3; c++) {
        int lo = (t->page_bg >> (8 * c)) & 0xFF, hi = (t->page_fg >> (8 * c)) & 0xFF;
        for (int i = 0; i < 256; i++)
            lut[c][i] = (uint8_t)(lo + i * (hi - lo) / 255);
    }
    for (int y = 0; y < h; y++) {
        uint8_t *p = px + (size_t)y * stride;
        const uint8_t *k = keep ? keep + (size_t)y * w : NULL;
        for (int x = 0; x < w; x++, p += 4) {
            if (k && k[x])
                continue;
            /* invert HSL lightness, keep hue and saturation: c + 255 - max - min */
            int b = p[0], g = p[1], r = p[2];
            int mx = SDL_max(r, SDL_max(g, b)), mn = SDL_min(r, SDL_min(g, b));
            int dlt = 255 - mx - mn;
            p[0] = lut[0][b + dlt];
            p[1] = lut[1][g + dlt];
            p[2] = lut[2][r + dlt];
        }
    }
}

static uint8_t *image_mask(Doc *d, int page, FPDF_PAGE p, int cx, int cy, int w, int h, int pw, int ph, int rot)
{
    doc_page_images(d, page);
    PageSlot *s = &d->slots[page];
    if (!s->nimages)
        return NULL;
    uint8_t *m = NULL;
    for (int k = 0; k < s->nimages; k++) {
        PRect r = s->images[k];
        int x0, y0, x1, y1;
        FPDF_PageToDevice(p, -cx, -cy, pw, ph, rot, r.l, r.t, &x0, &y0);
        FPDF_PageToDevice(p, -cx, -cy, pw, ph, rot, r.r, r.b, &x1, &y1);
        int l = SDL_max(0, SDL_min(x0, x1)), rr = SDL_min(w, SDL_max(x0, x1) + 1);
        int t = SDL_max(0, SDL_min(y0, y1)), b = SDL_min(h, SDL_max(y0, y1) + 1);
        if (l >= rr || t >= b)
            continue;
        if (!m)
            m = calloc((size_t)w * h, 1);
        for (int y = t; y < b; y++)
            memset(m + (size_t)y * w + l, 1, rr - l);
    }
    return m;
}

/* ---------- rendering ---------- */
static SDL_Texture *render_page(Window *win, View *v, int page, int cx, int cy, int w, int h)
{
    Doc *d = v->doc;
    FPDF_PAGE p = doc_page(d, page);
    if (!p || w <= 0 || h <= 0)
        return NULL;
    int pw = v->pwpx[page], ph = v->phpx[page], stride = w * 4;
    uint8_t *buf = malloc((size_t)stride * h);
    if (!buf)
        return NULL;
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA, buf, stride);
    FPDFBitmap_FillRect(bmp, 0, 0, w, h, 0xFFFFFFFF);
    int flags = FPDF_ANNOT | (cfg.subpixel_text ? FPDF_LCD_TEXT : 0);
    FPDF_RenderPageBitmap(bmp, p, -cx, -cy, pw, ph, v->rotation, flags);
    if (d->form)
        FPDF_FFLDraw(d->form, bmp, p, -cx, -cy, pw, ph, v->rotation, flags);
    FPDFBitmap_Destroy(bmp);
    if (v->dark) {
        uint8_t *keep = image_mask(d, page, p, cx, cy, w, h, pw, ph, v->rotation);
        darken(buf, w, h, stride, keep);
        free(keep);
    }
    SDL_Texture *t = SDL_CreateTexture(win->ren, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (t) {
        SDL_UpdateTexture(t, NULL, buf, stride);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST); /* drawn 1:1, never resampled */
    }
    free(buf);
    return t;
}

static CacheEntry *cache_find(View *v, int page, int cx, int cy, int w, int h)
{
    unsigned ver = v->doc->slots[page].version;
    for (int i = 0; i < CACHE_SLOTS; i++) {
        CacheEntry *e = &v->cache[i];
        if (e->tex && e->page == page && e->pw == v->pwpx[page] && e->ph == v->phpx[page] &&
            e->dark == v->dark && e->version == ver && e->x <= cx && e->y <= cy &&
            e->x + e->w >= cx + w && e->y + e->h >= cy + h) {
            e->used = ++cache_clock;
            return e;
        }
    }
    return NULL;
}

static CacheEntry *cache_put(Window *win, View *v, int page, int cx, int cy, int w, int h)
{
    SDL_Texture *t = render_page(win, v, page, cx, cy, w, h);
    if (!t)
        return NULL;
    CacheEntry *slot = &v->cache[0];
    for (int i = 0; i < CACHE_SLOTS; i++) {
        CacheEntry *e = &v->cache[i];
        /* replace stale versions of the same page first, then the LRU entry */
        if (e->tex && e->page == page && (e->version != v->doc->slots[page].version ||
                                          e->pw != v->pwpx[page] || e->dark != v->dark)) {
            slot = e;
            break;
        }
        if (!e->tex || e->used < slot->used)
            slot = e;
    }
    if (slot->tex)
        SDL_DestroyTexture(slot->tex);
    *slot = (CacheEntry){page, cx, cy, w, h, v->pwpx[page], v->phpx[page], v->dark,
                         v->doc->slots[page].version, t, ++cache_clock};
    return slot;
}

static bool big_page(View *v, int page)
{
    long pw = v->pwpx[page], ph = v->phpx[page];
    return pw * ph > FULL_PAGE_MAX_PIXELS || pw > FULL_PAGE_MAX_SIDE || ph > FULL_PAGE_MAX_SIDE;
}

void view_draw(Window *win, View *v, int top)
{
    int W = win->W, H = win->H;
    double ox = offset_x(v, W);
    for (int i = 0; i < v->doc->npages; i++) {
        double sy = v->py[i] - v->sy + top, sx = v->px[i] + ox;
        if (sy >= H)
            break;
        if (sy + v->phpx[i] <= top || sx >= W || sx + v->pwpx[i] <= 0)
            continue;
        int pw = v->pwpx[i], ph = v->phpx[i];
        int cx = 0, cy = 0, cw = pw, ch = ph;
        if (big_page(v, i)) {
            cx = (int)SDL_max(0, -sx);
            cy = (int)SDL_max(0, top - sy);
            cw = (int)SDL_min(pw, W - sx) - cx;
            ch = (int)SDL_min(ph, H - sy) - cy;
        }
        CacheEntry *e = cache_find(v, i, cx, cy, cw, ch);
        if (!e && big_page(v, i)) {
            /* render a margin around the viewport so small scrolls reuse it */
            int mx = W / 2, my = H / 2;
            int ex = SDL_max(0, cx - mx), ey = SDL_max(0, cy - my);
            int ew = SDL_min(pw, cx + cw + mx) - ex, eh = SDL_min(ph, cy + ch + my) - ey;
            e = cache_put(win, v, i, ex, ey, ew, eh);
        } else if (!e) {
            e = cache_put(win, v, i, cx, cy, cw, ch);
        }
        if (!e)
            continue;
        /* whole-pixel position: a fractional offset would blur the page */
        SDL_FRect dst = {floorf((float)(sx + e->x) + 0.5f), floorf((float)(sy + e->y) + 0.5f),
                         (float)e->w, (float)e->h};
        SDL_RenderTexture(win->ren, e->tex, NULL, &dst);
    }
}

/* Render one not-yet-cached neighbour page; returns true if work was done. */
bool view_prefetch(Window *win, View *v, int top)
{
    int H = win->H, first = -1, last = -1;
    for (int i = 0; i < v->doc->npages; i++) {
        double sy = v->py[i] - v->sy + top;
        if (sy < H && sy + v->phpx[i] > top) {
            if (first < 0)
                first = i;
            last = i;
        }
    }
    if (first < 0 || last - first + 3 > CACHE_SLOTS)
        return false;
    int step = v->dual ? 2 : 1;
    int cand[4] = {last + 1, last + 1 + step, first - step, first - 1};
    for (int k = 0; k < 4; k++) {
        int p = cand[k];
        if (p < 0 || p >= v->doc->npages || big_page(v, p))
            continue;
        if (!cache_find(v, p, 0, 0, v->pwpx[p], v->phpx[p])) {
            cache_put(win, v, p, 0, 0, v->pwpx[p], v->phpx[p]);
            return true;
        }
    }
    return false;
}
