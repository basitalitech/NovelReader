#include "reader.hpp"
#include "ui.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int columns(const std::string& text) {
    int result = 0;
    for (unsigned char ch : text) if ((ch & 0xc0) != 0x80) ++result;
    return result;
}
reader::Book illustrated(unsigned width, unsigned height) {
    reader::Book book;
    book.text = "Before.\n\n[Illustration: Moon]\n\nAfter.";
    const auto offset = book.text.find("[Illustration");
    const auto end = book.text.find(']', offset) + 1;
    book.illustrations.push_back({offset, "Moon", "image.png", end, width, height});
    return book;
}
void verify_pages(const reader::ChapterLayout& layout, int height) {
    check(!layout.blocks.empty() && !layout.pages.empty() && layout.pages[0] == 0, "Missing initial page");
    std::size_t consumed = 0;
    for (auto start : layout.pages) {
        check(start == consumed, "Pagination skipped or duplicated blocks");
        const auto end = reader::layout_page_end(layout.blocks, start, height);
        check(end > start && end <= layout.blocks.size(), "Pagination did not advance");
        int used = 0;
        for (auto i = start; i < end; ++i) used += layout.blocks[i].height;
        check(used <= height, "Page overflows reading area");
        consumed = end;
    }
    check(consumed == layout.blocks.size(), "Trailing content is unreachable");
}
}

int main() {
    try {
        auto book = illustrated(160, 60);
        auto layout = reader::layout_book(book, 200, 200, 20, columns);
        verify_pages(layout, 200);
        check(layout.blocks.size() == 3 && layout.pages.size() == 1, "Small image did not share page with text");
        check(layout.blocks[0].text == "Before." && layout.blocks[1].illustration == 0 &&
              layout.blocks[2].text == "After.", "Image is out of reading order or marker remains visible");
        check(layout.blocks[1].image_width == 160 && layout.blocks[1].image_height == 60 &&
              layout.blocks[1].height == 72 && !layout.blocks[1].standalone, "Small image size/aspect ratio");
        check(book.text == "Before.\n\n[Illustration: Moon]\n\nAfter.", "Layout changed persisted text offsets");
        for (auto anchor = book.illustrations[0].offset; anchor < book.illustrations[0].end; ++anchor)
            check(reader::block_at_offset(layout.blocks, anchor) == 1, "Legacy marker offset did not restore image");
        check(reader::block_at_offset(layout.blocks, book.text.find("After")) == 2, "Text after image lost its offset");

        book = illustrated(300, 900);
        layout = reader::layout_book(book, 200, 200, 20, columns);
        verify_pages(layout, 200);
        check(layout.pages == std::vector<std::size_t>({0, 1, 2}), "Large image did not get a dedicated page");
        const auto& image = layout.blocks[1];
        check(image.standalone && image.height == 200 && image.image_height == 188 &&
              image.image_width >= 62 && image.image_width <= 63, "Portrait fit or aspect ratio");
        check(reader::previous_page_start(layout.pages, 2) == 1 && reader::previous_page_start(layout.pages, 1) == 0,
              "Reverse page navigation skipped illustration");
        check(reader::previous_page_start(layout.pages, 0) == 0, "Previous page underflow");

        // Reflow and bookmarks retain the same canonical text anchor.
        const auto anchor = layout.blocks[1].offset;
        auto reflow = reader::layout_book(book, 100, 200, 32, columns);
        verify_pages(reflow, 200);
        check(reflow.blocks[reader::block_at_offset(reflow.blocks, anchor)].illustration == 0, "Image anchor lost on reflow");
        const auto after = book.text.find("After");
        check(reflow.blocks[reader::block_at_offset(reflow.blocks, after)].text == "After.", "Text anchor lost on reflow");
        check(reader::block_at_offset(reflow.blocks, book.text.size() + 99) == reflow.blocks.size() - 1, "Stale save not clamped");

        // Revealing controls changes image sizing and page boundaries, preserving anchors.
        const auto expanded = reader::ui::reading_area(true), controls = reader::ui::reading_area(false);
        check(controls.top>=72 && controls.top+controls.height<454,
              "Reading text overlaps the title or progress bar");
        check(reader::ui::image_top>=132 && reader::ui::image_top+reader::ui::image_height<reader::ui::toolbar_top,
              "Image viewer overlaps its heading or controller legend");
        check(expanded.height > controls.height && expanded.top + expanded.height < 510,
              "Immersive body overlaps progress/footer");
        auto expanded_layout = reader::layout_book(book,864,expanded.height,35,columns);
        auto controls_layout = reader::layout_book(book,864,controls.height,35,columns);
        verify_pages(expanded_layout,expanded.height); verify_pages(controls_layout,controls.height);
        for (const auto offset : {anchor,after}) {
            const auto expanded_at = reader::block_at_offset(expanded_layout.blocks,offset);
            const auto controls_at = reader::block_at_offset(controls_layout.blocks,offset);
            check(expanded_layout.blocks[expanded_at].offset == controls_layout.blocks[controls_at].offset,
                  "Control visibility changed a saved text or image anchor");
        }
        check(expanded_layout.blocks[1].image_height > controls_layout.blocks[1].image_height,
              "Immersive illustration did not use the extra height");

        book = illustrated(0, 0);
        book.illustrations[0].error = "Missing image";
        layout = reader::layout_book(book, 200, 200, 20, columns);
        verify_pages(layout, 200);
        check(layout.blocks.size() == 3 && layout.blocks[1].height == 40 && layout.blocks[1].image_width == 0 &&
              layout.blocks[2].text == "After.", "Unavailable image prevented text flow");

        // Consecutive image-only sections should have no whitespace-only pages.
        book = {};
        book.text = "[Illustration]\n\n[Illustration]\n\n";
        book.illustrations.push_back({0, "One", "one.png", 14, 200, 500});
        book.illustrations.push_back({16, "Two", "two.png", 30, 200, 500});
        layout = reader::layout_book(book, 200, 200, 20, columns);
        verify_pages(layout, 200);
        check(layout.blocks.size() == 2 && layout.pages.size() == 2, "Image-only chapter contains blank or duplicate pages");

        book = {};
        book.text = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\n\nLast paragraph.";
        layout = reader::layout_book(book, 2, 100, 20, columns);
        verify_pages(layout, 100);
        const auto wrapped = reader::wrap_text(book.text, 2, columns);
        check(layout.blocks.size() == wrapped.size(), "Text-only layout changed wrapping");
        for (std::size_t i = 0; i < wrapped.size(); ++i)
            check(layout.blocks[i].offset == wrapped[i].offset && layout.blocks[i].text == wrapped[i].text,
                  "Text-only layout split UTF-8 or changed anchors");
        for (std::size_t at = 0; at < layout.blocks.size(); ++at)
            check(reader::layout_page_end(layout.blocks, at, 100) > at, "Scrolling to block prevents forward progress");

        book = illustrated(160, 60);
        book.illustrations[0].end = book.text.size() + 1;
        bool rejected = false;
        try { reader::layout_book(book, 200, 200, 20, columns); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Invalid illustration range accepted");
        std::cout << "Inline layout tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
