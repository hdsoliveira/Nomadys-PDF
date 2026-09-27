/* Images put on a page (":image file", or I / P in the free cursor): a
 * signature, a screenshot, a figure. They become stamp annotations, so
 * they can be moved, resized (object.c) and deleted later, and other
 * readers show them.
 *
 * Size: inside the marked rectangle, or at its natural size; never more
 * than 80% of the page.
 *
 * Resolution: PDFium enlarges images without smoothing (blocky pixels) but
 * reduces them well, so an image is kept with more pixels than the screen
 * shows: small ones (signatures, screenshots) are enlarged with a bicubic
 * filter to IMAGE_MIN_DPI at the placed size; others are kept as they are;
 * only huge ones are reduced, never below IMAGE_KEEP_SIDE pixels. JPEG
 * stays JPEG (copied as is when not resized); PNG, with its transparency,
 * is stored losslessly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_MAX_DIMENSIONS 16384
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#define IMAGE_MIN_DPI 360         /* smooth up to ~250% zoom on a 2x screen */
#define IMAGE_MAX_DPI 600         /* above this a huge image is reduced... */
#define IMAGE_KEEP_SIDE 4096      /* ...but its longer side keeps this many pixels */
#define IMAGE_UP_SIDE 4096        /* enlarging stops here */
#define IMAGE_JPEG_QUALITY 92
#define IMAGE_MAX_PIXELS (120 * 1000 * 1000) /* decoding takes 4 bytes a pixel */
#define IMAGE_MAX_PAGE 0.8f          /* of the page width and height */
#define IMAGE_FILE_MAX (64 << 20)

typedef struct {
    uint8_t *data;
    size_t len, cap;
} Bytes;

static bool read_file(const char *path, Bytes *b, char *err, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, cap, "cannot open %s", path);
        return false;
    }
    b->len = 0;
    b->cap = 1 << 16;
    b->data = xmalloc(b->cap);
    size_t r;
    while ((r = fread(b->data + b->len, 1, b->cap - b->len, f)) > 0) {
        b->len += r;
        if (b->len > IMAGE_FILE_MAX) {
            fclose(f);
            free(b->data);
            snprintf(err, cap, "image too large (over 64 MB)");
            return false;
        }
        if (b->len == b->cap)
            b->data = xrealloc(b->data, b->cap *= 2);
    }
    fclose(f);
    return true;
}

static bool is_jpeg(const uint8_t *d, size_t n) { return n > 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF; }

/* ---------- where it goes ---------- */
static PRect fit_in(PRect box, float ratio)
{
    float bw = box.r - box.l, bh = box.t - box.b, w = bw, h = bw / ratio;
    if (h > bh)
        h = bh, w = bh * ratio;
    float x = box.l + (bw - w) / 2, y = box.b + (bh - h) / 2;
    return (PRect){x, y + h, x + w, y};
}

/* The rectangle for an image of iw x ih pixels: inside the marked area,
 * or with its top-left corner at the free cursor, or centred on the
 * screen; at most 80% of the page either way. */
static bool placement(Window *w, View *v, int iw, int ih, int *page, PRect *out)
{
    Doc *d = v->doc;
    float ratio = (float)iw / (float)ih;
    PRect r;
    bool is_rect = false, at_cursor = false;
    if (w->mode == MODE_AREA && area_target(v, page, &r, &is_rect)) {
        at_cursor = !is_rect;
    } else {
        double px, py;
        if (!screen_to_page(v, w->W / 2.0f, (w->H + top_of(w)) / 2.0f, top_of(w), page, &px, &py)) {
            *page = view_top_page(v);
            px = d->pw[*page] / 2, py = d->ph[*page] / 2;
        }
        r = (PRect){(float)px, (float)py, (float)px, (float)py};
    }
    if (!doc_page_ok(d, *page))
        return false;
    float pw = d->pw[*page], ph = d->ph[*page];
    if (is_rect) {
        *out = fit_in(r, ratio);
    } else {
        float wpt = iw * 0.75f, hpt = ih * 0.75f; /* pixels at 96 dpi */
        if (SDL_min(wpt, hpt) < 12) { /* a tiny picture: big enough to see and grab */
            float up = 12 / SDL_min(wpt, hpt);
            wpt *= up, hpt *= up;
        }
        float s = SDL_min(1.0f, SDL_min(IMAGE_MAX_PAGE * pw / wpt, IMAGE_MAX_PAGE * ph / hpt));
        wpt *= s, hpt *= s;
        wpt = SDL_max(wpt, 6.0f), hpt = SDL_max(hpt, 6.0f); /* a 1-pixel line stays visible */
        float x = at_cursor ? r.l : r.l - wpt / 2, top = at_cursor ? r.t : r.t + hpt / 2;
        x = SDL_clamp(x, 0.0f, SDL_max(0.0f, pw - wpt));
        top = SDL_clamp(top, hpt, ph);
        *out = (PRect){x, top, x + wpt, top - hpt};
    }
    /* a marked rectangle bigger than 80% of the page is the user's choice */
    return out->r - out->l >= 1 && out->t - out->b >= 1;
}

