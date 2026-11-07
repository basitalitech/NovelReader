#include "reader.hpp"
#include "vita_images.hpp"
#include "vita_fonts.hpp"
#include "controls.hpp"
#include "online.hpp"
#include "ui.hpp"

#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/touch.h>
#include <psp2/ime_dialog.h>
#include <psp2/apputil.h>
#include <vita2d.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <cmath>
#include <ctime>
#include <memory>
#include <string>
#include <stdexcept>
#include <vector>

extern "C" {
unsigned int _newlib_heap_size_user = 64 * 1024 * 1024;
}

namespace {
const std::string root = "ux0:data/NovelReader";
const std::string books_directory = root + "/books";
const std::string progress_directory = root + "/progress";
const std::string settings_path = root + "/settings.txt";
const std::string library_path = root + "/library.txt";

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

enum class Screen { Library, Reader, Settings, Contents, Illustrations, Image, Sources, Catalog, Novel, OnlineChapters };
enum class Command { Button, More, Sort, Jump, Immersive, Resume, Retry, SortTitle, SortRecent, SortProgress };
struct Action {
    std::string label;
    unsigned button;
    Command command;
    Action(std::string name, unsigned key = 0, Command action = Command::Button)
        : label(std::move(name)), button(key), command(action) {}
};
struct TextureDeleter { void operator()(vita2d_texture* texture) const { vita2d_free_texture(texture); } };
struct InlineTexture {
    std::size_t illustration;
    std::unique_ptr<vita2d_texture, TextureDeleter> texture;
    std::string error;
};

class App {
public:
    explicit App(vita2d_pgf* font) : fonts_(font), settings_(reader::load_settings(settings_path)) {
        sort_ = static_cast<reader::LibrarySort>(settings_.library_sort);
        filter_ = static_cast<reader::LibraryFilter>(settings_.library_filter);
        if (!reader::load_library(library_path, library_books_)) {
            SceIoStat state{};
            library_writable_ = sceIoGetstat(library_path.c_str(), &state) < 0;
        }
        scan();
        if (!fonts_.complete()) message_ = "Some bundled fonts are unavailable. Using the system font as fallback.";
    }
    ~App() {
        online_.cancel();
        if (ime_active_) { sceImeDialogAbort(); sceImeDialogTerm(); }
        release_image();
    }

    void draw() {
        if (message_ == notice_ && sceKernelGetProcessTimeWide() >= notice_until_) message_.clear();
        if (screen_ == Screen::Reader) sync_inline_images();
        const auto& color = palette();
        vita2d_set_clear_color(color.background);
        vita2d_start_drawing();
        vita2d_clear_screen();
        if (is_online_screen()) draw_online();
        else if (screen_ == Screen::Library) draw_library();
        else if (screen_ == Screen::Reader) draw_reader();
        else if (screen_ == Screen::Contents) draw_contents();
        else if (screen_ == Screen::Illustrations) draw_illustrations();
        else if (screen_ == Screen::Image) draw_image();
        else draw_settings();
        draw_toolbar();
        draw_feedback();
        if (!menu_.empty()) draw_menu();
        vita2d_end_drawing();
        if (ime_active_) vita2d_common_dialog_update();
        vita2d_swap_buffers();
        vita2d_wait_rendering_done();
    }

