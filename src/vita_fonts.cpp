#include "vita_fonts.hpp"
#include "performance.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <array>
#include <cstdint>
#include <map>
#include <algorithm>
#include <cstring>
#include <deque>

namespace reader
{
    struct VitaFonts::Impl
    {
        struct Face
        {
            vita2d_font *font = nullptr;
            FT_Face coverage = nullptr;
            const char *path = nullptr;
            mutable unsigned first_pixels = 0;
            mutable std::map<unsigned, vita2d_font *> sizes;

            vita2d_font *at_size(unsigned pixels) const
            {
                if (!font)
                    return nullptr;
                // vita2d's atlas keys only on glyph index, not raster size. Sharing
                // an atlas between sizes rescales whichever bitmap was cached first,
                // even when that first request was only a text-width measurement.
                if (!first_pixels)
                    first_pixels = pixels;
                if (first_pixels == pixels)
                    return font;
                const auto found = sizes.find(pixels);
                if (found != sizes.end())
                    return found->second;
                auto *sized_font = vita2d_load_font_file(path);
                sizes.emplace(pixels, sized_font);
                return sized_font;
            }
        };
        FT_Library library = nullptr;
        vita2d_pgf *system;
        std::array<Face, 4> faces{};
        struct TextureDeleter
        {
            void operator()(vita2d_texture *texture) const { vita2d_free_texture(texture); }
        };
        struct Title
        {
            std::string text;
            unsigned pixels;
            int left, top;
            std::unique_ptr<vita2d_texture, TextureDeleter> texture;
        };
        mutable std::deque<Title> titles;
        mutable std::map<std::pair<unsigned, std::string>, int> title_widths;

        template <class Visit>
        bool title_glyphs(unsigned pixels, const std::string &text, bool raster, Visit visit, int &advance) const
        {
            const auto face = faces[1].coverage;
            if (!face || !pixels || FT_Set_Pixel_Sizes(face, 0, pixels))
                return false;
            advance = 0;
            unsigned previous = 0;
            for (std::size_t at = 0; at < text.size();)
            {
                const auto lead = static_cast<unsigned char>(text[at++]);
                unsigned code = lead, extra = 0;
                if (lead >= 0xc2 && lead <= 0xdf)
                {
                    code &= 0x1f;
                    extra = 1;
                }
                else if (lead >= 0xe0 && lead <= 0xef)
                {
                    code &= 0x0f;
                    extra = 2;
                }
                else if (lead >= 0xf0 && lead <= 0xf4)
                {
                    code &= 7;
                    extra = 3;
                }
                else if (lead >= 0x80)
                    return false;
                for (unsigned i = 0; i < extra; ++i)
                {
                    if (at == text.size() || (static_cast<unsigned char>(text[at]) & 0xc0) != 0x80)
                        return false;
                    code = (code << 6) | (static_cast<unsigned char>(text[at++]) & 0x3f);
                }
                if (code < 32)
                    return false;
                const auto glyph = FT_Get_Char_Index(face, code);
                if (!glyph)
                    return false;
                if (previous && FT_HAS_KERNING(face))
                {
                    FT_Vector delta{};
                    if (!FT_Get_Kerning(face, previous, glyph, FT_KERNING_DEFAULT, &delta))
                        advance += delta.x >> 6;
                }
                if (FT_Load_Glyph(face, glyph, (raster ? FT_LOAD_RENDER : FT_LOAD_DEFAULT) | FT_LOAD_TARGET_NORMAL))
                    return false;
                visit(advance, face->glyph);
                advance += face->glyph->advance.x >> 6;
                previous = glyph;
            }
            return true;
        }

        int title_width(unsigned pixels, const std::string &text) const
        {
            const auto key = std::make_pair(pixels, text);
            const auto found = title_widths.find(key);
            if (found != title_widths.end())
                return found->second;
            int advance = 0;
            if (!title_glyphs(pixels, text, false, [](int, FT_GlyphSlot) {}, advance))
                advance = render(FontRole::Heading, pixels, 0, 0, 0, text, false);
            if (title_widths.size() >= 512)
                title_widths.clear();
            title_widths.emplace(key, advance);
            return advance;
        }

