#include "reader.hpp"

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

struct Entry { std::string filename, title, path; bool started = false; };
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

enum class Screen { Library, Reader, Settings, Contents };

class App {
public:
    explicit App(vita2d_pgf* font) : font_(font), settings_(reader::load_settings(settings_path)) {
        scan();
    }

    void draw() {
        const auto& color = palette();
        vita2d_set_clear_color(color.background);
        vita2d_start_drawing();
        vita2d_clear_screen();
        if (screen_ == Screen::Library) draw_library();
        else if (screen_ == Screen::Reader) draw_reader();
        else if (screen_ == Screen::Contents) draw_contents();
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
            if ((pressed & SCE_CTRL_DOWN) && selected_ + 1 < entries_.size()) ++selected_;
            if (pressed & SCE_CTRL_TRIANGLE) scan();
            if ((pressed & SCE_CTRL_CROSS) && !entries_.empty()) open();
        } else if (screen_ == Screen::Reader) {
            if (pressed & SCE_CTRL_CIRCLE) {
                save(); screen_ = Screen::Library;
                book_ = {};
                publication_ = {};
                std::vector<reader::Line>().swap(lines_);
                active_filename_.clear();
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
                    if (top_ + page_lines() >= lines_.size() && chapter_ + 1 < publication_.chapters.size())
                        change_chapter(chapter_ + 1, 0);
                    else top_ = std::min(top_ + page_lines(), lines_.size() - 1);
                }
                if (pressed & (SCE_CTRL_LEFT | SCE_CTRL_LTRIGGER)) {
                    if (top_ == 0 && chapter_ > 0) change_chapter(chapter_ - 1, 0, true);
                    else top_ = top_ > page_lines() ? top_ - page_lines() : 0;
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
                            top_ = reader::line_at_offset(lines_, progress_.bookmark); changed = true;
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
            if (pressed & SCE_CTRL_CROSS) {
                if (contents_selected_ == chapter_ || change_chapter(contents_selected_, 0)) {
                    screen_ = Screen::Reader; save();
                }
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
    const Palette& palette() const { return palettes[settings_.theme]; }
    float font_scale() const { return 1.0f + settings_.font * 0.2f; }
    int line_height() const { return 26 + settings_.font * 5 + settings_.spacing * 4; }
    std::size_t page_lines() const { return static_cast<std::size_t>(350 / line_height()); }

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
        entries_.clear();
        message_.clear();
        const auto directory = sceIoDopen(books_directory.c_str());
        if (directory >= 0) {
            SceIoDirent entry{};
            int result = 0;
            while ((result = sceIoDread(directory, &entry)) > 0) {
                const std::string name(entry.d_name);
                if (SCE_S_ISREG(entry.d_stat.st_mode) && reader::is_supported_book(name))
                    entries_.push_back({name, reader::title_from_filename(name), books_directory + '/' + name});
                entry = {};
            }
            if (result < 0) message_ = "Could not read the entire books folder.";
            sceIoDclose(directory);
        } else message_ = "Cannot open books folder. Check storage and restart.";
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            return a.title < b.title;
        });
        // Bundled guide is always available without modifying the user's library.
        entries_.push_back({"@welcome", "Welcome to Novel Reader", "app0:/welcome.txt"});
        for (auto& item : entries_) {
            const auto progress = reader::load_progress(reader::progress_path(progress_directory, item.filename));
            item.started = progress.offset > 0 || progress.chapter > 0;
        }
        selected_ = std::min(selected_, entries_.size() - 1);
    }

    void open() {
        message_ = "Opening book...";
        draw();
        std::string error;
        try {
            if (!reader::open_publication(entries_[selected_].path, publication_, error)) {
                message_ = error;
                return;
            }
            progress_ = reader::load_progress(reader::progress_path(progress_directory, entries_[selected_].filename));
            if (progress_.chapter >= publication_.chapters.size()) { progress_.chapter = 0; progress_.offset = 0; }
            if (progress_.bookmark_chapter >= publication_.chapters.size()) progress_.has_bookmark = false;
            if (!change_chapter(progress_.chapter, progress_.offset)) { publication_ = {}; return; }
            active_filename_ = entries_[selected_].filename;
            entries_[selected_].title = publication_.title;
        } catch (const std::exception&) {
            book_ = {};
            publication_ = {};
            std::vector<reader::Line>().swap(lines_);
            active_filename_.clear();
            error = "Book layout is too large for this version.";
            message_ = error;
            return;
        }
        message_.clear();
        screen_ = Screen::Reader;
    }

