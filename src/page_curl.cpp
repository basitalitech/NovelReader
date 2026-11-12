#include "page_curl.hpp"
#include <algorithm>
#include <cmath>

namespace reader
{
    namespace curl
    {
        namespace
        {
            constexpr float pi = 3.14159265358979323846f;
            constexpr int bands = 24;
            struct Point
            {
                float x, y;
            };
            struct Polygon
            {
                std::array<Point, 8> points;
                int size = 0;
            };
            struct Fold
            {
                float nx, ny, travel, radius;
                float distance(Point p) const { return nx * (p.x - width) + ny * (p.y - height) + travel / 2; }
            };
            Polygon clip(const Polygon &input, const Fold &fold, float boundary, bool above)
            {
                Polygon result;
                if (!input.size)
                    return result;
                auto previous = input.points[input.size - 1];
                float previous_d = fold.distance(previous) - boundary;
                bool previous_inside = above ? previous_d >= 0 : previous_d <= 0;
                for (int i = 0; i < input.size; ++i)
                {
                    const auto current = input.points[i];
                    const float current_d = fold.distance(current) - boundary;
                    const bool inside = above ? current_d >= 0 : current_d <= 0;
                    if (inside != previous_inside)
                    {
                        const float t = previous_d / (previous_d - current_d);
                        result.points[result.size++] = {previous.x + (current.x - previous.x) * t, previous.y + (current.y - previous.y) * t};
                    }
                    if (inside)
                        result.points[result.size++] = current;
                    previous = current;
                    previous_d = current_d;
                    previous_inside = inside;
                }
                return result;
            }
            // Each band is affine. Sampling its two ends once avoids trigonometry per
            // vertex, cracks at triangle boundaries, and a dense screen-sized grid.
            void emit(Mesh &mesh, const Polygon &polygon, const Fold &fold, bool forward, bool back,
                      float low, float high, float sin_low, float sin_high, float cos_low, float cos_high, int region)
            {
                if (polygon.size < 3)
                    return;
                std::array<Vertex, 8> transformed;
                for (int i = 0; i < polygon.size; ++i)
                {
                    const auto p = polygon.points[i];
                    const float d = fold.distance(p);
                    float q = d, z = 0, normal = 1;
                    if (region == 1)
                    {
                        const float t = std::max(0.f, std::min(1.f, (d - low) / (high - low)));
                        q = fold.radius * (sin_low + (sin_high - sin_low) * t);
                        normal = cos_low + (cos_high - cos_low) * t;
                        z = fold.radius * (1 - normal);
                    }
                    else if (region == 2)
                    {
                        q = pi * fold.radius - d;
                        z = 2 * fold.radius;
                        normal = -1;
                    }
                    float x = p.x + fold.nx * (q - d), y = p.y + fold.ny * (q - d);
                    // Mild perspective makes the lifted sheet swell naturally; the flat
                    // face stays pixel-aligned and retains the reader's original type.
                    x += (x - width / 2) * (z / 1800);
                    y += (y - height / 2) * (z / 1800);
                    transformed[i] = {forward ? x : width - x, y, forward ? p.x / width : 1 - p.x / width, p.y / height, z, normal};
                }
                auto &count = back ? mesh.back_count : mesh.front_count;
                auto &vertices = back ? mesh.back : mesh.front;
                for (int i = 1; i + 1 < polygon.size; ++i)
                {
                    if (count + 3 > max_vertices)
                    {
                        mesh.complete = false;
                        return;
                    }
                    // Zero-area polygons at the first/last frame need no GPU submission.
                    const auto &a = transformed[0];
                    const auto &b = transformed[i];
                    const auto &c = transformed[i + 1];
                    if (std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) < 0.001f)
                        continue;
                    vertices[count++] = a;
                    vertices[count++] = b;
                    vertices[count++] = c;
                }
            }
        }
        void build(Mesh &mesh, float progress, bool forward, float finger_height)
        {
            mesh.front_count = mesh.back_count = 0;
            mesh.complete = true;
            if (!std::isfinite(progress))
                progress = 0;
            progress = std::max(0.f, std::min(1.f, progress));
            if (!std::isfinite(finger_height))
                finger_height = 0.85f;
            finger_height = std::max(0.f, std::min(1.f, finger_height));
            if (progress >= 1)
                return;
            Polygon page;
            page.size = 4;
            page.points[0] = {0, 0};
            page.points[1] = {width, 0};
            page.points[2] = {width, height};
            page.points[3] = {0, height};
            const float bend = std::sin(pi * progress);
            const float dx = 2 * width * progress, dy = height * (0.55f + 0.25f * (1 - finger_height)) * bend;
            const float travel = std::sqrt(dx * dx + dy * dy);
            const Fold fold{travel > 0 ? dx / travel : 1, travel > 0 ? dy / travel : 0, travel, std::max(0.001f, 40 * bend)};
            emit(mesh, clip(page, fold, 0, false), fold, forward, false, 0, 0, 0, 0, 1, 1, 0);
            if (progress <= 0)
                return;
            // Continue the existing rounded fold into a complete reflected sheet,
            // rather than ending the reverse side after a narrow mirrored strip.
            for (int band = 0; band < bands; ++band)
            {
                const float a = pi * band / bands, b = pi * (band + 1) / bands;
                const float low = fold.radius * a, high = fold.radius * b;
                const auto polygon = clip(clip(page, fold, low, true), fold, high, false);
                if (polygon.size < 3)
                    continue;
                emit(mesh, polygon, fold, forward, band >= bands / 2, low, high, std::sin(a), std::sin(b), std::cos(a), std::cos(b), 1);
            }
            emit(mesh, clip(page, fold, pi * fold.radius, true), fold, forward, true, 0, 0, 0, 0, -1, -1, 2);
        }
    }
}