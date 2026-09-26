/* Talking to the outside world without blocking the UI:
 *  - a Unix socket ($XDG_RUNTIME_DIR/nomadys-pdf.sock) where a second
 *    `nomadys-pdf --synctex-forward ...` (run by VimTeX) hands its request to
 *    the running instance;
 *  - inotify on the directories of open PDFs, to reload them after LaTeX
 *    writes a new version.
 * A background thread waits on both and posts SDL user events to the loop. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "nomadys.h"

Uint32 ipc_event;            /* SDL event type; code IPC_REQUEST or IPC_FILE */
static int server_fd = -1, notify_fd = -1;
static SDL_AtomicInt stopping; /* set at exit: post no more events */

static void socket_path(struct sockaddr_un *a)
{
    const char *dir = getenv("XDG_RUNTIME_DIR");
    memset(a, 0, sizeof *a);
    a->sun_family = AF_UNIX;
    int n = snprintf(a->sun_path, sizeof a->sun_path, "%s/nomadys-pdf.sock", dir && *dir ? dir : "/tmp");
    if (!dir || !*dir || n >= (int)sizeof a->sun_path) /* socket paths are limited to ~108 bytes */
        snprintf(a->sun_path, sizeof a->sun_path, "/tmp/nomadys-pdf-%d.sock", (int)getuid());
}

bool ipc_send(const char *msg)
{
    struct sockaddr_un a;
    socket_path(&a);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    bool ok = connect(fd, (struct sockaddr *)&a, sizeof a) == 0;
    size_t n = strlen(msg);
    ok = ok && write(fd, msg, n) == (ssize_t)n;
    close(fd);
    return ok;
}

static void post(int code, char *text, int wd)
{
    if (SDL_GetAtomicInt(&stopping)) {
        free(text);
        return;
    }
    SDL_Event e = {0};
    e.type = ipc_event;
    e.user.code = code;
    e.user.data1 = text;
    e.user.data2 = (void *)(intptr_t)wd;
    if (!SDL_PushEvent(&e))
        free(text);
}

static void read_request(int client)
{
    /* a client that connects and stays silent must not block this thread,
     * which also watches the files for reloads */
    struct timeval tv = {0, 500000};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char buf[8192];
    size_t len = 0;
    for (;;) {
        ssize_t r = read(client, buf + len, sizeof buf - 1 - len);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0 || (len += (size_t)r) >= sizeof buf - 1)
            break;
    }
    close(client);
    buf[len] = 0;
    if (len)
        post(IPC_REQUEST, xstrdup(buf), 0);
}

static void read_notify(void)
{
    char buf[16384] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t n = read(notify_fd, buf, sizeof buf);
    for (char *p = buf; n > 0 && p < buf + n;) {
        struct inotify_event *ev = (struct inotify_event *)p;
        if (ev->len)
            post(IPC_FILE, xstrdup(ev->name), ev->wd);
        p += sizeof *ev + ev->len;
    }
}

static int loop(void *unused)
{
    (void)unused;
    for (;;) {
        struct pollfd fds[2] = {{server_fd, POLLIN, 0}, {notify_fd, POLLIN, 0}};
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            return 0;
        }
        if (fds[0].revents & POLLIN) {
            int c = accept4(server_fd, NULL, NULL, SOCK_CLOEXEC);
            if (c >= 0)
                read_request(c);
        }
        if (fds[1].revents & POLLIN)
            read_notify();
    }
}

void ipc_start(void)
{
    ipc_event = SDL_RegisterEvents(1);
    notify_fd = inotify_init1(IN_CLOEXEC);
    struct sockaddr_un a;
    socket_path(&a);
    server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd >= 0 && bind(server_fd, (struct sockaddr *)&a, sizeof a) != 0) {
        /* the name is taken: by a running instance (it serves), or left over
         * from a crash (remove it and take over) */
        int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        bool alive = probe >= 0 && connect(probe, (struct sockaddr *)&a, sizeof a) == 0;
        if (probe >= 0)
            close(probe);
        if (alive || unlink(a.sun_path) != 0 || bind(server_fd, (struct sockaddr *)&a, sizeof a) != 0) {
            close(server_fd);
            server_fd = -1;
        }
    }
    if (server_fd >= 0)
        listen(server_fd, 8);
    SDL_Thread *t = SDL_CreateThread(loop, "nomadys-ipc", NULL);
    if (t)
        SDL_DetachThread(t);
}

void ipc_stop(void)
{
    /* free the texts of events still queued (a file changed as we quit) */
    SDL_SetAtomicInt(&stopping, 1);
    SDL_Event e;
    while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, ipc_event, ipc_event) == 1)
        free(e.user.data1);
    if (server_fd >= 0) {
        struct sockaddr_un a;
        socket_path(&a);
        unlink(a.sun_path);
    }
}

void ipc_watch(Doc *d)
{
    if (notify_fd < 0 || d->watch_wd > 0)
        return;
    char dir[4096];
    snprintf(dir, sizeof dir, "%s", d->path);
    char *slash = strrchr(dir, '/');
    if (!slash)
        return;
    *slash = 0;
    d->watch_wd = inotify_add_watch(notify_fd, *dir ? dir : "/", IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
}
