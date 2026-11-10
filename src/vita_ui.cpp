#include "vita_ui.hpp"
#include <vita2d.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <cstring>

namespace reader { namespace visual {
unsigned mix(unsigned a, unsigned b, float amount) {
    unsigned result=0xff000000u;
    for (unsigned shift=0; shift<24; shift+=8)
        result |= static_cast<unsigned>(((a>>shift)&255)*(1-amount)+((b>>shift)&255)*amount)<<shift;
    return result;
}
void gradient(int x,int y,int width,int height,unsigned top,unsigned bottom) {
    if(width<=0 || height<=0)return;
    // Preserve every original scanline color (including mix's rounding), but
    // submit all rows together instead of up to 500 individual GPU commands.
    auto* vertices=static_cast<vita2d_color_vertex*>(vita2d_pool_memalign(height*6*sizeof(vita2d_color_vertex),8));
    if(!vertices) {
        for(int i=0;i<height;++i)vita2d_draw_rectangle(x,y+i,width,1,mix(top,bottom,float(i)/std::max(1,height-1)));
        return;
    }
    for(int i=0;i<height;++i) {
        const auto color=mix(top,bottom,float(i)/std::max(1,height-1));
        const vita2d_color_vertex a{float(x),float(y+i),0.5f,color},b{float(x+width),float(y+i),0.5f,color};
        const vita2d_color_vertex c{float(x),float(y+i+1),0.5f,color},d{float(x+width),float(y+i+1),0.5f,color};
        auto* row=vertices+i*6;
        row[0]=a;row[1]=b;row[2]=c;row[3]=b;row[4]=d;row[5]=c;
    }
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES,vertices,height*6);
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
}
namespace {
unsigned alpha(unsigned tint,unsigned opacity) { return (tint&0xffffffu)|(opacity<<24); }
unsigned blend(unsigned a,unsigned b,float t) {
    unsigned result=0;
    for(unsigned shift=0;shift<32;shift+=8)
        result|=static_cast<unsigned>(((a>>shift)&255)*(1-t)+((b>>shift)&255)*t)<<shift;
    return result;
}
// Non-overlapping horizontal triangles give exact vertical color interpolation,
// including alpha. Eight segments per quarter keep nine-pixel corners smooth.
struct ButtonMesh {
    std::array<vita2d_color_vertex,768> vertices;
    std::size_t count=0;
    void shape(float x,float y,float w,float h,float radius,unsigned top,unsigned bottom) {
        if(w<=0 || h<=0)return;
        const float r=std::min(radius,std::min(w,h)/2);
        static const auto inset=[] {
            std::array<float,9> values{};
            for(int i=0;i<=8;++i) {const float t=i/8.f;values[i]=1-std::sqrt(1-(1-t)*(1-t));}
            return values;
        }();
        const auto edge=[&](int i) {
            if(i<=8)return std::array<float,2>{{r*i/8.f,r*inset[i]}};
            const int j=17-i;
            return std::array<float,2>{{h-r*j/8.f,r*inset[j]}};
        };
        for(int i=0;i<17;++i) {
            const auto a=edge(i),b=edge(i+1);
            const unsigned ca=top==bottom?top:blend(top,bottom,a[0]/h);
            const unsigned cb=top==bottom?bottom:blend(top,bottom,b[0]/h);
            const vita2d_color_vertex tl{x+a[1],y+a[0],0.5f,ca},tr{x+w-a[1],y+a[0],0.5f,ca};
            const vita2d_color_vertex bl{x+b[1],y+b[0],0.5f,cb},br{x+w-b[1],y+b[0],0.5f,cb};
            for(const auto& v:{tl,tr,bl,tr,br,bl})vertices[count++]=v;
        }
    }
    void draw() const {
        const auto bytes=count*sizeof(vita2d_color_vertex);
        auto* data=static_cast<vita2d_color_vertex*>(vita2d_pool_memalign(bytes,8));
        if(!data)return;
        std::memcpy(data,vertices.data(),bytes);
        vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES,data,count);
    }
};
}
void button(int x,int y,int width,int height,ButtonState state) {
    if(width<4 || height<4)return;
    const bool focused=state==ButtonState::Focused,pressed=state==ButtonState::Pressed;
    const bool disabled=state==ButtonState::Disabled;
    ButtonMesh mesh;
    if(!disabled) {
        // Layered translucent purple silhouettes form a small, rounded shadow.
        mesh.shape(x-2,y+(pressed?0:2),width+4,height+3,11,alpha(background,0),alpha(background,36));
        mesh.shape(x-1,y+(pressed?1:3),width+2,height,10,alpha(background,18),alpha(background,66));
    }
    if(focused)mesh.shape(x-3,y-3,width+6,height+6,12,alpha(accent,38),alpha(accent,12));
    const auto base=mix(surface,accent,disabled?0.08f:pressed?0.16f:focused?0.36f:0.20f);
    const auto rim=focused?accent:mix(border,accent,0.32f);
    mesh.shape(x,y,width,height,9,alpha(rim,disabled?80:pressed?130:210),alpha(rim,disabled?48:110));
    const unsigned opacity=disabled?145:255;
    // The lighter top is the soft reflection; the face stays in the same palette.
    mesh.shape(x+1,y+1,width-2,height-2,8,
        alpha(mix(base,accent,pressed?0.02f:disabled?0.05f:0.16f),opacity),
        alpha(mix(base,background,pressed?0.32f:0.20f),opacity));
    // A fine upper bevel fades out into the face, following the same rounded edge.
    if(!disabled && !pressed)
        mesh.shape(x+2,y+2,width-4,std::min(14,height-4),7,alpha(accent,focused?36:24),alpha(accent,0));
    mesh.draw();
}
void popup(const ui::Rect& box) {
    ButtonMesh mesh;
    mesh.shape(box.x+3,box.y+4,box.width,box.height,10,alpha(background,100),alpha(background,160));
    mesh.shape(box.x,box.y,box.width,box.height,9,border,mix(border,accent,0.15f));
    mesh.shape(box.x+1,box.y+1,box.width-2,box.height-2,8,
        mix(surface,accent,0.14f),mix(surface,background,0.3f));
    mesh.draw();
}
void reading_card(int x,int y,int width,int height) {
    if(width<4 || height<4)return;
    ButtonMesh mesh;
    // Low-opacity rim and face retain the artwork beneath this Home-only card.
    // Keep the face close to the footer's purple-black palette, without its gloss.
    mesh.shape(x,y,width,height,9,alpha(border,55),alpha(border,45));
    mesh.shape(x+1,y+1,width-2,height-2,8,
        alpha(mix(surface,border,0.08f),190),alpha(background,210));
    mesh.draw();
}
void button_focus(int x,int y,int width,int height) {
    if(width<4 || height<4)return;
    ButtonMesh mesh;
    static const auto directions=[] {
        std::array<std::array<float,2>,36> points{};
        for(int corner=0;corner<4;++corner)for(int i=0;i<=8;++i) {
            const float angle=(corner*8+i)*3.14159265f/16;
            points[corner*9+i]={{std::cos(angle),std::sin(angle)}};
        }
        return points;
    }();
    const float r=std::min(9,std::min(width,height)/2);
    const auto vertex=[&](int index,float radius) {
        const int corner=index/9;
        const float cx=x+(corner==0 || corner==3?width-r:r);
        const float cy=y+(corner<2?height-r:r);
        return vita2d_color_vertex{cx+directions[index][0]*radius,cy+directions[index][1]*radius,0.5f,
            alpha(accent,radius>r?170:230)};
    };
    for(int i=0;i<36;++i) {
        const int j=(i+1)%36;
        const auto a=vertex(i,r+2),b=vertex(j,r+2),c=vertex(i,r),d=vertex(j,r);
        for(const auto& v:{a,b,c,b,d,c})mesh.vertices[mesh.count++]=v;
    }
    mesh.draw();
}
int controller_icon_width(ControllerButton key) {
    return key==ControllerButton::None?0:key==ControllerButton::Start?58:key==ControllerButton::Select?64:30;
}
void hint_bar() {
    const unsigned top=alpha(mix(surface,border,0.12f),235),bottom=alpha(background,248);
    const vita2d_color_vertex vertices[]={
        {0,496,0.5f,top},{960,496,0.5f,top},{0,544,0.5f,bottom},
        {960,496,0.5f,top},{960,544,0.5f,bottom},{0,544,0.5f,bottom}};
    auto* data=static_cast<vita2d_color_vertex*>(vita2d_pool_memalign(sizeof(vertices),8));
    if(!data)return;
    std::memcpy(data,vertices,sizeof(vertices));
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES,data,6);
    vita2d_draw_rectangle(0,hint_bar_top,960,1,alpha(mix(border,muted,0.12f),150));
}
void controller_icon(ControllerButton key,int x,int center_y,unsigned tint) {
    if(key==ControllerButton::None)return;
    ButtonMesh mesh;
    const auto triangle=[&](vita2d_color_vertex a,vita2d_color_vertex b,vita2d_color_vertex c) {
        mesh.vertices[mesh.count++]=a;mesh.vertices[mesh.count++]=b;mesh.vertices[mesh.count++]=c;
    };
    // Vita-style buttons: 30 px discs, rounded 2.9 px strokes,
    // and optically balanced symbols. White replaces the colored foregrounds.
    const auto foreground=tint==text?color(255,255,255):tint;
    const auto disc=[&](float cx,float cy,float radius,unsigned fill) {
        mesh.shape(cx-radius,cy-radius,2*radius,2*radius,radius,fill,fill);
    };
    const auto stroke=[&](float ax,float ay,float bx,float by,float width) {
        const float dx=bx-ax,dy=by-ay,length=std::sqrt(dx*dx+dy*dy);
        if(length==0)return;
        const float ox=-dy*width/(2*length),oy=dx*width/(2*length);
        const vita2d_color_vertex a{ax+ox,ay+oy,0.5f,foreground},b{bx+ox,by+oy,0.5f,foreground};
        const vita2d_color_vertex c{ax-ox,ay-oy,0.5f,foreground},d{bx-ox,by-oy,0.5f,foreground};
        triangle(a,b,c);triangle(b,d,c);
        disc(ax,ay,width/2,foreground);disc(bx,by,width/2,foreground);
    };
    // Cached unit circle: no per-frame trigonometry, images, or heap storage.
    static const auto circle=[] {
        std::array<std::array<float,2>,49> points{};
        for(int i=0;i<=48;++i)points[i]={{std::cos(i*6.2831853f/48),std::sin(i*6.2831853f/48)}};
        return points;
    }();
    const float cx=x+controller_icon_width(key)/2.f,cy=center_y;
    const auto ring=[&](float radius,float width) {
        for(int i=0;i<48;++i) {
            const auto point=[&](int j,float r,unsigned color) {
                return vita2d_color_vertex{cx+circle[j][0]*r,cy+circle[j][1]*r,0.5f,color};
            };
            {
                const auto a=point(i,radius,foreground),b=point(i+1,radius,foreground);
                const auto c=point(i,radius-width,foreground),d=point(i+1,radius-width,foreground);
                triangle(a,b,c);triangle(b,d,c);
            }
        }
    };
    if(key==ControllerButton::Start || key==ControllerButton::Select || key==ControllerButton::L || key==ControllerButton::R) {
        const float w=controller_icon_width(key);
        const float radius=key==ControllerButton::L || key==ControllerButton::R?6.f:15.f;
        mesh.shape(x,cy-15,w,30,radius,color(64,70,96),color(64,70,96));
    } else {
        disc(cx,cy,15,color(64,70,96));
        if(key==ControllerButton::Cross) {
            stroke(cx-5.7f,cy-5.7f,cx+5.7f,cy+5.7f,2.9f);
            stroke(cx-5.7f,cy+5.7f,cx+5.7f,cy-5.7f,2.9f);
        } else if(key==ControllerButton::Circle)ring(7.6f,2.9f);
        else if(key==ControllerButton::Square) {
            mesh.shape(cx-7,cy-7,14,14,1.6f,foreground,foreground);
            mesh.shape(cx-4.1f,cy-4.1f,8.2f,8.2f,0,color(64,70,96),color(64,70,96));
        } else if(key==ControllerButton::Triangle) {
            const float top=cy+0.5f-13.2f*0.58f,bottom=cy+0.5f+13.2f*0.42f;
            stroke(cx,top,cx+7.5f,bottom,2.9f);
            stroke(cx+7.5f,bottom,cx-7.5f,bottom,2.9f);
            stroke(cx-7.5f,bottom,cx,top,2.9f);
        }
    }
    mesh.draw();
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
    } else if(kind==Icon::Play || kind==Icon::PlayCircle) {
        if(kind==Icon::PlayCircle) {
            circle(0,0,13);line(-3,-6,6,0);line(6,0,-3,6);line(-3,6,-3,-6);
        } else {line(-5,-9,8,0);line(8,0,-5,9);line(-5,9,-5,-9);}
    } else if(kind==Icon::Chevron) {line(-3,-6,3,0);line(3,0,-3,6);
    } else if(kind==Icon::Check) {line(-8,0,-2,6);line(-2,6,9,-7);
    } else if(kind==Icon::Trash) {line(-10,-8,10,-8);line(-4,-12,4,-12);line(-7,-8,-6,12);line(-6,12,6,12);line(6,12,7,-8);line(-2,-4,-2,8);line(2,-4,2,8);
    } else if(kind==Icon::Refresh) {circle(0,0,10);line(9,-9,9,-2);line(9,-2,2,-2);
    } else if(kind==Icon::Download) {
        line(0,-11,0,4);line(-6,-2,0,4);line(0,4,6,-2);
        line(-10,5,-10,11);line(-10,11,10,11);line(10,11,10,5);
    } else if(kind==Icon::Downloaded) {
        line(-8,-12,4,-12);line(4,-12,10,-6);line(10,-6,10,12);line(10,12,-8,12);line(-8,12,-8,-12);
        line(-4,2,0,6);line(0,6,7,-2);
    } else if(kind==Icon::Lock) {
        line(-6,-2,-6,-9);line(-6,-9,6,-9);line(6,-9,6,-2);
        line(-10,-2,10,-2);line(10,-2,10,12);line(10,12,-10,12);line(-10,12,-10,-2);line(0,3,0,7);
    } else {line(-8,-12,4,-12);line(4,-12,10,-6);line(10,-6,10,12);line(10,12,-8,12);line(-8,12,-8,-12);for(int i=0;i<3;++i)line(-4,-3+i*5,6,-3+i*5);}
}
} }