    bool input(unsigned pressed) {
        tick_online();
        if (ime_active_) return true;
        if (pressed & SCE_CTRL_START) {
            online_.cancel();
            if (screen_ == Screen::Settings) reader::save_settings(settings_path, settings_);
            save(); return false;
        }
        if (online_busy_) {
            if (pressed & SCE_CTRL_CIRCLE) { online_.cancel(); message_ = "Canceling..."; }
            return true;
        }
        if (!menu_.empty()) {
            if (pressed & SCE_CTRL_CIRCLE) { close_menu(); return true; }
            if ((pressed & SCE_CTRL_UP) && menu_selected_) --menu_selected_;
            if ((pressed & SCE_CTRL_DOWN) && menu_selected_ + 1 < menu_.size()) ++menu_selected_;
            if (pressed & SCE_CTRL_CROSS) { const auto action = menu_[menu_selected_]; close_menu(); return perform(action); }
            return true;
        }
        if ((pressed & SCE_CTRL_LTRIGGER) && (pressed & SCE_CTRL_RTRIGGER)) { show_more(); return true; }
        if (is_online_screen()) return input_online(pressed);
        if (screen_ == Screen::Library) {
            if (pressed & SCE_CTRL_SELECT) { importing_ = false; current_folder_.clear(); release_image(); screen_ = Screen::Sources; online_selected_ = 0; message_.clear(); return true; }
            if (pressed & SCE_CTRL_CIRCLE) {
                if (importing_ && !current_folder_.empty()) {
                    const auto child = current_folder_;
                    browse_folder(reader::parent_library_folder(current_folder_), child);
                } else if (importing_) {
                    importing_ = false; browse_folder("");
                }
                return true;
            }
            if ((pressed & SCE_CTRL_UP) && selected_ > 0) --selected_;
            if ((pressed & SCE_CTRL_DOWN) && selected_ + 1 < order_.size()) ++selected_;
            if ((pressed & SCE_CTRL_LEFT) && !order_.empty()) selected_ = selected_ > 6 ? selected_ - 6 : 0;
            if ((pressed & SCE_CTRL_RIGHT) && !order_.empty()) selected_ = std::min(selected_ + 6, order_.size() - 1);
            if (pressed & SCE_CTRL_SQUARE) {
                show_sort(); return true;
            }
            if (!importing_ && (pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER))) {
                const auto step = pressed & SCE_CTRL_RTRIGGER ? 1 : 2;
                filter_ = static_cast<reader::LibraryFilter>((static_cast<int>(filter_) + step) % 3);
                selected_ = 0; refresh_library("");
                save_library_preferences();
            }
            if (pressed & SCE_CTRL_TRIANGLE) {
                if (!importing_) { importing_ = true; browse_folder(""); }
                else scan();
            }
            update_cover();
            if ((pressed & SCE_CTRL_CROSS) && !order_.empty()) open();
        } else if (screen_ == Screen::Reader) {
            if (pressed & SCE_CTRL_CIRCLE) {
                if (publication_.online) online_selected_ = chapter_;
                save(); screen_ = publication_.online ? Screen::OnlineChapters : Screen::Library;
                if (publication_.online) refresh_online_summary();
                inline_images_.clear();
                book_ = {};
                publication_ = {};
                std::vector<reader::LayoutBlock>().swap(lines_);
                pages_.clear();
                refresh_library(active_filename_);
                active_filename_.clear();
                if (screen_ == Screen::Library) update_cover();
            }
            else if (pressed & SCE_CTRL_TRIANGLE) {
                setting_row_ = 0; previous_settings_ = settings_; screen_ = Screen::Settings;
            }
            else if (pressed & SCE_CTRL_CROSS) {
                contents_selected_ = chapter_; screen_ = Screen::Contents; message_.clear();
            }
            else {
                if ((publication_.epub || publication_.online) && (pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER))) {
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
                    notify("Bookmark saved. SELECT returns here.");
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
            if (pressed & SCE_CTRL_TRIANGLE) { jump_to_chapter(); return true; }
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
            if ((pressed & SCE_CTRL_LEFT) && !book_.illustrations.empty()) illustration_selected_ = illustration_selected_ > 6 ? illustration_selected_ - 6 : 0;
            if ((pressed & SCE_CTRL_RIGHT) && !book_.illustrations.empty()) illustration_selected_ = std::min(illustration_selected_ + 6, book_.illustrations.size() - 1);
            if (pressed & SCE_CTRL_CIRCLE) { screen_ = Screen::Contents; message_.clear(); }
            if ((pressed & SCE_CTRL_CROSS) && !book_.illustrations.empty()) show_illustration(illustration_selected_, Screen::Illustrations);
        } else if (screen_ == Screen::Image) {
            if (pressed & SCE_CTRL_CIRCLE) { release_image(); screen_ = image_return_; message_.clear(); }
            else {
                if ((pressed & SCE_CTRL_LTRIGGER) && illustration_selected_ > 0) show_illustration(illustration_selected_ - 1, image_return_);
                if ((pressed & SCE_CTRL_RTRIGGER) && illustration_selected_ + 1 < book_.illustrations.size())
                    show_illustration(illustration_selected_ + 1, image_return_);
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
            if ((pressed & SCE_CTRL_DOWN) && setting_row_ < 3) ++setting_row_;
            int* value = setting_row_ == 0 ? &settings_.font :
                         setting_row_ == 1 ? &settings_.reading_font :
                         setting_row_ == 2 ? &settings_.spacing : &settings_.theme;
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

    Screen screen() const { return screen_; }
    bool can_pan_image() const { return screen_ == Screen::Image && menu_.empty() && !ime_active_; }
    std::size_t folder_revision() const { return folder_revision_; }
    void pan_image(float dx, float dy) {
        if (screen_ != Screen::Image) return;
        pan_x_ += dx; pan_y_ += dy; clamp_pan();
    }
    void transform_image(const reader::controls::Gesture& gesture) {
        pan_x_ += gesture.dx; pan_y_ += gesture.dy;
        const auto next = std::max(1.0f, std::min(4.0f, zoom_ * gesture.scale));
        const auto ratio = next / zoom_;
        // Keep the image point beneath the pinch midpoint stationary.
        pan_x_ = gesture.x - 480 - (gesture.x - 480 - pan_x_) * ratio;
        pan_y_ = gesture.y - 271 - (gesture.y - 271 - pan_y_) * ratio;
        zoom_ = next; clamp_pan();
    }

    bool gesture(const reader::controls::Gesture& gesture) {
        using reader::controls::GestureKind;
        if (gesture.kind == GestureKind::None) return true;
        const auto x = static_cast<int>(gesture.x), y = static_cast<int>(gesture.y);
        if (ime_active_) return true;
        if (!menu_.empty()) {
            if (gesture.kind != GestureKind::Tap) return true;
            const auto row = reader::controls::list_row_hit(x, y, reader::ui::menu_left, reader::ui::menu_right,
                reader::ui::menu_top, reader::ui::menu_pitch, 44, static_cast<int>(menu_.size()));
            if (row >= 0) { const auto action = menu_[row]; close_menu(); return perform(action); }
            if (y >= 448 || x < reader::ui::menu_left || x >= reader::ui::menu_right) close_menu();
            return true;
        }
        if (gesture.kind == GestureKind::Tap) {
            const auto actions = toolbar();
            const auto button = reader::controls::toolbar_hit(x, y, static_cast<int>(actions.size()));
            if (button >= 0) return perform(actions[button]);
            if (!message_.empty() && x >= 48 && x < 912 && y >= feedback_top() && y < feedback_bottom()) {
                if (message_ == notice_) message_.clear();
                return true;
            }
        }
        if (online_busy_) return true;
        if (is_online_screen()) {
            if (gesture.kind == GestureKind::Swipe) return input(gesture.dy < 0 || gesture.dx < 0 ? SCE_CTRL_RIGHT : SCE_CTRL_LEFT);
            if (gesture.kind != GestureKind::Tap) return true;
            if (screen_ == Screen::Novel && y >= 388 && y < 448 && x >= 60 && x < 900)
                return input(SCE_CTRL_SELECT);
            const auto row = reader::controls::list_row_hit(x,y,40,920,100,53,49);
            const auto size = online_size();
            const auto index = online_selected_ / 6 * 6 + (row < 0 ? 6 : row);
            if (screen_ != Screen::Novel && !online_busy_ && row >= 0 && index < size) { online_selected_ = index; return input(SCE_CTRL_CROSS); }
            return true;
        }
        if (gesture.kind == GestureKind::Transform) {
            if (screen_ == Screen::Image && y >= 96 && y < 446) transform_image(gesture);
            return true;
        }
        if (gesture.kind == GestureKind::Hold) {
            if (screen_ == Screen::Reader && x >= 48 && x < 912 && y >= reading_area().top && y < reading_area().top + reading_area().height) return input(SCE_CTRL_SQUARE);
            return true;
        }
        if (gesture.kind == GestureKind::Swipe) {
            const auto start_y = gesture.y - gesture.dy;
            const auto area = reading_area();
            if (start_y < (screen_ == Screen::Reader ? area.top : 96) ||
                start_y >= (screen_ == Screen::Reader ? area.top + area.height : 446)) return true;
            const bool horizontal = std::abs(gesture.dx) > std::abs(gesture.dy);
            const bool forward = horizontal ? gesture.dx < 0 : gesture.dy < 0;
            if (screen_ == Screen::Reader) {
                if (horizontal) return input(forward ? SCE_CTRL_RIGHT : SCE_CTRL_LEFT);
                const auto amount = static_cast<std::size_t>(std::max(1, std::min(6, static_cast<int>(std::abs(gesture.dy) / 40))));
                const auto previous = top_;
                top_ = forward ? std::min(top_ + amount, lines_.size() - 1) : (top_ > amount ? top_ - amount : 0);
                if (top_ != previous) { message_.clear(); save(); }
            } else if (screen_ == Screen::Settings) {
                return input(horizontal ? (forward ? SCE_CTRL_RIGHT : SCE_CTRL_LEFT) : (forward ? SCE_CTRL_DOWN : SCE_CTRL_UP));
            } else {
                auto* selection = screen_ == Screen::Library ? &selected_ :
                                  screen_ == Screen::Contents ? &contents_selected_ : &illustration_selected_;
                const auto size = screen_ == Screen::Library ? order_.size() :
                                  screen_ == Screen::Contents ? publication_.chapters.size() : book_.illustrations.size();
                if (size) *selection = forward ? std::min(*selection + 6, size - 1) : (*selection > 6 ? *selection - 6 : 0);
                if (screen_ == Screen::Library) update_cover();
            }
            return true;
        }
        if (screen_ == Screen::Library) {
            if (!importing_ && y >= 56 && y < 92 && x >= 40 && x < 610) {
                filter_ = static_cast<reader::LibraryFilter>((x - 40) / 190);
                selected_ = 0; refresh_library(""); update_cover(); save_library_preferences(); return true;
            }
            if (!importing_ && y >= 56 && y < 92 && x >= 642 && x < 920) { show_sort(); return true; }
            if (!importing_ && x >= 642 && x < 920 && y >= 402 && y < 466) { resume_recent(); return true; }
            const auto row = reader::controls::list_row_hit(x, y, 40, 610, reader::ui::library_top,
                reader::ui::library_pitch, reader::ui::library_row_height);
            const auto index = selected_ / 6 * 6 + (row < 0 ? 6 : row);
            if (row >= 0 && index < order_.size()) { selected_ = index; return input(SCE_CTRL_CROSS); }
            if (!order_.empty() && x >= 642 && x < 912 && y >= 104 && y < 396) return input(SCE_CTRL_CROSS);
        } else if (screen_ == Screen::Contents || screen_ == Screen::Illustrations) {
            const auto row = reader::controls::list_row_hit(x, y, 40, 920, 100, 53, 49);
            auto* selection = screen_ == Screen::Contents ? &contents_selected_ : &illustration_selected_;
            const auto size = screen_ == Screen::Contents ? publication_.chapters.size() : book_.illustrations.size();
            const auto index = *selection / 6 * 6 + (row < 0 ? 6 : row);
            if (row >= 0 && index < size) { *selection = index; return input(SCE_CTRL_CROSS); }
        } else if (screen_ == Screen::Settings) {
            const auto row = reader::controls::list_row_hit(x, y, 40, 490, 104, 78, 72, 4);
            if (row >= 0) {
                setting_row_ = row;
                if (x >= 390) return input(SCE_CTRL_RIGHT);
                if (x < 100) return input(SCE_CTRL_LEFT);
                if (row == 3 && x >= 110 && x < 380) settings_.theme = (x - 110) / 90;
            }
        } else if (screen_ == Screen::Reader && y >= reading_area().top && y < reading_area().top + reading_area().height && x >= 48 && x < 912) {
            int block_y = reading_area().top;
            for (auto at = top_; at < visible_end(); ++at) {
                const auto& block = lines_[at];
                if (block.illustration != reader::no_illustration && y >= block_y && y < block_y + block.height &&
                    (!block.image_width || std::abs(x - 480) <= block.image_width / 2)) {
                    show_illustration(block.illustration, Screen::Reader); return true;
                }
                block_y += block.height;
            }
            if (x >= 320 && x < 640) { toggle_immersive(); return true; }
            return input(x < 320 ? SCE_CTRL_LEFT : SCE_CTRL_RIGHT);
        } else if (screen_ == Screen::Image && x >= 48 && x < 912 && y >= 96 && y < 446) {
            auto zoom = gesture;
            zoom.scale = zoom_ > 1 ? 1 / zoom_ : 2;
            transform_image(zoom);
        }
        return true;
    }

private:
    bool is_online_screen() const {
        return screen_ == Screen::Sources || screen_ == Screen::Catalog || screen_ == Screen::Novel || screen_ == Screen::OnlineChapters;
    }
    std::size_t online_size() const {
        return screen_ == Screen::Sources ? reader::online::sources().size() : screen_ == Screen::Catalog ? online_catalog_.items.size() : online_novel_.chapters.size();
    }
    reader::ui::ReadingArea reading_area() const { return reader::ui::reading_area(immersive_); }
    void notify(const std::string& value) {
        message_ = notice_ = value;
        notice_until_ = sceKernelGetProcessTimeWide() + 2800000;
    }
    void close_menu() { menu_.clear(); ++folder_revision_; }
    void show_sort() {
        menu_title_ = "Sort books";
        menu_ = {{"Title", 0, Command::SortTitle}, {"Recently read", 0, Command::SortRecent}, {"Reading progress", 0, Command::SortProgress}};
        menu_selected_ = static_cast<std::size_t>(sort_); ++folder_revision_;
    }
    void show_more() {
        menu_title_ = "More actions";
        if (screen_ == Screen::Library && importing_) {
            menu_ = {{"Back", SCE_CTRL_CIRCLE}, {"Refresh folder", SCE_CTRL_TRIANGLE}, {"Sort files", 0, Command::Sort}, {"Save and exit", SCE_CTRL_START}};
        } else if (screen_ == Screen::Library) {
            menu_ = {{"Resume last read", 0, Command::Resume}, {"Sort books", 0, Command::Sort},
                {"Import books", SCE_CTRL_TRIANGLE}, {"Online novels", SCE_CTRL_SELECT}, {"Save and exit", SCE_CTRL_START}};
        } else if (screen_ == Screen::Reader) {
            menu_ = {{"Save bookmark", SCE_CTRL_SQUARE}, {"Go to bookmark", SCE_CTRL_SELECT},
                {immersive_ ? "Show reading controls" : "Hide reading controls", 0, Command::Immersive},
                {"Contents", SCE_CTRL_CROSS}, {"Reading settings", SCE_CTRL_TRIANGLE}};
        } else if (screen_ == Screen::Catalog) {
            menu_ = {{online_catalog_.next.empty() ? "Reload page" : "Next catalog page", SCE_CTRL_TRIANGLE}, {"Initial catalog", SCE_CTRL_SELECT}};
        } else if (screen_ == Screen::Novel) {
            menu_ = {{in_library() ? "Already in library" : "Add to library", SCE_CTRL_SQUARE}, {"Refresh chapters", SCE_CTRL_TRIANGLE}};
        } else if (screen_ == Screen::OnlineChapters) {
            menu_ = {{"Download all public chapters", SCE_CTRL_SELECT}, {"Jump to chapter", 0, Command::Jump}, {"Refresh chapters", SCE_CTRL_TRIANGLE}};
        } else if (screen_ == Screen::Contents) {
            menu_ = {{"Jump to chapter", 0, Command::Jump}, {"Illustrations", SCE_CTRL_SQUARE}};
        } else if (screen_ == Screen::Image) {
            menu_ = {{"Zoom in", SCE_CTRL_CROSS}, {"Zoom out", SCE_CTRL_SQUARE}, {"Fit to screen", SCE_CTRL_TRIANGLE}};
        } else { menu_ = {{"Back", SCE_CTRL_CIRCLE}}; }
        if (retry_available_ && (is_online_screen() || screen_ == Screen::Reader))
            menu_.insert(menu_.begin(), {"Retry failed request", 0, Command::Retry});
        menu_selected_ = 0; ++folder_revision_;
    }
    bool perform(const Action& action) {
        switch (action.command) {
        case Command::More: show_more(); break;
        case Command::Sort: show_sort(); break;
        case Command::Jump: jump_to_chapter(); break;
        case Command::Immersive: toggle_immersive(); break;
        case Command::Resume: resume_recent(); break;
        case Command::Retry:
            online_read_in_place_ = retry_in_place_; start_online(retry_request_); break;
        case Command::SortTitle: case Command::SortRecent: case Command::SortProgress: {
            const auto keep = selected_filename();
            sort_ = action.command == Command::SortTitle ? reader::LibrarySort::Title :
                action.command == Command::SortRecent ? reader::LibrarySort::Recent : reader::LibrarySort::Progress;
            refresh_library(keep); update_cover(); save_library_preferences(); break;
        }
        case Command::Button: return input(action.button);
        }
        return true;
    }
    void toggle_immersive() {
        const auto offset = lines_[top_].offset;
        immersive_ = !immersive_;
        try { reflow(offset); }
        catch (const std::exception&) { immersive_ = !immersive_; message_ = "Could not change reading layout. Previous layout kept."; }
        ++folder_revision_; save();
    }
    bool in_library() const {
        if (online_manifest_.size() <= books_directory.size()) return false;
        const auto filename = online_manifest_.substr(books_directory.size() + 1);
        return std::find(library_books_.begin(), library_books_.end(), filename) != library_books_.end();
    }
    void resume_recent() {
        const auto recent = reader::ui::recent_book(entries_);
        if (recent == entries_.size()) { notify("Open a book to start your reading journey."); return; }
        const auto filename = entries_[recent].filename;
        filter_ = reader::LibraryFilter::All;
        refresh_library(filename); save_library_preferences(); update_cover(); open();
        if (screen_ == Screen::Novel) resume_online();
    }
    void resume_online() {
        reader::Publication publication;
        if (reader::open_publication(online_manifest_, publication, message_)) {
            auto saved = reader::load_progress(reader::progress_path(progress_directory, online_manifest_.substr(books_directory.size() + 1)));
            reader::online::restore_progress(publication, saved);
            online_read_in_place_ = false; begin_online_read(saved.chapter);
        }
    }
    void jump_to_chapter() {
        const auto count = screen_ == Screen::Contents ? publication_.chapters.size() : online_novel_.chapters.size();
        if (!count) { notify("This book has no chapters."); return; }
        SceImeDialogParam param; sceImeDialogParamInit(&param);
        param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
        param.type = SCE_IME_TYPE_NUMBER;
        param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
        param.title = reinterpret_cast<const SceWChar16*>(u"Jump to chapter number");
        param.maxTextLength = 5; ime_input_[0] = ime_initial_[0] = 0;
        param.initialText = ime_initial_; param.inputTextBuffer = ime_input_;
        ime_jump_ = true;
        if (sceImeDialogInit(&param) >= 0) ime_active_ = true;
        else message_ = "Could not open chapter keyboard. Use left/right to browse chapters.";
    }
    std::vector<Action> toolbar() const {
        if (online_busy_) return {{"Cancel", SCE_CTRL_CIRCLE}};
        if (screen_ == Screen::Reader && immersive_) return {};
        if (screen_ == Screen::Library) return importing_ ? std::vector<Action>{{"Back", SCE_CTRL_CIRCLE}, {"Import", SCE_CTRL_CROSS}, {"Refresh", SCE_CTRL_TRIANGLE}} :
            std::vector<Action>{{"Import", SCE_CTRL_TRIANGLE}, {"Online", SCE_CTRL_SELECT}, {"Sort", SCE_CTRL_SQUARE}, {"More", 0, Command::More}};
        if (screen_ == Screen::Reader) return {{"Back", SCE_CTRL_CIRCLE}, {"Contents", SCE_CTRL_CROSS}, {"Settings", SCE_CTRL_TRIANGLE}, {"More", 0, Command::More}};
        if (screen_ == Screen::Contents) return {{"Back", SCE_CTRL_CIRCLE}, {"Open", SCE_CTRL_CROSS}, {"Jump", SCE_CTRL_TRIANGLE}, {"More", 0, Command::More}};
        if (screen_ == Screen::Illustrations) return {{"Back", SCE_CTRL_CIRCLE}, {"View", SCE_CTRL_CROSS}};
        if (screen_ == Screen::Settings) return {{"Decrease", SCE_CTRL_LEFT}, {"Increase", SCE_CTRL_RIGHT}, {"Apply", SCE_CTRL_CROSS}};
        if (screen_ == Screen::Image) return {{"Back", SCE_CTRL_CIRCLE}, {"Previous", SCE_CTRL_LTRIGGER}, {"Next", SCE_CTRL_RTRIGGER}, {"More", 0, Command::More}};
        if (screen_ == Screen::Sources) return {{"Library", SCE_CTRL_CIRCLE}, {"Browse", SCE_CTRL_CROSS}};
        if (screen_ == Screen::Catalog) return {{"Back", SCE_CTRL_CIRCLE}, {"Details", SCE_CTRL_CROSS}, {"Search", SCE_CTRL_SQUARE}, {"More", 0, Command::More}};
        if (screen_ == Screen::Novel) return {{"Back", SCE_CTRL_CIRCLE}, {online_started_ ? "Resume" : "Start reading", SCE_CTRL_SELECT}, {"Chapters", SCE_CTRL_CROSS}, {"More", 0, Command::More}};
        return {{"Details", SCE_CTRL_CIRCLE}, {"Read", SCE_CTRL_CROSS}, {"Download", SCE_CTRL_SQUARE}, {"More", 0, Command::More}};
    }
    void start_online(reader::online::Request request) {
        request.source = online_source_;
        retry_request_ = request; retry_in_place_ = online_read_in_place_; retry_available_ = false;
        download_completed_ = download_total_ = 0;
        if (online_.start(request)) { online_busy_ = true; message_ = "Loading... CIRCLE cancels."; }
        else {
            const auto failure = online_.poll();
            message_ = failure.error.empty() ? "An online operation is already running." : failure.error;
            retry_available_ = true;
        }
    }
    void browse_online(const std::string& url = "", const std::string& query = "") {
        reader::online::Request request; request.action = reader::online::Action::Browse;
        request.url = url; request.query = query; start_online(request);
    }
    void refresh_online_novel() {
        reader::online::Request request; request.action = reader::online::Action::Chapters;
        request.url = online_novel_.url; start_online(request);
    }
    void begin_online_read(std::size_t chapter) {
        reader::online::Request request; request.action = reader::online::Action::Read;
        request.novel = online_novel_; request.chapter = chapter; request.manifest = online_manifest_;
        start_online(request);
    }
    void download_online(bool all) {
        if (online_novel_.chapters.empty()) return;
        reader::online::Request request; request.action = reader::online::Action::Download;
        request.novel = online_novel_; request.chapter = std::min(online_selected_,online_novel_.chapters.size()-1);
        request.manifest = online_manifest_; request.all = all; start_online(request);
    }
    void search_online() {
        ime_jump_ = false;
        SceImeDialogParam param; sceImeDialogParamInit(&param);
        param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
        param.type = SCE_IME_TYPE_DEFAULT;
        param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
        param.title = reinterpret_cast<const SceWChar16*>(u"Search novels");
        param.maxTextLength = 64; ime_input_[0] = 0; ime_initial_[0] = 0;
        param.initialText = ime_initial_; param.inputTextBuffer = ime_input_;
        if (sceImeDialogInit(&param) >= 0) ime_active_ = true;
        else message_ = "Could not open search keyboard.";
    }
    void open_online_reader(std::size_t chapter) {
        if (online_read_in_place_) {
            online_read_in_place_ = false;
            if (change_chapter(chapter,pending_offset_,pending_end_)) { screen_ = Screen::Reader; save(); online_saved_chapter_ = chapter_; }
            return;
        }
        reader::Publication candidate;
        if (!reader::open_publication(online_manifest_,candidate,message_)) return;
        const auto identity = online_manifest_.substr(books_directory.size()+1);
        auto saved = reader::load_progress(reader::progress_path(progress_directory,identity));
        reader::online::restore_progress(candidate,saved);
        const auto offset = chapter == saved.chapter ? saved.offset : 0;
        publication_ = std::move(candidate); progress_ = saved;
        if (!change_chapter(chapter,offset)) return;
        active_filename_ = identity; online_selected_ = chapter;
        release_image(); screen_ = Screen::Reader; message_.clear(); save(); online_saved_chapter_ = chapter_;
    }
    void refresh_online_summary() {
        offline_chapters_.clear(); offline_count_ = 0;
        for (const auto& chapter : online_novel_.chapters) {
            const bool available = reader::online::cached(online_manifest_, chapter.url);
            offline_chapters_.push_back(available);
            if (available) ++offline_count_;
        }
        online_saved_chapter_ = 0; online_started_ = false;
        reader::Publication publication;
        std::string error;
        if (!online_manifest_.empty() && reader::open_publication(online_manifest_, publication, error)) {
            auto saved = reader::load_progress(reader::progress_path(progress_directory, online_manifest_.substr(books_directory.size() + 1)));
            reader::online::restore_progress(publication, saved);
            online_started_ = reader::reading_started(saved);
            online_saved_chapter_ = saved.chapter;
        }
    }
    void tick_online() {
        if (ime_active_) {
            if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return;
            SceImeDialogResult result{}; sceImeDialogGetResult(&result); sceImeDialogTerm(); ime_active_ = false;
            ++folder_revision_; // Suppress any touch left over from the keyboard.
            if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
                std::string query;
                for (std::size_t i=0;i<64 && ime_input_[i];++i) {
                    unsigned c=ime_input_[i];
                    if(c>=0xd800 && c<=0xdbff && i+1<64 && ime_input_[i+1]>=0xdc00 && ime_input_[i+1]<=0xdfff) { c=0x10000+(c-0xd800)*1024+ime_input_[++i]-0xdc00; }
                    if(c<128) query+=static_cast<char>(c);
                    else if(c<2048) { query+=static_cast<char>(0xc0|(c>>6)); query+=static_cast<char>(0x80|(c&63)); }
                    else if(c<65536) { query+=static_cast<char>(0xe0|(c>>12)); query+=static_cast<char>(0x80|((c>>6)&63)); query+=static_cast<char>(0x80|(c&63)); }
                    else { query+=static_cast<char>(0xf0|(c>>18)); query+=static_cast<char>(0x80|((c>>12)&63)); query+=static_cast<char>(0x80|((c>>6)&63)); query+=static_cast<char>(0x80|(c&63)); }
                }
                if (ime_jump_) {
                    const auto count = screen_ == Screen::Contents ? publication_.chapters.size() : online_novel_.chapters.size();
                    std::size_t index = 0;
                    if (reader::ui::chapter_number(query, count, index)) {
                        if (screen_ == Screen::Contents) contents_selected_ = index;
                        else online_selected_ = index;
                        message_.clear();
                    } else message_ = "Enter a chapter number from 1 to " + std::to_string(count) + ".";
                } else browse_online("",query);
            }
        }
        auto update = online_.poll();
        if (update.busy) {
            online_busy_ = true; message_ = update.message;
            download_completed_ = update.completed; download_total_ = update.total; return;
        }
        if (!update.done) return;
        online_busy_ = false;
        download_completed_ = update.completed; download_total_ = update.total;
        if (!update.error.empty()) {
            online_read_in_place_ = false; retry_available_ = true;
            refresh_online_summary(); message_ = update.error;
            if (download_total_) message_ += "  " + std::to_string(download_completed_) + "/" + std::to_string(download_total_) + " completed.";
            message_ += "  L+R: Retry in More."; return;
        }
        using reader::online::Action;
        if (update.request.action == Action::Browse) {
            online_catalog_ = std::move(update.result); online_selected_ = 0; screen_ = Screen::Catalog;
            online_browse_url_ = update.request.url; online_query_ = update.request.query;
            message_ = online_catalog_.items.empty() ? "No novels found. Try Search or another source." : "";
        } else if (update.request.action == Action::Chapters) {
            online_novel_ = std::move(update.novel); online_manifest_ = update.manifest;
            refresh_online_summary();
            online_selected_ = online_saved_chapter_; screen_ = Screen::Novel; message_.clear();
        } else if (update.request.action == Action::Read) {
            online_manifest_ = update.manifest;
            refresh_online_summary();
            open_online_reader(update.request.chapter);
        } else {
            refresh_online_summary();
            notify("Download complete. " + std::to_string(offline_count_) + " chapters available offline.");
        }
    }
    bool input_online(unsigned pressed) {
        if (pressed & SCE_CTRL_CIRCLE) {
            retry_available_ = false;
            if (screen_ == Screen::Sources) { screen_ = Screen::Library; scan(); }
            else if (screen_ == Screen::Catalog) { screen_ = Screen::Sources; online_selected_ = 0; }
            else if (screen_ == Screen::Novel) { screen_ = online_from_library_ ? Screen::Library : Screen::Catalog; online_selected_ = 0; if (online_from_library_) scan(); }
            else { screen_ = Screen::Novel; online_selected_ = online_saved_chapter_; }
            message_.clear(); return true;
        }
        const auto size = online_size();
        if (size && screen_ != Screen::Novel) {
            if ((pressed & SCE_CTRL_UP) && online_selected_) --online_selected_;
            if ((pressed & SCE_CTRL_DOWN) && online_selected_+1<size) ++online_selected_;
            if (pressed & SCE_CTRL_LEFT) online_selected_ = online_selected_>6 ? online_selected_-6 : 0;
            if (pressed & SCE_CTRL_RIGHT) online_selected_ = std::min(online_selected_+6,size-1);
        }
        if (screen_ == Screen::Sources) {
            if (pressed & SCE_CTRL_CROSS) { online_source_ = reader::online::sources()[online_selected_].id; browse_online(); }
        } else if (screen_ == Screen::Catalog) {
            if (pressed & SCE_CTRL_SQUARE) search_online();
            else if (pressed & SCE_CTRL_SELECT) browse_online();
            else if (pressed & SCE_CTRL_TRIANGLE) browse_online(online_catalog_.next.empty()?online_browse_url_:online_catalog_.next,online_query_);
            else if ((pressed & SCE_CTRL_CROSS) && size) {
                online_from_library_ = false;
                reader::online::Request request; request.action = reader::online::Action::Chapters;
                request.url = online_catalog_.items[online_selected_].url; start_online(request);
            }
        } else if (screen_ == Screen::Novel) {
            if (pressed & SCE_CTRL_CROSS) { screen_ = Screen::OnlineChapters; online_selected_ = online_saved_chapter_; message_.clear(); }
            else if (pressed & SCE_CTRL_SQUARE) add_to_library(online_manifest_.substr(books_directory.size()+1));
            else if (pressed & SCE_CTRL_TRIANGLE) refresh_online_novel();
            else if (pressed & SCE_CTRL_SELECT) {
                resume_online();
            }
        } else {
            if ((pressed & SCE_CTRL_CROSS) && size) { online_read_in_place_ = false; begin_online_read(online_selected_); }
            else if (pressed & SCE_CTRL_SQUARE) download_online(false);
            else if (pressed & SCE_CTRL_SELECT) download_online(true);
            else if (pressed & SCE_CTRL_TRIANGLE) refresh_online_novel();
        }
        return true;
    }
    void draw_online() const {
        const auto title = screen_ == Screen::Sources ? "Online sources" : screen_ == Screen::Catalog ? "Novels / " + online_source_ : online_novel_.title;
        heading(title, screen_ == Screen::Novel ? online_novel_.author : screen_ == Screen::OnlineChapters ?
            std::to_string(offline_count_) + " available offline  /  L+R: chapter actions" :
            screen_ == Screen::Catalog && !online_query_.empty() ? "Search: " + online_query_ : "Discover your next read");
        if (screen_ == Screen::Novel) {
            text(60,119,palette().accent,0.85f,std::to_string(online_novel_.chapters.size()) + " chapters  /  " + online_novel_.source);
            text(60,147,palette().muted,0.75f,std::to_string(offline_count_) + " available offline  /  " + (in_library() ? "In your library" : "Add to library from More"));
            const auto summary = reader::wrap_text(online_novel_.description.empty()?"No description provided by this source.":online_novel_.description,830,[this](const std::string& s){return fonts_.width(reader::FontRole::Interface,17,s);});
            for (std::size_t i=0;i<std::min<std::size_t>(summary.size(),7);++i)
                text(60,190+static_cast<int>(i)*25,palette().text,0.85f,summary[i].text);
            panel(60,388,840,60,true);
            text(82,413,palette().text,0.95f,online_started_ ? "Resume reading" : "Start reading", 780, reader::FontRole::Heading);
            text(82,437,palette().muted,0.7f,"Chapter " + std::to_string(online_saved_chapter_ + 1) + "  /  SELECT", 780);
        } else {
            const auto size = online_size(), first = online_selected_/6*6;
            for (auto i=first;i<size && i<first+6;++i) {
                const auto y = 100+static_cast<int>(i-first)*53;
                if (i==online_selected_) panel(40,y,880,49,true);
                std::string label,sub;
                if (screen_ == Screen::Sources) { label=reader::online::sources()[i].name; sub=reader::online::sources()[i].home; }
                else if (screen_ == Screen::Catalog) { label=online_catalog_.items[i].title; sub="Open novel details"; }
                else {
                    const auto& c=online_novel_.chapters[i]; label=c.title;
                    sub= i < offline_chapters_.size() && offline_chapters_[i] ? "Offline" : c.locked ? "Locked" : "Download to read offline";
                    if (online_started_ && i == online_saved_chapter_) sub = "Reading  /  " + sub;
                }
                if (screen_ == Screen::OnlineChapters && online_started_ && i == online_saved_chapter_)
                    vita2d_draw_fill_circle(58,y+19,3,palette().accent);
                text(70,y+23,palette().text,0.9f,label,825); text(70,y+43,palette().muted,0.7f,sub,825);
            }
            if (!size) text(60,160,palette().muted,0.9f,"No results. Try Search or another source.");
            text(48,465,palette().muted,0.7f,std::to_string(size?online_selected_+1:0)+" / "+std::to_string(size) + "  /  Left/right: browse");
        }
    }
    void panel(int x, int y, int width, int height, bool selected = false) const {
        // Rounded corners use only inexpensive vita2d primitives.
        const int radius = 6;
        vita2d_draw_rectangle(x + radius,y,width - 2 * radius,height,palette().panel);
        vita2d_draw_rectangle(x,y + radius,width,height - 2 * radius,palette().panel);
        for (const auto cx : {x + radius, x + width - radius})
            for (const auto cy : {y + radius, y + height - radius}) vita2d_draw_fill_circle(cx,cy,radius,palette().panel);
        if (selected) vita2d_draw_rectangle(x + 1,y + 10,3,height - 20,palette().accent);
    }
    void button_hint(int x, int y, const Action& action) const {
        const auto color = palette().accent;
        if (action.button == SCE_CTRL_CROSS) {
            vita2d_draw_line(x-5,y-5,x+5,y+5,color); vita2d_draw_line(x-5,y+5,x+5,y-5,color);
        } else if (action.button == SCE_CTRL_CIRCLE) {
            for (int i=0;i<16;++i) {
                const float a = i * 6.2831853f / 16, b = (i+1) * 6.2831853f / 16;
                vita2d_draw_line(x+7*std::cos(a),y+7*std::sin(a),x+7*std::cos(b),y+7*std::sin(b),color);
            }
        }
        else if (action.button == SCE_CTRL_TRIANGLE) {
            vita2d_draw_line(x,y-7,x-7,y+6,color); vita2d_draw_line(x-7,y+6,x+7,y+6,color); vita2d_draw_line(x+7,y+6,x,y-7,color);
        } else if (action.button == SCE_CTRL_SQUARE) {
            vita2d_draw_line(x-6,y-6,x+6,y-6,color); vita2d_draw_line(x+6,y-6,x+6,y+6,color);
            vita2d_draw_line(x+6,y+6,x-6,y+6,color); vita2d_draw_line(x-6,y+6,x-6,y-6,color);
        } else if (action.command == Command::More) {
            for (int i=-1;i<=1;++i) vita2d_draw_fill_circle(x+i*5,y,1.5f,color);
        } else {
            const auto label = action.button == SCE_CTRL_SELECT ? "SEL" : action.button == SCE_CTRL_START ? "START" :
                action.button == SCE_CTRL_LTRIGGER ? "L" : action.button == SCE_CTRL_RTRIGGER ? "R" :
                action.button == SCE_CTRL_LEFT ? "-" : action.button == SCE_CTRL_RIGHT ? "+" : "";
            text(x-10,y+5,color,0.55f,label,34);
        }
    }
    void draw_toolbar() const {
        const auto actions = toolbar();
        const auto count = static_cast<int>(actions.size());
        for (int i = 0; i < count; ++i) {
            const auto left = 40 + 880 * i / count, right = 40 + 880 * (i + 1) / count;
            panel(left + 3,488,right - left - 6,48);
            const auto width = fonts_.width(reader::FontRole::Interface, 16, actions[i].label);
            const auto center = (left + right) / 2;
            button_hint(center - width / 2 - 17,512,actions[i]);
            text(center - width / 2 + 4,518,palette().text,0.8f,actions[i].label,right - left - 44);
        }
    }
    void draw_menu() const {
        vita2d_draw_rectangle(0,0,960,544,0xb8000000u);
        panel(194,76,572,398);
        text(226,111,palette().text,1.05f,menu_title_,508,reader::FontRole::Heading);
        for (std::size_t i=0;i<menu_.size();++i) {
            const auto y = reader::ui::menu_top + static_cast<int>(i) * reader::ui::menu_pitch;
            if (i == menu_selected_) {
                vita2d_draw_rectangle(210,y,540,44,palette().background);
                vita2d_draw_rectangle(210,y+8,3,28,palette().accent);
            }
            button_hint(236,y+23,menu_[i]);
            text(260,y+29,palette().text,0.9f,menu_[i].label,468);
        }
        text(226,455,palette().muted,0.7f,"Up/down: choose    Cross: confirm    Circle: close",508);
    }
    std::vector<reader::Line> feedback_rows() const {
        return reader::wrap_text(message_,820,[this](const std::string& s) {
            return fonts_.width(reader::FontRole::Interface,16,s);
        });
    }
    int feedback_bottom() const { return screen_ == Screen::Reader && immersive_ && !online_busy_ ? 506 : 480; }
    int feedback_top() const {
        return feedback_bottom() - static_cast<int>(std::min<std::size_t>(feedback_rows().size(),4)) * 20 - (online_busy_ ? 68 : 18);
    }
    void draw_feedback() const {
        if (message_.empty()) return;
        const auto rows = feedback_rows();
        const auto count = std::min<std::size_t>(rows.size(),4);
        const int height = static_cast<int>(count) * 20 + (online_busy_ ? 68 : 18);
        const int bottom = feedback_bottom();
        panel(48,bottom-height,864,height,true);
        for (std::size_t i=0;i<count;++i) text(68,bottom-height+25+static_cast<int>(i)*20,palette().text,0.8f,rows[i].text,820);
        if (online_busy_) {
            const auto progress_y = bottom-34;
            vita2d_draw_rectangle(68,progress_y,820,4,palette().background);
            if (download_total_) vita2d_draw_rectangle(68,progress_y,820.0f * download_completed_ / download_total_,4,palette().accent);
            else {
                const auto phase = (sceKernelGetProcessTimeWide() / 4000) % 680;
                vita2d_draw_rectangle(68 + phase,progress_y,140,4,palette().accent);
            }
            text(68,bottom-10,palette().muted,0.65f,download_total_ ? std::to_string(download_completed_) + " / " +
                std::to_string(download_total_) + " completed  /  Circle: cancel" : "Working  /  Circle: cancel",820);
        }
    }
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

    std::size_t visible_end() const { return reader::layout_page_end(lines_, top_, reading_area().height); }

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
        order_ = reader::library_order(entries_, sort_, importing_ ? reader::LibraryFilter::All : filter_);
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

    void show_illustration(std::size_t index, Screen return_to) {
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
            image_return_ = return_to; screen_ = Screen::Image; message_.clear();
        } catch (const std::exception&) { message_ = "Not enough memory for this illustration."; }
    }
    const Palette& palette() const { return palettes[settings_.theme]; }
    unsigned reading_pixels() const { return 22 + settings_.font * 4; }
    reader::FontRole reading_font() const {
        return settings_.reading_font == 0 ? reader::FontRole::Serif :
            settings_.reading_font == 1 ? reader::FontRole::Sans : reader::FontRole::System;
    }
    int line_height() const { return 26 + settings_.font * 5 + settings_.spacing * 4; }

    std::string fit(std::string text, float scale, int width, reader::FontRole role) const {
        const auto pixels = static_cast<unsigned>(std::lround(20 * scale));
        if (fonts_.width(role, pixels, text) <= width) return text;
        while (!text.empty() && fonts_.width(role, pixels, text + "...") > width) {
            auto last = text.size() - 1;
            while (last > 0 && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80) --last;
            text.resize(last);
        }
        return text + "...";
    }

    void text(int x, int y, unsigned color, float scale, const std::string& value, int width = 864,
              reader::FontRole role = reader::FontRole::Interface) const {
        const auto clipped = fit(value, scale, width, role);
        fonts_.draw(role, static_cast<unsigned>(std::lround(20 * scale)), x, y, color, clipped);
    }

    void heading(const std::string& title, const std::string& subtitle) const {
        text(48, 43, palette().text, 1.2f, title, 864, reader::FontRole::Heading);
        text(48, 70, palette().muted, 0.75f, subtitle);
        vita2d_draw_rectangle(48, 82, 864, 1, palette().panel);
    }

    void browse_folder(const std::string& folder, const std::string& restore = {}) {
        release_image(); entries_.clear(); order_.clear(); selected_ = 0;
        current_folder_ = folder;
        ++folder_revision_;
        scan(restore);
    }

    bool add_to_library(const std::string& filename) {
        if (!library_writable_) { message_ = "Library index is unreadable. Restore library.txt before adding books."; return false; }
        if (std::find(library_books_.begin(), library_books_.end(), filename) != library_books_.end()) {
            notify("Already in your library."); return true;
        }
        auto updated = library_books_;
        updated.push_back(filename);
        if (!reader::save_library(library_path, updated)) { message_ = "Could not save library. Check free storage."; return false; }
        library_books_ = std::move(updated);
        notify("Added to library.");
        return true;
    }

    void scan(const std::string& restore = {}) {
        const auto keep = restore.empty() ? selected_filename() : restore;
        message_ = importing_ ? "Scanning import folder..." : "Loading library..."; draw();
        release_image();
        entries_.clear();
        order_.clear();
        message_.clear();
        if (importing_) {
            const auto folder_path = books_directory + (current_folder_.empty() ? "" : '/' + current_folder_);
            const auto directory = sceIoDopen(folder_path.c_str());
            if (directory >= 0) {
                SceIoDirent entry{};
                int result = 0;
                while ((result = sceIoDread(directory, &entry)) > 0) {
                    const std::string name(entry.d_name);
                    const bool is_folder = SCE_S_ISDIR(entry.d_stat.st_mode);
                    if (reader::valid_library_component(name) && (is_folder ||
                        (SCE_S_ISREG(entry.d_stat.st_mode) && reader::is_supported_book(name)))) {
                        reader::LibraryItem item;
                        try {
                            item.filename = reader::child_library_folder(current_folder_, name);
                            item.title = is_folder ? name : reader::title_from_filename(name);
                            item.path = books_directory + '/' + item.filename;
                            item.directory = is_folder;
                            entries_.push_back(std::move(item));
                        } catch (const std::invalid_argument&) { message_ = "Some paths exceed the supported folder length."; }
                    }
                    entry = {};
                }
                if (result < 0) message_ = "Could not read the entire books folder.";
                sceIoDclose(directory);
            } else message_ = "Cannot open this folder. Go Up or refresh after checking storage.";
        } else {
            for (const auto& filename : library_books_) {
                reader::LibraryItem item;
                item.filename = filename;
                item.path = books_directory + '/' + filename;
                item.title = reader::title_from_filename(filename.substr(filename.find_last_of('/') + 1));
                SceIoStat state{};
                if (sceIoGetstat(item.path.c_str(), &state) < 0 || !SCE_S_ISREG(state.st_mode))
                    item.error = "File missing. Restore it to its imported location.";
                entries_.push_back(std::move(item));
            }
            if (!library_writable_) message_ = "Library index is unreadable. Restore library.txt from backup.";
        }
        // Bundled guide is always available without modifying the user's library.
        if (!importing_) {
            reader::LibraryItem guide;
            guide.filename = "@welcome"; guide.title = "Welcome to Novel Reader"; guide.path = "app0:/welcome.txt"; guide.guide = true;
            entries_.push_back(std::move(guide));
        }
        for (auto& item : entries_) {
            if (item.directory) continue;
            item.progress = reader::load_progress(reader::progress_path(progress_directory, item.filename));
            if (item.error.empty() && (reader::is_epub_book(item.filename) || reader::is_online_book(item.filename))) {
                reader::Publication metadata;
                if (reader::open_publication(item.path, metadata, item.error)) {
                    if (metadata.online) reader::online::restore_progress(metadata,item.progress);
                    item.title = metadata.title; item.author = metadata.author;
                    item.cover_path = metadata.cover_path; item.chapters = metadata.chapters.size();
                }
            }
        }
        refresh_library(keep);
        if (!library_initialized_ && !importing_) {
            const auto recent = reader::ui::recent_book(entries_);
            if (recent < entries_.size()) refresh_library(entries_[recent].filename);
            library_initialized_ = true;
        }
        update_cover();
    }

    void open() {
        const auto entry_index = order_[selected_];
        if (entries_[entry_index].directory) {
            const auto folder = entries_[entry_index].filename;
            browse_folder(folder);
            return;
        }
        if (importing_) {
            const auto filename = entries_[entry_index].filename;
            if (add_to_library(filename)) {
                importing_ = false; current_folder_.clear();
                filter_ = reader::LibraryFilter::All; save_library_preferences();
                scan(filename); if (message_.empty()) notify("Added to library.");
            }
            return;
        }
        if (reader::is_online_book(entries_[entry_index].path)) {
            if (reader::online::load_novel(entries_[entry_index].path, online_novel_, message_)) {
                online_manifest_ = entries_[entry_index].path;
                online_source_ = online_novel_.source;
                online_from_library_ = true;
                refresh_online_summary();
                online_selected_ = online_saved_chapter_; screen_ = Screen::Novel; release_image();
            }
            return;
        }
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
        if (publication_.online && index < online_novel_.chapters.size() && !reader::online::cached(online_manifest_,online_novel_.chapters[index].url)) {
            pending_offset_ = offset; pending_end_ = at_end; online_read_in_place_ = true;
            begin_online_read(index); return false;
        }
        message_ = "Opening chapter...";
        draw();
        try {
            reader::Book candidate;
            std::string error;
            if (!reader::load_chapter(publication_, index, candidate, error)) { message_ = error; return false; }
            probe_illustrations(candidate);
            auto layout = reader::layout_book(candidate, 864, reading_area().height, line_height(), [this](const std::string& value) {
                return fonts_.width(reading_font(), reading_pixels(), value);
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
        auto layout = reader::layout_book(book_, 864, reading_area().height, line_height(), [this](const std::string& value) {
            return fonts_.width(reading_font(), reading_pixels(), value);
        });
        lines_ = std::move(layout.blocks); pages_ = std::move(layout.pages);
        top_ = reader::block_at_offset(lines_, offset);
        inline_images_.clear();
    }

    void save() {
        if (active_filename_.empty() || lines_.empty()) return;
        progress_.offset = lines_[top_].offset;
        progress_.chapter = chapter_;
        if (publication_.online) {
            progress_.chapter_id = publication_.chapters[chapter_].path;
            if (progress_.has_bookmark) progress_.bookmark_id = publication_.chapters[progress_.bookmark_chapter].path;
        }
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
        const char* filters[] = {"All", "Reading", "Finished"};
        const auto slash = current_folder_.find_last_of('/');
        if (importing_) heading("Import / " + (current_folder_.empty() ? std::string("books") : current_folder_.substr(slash == std::string::npos ? 0 : slash + 1)),
            "Choose a book  /  Circle: parent folder  /  Triangle: refresh");
        else {
            text(48,39,palette().text,1.2f,"Your library",560,reader::FontRole::Heading);
            text(642,37,palette().muted,0.7f,"L/R: filters    L+R: More",278);
            for (int i=0;i<3;++i) {
                const int x = 40 + i * 190;
                if (static_cast<int>(filter_) == i) panel(x,56,184,36);
                text(x+16,80,static_cast<int>(filter_) == i ? palette().accent : palette().muted,0.8f,filters[i],156);
                if (static_cast<int>(filter_) == i) vita2d_draw_rectangle(x+16,88,36,2,palette().accent);
            }
            panel(642,56,278,36);
            button_hint(661,74,{"Sort",SCE_CTRL_SQUARE});
            text(681,80,palette().text,0.75f,"Sort: " + std::string(sorts[static_cast<int>(sort_)]),224);
        }
        const auto first = (selected_ / 6) * 6;
        for (std::size_t i = first; i < order_.size() && i < first + 6; ++i) {
            const auto& item = entries_[order_[i]];
            const auto y = reader::ui::library_top + static_cast<int>(i - first) * reader::ui::library_pitch;
            if (i == selected_) panel(40,y,570,53,true);
            text(60, y + 24, item.directory ? palette().accent : palette().text, 0.95f,
                 item.directory ? "> " + item.title : item.title, 525);
            std::string status = item.directory ? "Folder" : item.guide ? "Guide" : reader::is_online_book(item.filename) ? "Online novel" : reader::is_epub_book(item.filename) ? "EPUB" : "TXT";
            if (item.directory) status += "  /  Open to browse";
            else if (!item.error.empty()) status += "  /  Could not read metadata";
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
            text(60, 155, palette().text, 1.0f, importing_ ? "No supported books in this folder." : "No books in this filter.", 525);
            text(60, 190, palette().muted, 0.8f, "Use Import to add local books, or Online to find novels.", 525);
        } else {
            const auto& item = entries_[order_[selected_]];
            panel(642,104,278,246);
            if (image_) {
                const auto scale = fit_scale(254, 226);
                vita2d_draw_texture_scale(image_, 781 - vita2d_texture_get_width(image_) * scale / 2,
                    227 - vita2d_texture_get_height(image_) * scale / 2, scale, scale);
            } else if (item.directory) {
                vita2d_draw_rectangle(694, 182, 78, 22, palette().accent);
                vita2d_draw_rectangle(694, 200, 166, 104, palette().accent);
                vita2d_draw_rectangle(704, 214, 146, 80, palette().background);
            } else {
                vita2d_draw_rectangle(696,119,170,216,palette().background);
                vita2d_draw_rectangle(706,131,3,192,palette().accent);
                text(723,151,palette().accent,0.6f,item.guide ? "NOVEL READER" : "YOUR COLLECTION",130);
                const auto title = reader::wrap_text(item.title,126,[this](const std::string& s) { return fonts_.width(reader::FontRole::Heading,20,s); });
                for (std::size_t i=0;i<std::min<std::size_t>(title.size(),5);++i)
                    text(723,190+static_cast<int>(i)*24,palette().text,1.0f,title[i].text,126,reader::FontRole::Heading);
                vita2d_draw_rectangle(723,315,42,2,palette().accent);
            }
            text(644, 372, palette().text, 0.8f, item.directory ? "Folder" : item.author.empty() ? "Local library" : item.author, 268);
            text(644, 393, palette().muted, 0.65f, item.directory ? "CROSS or tap to open" : !item.error.empty() ? item.error :
                !cover_message_.empty() ? cover_message_ : reader::is_epub_book(item.filename) ?
                std::to_string(item.chapters) + " chapters" : reader::is_online_book(item.filename) ? std::to_string(item.chapters) + " chapters / online" : "UTF-8 text", 268);
        }
        if (!importing_) {
            const auto recent = reader::ui::recent_book(entries_);
            panel(642,402,278,64,true);
            if (recent < entries_.size()) {
                const auto& item = entries_[recent];
                text(658,425,palette().text,0.8f,"Resume: " + item.title,244,reader::FontRole::Heading);
                text(658,449,palette().muted,0.65f,"Chapter " + std::to_string(item.progress.chapter+1) +
                    (item.progress.percent >= 0 ? "  /  " + std::to_string(item.progress.percent) + "% read" : "  /  Continue reading"),244);
            } else {
                text(658,428,palette().text,0.85f,"Your next chapter awaits",244);
                text(658,449,palette().muted,0.65f,"Open a book to begin",244);
            }
        }
        text(48,470,palette().muted,0.7f,std::to_string(order_.size()) + " books  /  " +
            (order_.empty() ? std::string("0") : std::to_string(selected_+1)) + " selected",560);
    }

    void draw_reader() const {
        const auto subtitle = (publication_.epub || publication_.online) ?
            std::to_string(chapter_ + 1) + "/" + std::to_string(publication_.chapters.size()) + "  " +
            publication_.chapters[chapter_].title + (publication_.author.empty() ? "" : "  /  " + publication_.author) : "LOCAL BOOK";
        if (!immersive_) heading(publication_.title, subtitle);
        const auto area = reading_area();
        int y = area.top;
        vita2d_enable_clipping();
        vita2d_set_clip_rectangle(48,area.top,912,area.top+area.height);
        const auto end = visible_end();
        for (auto i = top_; i < end; ++i) {
            if (lines_[i].illustration == reader::no_illustration) draw_line(lines_[i], y + line_height() - 8);
            else draw_inline_image(lines_[i], y);
            y += lines_[i].height;
        }
        vita2d_disable_clipping();
        const auto percent = end == lines_.size() ? 100 : static_cast<int>(100 * lines_[top_].offset / std::max<std::size_t>(1, book_.text.size()));
        const auto bar_y = immersive_ ? 510 : 452;
        vita2d_draw_rectangle(48,bar_y,864,2,palette().panel);
        vita2d_draw_rectangle(48,bar_y,864.0f * percent / 100.0f,2,palette().accent);
        const auto chapter_label = publication_.epub || publication_.online ? "Ch. " + std::to_string(chapter_ + 1) +
            "/" + std::to_string(publication_.chapters.size()) + "  /  " : "";
        text(48,bar_y+23,palette().muted,0.7f,chapter_label + std::to_string(percent) +
            (publication_.epub || publication_.online ? "% of chapter" : "% read"),420);
        text(516,bar_y+23,palette().muted,0.7f,"Page " +
            std::to_string(std::upper_bound(pages_.begin(), pages_.end(), top_) - pages_.begin()) + "/" + std::to_string(pages_.size()) +
            (immersive_ ? "  /  Tap center: controls" : "  /  L+R: More"),396);
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
            fonts_.draw(reading_font(), reading_pixels(), x, y, color, run);
            if (style & reader::Strong) fonts_.draw(reading_font(), reading_pixels(), x + 1, y, color, run);
            const auto width = fonts_.width(reading_font(), reading_pixels(), run);
            if (style & reader::Emphasis) vita2d_draw_rectangle(x, y + 3, width, 1, color);
            x += width; at = end;
        }
    }

    void draw_contents() const {
        heading("Contents", publication_.title);
        const auto first = (contents_selected_ / 6) * 6;
        for (auto i = first; i < publication_.chapters.size() && i < first + 6; ++i) {
            const auto y = 100 + static_cast<int>(i - first) * 53;
            if (i == contents_selected_) panel(40,y,880,49,true);
            text(60, y + 30, i == chapter_ ? palette().accent : palette().text, 1.0f,
                 std::to_string(i + 1) + ". " + publication_.chapters[i].title,690);
            if (i == chapter_) text(766,y+29,palette().accent,0.7f,"Reading",130);
        }
        text(48, 480, palette().muted, 0.75f,
             std::to_string(contents_selected_ + 1) + " / " + std::to_string(publication_.chapters.size()) + " chapters  /  Triangle: jump to chapter");
    }

    void draw_illustrations() const {
        heading("Illustrations", "Current chapter: " + publication_.chapters[chapter_].title);
        const auto first = (illustration_selected_ / 6) * 6;
        for (auto i = first; i < book_.illustrations.size() && i < first + 6; ++i) {
            const auto y = 100 + static_cast<int>(i - first) * 53;
            if (i == illustration_selected_) panel(40,y,880,49,true);
            text(60, y + 30, palette().text, 1.0f, std::to_string(i + 1) + ". " + book_.illustrations[i].label);
        }
        if (book_.illustrations.empty()) text(60, 155, palette().muted, 1.0f, "This chapter has no illustrations.");
        text(48, 480, palette().muted, 0.7f, "Tap an image to view. PNG and JPEG supported.");
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
        text(48,477,palette().muted,0.7f,std::to_string(static_cast<int>(zoom_ * 100)) + "% of fit size  /  L+R: zoom actions");
    }

    void draw_settings() const {
        heading("Make yourself comfortable", "Up/down: choose  /  Left/right: adjust  /  Cross or Circle: apply");
        const char* sizes[] = {"Small", "Medium", "Large"};
        const char* spacings[] = {"Compact", "Normal", "Relaxed"};
        const char* themes[] = {"Night", "Paper", "Sepia"};
        const char* families[] = {"Literata", "Atkinson Next", "Vita system"};
        const char* labels[] = {"Text size", "Reading font", "Line spacing", "Theme"};
        const std::string rows[] = {
            sizes[settings_.font], families[settings_.reading_font], spacings[settings_.spacing], themes[settings_.theme]
        };
        for (int i = 0; i < 4; ++i) {
            const auto y = 104 + i * 78;
            panel(40,y,450,72,setting_row_ == i);
            text(110,y+24,palette().muted,0.7f,labels[i],260);
            text(62,y+47,palette().accent,1.1f,"-",30);
            text(420,y+47,palette().accent,1.1f,"+",30);
            if (i < 3) text(110,y+54,palette().text,1.0f,rows[i],275);
            else for (int theme=0;theme<3;++theme) {
                const int x = 110 + theme * 90;
                vita2d_draw_rectangle(x,y+32,82,30,palettes[theme].background);
                text(x+6,y+53,palettes[theme].text,0.7f,themes[theme],72);
                if (theme == settings_.theme) vita2d_draw_rectangle(x,y+63,82,2,palette().accent);
            }
        }
        panel(518,104,402,338);
        text(538,135,palette().muted,0.7f,"LIVE PREVIEW",362);
        text(538,174,palette().accent,1.0f,"One more chapter",362,reader::FontRole::Heading);
        const auto preview = reader::wrap_text("A quiet evening. The next adventure waits between the lines. Outside, the last light fades. You settle in and turn the page.",350,
            [this](const std::string& s) { return fonts_.width(reading_font(),reading_pixels(),s); });
        int y = 211;
        for (const auto& line : preview) {
            if (y > 420) break;
            fonts_.draw(reading_font(),reading_pixels(),538,y,palette().text,line.text);
            y += line_height();
        }
        text(48,470,palette().muted,0.7f,"Your reading position and bookmark are preserved.");
    }

    reader::VitaFonts fonts_;
    bool immersive_ = true, library_initialized_ = false;
    std::vector<Action> menu_;
    std::string menu_title_, notice_;
    std::size_t menu_selected_ = 0;
    std::uint64_t notice_until_ = 0;
    bool ime_jump_ = false, retry_available_ = false, retry_in_place_ = false, online_started_ = false;
    reader::online::Request retry_request_;
    std::vector<bool> offline_chapters_;
    std::size_t offline_count_ = 0, online_saved_chapter_ = 0, download_completed_ = 0, download_total_ = 0;
    reader::online::Service online_{"app0:",books_directory+"/Online"};
    reader::online::Result online_catalog_;
    reader::online::Novel online_novel_;
    std::string online_source_,online_manifest_,online_browse_url_,online_query_;
    std::size_t online_selected_ = 0,pending_offset_ = 0;
    bool online_busy_ = false,online_read_in_place_ = false,pending_end_ = false,ime_active_ = false,online_from_library_ = false;
    SceWChar16 ime_input_[65]{},ime_initial_[1]{};
    reader::Settings settings_;
    reader::Settings previous_settings_;
    reader::Book book_;
    reader::Publication publication_;
    reader::Progress progress_;
    std::vector<reader::LibraryItem> entries_;
    std::vector<std::string> library_books_;
    bool importing_ = false, library_writable_ = true;
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
    std::string current_folder_;
    std::size_t folder_revision_ = 0;
    std::size_t selected_ = 0, top_ = 0;
    std::size_t chapter_ = 0, contents_selected_ = 0;
    int setting_row_ = 0;
    Screen screen_ = Screen::Library;
    Screen image_return_ = Screen::Illustrations;
};
} // namespace

int main() {
    SceAppUtilInitParam app_init{}; SceAppUtilBootParam app_boot{};
    sceAppUtilInit(&app_init,&app_boot);
    SceCommonDialogConfigParam dialog_config{}; sceCommonDialogSetConfigParam(&dialog_config);
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(root.c_str(), 0777);
    sceIoMkdir(books_directory.c_str(), 0777);
    sceIoMkdir(progress_directory.c_str(), 0777);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    const bool touch_enabled = sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START) >= 0;
    SceTouchPanelInfo panel{};
    if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &panel) < 0 || panel.maxDispX <= panel.minDispX || panel.maxDispY <= panel.minDispY) {
        panel.minDispX = panel.minDispY = 0; panel.maxDispX = 1919; panel.maxDispY = 1087;
    }
    if (vita2d_init() < 0) { sceKernelExitProcess(1); return 1; }
    vita2d_set_vblank_wait(1);
    auto* font = vita2d_load_default_pgf();
    if (!font) { vita2d_fini(); sceKernelExitProcess(1); return 1; }

