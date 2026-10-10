/* Quoting and citing: Y copies the selection as a quotation with its page
 * (“…” (p. 5), cite_format in the config), and :bibtex finds the paper's
 * DOI (or arXiv id) and copies its BibTeX entry, fetched with curl from
 * doi.org (or arxiv.org) in a thread, so reading goes on meanwhile. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpdf_doc.h"
#include "nomadys.h"

/* The page's printed number (its label: "iv", "S3") or its index + 1. */
static void page_name(Doc *d, int page, char *out, size_t cap)
{
    unsigned short u[64];
    unsigned long n = FPDF_GetPageLabel(d->pdf, page, u, sizeof u);
    if (n > 2 && n <= sizeof u) {
        char *s = utf16_to_utf8(u, (int)(n / 2) - 1);
        snprintf(out, cap, "%s", s);
        free(s);
        if (out[0])
            return;
    }
    snprintf(out, cap, "%d", page + 1);
}

/* Spaces collapsed: a quotation is one line. */
static void one_line(char *s)
{
    char *o = s;
    bool space = true; /* no leading space */
    for (char *p = s; *p; p++) {
        if (*p == '\r' || *p == '\n' || *p == '\t' || *p == ' ') {
            if (!space)
                *o++ = ' ';
            space = true;
        } else {
            *o++ = *p;
            space = false;
        }
    }
    while (o > s && o[-1] == ' ')
        o--;
    *o = 0;
}

void cite_copy(Window *w, View *v, char *text, int first_page, int last_page)
{
    one_line(text);
    char p0[64], p1[64], pages[140];
    page_name(v->doc, first_page, p0, sizeof p0);
    page_name(v->doc, last_page, p1, sizeof p1);
    if (first_page == last_page)
        snprintf(pages, sizeof pages, "%s", p0);
    else
        snprintf(pages, sizeof pages, "%s–%s", p0, p1);
    /* %t the text, %p the page(s); "p." becomes "pp." for a range */
    size_t cap = strlen(text) + strlen(cfg.cite_format) + 2 * sizeof pages + 8;
    char *out = xmalloc(cap), *o = out;
    for (const char *f = cfg.cite_format; *f && (size_t)(o - out) < cap - 1; f++) {
        size_t room = cap - (size_t)(o - out);
        if (f[0] == '%' && f[1] == 't') {
            o += snprintf(o, room, "%s", text), f++;
        } else if (f[0] == '%' && f[1] == 'p') {
            o += snprintf(o, room, "%s", pages), f++;
        } else if (!strncmp(f, "p. %p", 5) && first_page != last_page) {
            o += snprintf(o, room, "pp. %s", pages), f += 4;
        } else {
            *o++ = *f;
        }
        if ((size_t)(o - out) >= cap)
            o = out + cap - 1;
    }
    *o = 0;
    SDL_SetClipboardText(out);
    ui_message(w, "copied as a quotation: %.60s%s", out, strlen(out) > 60 ? "…" : "");
    free(out);
}

/* ---------- :bibtex ---------- */

/* A DOI in s ("10.1103/PhysRevLett.116.061102"), trailing punctuation off;
 * prefers one written as a DOI ("doi:", "doi.org/") over a bare one. */
static bool find_doi(const char *s, char *out, size_t cap)
{
    bool found = false;
    for (const char *p = s; (p = strstr(p, "10.")); p++) {
        const char *q = p + 3;
        int digits = 0;
        while (isdigit((unsigned char)*q))
            q++, digits++;
        if (digits < 4 || digits > 9 || *q != '/' || (p > s && isalnum((unsigned char)p[-1])))
            continue;
        q++;
        const char *end = q;
        while (*end && !isspace((unsigned char)*end) && *end != '"' && *end != '<' && *end != '>')
            end++;
        while (end > q && strchr(".,;:)]}'", end[-1]))
            end--;
        if (end - q < 1 || (size_t)(end - p) >= cap)
            continue;
        bool labelled = false;
        for (const char *b = p - 1; b >= s && b >= p - 12; b--)
            if (!strncasecmp(b, "doi", 3))
                labelled = true;
        if (!found || labelled) {
            snprintf(out, cap, "%.*s", (int)(end - p), p);
            found = true;
        }
        if (labelled)
            return true;
    }
    return found;
}

/* An arXiv identifier ("arXiv:2101.01234v2"), version dropped. */
static bool find_arxiv(const char *s, char *out, size_t cap)
{
    for (const char *p = s; (p = strcasestr(p, "arxiv")); p++) {
        const char *q = p + 5;
        while (*q == ':' || *q == ' ' || *q == '/' || *q == '.')
            q++;
        if (!strncasecmp(q, "org/abs/", 8))
            q += 8;
        int a = 0, b = 0;
        const char *r = q;
        while (isdigit((unsigned char)*r))
            r++, a++;
        if (a != 4 || *r != '.')
            continue;
        r++;
        while (isdigit((unsigned char)*r))
            r++, b++;
        if (b < 4 || b > 5)
            continue;
        snprintf(out, cap, "%.*s", (int)(r - q), q);
        return true;
    }
    return false;
}

static char *meta(Doc *d, const char *key)
{
    unsigned short u[512];
    unsigned long n = FPDF_GetMetaText(d->pdf, key, u, sizeof u);
    return n > 2 && n <= sizeof u ? utf16_to_utf8(u, (int)(n / 2) - 1) : xstrdup("");
}

