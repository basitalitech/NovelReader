#pragma once
#include "reader.hpp"
#include <array>
#include <cstdlib>
#include <limits>

// Shared display geometry and navigation rules, independent of Vita APIs.
namespace reader { namespace ui {
struct ReadingArea { int top, height; };
inline ReadingArea reading_area(bool immersive) { return immersive ? ReadingArea{24, 476} : ReadingArea{76, 340}; }
constexpr int page_size = 4;
constexpr int library_top = 144, library_pitch = 80, library_row_height = 76;
constexpr int list_left = 24, list_right = 856;
constexpr int toolbar_top = 480, toolbar_height = 64;
constexpr int image_top = 132, image_height = 332, image_center_y = image_top + image_height/2;
struct Rect {
    int x, y, width, height;
    bool contains(int px, int py) const { return px >= x && px < x + width && py >= y && py < y + height; }
};
constexpr int sidebar_width = 208;
constexpr int sidebar_count = 5;
inline Rect home_continue() { return {232,124,480,224}; }
constexpr int grid_columns = 4, grid_page_size = 4, contents_page_size = 5;
inline Rect sidebar_item(int index) { return {12, 76 + index * 64, 184, 56}; }
inline Rect library_action(int index) {
    return index==0 ? Rect{764,20,172,56} : Rect{0,0,0,0};
}
// One geometry source for popup drawing, focus and touch. Eleven compact rows
// fit even the Reader's complete action list without covering the whole screen.
struct Popup {
    static constexpr int capacity=11, pitch=36;
    Rect box;
    int first, visible;
    Popup(int count,int selected=0,int width=344) :
        box{960-24-width,484-(44+std::min(count,int(capacity))*pitch),width,44+std::min(count,int(capacity))*pitch},
        first(std::max(0,selected-capacity+1)),visible(std::min(count,int(capacity))) {}
    Rect row(int index) const { return {box.x+10,box.y+36+(index-first)*pitch,box.width-20,pitch-2}; }
    Rect back() const { return {box.x+8,box.y+2,32,32}; }
};
constexpr int grid_top=100, grid_bottom=480, grid_height=300, grid_cover_height=208, grid_pitch=316;
inline Rect grid_book(int index, int scroll=0) {
    return {232 + index%grid_columns*180,grid_top+index/grid_columns*grid_pitch-scroll,164,grid_height};
}
inline Rect settings_row(int index) { return {232,80+index*60,376,56}; }
inline Rect bookmark_row(int index) { return {232,100+index*104,704,96}; }
inline Rect about_action(int index) { return {638,154+index*72,286,56}; }
inline Rect contents_row(int index) { return {288,136+index*64,648,56}; }
inline Rect details_cover() { return {24,72,240,360}; }
inline Rect details_primary(bool online) { return {288,online?352:396,248,56}; }
inline Rect details_secondary(bool online) { return {552,online?352:396,148,56}; }
inline Rect details_utility(int index) { return {288+index*212,416,200,56}; }
inline int grid_hit(int x, int y, int count) {
    for (int i=0; i<count && i<grid_page_size; ++i) if (grid_book(i).contains(x,y)) return i;
    return -1;
}
inline Rect toolbar_button(int index, int count) {
    if (count <= 0 || index < 0 || index >= count) return {0,0,0,0};
    const int left = 24 + 912 * index / count, right = 24 + 912 * (index+1) / count;
    return {left + 4,toolbar_top+4,right-left-8,56};
}
// Cluster primary actions on the left; reserve the far right for Options.
inline Rect action_toolbar_button(int index, int count) {
    if (count < 2 || index < 0 || index >= count) return {0,0,0,0};
    if (index == count-1) return {776,toolbar_top+4,156,56};
    const int pitch = (752 / (count-1) < 160) ? 752 / (count-1) : 160;
    return {28 + index*pitch,toolbar_top+4,pitch-8,56};
}
enum class Direction { Left, Right, Up, Down };
struct FocusTarget { int id; Rect box; bool enabled=true; };
// Build a deterministic directional graph from the same rectangles used for
// drawing and touch. Prefer controls in the same row/column; never wrap.
inline int focus_neighbor(const std::vector<FocusTarget>& targets,int id,Direction direction) {
    const FocusTarget* current=nullptr;
    for(const auto& target:targets) if(target.id==id && target.enabled) current=&target;
    if(!current) { for(const auto& target:targets) if(target.enabled)return target.id; return -1; }
    const bool horizontal=direction==Direction::Left || direction==Direction::Right;
    const int sign=direction==Direction::Left || direction==Direction::Up ? -1:1;
    const auto& a=current->box;
    bool has_content=false;
    for(const auto& target:targets)if(target.enabled && target.box.y<toolbar_top &&
        target.box.x+target.box.width>sidebar_width)has_content=true;
    long best=std::numeric_limits<long>::max(); int result=id;
    for(const auto& target:targets) {
        if(!target.enabled || target.id==id)continue;
        const auto& b=target.box;
        const bool a_sidebar=a.y<toolbar_top && a.x+a.width<=sidebar_width;
        const bool b_sidebar=b.y<toolbar_top && b.x+b.width<=sidebar_width;
        // Up/Down stays in its region. Crossing the sidebar boundary requires
        // Left/Right; otherwise sparse content can pull focus diagonally away.
        if(!horizontal && a_sidebar!=b_sidebar && b.y<toolbar_top && (a_sidebar || has_content))continue;
        const int dx=(2*b.x+b.width)-(2*a.x+a.width),dy=(2*b.y+b.height)-(2*a.y+a.height);
        const int along=sign*(horizontal?dx:dy),across=horizontal?std::abs(dy):std::abs(dx);
        if(along<=0)continue;
        const bool overlap=horizontal ? a.y<b.y+b.height && b.y<a.y+a.height : a.x<b.x+b.width && b.x<a.x+a.width;
        if(horizontal && !overlap)continue;
        const long score=(overlap?0:1000000L)+along*10L+across;
        if(score<best){best=score;result=target.id;}
    }
    return result;
}
inline std::array<int,4> focus_neighbors(const std::vector<FocusTarget>& targets,int id) {
    return {{focus_neighbor(targets,id,Direction::Left),focus_neighbor(targets,id,Direction::Right),
        focus_neighbor(targets,id,Direction::Up),focus_neighbor(targets,id,Direction::Down)}};
}
inline std::size_t page_first(std::size_t selected) { return selected / page_size * page_size; }
inline int feedback_height(std::size_t lines, bool busy, bool retry) {
    const int height=static_cast<int>(lines>5?5:lines)*28+(busy?70:24);
    return retry && height<88?88:height;
}
inline std::size_t recent_book(const std::vector<LibraryItem>& items) {
    auto found = items.size();
    for (std::size_t i = 0; i < items.size(); ++i)
        if (!items[i].directory && items[i].error.empty() && reading_started(items[i].progress) &&
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
