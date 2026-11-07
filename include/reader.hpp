#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace reader {

constexpr std::size_t max_book_bytes = 8 * 1024 * 1024;

struct Book {
    std::string title;
    std::string text; // Normalized UTF-8; positions always refer to these bytes.
};

struct Line {
    std::size_t offset;
    std::string text;
};

struct Settings {
    int font = 1;    // Small, medium, large.
    int spacing = 1; // Compact, normal, relaxed.
    int theme = 0;   // Night, paper, sepia.
};

struct Progress {
    std::size_t offset = 0;
    bool has_bookmark = false;
    std::size_t bookmark = 0;
};

using Measure = std::function<int(const std::string&)>;

bool is_text_book(const std::string& name);
std::string title_from_filename(const std::string& name);
bool load_book(const std::string& path, Book& book, std::string& error);
std::vector<Line> wrap_text(const std::string& text, int width, const Measure& measure);
std::size_t line_at_offset(const std::vector<Line>& lines, std::size_t offset);
std::string progress_path(const std::string& directory, const std::string& filename);
Progress load_progress(const std::string& path);
bool save_progress(const std::string& path, const Progress& progress);
Settings load_settings(const std::string& path);
bool save_settings(const std::string& path, const Settings& settings);

} // namespace reader
