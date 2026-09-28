/* Views: layout, page rendering, texture cache, dark mode. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fpdf_progressive.h"
#include "nomadys.h"

#define GAP 18
#define FULL_PAGE_MAX_PIXELS (8 * 1024 * 1024)
#define FULL_PAGE_MAX_SIDE 8192
/* Page textures of every tab and window together; the least recently
 * used go first (usually tabs out of sight), never what is on screen. */
#define TEXTURE_BUDGET ((size_t)128 << 20)

static uint64_t cache_clock, frame_start;
static size_t texture_bytes;

static size_t entry_bytes(const CacheEntry *e) { return e->tex ? (size_t)e->w * (size_t)e->h * 4 : 0; }

static void drop_entry(CacheEntry *e)
{
    if (e->tex) {
        texture_bytes -= entry_bytes(e);
        SDL_DestroyTexture(e->tex);
    }
    *e = (CacheEntry){0};
}

static void trim_textures(void)
{
    while (texture_bytes > TEXTURE_BUDGET) {
        CacheEntry *old = NULL;
        for (int k = 0; k < nwindows; k++)
            for (int t = 0; t <= windows[k]->ntabs; t++) { /* the tabs, then the split pane */
                View *v = t < windows[k]->ntabs ? windows[k]->tabs[t] : windows[k]->split;
                for (int i = 0; v && i < CACHE_SLOTS; i++) {
                    CacheEntry *e = &v->cache[i];
                    if (e->tex && e->used <= frame_start && (!old || e->used < old->used))
                        old = e;
                }
            }
        if (!old)
            return; /* all on screen now */
        drop_entry(old);
        memory_freed = true;
    }
}

static void render_cancel_view(View *v);

View *view_new(Doc *d)
{
    View *v = xcalloc(1, sizeof *v);
    v->doc = d;
    v->fit_width = cfg.zoom <= 0;
    v->dark = cfg.dark;
    v->match_page = -1;
    v->previous.page = -1;
    v->caret.page = -1;
    v->pending_pos = cfg.restore_position ? d->last : (Pos){-1, 0};
    if (cfg.remember_view && d->prefs.set) {
        v->fit_width = d->prefs.zoom <= 0;
        v->dual = d->prefs.dual;
        v->dark = d->prefs.dark;
        v->rotation = d->prefs.rotation;
    }
    int n = d->npages ? d->npages : 1;
    v->px = xcalloc(n, sizeof *v->px);
    v->py = xcalloc(n, sizeof *v->py);
    v->pwpx = xcalloc(n, sizeof *v->pwpx);
    v->phpx = xcalloc(n, sizeof *v->phpx);
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
    render_cancel_view(v);
    for (int i = 0; i < CACHE_SLOTS; i++)
        drop_entry(&v->cache[i]);
}

/* After the document was reloaded: new page count and sizes, same place. */
void view_doc_reloaded(View *v)
{
    Pos p = v->lay_w ? view_pos(v) : v->pending_pos;
    int n = v->doc->npages ? v->doc->npages : 1;
    v->px = xrealloc(v->px, n * sizeof *v->px);
    v->py = xrealloc(v->py, n * sizeof *v->py);
    v->pwpx = xrealloc(v->pwpx, n * sizeof *v->pwpx);
    v->phpx = xrealloc(v->phpx, n * sizeof *v->phpx);
    view_invalidate_cache(v);
    v->lay_w = 0;
    v->content_h = 0;
    p.page = SDL_clamp(p.page, 0, n - 1);
    v->pending_pos = p;
    v->selecting = v->area_selecting = false;
    v->caret.page = -1;
    v->area_page = SDL_clamp(v->area_page, 0, n - 1); /* the free cursor stays on a page that exists */
    v->match_page = -1;
    v->query_absent = false; /* the text may have changed */
    v->flash_until = 0;
    diff_refresh(v); /* compare the new version again */
}

