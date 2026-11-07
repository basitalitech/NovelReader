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

enum class Screen { Library, Reader, Settings };

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
                std::vector<reader::Line>().swap(lines_);
                active_filename_.clear();
            }
            else if (pressed & SCE_CTRL_TRIANGLE) {
                setting_row_ = 0; previous_settings_ = settings_; screen_ = Screen::Settings;
            }
            else {
                const auto previous = top_;
                if (pressed & (SCE_CTRL_RIGHT | SCE_CTRL_RTRIGGER))
                    top_ = std::min(top_ + page_lines(), lines_.size() - 1);
                if (pressed & (SCE_CTRL_LEFT | SCE_CTRL_LTRIGGER))
                    top_ = top_ > page_lines() ? top_ - page_lines() : 0;
                if ((pressed & SCE_CTRL_UP) && top_ > 0) --top_;
                if ((pressed & SCE_CTRL_DOWN) && top_ + 1 < lines_.size()) ++top_;
                bool changed = previous != top_;
                if (changed) message_.clear();
                if (pressed & SCE_CTRL_SQUARE) {
                    progress_.has_bookmark = true;
                    progress_.bookmark = lines_[top_].offset;
                    message_ = "Bookmark saved. SELECT returns here.";
                    changed = true;
                }
                if (pressed & SCE_CTRL_SELECT) {
                    if (progress_.has_bookmark) {
                        top_ = reader::line_at_offset(lines_, progress_.bookmark);
                        changed = true;
                    } else message_ = "Press SQUARE to save a bookmark.";
                }
                if (changed) save();
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
                if (SCE_S_ISREG(entry.d_stat.st_mode) && reader::is_text_book(name))
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
        for (auto& item : entries_)
            item.started = reader::load_progress(reader::progress_path(progress_directory, item.filename)).offset > 0;
        selected_ = std::min(selected_, entries_.size() - 1);
    }

    void open() {
        message_ = "Opening book...";
        draw();
        std::string error;
        try {
            if (!reader::load_book(entries_[selected_].path, book_, error)) {
                message_ = error;
                return;
            }
            active_filename_ = entries_[selected_].filename;
            progress_ = reader::load_progress(reader::progress_path(progress_directory, active_filename_));
            reflow(progress_.offset);
        } catch (const std::exception&) {
            book_ = {};
            std::vector<reader::Line>().swap(lines_);
            active_filename_.clear();
            error = "Book layout is too large for this version.";
            message_ = error;
            return;
        }
        message_.clear();
        screen_ = Screen::Reader;
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
        if (!reader::save_progress(reader::progress_path(progress_directory, active_filename_), progress_))
            message_ = "Could not save progress. Check free space.";
        else
            for (auto& item : entries_)
                if (item.filename == active_filename_) item.started = progress_.offset > 0;
    }

    void draw_library() const {
        heading("Novel Reader", "LOCAL LIBRARY  /  UTF-8 TXT");
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
                 entries_[i].started ? "Continue reading" : "TXT  /  On this Vita");
        }
        text(48, 438, palette().muted, 0.7f, "Copy .txt books to ux0:data/NovelReader/books/");
        text(48, 463, palette().accent, 0.75f, message_);
        text(48, 500, palette().muted, 0.75f, "UP/DOWN  Browse     CROSS  Read     TRIANGLE  Refresh     START  Exit");
        text(48, 525, palette().muted, 0.65f,
             std::to_string(selected_ + 1) + " / " + std::to_string(entries_.size()) + " books");
    }

    void draw_reader() const {
        heading(book_.title, "LOCAL BOOK");
        for (std::size_t i = 0; i < page_lines() && top_ + i < lines_.size(); ++i)
            vita2d_pgf_draw_text(font_, 48, 112 + static_cast<int>(i) * line_height(),
                                palette().text, font_scale(), lines_[top_ + i].text.c_str());
        const auto end = std::min(top_ + page_lines(), lines_.size());
        const auto percent = end == lines_.size() ? 100 : static_cast<int>(100 * top_ / lines_.size());
        vita2d_draw_rectangle(48, 452, 864, 3, palette().panel);
        vita2d_draw_rectangle(48, 452, 864.0f * percent / 100.0f, 3, palette().accent);
        const auto status = message_.empty() ?
            std::to_string(percent) + "%  /  Line " + std::to_string(top_ + 1) + " of " + std::to_string(lines_.size()) : message_;
        text(48, 480, palette().accent, 0.75f, status);
        text(48, 506, palette().muted, 0.7f, "L/R or LEFT/RIGHT  Page     UP/DOWN  Line     TRIANGLE  Settings     CIRCLE  Library");
        text(48, 530, palette().muted, 0.7f, "SQUARE  Set bookmark     SELECT  Go to bookmark     START  Save & exit");
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
    reader::Progress progress_;
    std::vector<Entry> entries_;
    std::vector<reader::Line> lines_;
    std::string active_filename_, message_;
    std::size_t selected_ = 0, top_ = 0;
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