/* The text of the first pages, where a paper prints its DOI. */
static char *first_pages(Doc *d, int pages)
{
    char *all = xstrdup("");
    size_t len = 0;
    for (int p = 0; p < SDL_min(pages, d->npages); p++) {
        FPDF_TEXTPAGE tp = doc_text(d, p);
        int n = tp ? FPDFText_CountChars(tp) : 0;
        if (n <= 0)
            continue;
        unsigned short *u = xmalloc(((size_t)n + 1) * sizeof *u);
        int got = FPDFText_GetText(tp, 0, n, u);
        char *s = utf16_to_utf8(u, got > 0 ? got - 1 : 0);
        size_t sl = strlen(s);
        all = xrealloc(all, len + sl + 2);
        memcpy(all + len, s, sl);
        len += sl;
        all[len++] = '\n';
        all[len] = 0;
        free(s);
        free(u);
    }
    return all;
}

typedef struct {
    char url[600];
    char what[300]; /* "DOI 10.…" for the messages */
    SDL_WindowID win;
} Fetch;

static SDL_AtomicInt fetching;

static int fetch(void *arg)
{
    Fetch *f = arg;
    const char *args[] = {"curl", "-sSLf", "--max-time", "20", "--proto", "=https", "--proto-redir", "=https",
                          "--max-filesize", "2000000", "-H", "Accept: application/x-bibtex; charset=utf-8", f->url, NULL};
    SDL_Process *p = SDL_CreateProcess(args, true);
    size_t size = 0;
    int code = -1;
    char *body = p ? SDL_ReadProcess(p, &size, &code) : NULL;
    if (p)
        SDL_DestroyProcess(p);
    char *msg;
    size_t cap = size + sizeof f->what + 64;
    msg = xmalloc(cap);
    if (!p)
        snprintf(msg, cap, "!curl is needed for :bibtex (it is not installed)");
    else if (code != 0 || !body || !strchr(body, '@'))
        snprintf(msg, cap, "!no BibTeX for %s (%s)", f->what,
                 code == 6 || code == 7 ? "no connection" : code == 28 ? "timed out" : "not found");
    else
        snprintf(msg, cap, "%s", body + strspn(body, " \r\n\t"));
    SDL_free(body);
    SDL_Event e = {0};
    e.type = ipc_event;
    e.user.code = IPC_BIBTEX;
    e.user.data1 = msg;
    e.user.data2 = (void *)(uintptr_t)f->win;
    if (!SDL_PushEvent(&e))
        free(msg);
    free(f);
    SDL_SetAtomicInt(&fetching, 0);
    return 0;
}

/* Percent-encodes what a URL cannot carry (a DOI may contain <, #, spaces). */
static void url_part(char *out, size_t cap, const char *s)
{
    size_t o = 0;
    for (; *s && o + 4 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || strchr("-._~/:;()", c))
            out[o++] = (char)c;
        else
            o += (size_t)snprintf(out + o, cap - o, "%%%02X", c);
    }
    out[o] = 0;
}

void cite_bibtex(Window *w, View *v)
{
    if (SDL_GetAtomicInt(&fetching)) {
        ui_message(w, "already fetching a BibTeX entry…");
        return;
    }
    Doc *d = v->doc;
    char doi[256] = "", arxiv[32] = "";
    const char *keys[] = {"Subject", "Keywords", "Title"}; /* publishers often put "doi:…" in Subject */
    for (size_t i = 0; i < SDL_arraysize(keys) && !doi[0]; i++) {
        char *m = meta(d, keys[i]);
        find_doi(m, doi, sizeof doi);
        free(m);
    }
    if (!doi[0]) {
        char *text = first_pages(d, 2);
        if (!find_doi(text, doi, sizeof doi))
            find_arxiv(text, arxiv, sizeof arxiv);
        free(text);
    }
    Fetch *f = xcalloc(1, sizeof *f);
    char enc[520];
    if (doi[0]) {
        url_part(enc, sizeof enc, doi);
        snprintf(f->url, sizeof f->url, "https://doi.org/%s", enc);
        snprintf(f->what, sizeof f->what, "DOI %s", doi);
    } else if (arxiv[0]) {
        snprintf(f->url, sizeof f->url, "https://arxiv.org/bibtex/%s", arxiv);
        snprintf(f->what, sizeof f->what, "arXiv %s", arxiv);
    } else {
        free(f);
        ui_message_long(w, 5000, "no DOI or arXiv number found on the first pages");
        return;
    }
    f->win = SDL_GetWindowID(w->win);
    SDL_SetAtomicInt(&fetching, 1);
    SDL_Thread *t = SDL_CreateThread(fetch, "bibtex", f);
    if (!t) {
        SDL_SetAtomicInt(&fetching, 0);
        free(f);
        ui_message(w, "cannot start the download");
        return;
    }
    SDL_DetachThread(t);
    ui_message_long(w, 20000, "fetching BibTeX for %s…", doi[0] ? doi : arxiv);
}

/* The download finished (main thread): the entry goes to the clipboard. */
void cite_result(Window *w, const char *text)
{
    if (!w)
        return;
    if (text[0] == '!') {
        ui_message_long(w, 6000, "%s", text + 1);
        return;
    }
    SDL_SetClipboardText(text);
    char key[96] = "";
    const char *brace = strchr(text, '{');
    if (brace)
        snprintf(key, sizeof key, "%.*s", (int)strcspn(brace + 1, ",\n"), brace + 1);
    ui_message_long(w, 6000, "BibTeX copied%s%s   (paste it into your .bib)", key[0] ? ": " : "", key);
}