        bool draw_title(unsigned pixels, int x, int y, unsigned color, const std::string &text) const
        {
            for (const auto &title : titles)
                if (title.pixels == pixels && title.text == text)
                {
                    vita2d_draw_texture_tint(title.texture.get(), x + title.left, y + title.top, color);
                    return true;
                }
            int left = 0, top = 0, right = 0, bottom = 0, advance = 0;
            bool supported = true;
            if (!title_glyphs(pixels, text, true, [&](int pen, FT_GlyphSlot glyph)
                              {
                                  const auto &bitmap = glyph->bitmap;
                                  if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.pixel_mode != FT_PIXEL_MODE_MONO && bitmap.width && bitmap.rows)
                                      supported = false;
                                  left = std::min(left, pen + glyph->bitmap_left);
                                  top = std::min(top, -glyph->bitmap_top);
                                  right = std::max(right, pen + glyph->bitmap_left + static_cast<int>(bitmap.width));
                                  bottom = std::max(bottom, -glyph->bitmap_top + static_cast<int>(bitmap.rows)); }, advance) ||
                !supported)
                return false;
            // Two transparent pixels on all edges prevent filtering into neighbors.
            const int width = right - left + 4, height = bottom - top + 4;
            if (width > 4096 || height > 4096)
                return false;
            std::unique_ptr<vita2d_texture, TextureDeleter> texture(
                vita2d_create_empty_texture_format(width, height, SCE_GXM_TEXTURE_FORMAT_U8_R111));
            if (!texture)
                return false;
            const auto stride = vita2d_texture_get_stride(texture.get());
            auto *data = static_cast<unsigned char *>(vita2d_texture_get_datap(texture.get()));
            std::memset(data, 0, stride * height);
            if (!title_glyphs(pixels, text, true, [&](int pen, FT_GlyphSlot glyph)
                              {
                                  const auto &bitmap = glyph->bitmap;
                                  for (unsigned row = 0; row < bitmap.rows; ++row)
                                  {
                                      const auto *source = bitmap.buffer + static_cast<int>(row) * bitmap.pitch;
                                      auto *destination = data + (-glyph->bitmap_top - top + 2 + row) * stride + pen + glyph->bitmap_left - left + 2;
                                      for (unsigned col = 0; col < bitmap.width; ++col)
                                      {
                                          const unsigned alpha = bitmap.pixel_mode == FT_PIXEL_MODE_MONO
                                                                     ? ((source[col / 8] & (0x80 >> (col % 8))) ? 255 : 0)
                                                                     : source[col];
                                          destination[col] = alpha + (destination[col] * (255 - alpha) + 127) / 255;
                                      }
                                  } }, advance))
                return false;
            vita2d_texture_set_filters(texture.get(), SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
            // Home draws at most two lines. Keep a small cache across recent books.
            if (titles.size() >= 8)
                titles.pop_front();
            titles.push_back({text, pixels, left - 2, top - 2, std::move(texture)});
            const auto &title = titles.back();
            vita2d_draw_texture_tint(title.texture.get(), x + title.left, y + title.top, color);
            return true;
        }

        explicit Impl(vita2d_pgf *pgf) : system(pgf)
        {
            const perf::Scope timing(perf::Event::FontInit);
            const char *files[] = {
                "app0:/fonts/SourceSans3-Regular.ttf", "app0:/fonts/SourceSans3-Semibold.ttf",
                "app0:/fonts/Literata-Regular.ttf", "app0:/fonts/AtkinsonHyperlegibleNext-Regular.ttf"};
            if (FT_Init_FreeType(&library))
                return;
            for (std::size_t i = 0; i < faces.size(); ++i)
            {
                auto &face = faces[i];
                face.path = files[i];
                if (!FT_New_Face(library, files[i], 0, &face.coverage))
                {
                    if (FT_Select_Charmap(face.coverage, FT_ENCODING_UNICODE))
                        continue;
                    face.font = vita2d_load_font_file(files[i]);
                }
            }
        }
        ~Impl()
        {
            for (auto &face : faces)
            {
                for (const auto &sized : face.sizes)
                    if (sized.second)
                        vita2d_free_font(sized.second);
                if (face.font)
                    vita2d_free_font(face.font);
                if (face.coverage)
                    FT_Done_Face(face.coverage);
            }
            if (library)
                FT_Done_FreeType(library);
        }

