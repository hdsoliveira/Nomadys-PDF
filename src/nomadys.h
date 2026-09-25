/* Nomadys-PDF: minimal, fast, keyboard-driven PDF reader. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "fpdfview.h"
#include "fpdf_annot.h"
#include "fpdf_edit.h"
#include "fpdf_formfill.h"
#include "fpdf_save.h"
#include "fpdf_text.h"

#define MAX_TIMERS 16
#define MAX_TABS 32
#define MAX_WINDOWS 16
#define MAX_DOCS 64
#define CACHE_SLOTS 16
#define LOADED_PAGE_CAP 48
#define CMD_MAX 1024

typedef struct { float l, t, r, b; } PRect; /* page space: y up, t > b */

/* ---------- configuration ---------- */
typedef struct {
    bool dark;               /* initial theme */
    bool autosave;           /* save automatically after edits */
    uint32_t highlight;      /* 0xRRGGBB */
    double zoom;             /* 0 = fit width, otherwise pixels per point / density */
    uint8_t dark_lo, dark_hi;/* dark-mode luminance range */
    uint32_t bg_light, bg_dark;
    int autosave_delay_ms;
} Config;

extern Config cfg;
void config_load(void);

/* ---------- document ---------- */
typedef struct Doc Doc;

typedef struct {
    int idx;                 /* annotation index on the page */
    PRect r;
} Widget;

typedef struct {
    FPDF_PAGE page;
    FPDF_TEXTPAGE text;
    unsigned version;        /* bumped whenever the page appearance changes */
    uint64_t used;
    PRect *images;           /* regions kept untouched in dark mode */
    int nimages;
    bool images_ready;
    Widget *widgets;
    int nwidgets;
    bool widgets_ready;
    /* caret support */
    int nchars;
    PRect *boxes;
    int *line;               /* line number per char, -1 when not selectable */
    int nlines;
    bool chars_ready;
} PageSlot;

typedef struct {
    int id;
    int interval;
    uint64_t next;
    TimerCallback fn;
} FormTimer;

typedef struct {
    FPDF_FORMFILLINFO info;  /* must be first */
    Doc *doc;
} FormInfo;

struct Doc {
    char *path;
    FPDF_DOCUMENT pdf;
    FPDF_FORMHANDLE form;
    FormInfo ffi;
    int npages;
    float *pw, *ph;          /* page size in points */
    PageSlot *slots;
    int nloaded;
    int refs;
    bool modified;
    uint64_t changed_at;
    FormTimer timers[MAX_TIMERS];
    int next_timer_id;
    /* focused form field */
    int focus_page;          /* -1 = none */
    int focus_widget;        /* index into slots[focus_page].widgets */
};

Doc *doc_open(const char *path, char *err, size_t errlen);
void doc_ref(Doc *d);
void doc_unref(Doc *d);
FPDF_PAGE doc_page(Doc *d, int i);
FPDF_TEXTPAGE doc_text(Doc *d, int i);
void doc_touch_pages(Doc *d);
void doc_mark_changed(Doc *d, int page);
bool doc_save(Doc *d, char *err, size_t errlen);
void doc_page_images(Doc *d, int i);
void doc_page_widgets(Doc *d, int i);
void doc_page_chars(Doc *d, int i);
int doc_run_timers(Doc *d, uint64_t now);    /* returns ms until next timer, -1 none */
bool doc_focus_widget(Doc *d, int page, int w);
void doc_kill_focus(Doc *d);
bool doc_has_forms(Doc *d);
extern Doc *docs[MAX_DOCS];
extern int ndocs;
extern bool need_redraw_all;

/* ---------- views / tabs ---------- */
typedef struct {
    int page;
    int x, y, w, h;          /* clip in page pixel space */
    int pw, ph;              /* full page pixel size */
    bool dark;
    unsigned version;
    SDL_Texture *tex;
    uint64_t used;
} CacheEntry;

typedef enum { MODE_NORMAL, MODE_FORM, MODE_CARET, MODE_COMMAND, MODE_SEARCH } Mode;

typedef struct {
    int page, ch;            /* caret position */
} TextPos;

typedef struct {
    Doc *doc;
    double zoom;             /* pixels per point (device pixels) */
    bool fit_width;
    bool dual;
    bool dark;
    double sx, sy;           /* scroll offsets in pixels */
    /* layout */
    int lay_w, lay_h;
    double lay_zoom;
    bool lay_dual;
    int *px, *py;            /* page position in content space */
    int *pwpx, *phpx;        /* page pixel sizes */
    int content_w, content_h;
    CacheEntry cache[CACHE_SLOTS];
    /* caret / selection */
    TextPos caret;
    bool selecting;
    TextPos anchor;
    double caret_x;          /* preferred x for vertical motion */
    /* search */
    char query[CMD_MAX];
    int match_page, match_start, match_len;
} View;

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *font;
    View *tabs[MAX_TABS];
    int ntabs, cur;
    bool show_tabs;
    uint64_t tabs_flash_until;
    Mode mode;
    char cmd[CMD_MAX];
    int cmdlen;
    char msg[256];
    uint64_t msg_until;
    int count;
    bool pending_g;
    uint64_t quit_warned;    /* close confirmation valid until this time */
    bool redraw;
    int W, H;                /* pixel size */
    float density;
} Window;

extern Window *windows[MAX_WINDOWS];
extern int nwindows;

View *view_new(Doc *d);
View *view_clone(View *v);
void view_free(View *v);
void view_layout(View *v, int W, int H, int top);
void view_clamp(View *v, int W, int H, int top);
void view_draw(Window *w, View *v, int top);
bool view_prefetch(Window *w, View *v, int top);
void view_invalidate_cache(View *v);
int view_top_page(View *v);
void view_goto_page(View *v, int page);
void view_show_rect(View *v, int page, PRect r, int W, int H, int top);
void page_to_screen(View *v, int page, float x, float y, float *sx, float *sy, int top);
bool screen_to_page(View *v, float sx, float sy, int top, int *page, double *x, double *y);

/* ---------- text: caret, selection, search, highlights ---------- */
void caret_enter(Window *w, View *v);
bool caret_key(Window *w, View *v, SDL_Keycode key, SDL_Keymod mod, const char *text);
void caret_draw(Window *w, View *v, int top, bool blink_on);
bool search_run(Window *w, View *v, int dir, bool from_current);
void search_draw(Window *w, View *v, int top);

/* ---------- ui ---------- */
void ui_init(Window *w);
void ui_text(Window *w, float x, float y, const char *s, uint32_t rgb, int scale);
int ui_text_width(const char *s, int scale);
void ui_fill(Window *w, float x, float y, float wd, float ht, uint32_t rgb, uint8_t a);
void ui_message(Window *w, const char *fmt, ...);
int ui_tabbar_height(Window *w);
void ui_draw_overlays(Window *w, uint64_t now);

/* ---------- utilities ---------- */
int utf8_to_utf16(const char *s, unsigned short *out, int cap);
char *utf16_to_utf8(const unsigned short *s, int n);
