#pragma once
#include <vector>
struct vita2d_font { int id; unsigned raster_size=0; };
struct vita2d_pgf {};
struct vita2d_texture { unsigned width,height,stride; bool point=false; std::vector<unsigned char> data; };
constexpr int SCE_GXM_TEXTURE_FORMAT_U8_R111=1,SCE_GXM_TEXTURE_FILTER_POINT=0;
vita2d_texture* vita2d_create_empty_texture_format(unsigned,unsigned,int);
void vita2d_free_texture(vita2d_texture*);
unsigned vita2d_texture_get_stride(const vita2d_texture*);
void* vita2d_texture_get_datap(const vita2d_texture*);
void vita2d_texture_set_filters(vita2d_texture*,int,int);
void vita2d_draw_texture_tint(const vita2d_texture*,float,float,unsigned);
vita2d_font* vita2d_load_font_file(const char*);
void vita2d_free_font(vita2d_font*);
int vita2d_font_text_width(vita2d_font*,unsigned,const char*);
void vita2d_font_draw_text(vita2d_font*,int,int,unsigned,unsigned,const char*);
int vita2d_pgf_text_width(vita2d_pgf*,float,const char*);
void vita2d_pgf_draw_text(vita2d_pgf*,int,int,unsigned,float,const char*);
