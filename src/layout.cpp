#include "reader.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace reader {
std::size_t layout_page_end(const std::vector<LayoutBlock>& blocks, std::size_t start, int page_height) {
    int height = 0;
    auto end = start;
    while (end < blocks.size()) {
        const auto& block = blocks[end];
        if (end > start && (block.standalone || height + block.height > page_height)) break;
        height += block.height;
        ++end;
        if (block.standalone) break;
    }
    return end;
}

std::size_t block_at_offset(const std::vector<LayoutBlock>& blocks, std::size_t offset) {
    const auto found = std::upper_bound(blocks.begin(), blocks.end(), offset,
        [](std::size_t position, const LayoutBlock& block) { return position < block.offset; });
    return found == blocks.begin() ? 0 : static_cast<std::size_t>(found - blocks.begin() - 1);
}

std::size_t previous_page_start(const std::vector<std::size_t>& pages, std::size_t start) {
    const auto found = std::lower_bound(pages.begin(), pages.end(), start);
    return found == pages.begin() ? 0 : *(found - 1);
}

ChapterLayout layout_book(const Book& book, int width, int page_height, int line_height, const Measure& measure) {
    if (width <= 0 || width > 4096 || page_height < 24 || page_height > 4096 || line_height <= 0 || line_height > page_height)
        throw std::invalid_argument("Invalid reader layout dimensions");
    ChapterLayout result;
    auto& blocks = result.blocks;
    const auto append = [&](LayoutBlock block) {
        if (blocks.size() >= 100000) throw std::length_error("Too many display blocks");
        blocks.push_back(std::move(block));
    };
    const auto text = [&](std::size_t begin, std::size_t end, bool after_image) {
        // Images are paragraph blocks. Ignore only whitespace separating them
        // from text, keeping all original byte positions for save compatibility.
        if (after_image) while (begin < end && (book.text[begin] == '\n' || book.text[begin] == ' ')) ++begin;
        if (begin == end) return;
        const auto lines = wrap_text(book.text.substr(begin, end - begin), width, measure);
        for (const auto& line : lines) append({begin + line.offset, line.text, line_height});
    };
    std::size_t cursor = 0;
    bool after_image = false;
    for (std::size_t i = 0; i < book.illustrations.size(); ++i) {
        const auto& image = book.illustrations[i];
        if (image.offset < cursor || image.end <= image.offset || image.end > book.text.size())
            throw std::invalid_argument("Invalid illustration anchor");
        text(cursor, image.offset, after_image);
        // Trailing blank lines need not occupy an otherwise empty image page.
        while (!blocks.empty() && blocks.back().illustration == no_illustration && blocks.back().text.empty()) blocks.pop_back();
        LayoutBlock block{image.offset, "", std::min(page_height, line_height * 2), i};
        if (image.width && image.height) {
            const auto scale = std::min(1.0, std::min(static_cast<double>(width) / image.width,
                                                     static_cast<double>(page_height - 12) / image.height));
            block.image_width = std::max(1, static_cast<int>(std::floor(image.width * scale)));
            block.image_height = std::max(1, static_cast<int>(std::floor(image.height * scale)));
            block.height = block.image_height + 12;
            block.standalone = block.height > page_height * 2 / 3;
            if (block.standalone) block.height = page_height;
        }
        append(std::move(block));
        cursor = image.end; after_image = true;
    }
    text(cursor, book.text.size(), after_image);
    if (blocks.empty()) append({0, "", line_height});
    for (std::size_t at = 0; at < blocks.size();) {
        result.pages.push_back(at);
        at = layout_page_end(blocks, at, page_height);
    }
    return result;
}
} // namespace reader
