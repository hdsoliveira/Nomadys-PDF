/* libFuzzer: the configuration and theme files (they can be copied from anywhere). */
#include "../../src/config.c"
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 100000)
        return 0;
    char path[64];
    snprintf(path, sizeof path, "/tmp/npdf-fuzz-%d.conf", (int)getpid());
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    fwrite(data, 1, size, f);
    fclose(f);
    Theme light = {0}, dark = {0};
    parse_file(path, 0, &light, &dark, 0);
    unlink(path);
    return 0;
}
