/* The panel ('\l'): five tabs — the open tabs, the PDF's summary (its
 * outline), marks, highlights/comments, and the highlights made tasks —
 * each a list navigated with j/k and opened with Enter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fpdf_doc.h"
#include "nomadys.h"

static const char *tab_names[PANEL_NTABS] = {[PANEL_OPEN_TABS] = "Tabs", [PANEL_CONTENTS] = "Summary",
                                             [PANEL_MARKS] = "Marks", [PANEL_NOTES] = "Highlights",
                                             [PANEL_TASKS] = "Tasks"};

/* The Highlights and Tasks tabs both list notes: Enter, r, x act the same. */
static bool notes_tab(const Window *w) { return w->panel_tab == PANEL_NOTES || w->panel_tab == PANEL_TASKS; }

/* ---------- building the lists ---------- */
static PanelItem *add(Window *w, int *cap)
{
    if (w->nitems == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        w->items = xrealloc(w->items, *cap * sizeof *w->items);
    }
    PanelItem *it = &w->items[w->nitems++];
    memset(it, 0, sizeof *it);
    return it;
}

static void one_line(char *s)
{
    for (; *s; s++)
        if (*s == '\r' || *s == '\n' || *s == '\t')
            *s = ' ';
}

/* Text covered by a note, e.g. the highlighted sentence. */
static char *covered_text(Doc *d, int page, Note *n)
{
    FPDF_TEXTPAGE tp = doc_text(d, page);
    if (!tp)
        return NULL;
    char *all = NULL;
    size_t len = 0;
    for (int q = 0; q < n->nquads && len < 240; q++) {
        PRect r = n->quads[q];
        unsigned short u[256];
        int got = FPDFText_GetBoundedText(tp, r.l, r.t, r.r, r.b, u, 255);
        if (got <= 0)
            continue;
        char *t = utf16_to_utf8(u, got);
        size_t tl = strlen(t);
        all = xrealloc(all, len + tl + 2);
        if (len)
            all[len++] = ' ';
        memcpy(all + len, t, tl + 1);
        len += tl;
        free(t);
    }
    if (all)
        text_join_hyphens(all);
    if (all && !*all) { /* an area over a figure: no text */
        free(all);
        all = NULL;
    }
    return all;
}

static void build_open_tabs(Window *w, int *cap)
{
    for (int i = 0; i < w->ntabs; i++) {
        View *v = w->tabs[i];
        PanelItem *it = add(w, cap);
        const char *slash = strrchr(v->doc->path, '/');
        it->tab = i;
        it->page = v->lay_w ? view_pos(v).page : -1;
        it->pos.page = -1;
        snprintf(it->text, sizeof it->text, "%s", slash ? slash + 1 : v->doc->path);
        /* the folder, with the home directory shown as ~ */
        const char *home = getenv("HOME");
        int dirlen = slash ? (int)(slash - v->doc->path) : 0;
        size_t hl = home ? strlen(home) : 0;
        if (hl && !strncmp(v->doc->path, home, hl) && (size_t)dirlen >= hl)
            snprintf(it->sub, sizeof it->sub, "~%.*s", dirlen - (int)hl, v->doc->path + hl);
        else
            snprintf(it->sub, sizeof it->sub, "%.*s", dirlen, v->doc->path);
    }
}

static void build_marks(Window *w, Doc *d, int *cap)
{
    for (int m = 0; m < NMARKS; m++) {
        if (!d->marks[m].set)
            continue;
        PanelItem *it = add(w, cap);
        it->mark = m;
        it->pos = d->marks[m].pos;
        it->page = it->pos.page;
        snprintf(it->text, sizeof it->text, "%s", d->marks[m].name[0] ? d->marks[m].name : "(no name)");
    }
}