/* The view is kept with the file's marks, when it is not the default. */
static void view_remember(View *v)
{
    double zoom = v->fit_width || v->density <= 0 ? 0 : v->zoom / (96.0 / 72.0 * v->density);
    zoom = SDL_round(zoom * 100) / 100; /* whole percents */
    bool same_zoom = cfg.zoom <= 0 ? v->fit_width : !v->fit_width && SDL_fabs(zoom - cfg.zoom) < 0.005;
    Doc *d = v->doc;
    d->prefs.set = !same_zoom || v->dual || v->dark != cfg.dark || v->rotation;
    d->prefs.zoom = zoom;
    d->prefs.dual = v->dual;
    d->prefs.dark = v->dark;
    d->prefs.rotation = v->rotation;
}

void view_free(View *v)
{
    render_cancel_view(v);
    if (v->lay_w) {
        v->doc->last = view_pos(v);
        view_remember(v);
    }
    view_invalidate_cache(v);
    diff_free(v->diff);
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
        v->zoom = SDL_clamp(avail / widest, 0.05, 40.0); /* a 1-point page must not explode */
    }
    if (v->lay_w == W && v->lay_zoom == v->zoom && v->lay_dual == v->dual)
        return;
    double old_h = v->content_h;
    v->lay_w = W;
    v->lay_n = v->doc->npages;
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

static int laid_pages(View *v);

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
    p.page = SDL_clamp(p.page, 0, SDL_max(0, laid_pages(v) - 1));
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

/* Only the pages the layout knows: when a reload or an undo changed the
 * page count, the arrays are the old ones until the next layout. */
static int laid_pages(View *v) { return v->lay_w ? SDL_min(v->doc->npages, v->lay_n) : 0; }