        int render(FontRole role, unsigned pixels, int x, int y, unsigned color,
                   const std::string &text, bool drawing) const
        {
            const auto index = role == FontRole::Heading ? 1u : role == FontRole::Serif ? 2u
                                                            : role == FontRole::Sans    ? 3u
                                                                                        : 0u;
            const Face *face = role == FontRole::System ? nullptr : &faces[index];
            auto *sized_font = face ? face->at_size(pixels) : nullptr;
            int advance = 0;
            std::string run;
            bool custom_run = false;
            const auto flush = [&](bool last = false)
            {
                if (run.empty())
                    return;
                if (custom_run)
                {
                    if (drawing)
                        vita2d_font_draw_text(sized_font, x + advance, y, color, pixels, run.c_str());
                    if (!drawing || !last)
                        advance += vita2d_font_text_width(sized_font, pixels, run.c_str());
                }
                else
                {
                    const float scale = pixels / 20.0f;
                    if (drawing)
                        vita2d_pgf_draw_text(system, x + advance, y, color, scale, run.c_str());
                    if (!drawing || !last)
                        advance += vita2d_pgf_text_width(system, scale, run.c_str());
                }
                run.clear();
            };
            // Input is normalized UTF-8. Invalid/truncated sequences fall back safely.
            for (std::size_t at = 0; at < text.size();)
            {
                const auto start = at;
                const auto lead = static_cast<unsigned char>(text[at++]);
                std::uint32_t code = lead;
                unsigned extra = 0;
                if (lead >= 0xc2 && lead <= 0xdf)
                {
                    code &= 0x1f;
                    extra = 1;
                }
                else if (lead >= 0xe0 && lead <= 0xef)
                {
                    code &= 0x0f;
                    extra = 2;
                }
                else if (lead >= 0xf0 && lead <= 0xf4)
                {
                    code &= 7;
                    extra = 3;
                }
                bool valid = lead < 0x80 || extra;
                for (unsigned i = 0; i < extra; ++i)
                {
                    if (at >= text.size() || (static_cast<unsigned char>(text[at]) & 0xc0) != 0x80)
                    {
                        valid = false;
                        break;
                    }
                    code = (code << 6) | (static_cast<unsigned char>(text[at++]) & 0x3f);
                }
                const bool custom = valid && sized_font && FT_Get_Char_Index(face->coverage, code);
                if (!run.empty() && custom != custom_run)
                    flush();
                custom_run = custom;
                run.append(text, start, at - start);
            }
            flush(true);
            return advance;
        }
    };

    VitaFonts::VitaFonts(vita2d_pgf *system) : impl_(new Impl(system)) {}
    VitaFonts::~VitaFonts() = default;
    int VitaFonts::width(FontRole role, unsigned pixels, const std::string &text) const
    {
        return impl_->render(role, pixels, 0, 0, 0, text, false);
    }
    void VitaFonts::draw(FontRole role, unsigned pixels, int x, int y, unsigned color, const std::string &text) const
    {
        impl_->render(role, pixels, x, y, color, text, true);
    }
    bool VitaFonts::complete() const
    {
        for (const auto &face : impl_->faces)
            if (!face.font)
                return false;
        return true;
    }
    int VitaFonts::title_width(unsigned pixels, const std::string &text) const
    {
        return impl_->title_width(pixels, text);
    }
    void VitaFonts::draw_title(unsigned pixels, int x, int y, unsigned color, const std::string &text) const
    {
        if (!impl_->draw_title(pixels, x, y, color, text))
            impl_->render(FontRole::Heading, pixels, x, y, color, text, true);
    }
}