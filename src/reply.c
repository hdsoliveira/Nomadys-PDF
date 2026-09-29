/* The /IRT of the replies made here (annot.c, note_set_reply). PDFium reads
 * that reference but has no way to write it, so after each save the file
 * gets a small incremental update: each reply object again, with
 * /IRT n g R added, a cross-reference section for them and a trailer.
 * Acrobat, Okular and the like then show the reply in the thread of the
 * note it answers. The objects are found by their /NM.
 *
 * Only a file ending in a classic cross-reference table is changed (the
 * full rewrite PDFium does); otherwise, or if anything is not as expected,
 * the file stays as it was and Nomadys still knows the reply by its own
 * /NomadysReplyTo. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nomadys.h"

typedef struct {
    const char *s;
    size_t n;
} Bytes;

static bool is_ws(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == 0; }
static bool is_delim(char c) { return is_ws(c) || strchr("()<>[]{}/%", c); }

static const char *find(Bytes b, size_t from, const char *what)
{
    if (from >= b.n)
        return NULL;
    return memmem(b.s + from, b.n - from, what, strlen(what));
}

/* A string token at p (literal or hex), decoded; UTF-16 with a BOM becomes
 * the ASCII it holds. Returns its length, or -1. */
static int read_string(Bytes b, size_t p, char *out, size_t cap)
{
    while (p < b.n && is_ws(b.s[p]))
        p++;
    if (p >= b.n)
        return -1;
    size_t n = 0;
    unsigned char raw[256];
    if (b.s[p] == '(') {
        int depth = 1;
        for (p++; p < b.n && depth && n < sizeof raw; p++) {
            char c = b.s[p];
            if (c == '\\' && p + 1 < b.n) {
                c = b.s[++p];
                if (c >= '0' && c <= '7') {
                    int v = 0;
                    for (int k = 0; k < 3 && p < b.n && b.s[p] >= '0' && b.s[p] <= '7'; k++, p++)
                        v = v * 8 + (b.s[p] - '0');
                    p--;
                    raw[n++] = (unsigned char)v;
                    continue;
                }
                raw[n++] = c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c == 'b' ? '\b' : c == 'f' ? '\f' : c;
                continue;
            }
            depth += (c == '(') - (c == ')');
            if (depth)
                raw[n++] = (unsigned char)c;
        }
    } else if (b.s[p] == '<' && p + 1 < b.n && b.s[p + 1] != '<') {
        int half = -1;
        for (p++; p < b.n && b.s[p] != '>' && n < sizeof raw; p++) {
            char c = b.s[p];
            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0)
                continue;
            if (half < 0)
                half = v;
            else
                raw[n++] = (unsigned char)(half * 16 + v), half = -1;
        }
        if (half >= 0 && n < sizeof raw)
            raw[n++] = (unsigned char)(half * 16);
    } else {
        return -1;
    }
    size_t o = 0;
    if (n >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) { /* UTF-16BE */
        for (size_t i = 2; i + 1 < n && o + 1 < cap; i += 2)
            out[o++] = raw[i] ? '?' : (char)raw[i + 1];
    } else {
        for (size_t i = 0; i < n && o + 1 < cap; i++)
            out[o++] = (char)raw[i];
    }
    out[o] = 0;
    return (int)o;
}

typedef struct {
    long num, gen;
    size_t body, end;        /* after "obj", at "endobj" */
} Obj;

