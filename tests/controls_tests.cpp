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

        check(display_coordinate(0, 0, 1919, 960) == 0 && display_coordinate(1919, 0, 1919, 960) == 959 &&
              display_coordinate(960, 0, 1919, 960) == 480, "Touch display scaling");
        check(display_coordinate(50, 100, 2019, 960) == 0 && display_coordinate(3000, 100, 2019, 960) == 959,
              "Touch coordinate clamping");
        check(toolbar_hit(40, 488, 4) == 0 && toolbar_hit(919, 535, 4) == 3, "Toolbar boundary hit");
        check(toolbar_hit(186, 500, 6) == 1 && toolbar_hit(185, 500, 6) == 0, "Unequal-width toolbar boundary hit");
        check(toolbar_hit(920, 500, 4) == -1 && toolbar_hit(100, 487, 4) == -1 && toolbar_hit(100, 536, 4) == -1,
              "Toolbar hit outside bounds");
        check(list_row_hit(100, 96, 40, 610, 96, 57, 53) == 0 &&
              list_row_hit(100, 153, 40, 610, 96, 57, 53) == 1, "Library row selection");
        check(list_row_hit(100, 150, 40, 610, 96, 57, 53) == -1 &&
              list_row_hit(700, 100, 40, 610, 96, 57, 53) == -1 &&
              list_row_hit(100, 440, 40, 610, 96, 57, 53) == -1, "Row gap or blank area selected a book");

        for (int row = 0; row < 6; ++row) {
            const int y = reader::ui::library_top + row * reader::ui::library_pitch;
            check(list_row_hit(100,y,40,610,reader::ui::library_top,reader::ui::library_pitch,
                reader::ui::library_row_height) == row, "Modern library row is not touchable");
            check(list_row_hit(300,reader::ui::menu_top + row * reader::ui::menu_pitch,
                reader::ui::menu_left,reader::ui::menu_right,reader::ui::menu_top,reader::ui::menu_pitch,44) == row,
                "Overflow menu row is not touchable");
        }
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
        check(reader::ui::recent_book(collection) == 3, "Resume did not select the latest readable book");
        collection[3].directory = true;
        check(reader::ui::recent_book(collection) == 0, "Resume selected a directory");

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
        std::cout << "Analog and touch control tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
