#include "reader.hpp"
#include "performance.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace reader {
namespace {

std::size_t next_character(const std::string& text, std::size_t at) {
    ++at;
    while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80)
        ++at;
    return at;
}

} // namespace

bool valid_utf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) continue;
        unsigned count = 0;
        std::uint32_t value = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 1; value = first & 0x1f; minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2; value = first & 0x0f; minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3; value = first & 0x07; minimum = 0x10000;
        } else return false;
        if (count > text.size() - i) return false;
        while (count--) {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0) != 0x80) return false;
            value = (value << 6) | (byte & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}

namespace {

bool atomic_write(const std::string& path, const std::string& data) {
    const auto temporary = path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        if (!output) { std::remove(temporary.c_str()); return false; }
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

int bounded(int value) { return std::max(0, std::min(2, value)); }

} // namespace

bool is_text_book(const std::string& name) {
    const auto slash = name.find_last_of("/\\");
    const auto start = slash == std::string::npos ? 0 : slash + 1;
    if (name.size() - start < 4 || name[start] == '.') return false;
    auto extension = name.substr(name.size() - 4);
    for (auto& ch : extension) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return extension == ".txt";
}

bool is_epub_book(const std::string& name) {
    const auto slash = name.find_last_of("/\\");
    const auto start = slash == std::string::npos ? 0 : slash + 1;
    if (name.size() - start < 5 || name[start] == '.') return false;
    auto extension = name.substr(name.size() - 5);
    for (auto& ch : extension) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return extension == ".epub";
}

bool is_supported_book(const std::string& name) {
    return is_text_book(name) || is_epub_book(name) || is_online_book(name);
}
bool is_online_book(const std::string& name) {
    return name.size() > 4 && name.compare(name.size() - 4, 4, ".nvr") == 0;
}

std::string title_from_filename(const std::string& name) {
    const auto slash = name.find_last_of("/\\");
    auto title = name.substr(slash == std::string::npos ? 0 : slash + 1);
    if (is_text_book(title)) title.resize(title.size() - 4);
    else if (is_epub_book(title)) title.resize(title.size() - 5);
    std::replace(title.begin(), title.end(), '_', ' ');
    return title.empty() ? "Untitled" : title;
}

bool load_book(const std::string& path, Book& book, std::string& error) {
    const perf::Scope timing(perf::Event::TextRead);
    error.clear();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { error = "Cannot open this book."; return false; }
    const auto size = input.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(max_book_bytes)) {
        error = "Book exceeds the 8 MiB limit."; return false;
    }
    std::string raw(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    if (!raw.empty() && !input.read(&raw[0], size)) {
        error = "Could not read the complete book."; return false;
    }
    if (raw.compare(0, 3, "\xef\xbb\xbf") == 0) raw.erase(0, 3);
    if (!valid_utf8(raw) || raw.find('\0') != std::string::npos) {
        error = "Save this book as UTF-8 text first."; return false;
    }
    std::string normalized;
    normalized.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const auto ch = static_cast<unsigned char>(raw[i]);
        if (ch == '\r') {
            normalized += '\n';
            if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
        } else if (ch == '\t') normalized += "    ";
        else if (ch == '\n' || ch >= 0x20) normalized += static_cast<char>(ch);
        if (normalized.size() > max_book_bytes) {
            error = "Expanded text exceeds the 8 MiB limit."; return false;
        }
    }
    if (normalized.find_first_not_of(" \n") == std::string::npos) {
        error = "This book contains no readable text."; return false;
    }
    book = {title_from_filename(path), std::move(normalized), {}, {}};
    return true;
}

std::vector<Line> wrap_text(const std::string& text, int width, const Measure& measure) {
    std::vector<Line> lines;
    width = std::max(1, width);
    std::size_t start = 0;
    while (start < text.size()) {
        if (lines.size() >= 100000) throw std::length_error("Too many display lines");
        if (text[start] == '\n') { lines.push_back({start++, ""}); continue; }
        std::size_t end = start, space = std::string::npos;
        while (end < text.size() && text[end] != '\n') {
            const auto next = next_character(text, end);
            if (measure(text.substr(start, next - start)) > width) {
                if (end == start) end = next; // Always advance, even for a wide glyph.
                else if (space != std::string::npos && space > start) end = space;
                break;
            }
            if (text[end] == ' ') space = end;
            end = next;
        }
        auto visible_end = end;
        while (visible_end > start && text[visible_end - 1] == ' ') --visible_end;
        lines.push_back({start, text.substr(start, visible_end - start)});
        start = end;
        while (start < text.size() && text[start] == ' ') ++start;
        if (start < text.size() && text[start] == '\n') ++start;
    }
    if (lines.empty()) lines.push_back({0, ""});
    return lines;
}

