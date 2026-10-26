/* Talking to the outside world without blocking the UI:
 *  - a Unix socket ($XDG_RUNTIME_DIR/nomadys-pdf.sock) where a second
 *    `npdf --synctex-forward ...` (run by VimTeX) hands its request to
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
#ifdef __APPLE__
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <libgen.h>
#else
#include <sys/inotify.h>
#endif
#include <sys/stat.h>
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

/* Like ipc_send, then waits (a few seconds) for the running instance to say it
 * opened the file: the sender may delete it as soon as this returns. */
bool ipc_send_wait(const char *msg)
{
    struct sockaddr_un a;
    socket_path(&a);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    size_t n = strlen(msg);
    bool ok = connect(fd, (struct sockaddr *)&a, sizeof a) == 0 && write(fd, msg, n) == (ssize_t)n;
    if (ok) {
        shutdown(fd, SHUT_WR);
        struct pollfd p = {fd, POLLIN, 0};
        char reply[8];
        ok = poll(&p, 1, 5000) == 1 && read(fd, reply, sizeof reply) > 0;
    }
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
    buf[len] = 0;
    if (!strncmp(buf, "open\t", 5)) { /* the window answers once it has opened it */
        post(IPC_REQUEST, xstrdup(buf), client);
        return;
    }
    close(client);
    if (len)
        post(IPC_REQUEST, xstrdup(buf), 0);
}

#ifdef __APPLE__
/* macOS has no inotify: FSEvents reports changed files per path. A watch is
 * the index (+1) of its folder here; the stream is remade when one is added. */
#define MAX_WATCHED 256
static char *watched[MAX_WATCHED];
static int nwatched;
static FSEventStreamRef stream;
static dispatch_queue_t fs_queue;

static void fs_changed(ConstFSEventStreamRef ref, void *info, size_t n, void *paths, const FSEventStreamEventFlags flags[],
                       const FSEventStreamEventId ids[])
{
    (void)ref, (void)info, (void)ids;
    const FSEventStreamEventFlags wanted = kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemModified |
                                           kFSEventStreamEventFlagItemRenamed | kFSEventStreamEventFlagItemInodeMetaMod;
    for (size_t i = 0; i < n; i++) {
        if (!(flags[i] & wanted) || !(flags[i] & kFSEventStreamEventFlagItemIsFile))
            continue;
        char dir[4096];
        snprintf(dir, sizeof dir, "%s", ((char **)paths)[i]);
        char *slash = strrchr(dir, '/');
        if (!slash)
            continue;
        *slash = 0;
        const char *name = slash + 1, *parent = *dir ? dir : "/";
        for (int k = 0; k < nwatched; k++)
            if (!strcmp(watched[k], parent))
                post(IPC_FILE, xstrdup(name), k + 1);
    }
}

/* on fs_queue */
static void restart_stream(void)
{
    if (stream) {
        FSEventStreamStop(stream);
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
        stream = NULL;
    }
    if (!nwatched)
        return;
    CFMutableArrayRef list = CFArrayCreateMutable(NULL, nwatched, &kCFTypeArrayCallBacks);
    for (int k = 0; k < nwatched; k++) {
        CFStringRef str = CFStringCreateWithCString(NULL, watched[k], kCFStringEncodingUTF8);
        if (str) {
            CFArrayAppendValue(list, str);
            CFRelease(str);
        }
    }
    stream = FSEventStreamCreate(NULL, fs_changed, NULL, list, kFSEventStreamEventIdSinceNow, 0.05,
                                 kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
    CFRelease(list);
    if (stream) {
        FSEventStreamSetDispatchQueue(stream, fs_queue);
        FSEventStreamStart(stream);
    }
}

static int watch_dir(const char *dir)
{
    char real[PATH_MAX];
    if (!fs_queue || !realpath(dir, real)) /* FSEvents names files by their real path (/private/tmp) */
        return -1;
    __block int wd = -1;
    const char *want = real;
    dispatch_sync(fs_queue, ^{
        for (int k = 0; k < nwatched; k++)
            if (!strcmp(watched[k], want))
                wd = k + 1;
        if (wd < 0 && nwatched < MAX_WATCHED) {
            watched[nwatched++] = xstrdup(want);
            wd = nwatched;
            restart_stream();
        }
    });
    return wd;
}
#else
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

static int watch_dir(const char *dir)
{
    return notify_fd < 0 ? -1 : inotify_add_watch(notify_fd, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
}
#endif

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
            if (c >= 0) {
#ifdef __APPLE__
                uid_t uid;
                gid_t gid;
                if (getpeereid(c, &uid, &gid) == 0 && uid == geteuid())
#else
                struct ucred cred;
                socklen_t len = sizeof cred;
                if (getsockopt(c, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0 && cred.uid == geteuid())
#endif
                    read_request(c); /* only requests from you */
                else
                    close(c);
            }
        }
#ifndef __APPLE__
        if (fds[1].revents & POLLIN)
            read_notify();
#endif
    }
}

void ipc_start(void)
{
    ipc_event = SDL_RegisterEvents(1);
#ifdef __APPLE__
    fs_queue = dispatch_queue_create("nomadys-fsevents", DISPATCH_QUEUE_SERIAL);
#else
    notify_fd = inotify_init1(IN_CLOEXEC);
#endif
    struct sockaddr_un a;
    socket_path(&a);
    server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#ifdef __APPLE__
    if (server_fd >= 0)
        fcntl(server_fd, F_SETFD, FD_CLOEXEC); /* macOS has no SOCK_CLOEXEC */
#endif
    mode_t old_umask = umask(0077); /* the socket file is for you alone (it can sit in /tmp) */
    bool bound = server_fd >= 0 && bind(server_fd, (struct sockaddr *)&a, sizeof a) == 0;
    umask(old_umask);
    if (server_fd >= 0 && !bound) {
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

/* A folder whose files matter while running (the config, themes); its
 * events come as IPC_FILE with this watch. Returns it, or -1. */
int ipc_watch_dir(const char *dir)
{
    return watch_dir(dir);
}

void ipc_watch(Doc *d)
{
    if (d->watch_wd > 0)
        return;
    char dir[4096];
    snprintf(dir, sizeof dir, "%s", d->path);
    char *slash = strrchr(dir, '/');
    if (!slash)
        return;
    *slash = 0;
    d->watch_wd = watch_dir(*dir ? dir : "/");
}
