#pragma once
#include "reader.hpp"

// Shared display geometry and navigation rules, independent of Vita APIs.
namespace reader { namespace ui {
struct ReadingArea { int top, height; };
inline ReadingArea reading_area(bool immersive) { return immersive ? ReadingArea{24, 476} : ReadingArea{96, 350}; }
constexpr int library_top = 104, library_pitch = 57, library_row_height = 53;
constexpr int menu_left = 210, menu_right = 750, menu_top = 132, menu_pitch = 48;
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