/* One note as a row: its comment first, what it refers to below. */
static PanelItem *note_item(Window *w, Doc *d, int page, int i, int *cap)
{
    PageSlot *s = &d->slots[page];
    PanelItem *it = add(w, cap);
    it->page = page;
    it->note = i;
    it->color = s->notes[i].color;
    it->task = s->notes[i].task;
    it->pos = (Pos){page, SDL_max(0.0f, 1 - s->notes[i].r.t / d->ph[page] - 0.05f)};
    bool image = s->notes[i].subtype == FPDF_ANNOT_STAMP;
    char *c = note_comment(d, page, i), *t = image ? xstrdup("(image)") : covered_text(d, page, &s->notes[i]);
    if (c) {
        snprintf(it->text, sizeof it->text, "%s", c);
        if (t)
            snprintf(it->sub, sizeof it->sub, image ? "%s" : "“%s”", t);
    } else {
        snprintf(it->text, sizeof it->text, t && !image ? "“%s”" : "%s", t ? t : "(highlighted area)");
    }
    if (s->notes[i].has_reply) { /* answered: the last reply, where the quotation was */
        NoteReply *r;
        int n = note_replies(d, page, i, &r);
        if (n)
            snprintf(it->sub, sizeof it->sub, "↳ %s: %s", r[n - 1].author[0] ? r[n - 1].author : "reply", r[n - 1].text);
        note_replies_free(r, n);
    }
    one_line(it->text);
    one_line(it->sub);
    free(c);
    free(t);
    return it;
}

static void build_notes(Window *w, Doc *d, int *cap)
{
    for (int page = 0; page < d->npages; page++) {
        notes_page(d, page);
        PageSlot *s = &d->slots[page];
        for (int i = 0; i < s->nnotes; i++) {
            if (!note_filter_pass(&w->panel_filter, &s->notes[i]))
                continue;
            note_item(w, d, page, i, cap);
        }
    }
}