/* ---------- pixels ---------- */
/* ---------- resampling: separable Catmull-Rom (bicubic), alpha premultiplied ---------- */
static float cubic(float x)
{
    x = SDL_fabsf(x);
    if (x < 1)
        return (1.5f * x - 2.5f) * x * x + 1;
    if (x < 2)
        return ((-0.5f * x + 2.5f) * x - 4) * x + 2;
    return 0;
}

typedef struct {
    int *start, *count, max;
    float *w;                /* count[i] weights from w + i * max */
} Taps;

/* Weights mapping src samples to dst ones; a reduction widens the filter
 * so every source pixel counts (no aliasing). */
static Taps make_taps(int src, int dst)
{
    float scale = (float)dst / (float)src, fs = scale < 1 ? scale : 1, support = 2 / fs;
    Taps t = {xmalloc((size_t)dst * sizeof(int)), xmalloc((size_t)dst * sizeof(int)), (int)SDL_ceilf(2 * support) + 2, NULL};
    t.w = xcalloc((size_t)dst * t.max, sizeof(float));
    for (int i = 0; i < dst; i++) {
        float center = (i + 0.5f) / scale - 0.5f;
        int a = (int)SDL_floorf(center - support) + 1, b = (int)SDL_floorf(center + support);
        a = SDL_max(a, 0), b = SDL_min(b, src - 1);
        int n = SDL_min(b - a + 1, t.max);
        float sum = 0, *w = t.w + (size_t)i * t.max;
        for (int k = 0; k < n; k++)
            sum += w[k] = cubic((a + k - center) * fs);
        for (int k = 0; k < n && sum != 0; k++)
            w[k] /= sum;
        t.start[i] = a, t.count[i] = SDL_max(n, 0);
    }
    return t;
}

static void free_taps(Taps *t)
{
    free(t->start);
    free(t->count);
    free(t->w);
}

typedef struct {
    const uint8_t *src;
    int sw, sh, dw, y0, y1;  /* output rows y0..y1-1 */
    const Taps *h, *v;
    uint8_t *dst;
} Band;

/* Output rows of one band. Source rows are filtered horizontally once each
 * and kept in a small ring, so memory stays a few rows wide. */
