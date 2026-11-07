#pragma once
#include <cstdint>
#include <vector>

namespace reader { namespace controls {
enum Direction : unsigned { None = 0, Left = 1, Right = 2, Up = 4, Down = 8 };
class Stick {
public:
    unsigned direction(int x, int y);
    void cancel_until_neutral() { blocked_ = true; direction_ = None; }
private:
    unsigned direction_ = None;
    bool blocked_ = false;
};
float axis(int value); // Rescaled dead zone, for continuous image panning.
class Repeat {
public:
    unsigned sample(unsigned held, unsigned repeatable, std::uint64_t now);
private:
    unsigned previous_ = 0;
    std::uint64_t repeat_at_ = 0;
};

struct Point { int id, x, y; }; // Display coordinates, 960 x 544.
enum class GestureKind { None, Tap, Hold, Swipe, Transform };
struct Gesture {
    GestureKind kind = GestureKind::None;
    float x = 0, y = 0, dx = 0, dy = 0, scale = 1;
};
class Touch {
public:
    Gesture sample(const std::vector<Point>& points, std::uint64_t now, bool live_transform);
    void cancel_until_release();
private:
    bool active_ = false, blocked_ = false, multi_ = false, moved_ = false;
    Point start_{}, last_{};
    int second_id_ = -1;
    float distance_ = 0, middle_x_ = 0, middle_y_ = 0;
    std::uint64_t started_at_ = 0;
};

int display_coordinate(int raw, int minimum, int maximum, int extent);
int toolbar_hit(int x, int y, int buttons);
int list_row_hit(int x, int y, int left, int right, int top, int pitch, int row_height, int rows = 6);
} } // namespace reader::controls
