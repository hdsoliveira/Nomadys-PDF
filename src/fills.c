/* Saved answers for forms: fill in a PDF once, give the answers a name (P80),
 * and next time one menu (\F, or :form P80) fills every blank. Answers are
 * kept by field name, so they fit any PDF that has the same fields. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fpdf_annot.h"
#include "fpdf_fwlevent.h"
#include "nomadys.h"

#define NAME_MAX_LEN 63

typedef struct {
    char *key;    /* the field's name */
    char *export; /* a checkbox or radio button: which one of the field's buttons ("" otherwise) */
    char *value;  /* text, the option chosen, or "1" for a button that is ticked */
} Answer;

typedef struct {
    char *name;
    Answer *a;
    int n;
} Fill;

static void fill_free(Fill *f)
{
    for (int i = 0; i < f->n; i++)
        free(f->a[i].key), free(f->a[i].export), free(f->a[i].value);
    free(f->a);
    free(f->name);
    *f = (Fill){0};
}

static void fill_add(Fill *f, const char *key, const char *export, const char *value)
{
    f->a = xrealloc(f->a, (size_t)(f->n + 1) * sizeof *f->a);
    f->a[f->n++] = (Answer){xstrdup(key), xstrdup(export), xstrdup(value)};
}

/* ---------- the file: one line a thing, tabs and line breaks escaped ---------- */
static void put_escaped(FILE *f, const char *s)
{
    for (; *s; s++)
        switch (*s) {
        case '\\': fputs("\\\\", f); break;
        case '\t': fputs("\\t", f); break;
        case '\n': fputs("\\n", f); break;
        case '\r': fputs("\\r", f); break;
        default: fputc(*s, f);
        }
}

static char *unescape(const char *s)
{
    char *out = xmalloc(strlen(s) + 1), *o = out;
    for (; *s; s++) {
        if (*s == '\\' && s[1]) {
            s++;
            *o++ = *s == 't' ? '\t' : *s == 'n' ? '\n' : *s == 'r' ? '\r' : *s;
        } else {
            *o++ = *s;
        }
    }
    *o = 0;
    return out;
}

/* Every saved set, in the order they were saved; returns how many. */
static int store_load(Fill **out)
{
    char path[4200];
    data_path("forms", path, sizeof path);
    *out = NULL;
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    int n = 0;
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) > 0) {
        line[strcspn(line, "\n")] = 0;
        char *t[4] = {0};
        int k = 0;
        for (char *p = line; k < 4; k++) { /* tab-separated; escapes hide the tabs inside */
            t[k] = p;
            p = strchr(p, '\t');
            if (!p) {
                k++;
                break;
            }
            *p++ = 0;
        }
        if (!strcmp(t[0], "form") && k == 2 && *t[1]) {
            *out = xrealloc(*out, (size_t)(n + 1) * sizeof **out);
            (*out)[n++] = (Fill){unescape(t[1]), NULL, 0};
        } else if (!strcmp(t[0], "a") && k == 4 && n) {
            char *key = unescape(t[1]), *export = unescape(t[2]), *value = unescape(t[3]);
            fill_add(&(*out)[n - 1], key, export, value);
            free(key), free(export), free(value);
        }
    }
    free(line);
    fclose(f);
    return n;
}

static bool store_write(const Fill *all, int n)
{
    char path[4200], tmp[4300];
    data_path("forms", path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp%d", path, (int)getpid());
    FILE *f = fopen(tmp, "w");
    if (!f)
        return false;
    for (int i = 0; i < n; i++) {
        fputs("form\t", f);
        put_escaped(f, all[i].name);
        fputc('\n', f);
        for (int k = 0; k < all[i].n; k++) {
            fputs("a\t", f);
            put_escaped(f, all[i].a[k].key);
            fputc('\t', f);
            put_escaped(f, all[i].a[k].export);
            fputc('\t', f);
            put_escaped(f, all[i].a[k].value);
            fputc('\n', f);
        }
    }
    bool ok = !ferror(f);
    ok &= fclose(f) == 0;
    if (!ok || rename(tmp, path)) {
        unlink(tmp);
        return false;
    }
    return true;
}

static void store_free(Fill *all, int n)
{
    for (int i = 0; i < n; i++)
        fill_free(&all[i]);
    free(all);
}

static int store_find(const Fill *all, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(all[i].name, name))
            return i;
    return -1;
}

