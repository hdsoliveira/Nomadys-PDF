/* ~/.config/nomadys-pdf/config: "key = value" lines, '#' starts a comment. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomadys.h"

Config cfg = {
    .dark = false,
    .autosave = true,
    .highlight = 0xFFE14D,
    .zoom = 0,
    .dark_lo = 0x1C,
    .dark_hi = 0xE4,
    .bg_light = 0xD9D9D9,
    .bg_dark = 0x101010,
    .autosave_delay_ms = 1000,
};

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

static bool parse_bool(const char *v)
{
    return !strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on") || !strcmp(v, "1");
}

static uint32_t parse_color(const char *v, uint32_t fallback)
{
    if (*v == '#')
        v++;
    char *end;
    unsigned long c = strtoul(v, &end, 16);
    return (*end || end - v != 6) ? fallback : (uint32_t)c;
}

void config_load(void)
{
    char path[1024];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(path, sizeof path, "%s/nomadys-pdf/config", xdg);
    else if (home)
        snprintf(path, sizeof path, "%s/.config/nomadys-pdf/config", home);
    else
        return;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[512];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        n++;
        char *hash = strchr(line, '#');
        if (hash && (hash == line || isspace((unsigned char)hash[-1])))
            *hash = 0;
        char *eq = strchr(line, '=');
        if (!eq) {
            if (*trim(line))
                fprintf(stderr, "nomadys-pdf: %s:%d: expected key = value\n", path, n);
            continue;
        }
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        if (!strcmp(k, "theme"))
            cfg.dark = !strcmp(v, "dark");
        else if (!strcmp(k, "autosave"))
            cfg.autosave = parse_bool(v);
        else if (!strcmp(k, "show_tabs"))
            cfg.show_tabs = parse_bool(v);
        else if (!strcmp(k, "subpixel_text"))
            cfg.subpixel_text = parse_bool(v);
        else if (!strcmp(k, "autosave_delay_ms"))
            cfg.autosave_delay_ms = atoi(v);
        else if (!strcmp(k, "highlight_color"))
            cfg.highlight = parse_color(v, cfg.highlight);
        else if (!strcmp(k, "background_light"))
            cfg.bg_light = parse_color(v, cfg.bg_light);
        else if (!strcmp(k, "background_dark"))
            cfg.bg_dark = parse_color(v, cfg.bg_dark);
        else if (!strcmp(k, "dark_contrast_low"))
            cfg.dark_lo = (uint8_t)atoi(v);
        else if (!strcmp(k, "dark_contrast_high"))
            cfg.dark_hi = (uint8_t)atoi(v);
        else if (!strcmp(k, "zoom"))
            cfg.zoom = !strcmp(v, "fit-width") ? 0 : atof(v) / 100.0;
        else
            fprintf(stderr, "nomadys-pdf: %s:%d: unknown key '%s'\n", path, n, k);
    }
    fclose(f);
}
