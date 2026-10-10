/* libFuzzer: an image the user inserts (:image, paste) is decoded, resampled and put in a PDF.
 * The first 4 bytes choose the size of the place for it, the rest is the file. */
#include "../../src/image.c"

static FPDF_DOCUMENT doc;
static FPDF_PAGE page;
static Doc d;

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc, (void)argv;
    FPDF_LIBRARY_CONFIG lib = {.version = 2};
    FPDF_InitLibraryWithConfig(&lib);
    doc = FPDF_CreateNewDocument();
    page = FPDFPage_New(doc, 0, 612, 792);
    d.pdf = doc;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 5 || size > IMAGE_FILE_MAX)
        return 0;
    int iw, ih, comp; /* insert() refuses what is too large before it gets here; fuzzing is kept to 4 megapixels */
    if (!stbi_info_from_memory(data + 4, (int)(size - 4), &iw, &ih, &comp) || iw <= 0 || ih <= 0 ||
        (long long)iw * ih > 4000000)
        return 0;
    float w = 1 + data[0] * 3.0f, h = 1 + data[1] * 3.0f; /* points: 1 to 766 */
    PRect r = {10, 10 + h, 10 + w, 10};
    char err[200];
    FPDF_PAGEOBJECT obj = make_image(&d, page, data + 4, size - 4, r, err, sizeof err);
    if (obj)
        FPDFPageObj_Destroy(obj);
    SDL_Surface *s = image_surface(data + 4, size - 4);
    if (s)
        SDL_DestroySurface(s);
    return 0;
}