/* ---------- the fields of the PDF ---------- */
typedef unsigned long (*FieldString)(FPDF_FORMHANDLE, FPDF_ANNOTATION, FPDF_WCHAR *, unsigned long);

static char *field_string(Doc *d, FPDF_ANNOTATION a, FieldString get)
{
    unsigned long bytes = get(d->form, a, NULL, 0);
    if (bytes < 2)
        return xstrdup("");
    unsigned short *u = xmalloc(bytes + 2);
    get(d->form, a, u, bytes);
    char *s = utf16_to_utf8(u, (int)(bytes / 2));
    free(u);
    return s;
}

/* The label of option i of a choice field. */
static char *option_label(Doc *d, FPDF_ANNOTATION a, int i)
{
    unsigned long bytes = FPDFAnnot_GetOptionLabel(d->form, a, i, NULL, 0);
    if (bytes < 2)
        return xstrdup("");
    unsigned short *u = xmalloc(bytes + 2);
    FPDFAnnot_GetOptionLabel(d->form, a, i, u, bytes);
    char *s = utf16_to_utf8(u, (int)(bytes / 2));
    free(u);
    return s;
}

/* The option a choice field shows: the label of the selected one. */
static char *chosen_option(Doc *d, FPDF_ANNOTATION a)
{
    for (int i = 0, n = FPDFAnnot_GetOptionCount(d->form, a); i < n; i++)
        if (FPDFAnnot_IsOptionSelected(d->form, a, i))
            return option_label(d, a, i);
    return NULL;
}

/* Field names match when they are the same, or the same apart from capitals,
 * spaces and punctuation ("Full name", "full_name"). */
static bool same_name(const char *a, const char *b)
{
    if (!strcmp(a, b))
        return true;
    for (;; a++, b++) {
        while (*a && !isalnum((unsigned char)*a) && (unsigned char)*a < 0x80)
            a++;
        while (*b && !isalnum((unsigned char)*b) && (unsigned char)*b < 0x80)
            b++;
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        if (!*a)
            return true;
    }
}

/* Calls fn for every fillable widget of the PDF, with its annotation open. */
typedef void (*WidgetFn)(Doc *d, int page, int widget, FPDF_ANNOTATION a, int type, void *arg);

static void each_widget(Doc *d, WidgetFn fn, void *arg)
{
    for (int page = 0; page < d->npages; page++) {
        doc_page_widgets(d, page);
        FPDF_PAGE p = doc_page(d, page);
        for (int k = 0; p && k < d->slots[page].nwidgets; k++) {
            FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, d->slots[page].widgets[k].idx);
            if (!a)
                continue;
            fn(d, page, k, a, FPDFAnnot_GetFormFieldType(d->form, a), arg);
            FPDFPage_CloseAnnot(a);
        }
    }
}

static bool is_button(int type) { return type == FPDF_FORMFIELD_CHECKBOX || type == FPDF_FORMFIELD_RADIOBUTTON; }

static void collect_add(Fill *f, const char *key, const char *export, const char *value, bool by_value)
{
    if (!*key || !value || !*value)
        return;
    for (int i = 0; i < f->n; i++) /* the widgets of one field show one value */
        if (!strcmp(f->a[i].key, key) && !strcmp(f->a[i].export, export) && (!by_value || !strcmp(f->a[i].value, value)))
            return;
    fill_add(f, key, export, value);
}

