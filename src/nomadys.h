/* Nomadys-PDF: minimal, fast, keyboard-driven PDF reader.
 *
 * Module map (one feature per file):
 *   main.c    windows, tabs, event loop          input.c   key tokens, keymaps, actions
 *   config.c  config file and colour themes      font.c    UI text via fontconfig + FreeType
 *   doc.c     PDFium document, pages, saving     render.c  layout, page rendering, dark mode
 *   forms.c   form field navigation and typing   caret.c   text caret and free (area) cursor
 *   annot.c   highlights and comments            marks.c   marks, jump list, last position
 *   search.c  text search                        panel.c   list of marks, notes and contents
 *   ui.c      tab bar, prompt, messages, drawing helpers
 */
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
#define CMD_MAX 4096
#define NMARKS 26
#define JUMPS_MAX 100

typedef struct { float l, t, r, b; } PRect; /* page space: y up, t > b */

typedef struct Window Window;

/* A reading position independent of zoom: page and fraction of its height. */
typedef struct { int page; float y; } Pos;

/* ---------- configuration and themes (config.c) ---------- */
typedef struct {
    uint32_t background;                 /* around the pages */
    uint32_t tab_bg, tab_fg, tab_active_bg, tab_active_fg, tab_accent;
    uint32_t bar_bg, bar_fg, bar_cursor; /* ':' command line and '/' search */
    uint32_t message_bg, message_fg;
    uint32_t panel_bg, panel_fg, panel_selected_bg, panel_selected_fg, panel_header;
    uint32_t comment_bg, comment_fg, comment_marker;
    uint32_t selection, search_match, caret, form_focus;
    uint32_t mode_normal, mode_cursor, mode_visual, mode_area, mode_insert, mode_command; /* mode badge */
    uint32_t page_bg, page_fg;           /* dark mode: page paper and ink colours */
    uint32_t shadow;                     /* soft shadow around pages */
} Theme;

typedef struct {
    bool dark;               /* start in dark mode */
    bool autosave;
    int autosave_delay_ms;
    bool subpixel_text;
    bool show_tabs;
    bool page_shadow;        /* draw a soft shadow around pages */
    bool mode_indicator;     /* small coloured badge with the current mode */
    int dark_images;         /* DARK_IMAGES_*: how dark mode treats images */
    bool restore_position;   /* reopen a file where it was left */
    uint32_t highlights[5];  /* colours of new highlights for C V B N M, 0xRRGGBB */
    bool area_frame;         /* areas: frame around instead of a highlight */
    double zoom;             /* 0 = fit width, otherwise 1.0 = 100 % */
    char font[128];          /* UI font family (fontconfig pattern) */
    double font_size;        /* UI font size in points */
    char leader[8];          /* leader key token, e.g. "\\" */
    char synctex_editor[1024]; /* inverse search command: %l line, %c column, %f file */
    Theme light, dark_theme;
} Config;

enum { DARK_IMAGES_SMART, DARK_IMAGES_KEEP, DARK_IMAGES_INVERT };
extern Config cfg;
void config_load(void);
const Theme *theme_of(bool dark);
const char *leader_label(void);

/* ---------- document (doc.c) ---------- */
typedef struct Doc Doc;

typedef struct {
    int idx;                 /* annotation index on the page */
    PRect r;
} Widget;

typedef struct {
    int idx;                 /* annotation index on the page */
    int subtype;
    PRect r;
    PRect *quads;            /* highlight areas, or just the rect */
    int nquads;
    bool has_comment;
    uint32_t color;          /* 0xRRGGBB, for the panel */
} Note;

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
    Note *notes;             /* markup annotations (highlights, comments) */
    int nnotes;
    unsigned notes_version;  /* page version the list was built for, +1 */
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

typedef struct {
    bool set;
    Pos pos;
    char name[96];
} Mark;

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
    bool save_failed;        /* autosave waits for the next change to retry */
    uint64_t changed_at;
    FormTimer timers[MAX_TIMERS];
    int next_timer_id;
    int focus_page;          /* focused form field, -1 = none */
    int focus_widget;        /* index into slots[focus_page].widgets */
    /* marks.c */
    char *marks_path;
    Mark marks[NMARKS];
    Pos last;                /* last reading position, page -1 = none */
    int notes_scanned;       /* pages whose notes were listed in the background */
    /* reloading after an external change (ipc.c watches the directory) */
    int watch_wd;
    uint64_t reload_at;      /* 0 = nothing pending */
    int reload_tries;
    long long saved_mtime_ns; /* our own last save, not an external change */
};

