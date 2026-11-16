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
#include <set>
#ifdef __vita__
#include "firmware_fonts.hpp"
#endif

namespace reader
{
    struct VitaFonts::Impl
    {
        struct Face
        {
            mutable vita2d_font *font = nullptr;
            FT_Face coverage = nullptr;
            const char *path = nullptr;
            mutable unsigned first_pixels = 0;
            mutable std::map<unsigned, vita2d_font *> sizes;
            mutable std::map<unsigned, std::set<std::uint32_t>> drawn_glyphs;
            mutable std::vector<vita2d_font *> retired_fonts;
            mutable std::map<std::pair<unsigned, std::uint32_t>, int> advances;

            int advance(unsigned pixels, std::uint32_t code) const
            {
                const auto key = std::make_pair(pixels, code);
                const auto found = advances.find(key);
                if (found != advances.end())
                    return found->second;
                int result = 0;
                // Metrics must not rasterize glyphs or fill vita2d's finite atlas.
                if (!FT_Set_Pixel_Sizes(coverage, 0, pixels) &&
                    !FT_Load_Glyph(coverage, FT_Get_Char_Index(coverage, code), FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL))
                    result = coverage->glyph->advance.x >> 6;
                if (advances.size() >= 4096)
                    advances.clear();
                advances.emplace(key, result);
                return result;
            }

            vita2d_font *at_size(unsigned pixels) const
            {
                if (!font)
                    font = vita2d_load_font_file(path);
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

            bool atlas_full(unsigned pixels, std::uint32_t code) const
            {
                const auto found = drawn_glyphs.find(pixels);
                return found != drawn_glyphs.end() && found->second.size() >= 128 && !found->second.count(code);
            }

            void rotate_atlas(unsigned pixels) const
            {
                auto *replacement = vita2d_load_font_file(path);
                if (!replacement)
                    return;
                auto &current = first_pixels == pixels ? font : sizes[pixels];
                if (current)
                    retired_fonts.push_back(current);
                current = replacement;
                drawn_glyphs[pixels].clear();
            }

            void finish_frame() const
            {
                for (auto *old : retired_fonts)
                    vita2d_free_font(old);
                retired_fonts.clear();
            }
        };
        FT_Library library = nullptr;
        vita2d_pgf *system;
#ifdef __vita__
        FirmwareFonts firmware;
#endif
        std::array<Face, 5> faces{};
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
            unsigned face = 1;
        };
        mutable std::deque<Title> titles;
        mutable std::vector<std::unique_ptr<vita2d_texture, TextureDeleter>> retired;
        mutable std::map<std::pair<unsigned, std::string>, int> title_widths;
        mutable std::map<std::pair<unsigned, std::string>, int> system_widths;
        mutable std::map<std::pair<FontRole, std::uint32_t>, int> routes;

