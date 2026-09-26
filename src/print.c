/* Printing through CUPS: ":print [pages] [xN] [@printer] [duplex]".
 * The PDF is saved first so highlights and filled forms are printed too,
 * then handed to `lp`, which spools it and returns at once. */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "nomadys.h"

extern char **environ;

/* Runs a command and returns its standard output (and error), or NULL if it
 * could not be started. *status receives the exit status. */
static char *capture(char *const argv[], int *status)
{
    int fd[2];
    if (pipe(fd))
        return NULL;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, fd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, fd[0]);
    pid_t pid;
    int err = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fd[1]);
    if (err) {
        close(fd[0]);
        return NULL;
    }
    size_t len = 0, cap = 1024;
    char *out = xmalloc(cap);
    for (;;) {
        if (len + 256 > cap)
            out = xrealloc(out, cap *= 2);
        ssize_t r = read(fd[0], out + len, cap - len - 1);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        len += (size_t)r;
    }
    out[len] = 0;
    close(fd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    *status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return out;
}

/* Printer names known to CUPS, one per line in a malloc'd string ("" if none). */
char *print_printers(void)
{
    char *argv[] = {"lpstat", "-e", NULL};
    int st;
    char *out = capture(argv, &st);
    if (!out || st != 0 || strstr(out, "No destinations")) {
        free(out);
        return xstrdup("");
    }
    return out;
}

static bool has_line(const char *list, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = list; *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : p + strlen(p))
        if (!strncmp(p, name, n) && (p[n] == '\n' || p[n] == 0))
            return true;
    return false;
}

/* The system default printer, or NULL. */
static char *default_printer(void)
{
    char *argv[] = {"lpstat", "-d", NULL};
    int st;
    char *out = capture(argv, &st);
    char *colon = out ? strstr(out, "destination: ") : NULL;
    char *name = NULL;
    if (colon) {
        colon += strlen("destination: ");
        name = xstrdup(colon);
        name[strcspn(name, " \n")] = 0;
    }
    free(out);
    return name;
}

static bool valid_ranges(const char *s)
{
    if (!*s)
        return false;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s) && *s != ',' && *s != '-')
            return false;
    return true;
}

void print_document(Window *w, View *v, const char *args)
{
    char printer[256] = "", pages[256] = "", copies[16] = "";
    bool duplex = false;
    char buf[CMD_MAX];
    snprintf(buf, sizeof buf, "%s", args);
    for (char *t = strtok(buf, " "); t; t = strtok(NULL, " ")) {
        if (t[0] == '@')
            snprintf(printer, sizeof printer, "%s", t + 1);
        else if ((t[0] == 'x' || t[0] == 'X') && isdigit((unsigned char)t[1]) && atoi(t + 1) > 0)
            snprintf(copies, sizeof copies, "%d", SDL_min(atoi(t + 1), 999));
        else if (!strcmp(t, "duplex"))
            duplex = true;
        else if (valid_ranges(t))
            snprintf(pages, sizeof pages, "%s", t);
        else {
            ui_message_long(w, 5000, "print: unknown option '%s' (use pages like 3-7, x2, @printer, duplex)", t);
            return;
        }
    }
    char *list = print_printers();
    if (!*list) {
        free(list);
        ui_message_long(w, 5000, "no printer: CUPS has none configured (or lpstat is missing)");
        return;
    }
    if (!printer[0]) {
        char *def = default_printer();
        if (def && has_line(list, def)) {
            snprintf(printer, sizeof printer, "%s", def);
        } else if (!strchr(list, '\n') || !strchr(list, '\n')[1]) {
            snprintf(printer, sizeof printer, "%.*s", (int)strcspn(list, "\n"), list); /* the only one */
        } else {
            free(def);
            free(list);
            ui_message_long(w, 5000, "several printers and no default: :print @name  (Tab lists them)");
            return;
        }
        free(def);
    }
    if (!has_line(list, printer)) {
        free(list);
        ui_message_long(w, 5000, "unknown printer '%s' (Tab after @ lists them)", printer);
        return;
    }
    free(list);
    if (v->doc->modified) { /* print what is on screen: highlights and filled forms */
        char err[512];
        doc_kill_focus(v->doc);
        if (!doc_save(v->doc, err, sizeof err)) {
            ui_message_long(w, 5000, "not printed: %s", err);
            return;
        }
    }
    const char *title = strrchr(v->doc->path, '/') ? strrchr(v->doc->path, '/') + 1 : v->doc->path;
    char *argv[16];
    int n = 0;
    argv[n++] = "lp";
    argv[n++] = "-d", argv[n++] = printer;
    argv[n++] = "-t", argv[n++] = (char *)title;
    if (copies[0])
        argv[n++] = "-n", argv[n++] = copies;
    if (pages[0])
        argv[n++] = "-P", argv[n++] = pages;
    if (duplex)
        argv[n++] = "-o", argv[n++] = "sides=two-sided-long-edge";
    argv[n++] = "--", argv[n++] = v->doc->path;
    argv[n] = NULL;
    int st;
    char *out = capture(argv, &st);
    if (!out) {
        ui_message_long(w, 5000, "print: the lp command was not found (install CUPS)");
        return;
    }
    out[strcspn(out, "\n")] = 0;
    if (st == 0) {
        char *id = strstr(out, "request id is ");
        ui_message_long(w, 5000, "sent to %s%s%.*s", printer, id ? ": " : "",
                        id ? (int)strcspn(id + 14, " ") : 0, id ? id + 14 : "");
    } else {
        ui_message_long(w, 6000, "print failed: %s", out[0] ? out : "lp error");
    }
    free(out);
}
