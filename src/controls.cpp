#include "controls.hpp"
#include "ui.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace reader { namespace controls {
unsigned Stick::direction(int x, int y) {
    x = std::max(0, std::min(255, x)) - 128;
    y = std::max(0, std::min(255, y)) - 128;
    const auto ax = std::abs(x), ay = std::abs(y);
    if (blocked_) {
        if (ax <= 32 && ay <= 32) blocked_ = false;
        return None;
    }
    const auto threshold = direction_ == None ? 48 : 32;
    if (ax < threshold && ay < threshold) return direction_ = None;
    // Prefer the current axis near diagonals to avoid alternating page/line moves.
    bool horizontal = ax > ay;
    if ((direction_ == Left || direction_ == Right) && ax >= threshold && ay < ax + 16) horizontal = true;
    if ((direction_ == Up || direction_ == Down) && ay >= threshold && ax < ay + 16) horizontal = false;
    direction_ = horizontal ? (x < 0 ? Left : Right) : (y < 0 ? Up : Down);
    return direction_;
}

float axis(int value) {
    const auto delta = std::max(0, std::min(255, value)) - 128;
    const auto amount = std::abs(delta);
    if (amount <= 40) return 0;
    return (delta < 0 ? -1.0f : 1.0f) * std::min(1.0f, (amount - 40) / 87.0f);
}

unsigned Repeat::sample(unsigned held, unsigned repeatable, std::uint64_t now) {
    blocked_ &= held;
    held &= ~blocked_;
    held_ = held;
    released_ = previous_ & ~held;
    auto pressed = held & ~previous_;
    const auto directions = held & repeatable;
    if (directions != (previous_ & repeatable)) repeat_at_ = now + 350000;
    else if (directions && now >= repeat_at_) {
        pressed |= directions;
        repeat_at_ = now + 140000;
    }
    previous_ = held;
    return pressed;
}

void Repeat::cancel_until_release(unsigned held) {
    blocked_ |= held;
    previous_ &= ~held;
    held_ &= ~held;
    released_ = 0;
}

void Touch::cancel_until_release() {
    active_ = false; blocked_ = true; multi_ = false;
    second_id_ = -1; distance_ = 0;
}

Gesture Touch::sample(const std::vector<Point>& points, std::uint64_t now, bool live_transform) {
    if (blocked_) {
        if (points.empty()) blocked_ = false;
        return {};
    }
    if (points.empty()) {
        Gesture result;
        if (active_ && !multi_ && now >= started_at_) {
            const auto duration = now - started_at_;
            const float dx = last_.x - start_.x, dy = last_.y - start_.y;
            if (!moved_ && duration <= 500000) result = {GestureKind::Tap, float(last_.x), float(last_.y), 0, 0, 1};
            else if (!moved_ && duration >= 600000) result = {GestureKind::Hold, float(last_.x), float(last_.y), 0, 0, 1};
            else if (!live_transform && duration <= 1500000 && std::max(std::abs(dx), std::abs(dy)) >= 48)
                result = {GestureKind::Swipe, float(last_.x), float(last_.y), dx, dy, 1};
        }
        result.start_x = start_.x; result.start_y = start_.y;
        active_ = false; multi_ = false; second_id_ = -1; distance_ = 0;
        return result;
    }
    if (!active_) {
        start_ = last_ = points.front(); started_at_ = now;
        active_ = true; multi_ = false; moved_ = false; second_id_ = -1; distance_ = 0;
    }
    const auto primary = std::find_if(points.begin(), points.end(), [&](const Point& point) { return point.id == last_.id; });
    if (primary == points.end()) { cancel_until_release(); return {}; }
    if (points.size() > 2) { cancel_until_release(); return {}; }
    if (points.size() == 2) {
        multi_ = true;
        if (!live_transform) return {};
        const auto& second = points[0].id == primary->id ? points[1] : points[0];
        const float dx = second.x - primary->x, dy = second.y - primary->y;
        const auto distance = std::sqrt(dx * dx + dy * dy);
        const auto middle_x = (primary->x + second.x) / 2.0f, middle_y = (primary->y + second.y) / 2.0f;
        Gesture result;
        if (second_id_ == second.id && distance_ >= 12 && distance >= 12)
            result = {GestureKind::Transform, middle_x, middle_y, middle_x - middle_x_, middle_y - middle_y_, distance / distance_};
        second_id_ = second.id; distance_ = distance; middle_x_ = middle_x; middle_y_ = middle_y;
        result.start_x = start_.x; result.start_y = start_.y;
        last_ = *primary;
        return result;
    }
    if (second_id_ != -1) { last_ = *primary; second_id_ = -1; distance_ = 0; return {}; }
    const auto dx = primary->x - last_.x, dy = primary->y - last_.y;
    if (std::max(std::abs(primary->x - start_.x), std::abs(primary->y - start_.y)) > 12) moved_ = true;
    last_ = *primary;
    if (live_transform && moved_ && (dx || dy))
        return {GestureKind::Transform, float(last_.x), float(last_.y), float(dx), float(dy), 1, float(start_.x), float(start_.y)};
    return {};
}

int display_coordinate(int raw, int minimum, int maximum, int extent) {
    if (maximum <= minimum || extent <= 0) return 0;
    const auto scaled = static_cast<std::int64_t>(raw - minimum) * extent / (maximum - minimum + 1);
    return static_cast<int>(std::max<std::int64_t>(0, std::min<std::int64_t>(extent - 1, scaled)));
}
int toolbar_hit(int x, int y, int buttons) {
    if (buttons <= 0) return -1;
    for (int i = 0; i < buttons; ++i)
        if (ui::toolbar_button(i,buttons).contains(x,y)) return i;
    return -1;
}
int list_row_hit(int x, int y, int left, int right, int top, int pitch, int row_height, int rows) {
    if (pitch <= 0 || x < left || x >= right || y < top) return -1;
    const auto row = (y - top) / pitch;
    return row < rows && (y - top) % pitch < row_height ? row : -1;
}
} } // namespace reader::controls