    bool change_chapter(std::size_t index, std::size_t offset, bool at_end = false) {
        message_ = "Opening chapter...";
        draw();
        try {
            reader::Book candidate;
            std::string error;
            if (!reader::load_chapter(publication_, index, candidate, error)) { message_ = error; return false; }
            const auto scale = font_scale();
            auto layout = reader::wrap_text(candidate.text, 864, [this, scale](const std::string& value) {
                return vita2d_pgf_text_width(font_, scale, value.c_str());
            });
            const auto position = at_end ? (layout.size() > page_lines() ? layout.size() - page_lines() : 0) :
                                          reader::line_at_offset(layout, offset);
            if (publication_.chapters[index].title == "Chapter " + std::to_string(index + 1))
                publication_.chapters[index].title = candidate.title;
            book_ = std::move(candidate); lines_ = std::move(layout); top_ = position; chapter_ = index;
            message_.clear();
            return true;
        } catch (const std::exception&) {
            message_ = "Chapter layout is too large for this version.";
            return false;
        }
    }

    void reflow(std::size_t offset) {
        const auto scale = font_scale();
        lines_ = reader::wrap_text(book_.text, 864, [this, scale](const std::string& value) {
            return vita2d_pgf_text_width(font_, scale, value.c_str());
        });
        top_ = reader::line_at_offset(lines_, offset);
    }

    void save() {
        if (active_filename_.empty() || lines_.empty()) return;
        progress_.offset = lines_[top_].offset;
        progress_.chapter = chapter_;
        if (!reader::save_progress(reader::progress_path(progress_directory, active_filename_), progress_))
            message_ = "Could not save progress. Check free space.";
        else
            for (auto& item : entries_)
                if (item.filename == active_filename_) item.started = progress_.offset > 0 || chapter_ > 0;
    }

    void draw_library() const {
        heading("Novel Reader", "LOCAL LIBRARY  /  TXT + EPUB");
        const auto first = (selected_ / 6) * 6;
        for (std::size_t i = first; i < entries_.size() && i < first + 6; ++i) {
            const auto y = 96 + static_cast<int>(i - first) * 53;
            if (i == selected_) {
                vita2d_draw_rectangle(40, y, 880, 49, palette().panel);
                vita2d_draw_rectangle(40, y, 4, 49, palette().accent);
            }
            text(60, y + 25, palette().text, 1.0f, entries_[i].title, 720);
            text(60, y + 43, palette().muted, 0.65f,
                 entries_[i].filename == "@welcome" ? "Bundled guide" :
                 entries_[i].started ? "Continue reading" :
                 reader::is_epub_book(entries_[i].filename) ? "EPUB  /  On this Vita" : "TXT  /  On this Vita");
        }
        text(48, 438, palette().muted, 0.7f, "Copy .txt or .epub books to ux0:data/NovelReader/books/");
        text(48, 463, palette().accent, 0.75f, message_);
        text(48, 500, palette().muted, 0.75f, "UP/DOWN  Browse     CROSS  Read     TRIANGLE  Refresh     START  Exit");
        text(48, 525, palette().muted, 0.65f,
             std::to_string(selected_ + 1) + " / " + std::to_string(entries_.size()) + " books");
    }

    void draw_reader() const {
        const auto subtitle = publication_.epub ?
            std::to_string(chapter_ + 1) + "/" + std::to_string(publication_.chapters.size()) + "  " +
            publication_.chapters[chapter_].title + (publication_.author.empty() ? "" : "  /  " + publication_.author) : "LOCAL BOOK";
        heading(publication_.title, subtitle);
        for (std::size_t i = 0; i < page_lines() && top_ + i < lines_.size(); ++i)
            draw_line(lines_[top_ + i], 112 + static_cast<int>(i) * line_height());
        const auto end = std::min(top_ + page_lines(), lines_.size());
        const auto percent = end == lines_.size() ? 100 : static_cast<int>(100 * top_ / lines_.size());
        vita2d_draw_rectangle(48, 452, 864, 3, palette().panel);
        vita2d_draw_rectangle(48, 452, 864.0f * percent / 100.0f, 3, palette().accent);
        const auto status = message_.empty() ?
            std::to_string(percent) + (publication_.epub ? "% of chapter  /  Line " : "%  /  Line ") +
            std::to_string(top_ + 1) + " of " + std::to_string(lines_.size()) : message_;
        text(48, 480, palette().accent, 0.75f, status);
        text(48, 506, palette().muted, 0.65f, publication_.epub ?
             "LEFT/RIGHT  Page   L/R  Chapter   CROSS  Contents   TRIANGLE  Settings   CIRCLE  Library" :
             "L/R or LEFT/RIGHT  Page   UP/DOWN  Line   TRIANGLE  Settings   CIRCLE  Library");
        text(48, 530, palette().muted, 0.65f, "SQUARE  Set bookmark   SELECT  Go to bookmark   UP/DOWN  Line   START  Save & exit");
    }

    void draw_line(const reader::Line& line, int y) const {
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
        text(48, 485, palette().muted, 0.7f, "UP/DOWN  Choose   LEFT/RIGHT  Browse   CROSS  Open   CIRCLE  Return");
        text(48, 518, palette().muted, 0.75f,
             std::to_string(contents_selected_ + 1) + " / " + std::to_string(publication_.chapters.size()) + " chapters");
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
    std::vector<Entry> entries_;
    std::vector<reader::Line> lines_;
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
