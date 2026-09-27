/* Images put on a page (":image file", or I / P in the free cursor): a
 * signature, a screenshot, a figure. They become stamp annotations, so
 * they can be moved, resized (object.c) and deleted later, and other
 * readers show them.
 *
 * Size: inside the marked rectangle, or at its natural size; never more
 * than 80% of the page. Resolution: at most IMAGE_DPI at the placed size,
 * so a phone photo does not make the PDF huge. JPEG stays JPEG; PNG
 * (screenshots, signatures with transparency) is stored losslessly. */
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

#define IMAGE_DPI 250
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
        float s = SDL_min(1.0f, SDL_min(IMAGE_MAX_PAGE * pw / wpt, IMAGE_MAX_PAGE * ph / hpt));
        wpt *= s, hpt *= s;
        float x = at_cursor ? r.l : r.l - wpt / 2, top = at_cursor ? r.t : r.t + hpt / 2;
        x = SDL_clamp(x, 0.0f, SDL_max(0.0f, pw - wpt));
        top = SDL_clamp(top, hpt, ph);
        *out = (PRect){x, top, x + wpt, top - hpt};
    }
    /* a marked rectangle bigger than 80% of the page is the user's choice */
    return out->r - out->l >= 1 && out->t - out->b >= 1;
}

/* ---------- pixels ---------- */
/* Box-filter downscale of RGBA pixels. */
static uint8_t *downscale(const uint8_t *src, int sw, int sh, int dw, int dh)
{
    uint8_t *dst = xmalloc((size_t)dw * dh * 4);
    for (int y = 0; y < dh; y++) {
        int y0 = (int)((long long)y * sh / dh), y1 = SDL_max(y0 + 1, (int)((long long)(y + 1) * sh / dh));
        for (int x = 0; x < dw; x++) {
            int x0 = (int)((long long)x * sw / dw), x1 = SDL_max(x0 + 1, (int)((long long)(x + 1) * sw / dw));
            unsigned long sum[4] = {0}, n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    const uint8_t *p = src + ((size_t)yy * sw + xx) * 4;
                    unsigned a = p[3];
                    sum[0] += p[0] * a, sum[1] += p[1] * a, sum[2] += p[2] * a, sum[3] += a; /* weighted by alpha */
                    n++;
                }
            uint8_t *q = dst + ((size_t)y * dw + x) * 4;
            for (int c = 0; c < 3; c++)
                q[c] = (uint8_t)(sum[3] ? sum[c] / sum[3] : 0);
            q[3] = (uint8_t)(sum[3] / n);
        }
    }
    return dst;
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

/* The PDF image: JPEG kept as JPEG (re-encoded only when made smaller),
 * everything else as lossless pixels with its transparency. */
static FPDF_PAGEOBJECT make_image(Doc *d, FPDF_PAGE page, const uint8_t *data, size_t len, PRect r, char *err,
                                  size_t cap)
{
    int iw, ih, comp;
    if (!stbi_info_from_memory(data, (int)len, &iw, &ih, &comp)) {
        snprintf(err, cap, "not a PNG, JPEG or BMP image");
        return NULL;
    }
    /* pixels needed at the placed size */
    int tw = SDL_max(1, (int)((r.r - r.l) / 72.0f * IMAGE_DPI + 0.5f));
    int th = SDL_max(1, (int)((r.t - r.b) / 72.0f * IMAGE_DPI + 0.5f));
    bool smaller = iw > tw || ih > th;
    int dw = smaller ? SDL_min(iw, tw) : iw, dh = smaller ? SDL_min(ih, th) : ih;
    FPDF_PAGEOBJECT obj = FPDFPageObj_NewImageObj(d->pdf);
    if (!obj) {
        snprintf(err, cap, "cannot create the image");
        return NULL;
    }
    if (is_jpeg(data, len) && !smaller) {
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
    if (smaller)
        use = downscale(px, iw, ih, dw, dh);
    bool ok = false;
    if (is_jpeg(data, len)) { /* a photo: stays JPEG */
        Bytes out = {0};
        if (stbi_write_jpg_to_func(put_bytes, &out, dw, dh, 4, use, 90))
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
