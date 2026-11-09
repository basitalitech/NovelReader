#include "vita_fonts.hpp"
#include "font_backend.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int failed_face=-1;
struct Draw { int face,x,width; std::string text; };
std::vector<Draw> draws;
int face_id(const char* path) {
    const std::string name=path;
    if(name.find("Semibold")!=std::string::npos)return 1;
    if(name.find("Literata")!=std::string::npos)return 2;
    if(name.find("Atkinson")!=std::string::npos)return 3;
    return 0;
}
}
int FT_Init_FreeType(FT_Library* library){*library=new int(0);return 0;}
int FT_New_Face(FT_Library,const char* path,long,FT_Face* face) {
    if(face_id(path)==failed_face)return 1;
    *face=new MockFace{face_id(path)};return 0;
}
int FT_Select_Charmap(FT_Face,int){return 0;}
unsigned FT_Get_Char_Index(FT_Face,unsigned long code){return code<128?1:0;}
void FT_Done_Face(FT_Face face){delete face;}
void FT_Done_FreeType(FT_Library library){delete library;}
vita2d_font* vita2d_load_font_file(const char* path){return new vita2d_font{face_id(path)};}
void vita2d_free_font(vita2d_font* font){delete font;}
int vita2d_font_text_width(vita2d_font* font,unsigned pixels,const char* text){return (font->id+1)*pixels*std::strlen(text);}
void vita2d_font_draw_text(vita2d_font* font,int x,int,unsigned,unsigned pixels,const char* text) {
    draws.push_back({font->id,x,vita2d_font_text_width(font,pixels,text),text});
}
int vita2d_pgf_text_width(vita2d_pgf*,float scale,const char* text){return static_cast<int>(7*scale*std::strlen(text));}
void vita2d_pgf_draw_text(vita2d_pgf* font,int x,int,unsigned,float scale,const char* text) {
    draws.push_back({4,x,vita2d_pgf_text_width(font,scale,text),text});
}
int main() {
    try {
        vita2d_pgf pgf;
        reader::VitaFonts fonts(&pgf);
        check(fonts.complete(),"Bundled reading fonts are not loaded");
        const reader::FontRole roles[]={reader::FontRole::Interface,reader::FontRole::Heading,
            reader::FontRole::Serif,reader::FontRole::Sans,reader::FontRole::System};
        const int widths[]={60,120,180,240,21};
        for(int i=0;i<5;++i) {
            check(fonts.width(roles[i],20,"ABC")==widths[i],"Font role uses the wrong face");
            draws.clear();fonts.draw(roles[i],20,42,80,0,"ABC");
            check(draws.size()==1 && draws[0].face==i,"Drawing differs from the selected face");
            check(draws[0].x==42 && draws[0].width==widths[i],"Measurement and rendering disagree");
            draws.clear();const std::string mixed="AΩB";
            const auto width=fonts.width(roles[i],26,mixed);
            fonts.draw(roles[i],26,19,80,0,mixed);
            int end=19;
            for(const auto& draw:draws){check(draw.x==end,"Fallback glyph changed run positions");end+=draw.width;}
            check(end-19==width,"Mixed glyph fallback has inconsistent measurement");
            if(i<4)check(draws.size()==3 && draws[1].face==4,"Missing glyph does not use PGF fallback");
        }
        failed_face=2;
        reader::VitaFonts missing(&pgf);
        check(!missing.complete(),"Missing reader font is not reported");
        check(missing.width(reader::FontRole::Serif,20,"ABC")==21,"Missing reader font does not fall back safely");
        check(missing.width(reader::FontRole::Sans,20,"ABC")==240,"A missing font breaks other reading faces");
        std::cout<<"Vita font routing and fallback tests passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