int view_top_page(View *v)
{
    int n = laid_pages(v);
    for (int i = 0; i < n; i++)
        if (v->py[i] + v->phpx[i] > v->sy + 1)
            return i;
    return SDL_max(0, n - 1);
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
/* Recolours a rendered page with the theme's paper and ink, outside images.
 * Dark mode first inverts lightness (keeping hue: c + 255 - max - min), so
 * white paper becomes 0; each channel then maps paper -> page_bg, ink ->
 * page_fg. The map bends towards the ink (INK_WEIGHT): light text on dark
 * paper looks thinner than dark text on white, and the half-covered pixels
 * at the edges of letters would otherwise fade into the paper. (Light mode
 * never calls this: pages stay white.) */
#define INK_WEIGHT 1.6
static void recolor(uint8_t *px, int w, int h, int stride, const uint8_t *keep, bool dark)
{
    const Theme *t = theme_of(dark);
    uint8_t lut[3][256];
    for (int c = 0; c < 3; c++) {
        int paper = (t->page_bg >> (8 * c)) & 0xFF, ink = (t->page_fg >> (8 * c)) & 0xFF;
        for (int i = 0; i < 256; i++) {
            double k = dark ? 1 - pow(1 - i / 255.0, INK_WEIGHT) : i / 255.0;
            lut[c][i] = (uint8_t)SDL_lround(dark ? paper + k * (ink - paper) : ink + k * (paper - ink));
        }
    }
    for (int y = 0; y < h; y++) {
        uint8_t *p = px + (size_t)y * stride;
        const uint8_t *k = keep ? keep + (size_t)y * w : NULL;
        for (int x = 0; x < w; x++, p += 4) {
            if (k && k[x])
                continue;
            int b = p[0], g = p[1], r = p[2];
            if (dark) {
                int dlt = 255 - SDL_max(r, SDL_max(g, b)) - SDL_min(r, SDL_min(g, b));
                b += dlt, g += dlt, r += dlt;
            }
            p[0] = lut[0][b];
            p[1] = lut[1][g];
            p[2] = lut[2][r];
        }
    }
}

/* A photo keeps its colours in dark mode; a figure drawn on white (plots,
 * diagrams, schematics) is recoloured with the page so it does not glare.
 * Tell them apart by the share of near-white pixels. */
static bool looks_like_photo(const uint8_t *px, int stride, int l, int t, int r, int b)
{
    long white = 0, total = 0;
    int step = SDL_max(1, SDL_min(r - l, b - t) / 64);
    for (int y = t; y < b; y += step) {
        const uint8_t *row = px + (size_t)y * stride;
        for (int x = l; x < r; x += step, total++) {
            const uint8_t *q = row + 4 * x;
            white += q[0] > 225 && q[1] > 225 && q[2] > 225;
        }
    }
    return total && white * 100 < total * 30;
}

/* Pixels to leave untouched in dark mode (see dark_images in the config). */
static uint8_t *image_mask(Doc *d, int page, FPDF_PAGE p, int cx, int cy, int w, int h, int pw, int ph, int rot,
                           const uint8_t *px, int stride)
{
    if (cfg.dark_images == DARK_IMAGES_INVERT)
        return NULL;
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
        if (cfg.dark_images == DARK_IMAGES_SMART && k < s->placed_from && !looks_like_photo(px, stride, l, t, rr, b))
            continue;
        if (!m && !(m = calloc((size_t)w * h, 1)))
            return NULL; /* no memory for the mask: recolour everything */
        const Alpha *al = k >= s->placed_from && s->alphas ? &s->alphas[k - s->placed_from] : NULL;
        if (!al || !al->a) {
            for (int y = t; y < b; y++)
                memset(m + (size_t)y * w + l, 1, rr - l);
            continue;
        }
        /* a see-through picture: only where it is opaque; the page around
         * and under it is recoloured. Its top-left corner and edges on
         * screen give each pixel's place in the picture. */
        int ox, oy, ux, uy, vx, vy;
        FPDF_PageToDevice(p, -cx, -cy, pw, ph, rot, r.l, r.t, &ox, &oy);
        FPDF_PageToDevice(p, -cx, -cy, pw, ph, rot, r.r, r.t, &ux, &uy);
        FPDF_PageToDevice(p, -cx, -cy, pw, ph, rot, r.l, r.b, &vx, &vy);
        double ax = ux - ox, ay = uy - oy, bx = vx - ox, by = vy - oy, det = ax * by - ay * bx;
        if (SDL_fabs(det) < 1)
            continue;
        for (int y = t; y < b; y++)
            for (int x = l; x < rr; x++) {
                double dx = x + 0.5 - ox, dy = y + 0.5 - oy;
                double u = (dx * by - dy * bx) / det, vv = (ax * dy - ay * dx) / det;
                int iu = (int)(u * al->w), iv = (int)(vv * al->h);
                if (iu >= 0 && iu < al->w && iv >= 0 && iv < al->h && al->a[(size_t)iv * al->w + iu] >= 128)
                    m[(size_t)y * w + x] = 1;
            }
    }
    return m;
}

/* ---------- rendering ---------- */
/* ---------- rendering, a little at a time ----------
 * A page full of vector drawings can take a second or more. It is rendered
 * in slices (PDFium's progressive rendering): some in each frame, the rest
 * while idle, and meanwhile the page shows what was drawn of it before (at
 * another zoom) or blank paper, so scrolling and typing never wait. One page
 * at a time; the job is dropped when its page is closed or changed. */
#define RENDER_FLAGS (FPDF_ANNOT | FPDF_RENDER_LIMITEDIMAGECACHE | (cfg.subpixel_text ? FPDF_LCD_TEXT : 0))
#define SLICE_NS (12 * 1000000ull) /* a slice while idle */
#define FRAME_NS (30 * 1000000ull) /* rendering in one frame, at most */

typedef struct {
    bool active, visible;    /* visible: asked for by the screen, not by prefetching */
    Window *win;
    View *v;
    Doc *doc;
    int page, pw, ph, rot, cx, cy, w, h, stride;
    bool dark;
    unsigned version;
    uint8_t *buf;
    FPDF_BITMAP bmp;
    FPDF_PAGE p;
} Job;

static Job job;
static uint64_t pause_at;
bool render_incomplete;      /* a page on screen still shows a placeholder */

static FPDF_BOOL need_pause(IFSDK_PAUSE *pause)
{
    (void)pause;
    return SDL_GetTicksNS() >= pause_at;
}

