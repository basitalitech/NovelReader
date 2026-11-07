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
    if (selected) rounded(x-3,y-3,width+6,height+6,color(60,40,83),11);
    rounded(x,y,width,height,selected?accent:border);
    rounded(x+1,y+1,width-2,height-2,selected?color(83,53,124):surface,8);
    // Subtle upper light and lower shade, kept inside the rounded outline.
    for (int i=0;i<height-18;++i) {
        const auto top=selected?color(103,68,148):color(35,30,47);
        const auto bottom=selected?color(62,41,94):color(25,23,34);
        vita2d_draw_rectangle(x+2,y+9+i,width-4,1,mix(top,bottom,float(i)/std::max(1,height-19)));
    }
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
    const auto line=[&](float a,float b,float c,float d) {vita2d_draw_line(x+a*scale,y+b*scale,x+c*scale,y+d*scale,tint);};
    const auto circle=[&](float a,float b,float radius) {
        for(int i=0;i<32;++i) { const float t=i*6.2831853f/32,u=(i+1)*6.2831853f/32;
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
        circle(0,0,5);
        for(int i=0;i<24;++i){float a=i*6.2831853f/24,b=(i+1)*6.2831853f/24;float r=i%3==1?14:11,s=(i+1)%3==1?14:11;line(r*std::cos(a),r*std::sin(a),s*std::cos(b),s*std::sin(b));}
    } else if(kind==Icon::Info) {circle(0,0,12);line(0,-2,0,7);line(-3,7,3,7);circle(0,-7,1);
    } else if(kind==Icon::Search) {circle(-2,-2,8);line(4,4,12,12);
    } else if(kind==Icon::Filter) {line(-12,-9,12,-9);line(-8,-2,8,-2);line(-4,5,4,5);line(0,9,0,11);
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
