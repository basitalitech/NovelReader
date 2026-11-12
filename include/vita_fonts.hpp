#pragma once

#include <vita2d.h>
#include <memory>
#include <string>

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
        int width(FontRole role, unsigned pixels, const std::string &text) const;
        void draw(FontRole role, unsigned pixels, int x, int y, unsigned color, const std::string &text) const;
        // Native-size heading labels bypass vita2d's packed glyph atlas. Textures
        // are cached by complete line; unsupported glyphs retain normal fallback.
        int title_width(unsigned pixels, const std::string &text) const;
        void draw_title(unsigned pixels, int x, int y, unsigned color, const std::string &text) const;
        bool complete() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}