static void collect_one(Doc *d, int page, int widget, FPDF_ANNOTATION a, int type, void *arg)
{
    Fill *f = arg;
    char *key = field_string(d, a, FPDFAnnot_GetFormFieldName), *export = xstrdup(""), *value = NULL;
    if (is_button(type)) {
        bool on = FPDFAnnot_IsChecked(d->form, a);
        if (on) { /* a button is remembered only when ticked or chosen */
            free(export);
            export = field_string(d, a, FPDFAnnot_GetFormFieldExportValue);
            value = xstrdup("1");
        }
    } else if (type == FPDF_FORMFIELD_LISTBOX) { /* every option chosen */
        for (int i = 0, n = FPDFAnnot_GetOptionCount(d->form, a); i < n; i++)
            if (FPDFAnnot_IsOptionSelected(d->form, a, i)) {
                char *label = option_label(d, a, i);
                collect_add(f, key, export, label, true);
                free(label);
            }
    } else if (type == FPDF_FORMFIELD_COMBOBOX) {
        value = chosen_option(d, a);
        if (!value)
            value = field_string(d, a, FPDFAnnot_GetFormFieldValue); /* typed into an editable one */
    } else {
        value = field_string(d, a, FPDFAnnot_GetFormFieldValue);
    }
    collect_add(f, key, export, value, false);
    free(key), free(export), free(value);
}

/* What is filled in the PDF now. */
static Fill collect(Doc *d)
{
    Fill f = {0};
    doc_kill_focus(d); /* a field being typed in is committed */
    each_widget(d, collect_one, &f);
    return f;
}

typedef struct {
    const Fill *f;
    bool *used;
    int changed;
    bool only_empty; /* leave fields that already have something */
} Apply;

static void set_text(Doc *d, FPDF_PAGE p, const char *s)
{
    unsigned short *u = xmalloc((strlen(s) + 2) * sizeof *u);
    utf8_to_utf16(s, u, (int)strlen(s) + 2);
    FORM_SelectAllText(d->form, p);
    FORM_ReplaceSelection(d->form, p, u);
    free(u);
}

static void apply_one(Doc *d, int page, int widget, FPDF_ANNOTATION a, int type, void *arg)
{
    Apply *ap = arg;
    char *key = field_string(d, a, FPDFAnnot_GetFormFieldName), *export = NULL;
    for (int i = 0; i < ap->f->n; i++) {
        const Answer *an = &ap->f->a[i];
        if (!same_name(an->key, key))
            continue;
        if (is_button(type)) {
            if (!export)
                export = field_string(d, a, FPDFAnnot_GetFormFieldExportValue);
            if (!same_name(an->export, export))
                continue;
        } else if (*an->export) {
            continue;
        }
        ap->used[i] = true;
        FPDF_PAGE p = doc_page(d, page);
        if (!p)
            break;
        if (is_button(type)) {
            if (FPDFAnnot_IsChecked(d->form, a))
                continue;
            if (doc_focus_widget(d, page, widget)) {
                FORM_OnChar(d->form, p, ' ', 0);
                ap->changed++;
            }
        } else if (type == FPDF_FORMFIELD_LISTBOX || type == FPDF_FORMFIELD_COMBOBOX) {
            int option = -1;
            for (int o = 0, n = FPDFAnnot_GetOptionCount(d->form, a); o < n && option < 0; o++) {
                char *label = option_label(d, a, o);
                if (!strcmp(label, an->value))
                    option = o;
                free(label);
            }
            char *now = chosen_option(d, a);
            bool taken = ap->only_empty && now && *now; /* it has an answer already */
            if (option >= 0) {
                if (!FPDFAnnot_IsOptionSelected(d->form, a, option) && !taken && doc_focus_widget(d, page, widget)) {
                    FORM_SetIndexSelected(d->form, p, option, true);
                    ap->changed++;
                }
            } else if (type == FPDF_FORMFIELD_COMBOBOX) { /* an editable combo box takes any text */
                char *typed = field_string(d, a, FPDFAnnot_GetFormFieldValue);
                if (strcmp(typed, an->value) && !(ap->only_empty && *typed) && doc_focus_widget(d, page, widget)) {
                    set_text(d, p, an->value);
                    ap->changed++;
                }
                free(typed);
            }
            free(now);
        } else {
            char *now = field_string(d, a, FPDFAnnot_GetFormFieldValue);
            bool keep = !strcmp(now, an->value) || (ap->only_empty && *now);
            free(now);
            if (!keep && doc_focus_widget(d, page, widget)) {
                set_text(d, p, an->value);
                ap->changed++;
            }
        }
        doc_kill_focus(d); /* commits the field */
    }
    free(key), free(export);
}

