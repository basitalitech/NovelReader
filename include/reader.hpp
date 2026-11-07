#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace reader {

constexpr std::size_t max_book_bytes = 8 * 1024 * 1024;
constexpr std::size_t max_chapters = 4096;
constexpr std::size_t max_chapter_bytes = 2 * 1024 * 1024;
constexpr std::size_t max_image_bytes = 8 * 1024 * 1024;

enum TextStyle : unsigned { Plain = 0, Emphasis = 1, Strong = 2, Heading = 4 };
struct StyleSpan { std::size_t begin, end; unsigned style; };
struct Illustration {
    std::size_t offset;
    std::string label, path;
    std::size_t end = 0; // End of the compatibility marker in normalized text.
    unsigned width = 0, height = 0; // Probed once per chapter; zero means unavailable.
    std::string error = {};
};

struct Book {
    std::string title;
    std::string text; // Normalized UTF-8; positions always refer to these bytes.
    std::vector<StyleSpan> styles;
    std::vector<Illustration> illustrations;
};

struct Chapter { std::string title, path; };
struct Publication {
    std::string title, author, path;
    bool epub = false;
    std::vector<Chapter> chapters; // OPF spine order; chapter contents load on demand.
    std::string cover_path;
    bool online = false;
};

struct Line {
    std::size_t offset;
    std::string text;
};

constexpr std::size_t no_illustration = static_cast<std::size_t>(-1);
struct LayoutBlock {
    std::size_t offset;
    std::string text;
    int height;
    std::size_t illustration = no_illustration;
    int image_width = 0, image_height = 0;
    bool standalone = false;
};
struct ChapterLayout {
    std::vector<LayoutBlock> blocks;
    std::vector<std::size_t> pages;
};

struct Settings {
    int font = 1;    // Small, medium, large.
    int spacing = 1; // Compact, normal, relaxed.
    int theme = 0;   // Night, paper, sepia.
    int library_sort = 0;
    int library_filter = 0;
    int reading_font = 0; // Literata, Atkinson Hyperlegible Next, Vita system.
    int immersive = 1; // Saved reading view; control panels do not change pagination.
};

struct Progress {
    std::size_t offset = 0;
    bool has_bookmark = false;
    std::size_t bookmark = 0;
    std::size_t chapter = 0;
    std::size_t bookmark_chapter = 0;
    int percent = -1; // Unknown for legacy saves; EPUB estimate weights chapters equally.
    std::uint64_t last_read = 0; // Unix time, updated by the frontend on reading actions.
    std::string chapter_id = {}, bookmark_id = {}; // Stable paths for downloaded chapters.
};

enum class LibrarySort { Title, Recent, Progress };
enum class LibraryFilter { All, Reading, Finished };
struct LibraryItem {
    std::string filename, title, path, author, cover_path, error; // filename is the library-relative save identity.
    std::size_t chapters = 1;
    Progress progress;
    bool guide = false;
    bool directory = false;
};
bool valid_library_component(const std::string& name);
bool load_library(const std::string& path, std::vector<std::string>& books);
bool save_library(const std::string& path, const std::vector<std::string>& books);
std::string child_library_folder(const std::string& folder, const std::string& name);
std::string parent_library_folder(const std::string& folder);
std::vector<std::size_t> library_order(const std::vector<LibraryItem>& items, LibrarySort sort, LibraryFilter filter);
bool reading_started(const Progress& progress);
int reading_percent(std::size_t chapter, std::size_t chapters, std::size_t offset,
                    std::size_t text_size, bool final_page);

enum class ImageFormat { Png, Jpeg };
struct ImageInfo { ImageFormat format = ImageFormat::Png; unsigned width = 0, height = 0; };
// Validates headers/dimensions before decoding; the image decoder still validates the full stream.
bool inspect_image(const std::string& bytes, ImageInfo& image, std::string& error);
bool load_epub_image(const std::string& archive_path, const std::string& image_path,
                     std::string& bytes, std::string& error);

using Measure = std::function<int(const std::string&)>;

ChapterLayout layout_book(const Book& book, int width, int page_height, int line_height, const Measure& measure);
std::size_t layout_page_end(const std::vector<LayoutBlock>& blocks, std::size_t start, int page_height);
std::size_t block_at_offset(const std::vector<LayoutBlock>& blocks, std::size_t offset);
std::size_t previous_page_start(const std::vector<std::size_t>& pages, std::size_t start);

bool is_text_book(const std::string& name);
bool is_epub_book(const std::string& name);
bool is_online_book(const std::string& name);
bool is_supported_book(const std::string& name);
bool valid_utf8(const std::string& text);
bool open_publication(const std::string& path, Publication& publication, std::string& error);
bool load_chapter(const Publication& publication, std::size_t index, Book& chapter, std::string& error);
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
