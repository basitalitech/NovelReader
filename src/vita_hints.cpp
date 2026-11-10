#include "vita_hints.hpp"

namespace reader { namespace visual {
void controller_hint(const VitaFonts& fonts,ControllerButton key,const std::string& label,
                     int x,int center_y,int max_width,unsigned tint) {
    const int icon_width=controller_icon_width(key),gap=icon_width?10:0;
    if(max_width<=0)return;
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x,hint_bar_top+2,x+max_width,544);
    controller_icon(key,x,center_y,tint);
    if(key==ControllerButton::Start || key==ControllerButton::Select || key==ControllerButton::L || key==ControllerButton::R) {
        const std::string word=key==ControllerButton::Start?"START":key==ControllerButton::Select?"SELECT":key==ControllerButton::L?"L":"R";
        const auto width=fonts.width(FontRole::Heading,16,word);
        const auto foreground=tint==text?color(255,255,255):tint;
        fonts.draw(FontRole::Heading,16,x+(icon_width-width)/2,center_y+5,foreground,word);
    }
    if(max_width>icon_width+gap)
        fonts.draw(FontRole::Interface,22,x+icon_width+gap,center_y+7,tint,label);
    vita2d_disable_clipping();
}
} }