static int resample_band(void *arg)
{
    Band *b = arg;
    const Taps *h = b->h, *v = b->v;
    int dw = b->dw, ring = v->max;
    float *rows = xmalloc((size_t)ring * dw * 4 * sizeof(float));
    int *row_of = xmalloc((size_t)ring * sizeof(int));
    for (int i = 0; i < ring; i++)
        row_of[i] = -1;
    for (int y = b->y0; y < b->y1; y++) {
        int y0 = v->start[y], n = v->count[y];
        float acc[4];
        for (int k = 0; k < n; k++) { /* horizontally filtered source rows, premultiplied */
            int sy = y0 + k, slot = sy % ring;
            if (row_of[slot] == sy)
                continue;
            row_of[slot] = sy;
            float *out = rows + (size_t)slot * dw * 4;
            const uint8_t *line = b->src + (size_t)sy * b->sw * 4;
            for (int x = 0; x < dw; x++) {
                acc[0] = acc[1] = acc[2] = acc[3] = 0;
                const float *w = h->w + (size_t)x * h->max;
                const uint8_t *p = line + (size_t)h->start[x] * 4;
                for (int j = 0; j < h->count[x]; j++, p += 4) {
                    float a = p[3] * w[j];
                    acc[0] += p[0] * a, acc[1] += p[1] * a, acc[2] += p[2] * a, acc[3] += a;
                }
                memcpy(out + (size_t)x * 4, acc, sizeof acc);
            }
        }
        const float *w = v->w + (size_t)y * v->max;
        uint8_t *q = b->dst + (size_t)y * dw * 4;
        for (int x = 0; x < dw; x++, q += 4) {
            acc[0] = acc[1] = acc[2] = acc[3] = 0;
            for (int k = 0; k < n; k++) {
                const float *p = rows + ((size_t)((y0 + k) % ring) * dw + x) * 4;
                acc[0] += p[0] * w[k], acc[1] += p[1] * w[k], acc[2] += p[2] * w[k], acc[3] += p[3] * w[k];
            }
            float a = SDL_clamp(acc[3], 0.0f, 255.0f);
            for (int c = 0; c < 3; c++) /* back from premultiplied */
                q[c] = (uint8_t)(a > 0.5f ? SDL_clamp(acc[c] / acc[3], 0.0f, 255.0f) + 0.5f : 0);
            q[3] = (uint8_t)(a + 0.5f);
        }
    }
    free(rows);
    free(row_of);
    return 0;
}

/* Half the size, each pixel the alpha-weighted mean of 2 x 2: a cheap and
 * clean first step of a big reduction. */
static uint8_t *halve(const uint8_t *src, int sw, int sh, int *ow, int *oh)
{
    int w = sw / 2, h = sh / 2;
    uint8_t *dst = xmalloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        const uint8_t *r0 = src + (size_t)(2 * y) * sw * 4, *r1 = r0 + (size_t)sw * 4;
        uint8_t *q = dst + (size_t)y * w * 4;
        for (int x = 0; x < w; x++, q += 4) {
            const uint8_t *p[4] = {r0 + 8 * x, r0 + 8 * x + 4, r1 + 8 * x, r1 + 8 * x + 4};
            unsigned a = p[0][3] + p[1][3] + p[2][3] + p[3][3];
            for (int c = 0; c < 3; c++) {
                unsigned sum = p[0][c] * p[0][3] + p[1][c] * p[1][3] + p[2][c] * p[2][3] + p[3][c] * p[3][3];
                q[c] = (uint8_t)(a ? (sum + a / 2) / a : 0);
            }
            q[3] = (uint8_t)((a + 2) / 4);
        }
    }
    *ow = w, *oh = h;
    return dst;
}

/* RGBA sw x sh -> dw x dh: halvings while the reduction is 2x or more,
 * then the bicubic filter, in bands over the CPU cores. */
static uint8_t *resample(const uint8_t *src, int sw, int sh, int dw, int dh)
{
    uint8_t *tmp = NULL;
    while (sw >= 2 * dw && sh >= 2 * dh && sw >= 2 && sh >= 2) {
        uint8_t *next = halve(src, sw, sh, &sw, &sh);
        free(tmp);
        src = tmp = next;
    }
    if (sw == dw && sh == dh) {
        if (tmp)
            return tmp;
        uint8_t *copy = xmalloc((size_t)sw * sh * 4);
        memcpy(copy, src, (size_t)sw * sh * 4);
        return copy;
    }
    Taps h = make_taps(sw, dw), v = make_taps(sh, dh);
    uint8_t *dst = xmalloc((size_t)dw * dh * 4);
    int nb = SDL_clamp(SDL_GetNumLogicalCPUCores(), 1, 16);
    nb = SDL_max(1, SDL_min(nb, dh / 32)); /* small images: one band */
    Band bands[16];
    SDL_Thread *threads[16] = {0};
    for (int i = 0; i < nb; i++) {
        bands[i] = (Band){src, sw, sh, dw, dh * i / nb, dh * (i + 1) / nb, &h, &v, dst};
        if (i > 0)
            threads[i] = SDL_CreateThread(resample_band, "resample", &bands[i]);
        if (i > 0 && !threads[i])
            resample_band(&bands[i]); /* no thread: do it here */
    }
    resample_band(&bands[0]);
    for (int i = 1; i < nb; i++)
        if (threads[i])
            SDL_WaitThread(threads[i], NULL);
    free_taps(&h);
    free_taps(&v);
    free(tmp);
    return dst;
}