static IFSDK_PAUSE pauser = {1, need_pause, NULL};

static void job_drop(void)
{
    if (!job.active)
        return;
    FPDF_RenderPage_Close(job.p);
    FPDFBitmap_Destroy(job.bmp);
    free(job.buf);
    job = (Job){0};
}

/* Called before a page is closed or changed (doc.c), and for a view freed. */
void render_cancel(Doc *d, int page)
{
    if (job.active && job.doc == d && (page < 0 || job.page == page))
        job_drop();
}

static void render_cancel_view(View *v)
{
    if (job.active && job.v == v)
        job_drop();
}

/* ---------- highlights in dark mode ----------
 * PDFium draws a highlight by multiplying the page with its colour: black
 * text on yellow. Recoloured for dark mode that becomes light text on a
 * glaring yellow block. So on a dark page the highlighted places are drawn
 * again without annotations, recoloured like the rest, and then tinted
 * with the highlight's colour where the paper shows, the text keeping the
 * theme's ink. An area (a figure) is tinted more lightly than text. */
#define TINT_TEXT 0.35f
#define TINT_AREA 0.22f

typedef struct { int l, t, r, b; uint32_t color; bool area; } Tint;

static uint8_t *white_bitmap(int w, int h, FPDF_BITMAP *bmp);

/* The highlights of the page on the bitmap (cx, cy, w, h), in pixels. */
static int highlight_marks(Doc *d, int page, FPDF_PAGE p, int cx, int cy, int w, int h, int pw, int ph, int rot,
                           Tint **out)
{
    notes_page(d, page);
    PageSlot *s = &d->slots[page];
    int n = 0, cap = 0;
    *out = NULL;
    for (int i = 0; i < s->nnotes; i++) {
        const Note *nt = &s->notes[i];
        if (nt->subtype != FPDF_ANNOT_HIGHLIGHT)
            continue;
        for (int q = 0; q < nt->nquads; q++) {
            PRect r = nt->quads[q];
            int x0, y0, x1, y1;
            FPDF_PageToDevice(p, -cx, -cy, pw, ph, rot, r.l, r.t, &x0, &y0);
            FPDF_PageToDevice(p, -cx, -cy, pw, ph, rot, r.r, r.b, &x1, &y1);
            Tint m = {SDL_max(0, SDL_min(x0, x1)), SDL_max(0, SDL_min(y0, y1)), SDL_min(w, SDL_max(x0, x1) + 1),
                      SDL_min(h, SDL_max(y0, y1) + 1), nt->color, nt->area};
            if (m.l >= m.r || m.t >= m.b)
                continue;
            if (n == cap)
                *out = xrealloc(*out, (size_t)(cap = cap ? cap * 2 : 8) * sizeof **out);
            (*out)[n++] = m;
        }
    }
    return n;
}

/* The page under the highlights, as it is without them: drawn again in
 * bands, one for each group of highlights that overlap in height, so a
 * page with a highlight at the top and one at the bottom is not drawn
 * again whole (PDFium skips what lies outside the band). */
static int by_top(const void *a, const void *b) { return ((const Tint *)a)->t - ((const Tint *)b)->t; }

static void unhighlight(FPDF_PAGE p, uint8_t *buf, int stride, int cx, int cy, int pw, int ph, int rot,
                        const Tint *marks, int n)
{
    Tint *m = xmalloc((size_t)n * sizeof *m);
    memcpy(m, marks, (size_t)n * sizeof *m);
    qsort(m, (size_t)n, sizeof *m, by_top);
    for (int first = 0, last; first < n; first = last) {
        int l = m[first].l, t = m[first].t, r = m[first].r, b = m[first].b;
        for (last = first + 1; last < n && m[last].t <= b + 4; last++)
            l = SDL_min(l, m[last].l), r = SDL_max(r, m[last].r), b = SDL_max(b, m[last].b);
        FPDF_BITMAP bmp;
        uint8_t *under = white_bitmap(r - l, b - t, &bmp);
        if (!under)
            continue;
        FPDF_RenderPageBitmap(bmp, p, -(cx + l), -(cy + t), pw, ph, rot, RENDER_FLAGS & ~FPDF_ANNOT);
        FPDFBitmap_Destroy(bmp);
        for (int i = first; i < last; i++)
            for (int y = m[i].t; y < m[i].b; y++)
                memcpy(buf + (size_t)y * stride + 4 * m[i].l,
                       under + (size_t)(y - t) * (r - l) * 4 + 4 * (m[i].l - l), (size_t)(m[i].r - m[i].l) * 4);
        free(under);
    }
    free(m);
}

