/* Form fields: Ctrl-j / Ctrl-k navigation, typing, mouse clicks. */
#include <string.h>

#include "fpdf_fwlevent.h"
#include "nomadys.h"

static void focus_end(Doc *d, int page)
{
    /* start at the end of existing text, ready to append or erase */
    FPDF_PAGE p = doc_page(d, page);
    FORM_OnKeyDown(d->form, p, FWL_VKEY_End, 0);
    FORM_OnKeyUp(d->form, p, FWL_VKEY_End, 0);
}

void form_step(Window *w, int dir)
{
    View *v = cur_view(w);
    Doc *d = v->doc;
    if (!doc_has_forms(d)) {
        ui_message(w, "this PDF has no form");
        return;
    }
    int page, idx;
    if (d->focus_page >= 0) {
        page = d->focus_page;
        idx = d->focus_widget;
    } else {
        page = view_top_page(v);
        doc_page_widgets(d, page);
        idx = dir > 0 ? -1 : d->slots[page].nwidgets;
    }
    for (int visited = 0; visited <= d->npages;) {
        idx += dir;
        doc_page_widgets(d, page);
        if (idx < 0 || idx >= d->slots[page].nwidgets) {
            page = (page + dir + d->npages) % d->npages;
            doc_page_widgets(d, page);
            idx = dir > 0 ? -1 : d->slots[page].nwidgets;
            visited++;
            continue;
        }
        if (doc_focus_widget(d, page, idx)) {
            focus_end(d, page);
            w->mode = MODE_FORM;
            view_show_rect(v, page, d->slots[page].widgets[idx].r, w->W, w->H, top_of(w));
            w->redraw = true;
            return;
        }
    }
    ui_message(w, "no fillable field");
}

void form_leave(Window *w)
{
    doc_kill_focus(cur_view(w)->doc);
    w->mode = MODE_NORMAL;
    w->redraw = true;
}

static const struct { const char *tok; int vkey; } form_keys[] = {
    {"<Del>", FWL_VKEY_Delete}, {"<Left>", FWL_VKEY_Left}, {"<Right>", FWL_VKEY_Right},
    {"<Up>", FWL_VKEY_Up}, {"<Down>", FWL_VKEY_Down}, {"<Home>", FWL_VKEY_Home},
    {"<End>", FWL_VKEY_End}, {"<Enter>", FWL_VKEY_Return},
};

/* Non-text keys while a field is focused. */
void form_special(Window *w, const char *tok)
{
    Doc *d = cur_view(w)->doc;
    FPDF_PAGE p = d->focus_page >= 0 ? doc_page(d, d->focus_page) : NULL;
    if (!p) {
        w->mode = MODE_NORMAL;
        return;
    }
    if (!strcmp(tok, "<Esc>")) {
        form_leave(w);
    } else if (!strcmp(tok, "<Tab>") || !strcmp(tok, "<C-j>")) {
        form_step(w, 1);
    } else if (!strcmp(tok, "<S-Tab>") || !strcmp(tok, "<C-k>")) {
        form_step(w, -1);
    } else if (!strcmp(tok, "<C-s>")) {
        save(w, cur_view(w));
    } else if (!strcmp(tok, "<C-a>")) {
        FORM_SelectAllText(d->form, p);
    } else if (!strcmp(tok, "<C-z>")) {
        FORM_Undo(d->form, p);
    } else if (!strcmp(tok, "<C-v>")) {
        char *clip = SDL_GetClipboardText();
        if (clip) {
            static unsigned short u[8192];
            utf8_to_utf16(clip, u, 8192);
            FORM_ReplaceSelection(d->form, p, u);
            SDL_free(clip);
        }
    } else if (!strcmp(tok, "<BS>")) {
        /* PDFium edits delete backwards on the Backspace character, not the key */
        FORM_OnChar(d->form, p, FWL_VKEY_Back, 0);
    } else {
        for (size_t i = 0; i < SDL_arraysize(form_keys); i++) {
            if (strcmp(tok, form_keys[i].tok))
                continue;
            FORM_OnKeyDown(d->form, p, form_keys[i].vkey, 0);
            if (form_keys[i].vkey == FWL_VKEY_Return)
                FORM_OnChar(d->form, p, '\r', 0);
            FORM_OnKeyUp(d->form, p, form_keys[i].vkey, 0);
        }
    }
}

void form_text(Window *w, const char *text)
{
    Doc *d = cur_view(w)->doc;
    FPDF_PAGE p = d->focus_page >= 0 ? doc_page(d, d->focus_page) : NULL;
    if (!p)
        return;
    unsigned short u[64];
    int n = utf8_to_utf16(text, u, 64);
    for (int i = 0; i < n; i++)
        FORM_OnChar(d->form, p, u[i], 0);
}

void form_click(Window *w, float x, float y, bool down)
{
    View *v = cur_view(w);
    Doc *d = v->doc;
    int page;
    double px, py;
    if (!d->form || !screen_to_page(v, x, y, top_of(w), &page, &px, &py))
        return;
    FPDF_PAGE p = doc_page(d, page);
    if (down) {
        FORM_OnLButtonDown(d->form, p, 0, px, py);
        return;
    }
    FORM_OnLButtonUp(d->form, p, 0, px, py);
    int fpage = -1;
    FPDF_ANNOTATION a = NULL;
    if (FORM_GetFocusedAnnot(d->form, &fpage, &a) && a) {
        FS_RECTF r;
        FPDFAnnot_GetRect(a, &r);
        FPDFPage_CloseAnnot(a);
        doc_page_widgets(d, fpage);
        PageSlot *s = &d->slots[fpage];
        for (int i = 0; i < s->nwidgets; i++) {
            if (SDL_fabsf(s->widgets[i].r.l - SDL_min(r.left, r.right)) < 0.5f &&
                SDL_fabsf(s->widgets[i].r.t - SDL_max(r.top, r.bottom)) < 0.5f) {
                d->focus_page = fpage;
                d->focus_widget = i;
                w->mode = MODE_FORM;
            }
        }
    } else if (w->mode == MODE_FORM) {
        d->focus_page = -1;
        w->mode = MODE_NORMAL;
    }
    w->redraw = true;
}

void form_draw_focus(Window *w, View *v, int top)
{
    Doc *d = v->doc;
    if (d->focus_page < 0 || !d->slots[d->focus_page].widgets_ready)
        return;
    PRect r = d->slots[d->focus_page].widgets[d->focus_widget].r;
    float x0, y0, x1, y1, t = SDL_max(2.0f, w->density * 1.5f);
    page_to_screen(v, d->focus_page, r.l, r.t, &x0, &y0, top);
    page_to_screen(v, d->focus_page, r.r, r.b, &x1, &y1, top);
    ui_frame(w, SDL_min(x0, x1) - t, SDL_min(y0, y1) - t, SDL_fabsf(x1 - x0) + 2 * t,
             SDL_fabsf(y1 - y0) + 2 * t, t, wtheme(w)->form_focus);
}
