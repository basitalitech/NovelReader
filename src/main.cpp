#include "reader.hpp"
#include "vita_images.hpp"

#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

extern "C" {
unsigned int _newlib_heap_size_user = 64 * 1024 * 1024;
}

namespace {
const std::string root = "ux0:data/NovelReader";
const std::string books_directory = root + "/books";
const std::string progress_directory = root + "/progress";
const std::string settings_path = root + "/settings.txt";

struct Palette { unsigned background, panel, text, muted, accent; };
constexpr unsigned rgb(unsigned r, unsigned g, unsigned b) {
    return r | (g << 8) | (b << 16) | 0xff000000u;
}
const Palette palettes[] = {
    {rgb(19, 23, 30), rgb(30, 37, 47), rgb(227, 230, 233),
     rgb(156, 167, 180), rgb(113, 207, 182)},
    {rgb(245, 245, 241), rgb(226, 232, 229), rgb(30, 40, 43),
     rgb(84, 97, 100), rgb(22, 109, 87)},
    {rgb(239, 226, 199), rgb(224, 207, 174), rgb(64, 49, 33),
     rgb(113, 90, 61), rgb(115, 66, 36)}
};

enum class Screen { Library, Reader, Settings, Contents, Illustrations, Image };
struct TextureDeleter { void operator()(vita2d_texture* texture) const { vita2d_free_texture(texture); } };
struct InlineTexture {
    std::size_t illustration;
    std::unique_ptr<vita2d_texture, TextureDeleter> texture;
    std::string error;
};

class App {
public:
    explicit App(vita2d_pgf* font) : font_(font), settings_(reader::load_settings(settings_path)) {
        sort_ = static_cast<reader::LibrarySort>(settings_.library_sort);
        filter_ = static_cast<reader::LibraryFilter>(settings_.library_filter);
        scan();
    }
    ~App() { release_image(); }

    void draw() {
        if (screen_ == Screen::Reader) sync_inline_images();
        const auto& color = palette();
        vita2d_set_clear_color(color.background);
        vita2d_start_drawing();
        vita2d_clear_screen();
        if (screen_ == Screen::Library) draw_library();
        else if (screen_ == Screen::Reader) draw_reader();
        else if (screen_ == Screen::Contents) draw_contents();
        else if (screen_ == Screen::Illustrations) draw_illustrations();
        else if (screen_ == Screen::Image) draw_image();
        else draw_settings();
        vita2d_end_drawing();
        vita2d_swap_buffers();
        vita2d_wait_rendering_done();
    }