/* Puts the answers in the fields they belong to. Returns how many fields changed;
 * *missing counts the answers that no field of this PDF takes. */
static int apply(Doc *d, const Fill *f, int *missing, bool only_empty)
{
    Apply ap = {f, xcalloc((size_t)f->n + 1, sizeof(bool)), 0, only_empty};
    doc_kill_focus(d);
    each_widget(d, apply_one, &ap);
    *missing = 0;
    for (int i = 0; i < f->n; i++)
        *missing += !ap.used[i];
    free(ap.used);
    return ap.changed;
}

/* ---------- save, fill, delete ---------- */
static void clean_name(char *name)
{
    for (char *c = name; *c; c++)
        if ((unsigned char)*c < ' ')
            *c = ' ';
    char *s = name;
    while (*s == ' ')
        s++;
    memmove(name, s, strlen(s) + 1);
    if (strlen(name) > NAME_MAX_LEN) {
        size_t n = NAME_MAX_LEN;
        while (n && ((unsigned char)name[n] & 0xC0) == 0x80)
            n--;
        name[n] = 0;
    }
    for (char *end = name + strlen(name); end > name && end[-1] == ' ';)
        *--end = 0;
}

static bool form_ready(Window *w, Doc *d)
{
    if (doc_has_forms(d))
        return true;
    ui_message(w, "this PDF has no form");
    return false;
}

static void fills_save(Window *w, const char *given)
{
    Doc *d = cur_view(w)->doc;
    char name[CMD_MAX];
    snprintf(name, sizeof name, "%s", given);
    clean_name(name);
    if (!*name || !form_ready(w, d))
        return;
    Fill now = collect(d);
    if (!now.n) {
        ui_message(w, "nothing filled in yet, so nothing to save");
        fill_free(&now);
        return;
    }
    Fill *all;
    int n = store_load(&all), at = store_find(all, n, name);
    now.name = xstrdup(name);
    if (at >= 0) {
        fill_free(&all[at]);
        all[at] = now;
    } else {
        all = xrealloc(all, (size_t)(n + 1) * sizeof *all);
        all[n++] = now;
    }
    int count = now.n;
    if (store_write(all, n))
        ui_message(w, "%s \"%s\": %d answer%s", at >= 0 ? "replaced" : "saved", name, count, count == 1 ? "" : "s");
    else
        ui_message(w, "cannot write the saved forms");
    store_free(all, n);
}

static void fills_use(Window *w, const char *given, bool only_empty)
{
    Doc *d = cur_view(w)->doc;
    char name[CMD_MAX];
    snprintf(name, sizeof name, "%s", given);
    clean_name(name);
    if (!*name || !form_ready(w, d))
        return;
    Fill *all;
    int n = store_load(&all), at = store_find(all, n, name), missing = 0;
    if (at < 0) {
        ui_message(w, "no saved form called \"%s\" (\\F lists them)", name);
    } else {
        char *before = pages_snapshot(d); /* for u */
        int changed = apply(d, &all[at], &missing, only_empty);
        if (before && changed) {
            undo_forms(d, view_top_page(cur_view(w)), before);
        } else if (before) {
            unlink(before);
            free(before);
        }
        if (missing)
            ui_message_long(w, 5000, "\"%s\": %d field%s filled, %d answer%s had no field in this PDF", name, changed,
                            changed == 1 ? "" : "s", missing, missing == 1 ? "" : "s");
        else
            ui_message(w, "\"%s\": %d field%s filled", name, changed, changed == 1 ? "" : "s");
    }
    store_free(all, n);
    w->redraw = true;
}

