#pragma once
#include <array>
#include <cstddef>

namespace reader
{
    namespace curl
    {
        constexpr float width = 960.f, height = 544.f;
        // A rectangle clipped to a cylindrical band has at most six corners. Twelve
        // bands per face plus its flat polygon need at most 153 triangle vertices.
        constexpr std::size_t max_vertices = 192;
        struct Vertex
        {
            float x, y, u, v, height, normal;
        };
        struct Mesh
        {
            std::array<Vertex, max_vertices> front, back;
            std::size_t front_count = 0, back_count = 0;
            bool complete = true;
        };
        // Fixed storage, bounded clipping work, no heap allocation or publication I/O.
        // Positions and UVs mirror for a bottom-left turn; the corner stays at the bottom
        // while finger height controls how steeply the sheet lifts.
        void build(Mesh &mesh, float progress, bool forward, float finger_height = 0.85f);
    }
}