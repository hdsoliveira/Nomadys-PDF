/* A new version, asked of GitHub with curl: once a day when check_updates is
 * on, or at once with :update. Nothing is sent but the request itself, and
 * nothing is installed: you only read that a version is out. */
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "nomadys.h"

#define LATEST_URL "https://api.github.com/repos/hdsoliveira/Nomadys-PDF/releases/latest"
#define PAGE_URL "github.com/hdsoliveira/Nomadys-PDF/releases"
#define DAY 86400

extern char **environ;

static pid_t pid;
static bool forced;
static char out_path[4200];

/* "v0.6.1" -> 601 (major * 10000 + minor * 100 + patch, each under 100) */
static int version_number(const char *s)
{
    int a = 0, b = 0, c = 0;
    if (*s == 'v')
        s++;
    sscanf(s, "%d.%d.%d", &a, &b, &c);
    return a * 10000 + b * 100 + c;
}

bool update_busy(void) { return pid > 0; }

void update_check(bool now)
{
    if (pid > 0)
        return;
    char stamp[4200];
    data_path("update-check", stamp, sizeof stamp);
    struct stat st;
    if (!now && stat(stamp, &st) == 0 && time(NULL) - st.st_mtime < DAY)
        return;
    FILE *f = fopen(stamp, "w"); /* the day starts now, whether or not it works */
    if (f)
        fclose(f);
    data_path("update.json", out_path, sizeof out_path);
    unlink(out_path);
    char *argv[] = {"curl", "-fsSL", "--max-time", "10", "-H", "Accept: application/vnd.github+json", "-o", out_path,
                    LATEST_URL, NULL};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    forced = now;
    if (posix_spawnp(&pid, "curl", &fa, NULL, argv, environ))
        pid = 0;
    posix_spawn_file_actions_destroy(&fa);
}

/* True when there is something to say (in msg). */
bool update_poll(char *msg, size_t cap)
{
    int status;
    if (pid <= 0 || waitpid(pid, &status, WNOHANG) != pid)
        return false;
    pid = 0;
    char tag[64] = "";
    FILE *f = WIFEXITED(status) && WEXITSTATUS(status) == 0 ? fopen(out_path, "r") : NULL;
    char line[4096];
    while (f && fgets(line, sizeof line, f)) { /* "tag_name": "v0.6.1" */
        const char *k = strstr(line, "\"tag_name\"");
        const char *q = k ? strchr(k + 10, '"') : NULL;
        if (q) {
            snprintf(tag, sizeof tag, "%.*s", (int)strcspn(q + 1, "\""), q + 1);
            break;
        }
    }
    if (f)
        fclose(f);
    unlink(out_path);
    if (*tag && version_number(tag) > version_number(NOMADYS_VERSION)) {
        snprintf(msg, cap, "Nomadys-PDF %s is out (this is " NOMADYS_VERSION "): " PAGE_URL, tag + (*tag == 'v'));
        return true;
    }
    if (!forced)
        return false;
    snprintf(msg, cap, *tag ? "Nomadys-PDF " NOMADYS_VERSION " is the latest version"
                            : "could not check for a new version (is curl installed, and are you online?)");
    return true;
}