static void fills_delete(Window *w, const char *given)
{
    char name[CMD_MAX];
    snprintf(name, sizeof name, "%s", given);
    clean_name(name);
    Fill *all;
    int n = store_load(&all), at = store_find(all, n, name);
    if (at < 0) {
        ui_message(w, "no saved form called \"%s\"", name);
    } else {
        fill_free(&all[at]);
        memmove(all + at, all + at + 1, (size_t)(n - at - 1) * sizeof *all);
        n--;
        ui_message(w, store_write(all, n) ? "deleted \"%s\"" : "cannot write the saved forms (\"%s\")", name);
    }
    store_free(all, n);
}

/* :form  (the menu), :form save NAME, :form delete NAME, :form NAME */
void fills_command(Window *w, const char *arg)
{
    while (*arg == ' ')
        arg++;
    if (!*arg)
        fills_open(w);
    else if (!strncmp(arg, "save ", 5))
        fills_save(w, arg + 5);
    else if (!strncmp(arg, "delete ", 7))
        fills_delete(w, arg + 7);
    else { /* :form P80 empty fills only the fields that are still empty */
        char name[CMD_MAX];
        snprintf(name, sizeof name, "%s", arg);
        size_t n = strlen(name);
        bool only_empty = n > 6 && !strcmp(name + n - 6, " empty");
        if (only_empty)
            name[n - 6] = 0;
        fills_use(w, name, only_empty);
    }
}

/* ---------- the menu ---------- */
void fills_free(Window *w)
{
    for (int i = 0; i < w->nfill; i++)
        free(w->fill_names[i]);
    free(w->fill_names);
    free(w->fill_counts);
    w->fill_names = NULL;
    w->fill_counts = NULL;
    w->nfill = 0;
}

static void load_list(Window *w)
{
    fills_free(w);
    Fill *all;
    int n = store_load(&all);
    w->fill_names = xcalloc((size_t)n + 1, sizeof *w->fill_names);
    w->fill_counts = xcalloc((size_t)n + 1, sizeof *w->fill_counts);
    for (int i = 0; i < n; i++) {
        w->fill_names[i] = xstrdup(all[i].name);
        w->fill_counts[i] = all[i].n;
    }
    w->nfill = n;
    w->fill_sel = SDL_clamp(w->fill_sel, 0, SDL_max(0, n - 1));
    store_free(all, n);
}

void fills_open(Window *w)
{
    if (!form_ready(w, cur_view(w)->doc))
        return;
    w->fill_return = w->mode == MODE_FORMS ? w->fill_return : w->mode;
    w->fill_sel = w->fill_first = 0;
    load_list(w);
    w->mode = MODE_FORMS;
    w->redraw = true;
}

static void close_list(Window *w)
{
    w->mode = w->fill_return == MODE_FORMS ? MODE_NORMAL : w->fill_return;
    fills_free(w);
    w->redraw = true;
}

static void save_asked(Window *w, const char *name)
{
    fills_save(w, name);
    load_list(w);
    for (int i = 0; i < w->nfill; i++) /* the one just saved is selected */
        if (!strcmp(w->fill_names[i], name))
            w->fill_sel = i;
}

static void delete_asked(Window *w, const char *answer)
{
    if ((answer[0] == 'y' || answer[0] == 'Y') && w->nfill)
        fills_delete(w, w->fill_names[w->fill_sel]);
    load_list(w);
}