/* Every page accessor checks this: a reload can shrink the document under
 * a pending action that still holds an old page number. */
static inline bool doc_page_ok(const Doc *d, int i) { return i >= 0 && i < d->npages; }
Doc *doc_open(const char *path, char *err, size_t errlen);
void doc_ref(Doc *d);
void doc_unref(Doc *d);
FPDF_PAGE doc_page(Doc *d, int i);
FPDF_TEXTPAGE doc_text(Doc *d, int i);
void doc_mark_changed(Doc *d, int page);
bool doc_save(Doc *d, char *err, size_t errlen);
void doc_page_images(Doc *d, int i);
void doc_page_widgets(Doc *d, int i);
void doc_page_chars(Doc *d, int i);
int doc_run_timers(Doc *d, uint64_t now);    /* returns ms until next timer, -1 none */
bool doc_focus_widget(Doc *d, int page, int w);
void doc_kill_focus(Doc *d);
bool doc_has_forms(Doc *d);
bool doc_reload(Doc *d);                                  /* false while the file is incomplete */
long long file_mtime_ns(const char *path);
extern Doc *docs[MAX_DOCS];
extern int ndocs;
extern bool need_redraw_all;

/* ---------- annotations (annot.c) ---------- */
void notes_page(Doc *d, int page);                       /* fills slots[page].notes */
bool notes_scan(Doc *d, int pages);                      /* background listing; true if more remain */
int note_at(Doc *d, int page, float x, float y);         /* index into notes, or -1 */
bool note_create(Doc *d, int page, const PRect *quads, int n, const char *comment, uint32_t color);
bool note_create_area(Doc *d, int page, PRect r, const char *comment, uint32_t color);
char *note_comment(Doc *d, int page, int note);          /* malloc'd UTF-8, or NULL */
bool note_set_comment(Doc *d, int page, int note, const char *comment);
bool note_delete(Doc *d, int page, int note);

/* ---------- views / tabs (render.c) ---------- */
typedef struct {
    int page;
    int x, y, w, h;          /* clip in page pixel space */
    int pw, ph;              /* full page pixel size */
    bool dark;
    unsigned version;
    SDL_Texture *tex;
    uint64_t used;
} CacheEntry;

typedef enum { MODE_NORMAL, MODE_FORM, MODE_CARET, MODE_AREA, MODE_PANEL, MODE_PROMPT, MODE_HELP,
               MODE_HINTS, MODE_PREVIEW } Mode;

typedef struct { int page, ch; } TextPos;

typedef struct {
    Doc *doc;
    double zoom;             /* pixels per point (device pixels) */
    bool fit_width;
    bool dual;
    bool dark;
    int rotation;            /* quarter turns clockwise, 0-3 (view only) */
    double sx, sy;           /* scroll offsets in pixels */
    Pos pending_pos;         /* applied at the first layout, page -1 = none */
    /* layout */
    int lay_w;
    double lay_zoom;
    bool lay_dual;
    int *px, *py;            /* page position in content space */
    int *pwpx, *phpx;        /* page pixel sizes */
    int content_w, content_h;
    CacheEntry cache[CACHE_SLOTS];
    /* caret.c: text caret, free cursor and selections */
    TextPos caret;
    bool selecting;
    TextPos anchor;
    double caret_x;          /* preferred x for vertical motion */
    int area_page;
    float area_x, area_y, area_ax, area_ay;
    bool area_selecting;
    uint32_t mark_color;     /* colour for the highlight waiting on its comment */
    /* marks.c: jump list */
    Pos jumps[JUMPS_MAX];
    int njumps, jump_at;
    Pos previous;            /* for "mm": position before the last jump */
    /* synctex.c: spot shown briefly after a forward search */
    int flash_page;
    PRect flash_rect;
    uint64_t flash_until;
    /* search.c */
    char query[CMD_MAX];
    int match_page, match_start, match_len;
    bool query_absent;       /* the whole document was searched in vain */
} View;