        template <class Visit>
        bool title_glyphs(unsigned pixels, const std::string &text, bool raster, Visit visit, int &advance,
                          unsigned face_index = 1, bool kerning = true) const
        {
            const auto face = faces[face_index].coverage;
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
                if (kerning && previous && FT_HAS_KERNING(face))
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

        bool draw_run(unsigned face_index, unsigned pixels, int x, int y, unsigned color,
                      const std::string &text, bool heading = false) const
        {
            auto &cache = titles;
            for (const auto &title : cache)
                if (title.face == face_index && title.pixels == pixels && title.text == text)
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
                                  bottom = std::max(bottom, -glyph->bitmap_top + static_cast<int>(bitmap.rows)); }, advance, face_index, heading) ||
                !supported)
                return false;
            // Two transparent pixels on all edges prevent filtering into neighbors.
            const int width = right - left + 4, height = bottom - top + 4;
            if (width > 4096 || height > 4096 || static_cast<std::size_t>(width) * height > 2 * 1024 * 1024)
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
                                  } }, advance, face_index, heading))
                return false;
            vita2d_texture_set_filters(texture.get(), SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
            // Only the small Home heading cache uses whole-line textures.
            const auto bytes = [](const Title &entry)
            { return vita2d_texture_get_stride(entry.texture.get()) * vita2d_texture_get_height(entry.texture.get()); };
            std::size_t used = stride * height;
            for (const auto &entry : cache)
                used += bytes(entry);
            while (!cache.empty() && (cache.size() >= 8u || used > 2 * 1024 * 1024))
            {
                used -= bytes(cache.front());
                retired.push_back(std::move(cache.front().texture));
                cache.pop_front();
            }
            cache.push_back({text, pixels, left - 2, top - 2, std::move(texture), face_index});
            const auto &title = cache.back();
            vita2d_draw_texture_tint(title.texture.get(), x + title.left, y + title.top, color);
            return true;
        }

        explicit Impl(vita2d_pgf *pgf) : system(pgf)
        {
            const perf::Scope timing(perf::Event::FontInit);
            const char *files[] = {
                "app0:/fonts/SourceSans3-Regular.ttf", "app0:/fonts/SourceSans3-Semibold.ttf",
                "app0:/fonts/Literata-Regular.ttf", "app0:/fonts/AtkinsonHyperlegibleNext-Regular.ttf",
                "ux0:data/NovelReader/fonts/fallback.ttf"};
            if (FT_Init_FreeType(&library))
                return;
            for (std::size_t i = 0; i < faces.size(); ++i)
            {
                auto &face = faces[i];
                face.path = files[i];
                if (!FT_New_Face(library, files[i], 0, &face.coverage))
                {
                    if (FT_Select_Charmap(face.coverage, FT_ENCODING_UNICODE))
                    {
                        FT_Done_Face(face.coverage);
                        face.coverage = nullptr;
                        continue;
                    }
                }
            }
        }
        ~Impl()
        {
            for (auto &face : faces)
            {
                face.finish_frame();
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
                   const std::string &text, bool drawing, std::vector<int> *prefixes = nullptr) const
        {
            const auto index = role == FontRole::Heading ? 1u : role == FontRole::Serif ? 2u
                                                            : role == FontRole::Sans    ? 3u
                                                                                        : 0u;
            int advance = prefixes ? prefixes->back() : 0;
            std::string run;
            int run_face = -1;
            const auto system_width = [&](const std::string &value)
            {
                const auto key = std::make_pair(pixels, value);
                const auto found = system_widths.find(key);
                if (found != system_widths.end())
                    return found->second;
                const auto result = vita2d_pgf_text_width(system, pixels / 20.0f, value.c_str());
                if (system_widths.size() >= 4096)
                    system_widths.clear();
                system_widths.emplace(key, result);
                return result;
            };
            int run_width = 0;
            const auto flush = [&]()
            {
                if (run.empty())
                    return;
                if (run_face >= 0)
                {
                    if (drawing)
                        if (auto *font = faces[run_face].at_size(pixels))
                            vita2d_font_draw_text(font, x + advance, y, color, pixels, run.c_str());
                    advance += run_width;
                }
                else
                {
                    const float scale = pixels / 20.0f;
                    if (drawing)
                        vita2d_pgf_draw_text(system, x + advance, y, color, scale, run.c_str());
                    advance += system_width(run);
                }
                run.clear();
                run_width = 0;
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
                int selected = -1;
                if (valid && role != FontRole::System)
                {
                    const auto key = std::make_pair(role, code);
                    const auto found = routes.find(key);
                    if (found != routes.end())
                        selected = found->second;
                    else
                    {
                        // Prefer the chosen family, then other bundled coverage,
                        // then an optional user-supplied Unicode fallback font.
                        const unsigned candidates[] = {index, 0, 2, 3, 1, 4};
                        for (const auto candidate : candidates)
                            if (faces[candidate].coverage && FT_Get_Char_Index(faces[candidate].coverage, code))
                            {
                                selected = candidate;
                                break;
                            }
                        if (routes.size() >= 4096)
                            routes.clear();
                        routes.emplace(key, selected);
                    }
                }
                if (prefixes)
                {
                    if (selected >= 0)
                        advance += faces[selected].advance(pixels, code);
#ifdef __vita__
                    else if (firmware.available())
                        advance += firmware.advance(pixels, code);
#endif
                    else
                        return -1; // Legacy PGF measures a whole run with its own rounding.
                    prefixes->resize(prefixes->size() + at - start, advance);
                    continue;
                }
                if (!run.empty() && selected != run_face)
                    flush();
#ifdef __vita__
                if (selected < 0 && firmware.available())
                {
                    flush();
                    if (drawing)
                        firmware.draw(pixels, code, x + advance, y, color);
                    advance += firmware.advance(pixels, code);
                    continue;
                }
#endif
                run_face = selected;
                if (selected >= 0)
                {
                    if (drawing)
                    {
                        const auto &face = faces[selected];
                        if (face.atlas_full(pixels, code))
                        {
                            flush();
                            face.rotate_atlas(pixels);
                        }
                        face.drawn_glyphs[pixels].insert(code);
                    }
                    run_width += faces[selected].advance(pixels, code);
                }
                run.append(text, start, at - start);
            }
            flush();
            return advance;
        }
    };

    VitaFonts::VitaFonts(vita2d_pgf *system) : impl_(new Impl(system)) {}
    VitaFonts::~VitaFonts() = default;
    void VitaFonts::finish_frame() const
    {
        impl_->retired.clear();
        for (const auto &face : impl_->faces)
            face.finish_frame();
#ifdef __vita__
        impl_->firmware.finish_frame();
#endif
    }
    int VitaFonts::width(FontRole role, unsigned pixels, const std::string &text) const
    {
        return impl_->render(role, pixels, 0, 0, 0, text, false);
    }
    LineMeasure VitaFonts::line_measure(FontRole role, unsigned pixels) const
    {
        return [this, role, pixels, prefixes = std::vector<int>{}, additive = true]
               (const std::string &source, std::size_t begin, std::size_t end, bool first) mutable
        {
            if (first || prefixes.empty())
            {
                prefixes.assign(1, 0);
                additive = true;
            }
            const auto length = end - begin;
            if (additive && length >= prefixes.size() && length < 4096)
            {
                const auto known = prefixes.size() - 1;
                additive = impl_->render(role, pixels, 0, 0, 0, source.substr(begin + known, length - known), false, &prefixes) >= 0;
            }
            return length < prefixes.size() ? prefixes[length] : width(role, pixels, source.substr(begin, length));
        };
    }
    void VitaFonts::draw(FontRole role, unsigned pixels, int x, int y, unsigned color, const std::string &text) const
    {
        impl_->render(role, pixels, x, y, color, text, true);
    }
    bool VitaFonts::complete() const
    {
        for (std::size_t i = 0; i < 4; ++i)
            if (!impl_->faces[i].coverage)
                return false;
        return true;
    }
    int VitaFonts::title_width(unsigned pixels, const std::string &text) const
    {
        return impl_->title_width(pixels, text);
    }
    void VitaFonts::draw_title(unsigned pixels, int x, int y, unsigned color, const std::string &text) const
    {
        if (!impl_->draw_run(1, pixels, x, y, color, text, true))
            impl_->render(FontRole::Heading, pixels, x, y, color, text, true);
    }
}
