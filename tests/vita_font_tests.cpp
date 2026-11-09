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
int font_loads=0;
int texture_loads=0,texture_frees=0;
struct TextureDraw { const vita2d_texture* texture; int x,y; };
std::vector<TextureDraw> texture_draws;
void check_size(vita2d_font* font,unsigned pixels) {
    if(!font->raster_size)font->raster_size=pixels;
    check(font->raster_size==pixels,"A vita2d glyph atlas was reused across pixel sizes");
}
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
    *face=new MockFace{};(*face)->id=face_id(path);return 0;
}
int FT_Select_Charmap(FT_Face,int){return 0;}
unsigned FT_Get_Char_Index(FT_Face,unsigned long code){return code<128?code:0;}
int FT_Set_Pixel_Sizes(FT_Face face,unsigned,unsigned pixels){face->pixels=pixels;return 0;}
int FT_Load_Glyph(FT_Face face,unsigned glyph,int) {
    static unsigned char bitmap[]={255,128,0,0,64,32,0,0,16,8,0,0};
    face->storage.advance.x=(face->pixels/2)*64;
    face->storage.bitmap_left=glyph=='A'?-1:0;
    face->storage.bitmap_top=glyph=='g'?-1:static_cast<int>(face->pixels)-5;
    face->storage.bitmap={3,2,glyph=='N'?-4:4,bitmap+(glyph=='N'?8:0),FT_PIXEL_MODE_GRAY};
    return 0;
}
int FT_Get_Kerning(FT_Face,unsigned left,unsigned right,unsigned,FT_Vector* delta){delta->x=left=='A' && right=='V'?-64:0;return 0;}
vita2d_texture* vita2d_create_empty_texture_format(unsigned w,unsigned h,int) {
    ++texture_loads;const auto stride=(w+7)&~7u;
    return new vita2d_texture{w,h,stride,false,std::vector<unsigned char>(stride*h,0xcc)};
}
void vita2d_free_texture(vita2d_texture* texture){++texture_frees;delete texture;}
unsigned vita2d_texture_get_stride(const vita2d_texture* texture){return texture->stride;}
void* vita2d_texture_get_datap(const vita2d_texture* texture){return const_cast<unsigned char*>(texture->data.data());}
void vita2d_texture_set_filters(vita2d_texture* texture,int,int){texture->point=true;}
void vita2d_draw_texture_tint(const vita2d_texture* texture,float x,float y,unsigned){texture_draws.push_back({texture,static_cast<int>(x),static_cast<int>(y)});}
void FT_Done_Face(FT_Face face){delete face;}
void FT_Done_FreeType(FT_Library library){delete library;}
vita2d_font* vita2d_load_font_file(const char* path){++font_loads;return new vita2d_font{face_id(path)};}
void vita2d_free_font(vita2d_font* font){delete font;}
int vita2d_font_text_width(vita2d_font* font,unsigned pixels,const char* text){check_size(font,pixels);return (font->id+1)*pixels*std::strlen(text);}
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
        // Reproduce Home's mix of title, subtitle, chapter and percentage sizes.
        // Measurement also populates vita2d's atlas, so exercise both paths.
        fonts.width(reader::FontRole::Interface,34,"Welcome Back 24");
        fonts.width(reader::FontRole::Interface,22,"Pick up your story 2");
        fonts.draw(reader::FontRole::Interface,21,42,80,0,"Chapter 1 of 243");
        fonts.draw(reader::FontRole::Interface,22,42,80,0,"Pick up your story");
        const int cached_loads=font_loads;
        for(int i=0;i<20;++i) {
            fonts.width(reader::FontRole::Interface,21,"Chapter 3 of 24");
            fonts.draw(reader::FontRole::Interface,21,42,80,0,"Chapter 3 of 24");
            fonts.draw(reader::FontRole::Interface,21,42,80,0,"0%");
            fonts.draw(reader::FontRole::Interface,22,42,80,0,"Pick up your story");
        }
        check(font_loads==cached_loads,"Font atlases are being recreated during repeated rendering");
        {
            reader::VitaFonts title_fonts(&pgf);
            check(title_fonts.title_width(25,"AV")==23,"Title measurement ignored native-size kerning");
            draws.clear();texture_draws.clear();
            title_fonts.draw_title(25,42,80,0,"AVg");
            check(draws.empty() && texture_draws.size()==1,"Title still uses the shared glyph atlas");
            const auto texture=texture_draws.back().texture;
            check(texture->point && texture_draws.back().x==39 && texture_draws.back().y==58,"Title bearing or baseline changed");
            check(texture->height==28,"Title texture clipped the descender");
            for(unsigned row=0;row<texture->height;++row)for(unsigned col=0;col<texture->stride;++col)
                if(row<2 || row>=texture->height-2 || col<2 || col>=texture->width-2)
                    check(texture->data[row*texture->stride+col]==0,"Title texture has uninitialized edge padding");
            check(texture->data[2*texture->stride+2]==255 && texture->data[2*texture->stride+3]==128,"Glyph alpha or source stride was copied incorrectly");
            const auto cached_textures=texture_loads;
            for(int i=0;i<10;++i)title_fonts.draw_title(25,42,80,0,"AVg");
            check(texture_loads==cached_textures,"Stable title is rerasterized every frame");
            title_fonts.draw_title(25,42,80,0,"N");
            check(texture_draws.back().texture->data[2*texture_draws.back().texture->stride+2]==16,"Negative bitmap pitch is mishandled");
            draws.clear();title_fonts.draw_title(25,42,80,0,"AΩB");
            check(draws.size()==3 && draws[1].face==4,"Title path broke missing-glyph fallback");
            for(int i=0;i<20;++i)title_fonts.draw_title(25,42,80,0,"Title "+std::to_string(i));
            check(texture_loads-texture_frees==8,"Title texture cache is not bounded");
        }
        check(texture_loads==texture_frees,"Title textures leaked after font shutdown");
        failed_face=2;
        reader::VitaFonts missing(&pgf);
        check(!missing.complete(),"Missing reader font is not reported");
        check(missing.width(reader::FontRole::Serif,20,"ABC")==21,"Missing reader font does not fall back safely");
        check(missing.width(reader::FontRole::Sans,20,"ABC")==240,"A missing font breaks other reading faces");
        std::cout<<"Vita font routing and fallback tests passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