View *view_new(Doc *d);
View *view_clone(View *v);
void view_free(View *v);
void view_layout(View *v, int W, int H, int top);
void view_clamp(View *v, int W, int H, int top);
void view_draw(Window *w, View *v, int top);
bool view_prefetch(Window *w, View *v, int top);
void view_invalidate_cache(View *v);
SDL_Texture *view_render_area(Window *w, View *v, int page, int pw, int ph, int rot, int cx, int cy, int cw, int ch);
void view_doc_reloaded(View *v);                         /* page count/sizes changed */
void view_rotate(View *v, int quarter_turns);
float view_page_w(View *v, int page);                    /* points, after rotation */
float view_page_h(View *v, int page);
int view_top_page(View *v);
void view_goto_page(View *v, int page);
Pos view_pos(View *v);
void view_set_pos(View *v, Pos p);
void view_show_rect(View *v, int page, PRect r, int W, int H, int top);
void page_to_screen(View *v, int page, float x, float y, float *sx, float *sy, int top);
bool screen_to_page(View *v, float sx, float sy, int top, int *page, double *x, double *y);

/* ---------- windows ---------- */
typedef struct Font Font;
typedef void (*PromptFn)(Window *w, const char *text);

typedef enum { PANEL_OPEN_TABS, PANEL_CONTENTS, PANEL_MARKS, PANEL_NOTES, PANEL_NTABS } PanelTab; /* tab order */

typedef struct {
    char text[256];          /* main line */
    char sub[256];           /* second, dimmer line (highlighted text), or "" */
    int depth;               /* contents: nesting level */
    Pos pos;                 /* where Enter goes; page -1 = nowhere */
    int page, note, mark, tab;
    uint32_t color;          /* note colour */
} PanelItem;

/* links.c: a link on screen, labelled for 'f' / 'F' */
typedef struct {
    int page;
    PRect r;
    bool internal;           /* target is in this document */
    Pos target;
    char uri[1024];          /* external target */
    char label[4];
} Link;

struct Window {
    SDL_Window *win;
    SDL_Renderer *ren;
    Font *font;
    SDL_Texture *shadow;     /* 9-slice drop shadow (render.c) */
    int shadow_key;
    View *tabs[MAX_TABS];
    int ntabs, cur;
    bool show_tabs;
    uint64_t tabs_flash_until;
    Mode mode, prompt_return;
    /* prompt line (commands, search, comments, names) */
    char prompt_label[64];
    char cmd[CMD_MAX];
    int cmdlen;
    PromptFn prompt_done;
    int prompt_arg;
    /* comments are edited in a box next to the highlight (comment.c) */
    bool prompt_box;
    int cmdpos;              /* cursor, byte offset into cmd */
    int box_page;            /* -1: centred */
    PRect box_anchor;
    float box_maxw, box_scroll;
    uint64_t locate_at;      /* when the text or free cursor appeared (caret.c) */
    char **comp;             /* completion candidates (commands.c) */
    int ncomp, comp_at, comp_start;
    /* key sequences */
    char pending[64];
    int count;
    /* messages */
    char msg[512];
    uint64_t msg_until;
    uint64_t quit_warned;    /* close confirmation valid until this time */
    /* help screen */
    Mode help_return;
    int help_tab, help_sel, help_top; /* tab, selected row, first row shown */
    char help_filter[64];    /* '/' in the help: rows matching this, from every tab */
    /* panel */
    PanelItem *items;        /* entries of the current panel tab */
    int nitems, sel, first;
    PanelTab panel_tab;
    int panel_counts[PANEL_NTABS];
    /* link hints and reference preview (links.c) */
    Link *links;
    int nlinks;
    char hint_typed[4];
    bool hint_preview;
    Mode hint_return;
    struct {
        Link link;
        int page, th;
        float w, h, top;     /* box size in pixels; top of the view in points */
        bool at_top;
        SDL_Texture *tex;
    } preview;
    bool redraw;
    int W, H;                /* pixel size */
    float density;
};

