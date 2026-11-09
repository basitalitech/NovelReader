#include "vita_fonts.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <array>
#include <cstdint>

namespace reader {
struct VitaFonts::Impl {
    struct Face { vita2d_font* font = nullptr; FT_Face coverage = nullptr; };
    FT_Library library = nullptr;
    vita2d_pgf* system;
    std::array<Face, 4> faces{};

    explicit Impl(vita2d_pgf* pgf) : system(pgf) {
        const char* files[] = {
            "app0:/fonts/SourceSans3-Regular.ttf", "app0:/fonts/SourceSans3-Semibold.ttf",
            "app0:/fonts/Literata-Regular.ttf", "app0:/fonts/AtkinsonHyperlegibleNext-Regular.ttf"
        };
        if (FT_Init_FreeType(&library)) return;
        for (std::size_t i = 0; i < faces.size(); ++i) {
            auto& face = faces[i];
            if (!FT_New_Face(library, files[i], 0, &face.coverage)) {
                if (FT_Select_Charmap(face.coverage, FT_ENCODING_UNICODE)) continue;
                face.font = vita2d_load_font_file(files[i]);
            }
        }
    }
    ~Impl() {
        for (auto& face : faces) {
            if (face.font) vita2d_free_font(face.font);
            if (face.coverage) FT_Done_Face(face.coverage);
        }
        if (library) FT_Done_FreeType(library);
    }

    int render(FontRole role, unsigned pixels, int x, int y, unsigned color,
               const std::string& text, bool drawing) const {
        const auto index = role == FontRole::Heading ? 1u : role == FontRole::Serif ? 2u : role == FontRole::Sans ? 3u : 0u;
        const Face* face = role==FontRole::System ? nullptr : &faces[index];
        int advance = 0;
        std::string run;
        bool custom_run = false;
        const auto flush = [&] {
            if (run.empty()) return;
            if (custom_run) {
                if (drawing) vita2d_font_draw_text(face->font, x + advance, y, color, pixels, run.c_str());
                advance += vita2d_font_text_width(face->font, pixels, run.c_str());
            } else {
                const float scale = pixels / 20.0f;
                if (drawing) vita2d_pgf_draw_text(system, x + advance, y, color, scale, run.c_str());
                advance += vita2d_pgf_text_width(system, scale, run.c_str());
            }
            run.clear();
        };
        // Input is normalized UTF-8. Invalid/truncated sequences fall back safely.
        for (std::size_t at = 0; at < text.size();) {
            const auto start = at;
            const auto lead = static_cast<unsigned char>(text[at++]);
            std::uint32_t code = lead;
            unsigned extra = 0;
            if (lead >= 0xc2 && lead <= 0xdf) { code &= 0x1f; extra = 1; }
            else if (lead >= 0xe0 && lead <= 0xef) { code &= 0x0f; extra = 2; }
            else if (lead >= 0xf0 && lead <= 0xf4) { code &= 7; extra = 3; }
            bool valid = lead < 0x80 || extra;
            for (unsigned i = 0; i < extra; ++i) {
                if (at >= text.size() || (static_cast<unsigned char>(text[at]) & 0xc0) != 0x80) { valid = false; break; }
                code = (code << 6) | (static_cast<unsigned char>(text[at++]) & 0x3f);
            }
            const bool custom = valid && face && face->font && FT_Get_Char_Index(face->coverage, code);
            if (!run.empty() && custom != custom_run) flush();
            custom_run = custom;
            run.append(text, start, at - start);
        }
        flush();
        return advance;
    }
};

VitaFonts::VitaFonts(vita2d_pgf* system) : impl_(new Impl(system)) {}
VitaFonts::~VitaFonts() = default;
int VitaFonts::width(FontRole role, unsigned pixels, const std::string& text) const {
    return impl_->render(role, pixels, 0, 0, 0, text, false);
}
void VitaFonts::draw(FontRole role, unsigned pixels, int x, int y, unsigned color, const std::string& text) const {
    impl_->render(role, pixels, x, y, color, text, true);
}
bool VitaFonts::complete() const {
    for (const auto& face : impl_->faces) if (!face.font) return false;
    return true;
}
}
