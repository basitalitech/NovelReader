#pragma once
#include "ui.hpp"

// Small native drawing primitives shared by every screen. No font glyphs are
// used for icons, so controller and navigation symbols never need PGF fallback.
namespace reader { namespace visual {
constexpr unsigned color(unsigned r, unsigned g, unsigned b) { return r | (g<<8) | (b<<16) | 0xff000000u; }
constexpr unsigned background = color(22,20,31), surface = color(31,27,43);
constexpr unsigned border = color(76,62,99), accent = color(180,125,239);
constexpr unsigned text = color(245,242,248), muted = color(192,184,205);
enum class Icon { Book, Home, Library, Clock, Bookmark, Settings, Info, Search, Filter, Grid, Trash, Play, PlayCircle, Chevron, Check, Refresh, Document };
unsigned mix(unsigned a, unsigned b, float amount);
void gradient(int x, int y, int width, int height, unsigned top, unsigned bottom);
void rounded(int x, int y, int width, int height, unsigned fill, int radius=9);
void panel(int x, int y, int width, int height, bool selected=false);
enum class ButtonState { Normal, Focused, Pressed, Disabled };
enum class ControllerButton { None, Cross, Circle, Square, Triangle, Start, Select, L, R };
constexpr int hint_bar_top=496, hint_bar_height=48, hint_bar_center=520;
int controller_icon_width(ControllerButton button);
void controller_icon(ControllerButton button, int x, int center_y, unsigned tint=text);
void hint_bar();
void button(int x, int y, int width, int height, ButtonState state=ButtonState::Normal);
void button_focus(int x, int y, int width, int height);
void reading_card(int x, int y, int width, int height);
void icon(Icon kind, int x, int y, unsigned tint=text, float scale=1);
void progress(int x, int y, int width, float fraction, int height=5);
} }