std::size_t line_at_offset(const std::vector<Line>& lines, std::size_t offset) {
    const auto found = std::upper_bound(lines.begin(), lines.end(), offset,
        [](std::size_t position, const Line& line) { return position < line.offset; });
    return found == lines.begin() ? 0 : static_cast<std::size_t>(found - lines.begin() - 1);
}

std::string progress_path(const std::string& directory, const std::string& filename) {
    // Stable across launches and toolchains; avoids book names becoming save paths.
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned char ch : filename) { hash ^= ch; hash *= UINT64_C(1099511628211); }
    std::ostringstream result;
    result << directory << '/' << std::hex << std::setw(16) << std::setfill('0') << hash << ".pos";
    return result.str();
}

Progress load_progress(const std::string& path) {
    std::ifstream input(path);
    std::string magic;
    std::uint64_t offset = 0, bookmark = 0;
    std::uint64_t chapter = 0, bookmark_chapter = 0;
    int has_bookmark = 0;
    if (!(input >> magic >> offset >> has_bookmark >> bookmark) ||
        (magic != "NRP1" && magic != "NRP2" && magic != "NRP3" && magic != "NRP4") ||
        offset > max_book_bytes * 4 || bookmark > max_book_bytes * 4 ||
        (has_bookmark != 0 && has_bookmark != 1)) return {};
    if (magic != "NRP1" && (!(input >> chapter >> bookmark_chapter) ||
        chapter >= 20000 || bookmark_chapter >= 20000)) return {};
    int percent = -1;
    std::uint64_t last_read = 0;
    if ((magic == "NRP3" || magic == "NRP4") && (!(input >> percent >> last_read) || percent < -1 || percent > 100 ||
        last_read > UINT64_C(32503680000))) return {};
    Progress result{static_cast<std::size_t>(offset), has_bookmark == 1, static_cast<std::size_t>(bookmark),
            static_cast<std::size_t>(chapter), static_cast<std::size_t>(bookmark_chapter), percent, last_read};
    if (magic == "NRP4" && (!(input >> std::quoted(result.chapter_id) >> std::quoted(result.bookmark_id)) || result.chapter_id.size() > 1024 || result.bookmark_id.size() > 1024)) return {};
    return result;
}

bool save_progress(const std::string& path, const Progress& progress) {
    const perf::Scope timing(perf::Event::ProgressWrite);
    std::ostringstream output;
    output << "NRP4\n" << progress.offset << ' ' << progress.has_bookmark << ' ' << progress.bookmark
           << ' ' << progress.chapter << ' ' << progress.bookmark_chapter << ' ' << progress.percent
           << ' ' << progress.last_read << '\n' << std::quoted(progress.chapter_id) << ' ' << std::quoted(progress.bookmark_id) << '\n';
    return atomic_write(path, output.str());
}

Settings load_settings(const std::string& path) {
    std::ifstream input(path);
    std::string magic;
    Settings result;
    if (!(input >> magic >> result.font >> result.spacing >> result.theme) ||
        (magic != "NRS1" && magic != "NRS2" && magic != "NRS3" && magic != "NRS4")) return {};
    if (magic != "NRS1" && !(input >> result.library_sort >> result.library_filter)) return {};
    if ((magic == "NRS3" || magic == "NRS4") && !(input >> result.reading_font)) return {};
    if (magic == "NRS4" && !(input >> result.immersive)) return {};
    if (magic != "NRS4") result.immersive = 1; // Preserve the view used by older installations.
    int speech_rate = 175;
    if (magic == "NRS4" && (input >> speech_rate)) result.speech_rate = std::max(100, std::min(350, speech_rate));
    int page_effect;
    if (magic == "NRS4" && (input >> page_effect)) result.page_effect = bounded(page_effect);
    result.font = bounded(result.font);
    result.spacing = bounded(result.spacing);
    result.theme = std::max(0,std::min(page_theme_count-1,result.theme));
    result.library_sort = bounded(result.library_sort);
    result.library_filter = bounded(result.library_filter);
    result.reading_font = bounded(result.reading_font);
    result.immersive = std::max(0, std::min(1, result.immersive));
    return result;
}

bool save_settings(const std::string& path, const Settings& settings) {
    const perf::Scope timing(perf::Event::SettingsWrite);
    std::ostringstream output;
    output << "NRS4\n" << bounded(settings.font) << ' ' << bounded(settings.spacing) << ' '
           << std::max(0,std::min(page_theme_count-1,settings.theme)) << ' ' << bounded(settings.library_sort) << ' '
           << bounded(settings.library_filter) << ' ' << bounded(settings.reading_font) << ' '
           << std::max(0, std::min(1, settings.immersive)) << ' '
           << std::max(100, std::min(350, settings.speech_rate)) << ' '
           << bounded(settings.page_effect) << '\n';
    return atomic_write(path, output.str());
}

} // namespace reader
