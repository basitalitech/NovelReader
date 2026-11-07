#include "reader.hpp"
#include <algorithm>
#include <cctype>

namespace reader {
bool reading_started(const Progress& progress) {
    return progress.last_read > 0 || progress.percent >= 0 || progress.offset > 0 || progress.chapter > 0;
}

int reading_percent(std::size_t chapter, std::size_t chapters, std::size_t offset,
                    std::size_t text_size, bool final_page) {
    if (!chapters) return 0;
    chapter = std::min(chapter, chapters - 1);
    if (final_page && chapter + 1 == chapters) return 100;
    const auto fraction = final_page ? 1.0 : text_size ? static_cast<double>(std::min(offset, text_size)) / text_size : 0.0;
    return std::min(99, static_cast<int>(100.0 * (chapter + fraction) / chapters));
}

std::vector<std::size_t> library_order(const std::vector<LibraryItem>& items, LibrarySort sort, LibraryFilter filter) {
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (filter == LibraryFilter::Reading && (items[i].guide || !reading_started(items[i].progress) || items[i].progress.percent == 100)) continue;
        if (filter == LibraryFilter::Finished && (items[i].guide || items[i].progress.percent != 100)) continue;
        order.push_back(i);
    }
    const auto folded = [](std::string value) {
        for (auto& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return value;
    };
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        const auto& a = items[left]; const auto& b = items[right];
        if (a.guide != b.guide) return !a.guide;
        if (sort == LibrarySort::Recent && a.progress.last_read != b.progress.last_read)
            return a.progress.last_read > b.progress.last_read;
        if (sort == LibrarySort::Progress && a.progress.percent != b.progress.percent)
            return a.progress.percent > b.progress.percent;
        const auto title_a = folded(a.title), title_b = folded(b.title);
        return title_a == title_b ? a.filename < b.filename : title_a < title_b;
    });
    return order;
}
} // namespace reader