void fills_key(Window *w, const char *tok)
{
    int d = 0;
    if (!strcmp(tok, "j") || !strcmp(tok, "<Down>") || !strcmp(tok, "<C-j>") || !strcmp(tok, "<Tab>"))
        d = 1;
    else if (!strcmp(tok, "k") || !strcmp(tok, "<Up>") || !strcmp(tok, "<C-k>") || !strcmp(tok, "<S-Tab>"))
        d = -1;
    if (d) {
        w->fill_sel = SDL_clamp(w->fill_sel + d, 0, SDL_max(0, w->nfill - 1));
    } else if ((!strcmp(tok, "<Enter>") || !strcmp(tok, "e")) && w->nfill) {
        char name[NAME_MAX_LEN * 4 + 8];
        snprintf(name, sizeof name, "%s", w->fill_names[w->fill_sel]);
        close_list(w);
        fills_use(w, name, !strcmp(tok, "e"));
        return;
    } else if (!strcmp(tok, "n")) {
        prompt_open(w, "save the form as: ", "", save_asked, 0);
    } else if (!strcmp(tok, "x") && w->nfill) {
        char label[NAME_MAX_LEN * 4 + 40];
        snprintf(label, sizeof label, "delete \"%s\"? y to confirm: ", w->fill_names[w->fill_sel]);
        prompt_open(w, label, "", delete_asked, 0);
    } else if (!strcmp(tok, "<Esc>") || !strcmp(tok, "q")) {
        close_list(w);
        return;
    }
    w->redraw = true;
}

void fills_draw(Window *w)
{
    const Theme *t = uitheme(w);
    int pad = (int)(12 * w->density), lh = font_height(w->font), row = lh + (int)(12 * w->density);
    float bw = (float)SDL_min(w->W - 2 * pad, (int)(620 * w->density));
    float bh = SDL_min((w->H - top_of(w)) * 0.8f, (float)(SDL_max(w->nfill, 2) * row + 3 * lh + 3 * pad));
    float x = (w->W - bw) / 2, y = top_of(w) + (w->H - top_of(w) - bh) / 2;
    ui_fill(w, x, y, bw, bh, t->panel_bg, 255);
    ui_frame(w, x, y, bw, bh, SDL_max(1.0f, w->density), t->panel_header);
    font_draw(w->font, x + pad, y + pad / 2.0f, "Saved forms", t->panel_fg);
    float list_top = y + lh + pad * 1.5f, list_bottom = y + bh - lh - pad;
    ui_fill(w, x, list_top - pad / 2.0f, bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    ui_fill(w, x, list_bottom, bw, SDL_max(1.0f, w->density), t->panel_header, 90);
    char foot[200];
    snprintf(foot, sizeof foot - 4, "j k move   Enter fill   e only empty fields   n save   x delete   Esc");
    font_fit(w->font, foot, (int)bw - 2 * pad);
    font_draw(w->font, x + pad, list_bottom + pad / 2.0f, foot, t->panel_header);
    if (!w->nfill) {
        font_draw(w->font, x + pad, list_top + pad / 2.0f, "none yet: fill in the form, then press n", t->panel_header);
        return;
    }
    int rows = SDL_max(1, (int)((list_bottom - list_top) / row));
    if (w->fill_sel < w->fill_first)
        w->fill_first = w->fill_sel;
    if (w->fill_sel >= w->fill_first + rows)
        w->fill_first = w->fill_sel - rows + 1;
    for (int i = w->fill_first; i < w->nfill && i < w->fill_first + rows; i++) {
        float ry = list_top + (i - w->fill_first) * row, ty = ry + (row - lh) / 2.0f;
        bool sel = i == w->fill_sel;
        if (sel)
            ui_fill(w, x + 2, ry, bw - 4, (float)row, t->panel_selected_bg, 255);
        uint32_t fg = sel ? t->panel_selected_fg : t->panel_fg, dim = sel ? t->panel_selected_fg : t->panel_header;
        char name[NAME_MAX_LEN * 4 + 8], count[40];
        snprintf(name, sizeof name, "%s", w->fill_names[i]);
        snprintf(count, sizeof count, "%d answer%s", w->fill_counts[i], w->fill_counts[i] == 1 ? "" : "s");
        font_fit(w->font, name, (int)(bw - 3 * pad - font_width(w->font, count)));
        font_draw(w->font, x + pad, ty, name, fg);
        font_draw(w->font, x + bw - pad - font_width(w->font, count), ty, count, dim);
    }
}