    bool input(unsigned pressed) {
        if (pressed & SCE_CTRL_START) {
            if (screen_ == Screen::Settings) reader::save_settings(settings_path, settings_);
            save(); return false;
        }
        if (screen_ == Screen::Library) {
            if ((pressed & SCE_CTRL_UP) && selected_ > 0) --selected_;
            if ((pressed & SCE_CTRL_DOWN) && selected_ + 1 < order_.size()) ++selected_;
            if (pressed & SCE_CTRL_SQUARE) {
                const auto keep = selected_filename();
                sort_ = static_cast<reader::LibrarySort>((static_cast<int>(sort_) + 1) % 3);
                refresh_library(keep);
                save_library_preferences();
            }
            if (pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) {
                const auto step = pressed & SCE_CTRL_RTRIGGER ? 1 : 2;
                filter_ = static_cast<reader::LibraryFilter>((static_cast<int>(filter_) + step) % 3);
                selected_ = 0; refresh_library("");
                save_library_preferences();
            }
            if (pressed & SCE_CTRL_TRIANGLE) scan();
            update_cover();
            if ((pressed & SCE_CTRL_CROSS) && !order_.empty()) open();
        } else if (screen_ == Screen::Reader) {
            if (pressed & SCE_CTRL_CIRCLE) {
                save(); screen_ = Screen::Library;
                inline_images_.clear();
                book_ = {};
                publication_ = {};
                std::vector<reader::LayoutBlock>().swap(lines_);
                pages_.clear();
                refresh_library(active_filename_);
                active_filename_.clear();
                update_cover();
            }
            else if (pressed & SCE_CTRL_TRIANGLE) {
                setting_row_ = 0; previous_settings_ = settings_; screen_ = Screen::Settings;
            }
            else if (pressed & SCE_CTRL_CROSS) {
                contents_selected_ = chapter_; screen_ = Screen::Contents; message_.clear();
            }
            else {
                if (publication_.epub && (pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER))) {
                    if ((pressed & SCE_CTRL_RTRIGGER) && chapter_ + 1 < publication_.chapters.size()) {
                        if (change_chapter(chapter_ + 1, 0)) save();
                    } else if ((pressed & SCE_CTRL_LTRIGGER) && chapter_ > 0) {
                        if (change_chapter(chapter_ - 1, 0)) save();
                    }
                    return true;
                }
                const auto previous = top_;
                const auto previous_chapter = chapter_;
                if (pressed & (SCE_CTRL_RIGHT | SCE_CTRL_RTRIGGER)) {
                    if (visible_end() >= lines_.size() && chapter_ + 1 < publication_.chapters.size())
                        change_chapter(chapter_ + 1, 0);
                    else if (visible_end() < lines_.size()) top_ = visible_end();
                }
                if (pressed & (SCE_CTRL_LEFT | SCE_CTRL_LTRIGGER)) {
                    if (top_ == 0 && chapter_ > 0) change_chapter(chapter_ - 1, 0, true);
                    else top_ = reader::previous_page_start(pages_, top_);
                }
                if ((pressed & SCE_CTRL_UP) && top_ > 0) --top_;
                if ((pressed & SCE_CTRL_DOWN) && top_ + 1 < lines_.size()) ++top_;
                bool changed = previous != top_ || previous_chapter != chapter_;
                if (changed) message_.clear();
                if (pressed & SCE_CTRL_SQUARE) {
                    progress_.has_bookmark = true;
                    progress_.bookmark = lines_[top_].offset;
                    progress_.bookmark_chapter = chapter_;
                    message_ = "Bookmark saved. SELECT returns here.";
                    changed = true;
                }
                if (pressed & SCE_CTRL_SELECT) {
                    if (progress_.has_bookmark) {
                        if (progress_.bookmark_chapter == chapter_) {
                            top_ = reader::block_at_offset(lines_, progress_.bookmark); changed = true;
                        } else changed = change_chapter(progress_.bookmark_chapter, progress_.bookmark) || changed;
                    } else message_ = "Press SQUARE to save a bookmark.";
                }
                if (changed) save();
            }
        } else if (screen_ == Screen::Contents) {
            if ((pressed & SCE_CTRL_UP) && contents_selected_ > 0) --contents_selected_;
            if ((pressed & SCE_CTRL_DOWN) && contents_selected_ + 1 < publication_.chapters.size()) ++contents_selected_;
            if (pressed & SCE_CTRL_LEFT) contents_selected_ = contents_selected_ > 6 ? contents_selected_ - 6 : 0;
            if (pressed & SCE_CTRL_RIGHT) contents_selected_ = std::min(contents_selected_ + 6, publication_.chapters.size() - 1);
            if (pressed & SCE_CTRL_CIRCLE) { screen_ = Screen::Reader; message_.clear(); }
            if (pressed & SCE_CTRL_SQUARE) {
                illustration_selected_ = 0;
                // Start near the currently visible illustration, if any.
                for (std::size_t i = 0; i < book_.illustrations.size(); ++i)
                    if (book_.illustrations[i].offset <= lines_[top_].offset) illustration_selected_ = i;
                screen_ = Screen::Illustrations; message_.clear();
            }
            if (pressed & SCE_CTRL_CROSS) {
                if (contents_selected_ == chapter_ || change_chapter(contents_selected_, 0)) {
                    screen_ = Screen::Reader; save();
                }
            }
        } else if (screen_ == Screen::Illustrations) {
            if ((pressed & SCE_CTRL_UP) && illustration_selected_ > 0) --illustration_selected_;
            if ((pressed & SCE_CTRL_DOWN) && illustration_selected_ + 1 < book_.illustrations.size()) ++illustration_selected_;
            if (pressed & SCE_CTRL_CIRCLE) { screen_ = Screen::Contents; message_.clear(); }
            if ((pressed & SCE_CTRL_CROSS) && !book_.illustrations.empty()) show_illustration(illustration_selected_);
        } else if (screen_ == Screen::Image) {
            if (pressed & SCE_CTRL_CIRCLE) { release_image(); screen_ = Screen::Illustrations; message_.clear(); }
            else {
                if ((pressed & SCE_CTRL_LTRIGGER) && illustration_selected_ > 0) show_illustration(illustration_selected_ - 1);
                if ((pressed & SCE_CTRL_RTRIGGER) && illustration_selected_ + 1 < book_.illustrations.size())
                    show_illustration(illustration_selected_ + 1);
                if (pressed & SCE_CTRL_CROSS) zoom_ = std::min(4.0f, zoom_ * 1.25f);
                if (pressed & SCE_CTRL_SQUARE) zoom_ = std::max(1.0f, zoom_ / 1.25f);
                if (pressed & SCE_CTRL_TRIANGLE) { zoom_ = 1.0f; pan_x_ = pan_y_ = 0; }
                if (pressed & SCE_CTRL_LEFT) pan_x_ += 40;
                if (pressed & SCE_CTRL_RIGHT) pan_x_ -= 40;
                if (pressed & SCE_CTRL_UP) pan_y_ += 40;
                if (pressed & SCE_CTRL_DOWN) pan_y_ -= 40;
                clamp_pan();
            }
        } else {
            if ((pressed & SCE_CTRL_UP) && setting_row_ > 0) --setting_row_;
            if ((pressed & SCE_CTRL_DOWN) && setting_row_ < 2) ++setting_row_;
            int* value = setting_row_ == 0 ? &settings_.font :
                         setting_row_ == 1 ? &settings_.spacing : &settings_.theme;
            if (pressed & SCE_CTRL_LEFT) *value = std::max(0, *value - 1);
            if (pressed & SCE_CTRL_RIGHT) *value = std::min(2, *value + 1);
            if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE | SCE_CTRL_CROSS)) {
                const auto offset = lines_[top_].offset;
                try { reflow(offset); }
                catch (const std::exception&) {
                    settings_ = previous_settings_;
                    message_ = "Layout too large. Previous settings restored.";
                }
                screen_ = Screen::Reader;
                if (!reader::save_settings(settings_path, settings_))
                    message_ = "Could not save settings. Check free space.";
                save();
            }
        }
        return true;
    }