extern Window *windows[MAX_WINDOWS];
extern int nwindows;
View *cur_view(Window *w);
int top_of(Window *w);
const Theme *wtheme(Window *w);
void window_new_from(Window *w);
bool open_tab(Window *w, const char *path);
void close_tab(Window *w, int i);
void close_window(Window *w);
bool confirm_close(Window *w, View *only);
void save(Window *w, View *v);
void update_title(Window *w);

/* ---------- input (input.c) ---------- */
typedef struct {
    char keys[64];
    const char *text;
    bool header, group;      /* a tab / a titled group of keys */
    bool command;            /* ":name args": one key, arguments dimmed */
} HelpLine;
int help_lines(HelpLine **out);                          /* generated from the keymaps */
void input_token(Window *w, const char *tok);            /* "<Esc>", "<C-j>", "a", ... */
bool input_key_event(Window *w, const SDL_KeyboardEvent *e);
void input_text_event(Window *w, const char *text);
int input_wait_ms(uint64_t now);                           /* pending ambiguous key, or -1 */
void input_expire(uint64_t now);
void prompt_open(Window *w, const char *label, const char *initial, PromptFn done, int arg);
bool prompt_uses_document(Window *w);
const char *prompt_kind(Window *w);                     /* COMMAND, SEARCH, COMMENT, EDIT */
void scroll(Window *w, double dx, double dy);
void goto_page(Window *w, View *v, int page);            /* a jump: Ctrl-o returns */

/* ---------- ':' commands (commands.c) ---------- */
void command_run(Window *w, const char *line);
void command_complete(Window *w, int dir);               /* Tab (+1) / Shift-Tab (-1) */
void command_complete_reset(Window *w);
int command_count(void);
void command_describe(int i, char *keys, size_t cap, const char **text);
const char *command_help_for(const char *name);

/* ---------- forms (forms.c) ---------- */
void form_step(Window *w, int dir);
void form_leave(Window *w);
void form_special(Window *w, const char *tok);
void form_text(Window *w, const char *text);
void form_click(Window *w, float x, float y, bool down);
void form_draw_focus(Window *w, View *v, int top);

/* ---------- caret and free cursor (caret.c) ---------- */
bool caret_enter(Window *w, View *v);
void caret_move(Window *w, View *v, char motion, int count);
void caret_toggle_select(Window *w, View *v);
void caret_mark(Window *w, View *v, bool comment, uint32_t color); /* highlight selection */
void caret_copy(Window *w, View *v);
void area_enter(Window *w, View *v);
void area_move(Window *w, View *v, float dx, float dy);
void area_toggle_select(Window *w, View *v);
void area_mark(Window *w, View *v, bool comment, uint32_t color);
bool cursor_note(Window *w, View *v, int *page, int *note); /* note under the cursor */
void cursor_draw(Window *w, View *v, int top);
#define LOCATE_MS 650    /* rings closing on a cursor when it appears */
bool cursor_locating(Window *w, uint64_t now);

/* ---------- marks and jumps (marks.c) ---------- */
void marks_load(Doc *d);
void marks_save(Doc *d);
void mark_set(Window *w, View *v, int m);
void mark_jump(Window *w, View *v, int m);
void jump_to(Window *w, View *v, Pos p);                 /* records the jump */
void jump_back(Window *w, View *v);                      /* "mm" */
void jump_history(Window *w, View *v, int dir);          /* Ctrl-o / Ctrl-i */

/* ---------- LaTeX: SyncTeX and the link with VimTeX (synctex.c, ipc.c) ---------- */
enum { IPC_REQUEST = 1, IPC_FILE = 2 };
extern Uint32 ipc_event;
bool synctex_available(Doc *d);
bool synctex_inverse(Window *w, View *v, int page, double x, double y);
bool synctex_forward(Window *w, View *v, const char *tex, int line, int col);
void ipc_start(void);
void ipc_stop(void);
bool ipc_send(const char *msg);                          /* to a running instance */
void ipc_watch(Doc *d);

/* ---------- printing (print.c) ---------- */
void print_document(Window *w, View *v, const char *args);
char *print_printers(void);                              /* names, one per line */

/* ---------- search (search.c) ---------- */
bool search_run(Window *w, View *v, int dir, bool from_current);
void search_draw(Window *w, View *v, int top);

