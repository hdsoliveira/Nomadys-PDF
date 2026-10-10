/* libFuzzer: text and strings that come from files. The first byte picks what is tried. */
#include "../../src/cite.c"
#include "../../src/textbox.c"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > 100000)
        return 0;
    char *s = malloc(size + 1);
    memcpy(s, data + 1, size - 1);
    s[size - 1] = 0;
    switch (data[0] % 5) {
    case 0: { /* the layout of a text box, with the size and width taken from the text's first bytes */
        float sz = 4 + (size > 1 ? (uint8_t)s[0] % 120 : 12), wd = 20 + (size > 2 ? (uint8_t)s[1] * 4.0f : 200);
        Layout L;
        layout_run(s, sz, wd, &L);
        free(L.g);
        break;
    }
    case 1: { /* a text box as stored in the file */
        TextBox b;
        if (textbox_decode(s, &b)) {
            Layout L;
            layout_run(b.text, SDL_min(b.size, 1000), SDL_min(b.width, 100000), &L);
            free(L.g);
            free(b.text);
        }
        break;
    }
    case 2: {
        char out[256];
        find_doi(s, out, sizeof out);
        find_arxiv(s, out, sizeof out);
        break;
    }
    case 3: {
        char out[1100];
        url_part(out, sizeof out, s);
        break;
    }
    default: {
        int a, b;
        pages_parse_range(s, 1 + (uint8_t)data[0] % 50, &a, &b);
    }
    }
    free(s);
    return 0;
}