static int luma(int r, int g, int b) { return (r * 54 + g * 183 + b * 19) >> 8; }

static void tint(uint8_t *buf, int stride, const Tint *m, int n)
{
    const Theme *t = theme_of(true);
    int paper = luma(t->page_bg >> 16, (t->page_bg >> 8) & 0xFF, t->page_bg & 0xFF);
    int ink = luma(t->page_fg >> 16, (t->page_fg >> 8) & 0xFF, t->page_fg & 0xFF);
    int span = SDL_max(1, SDL_abs(ink - paper));
    for (int i = 0; i < n; i++) {
        int cr = m[i].color >> 16, cg = (m[i].color >> 8) & 0xFF, cb = m[i].color & 0xFF;
        float strength = m[i].area ? TINT_AREA : TINT_TEXT;
        for (int y = m[i].t; y < m[i].b; y++) {
            uint8_t *px = buf + (size_t)y * stride + 4 * m[i].l;
            for (int x = m[i].l; x < m[i].r; x++, px += 4) {
                /* full tint on the paper, none on the ink */
                float inkness = SDL_clamp((float)SDL_abs(luma(px[2], px[1], px[0]) - paper) / span, 0.0f, 1.0f);
                float a = strength * (1 - inkness);
                px[0] = (uint8_t)(px[0] + (cb - px[0]) * a);
                px[1] = (uint8_t)(px[1] + (cg - px[1]) * a);
                px[2] = (uint8_t)(px[2] + (cr - px[2]) * a);
            }
        }
    }
}

/* The page's pixels become a texture: form fields drawn on top, dark mode. */
static SDL_Texture *finish_pixels(Window *win, View *v, int page, FPDF_PAGE p, FPDF_BITMAP bmp, uint8_t *buf,
                                  int pw, int ph, int rot, int cx, int cy, int w, int h, int stride, bool dark)
{
    Doc *d = v->doc;
    if (d->form)
        FPDF_FFLDraw(d->form, bmp, p, -cx, -cy, pw, ph, rot, RENDER_FLAGS);
    FPDFBitmap_Destroy(bmp);
    if (dark) { /* light mode shows pages as they are */
        Tint *marks;
        int nmarks = highlight_marks(d, page, p, cx, cy, w, h, pw, ph, rot, &marks);
        if (nmarks)
            unhighlight(p, buf, stride, cx, cy, pw, ph, rot, marks, nmarks);
        uint8_t *keep = image_mask(d, page, p, cx, cy, w, h, pw, ph, rot, buf, stride);
        recolor(buf, w, h, stride, keep, dark);
        free(keep);
        if (nmarks)
            tint(buf, stride, marks, nmarks);
        free(marks);
    }
    SDL_Texture *t = SDL_CreateTexture(win->ren, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (t) {
        SDL_UpdateTexture(t, NULL, buf, stride);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST); /* drawn 1:1, never resampled */
    }
    free(buf);
    return t;
}

static uint8_t *white_bitmap(int w, int h, FPDF_BITMAP *bmp)
{
    uint8_t *buf = malloc((size_t)w * 4 * h);
    if (!buf)
        return NULL;
    *bmp = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA, buf, w * 4);
    if (!*bmp) {
        free(buf);
        return NULL;
    }
    FPDFBitmap_FillRect(*bmp, 0, 0, w, h, 0xFFFFFFFF);
    return buf;
}

/* Renders the part (cx, cy, w, h) of a page drawn at pw x ph pixels, with the
 * view's dark mode, into a texture, all at once (thumbnails, slides). */
