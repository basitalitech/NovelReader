#include "vita_ui.hpp"
#include <vita2d.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
std::vector<vita2d_color_vertex> captured;
int draws=0;
void capture(const vita2d_color_vertex* vertices,std::size_t count) {
    ++draws;captured.assign(vertices,vertices+count);
}
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
unsigned brightness(unsigned c) {return (c&255)+((c>>8)&255)+((c>>16)&255);}
}
int main() {
    try {
        using namespace reader::visual;
        draw_array_observer()=capture;
        unsigned normal_top=0,pressed_top=0;
        for(const auto state:{ButtonState::Normal,ButtonState::Focused,ButtonState::Pressed,ButtonState::Disabled}) {
            draws=0;button(100,100,180,56,state);
            check(draws==1,"A button must submit one batched draw");
            check(captured.size()%3==0 && captured.size()<=768,"Invalid triangle count");
            for(const auto& v:captured) {
                check(std::isfinite(v.x) && std::isfinite(v.y),"Invalid geometry");
                check(v.x>=97 && v.x<=283 && v.y>=97 && v.y<=162,"Effects exceed button bounds");
                // Rounded silhouettes must never fill the square top-left corner.
                check(!(v.x<101 && v.y<101),"Square corner artifact");
            }
            // The face is the last layer for pressed/disabled, penultimate for lit states.
            const std::size_t face=captured.size()-(state==ButtonState::Normal || state==ButtonState::Focused?204:102);
            const auto top=captured[face].color,bottom=captured[face+101].color;
            check(brightness(top)>brightness(bottom),"Face gradient must darken downward");
            if(state==ButtonState::Normal)normal_top=brightness(top);
            if(state==ButtonState::Pressed)pressed_top=brightness(top);
            if(state==ButtonState::Disabled)check((top>>24)<255,"Disabled face must be translucent");
        }
        check(pressed_top<normal_top,"Pressed face must darken");
        for(const int size:{4,8,16,56}) {
            button(100,100,size,size,ButtonState::Focused);
            for(const auto& v:captured)check(std::isfinite(v.x) && std::isfinite(v.y),"Small button geometry is invalid");
        }
        draws=0;button(100,100,0,56);check(draws==0,"Empty button must not draw");
        button_focus(100,100,180,56);
        check(captured.size()==216,"Focus ring must use a bounded mesh");
        std::cout<<"Button geometry passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
