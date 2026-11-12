#pragma once
struct FT_Vector
{
    long x = 0, y = 0;
};
struct MockBitmap
{
    unsigned rows = 0, width = 0;
    int pitch = 0;
    unsigned char *buffer = nullptr;
    unsigned char pixel_mode = 0;
};
struct MockGlyph
{
    FT_Vector advance;
    int bitmap_left = 0, bitmap_top = 0;
    MockBitmap bitmap;
};
using FT_GlyphSlot = MockGlyph *;
struct MockFace
{
    int id;
    unsigned pixels = 0;
    MockGlyph storage;
    FT_GlyphSlot glyph = &storage;
};
using FT_Face = MockFace *;
using FT_Library = int *;
constexpr int FT_ENCODING_UNICODE = 0;
constexpr int FT_LOAD_DEFAULT = 0, FT_LOAD_RENDER = 1, FT_LOAD_TARGET_NORMAL = 0, FT_KERNING_DEFAULT = 0;
constexpr unsigned char FT_PIXEL_MODE_MONO = 1, FT_PIXEL_MODE_GRAY = 2;
#define FT_HAS_KERNING(face) true
int FT_Init_FreeType(FT_Library *);
int FT_New_Face(FT_Library, const char *, long, FT_Face *);
int FT_Select_Charmap(FT_Face, int);
unsigned FT_Get_Char_Index(FT_Face, unsigned long);
int FT_Set_Pixel_Sizes(FT_Face, unsigned, unsigned);
int FT_Load_Glyph(FT_Face, unsigned, int);
int FT_Get_Kerning(FT_Face, unsigned, unsigned, unsigned, FT_Vector *);
void FT_Done_Face(FT_Face);
void FT_Done_FreeType(FT_Library);