#pragma once
#include <cstdint>
#include <vector>
using SceFontHandle = void *;
using SceFontLibHandle = void *;
enum SceFontLanguageCode
{
    SCE_FONT_LANGUAGE_JAPANESE = 1,
    SCE_FONT_LANGUAGE_LATIN = 2,
    SCE_FONT_LANGUAGE_KOREAN = 3,
    SCE_FONT_LANGUAGE_CHINESE = 4
};
struct SceFontNewLibParams
{
    unsigned numFonts = 0;
    void *(*allocFunc)(void *, unsigned) = nullptr;
    void (*freeFunc)(void *, void *) = nullptr;
};
struct SceFontStyle
{
    float fontH = 0, fontV = 0;
    unsigned fontLanguage = 0;
};
struct SceFontCharInfo
{
    unsigned bitmapWidth = 0, bitmapHeight = 0, bitmapLeft = 0, bitmapTop = 0;
    int sfp26AdvanceH = 0;
};
struct SceFontGlyphImage
{
    unsigned pixelFormat = 0;
    int xPos64 = 0, yPos64 = 0;
    unsigned short bufWidth = 0, bufHeight = 0, bytesPerLine = 0;
    std::uintptr_t bufferPtr = 0;
};
constexpr int SCE_FONT_PIXELFORMAT_8 = 2, SCE_GXM_TEXTURE_FORMAT_U8_R111 = 0,
              SCE_GXM_TEXTURE_FILTER_LINEAR = 1;
struct vita2d_texture
{
    unsigned width, height, stride;
    std::vector<unsigned char> data;
};
SceFontLibHandle sceFontNewLib(SceFontNewLibParams *, unsigned *);
int sceFontFindOptimumFont(SceFontLibHandle, SceFontStyle *, unsigned *);
SceFontHandle sceFontOpen(SceFontLibHandle, int, int, unsigned *);
int sceFontGetCharInfo(SceFontHandle, unsigned, SceFontCharInfo *);
int sceFontGetCharGlyphImage(SceFontHandle, unsigned, SceFontGlyphImage *);
int sceFontClose(SceFontHandle);
int sceFontDoneLib(SceFontLibHandle);
vita2d_texture *vita2d_create_empty_texture_format(unsigned, unsigned, int);
void vita2d_free_texture(vita2d_texture *);
unsigned vita2d_texture_get_stride(const vita2d_texture *);
void *vita2d_texture_get_datap(const vita2d_texture *);
void vita2d_texture_set_filters(vita2d_texture *, int, int);
void vita2d_draw_texture_tint_part_scale(const vita2d_texture *, float, float, float, float, float, float, float, float, unsigned);