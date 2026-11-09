#include "vita_ui.hpp"
#include <vita2d.h>
#include <algorithm>
#include <cmath>

namespace reader { namespace visual {
unsigned mix(unsigned a, unsigned b, float amount) {
    unsigned result=0xff000000u;
    for (unsigned shift=0; shift<24; shift+=8)
        result |= static_cast<unsigned>(((a>>shift)&255)*(1-amount)+((b>>shift)&255)*amount)<<shift;
    return result;
}
void gradient(int x,int y,int width,int height,unsigned top,unsigned bottom) {
    for (int i=0;i<height;++i) vita2d_draw_rectangle(x,y+i,width,1,mix(top,bottom,float(i)/std::max(1,height-1)));
}
void rounded(int x,int y,int width,int height,unsigned fill,int radius) {
    radius=std::min(radius,std::min(width,height)/2);
    vita2d_draw_rectangle(x+radius,y,width-2*radius,height,fill);
    vita2d_draw_rectangle(x,y+radius,width,height-2*radius,fill);
    for (const auto cx:{x+radius,x+width-radius}) for (const auto cy:{y+radius,y+height-radius})
        vita2d_draw_fill_circle(cx,cy,radius,fill);
}
void panel(int x,int y,int width,int height,bool selected) {
    // Button surfaces stay flat. Future shared artwork belongs in this primitive.
    if (selected) rounded(x-3,y-3,width+6,height+6,color(60,40,83),11);
    rounded(x,y,width,height,selected?accent:border);
    rounded(x+1,y+1,width-2,height-2,selected?color(83,53,124):surface,8);
}
void progress(int x,int y,int width,float fraction,int height) {
    rounded(x,y,width,height,color(69,61,81),height/2);
    const int filled=static_cast<int>(width*std::max(0.f,std::min(1.f,fraction)));
    if (filled>0) {
        rounded(x,y,std::max(height,filled),height,accent,height/2);
        if (filled>height) vita2d_draw_line(x+height/2,y+1,x+filled-height/2,y+1,color(211,174,255));
    }
}
void icon(Icon kind,int x,int y,unsigned tint,float scale) {
    // A consistent two-pixel optical stroke keeps small icons legible on the
    // Vita's 960 x 544 display without relying on font glyphs.
    const auto line=[&](float a,float b,float c,float d) {
        const float x1=x+a*scale,y1=y+b*scale,x2=x+c*scale,y2=y+d*scale;
        const float dx=x2-x1,dy=y2-y1,length=std::sqrt(dx*dx+dy*dy);
        const float stroke=std::max(0.55f,0.65f*scale);
        const float ox=length>0.01f?-dy/length*stroke:0,oy=length>0.01f?dx/length*stroke:0;
        vita2d_draw_line(x1,y1,x2,y2,tint);
        vita2d_draw_line(x1+ox,y1+oy,x2+ox,y2+oy,tint);
        vita2d_draw_line(x1-ox,y1-oy,x2-ox,y2-oy,tint);
    };
    const auto circle=[&](float a,float b,float radius) {
        for(int i=0;i<40;++i) { const float t=i*6.2831853f/40,u=(i+1)*6.2831853f/40;
            line(a+radius*std::cos(t),b+radius*std::sin(t),a+radius*std::cos(u),b+radius*std::sin(u)); }
    };
    if(kind==Icon::Book) {
        line(-13,-10,-2,-8);line(-2,-8,0,-5);line(0,-5,2,-8);line(2,-8,13,-10);
        line(-13,-10,-13,10);line(13,-10,13,10);line(-13,10,-2,11);line(-2,11,0,13);line(0,13,2,11);line(2,11,13,10);line(0,-5,0,13);
        for(int i=0;i<3;++i){line(-10,-4+i*4,-4,-3+i*4);line(4,-3+i*4,10,-4+i*4);}
    } else if(kind==Icon::Home) {
        line(-13,0,0,-12);line(0,-12,13,0);line(-9,-3,-9,12);line(9,-3,9,12);line(-9,12,-3,12);line(3,12,9,12);line(-3,12,-3,3);line(-3,3,3,3);line(3,3,3,12);
    } else if(kind==Icon::Library) {
        for(int i=0;i<2;++i){int a=-12+i*8;line(a,-11,a,11);line(a,-11,a+5,-11);line(a+5,-11,a+5,11);line(a,11,a+5,11);line(a,-6,a+5,-6);}
        line(4,-10,9,-11);line(9,-11,14,10);line(14,10,9,11);line(9,11,4,-10);
    } else if(kind==Icon::Clock) {circle(0,0,12);line(0,-8,0,0);line(0,0,6,3);
    } else if(kind==Icon::Bookmark) {line(-8,-12,8,-12);line(-8,-12,-8,12);line(8,-12,8,12);line(-8,12,0,6);line(0,6,8,12);
    } else if(kind==Icon::Settings) {
        circle(0,0,10); circle(0,0,4);
        for(int i=0;i<8;++i){const float a=i*6.2831853f/8;line(10*std::cos(a),10*std::sin(a),14*std::cos(a),14*std::sin(a));}
    } else if(kind==Icon::Info) {circle(0,0,12);line(0,-2,0,7);line(-3,7,3,7);vita2d_draw_fill_circle(x,y-7*scale,std::max(1.f,1.6f*scale),tint);
    } else if(kind==Icon::Search) {circle(-2,-2,8);line(4,4,12,12);
    } else if(kind==Icon::Filter) {line(-12,-9,12,-9);line(12,-9,4,0);line(4,0,4,8);line(4,8,-3,12);line(-3,12,-3,0);line(-3,0,-12,-9);
    } else if(kind==Icon::Grid) {
        for(int a:{-10,2})for(int b:{-10,2}){line(a,b,a+8,b);line(a+8,b,a+8,b+8);line(a+8,b+8,a,b+8);line(a,b+8,a,b);}
    } else if(kind==Icon::Play) {line(-5,-9,8,0);line(8,0,-5,9);line(-5,9,-5,-9);
    } else if(kind==Icon::Chevron) {line(-3,-6,3,0);line(3,0,-3,6);
    } else if(kind==Icon::Check) {line(-8,0,-2,6);line(-2,6,9,-7);
    } else if(kind==Icon::Trash) {line(-10,-8,10,-8);line(-4,-12,4,-12);line(-7,-8,-6,12);line(-6,12,6,12);line(6,12,7,-8);line(-2,-4,-2,8);line(2,-4,2,8);
    } else if(kind==Icon::Refresh) {circle(0,0,10);line(9,-9,9,-2);line(9,-2,2,-2);
    } else {line(-8,-12,4,-12);line(4,-12,10,-6);line(10,-6,10,12);line(10,12,-8,12);line(-8,12,-8,-12);for(int i=0;i<3;++i)line(-4,-3+i*5,6,-3+i*5);}
}
} }
