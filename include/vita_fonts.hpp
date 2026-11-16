#pragma once

#include "reader.hpp"
#include <memory>
#include <string>
#include <vita2d.h>

namespace reader
{
    enum class FontRole
    {
        Interface,
        Heading,
        Serif,
        Sans,
        System
    };

    // All measurement and drawing share the same glyph fallback and pixel sizes.
    class VitaFonts
    {
    public:
        explicit VitaFonts(vita2d_pgf *system);
        ~VitaFonts();
        // Release evicted textures after vita2d_wait_rendering_done().
        void finish_frame() const;
        int width(FontRole role, unsigned pixels, const std::string &text) const;
        LineMeasure line_measure(FontRole role, unsigned pixels) const;
        void draw(FontRole role, unsigned pixels, int x, int y, unsigned color,
                  const std::string &text) const;
        // Heading labels additionally use native FreeType kerning.
        int title_width(unsigned pixels, const std::string &text) const;
        void draw_title(unsigned pixels, int x, int y, unsigned color,
                        const std::string &text) const;
        bool complete() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace reader
