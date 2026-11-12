#include "vita_ui.hpp"
#include <vita2d.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    std::vector<vita2d_color_vertex> captured;
    int draws = 0;
    void capture(const vita2d_color_vertex *vertices, std::size_t count)
    {
        ++draws;
        captured.assign(vertices, vertices + count);
    }
    void check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    unsigned brightness(unsigned c) { return (c & 255) + ((c >> 8) & 255) + ((c >> 16) & 255); }
}
int main()
{
    try
    {
        using namespace reader::visual;
        draw_array_observer() = capture;
        draws = 0;
        gradient(0, 44, 960, 500, color(22, 20, 31), color(32, 27, 46));
        check(draws == 1 && captured.size() == 3000, "Background gradient is not batched");
        for (int row = 0; row < 500; ++row)
        {
            const auto expected = mix(color(22, 20, 31), color(32, 27, 46), row / 499.f);
            for (int col = 0; col < 6; ++col)
                check(captured[row * 6 + col].color == expected, "Gradient scanline colors changed");
            check(captured[row * 6].y == 44 + row && captured[row * 6 + 5].y == 45 + row, "Gradient scanline geometry changed");
        }
        unsigned normal_top = 0, pressed_top = 0;
        for (const auto state : {ButtonState::Normal, ButtonState::Focused, ButtonState::Pressed, ButtonState::Disabled})
        {
            draws = 0;
            button(100, 100, 180, 56, state);
            check(draws == 1, "A button must submit one batched draw");
            check(captured.size() % 3 == 0 && captured.size() <= 768, "Invalid triangle count");
            for (const auto &v : captured)
            {
                check(std::isfinite(v.x) && std::isfinite(v.y), "Invalid geometry");
                check(v.x >= 97 && v.x <= 283 && v.y >= 97 && v.y <= 162, "Effects exceed button bounds");
                // Rounded silhouettes must never fill the square top-left corner.
                check(!(v.x < 101 && v.y < 101), "Square corner artifact");
            }
            // The face is the last layer for pressed/disabled, penultimate for lit states.
            const std::size_t face = captured.size() - (state == ButtonState::Normal || state == ButtonState::Focused ? 204 : 102);
            const auto top = captured[face].color, bottom = captured[face + 101].color;
            check(brightness(top) > brightness(bottom), "Face gradient must darken downward");
            if (state == ButtonState::Normal)
                normal_top = brightness(top);
            if (state == ButtonState::Pressed)
                pressed_top = brightness(top);
            if (state == ButtonState::Disabled)
                check((top >> 24) < 255, "Disabled face must be translucent");
        }
        check(pressed_top < normal_top, "Pressed face must darken");
        const reader::ui::Popup popup_layout(11);
        draws = 0;
        popup(popup_layout.box);
        check(draws == 1 && captured.size() == 306, "Popup surface must use one bounded geometry submission");
        for (const auto &v : captured)
            check(std::isfinite(v.x) && std::isfinite(v.y) && v.x >= popup_layout.box.x && v.x <= 939 &&
                      v.y >= 44 && v.y <= 488,
                  "Popup or shadow escapes display margins");
        for (const int size : {4, 8, 16, 56})
        {
            button(100, 100, size, size, ButtonState::Focused);
            for (const auto &v : captured)
                check(std::isfinite(v.x) && std::isfinite(v.y), "Small button geometry is invalid");
        }
        draws = 0;
        button(100, 100, 0, 56);
        check(draws == 0, "Empty button must not draw");
        button_focus(100, 100, 180, 56);
        check(captured.size() == 216, "Focus ring must use a bounded mesh");
        for (const auto key : {ControllerButton::Cross, ControllerButton::Circle, ControllerButton::Square,
                               ControllerButton::Triangle, ControllerButton::Start, ControllerButton::Select,
                               ControllerButton::L, ControllerButton::R})
        {
            draws = 0;
            controller_icon(key, 44, hint_bar_center);
            check(draws == 1, "Controller icon must use one geometry submission");
            check(!captured.empty() && captured.size() <= 768 && captured.size() % 3 == 0, "Invalid icon mesh");
            for (const auto &v : captured)
            {
                check(std::isfinite(v.x) && std::isfinite(v.y), "Invalid controller icon vertex");
                check(v.x >= 43.99f && v.x <= 44 + controller_icon_width(key) + 0.01f, "Icon escapes its horizontal bounds");
                check(v.y >= hint_bar_top + 1 && v.y <= 543, "Icon escapes the footer safe area");
            }
            const bool lettered = key == ControllerButton::Start || key == ControllerButton::Select ||
                                  key == ControllerButton::L || key == ControllerButton::R;
            if (lettered)
            {
                for (const auto &v : captured)
                    check(v.color == color(64, 70, 96), "Lettered buttons must match the dark face-button backgrounds");
                float top = hint_bar_center, bottom = hint_bar_center;
                for (const auto &v : captured)
                {
                    top = std::min(top, v.y);
                    bottom = std::max(bottom, v.y);
                }
                check(bottom - top == 30, "Lettered buttons must match the face-button height");
            }
            else
            {
                check(controller_icon_width(key) == 30, "Face buttons must use reference dimensions");
                bool white = false, dark = false;
                for (const auto &v : captured)
                {
                    white = white || v.color == color(255, 255, 255);
                    dark = dark || v.color == color(64, 70, 96);
                    check(v.color == color(255, 255, 255) || v.color == color(64, 70, 96),
                          "Face button contains an unexpected color");
                }
                check(white && dark, "Face buttons need white symbols and dark discs");
            }
        }
        draws = 0;
        controller_icon(ControllerButton::None, 44, hint_bar_center);
        check(draws == 0, "Text-only hints must not draw an icon");
        draws = 0;
        hint_bar();
        check(draws == 1, "Bar gradient must use one geometry submission");
        check((captured.front().color >> 24) < 255, "Bar should retain slight translucency");
        check(brightness(captured.front().color) > brightness(captured.back().color), "Bar must darken downward");
        for (const auto &v : captured)
            check(v.y >= 496 && v.y <= 544 && v.x >= 0 && v.x <= 960, "Bar must span only the bottom 48 pixels");
        std::cout << "Button geometry passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}