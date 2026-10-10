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
/* PDFium pages kept open, all documents together: an open page keeps its
 * decoded images (a 24 Mpixel photo is ~70 MB), so few, least recently used
 * first. Rendered pages stay as textures (render.c). */
#define LOADED_PAGE_CAP 8
#define NOMADYS_VERSION "0.5.0"
#define NOMADYS_AUTHOR "Hugo Oliveira"
#define NOMADYS_CONTACT "hs.oliveira@mailbox.org"
#define NOMADYS_URL "github.com/hdsoliveira/Nomadys-PDF"
#define CMD_MAX 4096
#define NMARKS 26
#define JUMPS_MAX 100

typedef struct { float l, t, r, b; } PRect; /* page space: y up, t > b */

typedef struct Window Window;
typedef struct {
    unsigned colors;         /* bits 0-4: C V B N M colours, bit 5: others; 0 = every colour */
    bool commented;          /* only those with a comment */
    bool open_tasks;         /* only tasks still to do */
} NoteFilter;

/* A highlight or comment can be a task (t in a cursor or the panel):
 * kept in the annotation as NomadysTask = "open" or "done 2026-09-29". */
enum { TASK_NONE, TASK_OPEN, TASK_DONE };

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
    uint32_t hint_bg, hint_fg;           /* link letters (f, F); THEME_AUTO: from the accent */
} Theme;
#define THEME_AUTO 0x01000000u             /* a colour a theme leaves to be derived */

typedef struct {
    bool dark;               /* start in dark mode */
    bool autosave;
    int autosave_delay_ms;
    bool backup;             /* keep the file as it was before the first save of a session (name.pdf.bak) */
    bool subpixel_text;
    int copy_dpi;            /* y / Y in the free cursor: the region as an image */
    bool show_tabs;
    bool page_shadow;        /* draw a soft shadow around pages */
    bool mode_indicator;     /* small coloured badge with the current mode */
    int dark_images;         /* DARK_IMAGES_*: how dark mode treats images */
    double dark_text_weight; /* dark mode: how firm light letters are drawn, 1 = plain inversion */
    bool restore_position;   /* reopen a file where it was left */
    bool remember_view;      /* and with its zoom, dark mode, two pages, rotation */
    uint32_t highlights[5];  /* colours of new highlights for C V B N M, 0xRRGGBB */
    bool area_frame;         /* areas: frame around instead of a highlight */
    double zoom;             /* 0 = fit width, otherwise 1.0 = 100 % */
    char font[128];          /* UI font family (fontconfig pattern) */
    char pdf_dirs[1024];     /* where \f looks, comma separated */
    char cite_format[128];   /* Y: %t the text, %p the page */
    double font_size;        /* UI font size in points */
    char leader[8];          /* leader key token, e.g. "\\" */
    char synctex_editor[1024]; /* inverse search command: %l line, %c column, %f file */
    char theme[64];          /* the theme chosen (include = ...), "" = built in */
    char theme_path[2048];
    Theme light, dark_theme;
} Config;

enum { DARK_IMAGES_SMART, DARK_IMAGES_KEEP, DARK_IMAGES_INVERT };
extern Config cfg;
void config_load(void);
const Theme *theme_of(bool dark);
const char *config_file(void);
extern char config_error[256];                           /* the first problem in the config or a theme */
bool theme_peek(const char *name, Theme *light, Theme *dark);
bool theme_apply(const char *name);
int theme_list(char ***names, bool **yours);
bool theme_save(const char *name, char *err, size_t cap);
void theme_user_dir(char *out, size_t cap);
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
    bool has_reply;          /* someone answered it (a reply, /IRT) */
    bool area;               /* a highlight over an area (a figure), not over text */
    bool textbox;            /* a free text (textbox.c): a stamp whose letters are shapes */
    int task;                /* TASK_*: not a task, to do, done */
    char done_on[11];        /* the day it was done, "2026-09-29", or "" */
    uint32_t color;          /* 0xRRGGBB, for the panel */
} Note;

typedef struct {
    int w, h;
    uint8_t *a;              /* opacity, top row first; NULL: opaque all over */
    uint8_t *rgb;            /* with a: its colours, B G R, to tell where the page shows it */
    PRect full;              /* the whole picture on the page, before any clipping */
    bool flip_x, flip_y;     /* a is mirrored against the page (a form around the image) */
} Alpha;