/* The pixels to keep for an iw x ih image placed in r (see the top). */
static void target_size(int iw, int ih, PRect r, int *dw, int *dh)
{
    float in_w = (r.r - r.l) / 72.0f, in_h = (r.t - r.b) / 72.0f; /* inches */
    float dpi = SDL_min(iw / in_w, ih / in_h), f = 1;
    int side = SDL_max(iw, ih);
    if (dpi < IMAGE_MIN_DPI) /* enlarge, up to a limit */
        f = SDL_min(IMAGE_MIN_DPI / dpi, (float)IMAGE_UP_SIDE / side);
    else if (side > IMAGE_KEEP_SIDE && dpi > IMAGE_MAX_DPI) /* reduce a huge one, not too much */
        f = SDL_max(IMAGE_MAX_DPI / dpi, (float)IMAGE_KEEP_SIDE / side);
    if (f > 0.97f && f < 1.03f)
        f = 1; /* not worth resampling */
    f = SDL_max(f, 1.0f / 64);
    *dw = SDL_max(1, (int)(iw * f + 0.5f)), *dh = SDL_max(1, (int)(ih * f + 0.5f));
}

static void put_bytes(void *ctx, void *data, int size)
{
    Bytes *b = ctx;
    if (b->len + (size_t)size > b->cap) {
        b->cap = (b->len + (size_t)size) * 2;
        b->data = xrealloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, data, (size_t)size);
    b->len += (size_t)size;
}

typedef struct {
    const uint8_t *data;
    size_t len;
} Mem;

static int get_block(void *param, unsigned long pos, unsigned char *buf, unsigned long size)
{
    const Mem *m = param;
    if (pos + size > m->len)
        return 0;
    memcpy(buf, m->data + pos, size);
    return 1;
}

static bool load_jpeg(FPDF_PAGEOBJECT obj, FPDF_PAGE page, const uint8_t *data, size_t len)
{
    Mem m = {data, len};
    FPDF_FILEACCESS fa = {.m_FileLen = (unsigned long)len, .m_GetBlock = get_block, .m_Param = &m};
    return FPDFImageObj_LoadJpegFileInline(&page, 1, obj, &fa);
}

/* The PDF image: JPEG kept as JPEG (re-encoded only when resized),
 * everything else as lossless pixels with its transparency. */
