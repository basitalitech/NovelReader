#include "controls.hpp"
#include "ui.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool near(float a, float b) { return std::abs(a - b) < 0.001f; }
}

int main() {
    using namespace reader::controls;
    try {
        Stick stick;
        check(stick.direction(128, 128) == None && stick.direction(160, 128) == None, "Stick drift triggered movement");
        check(stick.direction(200, 128) == Right, "Horizontal stick direction");
        check(stick.direction(163, 128) == Right, "Stick hysteresis lost hold near threshold");
        check(stick.direction(155, 128) == None, "Stick did not release inside dead zone");
        check(stick.direction(128, 0) == Up, "Vertical stick direction");
        check(stick.direction(230, 25) == Up, "Diagonal stick changed axis from small jitter");
        check(stick.direction(255, 100) == Right, "Dominant stick axis did not change");
        stick.cancel_until_neutral();
        check(stick.direction(255, 128) == None, "Held stick carried into another screen");
        check(stick.direction(128, 128) == None && stick.direction(0, 128) == Left, "Stick did not rearm after neutral");
        check(near(axis(128), 0) && near(axis(168), 0), "Continuous stick dead zone");
        check(near(axis(0), -1) && near(axis(255), 1) && axis(200) > 0 && axis(200) < 1, "Continuous stick range");

        Repeat repeat;
        constexpr unsigned confirm = 16;
        check(repeat.sample(Right, Right, 1000) == Right, "Initial direction not immediate");
        check(repeat.sample(Right, Right, 350999) == 0, "Repeat started before delay");
        check(repeat.sample(Right, Right, 351000) == Right, "First repeat missing");
        check(repeat.sample(Right | confirm, Right, 360000) == confirm, "Confirm edge lost or direction repeated early");
        check(repeat.sample(Right | confirm, Right, 491000) == Right, "Held confirm repeated or direction stalled");
        check(repeat.sample(0, Right, 500000) == 0 && repeat.sample(Right, Right, 510000) == Right, "Repress not immediate");
        check(repeat.held()==Right && repeat.released()==0,"Held state does not match the sample");
        repeat.sample(0,Right,520000);
        check(repeat.held()==0 && repeat.released()==Right,"Direction release was lost");
        repeat.sample(Right|confirm,Right,530000);
        repeat.cancel_until_release(Right|confirm);
        check(repeat.sample(Right|confirm,Right,1000000)==0,"Held inputs leaked across a transition");
        check(repeat.sample(Right,Right,1100000)==0,"Releasing confirm rearmed a held direction");
        check(repeat.sample(Right|confirm,Right,1200000)==confirm,"A fresh confirm was blocked by another held key");
        repeat.sample(0,Right,1210000);
        check(repeat.sample(Right,Right,1220000)==Right,"Transition suppression imposed a cooldown after release");
        Repeat shoulders;
        check(shoulders.sample(confirm,0,1000)==confirm && shoulders.sample(confirm,0,900000)==0,
              "A nonrepeatable shortcut repeats when held");
        shoulders.cancel_until_release(Left|Right);
        check(shoulders.sample(confirm|Left|Right,Left|Right,1000000)==0 &&
              shoulders.sample(confirm|Right,Left|Right,1100000)==0,
              "Opposing-shoulder cancellation rearmed a held confirm or shoulder");

        check(display_coordinate(0, 0, 1919, 960) == 0 && display_coordinate(1919, 0, 1919, 960) == 959 &&
              display_coordinate(960, 0, 1919, 960) == 480, "Touch display scaling");
        check(display_coordinate(50, 100, 2019, 960) == 0 && display_coordinate(3000, 100, 2019, 960) == 959,
              "Touch coordinate clamping");
        check(toolbar_hit(28,500,4)==0 && toolbar_hit(931,539,4)==3,"Native toolbar boundary hit");
        check(toolbar_hit(180,500,6)==1 && toolbar_hit(176,500,6)==-1,"Toolbar gap activated an action");
        check(toolbar_hit(936,500,4)==-1 && toolbar_hit(100,483,4)==-1 && toolbar_hit(100,544,4)==-1,
              "Toolbar hit outside bounds");
        check(list_row_hit(100, 96, 40, 610, 96, 57, 53) == 0 &&
              list_row_hit(100, 153, 40, 610, 96, 57, 53) == 1, "Library row selection");
        check(list_row_hit(100, 150, 40, 610, 96, 57, 53) == -1 &&
              list_row_hit(700, 100, 40, 610, 96, 57, 53) == -1 &&
              list_row_hit(100, 440, 40, 610, 96, 57, 53) == -1, "Row gap or blank area selected a book");

        for (int row = 0; row < reader::ui::page_size; ++row) {
            const int y = reader::ui::library_top + row * reader::ui::library_pitch;
            check(list_row_hit(100,y,40,610,reader::ui::library_top,reader::ui::library_pitch,
                reader::ui::library_row_height) == row, "Modern library row is not touchable");
        }
        for (int item=0;item<6;++item) {
            const auto tile=reader::ui::menu_button(item);
            check(tile.width>=400 && tile.height>=72,"Control panel target is too small");
            check(reader::ui::menu_hit(tile.x+20,tile.y+20,6)==item,"Control panel tile is not touchable");
            check(reader::ui::menu_hit(tile.x+tile.width,tile.y+20,6)==-1,"Control panel gap activates an action");
            check(tile.y+tile.height<reader::ui::toolbar_top,"Control panel overlaps Close");
        }
        check(reader::ui::menu_hit(60,160,7,6)==6 && reader::ui::menu_hit(500,160,7,6)==-1,
              "Last options page exposes an invalid action");
        for (int count=2;count<=6;++count) {
            const auto options=reader::ui::action_toolbar_button(count-1,count);
            check(options.x==776 && options.x+options.width==932,"Options must stay at the right edge");
            for (int item=0;item<count-1;++item) {
                const auto target=reader::ui::action_toolbar_button(item,count);
                const auto next=reader::ui::action_toolbar_button(item+1,count);
                check(target.width>=140 && target.x+target.width<next.x,"Footer actions overlap or are too small");
                check(!target.contains(target.x+target.width,522),"Footer gap activates an action");
            }
        }
        const auto resume=reader::ui::home_continue();
        check(resume.x>=reader::ui::sidebar_width && resume.y+resume.height<reader::ui::toolbar_top,
              "Home action overlaps navigation or footer");
        using reader::ui::FocusTarget;
        using reader::ui::Direction;
        const auto verify_focus=[](const std::vector<FocusTarget>& targets) {
            for(const auto& start:targets) {
                if(!start.enabled)continue;
                std::vector<int> reached{start.id};
                for(std::size_t at=0;at<reached.size();++at) {
                    const auto neighbors=reader::ui::focus_neighbors(targets,reached[at]);
                    for(const auto next:neighbors) {
                        bool seen=false;for(const auto known:reached)if(known==next)seen=true;
                        if(!seen)reached.push_back(next);
                    }
                }
                for(const auto& target:targets)if(target.enabled) {
                    bool seen=false;for(const auto known:reached)if(known==target.id)seen=true;
                    check(seen,"A visible enabled control is unreachable by D-pad");
                }
            }
            for(const auto& target:targets) {
                check(target.box.height>=56,"Interactive control is below minimum height");
                check(target.box.x>=0 && target.box.y>=44 && target.box.x+target.box.width<=960 &&
                    target.box.y+target.box.height<=544,"Control extends outside the screen");
            }
        };
        // The Library registry contains its header and visible cards. Sidebar
        // and footer are hints/navigation shortcuts, not focusable widgets.
        for(int books=0;books<=4;++books) {
            std::vector<FocusTarget> targets;
            for(int i=0;i<2;++i)targets.push_back({200+i,reader::ui::library_action(i),true});
            for(int i=0;i<books;++i)targets.push_back({1000+i,reader::ui::grid_book(i),true});
            verify_focus(targets);
            check(reader::ui::focus_neighbor(targets,200,Direction::Right)==201,"Library header skips Search");
            if(books) {
                check(reader::ui::focus_neighbor(targets,201,Direction::Down)>=1000,
                      "Sparse Library pulls Search focus into the sidebar");
                check(reader::ui::focus_neighbor(targets,1000,Direction::Up)==200,"Cover cannot reach action row");
                const auto down=reader::ui::focus_neighbor(targets,1000,Direction::Down);
                check(down==1000,"Last grid row moved focus into a non-content region");
            }
        }
        for(int rows=1;rows<=5;++rows) {
            std::vector<FocusTarget> targets;
            for(int i=0;i<rows;++i)targets.push_back({1000+i,reader::ui::contents_row(i),true});
            for(int i=0;i<5;++i)targets.push_back({300+i,reader::ui::action_toolbar_button(i,5),i!=2});
            verify_focus(targets);
            check(reader::ui::focus_neighbor(targets,1000,Direction::Left)==1000,
                  "Left from a chapter row jumps diagonally to the footer");
        }
        // A modal graph contains only its own controls. Disabled menu entries
        // and partial pages are skipped; Close and paging remain reachable.
        for(int entries=1;entries<=6;++entries) {
            std::vector<FocusTarget> targets;
            for(int i=0;i<entries;++i)targets.push_back({2000+i,reader::ui::menu_button(i),i!=1});
            for(int i=0;i<3;++i)targets.push_back({300+i,reader::ui::toolbar_button(i,3),i!=0});
            verify_focus(targets);
        }
        std::vector<FocusTarget> single{{7,{24,124,200,56},true},{8,{240,124,200,56},false}};
        for(const auto neighbor:reader::ui::focus_neighbors(single,7))check(neighbor==7,"Focus wraps or selects a disabled control");
        single[0].enabled=false;
        check(reader::ui::focus_neighbor(single,7,Direction::Right)==-1,"All-disabled graph exposes a target");
        std::vector<FocusTarget> settings;
        for(int i=0;i<5;++i)settings.push_back({1000+i,reader::ui::settings_row(i),true});
        for(int i=0;i<4;++i)settings.push_back({300+i,reader::ui::action_toolbar_button(i,4),true});
        verify_focus(settings);
        for (int count=1;count<=6;++count) for (int item=0;item<count;++item) {
            const auto target=reader::ui::toolbar_button(item,count);
            check(target.height>=44 && target.width>=140,"Toolbar target is too small");
            check(toolbar_hit(target.x,target.y,count)==item &&
                toolbar_hit(target.x+target.width-1,target.y+target.height-1,count)==item,
                "Visible toolbar and hit regions disagree");
        }
        check(reader::ui::page_first(0)==0 && reader::ui::page_first(3)==0 && reader::ui::page_first(4)==4,
              "Four-row list boundaries are wrong");
        check(reader::ui::library_top+reader::ui::page_size*reader::ui::library_pitch<reader::ui::toolbar_top,
              "Book rows overlap toolbar");
        for(int i=0;i<reader::ui::grid_page_size;++i) {
            const auto cover=reader::ui::grid_book(i);
            check(reader::ui::grid_hit(cover.x+cover.width/2,cover.y+cover.height/2,8)==i,
                  "Cover grid selects the wrong book");
            check(cover.x>reader::ui::sidebar_width && cover.y+cover.height<reader::ui::toolbar_top,
                  "Cover overlaps navigation or footer");
            check(reader::ui::grid_hit(cover.x+cover.width,cover.y,8)==-1,"Cover gutter opens a book");
        }
        check(reader::ui::grid_hit(412,142,1)==-1 && reader::ui::grid_hit(100,200,8)==-1,
              "Empty grid cells or sidebar open a book");
        for(int i=0;i<6;++i) {
            const auto nav=reader::ui::sidebar_item(i);
            check(nav.x>=0 && nav.x+nav.width<reader::ui::sidebar_width && nav.y+nav.height<reader::ui::toolbar_top,
                  "Sidebar navigation overlaps content");
        }
        for (std::size_t lines=1;lines<=5;++lines)
            check(reader::ui::feedback_height(lines,false,true)-16>=64,"A short error makes Retry too small to tap");
        std::size_t chapter = 99;
        check(reader::ui::chapter_number("1",20000,chapter) && chapter == 0, "Jump to first chapter");
        check(reader::ui::chapter_number("20000",20000,chapter) && chapter == 19999, "Jump to last online chapter");
        for (const auto& invalid : {"", "0", "20001", "-1", "1.5", "1x", "999999999999999999999"})
            check(!reader::ui::chapter_number(invalid,20000,chapter), "Invalid chapter number accepted");
        check(!reader::ui::chapter_number("1",0,chapter), "Jump accepted an empty contents list");
        std::vector<reader::LibraryItem> collection(4);
        check(reader::ui::recent_book(collection) == collection.size(), "New library has a resume target");
        collection[0].progress.last_read = 10;
        collection[1].guide = true; collection[1].progress.last_read = 40;
        collection[2].error = "Missing file"; collection[2].progress.last_read = 50;
        collection[3].progress.last_read = 30;
        check(reader::ui::recent_book(collection) == 1, "Resume excluded the most recently read welcome TXT");
        collection[1].progress = {};
        check(reader::ui::recent_book(collection) == 3, "Resume did not select the latest readable book");
        collection[3].directory = true;
        check(reader::ui::recent_book(collection) == 0, "Resume selected a directory");
        collection[0].progress.last_read = 0; collection[0].progress.offset = 120;
        check(reader::ui::recent_book(collection) == 0, "Resume ignored legacy TXT progress without a timestamp");

        Touch touch;
        check(touch.sample({{1, 100, 100}}, 1000, false).kind == GestureKind::None, "Touch activated before release");
        auto gesture = touch.sample({}, 101000, false);
        check(gesture.kind == GestureKind::Tap && near(gesture.x, 100), "Tap recognition");
        check(touch.sample({}, 102000, false).kind == GestureKind::None, "Tap repeated while idle");
        touch.sample({{1, 100, 100}}, 200000, false);
        check(touch.sample({}, 850000, false).kind == GestureKind::Hold, "Long press recognition");
        touch.sample({{1, 700, 220}}, 1000000, false);
        touch.sample({{1, 400, 225}}, 1100000, false);
        gesture = touch.sample({}, 1200000, false);
        check(gesture.kind == GestureKind::Swipe && near(gesture.dx, -300) && near(gesture.dy, 5), "Page swipe recognition");
        check(near(gesture.start_x,700) && near(gesture.start_y,220),"Swipe lost its touch-down position");
        touch.sample({{1, 100, 100}}, 1300000, false);
        touch.sample({{1, 180, 100}}, 1400000, false);
        touch.sample({{1, 100, 100}}, 1500000, false);
        check(touch.sample({}, 1600000, false).kind == GestureKind::None, "Drag returning to start became an accidental tap");
        touch.sample({{1, 100, 100}}, 1700000, false);
        touch.cancel_until_release();
        check(touch.sample({{1, 100, 100}}, 1800000, false).kind == GestureKind::None &&
              touch.sample({}, 1900000, false).kind == GestureKind::None, "Screen transition generated a tap");
        touch.sample({{1, 100, 100}}, 2000000, false);
        check(touch.sample({{2, 100, 100}}, 2100000, false).kind == GestureKind::None &&
              touch.sample({}, 2200000, false).kind == GestureKind::None, "Replaced finger generated a tap");

        touch.sample({{1, 200, 200}, {2, 400, 200}}, 2300000, true);
        gesture = touch.sample({{2, 500, 200}, {1, 100, 200}}, 2400000, true);
        check(gesture.kind == GestureKind::Transform && near(gesture.scale, 2) && near(gesture.x, 300) && near(gesture.dx, 0),
              "Pinch scale changed with report order");
        check(touch.sample({{1, 100, 200}}, 2500000, true).kind == GestureKind::None, "Pinch-to-drag jumped");
        gesture = touch.sample({{1, 120, 210}}, 2600000, true);
        check(gesture.kind == GestureKind::Transform && near(gesture.dx, 20) && near(gesture.dy, 10), "Live image drag");
        check(touch.sample({}, 2700000, true).kind == GestureKind::None, "Pinch release generated a tap");
        touch.sample({{1, 100, 100}, {2, 200, 100}}, 2800000, false);
        check(touch.sample({}, 2900000, false).kind == GestureKind::None, "Two fingers activated a menu row");
        touch.sample({{1, 100, 100}}, 3000000, true);
        touch.sample({{1, 140, 100}}, 3100000, true);
        check(touch.sample({}, 3200000, true).kind == GestureKind::None, "Image drag generated a page swipe");
        touch.sample({{1, 319, 200}},3300000,false);
        touch.sample({{1, 322, 200}},3350000,false);
        gesture=touch.sample({},3400000,false);
        check(gesture.kind==GestureKind::Tap && near(gesture.start_x,319) && near(gesture.x,322),
              "Tap cannot validate its original hit target");
        std::cout << "Analog and touch control tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