private:
    void probe_illustrations(reader::Book& book) {
        for (auto& illustration : book.illustrations) {
            std::string bytes;
            reader::ImageInfo info;
            if (reader::load_epub_image(publication_.path, illustration.path, bytes, illustration.error) &&
                reader::inspect_image(bytes, info, illustration.error)) {
                illustration.width = info.width; illustration.height = info.height;
            }
        }
    }

    std::size_t visible_end() const { return reader::layout_page_end(lines_, top_, 350); }

    void sync_inline_images() {
        std::vector<std::size_t> visible;
        const auto end = visible_end();
        for (auto at = top_; at < end; ++at)
            if (lines_[at].illustration != reader::no_illustration) visible.push_back(lines_[at].illustration);
        inline_images_.erase(std::remove_if(inline_images_.begin(), inline_images_.end(), [&](const InlineTexture& cached) {
            return std::find(visible.begin(), visible.end(), cached.illustration) == visible.end();
        }), inline_images_.end());
        for (auto at = top_; at < end; ++at) {
            const auto& block = lines_[at];
            if (block.illustration == reader::no_illustration) continue;
            const auto found = std::find_if(inline_images_.begin(), inline_images_.end(), [&](const InlineTexture& cached) {
                return cached.illustration == block.illustration;
            });
            if (found != inline_images_.end()) continue;
            InlineTexture cached{block.illustration, {}, book_.illustrations[block.illustration].error};
            if (cached.error.empty()) {
                try {
                    std::string bytes;
                    if (reader::load_epub_image(publication_.path, book_.illustrations[block.illustration].path, bytes, cached.error))
                        cached.texture.reset(decode_thumbnail(bytes, block.image_width, block.image_height, cached.error));
                } catch (const std::exception&) { cached.error = "Not enough memory for this illustration."; }
            }
            inline_images_.push_back(std::move(cached));
        }
    }

    void draw_inline_image(const reader::LayoutBlock& block, int y) const {
        const auto cached = std::find_if(inline_images_.begin(), inline_images_.end(), [&](const InlineTexture& item) {
            return item.illustration == block.illustration;
        });
        if (cached != inline_images_.end() && cached->texture) {
            auto* texture = cached->texture.get();
            const auto width = vita2d_texture_get_width(texture), height = vita2d_texture_get_height(texture);
            const auto scale = std::min(static_cast<float>(block.image_width) / width,
                                        static_cast<float>(block.image_height) / height);
            vita2d_draw_texture_scale(texture, 480 - width * scale / 2,
                                     y + (block.height - height * scale) / 2, scale, scale);
        } else {
            vita2d_draw_rectangle(48, y + 2, 864, block.height - 4, palette().panel);
            text(60, y + 24, palette().text, 0.75f, book_.illustrations[block.illustration].label, 840);
            text(60, y + 47, palette().muted, 0.65f,
                 cached == inline_images_.end() || cached->error.empty() ? "Illustration unavailable" : cached->error, 840);
        }
    }

    void save_library_preferences() {
        settings_.library_sort = static_cast<int>(sort_);
        settings_.library_filter = static_cast<int>(filter_);
        if (!reader::save_settings(settings_path, settings_)) message_ = "Could not save library preferences.";
    }
    void release_image() {
        if (image_) { vita2d_wait_rendering_done(); vita2d_free_texture(image_); image_ = nullptr; }
        cover_key_.clear();
    }
    std::string selected_filename() const {
        return order_.empty() ? "" : entries_[order_[selected_]].filename;
    }
    void refresh_library(const std::string& keep) {
        order_ = reader::library_order(entries_, sort_, filter_);
        selected_ = order_.empty() ? 0 : std::min(selected_, order_.size() - 1);
        for (std::size_t i = 0; i < order_.size(); ++i)
            if (entries_[order_[i]].filename == keep) { selected_ = i; break; }
        release_image();
    }

    void update_cover() {
        if (order_.empty()) { release_image(); cover_message_.clear(); return; }
        const auto& entry = entries_[order_[selected_]];
        if (cover_key_ == entry.filename) return;
        release_image();
        cover_key_ = entry.filename;
        cover_message_.clear();
        if (entry.cover_path.empty()) return;
        try {
            std::string bytes;
            if (reader::load_epub_image(entry.path, entry.cover_path, bytes, cover_message_))
                image_ = decode_texture(bytes, cover_message_);
        } catch (const std::exception&) { cover_message_ = "Cover could not be loaded."; }
    }

    float fit_scale(float width, float height) const {
        if (!image_) return 1;
        return std::min(width / vita2d_texture_get_width(image_), height / vita2d_texture_get_height(image_));
    }
    void clamp_pan() {
        if (!image_) return;
        const auto scale = fit_scale(864, 350) * zoom_;
        const auto limit_x = std::max(0.0f, (vita2d_texture_get_width(image_) * scale - 864) / 2);
        const auto limit_y = std::max(0.0f, (vita2d_texture_get_height(image_) * scale - 350) / 2);
        pan_x_ = std::max(-limit_x, std::min(limit_x, pan_x_));
        pan_y_ = std::max(-limit_y, std::min(limit_y, pan_y_));
    }

    void show_illustration(std::size_t index) {
        message_ = "Opening illustration..."; draw();
        try {
            std::string bytes, error;
            if (!reader::load_epub_image(publication_.path, book_.illustrations[index].path, bytes, error)) {
                message_ = error; return;
            }
            auto* candidate = decode_texture(bytes, error);
            if (!candidate) { message_ = error; return; }
            release_image(); image_ = candidate;
            illustration_selected_ = index; zoom_ = 1; pan_x_ = pan_y_ = 0;
            screen_ = Screen::Image; message_.clear();
        } catch (const std::exception&) { message_ = "Not enough memory for this illustration."; }
    }
    const Palette& palette() const { return palettes[settings_.theme]; }
    float font_scale() const { return 1.0f + settings_.font * 0.2f; }
    int line_height() const { return 26 + settings_.font * 5 + settings_.spacing * 4; }

    std::string fit(std::string text, float scale, int width) const {
        if (vita2d_pgf_text_width(font_, scale, text.c_str()) <= width) return text;
        while (!text.empty() && vita2d_pgf_text_width(font_, scale, (text + "...").c_str()) > width) {
            auto last = text.size() - 1;
            while (last > 0 && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80) --last;
            text.resize(last);
        }
        return text + "...";
    }

    void text(int x, int y, unsigned color, float scale, const std::string& value, int width = 864) const {
        const auto clipped = fit(value, scale, width);
        vita2d_pgf_draw_text(font_, x, y, color, scale, clipped.c_str());
    }

    void heading(const std::string& title, const std::string& subtitle) const {
        text(48, 43, palette().text, 1.2f, title);
        text(48, 70, palette().muted, 0.75f, subtitle);
        vita2d_draw_rectangle(48, 82, 864, 1, palette().panel);
    }

    void scan() {
        const auto keep = selected_filename();
        message_ = "Scanning local books..."; draw();
        release_image();
        entries_.clear();
        order_.clear();
        message_.clear();
        const auto directory = sceIoDopen(books_directory.c_str());
        if (directory >= 0) {
            SceIoDirent entry{};
            int result = 0;
            while ((result = sceIoDread(directory, &entry)) > 0) {
                const std::string name(entry.d_name);
                if (SCE_S_ISREG(entry.d_stat.st_mode) && reader::is_supported_book(name)) {
                    reader::LibraryItem item;
                    item.filename = name; item.title = reader::title_from_filename(name); item.path = books_directory + '/' + name;
                    entries_.push_back(std::move(item));
                }
                entry = {};
            }
            if (result < 0) message_ = "Could not read the entire books folder.";
            sceIoDclose(directory);
        } else message_ = "Cannot open books folder. Check storage and restart.";
        // Bundled guide is always available without modifying the user's library.
        reader::LibraryItem guide;
        guide.filename = "@welcome"; guide.title = "Welcome to Novel Reader"; guide.path = "app0:/welcome.txt"; guide.guide = true;
        entries_.push_back(std::move(guide));
        for (auto& item : entries_) {
            item.progress = reader::load_progress(reader::progress_path(progress_directory, item.filename));
            if (reader::is_epub_book(item.filename)) {
                reader::Publication metadata;
                if (reader::open_publication(item.path, metadata, item.error)) {
                    item.title = metadata.title; item.author = metadata.author;
                    item.cover_path = metadata.cover_path; item.chapters = metadata.chapters.size();
                }
            }
        }
        refresh_library(keep);
        update_cover();
    }

    void open() {
        const auto entry_index = order_[selected_];
        message_ = "Opening book...";
        draw();
        std::string error;
        try {
            if (!reader::open_publication(entries_[entry_index].path, publication_, error)) {
                message_ = error;
                return;
            }
            progress_ = reader::load_progress(reader::progress_path(progress_directory, entries_[entry_index].filename));
            if (progress_.chapter >= publication_.chapters.size()) { progress_.chapter = 0; progress_.offset = 0; }
            if (progress_.bookmark_chapter >= publication_.chapters.size()) progress_.has_bookmark = false;
            if (!change_chapter(progress_.chapter, progress_.offset)) { publication_ = {}; return; }
            active_filename_ = entries_[entry_index].filename;
            entries_[entry_index].title = publication_.title;
        } catch (const std::exception&) {
            book_ = {};
            publication_ = {};
            std::vector<reader::LayoutBlock>().swap(lines_);
            pages_.clear(); inline_images_.clear();
            active_filename_.clear();
            error = "Book layout is too large for this version.";
            message_ = error;
            return;
        }
        message_.clear();
        release_image();
        screen_ = Screen::Reader;
        save();
    }

    bool change_chapter(std::size_t index, std::size_t offset, bool at_end = false) {
        message_ = "Opening chapter...";
        draw();
        try {
            reader::Book candidate;
            std::string error;
            if (!reader::load_chapter(publication_, index, candidate, error)) { message_ = error; return false; }
            probe_illustrations(candidate);
            const auto scale = font_scale();
            auto layout = reader::layout_book(candidate, 864, 350, line_height(), [this, scale](const std::string& value) {
                return vita2d_pgf_text_width(font_, scale, value.c_str());
            });
            const auto position = at_end ? layout.pages.back() : reader::block_at_offset(layout.blocks, offset);
            if (publication_.chapters[index].title == "Chapter " + std::to_string(index + 1))
                publication_.chapters[index].title = candidate.title;
            inline_images_.clear();
            book_ = std::move(candidate); lines_ = std::move(layout.blocks); pages_ = std::move(layout.pages);
            top_ = position; chapter_ = index;
            message_.clear();
            return true;
        } catch (const std::exception&) {
            message_ = "Chapter layout is too large for this version.";
            return false;
        }
    }

    void reflow(std::size_t offset) {
        const auto scale = font_scale();
        auto layout = reader::layout_book(book_, 864, 350, line_height(), [this, scale](const std::string& value) {
            return vita2d_pgf_text_width(font_, scale, value.c_str());
        });
        lines_ = std::move(layout.blocks); pages_ = std::move(layout.pages);
        top_ = reader::block_at_offset(lines_, offset);
        inline_images_.clear();
    }

    void save() {
        if (active_filename_.empty() || lines_.empty()) return;
        progress_.offset = lines_[top_].offset;
        progress_.chapter = chapter_;
        progress_.percent = reader::reading_percent(chapter_, publication_.chapters.size(),
            progress_.offset, book_.text.size(), visible_end() >= lines_.size());
        const auto now = std::time(nullptr);
        if (now > 0) progress_.last_read = static_cast<std::uint64_t>(now);
        if (!reader::save_progress(reader::progress_path(progress_directory, active_filename_), progress_))
            message_ = "Could not save progress. Check free space.";
        else
            for (auto& item : entries_)
                if (item.filename == active_filename_) item.progress = progress_;
    }

    void draw_library() const {
        const char* sorts[] = {"Title", "Recent", "Progress"};
        const char* filters[] = {"All books", "Reading", "Finished"};
        heading("Novel Reader", std::string(filters[static_cast<int>(filter_)]) + "  /  Sort: " + sorts[static_cast<int>(sort_)]);
        const auto first = (selected_ / 6) * 6;
        for (std::size_t i = first; i < order_.size() && i < first + 6; ++i) {
            const auto& item = entries_[order_[i]];
            const auto y = 96 + static_cast<int>(i - first) * 57;
            if (i == selected_) {
                vita2d_draw_rectangle(40, y, 570, 53, palette().panel);
                vita2d_draw_rectangle(40, y, 4, 53, palette().accent);
            }
            text(60, y + 24, palette().text, 0.95f, item.title, 525);
            std::string status = item.guide ? "Guide" : reader::is_epub_book(item.filename) ? "EPUB" : "TXT";
            if (!item.error.empty()) status += "  /  Could not read metadata";
            else if (item.progress.percent == 100) status += "  /  Finished";
            else if (item.progress.percent >= 0) status += "  /  " + std::to_string(item.progress.percent) + "% read";
            else status += reader::reading_started(item.progress) ? "  /  Continue reading" : "  /  New";
            text(60, y + 43, palette().muted, 0.65f, status, 525);
            if (item.progress.percent >= 0) {
                vita2d_draw_rectangle(60, y + 49, 525, 2, palette().background);
                vita2d_draw_rectangle(60, y + 49, 525.0f * item.progress.percent / 100, 2, palette().accent);
            }
        }
        if (order_.empty()) {
            text(60, 155, palette().text, 1.0f, "No books in this filter.", 525);
            text(60, 190, palette().muted, 0.8f, "Use L/R to return to All books.", 525);
        } else {
            const auto& item = entries_[order_[selected_]];
            vita2d_draw_rectangle(642, 96, 270, 300, palette().panel);
            if (image_) {
                const auto scale = fit_scale(246, 276);
                vita2d_draw_texture_scale(image_, 777 - vita2d_texture_get_width(image_) * scale / 2,
                    246 - vita2d_texture_get_height(image_) * scale / 2, scale, scale);
            } else {
                vita2d_draw_rectangle(722, 173, 108, 136, palette().background);
                vita2d_draw_rectangle(734, 184, 3, 114, palette().accent);
                for (int line = 0; line < 4; ++line) vita2d_draw_rectangle(750, 211 + line * 18, 62, 2, palette().muted);
                text(675, 362, palette().muted, 0.75f, item.guide ? "Reader guide" : "No cover available", 230);
            }
            text(644, 420, palette().text, 0.8f, item.author.empty() ? "Local library" : item.author, 268);
            text(644, 442, palette().muted, 0.65f, !item.error.empty() ? item.error :
                !cover_message_.empty() ? cover_message_ : reader::is_epub_book(item.filename) ?
                std::to_string(item.chapters) + " chapters" : "UTF-8 text", 268);
        }
        text(48, 471, palette().accent, 0.7f, message_.empty() ?
            "ux0:data/NovelReader/books/  /  " + std::to_string(order_.size()) + " shown" : message_);
        text(48, 501, palette().muted, 0.7f, "UP/DOWN  Browse   CROSS  Read   SQUARE  Sort   L/R  Filter");
        text(48, 526, palette().muted, 0.7f, "TRIANGLE  Refresh library   START  Exit");
    }

    void draw_reader() const {
        const auto subtitle = publication_.epub ?
            std::to_string(chapter_ + 1) + "/" + std::to_string(publication_.chapters.size()) + "  " +
            publication_.chapters[chapter_].title + (publication_.author.empty() ? "" : "  /  " + publication_.author) : "LOCAL BOOK";
        heading(publication_.title, subtitle);
        int y = 96;
        const auto end = visible_end();
        for (auto i = top_; i < end; ++i) {
            if (lines_[i].illustration == reader::no_illustration) draw_line(lines_[i], y + line_height() - 8);
            else draw_inline_image(lines_[i], y);
            y += lines_[i].height;
        }
        const auto percent = end == lines_.size() ? 100 : static_cast<int>(100 * lines_[top_].offset / std::max<std::size_t>(1, book_.text.size()));
        vita2d_draw_rectangle(48, 452, 864, 3, palette().panel);
        vita2d_draw_rectangle(48, 452, 864.0f * percent / 100.0f, 3, palette().accent);
        const auto status = message_.empty() ?
            std::to_string(percent) + (publication_.epub ? "% of chapter" : "% read") + "  /  Page " +
            std::to_string(std::upper_bound(pages_.begin(), pages_.end(), top_) - pages_.begin()) +
            " of " + std::to_string(pages_.size()) : message_;
        text(48, 480, palette().accent, 0.75f, status);
        text(48, 506, palette().muted, 0.65f, publication_.epub ?
             "LEFT/RIGHT  Page   L/R  Chapter   CROSS  Contents   TRIANGLE  Settings   CIRCLE  Library" :
             "L/R or LEFT/RIGHT  Page   UP/DOWN  Scroll   TRIANGLE  Settings   CIRCLE  Library");
        text(48, 530, palette().muted, 0.65f, "SQUARE  Set bookmark   SELECT  Go to bookmark   UP/DOWN  Scroll   START  Save & exit");
    }

    void draw_line(const reader::LayoutBlock& line, int y) const {
        std::size_t at = 0;
        int x = 48;
        auto span = std::lower_bound(book_.styles.begin(), book_.styles.end(), line.offset,
            [](const reader::StyleSpan& value, std::size_t offset) { return value.end <= offset; });
        while (at < line.text.size()) {
            const auto absolute = line.offset + at;
            while (span != book_.styles.end() && span->end <= absolute) ++span;
            unsigned style = reader::Plain;
            auto end = line.text.size();
            if (span != book_.styles.end()) {
                if (span->begin <= absolute) { style = span->style; end = std::min(end, span->end - line.offset); }
                else end = std::min(end, span->begin - line.offset);
            }
            const auto run = line.text.substr(at, end - at);
            const auto color = style & reader::Heading ? palette().accent : palette().text;
            vita2d_pgf_draw_text(font_, x, y, color, font_scale(), run.c_str());
            if (style & reader::Strong) vita2d_pgf_draw_text(font_, x + 1, y, color, font_scale(), run.c_str());
            const auto width = vita2d_pgf_text_width(font_, font_scale(), run.c_str());
            if (style & reader::Emphasis) vita2d_draw_rectangle(x, y + 3, width, 1, color);
            x += width; at = end;
        }
    }

    void draw_contents() const {
        heading("Contents", publication_.title);
        const auto first = (contents_selected_ / 6) * 6;
        for (auto i = first; i < publication_.chapters.size() && i < first + 6; ++i) {
            const auto y = 100 + static_cast<int>(i - first) * 53;
            if (i == contents_selected_) vita2d_draw_rectangle(40, y, 880, 49, palette().panel);
            text(60, y + 30, i == chapter_ ? palette().accent : palette().text, 1.0f,
                 std::to_string(i + 1) + ". " + publication_.chapters[i].title);
        }
        text(48, 453, palette().accent, 0.75f, message_);
        text(48, 485, palette().muted, 0.65f, "UP/DOWN  Choose   LEFT/RIGHT  Browse   CROSS  Open   SQUARE  Illustrations   CIRCLE  Return");
        text(48, 518, palette().muted, 0.75f,
             std::to_string(contents_selected_ + 1) + " / " + std::to_string(publication_.chapters.size()) + " chapters");
    }

    void draw_illustrations() const {
        heading("Illustrations", "Current chapter: " + publication_.chapters[chapter_].title);
        const auto first = (illustration_selected_ / 6) * 6;
        for (auto i = first; i < book_.illustrations.size() && i < first + 6; ++i) {
            const auto y = 100 + static_cast<int>(i - first) * 53;
            if (i == illustration_selected_) vita2d_draw_rectangle(40, y, 880, 49, palette().panel);
            text(60, y + 30, palette().text, 1.0f, std::to_string(i + 1) + ". " + book_.illustrations[i].label);
        }
        if (book_.illustrations.empty()) text(60, 155, palette().muted, 1.0f, "This chapter has no illustrations.");
        text(48, 453, palette().accent, 0.75f, message_);
        text(48, 490, palette().muted, 0.75f, "UP/DOWN  Choose   CROSS  View   CIRCLE  Contents");
        text(48, 521, palette().muted, 0.7f, "PNG and JPEG images. Your reading position stays unchanged.");
    }

    void draw_image() const {
        heading(book_.illustrations[illustration_selected_].label,
                std::to_string(illustration_selected_ + 1) + " / " + std::to_string(book_.illustrations.size()) + " illustrations");
        if (image_) {
            const auto scale = fit_scale(864, 350) * zoom_;
            vita2d_enable_clipping();
            vita2d_set_clip_rectangle(48, 96, 912, 446);
            vita2d_draw_texture_scale(image_, 480 - vita2d_texture_get_width(image_) * scale / 2 + pan_x_,
                271 - vita2d_texture_get_height(image_) * scale / 2 + pan_y_, scale, scale);
            vita2d_disable_clipping();
        }
        text(48, 477, palette().accent, 0.7f, message_.empty() ?
             std::to_string(static_cast<int>(zoom_ * 100)) + "% of fit size" : message_);
        text(48, 505, palette().muted, 0.7f, "CROSS  Zoom in   SQUARE  Zoom out   D-PAD  Pan   TRIANGLE  Fit");
        text(48, 530, palette().muted, 0.7f, "L/R  Previous/next illustration   CIRCLE  Illustration list");
    }

    void draw_settings() const {
        heading("Reading settings", "UP/DOWN  Select     LEFT/RIGHT  Adjust     CIRCLE  Apply & return");
        const char* sizes[] = {"Small", "Medium", "Large"};
        const char* spacings[] = {"Compact", "Normal", "Relaxed"};
        const char* themes[] = {"Night", "Paper", "Sepia"};
        const std::string rows[] = {
            std::string("Font size:     ") + sizes[settings_.font],
            std::string("Line spacing:  ") + spacings[settings_.spacing],
            std::string("Theme:         ") + themes[settings_.theme]
        };
        for (int i = 0; i < 3; ++i) {
            if (setting_row_ == i) vita2d_draw_rectangle(40, 106 + i * 52, 880, 46, palette().panel);
            text(60, 136 + i * 52, palette().text, 1.0f, rows[i]);
        }
        text(48, 323, palette().muted, 0.75f, "Preview");
        text(48, 365, palette().text, font_scale(), "A quiet evening. One more chapter.");
        text(48, 365 + line_height(), palette().text, font_scale(), "The next adventure waits between the lines.");
        text(48, 500, palette().muted, 0.75f, "Your reading position is kept when the text size changes.");
    }

    vita2d_pgf* font_;
    reader::Settings settings_;
    reader::Settings previous_settings_;
    reader::Book book_;
    reader::Publication publication_;
    reader::Progress progress_;
    std::vector<reader::LibraryItem> entries_;
    std::vector<std::size_t> order_;
    reader::LibrarySort sort_ = reader::LibrarySort::Title;
    reader::LibraryFilter filter_ = reader::LibraryFilter::All;
    vita2d_texture* image_ = nullptr;
    std::string cover_key_, cover_message_;
    float zoom_ = 1, pan_x_ = 0, pan_y_ = 0;
    std::size_t illustration_selected_ = 0;
    std::vector<reader::LayoutBlock> lines_;
    std::vector<std::size_t> pages_;
    std::vector<InlineTexture> inline_images_;
    std::string active_filename_, message_;
    std::size_t selected_ = 0, top_ = 0;
    std::size_t chapter_ = 0, contents_selected_ = 0;
    int setting_row_ = 0;
    Screen screen_ = Screen::Library;
};
} // namespace

int main() {
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(root.c_str(), 0777);
    sceIoMkdir(books_directory.c_str(), 0777);
    sceIoMkdir(progress_directory.c_str(), 0777);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    if (vita2d_init() < 0) { sceKernelExitProcess(1); return 1; }
    vita2d_set_vblank_wait(1);
    auto* font = vita2d_load_default_pgf();
    if (!font) { vita2d_fini(); sceKernelExitProcess(1); return 1; }

    {
        App app(font);
        unsigned previous = 0;
        std::uint64_t repeat_at = 0;
        constexpr unsigned repeatable = SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT |
                                        SCE_CTRL_RIGHT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;
        bool running = true;
        while (running) {
            SceCtrlData pad{};
            sceCtrlPeekBufferPositive(0, &pad, 1);
            auto pressed = pad.buttons & ~previous;
            const auto now = sceKernelGetProcessTimeWide();
            if (pad.buttons != previous) repeat_at = now + 350000;
            else if ((pad.buttons & repeatable) && now >= repeat_at) {
                pressed |= pad.buttons & repeatable;
                repeat_at = now + 140000;
            }
            previous = pad.buttons;
            running = app.input(pressed);
            app.draw();
        }
    }
    vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
