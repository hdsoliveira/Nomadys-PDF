/* macOS stand-ins for the Linux/glibc calls the reader uses. The Makefile
 * force-includes this file (-include) on Darwin only, so Linux builds never see it. */
#pragma once
#ifdef __APPLE__

#define __STDC_WANT_LIB_EXT1__ 1 /* memset_s */
#include <fcntl.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* struct stat names its nanosecond times differently */
#define st_mtim st_mtimespec

/* memset that the compiler may not drop */
static inline void explicit_bzero(void *p, size_t n) { memset_s(p, n, 0, n); }

/* no SOCK_CLOEXEC flag: sockets get FD_CLOEXEC after the fact where it matters */
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

static inline int accept4(int fd, struct sockaddr *a, socklen_t *len, int flags)
{
    (void)flags;
    int c = accept(fd, a, len);
    if (c >= 0)
        fcntl(c, F_SETFD, FD_CLOEXEC);
    return c;
}

/* readlink("/proc/self/exe") is how the program finds itself; macOS has no
 * /proc, so that one path is answered by dyld instead. */
static inline ssize_t npdf_readlink(const char *path, char *buf, size_t cap)
{
    if (strcmp(path, "/proc/self/exe") != 0)
        return readlink(path, buf, cap);
    char raw[PATH_MAX], real[PATH_MAX];
    uint32_t size = sizeof raw;
    if (_NSGetExecutablePath(raw, &size) != 0 || !realpath(raw, real))
        return -1;
    size_t n = strlen(real);
    if (n > cap)
        n = cap;
    memcpy(buf, real, n); /* like readlink: not terminated */
    return (ssize_t)n;
}
#define readlink npdf_readlink

#endif
