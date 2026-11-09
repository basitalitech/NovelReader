#pragma once
struct MockFace { int id; };
using FT_Face = MockFace*;
using FT_Library = int*;
constexpr int FT_ENCODING_UNICODE=0;
int FT_Init_FreeType(FT_Library*);
int FT_New_Face(FT_Library,const char*,long,FT_Face*);
int FT_Select_Charmap(FT_Face,int);
unsigned FT_Get_Char_Index(FT_Face,unsigned long);
void FT_Done_Face(FT_Face);
void FT_Done_FreeType(FT_Library);