/* The object around offset at: "num gen obj" before it, "endobj" after. */
static bool object_around(Bytes b, size_t at, Obj *o)
{
    for (size_t p = at; p >= 3; p--) {
        if (memcmp(b.s + p - 3, "obj", 3) || (p < b.n && !is_delim(b.s[p])))
            continue;
        size_t q = p - 3;
        if (q > 0 && !is_ws(b.s[q - 1]))
            continue; /* "endobj" */
        while (q > 0 && is_ws(b.s[q - 1]))
            q--;
        size_t g_end = q;
        while (q > 0 && b.s[q - 1] >= '0' && b.s[q - 1] <= '9')
            q--;
        if (q == g_end || q == 0 || !is_ws(b.s[q - 1]))
            return false;
        o->gen = strtol(b.s + q, NULL, 10);
        while (q > 0 && is_ws(b.s[q - 1]))
            q--;
        size_t n_end = q;
        while (q > 0 && b.s[q - 1] >= '0' && b.s[q - 1] <= '9')
            q--;
        if (q == n_end)
            return false;
        o->num = strtol(b.s + q, NULL, 10);
        o->body = p;
        const char *e = find(b, at, "endobj");
        if (!e)
            return false;
        o->end = (size_t)(e - b.s);
        return true;
    }
    return false;
}

/* The object whose /NM is nm (and that has the key need, if given). */
static bool object_named(Bytes b, const char *nm, const char *need, Obj *o)
{
    for (const char *p = find(b, 0, "/NM"); p; p = find(b, (size_t)(p - b.s) + 3, "/NM")) {
        size_t at = (size_t)(p - b.s) + 3;
        char got[128];
        if (at < b.n && !is_delim(b.s[at]))
            continue; /* a longer key */
        if (read_string(b, at, got, sizeof got) < 0 || strcmp(got, nm) || !object_around(b, at, o))
            continue;
        const char *k = need ? find(b, o->body, need) : NULL;
        if (!need || (k && (size_t)(k - b.s) < o->end))
            return true;
    }
    return false;
}

/* The dictionary starting at p ("<<"): its end, after ">>". */
static size_t dict_end(Bytes b, size_t p)
{
    int depth = 0;
    while (p + 1 < b.n) {
        if (b.s[p] == '<' && b.s[p + 1] == '<')
            depth++, p += 2;
        else if (b.s[p] == '>' && b.s[p + 1] == '>') {
            p += 2;
            if (--depth == 0)
                return p;
        } else if (b.s[p] == '<') {
            while (p < b.n && b.s[p] != '>')
                p++;
            p++;
        } else if (b.s[p] == '(') {
            int d = 0;
            for (; p < b.n; p++) {
                if (b.s[p] == '\\') {
                    p++;
                    continue;
                }
                d += (b.s[p] == '(') - (b.s[p] == ')');
                if (!d)
                    break;
            }
            p++;
        } else {
            p++;
        }
    }
    return 0;
}

typedef struct {
    long num, gen;
    long off;
} Entry;

static int entry_cmp(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    return (x->num > y->num) - (x->num < y->num);
}

static bool put(int fd, const char *s, size_t n, long *at)
{
    while (n) {
        ssize_t k = write(fd, s, n);
        if (k <= 0)
            return false;
        s += k, n -= (size_t)k, *at += k;
    }
    return true;
}

/* Appends the /IRT of the replies that lack it to the file just written;
 * true if the file is complete (changed or not). */