    {
        App app(font);
        reader::controls::Repeat repeat;
        reader::controls::Stick left_stick, right_stick;
        reader::controls::Touch touch;
        std::vector<reader::controls::Point> points;
        points.reserve(SCE_TOUCH_MAX_REPORT);
        std::uint64_t previous_time = 0;
        constexpr unsigned repeatable = SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT |
                                        SCE_CTRL_RIGHT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;
        bool running = true, shoulder_chord = false;
        while (running) {
            SceCtrlData pad{};
            pad.lx = pad.ly = pad.rx = pad.ry = 128;
            const bool pad_ready = sceCtrlPeekBufferPositive(0, &pad, 1) > 0;
            const auto now = sceKernelGetProcessTimeWide();
            const auto seconds = previous_time && now >= previous_time ? std::min<std::uint64_t>(now - previous_time, 50000) / 1000000.0f : 0;
            previous_time = now;
            const auto before = app.screen();
            const auto folder_before = app.folder_revision();
            const auto left = left_stick.direction(pad.lx, pad.ly), right = right_stick.direction(pad.rx, pad.ry);
            const auto buttons = [](unsigned directions) {
                unsigned result = 0;
                if (directions & reader::controls::Left) result |= SCE_CTRL_LEFT;
                if (directions & reader::controls::Right) result |= SCE_CTRL_RIGHT;
                if (directions & reader::controls::Up) result |= SCE_CTRL_UP;
                if (directions & reader::controls::Down) result |= SCE_CTRL_DOWN;
                return result;
            };
            const bool image_pan = app.can_pan_image();
            auto held = pad.buttons | (image_pan ? 0 : buttons(left | right));
            if ((held & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) == (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) held &= ~(SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
            if ((held & (SCE_CTRL_UP | SCE_CTRL_DOWN)) == (SCE_CTRL_UP | SCE_CTRL_DOWN)) held &= ~(SCE_CTRL_UP | SCE_CTRL_DOWN);
            auto pressed = pad_ready ? repeat.sample(held, repeatable, now) : 0;
            const unsigned shoulders = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;
            if ((held & shoulders) == shoulders) {
                if (!shoulder_chord && (pressed & shoulders)) pressed = shoulders;
                else pressed &= ~shoulders;
                shoulder_chord = true;
            } else shoulder_chord = false;
            reader::controls::Gesture gesture;
            if (touch_enabled) {
                SceTouchData data{};
                if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &data, 1) > 0 && !pressed) {
                    points.clear();
                    for (unsigned i = 0; i < std::min<unsigned>(data.reportNum, SCE_TOUCH_MAX_REPORT); ++i)
                        points.push_back({data.report[i].id,
                            reader::controls::display_coordinate(data.report[i].x, panel.minDispX, panel.maxDispX, 960),
                            reader::controls::display_coordinate(data.report[i].y, panel.minDispY, panel.maxDispY, 544)});
                    gesture = touch.sample(points, now, before == Screen::Image);
                } else touch.cancel_until_release();
            }
            running = app.input(pressed);
            if (!running) break;
            if (image_pan && app.can_pan_image() && pad_ready) {
                const auto x = (left ? reader::controls::axis(pad.lx) : 0) + (right ? reader::controls::axis(pad.rx) : 0);
                const auto y = (left ? reader::controls::axis(pad.ly) : 0) + (right ? reader::controls::axis(pad.ry) : 0);
                app.pan_image(-std::max(-1.0f, std::min(1.0f, x)) * 480 * seconds,
                              -std::max(-1.0f, std::min(1.0f, y)) * 480 * seconds);
            }
            if (app.screen() == before && app.folder_revision() == folder_before) running = app.gesture(gesture);
            if (app.screen() != before || app.folder_revision() != folder_before) {
                left_stick.cancel_until_neutral(); right_stick.cancel_until_neutral(); touch.cancel_until_release();
            }
            app.draw();
        }
    }
    vita2d_free_pgf(font);
    vita2d_fini();
    sceAppUtilShutdown();
    if (touch_enabled) sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_STOP);
    sceKernelExitProcess(0);
    return 0;
}
