#pragma once

// The firmware PGF fonts supply Latin, Japanese, Chinese and Korean coverage.
// Use metrics directly: vita2d's text-width API rasterizes into a fixed atlas
// and starts losing glyphs/advances once that atlas fills.
#include <vita2d.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace reader
{
    class FirmwareFonts
    {
        struct TextureDeleter
        {
            void operator()(vita2d_texture *texture) const { vita2d_free_texture(texture); }
        };
        using Texture = std::unique_ptr<vita2d_texture, TextureDeleter>;
        struct Glyph
        {
            SceFontHandle font = nullptr;
            SceFontCharInfo info{};
            unsigned code = 0;
        };
        mutable SceFontLibHandle library_ = nullptr;
        mutable bool library_tried_ = false;
        mutable std::array<SceFontHandle, 4> fonts_{};
        mutable std::array<bool, 4> font_tried_{};
        mutable std::map<unsigned, Glyph> metrics_;
        struct Page
        {
            Texture texture;
            unsigned id, x = 0, y = 0, row_height = 0;
        };
        struct Placement
        {
            unsigned page, x, y;
        };
        mutable std::vector<Page> pages_;
        mutable std::map<unsigned, Placement> placements_;
        mutable unsigned next_page_ = 0;
        mutable std::vector<Texture> retired_;

        bool ensure_library() const
        {
            if (library_tried_)
                return library_ != nullptr;
            library_tried_ = true;
            SceFontNewLibParams params{};
            params.numFonts = fonts_.size();
            params.allocFunc = [](void *, unsigned size) -> void *
            { return std::malloc(size); };
            params.freeFunc = [](void *, void *memory)
            { std::free(memory); };
            unsigned error = 0;
            library_ = sceFontNewLib(&params, &error);
            if (error && library_)
            {
                sceFontDoneLib(library_);
                library_ = nullptr;
            }
            return library_ != nullptr;
        }

        SceFontHandle font(unsigned i) const
        {
            if (!ensure_library() || font_tried_[i])
                return fonts_[i];
            font_tried_[i] = true;
            const SceFontLanguageCode languages[] = {SCE_FONT_LANGUAGE_LATIN, SCE_FONT_LANGUAGE_JAPANESE,
                                                     SCE_FONT_LANGUAGE_KOREAN, SCE_FONT_LANGUAGE_CHINESE};
            SceFontStyle style{};
            style.fontH = style.fontV = 10.0f;
            style.fontLanguage = languages[i];
            unsigned error = 0;
            const auto index = sceFontFindOptimumFont(library_, &style, &error);
            if (!error)
            {
                auto *opened = sceFontOpen(library_, index, 0, &error);
                if (!error)
                    fonts_[i] = opened;
                else if (opened)
                    sceFontClose(opened);
            }
            return fonts_[i];
        }

        Glyph glyph(unsigned code) const
        {
            // PGF has BMP coverage. Use a visible replacement for other scalars.
            if (code > 0xffff)
                code = 0xfffd;
            const auto found = metrics_.find(code);
            if (found != metrics_.end())
                return found->second;
            const unsigned preferred = (code >= 0xac00 && code <= 0xd7af) ||
                                               (code >= 0x1100 && code <= 0x11ff) ||
                                               (code >= 0x3130 && code <= 0x318f)
                                           ? 2
                                       : code >= 0x3000 && code <= 0x30ff ? 1
                                       : code >= 0x3400 && code <= 0x9fff ? 3
                                                                          : 0;
            Glyph result;
            result.code = code;
            const unsigned order[] = {preferred, 0, 1, 2, 3};
            for (const auto index : order)
                if (font(index) && sceFontGetCharInfo(fonts_[index], code, &result.info) >= 0 &&
                    (result.info.sfp26AdvanceH || result.info.bitmapWidth))
                {
                    result.font = fonts_[index];
                    break;
                }
            if (!result.font && code != '?')
                result = glyph('?');
            if (metrics_.size() >= 4096)
                metrics_.clear();
            metrics_.emplace(code, result);
            return result;
        }

    public:
        FirmwareFonts() = default;
        ~FirmwareFonts()
        {
            for (auto *font : fonts_)
                if (font)
                    sceFontClose(font);
            if (library_)
                sceFontDoneLib(library_);
        }
        bool available() const { return ensure_library(); }

        int advance(unsigned pixels, unsigned code) const
        {
            const auto value = glyph(code);
            return static_cast<int>((value.info.sfp26AdvanceH >> 6) * (pixels / 20.0f));
        }

        void draw(unsigned pixels, unsigned code, int x, int y, unsigned color) const
        {
            if (code > 0xffff)
                code = 0xfffd;
            const auto value = glyph(code);
            const auto &info = value.info;
            if (!value.font || !info.bitmapWidth || !info.bitmapHeight || info.bitmapWidth > 128 || info.bitmapHeight > 128)
                return;
            auto found = placements_.find(code);
            if (found == placements_.end())
            {
                const unsigned width = info.bitmapWidth + 4, height = info.bitmapHeight + 4;
                if (!pages_.empty() && pages_.back().x + width > 512)
                {
                    auto &page = pages_.back();
                    page.x = 0;
                    page.y += page.row_height;
                    page.row_height = 0;
                }
                if (pages_.empty() || pages_.back().y + height > 512)
                {
                    Texture texture(vita2d_create_empty_texture_format(512, 512, SCE_GXM_TEXTURE_FORMAT_U8_R111));
                    if (!texture)
                        return;
                    const auto stride = vita2d_texture_get_stride(texture.get());
                    std::memset(vita2d_texture_get_datap(texture.get()), 0, stride * 512);
                    vita2d_texture_set_filters(texture.get(), SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
                    if (pages_.size() >= 4)
                    {
                        const auto old = pages_.front().id;
                        retired_.push_back(std::move(pages_.front().texture));
                        pages_.erase(pages_.begin());
                        for (auto at = placements_.begin(); at != placements_.end();)
                            if (at->second.page == old)
                                at = placements_.erase(at);
                            else
                                ++at;
                    }
                    pages_.push_back({std::move(texture), next_page_++});
                }
                auto &page = pages_.back();
                SceFontGlyphImage image{};
                image.pixelFormat = SCE_FONT_PIXELFORMAT_8;
                image.xPos64 = (page.x + 2) * 64;
                image.yPos64 = (page.y + 2) * 64;
                image.bufWidth = image.bufHeight = 512;
                image.bytesPerLine = vita2d_texture_get_stride(page.texture.get());
                image.bufferPtr = reinterpret_cast<std::uintptr_t>(vita2d_texture_get_datap(page.texture.get()));
                // glyph() can select the '?' replacement when a script is absent.
                if (sceFontGetCharGlyphImage(value.font, value.code, &image) < 0)
                    return;
                found = placements_.emplace(code, Placement{page.id, page.x, page.y}).first;
                page.x += width;
                page.row_height = std::max(page.row_height, height);
            }
            const auto &placement = found->second;
            const auto page = std::find_if(pages_.begin(), pages_.end(), [&](const Page &entry)
                                           { return entry.id == placement.page; });
            const float scale = pixels / 20.0f;
            vita2d_draw_texture_tint_part_scale(page->texture.get(),
                                                x + (static_cast<int>(info.bitmapLeft) - 2) * scale,
                                                y - (static_cast<int>(info.bitmapTop) + 2) * scale,
                                                placement.x, placement.y, info.bitmapWidth + 4, info.bitmapHeight + 4, scale, scale, color);
        }

        // Called only after the GPU has completed the submitted frame.
        void finish_frame() const { retired_.clear(); }
    };
}