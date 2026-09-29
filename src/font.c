/* UI text: a system font found by fontconfig, rasterised by FreeType.
 * Each window owns a Font with a glyph texture cache for its renderer. */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "nomadys.h"

typedef struct {
    unsigned cp;             /* 0 = empty slot */
    SDL_Texture *tex;
    int w, h, left, top, advance;
} Glyph;

struct Font {
    SDL_Renderer *ren;
    FT_Face face;
    int ascent, height;
    Glyph *glyphs;           /* open-addressing hash on the code point */
    int cap, n;
};

static FT_Library ft;

/* File of the best match for a fontconfig pattern such as "JetBrains Mono". */
/* The config's font was not found: fontconfig used this one instead (""). */
char font_fallback[160];

/* True if one of the match's family names is the one asked for (the part
 * before any ":style=..."), ignoring case. */
static bool same_family(FcPattern *match, const char *family)
{
    size_t n = strcspn(family, ":");
    FcChar8 *name;
    for (int i = 0; FcPatternGetString(match, FC_FAMILY, i, &name) == FcResultMatch; i++)
        if (strlen((const char *)name) == n && !strncasecmp((const char *)name, family, n))
            return true;
    return false;
}

static char *find_font(const char *family, int *index)
{
    FcPattern *pat = FcNameParse((const FcChar8 *)family);
    if (!pat)
        return NULL;
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    FcPattern *match = FcFontMatch(NULL, pat, &res);
    FcPatternDestroy(pat);
    if (!match)
        return NULL;
    FcChar8 *file = NULL, *got = NULL;
    char *out = NULL;
    font_fallback[0] = 0;
    if (strcmp(family, "monospace") && !same_family(match, family) &&
        FcPatternGetString(match, FC_FAMILY, 0, &got) == FcResultMatch)
        snprintf(font_fallback, sizeof font_fallback, "font \"%.60s\" not found, using %.60s", family, (const char *)got);
    if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch)
        out = xstrdup((const char *)file);
    if (FcPatternGetInteger(match, FC_INDEX, 0, index) != FcResultMatch)
        *index = 0;
    FcPatternDestroy(match);
    return out;
}

Font *font_open(SDL_Renderer *ren, const char *family, double points, float density)
{
    if (!ft && FT_Init_FreeType(&ft))
        return NULL;
    int index = 0;
    char *file = find_font(family, &index);
    if (!file)
        file = find_font("monospace", &index);
    Font *f = xcalloc(1, sizeof *f);
    f->ren = ren;
    if (!file || FT_New_Face(ft, file, index, &f->face)) {
        free(file);
        free(f);
        return NULL;
    }
    free(file);
    /* points at 96 dpi, times the display density */
    int px = (int)(points * 96.0 / 72.0 * density + 0.5);
    FT_Set_Pixel_Sizes(f->face, 0, (FT_UInt)SDL_max(6, px));
    f->ascent = (int)(f->face->size->metrics.ascender >> 6);
    f->height = (int)((f->face->size->metrics.ascender - f->face->size->metrics.descender) >> 6);
    f->cap = 256;
    f->glyphs = xcalloc(f->cap, sizeof *f->glyphs);
    return f;
}

void font_close(Font *f)
{
    if (!f)
        return;
    for (int i = 0; i < f->cap; i++)
        if (f->glyphs[i].tex)
            SDL_DestroyTexture(f->glyphs[i].tex);
    free(f->glyphs);
    FT_Done_Face(f->face);
    free(f);
}

int font_height(Font *f) { return f ? f->height : 16; }

static Glyph *slot(Font *f, unsigned cp)
{
    unsigned i = (cp * 2654435761u) & (unsigned)(f->cap - 1);
    while (f->glyphs[i].cp && f->glyphs[i].cp != cp)
        i = (i + 1) & (unsigned)(f->cap - 1);
    return &f->glyphs[i];
}

static void grow(Font *f)
{
    Glyph *old = f->glyphs;
    int oldcap = f->cap;
    f->cap *= 2;
    f->glyphs = xcalloc(f->cap, sizeof *f->glyphs);
    for (int i = 0; i < oldcap; i++)
        if (old[i].cp)
            *slot(f, old[i].cp) = old[i];
    free(old);
}

static Glyph *glyph(Font *f, unsigned cp)
{
    Glyph *g = slot(f, cp);
    if (g->cp)
        return g;
    if (f->n * 2 >= f->cap) {
        grow(f);
        g = slot(f, cp);
    }
    f->n++;
    g->cp = cp;
    if (FT_Load_Char(f->face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT))
        return g;
    FT_GlyphSlot s = f->face->glyph;
    g->advance = (int)(s->advance.x >> 6);
    g->left = s->bitmap_left;
    g->top = s->bitmap_top;
    g->w = (int)s->bitmap.width;
    g->h = (int)s->bitmap.rows;
    if (g->w > 0 && g->h > 0 && s->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
        uint32_t *px = xmalloc((size_t)g->w * g->h * 4);
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++)
                px[y * g->w + x] = ((uint32_t)s->bitmap.buffer[y * s->bitmap.pitch + x] << 24) | 0xFFFFFF;
        g->tex = SDL_CreateTexture(f->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, g->w, g->h);
        if (g->tex) {
            SDL_UpdateTexture(g->tex, NULL, px, g->w * 4);
            SDL_SetTextureBlendMode(g->tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(g->tex, SDL_SCALEMODE_NEAREST);
        }
        free(px);
    }
    return g;
}

int font_ascent(Font *f) { return f ? f->ascent : 0; }

/* Pixels per em: the size a 1em-high thing (a formula) should match. */
float font_em(Font *f) { return f ? (float)f->face->size->metrics.y_ppem : 12.0f; }

int font_width(Font *f, const char *s)
{
    int w = 0;
    while (f && *s)
        w += glyph(f, utf8_next(&s))->advance;
    return w;
}

void font_draw(Font *f, float x, float y, const char *s, uint32_t rgb)
{
    if (!f)
        return;
    int pen = (int)(x + 0.5f), base = (int)(y + 0.5f) + f->ascent;
    while (*s) {
        Glyph *g = glyph(f, utf8_next(&s));
        if (g->tex) {
            SDL_SetTextureColorMod(g->tex, rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF);
            SDL_FRect dst = {(float)(pen + g->left), (float)(base - g->top), (float)g->w, (float)g->h};
            SDL_RenderTexture(f->ren, g->tex, NULL, &dst);
        }
        pen += g->advance;
    }
}

/* Shorten s with an ellipsis until it fits in maxw pixels; s needs 3 spare bytes. */
void font_fit(Font *f, char *s, int maxw)
{
    size_t n = strlen(s);
    while (n > 0 && font_width(f, s) > maxw) {
        do
            n--;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80);
        strcpy(s + n, "\u2026");
    }
}

/* Releases FreeType at exit, and fontconfig's caches in a sanitizer build
 * (so its leak report stays clean). Not otherwise: under Wayland, SDL's
 * window decorations (libdecor, GTK) still use fontconfig then, and
 * fontconfig 2.13 / 2.14 (Ubuntu 22.04, Debian 12) aborts on FcFini. */
void font_shutdown(void)
{
    if (ft)
        FT_Done_FreeType(ft);
    ft = NULL;
#if defined(__SANITIZE_ADDRESS__)
    FcFini();
#endif
}
