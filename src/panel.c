/* The panel ('\l'): marks, highlights/comments and the PDF's own contents,
 * navigated with j/k and opened with Enter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpdf_doc.h"
#include "nomadys.h"

enum { ITEM_HEADER, ITEM_MARK, ITEM_NOTE, ITEM_CONTENTS };

static PanelItem *add(Window *w, int *cap, int kind)
{
    if (w->nitems == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        w->items = realloc(w->items, *cap * sizeof *w->items);
    }
    PanelItem *it = &w->items[w->nitems++];
    memset(it, 0, sizeof *it);
    it->kind = kind;
    return it;
}

static void one_line(char *s)
{
    for (; *s; s++)
        if (*s == '\r' || *s == '\n' || *s == '\t')
            *s = ' ';
}

/* Text covered by a note, for listing highlights without a comment. */
static char *covered_text(Doc *d, int page, Note *n)
{
    FPDF_TEXTPAGE tp = doc_text(d, page);
    if (!tp)
        return NULL;
    PRect r = n->quads[0];
    unsigned short u[160];
    int got = FPDFText_GetBoundedText(tp, r.l, r.t, r.r, r.b, u, 159);
    return got > 0 ? utf16_to_utf8(u, got) : NULL;
}

static void add_notes(Window *w, int *cap, Doc *d)
{
    for (int page = 0; page < d->npages; page++) {
        notes_page(d, page);
        PageSlot *s = &d->slots[page];
        for (int i = 0; i < s->nnotes; i++) {
            PanelItem *it = add(w, cap, ITEM_NOTE);
            it->page = page;
            it->note = i;
            it->pos = (Pos){page, SDL_max(0.0f, 1 - s->notes[i].r.t / d->ph[page] - 0.05f)};
            char *c = note_comment(d, page, i), *t = c ? NULL : covered_text(d, page, &s->notes[i]);
            snprintf(it->text, sizeof it->text, "p.%-4d %s%s", page + 1, c ? "" : "“",
                     c ? c : t ? t : "(destaque)");
            if (!c && t)
                strncat(it->text, "”", sizeof it->text - strlen(it->text) - 1);
            one_line(it->text);
            free(c);
            free(t);
        }
    }
}

static void add_outline(Window *w, int *cap, Doc *d, FPDF_BOOKMARK parent, int depth, int *budget)
{
    for (FPDF_BOOKMARK b = FPDFBookmark_GetFirstChild(d->pdf, parent); b && (*budget)-- > 0;
         b = FPDFBookmark_GetNextSibling(d->pdf, b)) {
        PanelItem *it = add(w, cap, ITEM_CONTENTS);
        it->depth = depth;
        unsigned short u[128] = {0};
        unsigned long bytes = FPDFBookmark_GetTitle(b, u, sizeof u - 2);
        char *t = utf16_to_utf8(u, bytes >= sizeof u - 2 ? 0 : (int)(bytes / 2));
        FPDF_DEST dest = FPDFBookmark_GetDest(d->pdf, b);
        if (!dest && FPDFBookmark_GetAction(b))
            dest = FPDFAction_GetDest(d->pdf, FPDFBookmark_GetAction(b));
        it->pos.page = dest ? FPDFDest_GetDestPageIndex(d->pdf, dest) : -1;
        FPDF_BOOL hx, hy, hz;
        FS_FLOAT x, y, z;
        if (it->pos.page >= 0 && FPDFDest_GetLocationInPage(dest, &hx, &hy, &hz, &x, &y, &z) && hy)
            it->pos.y = SDL_max(0.0f, 1 - y / d->ph[it->pos.page]);
        snprintf(it->text, sizeof it->text, "%*s%s", depth * 2, "", t);
        if (it->pos.page >= 0) {
            char pg[16];
            snprintf(pg, sizeof pg, "  p.%d", it->pos.page + 1);
            strncat(it->text, pg, sizeof it->text - strlen(it->text) - 1);
        }
        one_line(it->text);
        free(t);
        if (depth < 6)
            add_outline(w, cap, d, b, depth + 1, budget);
    }
}

static void build(Window *w)
{
    Doc *d = cur_view(w)->doc;
    int cap = 0;
    w->nitems = 0;
    snprintf(add(w, &cap, ITEM_HEADER)->text, 256, "Marcas  (%sm<letra> grava, m<letra> salta)", leader_label());
    for (int m = 0; m < NMARKS; m++) {
        if (!d->marks[m].set)
            continue;
        PanelItem *it = add(w, &cap, ITEM_MARK);
        it->mark = m;
        it->pos = d->marks[m].pos;
        snprintf(it->text, sizeof it->text, "%c  p.%-4d %s", 'a' + m, it->pos.page + 1, d->marks[m].name);
    }
    int before = w->nitems;
    snprintf(add(w, &cap, ITEM_HEADER)->text, 256, "Destaques e comentários");
    add_notes(w, &cap, d);
    if (w->nitems == before + 1)
        w->nitems--;
    before = w->nitems;
    snprintf(add(w, &cap, ITEM_HEADER)->text, 256, "Sumário do PDF");
    int budget = 5000;
    add_outline(w, &cap, d, NULL, 0, &budget);
    if (w->nitems == before + 1)
        w->nitems--;
}

void panel_open(Window *w)
{
    build(w);
    w->sel = 0;
    w->first = 0;
    panel_move(w, 1);
    w->mode = MODE_PANEL;
    w->redraw = true;
}

void panel_close(Window *w)
{
    w->mode = MODE_NORMAL;
    w->redraw = true;
}

