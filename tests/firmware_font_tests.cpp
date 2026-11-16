#include "../src/firmware_fonts.hpp"
#include <iostream>
#include <stdexcept>

namespace
{
    void check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    int loads = 0, frees = 0, rasters = 0, lookups = 0, opens = 0, closes = 0, libraries = 0;
    unsigned last_language = 0, last_code = 0;
    bool allocation_failure = false;
    float drawn_x = 0, drawn_y = 0, drawn_scale = 0;
}
SceFontLibHandle sceFontNewLib(SceFontNewLibParams *params, unsigned *error)
{
    check(params->numFonts == 4 && params->allocFunc && params->freeFunc, "Missing firmware fonts or allocator");
    ++libraries;
    *error = 0;
    return new int(0);
}
int sceFontFindOptimumFont(SceFontLibHandle, SceFontStyle *style, unsigned *error)
{
    check(style->fontH == 10 && style->fontV == 10, "Firmware native size differs from PGF scale");
    *error = 0;
    return style->fontLanguage;
}
SceFontHandle sceFontOpen(SceFontLibHandle, int index, int, unsigned *error)
{
    ++opens;
    *error = 0;
    return new int(index);
}
int sceFontGetCharInfo(SceFontHandle font, unsigned code, SceFontCharInfo *info)
{
    ++lookups;
    const int language = *static_cast<int *>(font);
    const bool supported = code < 128 ||
                           (language == SCE_FONT_LANGUAGE_JAPANESE && code >= 0x3000 && code <= 0x30ff) ||
                           (language == SCE_FONT_LANGUAGE_KOREAN && code >= 0xac00 && code <= 0xd7af) ||
                           (language == SCE_FONT_LANGUAGE_CHINESE && code >= 0x3400 && code <= 0x9fff);
    *info = {};
    if (supported)
        *info = {code == ' ' ? 0u : 10u, 12, 1, 9, 11 * 64};
    return 0;
}
int sceFontGetCharGlyphImage(SceFontHandle font, unsigned code, SceFontGlyphImage *image)
{
    ++rasters;
    last_language = *static_cast<int *>(font);
    last_code = code;
    const auto left = image->xPos64 / 64, top = image->yPos64 / 64;
    check(image->pixelFormat == SCE_FONT_PIXELFORMAT_8 && left >= 2 && top >= 2 && left + 12 <= 512 && top + 14 <= 512,
          "Firmware glyph texture has no transparent padding");
    auto *data = reinterpret_cast<unsigned char *>(image->bufferPtr);
    for (int y = top - 2; y < top + 14; ++y)
        for (int x = left - 2; x < left + 12; ++x)
            check(!data[y * image->bytesPerLine + x], "Firmware bitmap is not initialized");
    data[top * image->bytesPerLine + left] = 255;
    return 0;
}
int sceFontClose(SceFontHandle font)
{
    ++closes;
    delete static_cast<int *>(font);
    return 0;
}
int sceFontDoneLib(SceFontLibHandle library)
{
    delete static_cast<int *>(library);
    return 0;
}
vita2d_texture *vita2d_create_empty_texture_format(unsigned width, unsigned height, int)
{
    if (allocation_failure)
        return nullptr;
    ++loads;
    const auto stride = (width + 7) & ~7u;
    return new vita2d_texture{width, height, stride, std::vector<unsigned char>(stride * height, 0xcc)};
}
void vita2d_free_texture(vita2d_texture *texture)
{
    ++frees;
    delete texture;
}
unsigned vita2d_texture_get_stride(const vita2d_texture *texture) { return texture->stride; }
void *vita2d_texture_get_datap(const vita2d_texture *texture) { return const_cast<unsigned char *>(texture->data.data()); }
void vita2d_texture_set_filters(vita2d_texture *, int, int) {}
void vita2d_draw_texture_tint_part_scale(const vita2d_texture *texture, float x, float y,
                                         float tx, float ty, float width, float height, float sx, float sy, unsigned)
{
    check(sx == sy, "Firmware glyph scale is distorted");
    check(tx >= 0 && ty >= 0 && tx + width <= texture->width && ty + height <= texture->height,
          "Firmware glyph sampling exceeds its atlas");
    drawn_x = x;
    drawn_y = y;
    drawn_scale = sx;
}
int main()
{
    try
    {
        {
            reader::FirmwareFonts fonts;
            check(!libraries && !opens && !loads, "Startup eagerly loads firmware fonts or allocates atlases");
            check(fonts.available() && libraries == 1 && !opens, "Availability check opens unused font languages");
            for (unsigned code = 0x3400; code < 0x8000; ++code)
                check(fonts.advance(26, code) == 14, "CJK advance was lost in a large book");
            check(!loads && !rasters, "Firmware measurement allocated GPU glyphs");
            check(opens == 1, "CJK measurements opened unrelated font languages");
            const auto count = lookups;
            for (int i = 0; i < 100; ++i)
                fonts.advance(30, 0x7fff);
            check(lookups == count, "Firmware metric cache is not reused across sizes");
            const unsigned codes[] = {'A', 0x65e5, 0x3042, 0xd55c};
            const unsigned languages[] = {SCE_FONT_LANGUAGE_LATIN, SCE_FONT_LANGUAGE_CHINESE,
                                          SCE_FONT_LANGUAGE_JAPANESE, SCE_FONT_LANGUAGE_KOREAN};
            for (int i = 0; i < 4; ++i)
            {
                fonts.draw(30, codes[i], 40, 80, 0);
                check(last_code == codes[i] && last_language == languages[i], "Wrong firmware language selected");
                check(drawn_x == 38.5f && drawn_y == 63.5f && drawn_scale == 1.5f, "Firmware bearing or scale changed");
            }
            check(opens == 4, "Firmware languages were not loaded on demand");
            check(loads == 1, "Firmware glyphs allocate a texture per character");
            const auto cached = rasters;
            fonts.draw(22, 0x65e5, 0, 80, 0);
            check(rasters == cached, "Firmware glyph is rasterized again at another size");
            fonts.draw(26, 0x1f600, 0, 80, 0);
            check(last_code == '?', "Unsupported firmware scalar has no visible replacement");
            const auto before = frees;
            for (unsigned code = 0x4e00; code < 0x6800; ++code)
                fonts.draw(26, code, 0, 80, 0);
            check(frees == before, "A glyph texture was freed while its GPU draw was in flight");
            check(loads <= 7, "Firmware page turns allocate individual glyph textures");
            fonts.finish_frame();
            check(loads - frees == 4, "Firmware atlas cache is unbounded");
            {
                reader::FirmwareFonts failed;
                allocation_failure = true;
                failed.draw(26, 0x65e6, 0, 80, 0);
                allocation_failure = false;
                check(failed.advance(26, 0x65e6) == 14, "Atlas allocation failure changed text metrics");
            }
        }
        check(loads == frees && opens == closes, "Firmware font resources leaked");
        std::cout << "Firmware font metrics, languages and cache tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}