/* ---------- panel (panel.c) ---------- */
void panel_open(Window *w);
void panel_close(Window *w);
void panel_move(Window *w, int delta);
void panel_activate(Window *w);
void panel_delete(Window *w);
void panel_delete_comment(Window *w);
void panel_rename(Window *w);
void panel_switch(Window *w, int tab);                    /* tab index, or -1 / -2 for prev / next */
void panel_draw(Window *w);

/* ---------- UI text (font.c) ---------- */
Font *font_open(SDL_Renderer *ren, const char *family, double points, float density);
void font_close(Font *f);
void font_shutdown(void);
int font_height(Font *f);
int font_ascent(Font *f);
float font_em(Font *f);
int font_width(Font *f, const char *s);
void font_fit(Font *f, char *s, int maxw);         /* ellipsis; s needs 3 spare bytes */
void font_draw(Font *f, float x, float y, const char *s, uint32_t rgb);   /* y = top */

/* ---------- drawing helpers and overlays (ui.c) ---------- */
void ui_ring(Window *w, float cx, float cy, float r, float th, uint32_t rgb, uint8_t a);
uint32_t rgb_mix(uint32_t a, uint32_t b, float f);       /* f = 0: a, 1: b */
void ui_fill(Window *w, float x, float y, float wd, float ht, uint32_t rgb, uint8_t a);
void ui_frame(Window *w, float x, float y, float wd, float ht, float t, uint32_t rgb);
void ui_message(Window *w, const char *fmt, ...);
void ui_message_long(Window *w, int ms, const char *fmt, ...);
int ui_bar_height(Window *w);
int ui_tabbar_height(Window *w);
void ui_draw_overlays(Window *w, uint64_t now);
void ui_draw_notes(Window *w, View *v, int top);

/* ---------- utilities (ui.c) ---------- */
/* Allocation that cannot return NULL: out of memory ends the program with a
 * message instead of undefined behaviour later. */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
/* math.c: "$...$" in comments, typeset by pdflatex in the background */
typedef enum { MATH_PENDING, MATH_READY, MATH_FAILED } MathStatus;
typedef struct {
    SDL_Texture *tex;        /* white, tinted with SDL_SetTextureColorMod */
    int w, h, baseline;      /* pixels; baseline from the top */
    const char *error;       /* MATH_FAILED: the LaTeX error, valid until the next math_get */
} MathImage;
MathStatus math_get(SDL_Renderer *ren, const char *src, size_t len, bool display, float scale, MathImage *out);
bool math_poll(void);                                     /* true when a formula finished */
bool math_busy(void);
void math_forget_renderer(SDL_Renderer *ren);
void math_shutdown(void);

/* comment.c: comments with equations, shown and edited in a box */
void comment_draw(Window *w, const char *text);          /* the comment under the cursor */
void comment_editor_open(Window *w, int page, PRect anchor);
bool comment_editor_key(Window *w, const char *tok);    /* false: not handled */
void comment_editor_insert(Window *w, const char *text); /* typed text */
void comment_editor_draw(Window *w);

/* help.c: the help screen (\c) */
void help_open(Window *w);
void help_draw(Window *w);
void help_move(Window *w, int delta);
void help_edge(Window *w, bool last);
void help_switch(Window *w, int tab);                     /* -2 next, -1 previous */
void help_search(Window *w);
void help_escape(Window *w);

/* export.c */
int export_write(Doc *d, FILE *f, int *ncomments);       /* Markdown; returns highlights */
void export_notes(Window *w, View *v, const char *arg, bool force);

/* links.c */
void links_start(Window *w, bool preview);
bool links_click(Window *w, float sx, float sy);
void hints_key(Window *w, const char *tok);
void hints_draw(Window *w);
void preview_draw(Window *w);
void preview_scroll(Window *w, float lines);
void preview_go(Window *w);
void preview_close(Window *w);

int utf8_to_utf16(const char *s, unsigned short *out, int cap);
char *utf16_to_utf8(const unsigned short *s, int n);
void text_join_hyphens(char *s);                          /* removes PDFium's U+0002 breaks */
unsigned utf8_next(const char **s);