SDL_Texture *view_render_area(Window *win, View *v, int page, int pw, int ph, int rot, int cx, int cy, int w, int h)
{
    Doc *d = v->doc;
    render_cancel(d, page); /* not while a slice of it is half done */
    FPDF_PAGE p = doc_page(d, page);
    if (!p || w <= 0 || h <= 0 || pw <= 0 || ph <= 0)
        return NULL;
    FPDF_BITMAP bmp;
    uint8_t *buf = white_bitmap(w, h, &bmp);
    if (!buf)
        return NULL;
    FPDF_RenderPageBitmap(bmp, p, -cx, -cy, pw, ph, rot, RENDER_FLAGS);
    return finish_pixels(win, v, page, p, bmp, buf, pw, ph, rot, cx, cy, w, h, w * 4, v->dark);
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

static CacheEntry *cache_store(View *v, int page, int cx, int cy, int w, int h, SDL_Texture *t)
{
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
    drop_entry(slot);
    *slot = (CacheEntry){page, cx, cy, w, h, v->pwpx[page], v->phpx[page], v->dark,
                         v->doc->slots[page].version, t, ++cache_clock};
    texture_bytes += entry_bytes(slot);
    trim_textures();
    return slot->tex ? slot : NULL;
}

/* The job is done (or failed): its texture goes to the cache. */
static CacheEntry *job_finish(void)
{
    Job j = job;
    FPDF_RenderPage_Close(j.p);
    job = (Job){0};
    bool current = j.v->pwpx[j.page] == j.pw && j.v->phpx[j.page] == j.ph && j.v->dark == j.dark &&
                   j.v->rotation == j.rot && j.doc->slots[j.page].version == j.version;
    SDL_Texture *t = finish_pixels(j.win, j.v, j.page, j.p, j.bmp, j.buf, j.pw, j.ph, j.rot, j.cx, j.cy, j.w, j.h,
                                   j.stride, j.dark);
    if (!t)
        return NULL;
    if (!current) { /* zoomed or turned meanwhile */
        SDL_DestroyTexture(t);
        return NULL;
    }
    return cache_store(j.v, j.page, j.cx, j.cy, j.w, j.h, t);
}

/* More of the job, for up to budget_ns; the entry once it is finished. */
static CacheEntry *job_continue(uint64_t budget_ns)
{
    if (!job.active)
        return NULL;
    pause_at = SDL_GetTicksNS() + budget_ns;
    if (FPDF_RenderPage_Continue(job.p, &pauser) == FPDF_RENDER_TOBECONTINUED)
        return NULL;
    return job_finish();
}

static CacheEntry *job_start(Window *win, View *v, int page, int cx, int cy, int w, int h, uint64_t budget_ns,
                             bool visible)
{
    job_drop();
    int pw = v->pwpx[page], ph = v->phpx[page];
    FPDF_PAGE p = doc_page(v->doc, page);
    if (!p || w <= 0 || h <= 0 || pw <= 0 || ph <= 0)
        return NULL;
    FPDF_BITMAP bmp;
    uint8_t *buf = white_bitmap(w, h, &bmp);
    if (!buf)
        return NULL;
    job = (Job){true, visible, win, v, v->doc, page, pw, ph, v->rotation, cx, cy, w, h, w * 4, v->dark,
                v->doc->slots[page].version, buf, bmp, p};
    pause_at = SDL_GetTicksNS() + budget_ns;
    if (FPDF_RenderPageBitmap_Start(bmp, p, -cx, -cy, pw, ph, v->rotation, RENDER_FLAGS, &pauser) ==
        FPDF_RENDER_TOBECONTINUED)
        return NULL;
    return job_finish();
}

/* Is the job making this part of the page, as it is now? */
static bool job_for(View *v, int page, int cx, int cy, int w, int h)
{
    return job.active && job.v == v && job.page == page && job.pw == v->pwpx[page] && job.ph == v->phpx[page] &&
           job.dark == v->dark && job.rot == v->rotation && job.version == v->doc->slots[page].version &&
           job.cx <= cx && job.cy <= cy && job.cx + job.w >= cx + w && job.cy + job.h >= cy + h;
}

/* While idle: more of the page being rendered. True while there is more. */
bool render_work(void)
{
    if (!job.active)
        return false;
    bool visible = job.visible;
    if (job_continue(SLICE_NS) || !job.active) {
        if (visible)
            need_redraw_all = true; /* the page can be shown now */
        return false;
    }
    return true;
}

bool render_busy(void) { return job.active; }

/* Test mode: every page on screen finished before a screenshot. */
void render_finish(void)
{
    while (job.active)
        job_continue(UINT64_MAX / 4);
}

/* Until the page is rendered: what was drawn of it at another zoom,
 * stretched, or blank paper. */
static void placeholder(Window *win, View *v, int page, float sx, float sy)
{
    int pw = v->pwpx[page], ph = v->phpx[page];
    CacheEntry *best = NULL;
    for (int i = 0; i < CACHE_SLOTS; i++) {
        CacheEntry *e = &v->cache[i];
        if (e->tex && e->page == page && e->dark == v->dark && e->pw > 0 && e->ph > 0 &&
            (!best || (size_t)e->w * e->h > (size_t)best->w * best->h))
            best = e;
    }
    uint32_t paper = v->dark ? theme_of(true)->page_bg : 0xFFFFFF;
    ui_fill(win, sx, sy, (float)pw, (float)ph, paper, 255);
    if (!best)
        return;
    float kx = (float)pw / best->pw, ky = (float)ph / best->ph;
    SDL_FRect dst = {sx + best->x * kx, sy + best->y * ky, best->w * kx, best->h * ky};
    SDL_SetTextureScaleMode(best->tex, SDL_SCALEMODE_LINEAR);
    SDL_RenderTexture(win->ren, best->tex, NULL, &dst);
    SDL_SetTextureScaleMode(best->tex, SDL_SCALEMODE_NEAREST);
}

/* A part of a page on screen: from the cache, or rendered now if that is
 * quick enough; NULL while it is still being rendered. */
static CacheEntry *render_visible(Window *win, View *v, int page, int cx, int cy, int w, int h, int ex, int ey,
                                  int ew, int eh, uint64_t deadline)
{
    uint64_t now = SDL_GetTicksNS();
    uint64_t left = deadline > now ? deadline - now : 0;
    if (job_for(v, page, cx, cy, w, h))
        return left ? job_continue(left) : NULL;
    if (job.active && job.visible && job.v == v && job.page != page)
        return NULL; /* another page on screen first; this one next */
    if (!left)
        return NULL;
    return job_start(win, v, page, ex, ey, ew, eh, left, true); /* drops a prefetch */
}

static bool big_page(View *v, int page)
{
    long pw = v->pwpx[page], ph = v->phpx[page];
    return pw * ph > FULL_PAGE_MAX_PIXELS || pw > FULL_PAGE_MAX_SIDE || ph > FULL_PAGE_MAX_SIDE;
}

/* A soft drop shadow drawn as nine slices of a small texture: gaussian
 * corners, edges stretched along the page, and a solid centre. */
#define SHADOW_RADIUS 14

static SDL_Texture *shadow_texture(Window *win, int r, float strength)
{
    int key = r * 1000 + (int)(strength * 100);
    if (win->shadow && win->shadow_key == key)
        return win->shadow;
    if (win->shadow)
        SDL_DestroyTexture(win->shadow);
    int n = 2 * r + 2;
    uint32_t *px = xmalloc((size_t)n * n * 4);
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            /* distance outside the 2x2 core, per axis */
            float dx = (float)SDL_max(0, SDL_max(r - x, x - (r + 1)));
            float dy = (float)SDL_max(0, SDL_max(r - y, y - (r + 1)));
            float s = 0.45f * r;
            float a = strength * SDL_expf(-(dx * dx + dy * dy) / (s * s));
            px[y * n + x] = (uint32_t)(a * 255 + 0.5f) << 24 | 0xFFFFFF;
        }
    }
    win->shadow = SDL_CreateTexture(win->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, n, n);
    SDL_UpdateTexture(win->shadow, NULL, px, n * 4);
    SDL_SetTextureBlendMode(win->shadow, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(win->shadow, SDL_SCALEMODE_LINEAR);
    win->shadow_key = key;
    free(px);
    return win->shadow;
}