/* "today", "yesterday", "3 days ago", or the date. */
static void done_when(const char *day, char *out, size_t cap)
{
    struct tm t = {0};
    out[0] = 0;
    if (!day[0] || sscanf(day, "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3)
        return;
    t.tm_year -= 1900, t.tm_mon -= 1, t.tm_hour = 12;
    time_t now = time(NULL);
    struct tm today = *localtime(&now);
    today.tm_hour = 12, today.tm_min = today.tm_sec = 0;
    long days = (long)((mktime(&today) - mktime(&t) + 43200) / 86400);
    if (days <= 0)
        snprintf(out, cap, "done today");
    else if (days == 1)
        snprintf(out, cap, "done yesterday");
    else if (days < 30)
        snprintf(out, cap, "done %ld days ago", days);
    else
        snprintf(out, cap, "done %s", day);
}

/* To do first, in page order; then a "Done" line and the done ones. */
static void build_tasks(Window *w, Doc *d, int *cap)
{
    int ndone = 0;
    for (int pass = TASK_OPEN; pass <= TASK_DONE; pass++) {
        if (pass == TASK_DONE) {
            for (int page = 0; page < d->npages; page++)
                for (int i = 0; i < d->slots[page].nnotes; i++)
                    ndone += d->slots[page].notes[i].task == TASK_DONE;
            if (!ndone)
                break;
            PanelItem *h = add(w, cap);
            h->header = true;
            h->page = -1;
            h->pos.page = -1;
            snprintf(h->text, sizeof h->text, w->panel_hide_done ? "Done \u00b7 %d   (D shows them)" : "Done \u00b7 %d", ndone);
            if (w->panel_hide_done)
                break;
        }
        for (int page = 0; page < d->npages; page++) {
            notes_page(d, page);
            PageSlot *s = &d->slots[page];
            for (int i = 0; i < s->nnotes; i++)
                if (s->notes[i].task == pass) {
                    PanelItem *it = note_item(w, d, page, i, cap);
                    done_when(s->notes[i].done_on, it->when, sizeof it->when);
                }
        }
    }
}

static void build_contents(Window *w, Doc *d, FPDF_BOOKMARK parent, int depth, int *cap, int *budget)
{
    for (FPDF_BOOKMARK b = FPDFBookmark_GetFirstChild(d->pdf, parent); b && (*budget)-- > 0;
         b = FPDFBookmark_GetNextSibling(d->pdf, b)) {
        PanelItem *it = add(w, cap);
        it->depth = depth;
        unsigned short u[128] = {0};
        unsigned long bytes = FPDFBookmark_GetTitle(b, u, sizeof u - 2);
        char *t = utf16_to_utf8(u, bytes >= sizeof u - 2 ? 0 : (int)(bytes / 2));
        snprintf(it->text, sizeof it->text, "%s", t);
        one_line(it->text);
        free(t);
        FPDF_DEST dest = FPDFBookmark_GetDest(d->pdf, b);
        if (!dest && FPDFBookmark_GetAction(b))
            dest = FPDFAction_GetDest(d->pdf, FPDFBookmark_GetAction(b));
        it->pos.page = it->page = dest ? FPDFDest_GetDestPageIndex(d->pdf, dest) : -1;
        FPDF_BOOL hx, hy, hz;
        FS_FLOAT x, y, z;
        if (it->page >= 0 && FPDFDest_GetLocationInPage(dest, &hx, &hy, &hz, &x, &y, &z) && hy)
            it->pos.y = SDL_max(0.0f, 1 - y / d->ph[it->page]);
        if (depth < 6)
            build_contents(w, d, b, depth + 1, cap, budget);
    }
}

static void build(Window *w)
{
    Doc *d = cur_view(w)->doc;
    int cap = 0, budget = 5000;
    w->nitems = 0;
    if (w->panel_tab == PANEL_OPEN_TABS)
        build_open_tabs(w, &cap);
    else if (w->panel_tab == PANEL_MARKS)
        build_marks(w, d, &cap);
    else if (w->panel_tab == PANEL_NOTES)
        build_notes(w, d, &cap);
    else if (w->panel_tab == PANEL_TASKS)
        build_tasks(w, d, &cap);
    else
        build_contents(w, d, NULL, 0, &cap, &budget);
    w->sel = SDL_clamp(w->sel, 0, SDL_max(0, w->nitems - 1));
    if (w->sel < w->nitems && w->items[w->sel].header) /* never on the "Done" line */
        w->sel = w->sel + 1 < w->nitems ? w->sel + 1 : SDL_max(0, w->sel - 1);
}

/* Entries per tab, for the tab labels: counted without building the lists. */
static int count_contents(Doc *d, FPDF_BOOKMARK parent, int depth, int *budget)
{
    int n = 0;
    for (FPDF_BOOKMARK b = FPDFBookmark_GetFirstChild(d->pdf, parent); b && (*budget)-- > 0;
         b = FPDFBookmark_GetNextSibling(d->pdf, b))
        n += 1 + (depth < 6 ? count_contents(d, b, depth + 1, budget) : 0);
    return n;
}

static void count_tabs(Window *w)
{
    Doc *d = cur_view(w)->doc;
    int marks = 0, notes = 0, tasks = 0, budget = 5000;
    for (int m = 0; m < NMARKS; m++)
        marks += d->marks[m].set;
    for (int page = 0; page < d->npages; page++) {
        notes_page(d, page);
        notes += d->slots[page].nnotes;
        for (int i = 0; i < d->slots[page].nnotes; i++)
            tasks += d->slots[page].notes[i].task != TASK_NONE;
    }
    w->panel_counts[PANEL_TASKS] = tasks;
    w->panel_counts[PANEL_OPEN_TABS] = w->ntabs;
    w->panel_counts[PANEL_MARKS] = marks;
    w->panel_counts[PANEL_NOTES] = notes;
    w->panel_counts[PANEL_CONTENTS] = count_contents(d, NULL, 0, &budget);
}

static void rebuild(Window *w)
{
    build(w);
    count_tabs(w);
}

/* ---------- actions ---------- */
void panel_open(Window *w)
{
    w->sel = w->first = 0;
    count_tabs(w);
    /* start on a tab that has something, preferring the last one used */
    for (int k = 0; k < PANEL_NTABS && !w->panel_counts[w->panel_tab]; k++)
        w->panel_tab = (w->panel_tab + 1) % PANEL_NTABS;
    build(w);
    w->mode = MODE_PANEL;
    w->redraw = true;
}

void panel_close(Window *w)
{
    w->mode = MODE_NORMAL;
    w->redraw = true;
}

void panel_switch(Window *w, int tab)
{
    if (tab == -1 || tab == -2)
        tab = (w->panel_tab + (tab == -2 ? 1 : PANEL_NTABS - 1)) % PANEL_NTABS;
    w->panel_tab = SDL_clamp(tab, 0, PANEL_NTABS - 1);
    w->sel = w->first = 0;
    build(w);
    w->redraw = true;
}

/* Highlights tab: C V B N M show / hide a colour, c only those with a
 * comment, a all again, e exports what is shown. */
void panel_filter(Window *w, char key)
{
    if (w->panel_tab != PANEL_NOTES) {
        ui_message(w, "filters are for the Highlights tab (4)");
        return;
    }
    const char *colors = "CVBNM";
    NoteFilter *f = &w->panel_filter;
    if (strchr(colors, key) && key)
        f->colors ^= 1u << (strchr(colors, key) - colors);
    else if (key == 'c')
        f->commented = !f->commented;
    else if (key == 'a')
        *f = (NoteFilter){0};
    else if (key == 'e') {
        export_notes(w, cur_view(w), "", false, f);
        return;
    }
    w->sel = w->first = 0;
    build(w);
    w->redraw = true;
}

void panel_move(Window *w, int delta)
{
    int to = SDL_clamp(w->sel + delta, 0, SDL_max(0, w->nitems - 1));
    if (to < w->nitems && w->items[to].header) /* the "Done" line is stepped over */
        to = to + (delta < 0 ? -1 : 1) >= 0 && to + (delta < 0 ? -1 : 1) < w->nitems ? to + (delta < 0 ? -1 : 1) : w->sel;
    w->sel = to;
    w->redraw = true;
}

void panel_activate(Window *w)
{
    if (w->sel >= w->nitems || w->items[w->sel].header)
        return;
    PanelItem it = w->items[w->sel];
    View *v = cur_view(w);
    PanelTab tab = w->panel_tab;
    panel_close(w);
    if (tab == PANEL_OPEN_TABS) {
        w->cur = it.tab;
        w->tabs_flash_until = SDL_GetTicks() + 1000;
        update_title(w);
        return;
    }
    if (it.pos.page < 0)
        return;
    jump_to(w, v, it.pos);
    if (tab == PANEL_NOTES || tab == PANEL_TASKS) {
        char *c = note_thread(v->doc, it.page, it.note);
        if (c)
            ui_message_long(w, 6000, "%s", c);
        free(c);
    }
}

void panel_delete(Window *w)
{
    if (w->sel >= w->nitems)
        return;
    PanelItem *it = &w->items[w->sel];
    Doc *d = cur_view(w)->doc;
    if (w->panel_tab == PANEL_OPEN_TABS) {
        if (w->ntabs == 1) {
            ui_message(w, "the last tab: q closes the window");
            return;
        }
        if (!confirm_close(w, w->tabs[it->tab]))
            return;
        close_tab(w, it->tab);
    } else if (w->panel_tab == PANEL_MARKS) {
        d->marks[it->mark].set = false;
        marks_save(d);
        ui_message(w, "mark %c deleted", 'a' + it->mark);
    } else if (notes_tab(w)) {
        if (it->header)
            return;
        note_delete(d, it->page, it->note);
        ui_message(w, "highlight deleted");
    } else {
        return;
    }
    rebuild(w);
    w->redraw = true;
}

/* t: a highlight or comment goes on the task list, or off it (the note
 * stays); d: a task is done, or to do again. In a cursor or on the panel. */
void note_task_key(Window *w, Doc *d, int page, int note, char key)
{
    notes_page(d, page);
    if (note < 0 || note >= d->slots[page].nnotes)
        return;
    int now = d->slots[page].notes[note].task, next;
    if (key == 't')
        next = now == TASK_NONE ? TASK_OPEN : TASK_NONE;
    else if (now == TASK_NONE) {
        ui_message(w, "not a task: t puts it on the task list (%sl 5)", leader_label());
        return;
    } else
        next = now == TASK_DONE ? TASK_OPEN : TASK_DONE;
    if (!note_set_task(d, page, note, next))
        return;
    int open, done;
    notes_tasks(d, &open, &done);
    if (next == TASK_NONE)
        ui_message(w, "removed from the tasks (the highlight stays)   u undoes it");
    else if (now == TASK_NONE)
        ui_message(w, "added to the tasks: %d to do   d marks it done   %sl 5 lists them", open, leader_label());
    else
        ui_message(w, "%s   %d of %d tasks done   u undoes it", next == TASK_DONE ? "done \u2713" : "to do again", done,
                   open + done);
    w->redraw = true;
}

/* Highlights tab: t. Tasks tab: d or Space done / to do, t off the list,
 * D hides / shows the done ones. The selection stays where it was. */
void panel_task(Window *w, char key)
{
    if (key == 'D') {
        if (w->panel_tab != PANEL_TASKS)
            return;
        w->panel_hide_done = !w->panel_hide_done;
        build(w);
        w->redraw = true;
        return;
    }
    if (!notes_tab(w) || w->sel >= w->nitems || w->items[w->sel].header)
        return;
    if (w->panel_tab == PANEL_NOTES && key != 't')
        return;
    PanelItem *it = &w->items[w->sel];
    note_task_key(w, cur_view(w)->doc, it->page, it->note, key == ' ' ? 'd' : key);
    int sel = w->sel;
    rebuild(w);
    w->sel = SDL_clamp(sel, 0, SDL_max(0, w->nitems - 1)); /* the same place: the next task comes under it */
    if (w->sel < w->nitems && w->items[w->sel].header)
        panel_move(w, w->sel + 1 < w->nitems ? 1 : -1);
}

void panel_delete_comment(Window *w)
{
    if (w->panel_tab != PANEL_NOTES || w->sel >= w->nitems)
        return;
    PanelItem *it = &w->items[w->sel];
    Doc *d = cur_view(w)->doc;
    char *c = note_comment(d, it->page, it->note);
    if (!c) {
        ui_message(w, "this highlight has no comment");
        return;
    }
    free(c);
    note_set_comment(d, it->page, it->note, "");
    ui_message(w, "comment deleted");
    rebuild(w);
}

static void renamed(Window *w, const char *text)
{
    Doc *d = cur_view(w)->doc;
    PanelItem *it = &w->items[w->prompt_arg];
    if (w->panel_tab == PANEL_MARKS) {
        snprintf(d->marks[it->mark].name, sizeof d->marks[it->mark].name, "%s", text);
        marks_save(d);
    } else if (notes_tab(w)) {
        note_set_comment(d, it->page, it->note, text);
    }
    rebuild(w);
}

void panel_rename(Window *w)
{
    if (w->sel >= w->nitems)
        return;
    PanelItem *it = &w->items[w->sel];
    Doc *d = cur_view(w)->doc;
    if (w->panel_tab == PANEL_MARKS) {
        prompt_open(w, "mark name: ", d->marks[it->mark].name, renamed, w->sel);
    } else if (notes_tab(w) && !it->header) {
        char *c = note_comment(d, it->page, it->note);
        prompt_open(w, "comment: ", c ? c : "", renamed, w->sel);
        comment_editor_open(w, -1, (PRect){0}); /* centred, over the panel */
        free(c);
    }
}

/* ---------- drawing ---------- */
typedef struct {
    Window *w;
    const Theme *t;
    float x, y, bw;          /* box */
    int pad, lh;             /* padding, line height */
} Box;

static int row_height(Box *b, PanelItem *it)
{
    return it->sub[0] ? 2 * b->lh + b->pad / 2 : b->lh + b->pad / 2;
}

static void draw_tabs(Box *b, Window *w)
{
    const Theme *t = b->t;
    float x = b->x + b->pad, y = b->y + b->pad / 2.0f;
    for (int k = 0; k < PANEL_NTABS; k++) {
        char label[64];
        bool filtered = k == PANEL_NOTES && (w->panel_filter.colors || w->panel_filter.commented);
        int open = 0, done = 0;
        if (k == PANEL_TASKS && w->panel_counts[k] && cur_view(w))
            notes_tasks(cur_view(w)->doc, &open, &done);
        if (filtered && k == (int)w->panel_tab)
            snprintf(label, sizeof label, "%s  %d of %d", tab_names[k], w->nitems, w->panel_counts[k]);
        else if (k == PANEL_TASKS && open + done) /* how many are done */
            snprintf(label, sizeof label, "%s  %d/%d", tab_names[k], done, open + done);
        else
            snprintf(label, sizeof label, "%s  %d", tab_names[k], w->panel_counts[k]);
        int tw = font_width(w->font, label);
        bool cur = k == (int)w->panel_tab;
        if (cur)
            ui_fill(w, x - b->pad / 2.0f, y + b->lh + 1, (float)(tw + b->pad), 2 * w->density, t->tab_accent, 255);
        font_draw(w->font, x, y, label, cur ? t->panel_fg : t->panel_header);
        x += tw + 2.5f * b->pad;
    }
}

static void draw_row(Box *b, PanelItem *it, float y, bool sel)
{
    Window *w = b->w;
    const Theme *t = b->t;
    int h = row_height(b, it);
    float x = b->x + b->pad, right = b->x + b->bw - b->pad;
    if (it->header) { /* "Done · 3", with a rule */
        float ty = y + b->pad / 4.0f;
        font_draw(w->font, x, ty, it->text, t->panel_header);
        float after = x + font_width(w->font, it->text) + b->pad;
        ui_fill(w, after, ty + b->lh / 2.0f, right - after, SDL_max(1.0f, w->density), t->panel_header, 90);
        return;
    }
    if (sel)
        ui_fill(w, b->x + 2, y, b->bw - 4, (float)h, t->panel_selected_bg, 255);
    uint32_t fg = sel ? t->panel_selected_fg : t->panel_fg, dim = sel ? t->panel_selected_fg : t->panel_header;
    float ty = y + b->pad / 4.0f;
    /* page number, right-aligned */
    char page[48] = "";
    if (it->page >= 0 && it->when[0])
        snprintf(page, sizeof page, "%s   p. %d", it->when, it->page + 1);
    else if (it->page >= 0)
        snprintf(page, sizeof page, "p. %d", it->page + 1);
    int pw = font_width(w->font, page);
    font_draw(w->font, right - pw, ty, page, dim);
    /* left marker: mark letter, note colour, or contents indentation */
    if (b->w->panel_tab == PANEL_OPEN_TABS) {
        float s = b->lh * 0.45f; /* a dot on the active tab */
        if (it->tab == w->cur)
            ui_fill(w, x + (b->lh - s) / 2, ty + (b->lh - s) / 2, s, s, sel ? t->panel_selected_fg : t->tab_accent, 255);
        x += b->lh + b->pad / 2.0f;
    } else if (b->w->panel_tab == PANEL_MARKS) {
        char letter[2] = {(char)('a' + it->mark), 0};
        float bx = x, bs = (float)b->lh;
        ui_fill(w, bx, ty, bs, bs, t->tab_accent, 255);
        font_draw(w->font, bx + (bs - font_width(w->font, letter)) / 2, ty, letter, t->panel_bg);
        x += bs + b->pad;
    } else if (b->w->panel_tab == PANEL_TASKS) { /* the check box first, then the colour */
        float s = b->lh * 0.8f, c = b->lh * 0.5f;
        ui_task_box(w, x, ty + (b->lh - s) / 2, s, it->task == TASK_DONE, sel ? t->panel_selected_fg : t->tab_accent,
                    sel ? t->panel_selected_bg : t->panel_bg);
        x += s + b->pad / 2.0f;
        ui_fill(w, x, ty + (b->lh - c) / 2, c, c, it->color, 255);
        x += c + b->pad;
        if (it->task == TASK_DONE && !sel)
            fg = dim; /* done: it steps back */
    } else if (b->w->panel_tab == PANEL_NOTES) {
        float s = b->lh * 0.7f;
        ui_fill(w, x, ty + (b->lh - s) / 2, s, s, it->color, 255);
        x += s + b->pad;
        if (it->task != TASK_NONE) { /* on the task list: a small box */
            float c = b->lh * 0.5f;
            ui_task_box(w, x, ty + (b->lh - c) / 2, c, it->task == TASK_DONE, sel ? t->panel_selected_fg : t->panel_header,
                        sel ? t->panel_selected_bg : t->panel_bg);
            x += c + b->pad / 2.0f;
        }
    } else {
        x += it->depth * 2.0f * font_width(w->font, " ");
        if (it->depth == 0)
            fg = sel ? t->panel_selected_fg : t->tab_accent; /* chapters stand out */
    }
    int avail = (int)(right - pw - b->pad - x);
    char line[264];
    SDL_strlcpy(line, it->text, 256);
    font_fit(w->font, line, avail);
    font_draw(w->font, x, ty, line, fg);
    if (it->sub[0]) {
        SDL_strlcpy(line, it->sub, 256);
        font_fit(w->font, line, avail);
        font_draw(w->font, x, ty + b->lh, line, dim);
    }
}

static const char *empty_hint(Window *w, char *buf, size_t cap)
{
    if (w->panel_tab == PANEL_MARKS)
        snprintf(buf, cap, "No marks yet. Mark the current position with %sm and a letter.", leader_label());
    else if (w->panel_tab == PANEL_TASKS)
        snprintf(buf, cap, "No tasks. t on a highlight (here in Highlights, or in a cursor) adds it to this list.");
    else if (w->panel_tab == PANEL_NOTES && (w->panel_filter.colors || w->panel_filter.commented))
        snprintf(buf, cap, "No highlights of that kind. a shows them all.");
    else if (w->panel_tab == PANEL_NOTES)
        snprintf(buf, cap, "No highlights. In a cursor (v or Ctrl-v), select with v, then C V B N M or c.");
    else
        snprintf(buf, cap, "This PDF has no summary (outline).");
    return buf;
}

static const char *footer(Window *w)
{
    static char filtered[400];
    if (w->panel_tab == PANEL_NOTES) {
        char which[160];
        note_filter_describe(&w->panel_filter, which, sizeof which);
        snprintf(filtered, sizeof filtered, "%s%s%sC V B N M colour   c with comments%s   e export %s   t task   Enter go   r comment   x delete",
                 which[0] ? "only " : "", which, which[0] ? "   " : "", which[0] ? "   a all" : "",
                 which[0] ? "these" : "");
        return filtered;
    }
    static const char *keys[PANEL_NTABS] = {
        [PANEL_OPEN_TABS] = "Enter switch to it   x close it   Tab next panel tab   Esc close",
        [PANEL_CONTENTS] = "Enter go   j/k move   Tab next panel tab   Esc close",
        [PANEL_MARKS] = "Enter go   r rename   x delete   Tab next panel tab   Esc close",
        [PANEL_NOTES] = "Enter go   r comment   d delete comment   x delete highlight   Tab next panel tab   Esc close",
        [PANEL_TASKS] = "d / Space done   t off the list   D hide done   Enter go   r comment   x delete   Esc close",
    };
    return keys[w->panel_tab];
}

void panel_draw(Window *w)
{
    const Theme *t = wtheme(w);
    Box b = {w, t, 0, 0, 0, (int)(12 * w->density), font_height(w->font) + (int)(4 * w->density)};
    b.bw = (float)SDL_min(w->W - 2 * b.pad, (int)(820 * w->density));
    float bh = (w->H - top_of(w) - ui_bar_height(w)) * 0.8f;
    b.x = (w->W - b.bw) / 2;
    b.y = top_of(w) + (w->H - top_of(w) - ui_bar_height(w) - bh) / 2;
    ui_fill(w, b.x, b.y, b.bw, bh, t->panel_bg, 255);
    ui_frame(w, b.x, b.y, b.bw, bh, SDL_max(1.0f, w->density), t->panel_header);
    draw_tabs(&b, w);
    float list_top = b.y + b.lh + b.pad * 1.5f, list_bottom = b.y + bh - b.lh - b.pad;
    ui_fill(w, b.x, list_top - b.pad / 2.0f, b.bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    ui_fill(w, b.x, list_bottom, b.bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    char help[200];
    SDL_strlcpy(help, footer(w), sizeof help - 4);
    font_fit(w->font, help, (int)b.bw - 2 * b.pad);
    font_draw(w->font, b.x + b.pad, list_bottom + b.pad / 2.0f, help, t->panel_header);
    if (!w->nitems) {
        char msg[200];
        font_draw(w->font, b.x + b.pad, list_top + b.pad, empty_hint(w, msg, sizeof msg), t->panel_header);
        return;
    }
    /* keep the selection visible with rows of different heights */
    float avail = list_bottom - list_top;
    if (w->sel < w->first)
        w->first = w->sel;
    for (;;) {
        float used = 0;
        for (int i = w->first; i <= w->sel; i++)
            used += row_height(&b, &w->items[i]);
        if (used <= avail || w->first >= w->sel)
            break;
        w->first++;
    }
    float y = list_top;
    for (int i = w->first; i < w->nitems; i++) {
        int h = row_height(&b, &w->items[i]);
        if (y + h > list_bottom)
            break;
        draw_row(&b, &w->items[i], y, i == w->sel);
        y += h;
    }
    {
        char pos[32];
        int at = 0, total = 0; /* the "Done" line is not an entry */
        for (int i = 0; i < w->nitems; i++)
            if (!w->items[i].header)
                total++, at += i <= w->sel;
        snprintf(pos, sizeof pos, "%d/%d", total ? SDL_max(1, at) : 0, total);
        font_draw(w->font, b.x + b.bw - b.pad - font_width(w->font, pos), b.y + b.pad / 2.0f, pos, t->panel_header);
    }
}