typedef struct {
    FPDF_PAGE page;
    FPDF_TEXTPAGE text;
    unsigned version;        /* bumped whenever the page appearance changes */
    uint64_t used;
    PRect *images;           /* regions kept untouched in dark mode */
    int nimages;
    int placed_from;         /* images[placed_from..]: pictures you placed, always kept */
    Alpha *alphas;           /* one per image: where it is see-through */
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

/* How a placed image is shown: the picture is drawn into its appearance
 * box, cropped (the part u0..u1, v0..v1 of it, 0-1, v up) and turned;
 * whatever falls outside the box is clipped by every reader. Nothing is
 * re-encoded, so cropping can always be undone. */
typedef struct {
    float box[4];            /* appearance box: l, b, r, t */
    float u0, v0, u1, v1;
    int turns;               /* quarter turns clockwise */
    bool flip_h, flip_v;     /* mirrored on screen, after the turns */
    float angle;             /* free turn in degrees, clockwise, around the centre of the box */
    float rw, rh;            /* with an angle: the size of the picture in box units (the box is fixed) */
} ImageView;

/* The mouse working on an image or area (object.c). */
typedef struct {
    int kind;                /* 0 none, 1 move, 2 resize, 3 turn */
    int sx, sy;              /* the handle: -1, 0 or 1 along the picture's x and y */
    bool pending;            /* the pointer moved since the last frame */
    float px, py;            /* the pointer when it began, in page points */
    float cx, cy, W, H;      /* the picture when it began: centre and size in page points */
    PRect rect0;
    ImageView view0;
    float wx, wy, x0, y0;    /* the last and the first pointer position, window pixels */
    bool live;               /* the PDF is left alone while dragging: the picture shows from a texture */
    SDL_Texture *tex;        /* the picture, for that (NULL for an area) */
    uint32_t color;          /* an area's colour */
    int area_kind;           /* an area: its annotation subtype */
} ObjDrag;

/* A free text: what is written, how big the letters are, and the box (its
 * top left corner and width; the height follows the text). */
typedef struct {
    char *text;              /* UTF-8, malloc'd */
    float size;              /* points */
    float width, left, top;  /* the box, page points */
} TextBox;

/* The free text being written (textbox.c). */
typedef struct {
    int page, annot;
    TextBox box;
    int cursor;              /* byte offset into box.text */
    float want_x;            /* the column kept by Up and Down */
    bool fresh;              /* created by this editing: Esc on an empty one removes it */
    char *before;            /* the box as it was (an existing one), for undo */
    int back;                /* the mode to return to (a Mode) */
} TextEd;

typedef enum { U_CREATE, U_DELETE, U_COMMENT, U_EDIT, U_PAGES, U_TASK, U_TEXTBOX } UndoKind;
typedef struct {
    UndoKind kind;
    int page, group;         /* a group is undone at once */
    char nm[64];             /* the annotation (/NM) */
    PRect before, after;     /* U_EDIT */
    bool has_view;
    ImageView vbefore, vafter;
    char *text_before, *text_after; /* U_COMMENT, U_TASK */
    Mark *marks_before, *marks_after; /* U_PAGES: text_before / text_after are snapshot files */
} Undo;

struct Doc {
    char *path;
    char *password;          /* only in memory, NULL when not protected */
    Undo *undo;              /* changes to annotations, for u / Ctrl-r (annot.c) */
    int nundo, undo_at, undo_group, undo_group_open;
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
    struct {                 /* how it was last seen, when not the default */
        bool set, dual, dark;
        double zoom;         /* 0 = fit width, 1.0 = 100 % */
        int rotation;
    } prefs;
    /* reloading after an external change (ipc.c watches the directory) */
    int watch_wd;
    uint64_t reload_at;      /* 0 = nothing pending */
    int reload_tries;
    long long saved_mtime_ns; /* our own last save, not an external change */
    int pages_opened;        /* since the last (re)load: when to compact */
    int save_ms;             /* how long the last save took */
    bool backed_up;          /* the .bak of this session is made */
    bool backup_new;         /* ... just now: save() says so once */
    uint64_t hidden_since;   /* 0 while a window shows it */
    /* replies made here: (reply /NM, the /NM of the note it answers); their
     * /IRT is written into the file after each save (reply.c) */
    char (*replies)[2][64];
    int nreplies;
};

/* Every page accessor checks this: a reload can shrink the document under
 * a pending action that still holds an old page number. */
static inline bool doc_page_ok(const Doc *d, int i) { return i >= 0 && i < d->npages; }
Doc *doc_open(const char *path, char *err, size_t errlen);
Doc *doc_open_locked(const char *path, const char *password, bool *locked, char *err, size_t errlen);
void doc_ref(Doc *d);
void doc_unref(Doc *d);
FPDF_PAGE doc_page(Doc *d, int i);
FPDF_TEXTPAGE doc_text(Doc *d, int i);
void doc_mark_changed(Doc *d, int page);
void doc_reload_page(Doc *d, int page);                   /* parse it again (stale appearances) */
bool doc_save(Doc *d, char *err, size_t errlen);
bool doc_save_copy(Doc *d, const char *path, bool overwrite, char *err, size_t errlen); /* a copy under another name */
void doc_page_images(Doc *d, int i);
void doc_page_widgets(Doc *d, int i);
void doc_page_chars(Doc *d, int i);
int doc_run_timers(Doc *d, uint64_t now);    /* returns ms until next timer, -1 none */
bool doc_focus_widget(Doc *d, int page, int w);
void doc_kill_focus(Doc *d);
bool doc_has_forms(Doc *d);
bool doc_reload(Doc *d);
bool doc_compact(Doc *d);                                 /* reopen to free PDFium's caches */                                  /* false while the file is incomplete */
long long file_mtime_ns(const char *path);
extern Doc *docs[MAX_DOCS];
extern int ndocs;
extern bool need_redraw_all;
extern bool memory_freed;                                  /* give memory back to the system when idle */

/* ---------- annotations (annot.c) ---------- */
void notes_page(Doc *d, int page);                       /* fills slots[page].notes */
int note_at(Doc *d, int page, float x, float y);         /* index into notes, or -1 */
bool note_create(Doc *d, int page, const PRect *quads, int n, const char *comment, uint32_t color);
bool note_create_area(Doc *d, int page, PRect r, const char *comment, uint32_t color);
char *note_comment(Doc *d, int page, int note);          /* malloc'd UTF-8, or NULL */
bool note_set_comment(Doc *d, int page, int note, const char *comment);
bool note_set_task(Doc *d, int page, int note, int task);  /* TASK_*, undoable */
void note_author(Doc *d, int page, int note, char *out, size_t cap); /* /T, or "" */
typedef struct {
    char author[128];
    char *text;              /* malloc'd UTF-8 */
    bool mine;               /* written by this user (answering a reviewer) */
} NoteReply;
int note_replies(Doc *d, int page, int note, NoteReply **out); /* the replies to a note, oldest first */
void note_replies_free(NoteReply *r, int n);
bool note_set_reply(Doc *d, int page, int note, const char *text); /* ours: new, changed, "" deletes; undoable */
const char *note_user(void);                                /* the author name we write (/T) */
void notes_tasks(Doc *d, int *open, int *done);            /* the tasks of the whole PDF */
void note_task_key(Window *w, Doc *d, int page, int note, char key); /* t: a task or not, d: done or not */
bool note_delete(Doc *d, int page, int note);
int note_create_image(Doc *d, int page, PRect r, FPDF_PAGEOBJECT image); /* annotation index, or -1 */
int note_pick(Doc *d, int page, float x, float y, float tol);
int note_move(Doc *d, int page, int annot, PRect r);     /* new annotation index, or -1 */

bool note_image_view(Doc *d, int page, int annot, ImageView *iv, int *pxw, int *pxh);
/* annot.c: free texts */
int note_text_create(Doc *d, int page, const TextBox *b);                 /* annotation index, or -1 */
int note_text_update(Doc *d, int page, int annot, const TextBox *b);      /* the same box, as now written: its new index, or -1 */
bool note_text_get(Doc *d, int page, int annot, TextBox *b);              /* false: not a free text */
void undo_textbox(Doc *d, int page, int annot, const char *before, const char *after);
bool note_image_apply(Doc *d, int page, int annot, const ImageView *iv, PRect r);
void note_view_unit(const ImageView *iv, float f[6]);
bool note_image_pixels(Doc *d, int page, int annot, int side, uint8_t **bgra, int *wd, int *ht);
void note_set_shown(Doc *d, int page, int annot, bool shown);
int note_of_annot(Doc *d, int page, int annot);           /* note index, or -1 */
const char *doc_undo(Doc *d, bool redo, int *page);       /* what was undone, NULL if nothing */
void doc_undo_clear(Doc *d);
void doc_purge_deleted(Doc *d);                           /* closing: hidden deletions go */
int undo_group_begin(Doc *d);
void undo_group_end(Doc *d);
void undo_pages(Doc *d, int page, char *snapshot, const Mark *marks_before); /* takes the snapshot path */
char *pages_snapshot(Doc *d);                              /* the document as it is, to a temporary file */
bool pages_restore(Doc *d, const char *snapshot, const Mark *marks); /* back to a snapshot (undo) */
void doc_pages_changed(Doc *d);
bool *pages_pick(int npages, int first, int last);        /* first..last as one bool per page (free it) */
bool pages_pick_span(const bool *pick, int n, int *first, int *last); /* side by side? */
void pages_delete(Window *w, Doc *d, const bool *pick);
void pages_rotate(Window *w, Doc *d, const bool *pick, int quarters);
int pages_move(Window *w, Doc *d, int first, int last, int dest); /* where they went, or -1 */
bool pages_extract(Window *w, Doc *d, const bool *pick, const char *path);
void pages_insert(Window *w, Doc *d, const char *path, int after);
bool pages_parse_range(const char *s, int npages, int *first, int *last); /* "3-7" */
void pages_default_name(Doc *d, const bool *pick, char *out, size_t cap);
bool doc_replace_pdf(Doc *d, const char *path);
bool pdf_write(FPDF_DOCUMENT pdf, const char *path, char *err, size_t errlen);
typedef struct { int added, already, skipped, first_page; bool pages_differ; } MergeResult;
bool doc_merge(Doc *d, const char *path, MergeResult *res, char *err, size_t cap);
void undo_edit(Doc *d, int page, int annot, PRect before, PRect after, const ImageView *vbefore, const ImageView *vafter);

/* ---------- views / tabs (render.c) ---------- */
typedef struct {
    int page;
    int x, y, w, h;          /* clip in page pixel space */
    int pw, ph;              /* full page pixel size */
    bool dark;
    unsigned version;
    SDL_Texture *tex;
    uint64_t used;
    unsigned theme;          /* dark pages: the theme they were drawn in (render_theme) */
} CacheEntry;

typedef enum { MODE_NORMAL, MODE_FORM, MODE_CARET, MODE_AREA, MODE_PANEL, MODE_PROMPT, MODE_HELP,
               MODE_HINTS, MODE_PREVIEW, MODE_OBJECT, MODE_CROP, MODE_ROTATE, MODE_TEXT, MODE_PICKER, MODE_LIBRARY, MODE_GRID, MODE_PRESENT, MODE_THEMES, MODE_ABOUT } Mode;

typedef struct { int page, ch; } TextPos;

typedef struct {
    Doc *doc;
    double zoom;             /* pixels per point (device pixels) */
    float density;           /* of the window it was last drawn in */
    bool fit_width;
    bool dual;
    bool dark;
    int rotation;            /* quarter turns clockwise, 0-3 (view only) */
    double sx, sy;           /* scroll offsets in pixels */
    Pos pending_pos;         /* applied at the first layout, page -1 = none */
    /* layout */
    int lay_w;
    int lay_n;               /* pages the layout was made for (the document may have changed since) */
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
    bool highlight_all;      /* every match on screen, until Esc or :noh */
    bool match_case;         /* Ctrl-t in the prompt; otherwise smart case */
    Pos search_origin;       /* where '/' was pressed: Esc goes back there */
    char search_saved[CMD_MAX];
    int search_saved_page;
    struct Diff *diff;       /* :diff old.pdf (diff.c), NULL when off */
} View;

View *view_new(Doc *d);
View *view_clone(View *v);
void view_free(View *v);
void view_layout(View *v, int W, int H, int top);
void view_clamp(View *v, int W, int H, int top);
void view_draw(Window *w, View *v, int top);
bool view_prefetch(Window *w, View *v, int top);
void view_invalidate_cache(View *v);
void render_new_theme(void);          /* the colours changed: then view_theme_changed on each view */
void view_theme_changed(View *v);    /* dark pages drawn again, the old ones shown meanwhile */
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
/* FPDF_PageToDevice for coordinates that come from a file: a hostile one can ask for 1e30, which turns
 * into an integer that overflows when the layout adds to it. The inputs are kept within a range any
 * real page is inside of. */
static inline void pdev(FPDF_PAGE p, int sx, int sy, int pw, int ph, int rot, double x, double y, int *dx, int *dy)
{
    x = x != x ? 0 : x < -1e6 ? -1e6 : x > 1e6 ? 1e6 : x;
    y = y != y ? 0 : y < -1e6 ? -1e6 : y > 1e6 ? 1e6 : y;
    FPDF_PageToDevice(p, sx, sy, pw, ph, rot, x, y, dx, dy);
}

void page_to_screen(View *v, int page, float x, float y, float *sx, float *sy, int top);
float page_left_screen(View *v, int page);
bool screen_to_page(View *v, float sx, float sy, int top, int *page, double *x, double *y);

/* ---------- windows ---------- */
typedef struct Font Font;
typedef struct Thumb Thumb;
typedef struct Slide Slide;
typedef void (*PromptFn)(Window *w, const char *text);

typedef enum { PANEL_OPEN_TABS, PANEL_CONTENTS, PANEL_MARKS, PANEL_NOTES, PANEL_TASKS, PANEL_NTABS } PanelTab; /* tab order */

typedef struct {
    char text[256];          /* main line */
    char sub[256];           /* second, dimmer line (highlighted text), or "" */
    int depth;               /* contents: nesting level */
    Pos pos;                 /* where Enter goes; page -1 = nowhere */
    int page, note, mark, tab;
    uint32_t color;          /* note colour */
    int task;                /* note: TASK_* */
    bool header;             /* Tasks: the "Done" line between to do and done; not selectable */
    char when[32];           /* Tasks: when it was done ("today", "3 days ago") */
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
    bool prompt_secret;      /* a password: shown as dots */
    char pending_path[4096]; /* the PDF the password is for */
    int cmdpos;              /* cursor, byte offset into cmd */
    int box_page;            /* -1: centred */
    PRect box_anchor;
    float box_maxw, box_scroll;
    uint64_t locate_at;      /* when the text or free cursor appeared (caret.c) */
    /* recent files picker (recent.c) */
    char picker_query[256];
    int picker_sel, picker_first;
    Mode picker_return;
    bool picker_files;       /* the finder (\f), not the recent files */
    /* library search (library.c) */
    char lib_query[256];
    bool lib_notes;          /* comments and highlights, not the text */
    bool lib_tasks;          /* with lib_notes: only the tasks to do (:tasks) */
    int lib_sel, lib_first;
    Mode lib_return;
    /* split view (split.c): a second pane, its own view */
    View *split;
    bool split_side, split_focus;  /* side by side / above each other; the second pane has the keys */
    bool in_pane, pane_second;     /* working in a pane now: W, H are its size */
    int full_W, full_H, pane_x, pane_y;
    /* page grid (grid.c) */
    Thumb *thumbs;
    int grid_sel, grid_top, grid_size;
    int grid_anchor;         /* v in the grid: the other end of the pages chosen, -1 none */
    bool *grid_marked;       /* Space in the grid: pages chosen one by one */
    int grid_marked_n;       /* the page count they were chosen in */
    bool *grid_extract;      /* y: the pages to save, while their name is typed */
    int grid_extract_n;
    Mode grid_return;
    /* presentation (present.c) */
    /* about screen (about.c) */
    SDL_Texture *about_tex;
    Mode about_return;
    /* theme list (theme.c) */
    char **theme_names;
    bool *theme_yours;
    Theme *theme_samples;    /* light and dark of each, for the samples */
    int ntheme, theme_sel, theme_first;
    char theme_orig[64];     /* Esc goes back to it */
    Mode theme_return;
    Slide *slides;
    int present_page;
    bool present_black, present_was_full;
    Mode present_return;
    /* an image or area being moved / resized (object.c) */
    int obj_page, obj_annot; /* annotation index on the page */
    PRect obj_rect, obj_orig;
    float obj_ratio;         /* the picture's width / height */
    bool obj_image;
    bool obj_view_ok;        /* an image placed here: it can be cropped and turned */
    ImageView obj_view, obj_view_orig, crop_view;
    PRect crop_rect;         /* before cropping started, for Esc */
    int obj_pxw, obj_pxh;
    Mode obj_return;
    ImageView rot_view;      /* before the free turn started, for Esc */
    PRect rot_rect;
    bool text_hidden;        /* a free text's annotation is hidden while its letters are drawn by textbox.c */
    bool obj_text;           /* a free text: moves and resizes by being written again (textbox.c) */
    TextBox obj_tb;          /* ... and what it says now */
    char *obj_tx_before;     /* ... as it was, for Esc and undo */
    ObjDrag obj_drag;        /* the mouse is moving / resizing / turning it (object.c) */
    TextEd tx;               /* the free text being written (textbox.c) */
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
    NoteFilter panel_filter; /* Highlights tab: colours shown, only with comments */
    bool panel_hide_done;    /* Tasks tab: only those to do (D) */
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
const Theme *uitheme(Window *w);                /* menus and bars: the theme's dark mode, always */
void window_new_from(Window *w);
bool open_tab(Window *w, const char *path);
void ask_password(Window *w, const char *path, bool again);
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
void caret_copy(Window *w, View *v, bool cite);            /* cite: as a quotation with its page */
bool note_reply_edit(Window *w, View *v);                  /* r: answer the note under the cursor */
char *note_thread(Doc *d, int page, int note);             /* its comment and the replies, for showing */
void caret_mouse_down(Window *w, View *v, float x, float y); /* selecting by dragging over the text */
void caret_mouse_drag(Window *w, View *v, float x, float y);
void caret_mouse_up(Window *w, View *v);                     /* copies what was dragged over */
void cite_copy(Window *w, View *v, char *text, int first_page, int last_page);
void cite_bibtex(Window *w, View *v);                      /* :bibtex */
void cite_result(Window *w, const char *text);             /* the download finished */
void area_enter(Window *w, View *v);
void area_move(Window *w, View *v, float dx, float dy);
void area_toggle_select(Window *w, View *v);
void area_mark(Window *w, View *v, bool comment, uint32_t color);
bool area_target(View *v, int *page, PRect *rect, bool *is_rect); /* free cursor point or rectangle */
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
enum { IPC_REQUEST = 1, IPC_FILE = 2, IPC_BIBTEX = 3, IPC_FINDER = 4, IPC_INDEX = 5, IPC_LIBRARY = 6 };
extern Uint32 ipc_event;
bool synctex_available(Doc *d);
bool synctex_inverse(Window *w, View *v, int page, double x, double y);
bool synctex_forward(Window *w, View *v, const char *tex, int line, int col);
void ipc_start(void);
void ipc_stop(void);
bool ipc_send(const char *msg);                          /* to a running instance */
void ipc_watch(Doc *d);
int ipc_watch_dir(const char *dir);

/* ---------- printing (print.c) ---------- */
void print_document(Window *w, View *v, const char *args);
char *print_printers(void);                              /* names, one per line */

/* ---------- search (search.c) ---------- */
bool search_run(Window *w, View *v, int dir, bool from_current);
void search_begin(Window *w, View *v);
void search_live(Window *w, View *v, const char *text);   /* the prompt changed */
void search_cancel(Window *w, View *v);
void search_commit(Window *w, View *v, const char *text);
void search_forget(const Doc *d);
void search_draw(Window *w, View *v, int top);

/* ---------- panel (panel.c) ---------- */
void panel_open(Window *w);
void panel_close(Window *w);
void panel_move(Window *w, int delta);
void panel_activate(Window *w);
void panel_delete(Window *w);
void panel_delete_comment(Window *w);
void panel_task(Window *w, char key);                      /* t, d, Space, D on the Highlights / Tasks tabs */
void panel_rename(Window *w);
void panel_switch(Window *w, int tab);                    /* tab index, or -1 / -2 for prev / next */
void panel_draw(Window *w);
void panel_filter(Window *w, char key);                    /* C V B N M c a e in the Highlights tab */

/* ---------- UI text (font.c) ---------- */
char *font_file(const char *family, int *index);            /* the file of a font (malloc'd), or NULL */
Font *font_open(SDL_Renderer *ren, const char *family, double points, float density);
extern char font_fallback[160];                             /* set when the config's font is missing */
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
void ui_line(Window *w, float x0, float y0, float x1, float y1, float th, uint32_t rgb);
void ui_task_box(Window *w, float x, float y, float size, bool done, uint32_t rgb, uint32_t tick);
uint32_t rgb_mix(uint32_t a, uint32_t b, float f);       /* f = 0: a, 1: b */
void ui_round(Window *w, float x, float y, float wd, float ht, float r, uint32_t rgb, uint8_t a); /* rounded rectangle */
uint32_t ink_on(uint32_t bg, uint32_t a, uint32_t b);      /* of two inks, the one that reads better on bg */
uint32_t hint_bg(const Theme *t);
uint32_t hint_fg(const Theme *t);
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

/* image.c: images put on a page (stamp annotations) */
void image_insert_file(Window *w, const char *path);
void image_paste(Window *w);                              /* from the clipboard */
void image_copy_region(Window *w, View *v, bool to_file); /* y / Y: the marked region as a PNG */

/* textbox.c: free text, its letters drawn as shapes */
#define TEXT_PAD 3.0f
#define TEXT_SIZE_MIN 4.0f
#define TEXT_SIZE_MAX 144.0f
float textbox_height(const TextBox *b);                    /* the box's height: the text and its padding */
bool textbox_objects(Doc *d, FPDF_ANNOTATION annot, const TextBox *b); /* the text, put in an annotation */
void textbox_paint(Window *w, View *v, int top, int page, const TextBox *b); /* the letters over the page */
void textbox_forget_renderer(SDL_Renderer *ren);
void textbox_abort(Window *w);
char *textbox_encode(const TextBox *b);                    /* for undo: malloc'd */
bool textbox_decode(const char *s, TextBox *b);            /* b->text malloc'd */
void textbox_free(TextBox *b);
void textbox_begin(Window *w, View *v);                    /* T in the free cursor: a new one */
void textbox_edit(Window *w, View *v, int page, int annot, int back); /* write in an existing one */
bool textbox_key(Window *w, const char *tok);
void textbox_insert(Window *w, const char *text);
void textbox_draw(Window *w, View *v, int top);            /* the cursor, in the page */
void textbox_finish(Window *w);
void textbox_shutdown(void);

/* object.c: moving and resizing images and areas */
void object_begin(Window *w, View *v, int page, int annot, float ratio);
void object_begin_at_cursor(Window *w, View *v);
void object_move(Window *w, float dx, float dy);
void object_resize(Window *w, float dw, float dh);
void object_scale(Window *w, float f);
void object_proportions(Window *w);
void object_done(Window *w, bool keep);
void object_delete(Window *w);
void object_rotate(Window *w, int dir);                   /* quarter turns: 1 clockwise, -1 back */
void object_crop_begin(Window *w);
void object_crop(Window *w, int side, float amount);      /* side 0-3: left right top bottom */
void object_crop_reset(Window *w);
void object_crop_done(Window *w, bool keep);
void object_flip(Window *w, bool horizontal);
void object_text_edit(Window *w);                         /* i: write in the text box again */             /* mirror left-right / top-bottom */
void object_rotate_begin(Window *w);
void object_angle(Window *w, float degrees);              /* turn freely: clockwise when positive */
void object_angle_reset(Window *w);
void object_rotate_done(Window *w, bool keep);
bool object_mouse_down(Window *w, float x, float y, int clicks);
bool object_mouse_motion(Window *w, float x, float y);    /* true while a drag is going on */
bool object_mouse_up(Window *w, float x, float y);
void object_mouse_flush(Window *w);                       /* once per frame: applies the last pointer position */
bool object_mouse_active(const Window *w);
void object_draw(Window *w, View *v, int top);

/* recent.c: recent files, the session, the picker */
void recent_add(const char *path);
void recent_shutdown(void);
void session_save(void);
int session_files(char ***out, int *current);             /* the last session's files */
void session_restore(Window *w);
void picker_open(Window *w, bool files);                   /* files: \f, every PDF in pdf_dirs */
void picker_refresh(void);
int pdf_scan(const char *dirs, char ***paths, long long **when); /* every PDF in pdf_dirs */
int library_worker(const char *dir, int part);            /* npdf --index-worker DIR K */
void library_open(Window *w, bool notes);                 /* \/ text, \n comments and highlights */
void library_tasks(Window *w);                            /* :tasks, the tasks to do in every PDF */
void library_event(int code);                             /* IPC_INDEX / IPC_LIBRARY */
void library_key(Window *w, const char *tok);
void library_text(Window *w, const char *text);
void library_draw(Window *w);
void library_shutdown(void);
View *pane_view(Window *w, bool second);
bool pane_enter(Window *w, bool second);                   /* false: no split, nothing to leave */
bool pane_enter_focused(Window *w);
bool pane_enter_at(Window *w, float *x, float *y);         /* the pane under the point gets the focus */
void pane_leave(Window *w);
void split_draw_frame(Window *w);
void split_open(Window *w, bool side_by_side, const char *path);
void split_close(Window *w, bool focused);
void split_focus_other(Window *w);
void split_focus_dir(Window *w, char dir);
void split_swap(Window *w);
bool window_alive(Window *w);
void render_cancel(Doc *d, int page);                      /* page -1: any; before it is closed or changed */
bool render_work(void);                                    /* more of a heavy page, while idle */
bool render_busy(void);
void render_finish(void);                                  /* test mode: finish it now */
extern bool render_incomplete;                             /* a page on screen still being rendered */                                 /* the finder found more */
char *expand_home(const char *path);                      /* "~/x" -> "/home/you/x", malloc'd */
void picker_key(Window *w, const char *tok);
void picker_text(Window *w, const char *text);
void picker_draw(Window *w);

/* grid.c: the page grid */
void grid_open(Window *w, View *v);
void grid_close(Window *w);
void grid_free(Window *w);
void grid_reset(Window *w);
void grid_move(Window *w, int dx, int dy);
void grid_edge(Window *w, bool last);
void grid_screen(Window *w, int dir);
void grid_zoom(Window *w, int dir);
void grid_go(Window *w);
bool *grid_pick(Window *w, View *v);                       /* the pages a change acts on (free it) */
void grid_mark(Window *w);                                 /* Space: choose / unchoose this page */
void grid_marks_remap(Window *w, const Doc *d, const int *map, int old_n); /* after the pages changed */
int grid_cols(Window *w);
bool grid_work(void);                                      /* one thumbnail while idle */
void grid_draw(Window *w);

/* present.c: presentation mode */
void present_start(Window *w, View *v);
void present_stop(Window *w);
void present_free(Window *w);
void present_reset(Window *w);
void present_go(Window *w, int page);
void present_step(Window *w, int delta);
void present_blank(Window *w);
void present_draw(Window *w);
bool present_work(void);                                   /* the next slide while idle */

/* diff.c: comparing with an older version */
typedef struct Diff Diff;
void diff_start(Window *w, View *v, const char *old_path);
void diff_stop(Window *w, View *v);
void diff_free(Diff *d);
void diff_refresh(View *v);                               /* the PDF was reloaded */
void diff_step(Window *w, View *v, int dir);              /* ]c [c */
void diff_draw(Window *w, View *v, int top);

/* theme.c: choosing and editing themes while running */
void theme_open(Window *w);
void theme_key(Window *w, const char *tok);
void theme_draw(Window *w);
void theme_command(Window *w, const char *arg);         /* :theme [NAME | new NAME] */
void theme_new(Window *w, const char *arg);
void theme_watch(void);
bool theme_file_changed(int wd, const char *name);

/* about.c: the icon and the about screen */
SDL_Surface *about_icon(void);
void about_open(Window *w);
void about_close(Window *w);
void about_key(Window *w, const char *tok);
void about_draw(Window *w);
SDL_Surface *image_surface(const unsigned char *data, size_t len);

/* export.c */
int export_write(Doc *d, FILE *f, int *ncomments, const NoteFilter *only); /* Markdown; returns highlights */
int response_write(Doc *d, FILE *f, int *answered);         /* the reply to the reviewers; returns points */
void response_letter(Window *w, View *v, const char *arg, bool force);

/* ---------- replies in the file (reply.c) ---------- */
bool pdf_link_replies(int fd, char (*replies)[2][64], int n);
void export_notes(Window *w, View *v, const char *arg, bool force, const NoteFilter *only);
int note_color_index(uint32_t c);                         /* 0-4: C V B N M; 5: another colour */
bool note_filter_word(const char *word, NoteFilter *f);   /* "red", "green", "commented"... */
bool note_filter_pass(const NoteFilter *f, const Note *n);
void note_filter_describe(const NoteFilter *f, char *out, size_t cap);

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
