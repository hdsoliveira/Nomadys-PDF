/* The panel ('\l'): three tabs — marks, highlights/comments, and the PDF's
 * own contents — each a list navigated with j/k and opened with Enter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpdf_doc.h"
#include "nomadys.h"

static const char *tab_names[PANEL_TABS] = {"Marcas", "Destaques", "Sumário"};

/* ---------- building the lists ---------- */
static PanelItem *add(Window *w, int *cap)
{
    if (w->nitems == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        w->items = realloc(w->items, *cap * sizeof *w->items);
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
        all = realloc(all, len + tl + 2);
        if (len)
            all[len++] = ' ';
        memcpy(all + len, t, tl + 1);
        len += tl;
        free(t);
    }
    if (all && !*all) { /* an area over a figure: no text */
        free(all);
        all = NULL;
    }
    return all;
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
        snprintf(it->text, sizeof it->text, "%s", d->marks[m].name[0] ? d->marks[m].name : "(sem nome)");
    }
}

static void build_notes(Window *w, Doc *d, int *cap)
{
    for (int page = 0; page < d->npages; page++) {
        notes_page(d, page);
        PageSlot *s = &d->slots[page];
        for (int i = 0; i < s->nnotes; i++) {
            PanelItem *it = add(w, cap);
            it->page = page;
            it->note = i;
            it->color = s->notes[i].color;
            it->pos = (Pos){page, SDL_max(0.0f, 1 - s->notes[i].r.t / d->ph[page] - 0.05f)};
            char *c = note_comment(d, page, i), *t = covered_text(d, page, &s->notes[i]);
            if (c) { /* the comment first, what it refers to below */
                snprintf(it->text, sizeof it->text, "%s", c);
                if (t)
                    snprintf(it->sub, sizeof it->sub, "“%s”", t);
            } else {
                snprintf(it->text, sizeof it->text, t ? "“%s”" : "%s", t ? t : "(área destacada)");
            }
            one_line(it->text);
            one_line(it->sub);
            free(c);
            free(t);
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
    if (w->panel_tab == PANEL_MARKS)
        build_marks(w, d, &cap);
    else if (w->panel_tab == PANEL_NOTES)
        build_notes(w, d, &cap);
    else
        build_contents(w, d, NULL, 0, &cap, &budget);
    w->sel = SDL_clamp(w->sel, 0, SDL_max(0, w->nitems - 1));
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
    int marks = 0, notes = 0, budget = 5000;
    for (int m = 0; m < NMARKS; m++)
        marks += d->marks[m].set;
    for (int page = 0; page < d->npages; page++) {
        notes_page(d, page);
        notes += d->slots[page].nnotes;
    }
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
    for (int k = 0; k < PANEL_TABS && !w->panel_counts[w->panel_tab]; k++)
        w->panel_tab = (w->panel_tab + 1) % PANEL_TABS;
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
        tab = (w->panel_tab + (tab == -2 ? 1 : PANEL_TABS - 1)) % PANEL_TABS;
    w->panel_tab = SDL_clamp(tab, 0, PANEL_TABS - 1);
    w->sel = w->first = 0;
    build(w);
    w->redraw = true;
}

void panel_move(Window *w, int delta)
{
    w->sel = SDL_clamp(w->sel + delta, 0, SDL_max(0, w->nitems - 1));
    w->redraw = true;
}

void panel_activate(Window *w)
{
    if (w->sel >= w->nitems)
        return;
    PanelItem it = w->items[w->sel];
    View *v = cur_view(w);
    PanelTab tab = w->panel_tab;
    panel_close(w);
    if (it.pos.page < 0)
        return;
    jump_to(w, v, it.pos);
    if (tab == PANEL_NOTES) {
        char *c = note_comment(v->doc, it.page, it.note);
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
    if (w->panel_tab == PANEL_MARKS) {
        d->marks[it->mark].set = false;
        marks_save(d);
        ui_message(w, "marca %c apagada", 'a' + it->mark);
    } else if (w->panel_tab == PANEL_NOTES) {
        note_delete(d, it->page, it->note);
        ui_message(w, "destaque apagado");
    } else {
        return;
    }
    rebuild(w);
    w->redraw = true;
}

void panel_delete_comment(Window *w)
{
    if (w->panel_tab != PANEL_NOTES || w->sel >= w->nitems)
        return;
    PanelItem *it = &w->items[w->sel];
    Doc *d = cur_view(w)->doc;
    char *c = note_comment(d, it->page, it->note);
    if (!c) {
        ui_message(w, "este destaque não tem comentário");
        return;
    }
    free(c);
    note_set_comment(d, it->page, it->note, "");
    ui_message(w, "comentário apagado");
    rebuild(w);
}

static void renamed(Window *w, const char *text)
{
    Doc *d = cur_view(w)->doc;
    PanelItem *it = &w->items[w->prompt_arg];
    if (w->panel_tab == PANEL_MARKS) {
        snprintf(d->marks[it->mark].name, sizeof d->marks[it->mark].name, "%s", text);
        marks_save(d);
    } else if (w->panel_tab == PANEL_NOTES) {
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
        prompt_open(w, "nome da marca: ", d->marks[it->mark].name, renamed, w->sel);
    } else if (w->panel_tab == PANEL_NOTES) {
        char *c = note_comment(d, it->page, it->note);
        prompt_open(w, "comentário: ", c ? c : "", renamed, w->sel);
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
    for (int k = 0; k < PANEL_TABS; k++) {
        char label[64];
        snprintf(label, sizeof label, "%d %s  %d", k + 1, tab_names[k], w->panel_counts[k]);
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
    if (sel)
        ui_fill(w, b->x + 2, y, b->bw - 4, (float)h, t->panel_selected_bg, 255);
    uint32_t fg = sel ? t->panel_selected_fg : t->panel_fg, dim = sel ? t->panel_selected_fg : t->panel_header;
    float ty = y + b->pad / 4.0f;
    /* page number, right-aligned */
    char page[16] = "";
    if (it->page >= 0)
        snprintf(page, sizeof page, "p. %d", it->page + 1);
    int pw = font_width(w->font, page);
    font_draw(w->font, right - pw, ty, page, dim);
    /* left marker: mark letter, note colour, or contents indentation */
    if (b->w->panel_tab == PANEL_MARKS) {
        char letter[2] = {(char)('a' + it->mark), 0};
        float bx = x, bs = (float)b->lh;
        ui_fill(w, bx, ty, bs, bs, t->tab_accent, 255);
        font_draw(w->font, bx + (bs - font_width(w->font, letter)) / 2, ty, letter, t->panel_bg);
        x += bs + b->pad;
    } else if (b->w->panel_tab == PANEL_NOTES) {
        float s = b->lh * 0.7f;
        ui_fill(w, x, ty + (b->lh - s) / 2, s, s, it->color, 255);
        x += s + b->pad;
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
        snprintf(buf, cap, "Nenhuma marca ainda. Grave a posição atual com %sm e uma letra.", leader_label());
    else if (w->panel_tab == PANEL_NOTES)
        snprintf(buf, cap, "Nenhum destaque. No cursor (v ou Ctrl-v), selecione com v e use C V B N M ou c.");
    else
        snprintf(buf, cap, "Este PDF não tem sumário.");
    return buf;
}

static const char *footer(Window *w)
{
    static const char *keys[PANEL_TABS] = {
        "Enter ir   r renomear   x apagar   Tab outra aba   Esc fechar",
        "Enter ir   r comentar   d apagar comentário   x apagar destaque   Tab outra aba   Esc fechar",
        "Enter ir   j/k mover   Tab outra aba   Esc fechar",
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
        snprintf(pos, sizeof pos, "%d/%d", w->sel + 1, w->nitems);
        font_draw(w->font, b.x + b.bw - b.pad - font_width(w->font, pos), b.y + b.pad / 2.0f, pos, t->panel_header);
    }
}