static void draw_shadow(Window *win, View *v, float x, float y, float w, float h)
{
    int r = (int)(SHADOW_RADIUS * win->density);
    SDL_Texture *t = shadow_texture(win, r, v->dark ? 0.6f : 0.3f);
    uint32_t c = theme_of(v->dark)->shadow;
    SDL_SetTextureColorMod(t, c >> 16, (c >> 8) & 0xFF, c & 0xFF);
    float R = (float)r, sx = x - R, sy = y - R + 3 * win->density; /* dropped a little */
    float xs[3] = {sx, sx + R, sx + R + w}, ws[3] = {R, w, R};
    float ys[3] = {sy, sy + R, sy + R + h}, hs[3] = {R, h, R};
    float us[3] = {0, R, R + 2}, uw[3] = {R, 2, R};
    SDL_FRect screen = {0, 0, (float)win->W, (float)win->H};
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            SDL_FRect src = {us[i], us[j], uw[i], uw[j]}, dst = {xs[i], ys[j], ws[i], hs[j]}, vis;
            /* stretched slices are uniform along the stretch, so clipping them to
             * the window is exact; a huge page must not become a huge texture */
            if (i == 1 || j == 1) {
                if (!SDL_GetRectIntersectionFloat(&dst, &screen, &vis))
                    continue;
                if (i == 1)
                    dst.x = vis.x, dst.w = vis.w;
                if (j == 1)
                    dst.y = vis.y, dst.h = vis.h;
            }
            SDL_RenderTexture(win->ren, t, &src, &dst);
        }
}