bool pdf_link_replies(int fd, char (*replies)[2][64], int n)
{
    struct stat st;
    if (n <= 0 || fstat(fd, &st) != 0 || st.st_size < 32)
        return true;
    void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
        return true;
    Bytes b = {map, (size_t)st.st_size};
    /* the last cross-reference section must be a classic table */
    const char *sx = NULL;
    for (const char *p = find(b, 0, "startxref"); p; p = find(b, (size_t)(p - b.s) + 9, "startxref"))
        sx = p;
    long prev = sx ? strtol(sx + 9, NULL, 10) : -1;
    const char *tr = prev > 0 && (size_t)prev + 4 < b.n && !memcmp(b.s + prev, "xref", 4) ? find(b, (size_t)prev, "trailer") : NULL;
    const char *td = tr ? find(b, (size_t)(tr - b.s), "<<") : NULL;
    size_t tend = td ? dict_end(b, (size_t)(td - b.s)) : 0;
    if (!tend) {
        munmap(map, b.n);
        return true;
    }
    Entry *e = xcalloc((size_t)n, sizeof *e);
    char **body = xcalloc((size_t)n, sizeof *body);
    int ne = 0;
    for (int i = 0; i < n; i++) {
        Obj r, p;
        if (!object_named(b, replies[i][0], "/NomadysReplyTo", &r) || !object_named(b, replies[i][1], NULL, &p))
            continue; /* deleted since, or never saved */
        const char *open = find(b, r.body, "<<");
        const char *irt = find(b, r.body, "/IRT");
        if (!open || (size_t)(open - b.s) > r.end || (irt && (size_t)(irt - b.s) < r.end))
            continue; /* not a dictionary, or linked already */
        bool dup = false;
        for (int k = 0; k < ne; k++)
            dup |= e[k].num == r.num;
        if (dup)
            continue;
        size_t head = (size_t)(open - b.s) + 2 - r.body, rest = r.end - r.body - head;
        char ref[64];
        int rl = snprintf(ref, sizeof ref, "/IRT %ld %ld R", p.num, p.gen);
        char *t = xmalloc(head + (size_t)rl + rest + 1);
        memcpy(t, b.s + r.body, head);
        memcpy(t + head, ref, (size_t)rl);
        memcpy(t + head + rl, b.s + r.body + head, rest);
        t[head + rl + rest] = 0;
        e[ne] = (Entry){r.num, r.gen, 0};
        body[ne++] = t;
    }
    /* the trailer again, without /Prev (ours points at the last table) */
    char *trailer = NULL;
    if (ne) {
        size_t ts = (size_t)(td - b.s), tl = tend - ts;
        trailer = xmalloc(tl + 1);
        memcpy(trailer, b.s + ts, tl);
        trailer[tl] = 0;
        const char *keys[] = {"/Prev", "/XRefStm"};
        for (int k = 0; k < 2; k++) {
            char *at = strstr(trailer, keys[k]);
            if (!at || !is_delim(at[strlen(keys[k])]))
                continue;
            char *q = at + strlen(keys[k]);
            while (is_ws(*q))
                q++;
            while (*q >= '0' && *q <= '9')
                q++;
            memmove(at, q, strlen(q) + 1);
        }
        trailer[strlen(trailer) - 2] = 0; /* the closing ">>" */
    }
    munmap(map, b.n);
    bool ok = true;
    if (ne) {
        long at = st.st_size;
        ok = lseek(fd, 0, SEEK_END) == st.st_size && put(fd, "\n", 1, &at);
        for (int i = 0; ok && i < ne; i++) {
            char hdr[64];
            int hl = snprintf(hdr, sizeof hdr, "%ld %ld obj", e[i].num, e[i].gen);
            e[i].off = at;
            ok = put(fd, hdr, (size_t)hl, &at) && put(fd, body[i], strlen(body[i]), &at) && put(fd, "endobj\n", 7, &at);
        }
        qsort(e, (size_t)ne, sizeof *e, entry_cmp); /* sections go up */
        long xref = at;
        ok = ok && put(fd, "xref\n", 5, &at);
        for (int i = 0; ok && i < ne; i++) {
            char line[64];
            int ll = snprintf(line, sizeof line, "%ld 1\n%010ld %05ld n\r\n", e[i].num, e[i].off, e[i].gen);
            ok = put(fd, line, (size_t)ll, &at);
        }
        char tail[96];
        int tl = snprintf(tail, sizeof tail, " /Prev %ld>>\nstartxref\n%ld\n%%%%EOF\n", prev, xref);
        ok = ok && put(fd, "trailer\n", 8, &at) && put(fd, trailer, strlen(trailer), &at) && put(fd, tail, (size_t)tl, &at);
        if (!ok && ftruncate(fd, st.st_size) == 0) /* back to the file PDFium wrote */
            ok = true;
    }
    for (int i = 0; i < ne; i++)
        free(body[i]);
    free(body);
    free(e);
    free(trailer);
    return ok;
}
