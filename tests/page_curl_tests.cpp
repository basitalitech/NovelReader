#include "page_curl.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    bool near(float a, float b) { return std::abs(a - b) < 0.003f; }
    float area(const reader::curl::Mesh &mesh)
    {
        float result = 0;
        const auto sum = [&](const auto &vertices, std::size_t count)
        {
            float total = 0;
            for (std::size_t i = 0; i < count; i += 3)
            {
                const auto &a = vertices[i];
                const auto &b = vertices[i + 1];
                const auto &c = vertices[i + 2];
                // UV area measures sheet coverage before the fold projection.
                total += std::abs((b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u)) / 2;
            }
            return total;
        };
        result += sum(mesh.front, mesh.front_count);
        result += sum(mesh.back, mesh.back_count);
        return result;
    }
}
int main()
{
    using namespace reader::curl;
    try
    {
        Mesh forward, backward;
        build(forward, 0, true);
        check(forward.front_count == 6 && !forward.back_count && near(area(forward), 1), "Curl does not start with a complete sheet");
        bool corners[4] = {};
        for (std::size_t i = 0; i < forward.front_count; ++i)
        {
            const auto &v = forward.front[i];
            check(near(v.x, v.u * width) && near(v.y, v.v * height), "Flat page moved or stretched before the turn");
            if (near(v.x, 0) && near(v.y, 0))
                corners[0] = true;
            if (near(v.x, width) && near(v.y, 0))
                corners[1] = true;
            if (near(v.x, 0) && near(v.y, height))
                corners[2] = true;
            if (near(v.x, width) && near(v.y, height))
                corners[3] = true;
        }
        for (bool corner : corners)
            check(corner, "Curl snapshot excludes a screen corner");
        build(forward, 0.03f, true);
        bool top_right_flat = false, bottom_right_moved = false;
        for (std::size_t i = 0; i < forward.front_count; ++i)
        {
            const auto &v = forward.front[i];
            if (near(v.u, 1) && near(v.v, 0) && near(v.x, width) && near(v.y, 0))
                top_right_flat = true;
        }
        for (std::size_t i = 0; i < forward.back_count; ++i)
        {
            const auto &v = forward.back[i];
            if (near(v.u, 1) && near(v.v, 1) && v.x < width - 1 && v.y < height - 1)
                bottom_right_moved = true;
        }
        check(top_right_flat && bottom_right_moved, "Turn does not originate at the bottom-right corner");
        build(forward, 0.5f, true);
        float left = width, right = 0, back_area = 0;
        for (std::size_t i = 0; i < forward.back_count; ++i)
        {
            left = std::min(left, forward.back[i].x);
            right = std::max(right, forward.back[i].x);
        }
        for (std::size_t i = 0; i < forward.back_count; i += 3)
        {
            const auto &a = forward.back[i];
            const auto &b = forward.back[i + 1];
            const auto &c = forward.back[i + 2];
            back_area += std::abs((b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u)) / 2;
        }
        check(right - left > width / 3 && back_area > 0.25f, "Folded sheet is still only a narrow band");
        std::size_t peak = 0;
        for (int step = 1; step < 1000; ++step)
            for (float finger : {0.f, 0.5f, 1.f})
            {
                const float p = step / 1000.f;
                build(forward, p, true, finger);
                build(backward, p, false, finger);
                check(forward.complete && backward.complete, "Curl exceeded its fixed geometry storage");
                check(forward.front_count == backward.front_count && forward.back_count == backward.back_count, "Turn directions have different geometry");
                check(near(area(forward), 1), "Fold drops or duplicates part of the page");
                peak = std::max(peak, forward.front_count + forward.back_count);
                const auto validate = [&](const auto &a, const auto &b, std::size_t count)
                {
                    check(count <= max_vertices && count % 3 == 0, "Invalid triangle count");
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        check(std::isfinite(a[i].x) && std::isfinite(a[i].y) && std::isfinite(a[i].height), "Curl produces invalid geometry");
                        check(a[i].u >= -0.001f && a[i].u <= 1.001f && a[i].v >= -0.001f && a[i].v <= 1.001f, "Curl samples outside the captured screen");
                        check(near(a[i].x + b[i].x, width) && near(a[i].y, b[i].y) && near(a[i].u + b[i].u, 1), "Bottom-left turn does not mirror bottom-right");
                    }
                };
                validate(forward.front, backward.front, forward.front_count);
                validate(forward.back, backward.back, forward.back_count);
            }
        build(forward, 1, true);
        check(!forward.front_count && !forward.back_count, "Completed sheet still covers the next page");
        build(forward, std::numeric_limits<float>::quiet_NaN(), true);
        check(forward.complete && forward.front_count == 6, "Invalid progress does not recover safely");
        const auto start = std::chrono::steady_clock::now();
        std::size_t checksum = 0;
        for (int i = 0; i < 20000; ++i)
        {
            build(forward, (i % 999 + 1) / 1000.f, true);
            checksum += forward.front_count + forward.back_count;
        }
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        std::cout << "Curl geometry passed; peak " << peak << " vertices, host average " << micros / 20000.0 << " us/frame; checksum " << checksum << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}