void view_draw(Window *win, View *v, int top)
{
    int W = win->W, H = win->H;
    double ox = offset_x(v, W);
    uint64_t deadline = SDL_GetTicksNS() + FRAME_NS;
    frame_start = cache_clock; /* textures used from here on are on screen */
    /* shadows first, so one page's shadow never covers its neighbour */
    for (int i = 0; cfg.page_shadow && i < v->doc->npages; i++) {
        double sy = v->py[i] - v->sy + top, sx = v->px[i] + ox;
        if (sy >= H + GAP)
            break;
        if (sy + v->phpx[i] + GAP > top && sx < W + GAP && sx + v->pwpx[i] + GAP > 0)
            draw_shadow(win, v, floorf((float)sx + 0.5f), floorf((float)sy + 0.5f), (float)v->pwpx[i],
                        (float)v->phpx[i]);
    }
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
        if (!e) {
            int ex = cx, ey = cy, ew = cw, eh = ch;
            if (big_page(v, i)) { /* a margin around the viewport, so small scrolls reuse it */
                int mx = W / 2, my = H / 2;
                ex = SDL_max(0, cx - mx), ey = SDL_max(0, cy - my);
                ew = SDL_min(pw, cx + cw + mx) - ex, eh = SDL_min(ph, cy + ch + my) - ey;
            }
            e = render_visible(win, v, i, cx, cy, cw, ch, ex, ey, ew, eh, deadline);
        }
        if (!e) {
            placeholder(win, v, i, floorf((float)sx + 0.5f), floorf((float)sy + 0.5f));
            render_incomplete = true;
            continue;
        }
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
        if (texture_bytes + (size_t)v->pwpx[p] * v->phpx[p] * 4 > TEXTURE_BUDGET * 3 / 4)
            return false; /* memory for what is on screen comes first */
        if (!cache_find(v, p, 0, 0, v->pwpx[p], v->phpx[p])) {
            if (job.active) /* one page at a time: finish that first */
                return render_work();
            job_start(win, v, p, 0, 0, v->pwpx[p], v->phpx[p], SLICE_NS, false);
            return true;
        }
    }
    return false;
}