static FPDF_PAGEOBJECT make_image(Doc *d, FPDF_PAGE page, const uint8_t *data, size_t len, PRect r, char *err,
                                  size_t cap)
{
    int iw, ih, comp;
    if (!stbi_info_from_memory(data, (int)len, &iw, &ih, &comp)) {
        snprintf(err, cap, "not a PNG, JPEG or BMP image");
        return NULL;
    }
    int dw, dh;
    target_size(iw, ih, r, &dw, &dh);
    bool resized = dw != iw || dh != ih;
    FPDF_PAGEOBJECT obj = FPDFPageObj_NewImageObj(d->pdf);
    if (!obj) {
        snprintf(err, cap, "cannot create the image");
        return NULL;
    }
    if (is_jpeg(data, len) && !resized) { /* the original bytes: no loss at all */
        if (load_jpeg(obj, page, data, len))
            return obj;
    }
    uint8_t *px = stbi_load_from_memory(data, (int)len, &iw, &ih, &comp, 4);
    if (!px) {
        snprintf(err, cap, "cannot read the image: %s", stbi_failure_reason());
        FPDFPageObj_Destroy(obj);
        return NULL;
    }
    uint8_t *use = px;
    if (resized)
        use = resample(px, iw, ih, dw, dh);
    bool ok = false;
    if (is_jpeg(data, len)) { /* a photo: stays JPEG */
        Bytes out = {0};
        if (stbi_write_jpg_to_func(put_bytes, &out, dw, dh, 4, use, IMAGE_JPEG_QUALITY))
            ok = load_jpeg(obj, page, out.data, out.len);
        free(out.data);
    }
    if (!ok) {
        bool alpha = false;
        for (size_t i = 3; i < (size_t)dw * dh * 4 && !alpha; i += 4)
            alpha = use[i] < 255;
        FPDF_BITMAP bmp = FPDFBitmap_Create(dw, dh, alpha);
        if (bmp) {
            uint8_t *dst = FPDFBitmap_GetBuffer(bmp);
            int stride = FPDFBitmap_GetStride(bmp);
            for (int y = 0; y < dh; y++)
                for (int x = 0; x < dw; x++) {
                    const uint8_t *s = use + ((size_t)y * dw + x) * 4;
                    uint8_t *q = dst + (size_t)y * stride + x * 4; /* BGRA */
                    q[0] = s[2], q[1] = s[1], q[2] = s[0], q[3] = alpha ? s[3] : 255;
                }
            ok = FPDFImageObj_SetBitmap(&page, 1, obj, bmp);
            FPDFBitmap_Destroy(bmp);
        }
    }
    if (use != px)
        free(use);
    stbi_image_free(px);
    memory_freed = true; /* decoded pixels can be large: give them back */
    if (!ok) {
        snprintf(err, cap, "cannot put the image in the PDF");
        FPDFPageObj_Destroy(obj);
        return NULL;
    }
    return obj;
}

/* Puts an encoded image (PNG, JPEG, BMP) on the page and starts moving /
 * resizing it. */
static void insert(Window *w, const uint8_t *data, size_t len)
{
    View *v = cur_view(w);
    Doc *d = v->doc;
    char err[256];
    int iw, ih, comp, page;
    PRect r;
    if (!stbi_info_from_memory(data, (int)len, &iw, &ih, &comp) || iw <= 0 || ih <= 0) {
        ui_message_long(w, 5000, "not a PNG, JPEG or BMP image");
        return;
    }
    if ((long long)iw * ih > IMAGE_MAX_PIXELS) {
        ui_message_long(w, 6000, "image too large: %d \u00d7 %d (%.0f megapixels, at most %d)", iw, ih,
                        (double)iw * ih / 1e6, IMAGE_MAX_PIXELS / 1000000);
        return;
    }
    if (!placement(w, v, iw, ih, &page, &r)) {
        ui_message(w, "no page to put the image on");
        return;
    }
    FPDF_PAGE p = doc_page(d, page);
    FPDF_PAGEOBJECT obj = p ? make_image(d, p, data, len, r, err, sizeof err) : NULL;
    if (!obj) {
        ui_message_long(w, 5000, "%s", p ? err : "cannot open the page");
        return;
    }
    int annot = note_create_image(d, page, r, obj);
    if (annot < 0) {
        ui_message_long(w, 5000, "cannot add the image to the page");
        return;
    }
    if (w->mode == MODE_AREA)
        v->area_selecting = false;
    object_begin(w, v, page, annot, (float)iw / (float)ih);
}

void image_insert_file(Window *w, const char *path)
{
    Bytes b;
    char err[512];
    if (!read_file(path, &b, err, sizeof err)) {
        ui_message_long(w, 5000, "%s", err);
        return;
    }
    insert(w, b.data, b.len);
    free(b.data);
}

void image_paste(Window *w)
{
    static const char *types[] = {"image/png", "image/jpeg", "image/bmp"};
    for (size_t i = 0; i < SDL_arraysize(types); i++) {
        if (!SDL_HasClipboardData(types[i]))
            continue;
        size_t n = 0;
        void *data = SDL_GetClipboardData(types[i], &n);
        if (data && n > 0 && n <= IMAGE_FILE_MAX) {
            insert(w, data, n);
            SDL_free(data);
            return;
        }
        SDL_free(data);
    }
    ui_message_long(w, 5000, "no image in the clipboard (copy one, or take a screenshot to the clipboard)");
}
