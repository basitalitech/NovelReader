#pragma once
struct vita2d_font { int id; };
struct vita2d_pgf {};
vita2d_font* vita2d_load_font_file(const char*);
void vita2d_free_font(vita2d_font*);
int vita2d_font_text_width(vita2d_font*,unsigned,const char*);
void vita2d_font_draw_text(vita2d_font*,int,int,unsigned,unsigned,const char*);
int vita2d_pgf_text_width(vita2d_pgf*,float,const char*);
void vita2d_pgf_draw_text(vita2d_pgf*,int,int,unsigned,float,const char*);
