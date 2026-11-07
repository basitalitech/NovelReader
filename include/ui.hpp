#pragma once
#include "reader.hpp"

// Shared display geometry and navigation rules, independent of Vita APIs.
namespace reader { namespace ui {
struct ReadingArea { int top, height; };
inline ReadingArea reading_area(bool immersive) { return immersive ? ReadingArea{24, 476} : ReadingArea{104, 336}; }
constexpr int page_size = 4;
constexpr int library_top = 124, library_pitch = 80, library_row_height = 76;
constexpr int list_left = 24, list_right = 856;
constexpr int toolbar_top = 464, toolbar_height = 72;
struct Rect {
    int x, y, width, height;
    bool contains(int px, int py) const { return px >= x && px < x + width && py >= y && py < y + height; }
};
inline Rect toolbar_button(int index, int count) {
    if (count <= 0 || index < 0 || index >= count) return {0,0,0,0};
    const int left = 24 + 912 * index / count, right = 24 + 912 * (index+1) / count;
    return {left + 4,toolbar_top,right-left-8,toolbar_height};
}
inline Rect menu_button(int index) { return {40 + (index % 2) * 448,140 + (index / 2) * 96,432,84}; }
inline int menu_hit(int x, int y, int count, int first = 0) {
    for (int i=0;i<6 && first+i<count;++i) if (menu_button(i).contains(x,y)) return first+i;
    return -1;
}
inline std::size_t page_first(std::size_t selected) { return selected / page_size * page_size; }
inline int feedback_height(std::size_t lines, bool busy, bool retry) {
    const int height=static_cast<int>(lines>5?5:lines)*28+(busy?70:24);
    return retry && height<88?88:height;
}
inline std::size_t recent_book(const std::vector<LibraryItem>& items) {
    auto found = items.size();
    for (std::size_t i = 0; i < items.size(); ++i)
        if (!items[i].guide && !items[i].directory && items[i].error.empty() && items[i].progress.last_read &&
            (found == items.size() || items[i].progress.last_read > items[found].progress.last_read)) found = i;
    return found;
}
inline bool chapter_number(const std::string& input, std::size_t count, std::size_t& index) {
    std::size_t number = 0;
    if (input.empty() || !count) return false;
    for (const auto c : input) {
        if (c < '0' || c > '9' || number > count / 10) return false;
        number = number * 10 + static_cast<unsigned>(c - '0');
        if (number > count) return false;
    }
    if (!number) return false;
    index = number - 1;
    return true;
}
} }