void panel_move(Window *w, int delta)
{
    int dir = delta >= 0 ? 1 : -1, i = w->sel, moved = 0;
    if (delta == 0)
        return;
    for (int k = i + dir; k >= 0 && k < w->nitems && moved < SDL_abs(delta); k += dir) {
        if (w->items[k].kind != ITEM_HEADER) {
            i = k;
            moved++;
        }
    }
    if (w->items && w->items[i].kind == ITEM_HEADER) /* only headers above */
        for (int k = i; k < w->nitems; k++)
            if (w->items[k].kind != ITEM_HEADER) {
                i = k;
                break;
            }
    w->sel = i;
    w->redraw = true;
}

void panel_activate(Window *w)
{
    if (w->sel >= w->nitems || w->items[w->sel].kind == ITEM_HEADER)
        return;
    PanelItem it = w->items[w->sel];
    View *v = cur_view(w);
    panel_close(w);
    if (it.pos.page < 0)
        return;
    jump_to(w, v, it.pos);
    if (it.kind == ITEM_NOTE) {
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
    if (it->kind == ITEM_MARK) {
        d->marks[it->mark].set = false;
        marks_save(d);
        ui_message(w, "marca %c apagada", 'a' + it->mark);
    } else if (it->kind == ITEM_NOTE) {
        note_delete(d, it->page, it->note);
        ui_message(w, "destaque apagado");
    } else {
        return;
    }
    int sel = w->sel;
    build(w);
    w->sel = SDL_min(sel, w->nitems - 1);
    panel_move(w, 0);
    if (w->sel >= 0 && w->sel < w->nitems && w->items[w->sel].kind == ITEM_HEADER)
        panel_move(w, 1);
    w->redraw = true;
}

/* Removes the comment of the selected note and keeps the highlight. */
void panel_delete_comment(Window *w)
{
    if (w->sel >= w->nitems || w->items[w->sel].kind != ITEM_NOTE) {
        ui_message(w, "selecione um destaque");
        return;
    }
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
    int sel = w->sel;
    build(w);
    w->sel = SDL_min(sel, w->nitems - 1);
}

static void renamed(Window *w, const char *text)
{
    Doc *d = cur_view(w)->doc;
    PanelItem *it = &w->items[w->prompt_arg];
    if (it->kind == ITEM_MARK) {
        snprintf(d->marks[it->mark].name, sizeof d->marks[it->mark].name, "%s", text);
        marks_save(d);
    } else if (it->kind == ITEM_NOTE) {
        note_set_comment(d, it->page, it->note, text);
    }
    int sel = w->sel;
    build(w);
    w->sel = sel;
}

void panel_rename(Window *w)
{
    if (w->sel >= w->nitems)
        return;
    PanelItem *it = &w->items[w->sel];
    Doc *d = cur_view(w)->doc;
    if (it->kind == ITEM_MARK) {
        prompt_open(w, "nome da marca: ", d->marks[it->mark].name, renamed, w->sel);
    } else if (it->kind == ITEM_NOTE) {
        char *c = note_comment(d, it->page, it->note);
        prompt_open(w, "comentário: ", c ? c : "", renamed, w->sel);
        free(c);
    }
}

void panel_draw(Window *w)
{
    const Theme *t = wtheme(w);
    int lh = font_height(w->font) + (int)(6 * w->density), pad = (int)(14 * w->density);
    int bw = SDL_min(w->W - 2 * pad, (int)(760 * w->density));
    int rows = SDL_max(3, (int)((w->H - top_of(w) - ui_bar_height(w) - 4 * pad) / lh) - 1);
    int shown = SDL_min(rows, SDL_max(1, w->nitems));
    int bh = (shown + 1) * lh + pad;
    float x = (w->W - bw) / 2.0f, y = top_of(w) + (w->H - top_of(w) - bh) / 3.0f;
    ui_fill(w, x, y, (float)bw, (float)bh, t->panel_bg, 255);
    ui_frame(w, x, y, (float)bw, (float)bh, SDL_max(1.0f, w->density), t->panel_header);
    char help[160] = "j/k mover  Enter ir  r renomear/editar  d apaga comentário  x apaga destaque/marca  Esc fechar";
    font_fit(w->font, help, bw - 2 * pad);
    font_draw(w->font, x + pad, y + pad / 2.0f, help, t->panel_header);
    if (!w->nitems)
        return;
    if (w->sel < w->first)
        w->first = w->sel;
    if (w->sel >= w->first + rows)
        w->first = w->sel - rows + 1;
    if (w->first > 0 && w->items[w->first - 1].kind == ITEM_HEADER && w->sel - (w->first - 1) < rows)
        w->first--;
    for (int i = 0; i < shown && w->first + i < w->nitems; i++) {
        PanelItem *it = &w->items[w->first + i];
        float ry = y + pad / 2.0f + (i + 1) * lh;
        bool sel = w->first + i == w->sel;
        if (sel)
            ui_fill(w, x + 2, ry - 2, (float)bw - 4, (float)lh, t->panel_selected_bg, 255);
        uint32_t fg = it->kind == ITEM_HEADER ? t->panel_header : sel ? t->panel_selected_fg : t->panel_fg;
        char text[264]; /* room for the ellipsis */
        SDL_strlcpy(text, it->kind == ITEM_HEADER ? "" : "  ", 256);
        SDL_strlcat(text, it->text, 256);
        font_fit(w->font, text, bw - 2 * pad);
        font_draw(w->font, x + pad, ry + 1, text, fg);
    }
}
