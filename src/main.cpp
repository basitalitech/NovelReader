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
const Palette menu_palette = {rgb(24,24,26), rgb(48,48,51), rgb(248,248,246), rgb(204,204,201), rgb(188,234,213)};

constexpr std::size_t list_page = reader::ui::page_size;

enum class Screen { Library, Book, Reader, Settings, Contents, Illustrations, Image, Sources, Catalog, Novel, OnlineChapters };
enum class Command { Button, More, Sort, Jump, Immersive, Resume, Retry, Appearance, Details, DownloadAll, SortTitle, SortRecent, SortProgress };
struct Action {
    std::string label;
    unsigned button;
    Command command;
    bool enabled;
    Action(std::string name, unsigned key = 0, Command action = Command::Button, bool available = true)
        : label(std::move(name)), button(key), command(action), enabled(available) {}
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
        immersive_ = settings_.immersive != 0;
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
        else if (screen_ == Screen::Book) draw_book();
        else if (screen_ == Screen::Reader) draw_reader();
        else if (screen_ == Screen::Contents) draw_contents();
        else if (screen_ == Screen::Illustrations) draw_illustrations();
        else if (screen_ == Screen::Image) draw_image();
        else draw_settings();
        interface_layer_ = true;
        draw_toolbar();
        draw_feedback();
        if (!menu_.empty()) draw_menu();
        interface_layer_ = false;
        vita2d_end_drawing();
        if (ime_active_) vita2d_common_dialog_update();
        vita2d_swap_buffers();
        vita2d_wait_rendering_done();
    }

    bool input(unsigned pressed, bool physical = true) {
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
            if ((pressed & SCE_CTRL_LTRIGGER) && menu_selected_>=6) menu_selected_=menu_selected_/6*6-6;
            if ((pressed & SCE_CTRL_RTRIGGER) && (menu_selected_/6+1)*6<menu_.size()) menu_selected_=(menu_selected_/6+1)*6;
            if ((pressed & SCE_CTRL_LEFT) && menu_selected_) --menu_selected_;
            if ((pressed & SCE_CTRL_RIGHT) && menu_selected_ + 1 < menu_.size()) ++menu_selected_;
            if ((pressed & SCE_CTRL_UP) && menu_selected_ >= 2) menu_selected_ -= 2;
            if ((pressed & SCE_CTRL_DOWN) && menu_selected_ + 2 < menu_.size()) menu_selected_ += 2;
            if ((pressed & SCE_CTRL_CROSS) && menu_[menu_selected_].enabled) { const auto action = menu_[menu_selected_]; close_menu(); return perform(action); }
            return true;
        }
        if ((pressed & SCE_CTRL_LTRIGGER) && (pressed & SCE_CTRL_RTRIGGER)) { show_more(); return true; }
        if (physical && (pressed & SCE_CTRL_TRIANGLE) && screen_ != Screen::Settings) { show_more(); return true; }
        if (screen_ == Screen::Book) {
            if (pressed & SCE_CTRL_CIRCLE) { screen_ = Screen::Library; message_.clear(); }
            else if (pressed & SCE_CTRL_CROSS) { open(); if (screen_ == Screen::Novel) resume_online(); }
            return true;
        }
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
            if ((pressed & SCE_CTRL_LEFT) && !order_.empty()) selected_ = selected_ > list_page ? selected_ - list_page : 0;
            if ((pressed & SCE_CTRL_RIGHT) && !order_.empty()) selected_ = std::min(selected_ + list_page, order_.size() - 1);
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
            else if (pressed & SCE_CTRL_TRIANGLE) show_more();
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
            if (pressed & SCE_CTRL_LEFT) contents_selected_ = contents_selected_ > list_page ? contents_selected_ - list_page : 0;
            if (pressed & SCE_CTRL_RIGHT) contents_selected_ = std::min(contents_selected_ + list_page, publication_.chapters.size() - 1);
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
            if ((pressed & SCE_CTRL_LEFT) && !book_.illustrations.empty()) illustration_selected_ = illustration_selected_ > list_page ? illustration_selected_ - list_page : 0;
            if ((pressed & SCE_CTRL_RIGHT) && !book_.illustrations.empty()) illustration_selected_ = std::min(illustration_selected_ + list_page, book_.illustrations.size() - 1);
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
            if (pressed & SCE_CTRL_LTRIGGER) setting_row_ = 0;
            if (pressed & SCE_CTRL_RTRIGGER) setting_row_ = 3;
            if ((pressed & SCE_CTRL_UP) && setting_row_ > 0) --setting_row_;
            if ((pressed & SCE_CTRL_DOWN) && setting_row_ < 4) ++setting_row_;
            int* value = setting_row_ == 0 ? &settings_.font :
                         setting_row_ == 1 ? &settings_.reading_font :
                         setting_row_ == 2 ? &settings_.spacing : setting_row_ == 3 ? &settings_.theme : &settings_.immersive;
            if (pressed & SCE_CTRL_LEFT) *value = std::max(0, *value - 1);
            if (pressed & SCE_CTRL_RIGHT) *value = std::min(setting_row_ == 4 ? 1 : 2, *value + 1);
            if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE | SCE_CTRL_CROSS)) {
                const auto offset = lines_[top_].offset;
                immersive_ = settings_.immersive != 0;
                try { reflow(offset); }
                catch (const std::exception&) {
                    settings_ = previous_settings_;
                    immersive_ = settings_.immersive != 0;
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
        pan_y_ = gesture.y - 272 - (gesture.y - 272 - pan_y_) * ratio;
        zoom_ = next; clamp_pan();
    }

    bool gesture(const reader::controls::Gesture& gesture) {
        using reader::controls::GestureKind;
        if (gesture.kind == GestureKind::None || ime_active_) return true;
        const int x = static_cast<int>(gesture.x), y = static_cast<int>(gesture.y);
        if (!menu_.empty()) {
            if (gesture.kind != GestureKind::Tap) return true;
            const auto first = static_cast<int>(menu_selected_ / 6 * 6);
            const auto item = reader::ui::menu_hit(x,y,static_cast<int>(menu_.size()),first);
            if (item >= 0) { const auto action = menu_[item]; if (!action.enabled) return true; close_menu(); return perform(action); }
            const int footer=reader::controls::toolbar_hit(x,y,menu_.size()>6?3:1);
            if (footer>=0) {
                if (menu_.size()<=6 || footer==1) close_menu();
                else if (footer==0 && first>=6) menu_selected_=first-6;
                else if (footer==2 && first+6<static_cast<int>(menu_.size())) menu_selected_=first+6;
            }
            return true;
        }
        if (gesture.kind == GestureKind::Tap) {
            const auto actions = toolbar();
            const auto hit = reader::controls::toolbar_hit(x,y,static_cast<int>(actions.size()));
            if (hit >= 0) return perform(actions[hit]);
            if (!message_.empty() && x >= 24 && x < 936 && y >= feedback_top() && y < feedback_bottom()) {
                if (retry_available_) return perform({"Retry",0,Command::Retry});
                if (message_ == notice_) message_.clear();
                return true;
            }
        }
        if (online_busy_) return true;
        if (gesture.kind == GestureKind::Tap && screen_ != Screen::Library && screen_ != Screen::Settings &&
            screen_ != Screen::Reader && x >= 816 && y < 72) { show_more(); return true; }
        if (gesture.kind == GestureKind::Transform) {
            if (screen_ == Screen::Image && y >= 104 && y < 440) transform_image(gesture);
            return true;
        }
        if (gesture.kind == GestureKind::Hold) {
            if (screen_ == Screen::Reader && x >= 48 && x < 912 && y >= reading_area().top &&
                y < reading_area().top + reading_area().height) return input(SCE_CTRL_SQUARE);
            return true;
        }
        if (gesture.kind == GestureKind::Swipe) {
            const auto start_y = gesture.y - gesture.dy;
            if (start_y < (screen_ == Screen::Reader ? reading_area().top : 124) ||
                start_y >= (screen_ == Screen::Reader ? reading_area().top + reading_area().height : 444)) return true;
            const bool horizontal = std::abs(gesture.dx) > std::abs(gesture.dy);
            const bool forward = horizontal ? gesture.dx < 0 : gesture.dy < 0;
            if (screen_ == Screen::Reader) {
                if (horizontal) return input(forward ? SCE_CTRL_RIGHT : SCE_CTRL_LEFT);
                const auto amount = static_cast<std::size_t>(std::max(1,std::min(6,static_cast<int>(std::abs(gesture.dy)/40))));
                top_ = forward ? std::min(top_+amount,lines_.size()-1) : top_ > amount ? top_-amount : 0;
                message_.clear(); save();
            } else if (screen_ == Screen::Novel) {
                return input(forward ? SCE_CTRL_DOWN : SCE_CTRL_UP);
            } else if (screen_ == Screen::Settings) {
                return input(horizontal ? (forward ? SCE_CTRL_RIGHT : SCE_CTRL_LEFT) : (forward ? SCE_CTRL_DOWN : SCE_CTRL_UP));
            } else if (screen_ == Screen::Library || screen_ == Screen::Contents || screen_ == Screen::Illustrations ||
                       screen_ == Screen::Catalog || screen_ == Screen::OnlineChapters || screen_ == Screen::Sources) {
                return input(forward ? SCE_CTRL_RIGHT : SCE_CTRL_LEFT);
            }
            return true;
        }
        if (gesture.kind != GestureKind::Tap) return true;
        if (screen_ == Screen::Settings) {
            if (y >= 56 && y < 120 && x >= 24 && x < 936) { setting_row_ = x < 480 ? 0 : 3; return true; }
            if (setting_row_ >= 3 && x >= 548 && x < 920 && y >= 352 && y < 416) {
                settings_.theme = (x-548)/124; return true;
            }
            const int first = setting_row_ / 3 * 3;
            const int row = reader::controls::list_row_hit(x,y,24,512,128,100,92,first == 0 ? 3 : 2);
            if (row >= 0) {
                setting_row_ = first + row;
                if (x < 104) return input(SCE_CTRL_LEFT);
                if (x >= 432) return input(SCE_CTRL_RIGHT);
                // Tap the value to cycle it; +/- remain separate, generous targets.
                int* value = setting_row_ == 0 ? &settings_.font : setting_row_ == 1 ? &settings_.reading_font :
                    setting_row_ == 2 ? &settings_.spacing : setting_row_ == 3 ? &settings_.theme : &settings_.immersive;
                *value = (*value + 1) % (setting_row_ == 4 ? 2 : 3);
            }
            return true;
        }
        if (screen_ == Screen::Reader) {
            const auto area = reading_area();
            if (!immersive_ && y < 104) { show_more(); return true; }
            if (y < area.top || y >= area.top+area.height || x < 48 || x >= 912) return true;
            int block_y = area.top;
            for (auto at=top_;at<visible_end();++at) {
                const auto& block = lines_[at];
                if (block.illustration != reader::no_illustration && y >= block_y && y < block_y+block.height &&
                    (!block.image_width || std::abs(x-480) <= block.image_width/2)) {
                    show_illustration(block.illustration,Screen::Reader); return true;
                }
                block_y += block.height;
            }
            if (x >= 320 && x < 640) { show_more(); return true; }
            return input(x < 320 ? SCE_CTRL_LEFT : SCE_CTRL_RIGHT);
        }
        if (screen_ == Screen::Image) {
            if (x >= 48 && x < 912 && y >= 104 && y < 440) {
                auto zoom=gesture; zoom.scale=zoom_>1 ? 1/zoom_ : 2; transform_image(zoom);
            }
            return true;
        }
        if (screen_ == Screen::Novel) {
            if (reader::ui::Rect{24,352,404,80}.contains(x,y)) { download_online(true); return true; }
            if (reader::ui::Rect{440,352,496,80}.contains(x,y)) { refresh_online_novel(); return true; }
            return true;
        }
        if (screen_ == Screen::Book) return true;
        if (screen_ == Screen::Library && !importing_) {
            if (reader::ui::Rect{568,8,368,104}.contains(x,y)) { resume_recent(); return true; }
            if (y >= 56 && y < 120 && x >= 24 && x < 552) {
                filter_ = static_cast<reader::LibraryFilter>((x-24)/176);
                selected_=0; refresh_library(""); update_cover(); save_library_preferences(); return true;
            }
        }
        const bool list = screen_ == Screen::Library || screen_ == Screen::Contents || screen_ == Screen::Illustrations || is_online_screen();
        if (list) {
            const int row = reader::controls::list_row_hit(x,y,24,856,reader::ui::library_top,reader::ui::library_pitch,
                reader::ui::library_row_height,reader::ui::page_size);
            auto* selection = screen_ == Screen::Library ? &selected_ : screen_ == Screen::Contents ? &contents_selected_ :
                screen_ == Screen::Illustrations ? &illustration_selected_ : &online_selected_;
            const auto size = screen_ == Screen::Library ? order_.size() : screen_ == Screen::Contents ? publication_.chapters.size() :
                screen_ == Screen::Illustrations ? book_.illustrations.size() : online_size();
            if (reader::ui::Rect{872,140,64,104}.contains(x,y)) return *selection >= list_page ? input(SCE_CTRL_LEFT) : true;
            if (reader::ui::Rect{872,324,64,104}.contains(x,y)) return reader::ui::page_first(*selection)+list_page < size ? input(SCE_CTRL_RIGHT) : true;
            const auto index = reader::ui::page_first(*selection) + (row < 0 ? list_page : row);
            if (row >= 0 && index < size) {
                *selection=index;
                if (screen_ == Screen::Library) {
                    update_cover();
                    if (!importing_ && x >= 776) return perform({"Details",0,Command::Details});
                }
                return input(SCE_CTRL_CROSS);
            }
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
        menu_title_ = screen_ == Screen::Reader ? "Reading controls" : "Options";
        if (screen_ == Screen::Library && importing_) {
            menu_={{"Back",SCE_CTRL_CIRCLE},{"Refresh folder",SCE_CTRL_TRIANGLE},{"Sort files",0,Command::Sort},{"Save and exit",SCE_CTRL_START}};
        } else if (screen_ == Screen::Library) {
            menu_={{"Continue reading",0,Command::Resume},{"Book details",0,Command::Details},
                {"Import books",SCE_CTRL_TRIANGLE},{"Online novels",SCE_CTRL_SELECT},{"Sort books",0,Command::Sort},{"Save and exit",SCE_CTRL_START}};
        } else if (screen_ == Screen::Reader) {
            menu_={{"Contents",SCE_CTRL_CROSS},{"Appearance",0,Command::Appearance},{"Save bookmark",SCE_CTRL_SQUARE},
                {"Go to bookmark",SCE_CTRL_SELECT,Command::Button,progress_.has_bookmark},{publication_.online?"Back to chapters":"Back to library",SCE_CTRL_CIRCLE},
                {immersive_ ? "Show reading bar" : "Hide reading bar",0,Command::Immersive}};
        } else if (screen_ == Screen::Catalog) {
            menu_={{"Search",SCE_CTRL_SQUARE},{online_catalog_.next.empty()?"Reload catalog":"Next catalog page",SCE_CTRL_TRIANGLE},{"Initial catalog",SCE_CTRL_SELECT}};
        } else if (screen_ == Screen::Novel) {
            menu_={{online_started_?"Resume reading":"Start reading",SCE_CTRL_SELECT},{"Chapters",SCE_CTRL_CROSS},
                {in_library()?"In your library":"Add to library",SCE_CTRL_SQUARE,Command::Button,!in_library()},
                {"Download all",0,Command::DownloadAll},{"Refresh chapters",SCE_CTRL_TRIANGLE}};
        } else if (screen_ == Screen::OnlineChapters) {
            menu_={{"Read chapter",SCE_CTRL_CROSS},{"Download chapter",SCE_CTRL_SQUARE},{"Download all",SCE_CTRL_SELECT},
                {"Jump to chapter",0,Command::Jump},{"Refresh chapters",SCE_CTRL_TRIANGLE}};
        } else if (screen_ == Screen::Contents) {
            menu_={{"Jump to chapter",0,Command::Jump},{"Illustrations",SCE_CTRL_SQUARE},{"Back to reading",SCE_CTRL_CIRCLE}};
        } else if (screen_ == Screen::Image) {
            menu_={{"Zoom in",SCE_CTRL_CROSS},{"Zoom out",SCE_CTRL_SQUARE},{"Fit image",SCE_CTRL_TRIANGLE},
                {"Previous image",SCE_CTRL_LTRIGGER},{"Next image",SCE_CTRL_RTRIGGER},{"Back",SCE_CTRL_CIRCLE}};
        } else { menu_={{"Back",SCE_CTRL_CIRCLE}}; }
        if (retry_available_ && (is_online_screen() || screen_ == Screen::Reader)) menu_.insert(menu_.begin(),{"Retry request",0,Command::Retry});
        menu_selected_=0; ++folder_revision_;
    }
    bool perform(const Action& action) {
        if (!action.enabled) return true;
        pressed_action_ = action.label; pressed_until_ = sceKernelGetProcessTimeWide()+160000;
        switch (action.command) {
        case Command::More: show_more(); break;
        case Command::DownloadAll: download_online(true); break;
        case Command::Sort: show_sort(); break;
        case Command::Jump: jump_to_chapter(); break;
        case Command::Immersive: toggle_immersive(); break;
        case Command::Resume: resume_recent(); break;
        case Command::Details:
            if (!order_.empty()) {
                if (reader::is_online_book(selected_filename())) open();
                else { screen_=Screen::Book; update_cover(); ++folder_revision_; }
            }
            break;
        case Command::Appearance:
            setting_row_=0; previous_settings_=settings_; screen_=Screen::Settings; break;
        case Command::Retry: online_read_in_place_=retry_in_place_; start_online(retry_request_); break;
        case Command::SortTitle: case Command::SortRecent: case Command::SortProgress: {
            const auto keep=selected_filename();
            sort_=action.command==Command::SortTitle?reader::LibrarySort::Title:action.command==Command::SortRecent?reader::LibrarySort::Recent:reader::LibrarySort::Progress;
            refresh_library(keep); update_cover(); save_library_preferences(); break;
        }
        case Command::Button: return input(action.button,false);
        }
        return true;
    }
    void toggle_immersive() {
        const auto offset=lines_[top_].offset;
        immersive_=!immersive_;
        try { reflow(offset); }
        catch (const std::exception&) { immersive_=!immersive_; message_="Could not change reading view. Previous view kept."; }
        settings_.immersive=immersive_?1:0;
        if (!reader::save_settings(settings_path,settings_)) message_="Could not save reading view. Check storage.";
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
        if (online_busy_) return {{"Cancel",SCE_CTRL_CIRCLE}};
        if (screen_==Screen::Reader && immersive_) return {};
        if (screen_==Screen::Library) return importing_ ? std::vector<Action>{{"Back",SCE_CTRL_CIRCLE},{"Import",SCE_CTRL_CROSS},{"Refresh",SCE_CTRL_TRIANGLE}} :
            std::vector<Action>{{"Import",SCE_CTRL_TRIANGLE},{"Online",SCE_CTRL_SELECT},
                {sort_==reader::LibrarySort::Title?"Sort: Title":sort_==reader::LibrarySort::Recent?"Sort: Recent":"Sort: Progress",SCE_CTRL_SQUARE},{"Options",0,Command::More}};
        if (screen_==Screen::Book) return {{"Library",SCE_CTRL_CIRCLE},{"Read book",SCE_CTRL_CROSS,Command::Button,
            !order_.empty() && entries_[order_[selected_]].error.empty()}};
        if (screen_==Screen::Reader) return {{"Back",SCE_CTRL_CIRCLE},{"Contents",SCE_CTRL_CROSS},{"Bookmark",SCE_CTRL_SQUARE},{"Appearance",0,Command::Appearance}};
        if (screen_==Screen::Contents) return {{"Back",SCE_CTRL_CIRCLE},{"Open",SCE_CTRL_CROSS},{"Jump",0,Command::Jump},{"Images",SCE_CTRL_SQUARE}};
        if (screen_==Screen::Illustrations) return {{"Back",SCE_CTRL_CIRCLE},{"View image",SCE_CTRL_CROSS}};
        if (screen_==Screen::Settings) return {{"Decrease",SCE_CTRL_LEFT},{"Increase",SCE_CTRL_RIGHT},{"Apply",SCE_CTRL_CROSS}};
        if (screen_==Screen::Image) return {{"Back",SCE_CTRL_CIRCLE},{"Previous",SCE_CTRL_LTRIGGER},{"-",SCE_CTRL_SQUARE},
            {"Fit",SCE_CTRL_TRIANGLE},{"+",SCE_CTRL_CROSS},{"Next",SCE_CTRL_RTRIGGER}};
        if (screen_==Screen::Sources) return {{"Library",SCE_CTRL_CIRCLE},{"Browse",SCE_CTRL_CROSS}};
        if (screen_==Screen::Catalog) return {{"Back",SCE_CTRL_CIRCLE},{"Details",SCE_CTRL_CROSS},{"Search",SCE_CTRL_SQUARE},
            {online_catalog_.next.empty()?"Reload":"Next",SCE_CTRL_TRIANGLE}};
        if (screen_==Screen::Novel) return {{"Back",SCE_CTRL_CIRCLE},{online_started_?"Resume":"Read",SCE_CTRL_SELECT},
            {"Chapters",SCE_CTRL_CROSS},{in_library()?"Saved":"Save",SCE_CTRL_SQUARE,Command::Button,!in_library()}};
        return {{"Back",SCE_CTRL_CIRCLE},{"Read",SCE_CTRL_CROSS},{"Download",SCE_CTRL_SQUARE},{"Download all",SCE_CTRL_SELECT},{"Jump",0,Command::Jump}};
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
            message_ += "  Tap Retry or open Options."; return;
        }
        using reader::online::Action;
        if (update.request.action == Action::Browse) {
            online_catalog_ = std::move(update.result); online_selected_ = 0; screen_ = Screen::Catalog;
            online_browse_url_ = update.request.url; online_query_ = update.request.query;
            message_ = online_catalog_.items.empty() ? "No novels found. Try Search or another source." : "";
        } else if (update.request.action == Action::Chapters) {
            online_novel_ = std::move(update.novel); online_manifest_ = update.manifest;
            description_row_ = 0;
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
        if (screen_ == Screen::Novel) {
            if ((pressed & SCE_CTRL_UP) && description_row_) --description_row_;
            if ((pressed & SCE_CTRL_DOWN) && description_row_+5 < description_lines().size()) ++description_row_;
        }
        if (size && screen_ != Screen::Novel) {
            if ((pressed & SCE_CTRL_UP) && online_selected_) --online_selected_;
            if ((pressed & SCE_CTRL_DOWN) && online_selected_+1<size) ++online_selected_;
            if (pressed & SCE_CTRL_LEFT) online_selected_ = online_selected_>list_page ? online_selected_-list_page : 0;
            if (pressed & SCE_CTRL_RIGHT) online_selected_ = std::min(online_selected_+list_page,size-1);
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
    std::vector<reader::Line> description_lines() const {
        return reader::wrap_text(online_novel_.description.empty()?"No description provided.":online_novel_.description,912,
            [this](const std::string& s){return fonts_.width(reader::FontRole::Interface,22,s);});
    }
    void panel(int x,int y,int width,int height,bool selected=false) const {
        const int radius=12;
        const auto fill=selected?rgb(57,66,62):palette().panel;
        vita2d_draw_rectangle(x+radius,y,width-2*radius,height,fill);
        vita2d_draw_rectangle(x,y+radius,width,height-2*radius,fill);
        for (const auto cx:{x+radius,x+width-radius})
            for (const auto cy:{y+radius,y+height-radius}) vita2d_draw_fill_circle(cx,cy,radius,fill);
        if (selected) {
            for (int border=0;border<2;++border) {
                vita2d_draw_line(x+12,y+border,x+width-12,y+border,palette().accent);
                vita2d_draw_line(x+12,y+height-1-border,x+width-12,y+height-1-border,palette().accent);
                vita2d_draw_line(x+border,y+12,x+border,y+height-12,palette().accent);
                vita2d_draw_line(x+width-1-border,y+12,x+width-1-border,y+height-12,palette().accent);
            }
        } else vita2d_draw_line(x+12,y+1,x+width-12,y+1,rgb(72,72,75));
    }
    void button_hint(int x,int y,const Action& action) const {
        const auto c=action.enabled?palette().accent:palette().muted;
        if (action.button==SCE_CTRL_CROSS) {
            vita2d_draw_line(x-8,y-8,x+8,y+8,c); vita2d_draw_line(x-8,y+8,x+8,y-8,c);
        } else if (action.button==SCE_CTRL_CIRCLE) {
            for (int i=0;i<20;++i) {
                const float a=i*6.2831853f/20,b=(i+1)*6.2831853f/20;
                vita2d_draw_line(x+11*std::cos(a),y+11*std::sin(a),x+11*std::cos(b),y+11*std::sin(b),c);
            }
        } else if (action.button==SCE_CTRL_TRIANGLE) {
            vita2d_draw_line(x,y-11,x-11,y+9,c); vita2d_draw_line(x-11,y+9,x+11,y+9,c); vita2d_draw_line(x+11,y+9,x,y-11,c);
        } else if (action.button==SCE_CTRL_SQUARE) {
            vita2d_draw_line(x-9,y-9,x+9,y-9,c); vita2d_draw_line(x+9,y-9,x+9,y+9,c);
            vita2d_draw_line(x+9,y+9,x-9,y+9,c); vita2d_draw_line(x-9,y+9,x-9,y-9,c);
        } else if (action.button==SCE_CTRL_SELECT) {
            text(x-16,y+7,c,0.85f,"SEL",40);
        } else if (action.button==SCE_CTRL_START) {
            vita2d_draw_line(x-9,y-9,x-9,y+9,c); vita2d_draw_line(x-9,y+9,x+9,y,c); vita2d_draw_line(x+9,y,x-9,y-9,c);
        }
    }
    void button(const reader::ui::Rect& box,const Action& action,bool focused=false) const {
        panel(box.x,box.y,box.width,box.height,focused && action.enabled);
        const bool pressed=action.label==pressed_action_ && sceKernelGetProcessTimeWide()<pressed_until_;
        if (pressed && action.enabled) vita2d_draw_rectangle(box.x+6,box.y+6,box.width-12,box.height-12,palette().accent);
        auto c=!action.enabled?rgb(146,146,148):pressed?rgb(24,24,26):palette().text;
        const bool hint=action.button && box.width>=160 && action.button!=SCE_CTRL_LEFT && action.button!=SCE_CTRL_RIGHT &&
            action.button!=SCE_CTRL_LTRIGGER && action.button!=SCE_CTRL_RTRIGGER;
        if (hint && !pressed) button_hint(box.x+22,box.y+box.height/2,action);
        const int left=box.x+(hint?40:12),available=box.width-(hint?48:24);
        const auto lines=reader::wrap_text(action.label,available,[this](const std::string& s){return fonts_.width(reader::FontRole::Interface,26,s);});
        const auto count=std::min<std::size_t>(2,lines.size());
        for (std::size_t i=0;i<count;++i) {
            const auto& line=lines[i].text;
            const auto width=fonts_.width(reader::FontRole::Interface,26,line);
            text(left+(available-width)/2,box.y+box.height/2+9+(static_cast<int>(i)*30-static_cast<int>(count-1)*15),c,1.0f,line,available);
        }
    }
    void draw_pager(std::size_t selected,std::size_t count) const {
        const auto page=selected/list_page, pages=(count+list_page-1)/list_page;
        button({872,140,64,104},{"<",0,Command::Button,page>0});
        text(876,282,palette().text,0.85f,std::to_string(count?page+1:0),60);
        text(876,309,palette().muted,0.85f,"/"+std::to_string(pages),60);
        button({872,324,64,104},{">",0,Command::Button,page+1<pages});
    }
    void list_row(int y,const std::string& title,const std::string& subtitle,bool selected,bool reading=false) const {
        panel(24,y,832,76,selected);
        if (reading) vita2d_draw_fill_circle(42,y+24,5,palette().accent);
        text(56,y+31,palette().text,1.0f,title,780);
        text(56,y+62,reading?palette().accent:palette().muted,0.85f,subtitle,780);
    }
    void draw_online() const {
        const auto title=screen_==Screen::Sources?"Online novels":screen_==Screen::Catalog?"Browse / "+online_source_:online_novel_.title;
        heading(title,screen_==Screen::Novel?online_novel_.author:screen_==Screen::OnlineChapters?
            std::to_string(offline_count_)+" chapters available offline":screen_==Screen::Catalog && !online_query_.empty()?"Search: "+online_query_:"Choose your next read");
        if (screen_==Screen::Novel) {
            text(24,139,palette().accent,1.0f,std::to_string(online_novel_.chapters.size())+" chapters  /  "+
                std::to_string(offline_count_)+" offline  /  "+(in_library()?"In library":"Not saved"),912);
            const auto lines=description_lines();
            for (std::size_t i=description_row_;i<lines.size() && i<description_row_+5;++i)
                text(24,187+static_cast<int>(i-description_row_)*28,palette().text,0.85f,lines[i].text,912);
            if (lines.size()>5) text(24,338,palette().muted,0.85f,"Up/down: more description",912);
            button({24,352,404,80},{"Download all",0,Command::DownloadAll});
            button({440,352,496,80},{"Refresh chapters",SCE_CTRL_TRIANGLE});
            return;
        }
        const auto size=online_size(),first=reader::ui::page_first(online_selected_);
        for (auto i=first;i<size && i<first+list_page;++i) {
            const auto y=reader::ui::library_top+static_cast<int>(i-first)*reader::ui::library_pitch;
            std::string label,sub; bool reading=false;
            if (screen_==Screen::Sources) {label=reader::online::sources()[i].name;sub="Browse public novels";}
            else if (screen_==Screen::Catalog) {label=online_catalog_.items[i].title;sub="View details and chapters";}
            else {
                const auto& chapter=online_novel_.chapters[i];label=std::to_string(i+1)+". "+chapter.title;
                sub=i<offline_chapters_.size() && offline_chapters_[i]?"Downloaded / Offline":chapter.locked?"Locked":"Available to download";
                reading=online_started_ && i==online_saved_chapter_;
                if (reading) sub="Reading / "+sub;
            }
            list_row(y,label,sub,i==online_selected_,reading);
        }
        if (!size) text(40,188,palette().text,1.0f,"No results. Try Search or another source.",800);
        draw_pager(online_selected_,size);
    }
    void draw_toolbar() const {
        const auto actions=toolbar();
        for (std::size_t i=0;i<actions.size();++i) button(reader::ui::toolbar_button(static_cast<int>(i),static_cast<int>(actions.size())),actions[i]);
    }
    void draw_menu() const {
        vita2d_draw_rectangle(0,0,960,544,0xe8000000u);
        text(40,60,palette().text,1.3f,menu_title_+(menu_.size()>6?"  "+std::to_string(menu_selected_/6+1)+" / "+std::to_string((menu_.size()+5)/6):""),880,reader::FontRole::Heading);
        text(40,104,palette().muted,0.85f,"Choose an action. Circle returns to the same page.",880);
        const auto first=menu_selected_/6*6;
        for (auto i=first;i<menu_.size() && i<first+6;++i) button(reader::ui::menu_button(static_cast<int>(i-first)),menu_[i],i==menu_selected_);
        if (menu_.size()>6) {
            button(reader::ui::toolbar_button(0,3),{"Previous",SCE_CTRL_LTRIGGER,Command::Button,first>=6});
            button(reader::ui::toolbar_button(1,3),{"Close",SCE_CTRL_CIRCLE});
            button(reader::ui::toolbar_button(2,3),{"Next",SCE_CTRL_RTRIGGER,Command::Button,first+6<menu_.size()});
        } else button(reader::ui::toolbar_button(0,1),{"Close",SCE_CTRL_CIRCLE});
    }
    std::vector<reader::Line> feedback_rows() const {
        return reader::wrap_text(message_,retry_available_?660:856,[this](const std::string& s){return fonts_.width(reader::FontRole::Interface,24,s);});
    }
    int feedback_bottom() const {return screen_==Screen::Reader && immersive_ && !online_busy_?500:452;}
    int feedback_top() const {return feedback_bottom()-reader::ui::feedback_height(feedback_rows().size(),online_busy_,retry_available_);}
    void draw_feedback() const {
        if (message_.empty()) return;
        const auto rows=feedback_rows();const auto count=std::min<std::size_t>(rows.size(),5);
        const auto top=feedback_top(),bottom=feedback_bottom();
        panel(24,top,912,bottom-top,true);
        for (std::size_t i=0;i<count;++i) text(44,top+32+static_cast<int>(i)*28,palette().text,0.92f,rows[i].text,retry_available_?660:856);
        if (retry_available_ && !online_busy_) button({728,top+8,192,bottom-top-16},{"Retry",0,Command::Retry});
        if (online_busy_) {
            vita2d_draw_rectangle(44,bottom-48,872,6,palette().background);
            if (download_total_) vita2d_draw_rectangle(44,bottom-48,872.0f*download_completed_/download_total_,6,palette().accent);
            else vita2d_draw_rectangle(44+(sceKernelGetProcessTimeWide()/4000)%712,bottom-48,160,6,palette().accent);
            text(44,bottom-14,palette().muted,0.85f,download_total_?std::to_string(download_completed_)+" / "+std::to_string(download_total_)+" completed":"Connecting / Circle to cancel",856);
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
        const auto scale = fit_scale(864, 336) * zoom_;
        const auto limit_x = std::max(0.0f, (vita2d_texture_get_width(image_) * scale - 864) / 2);
        const auto limit_y = std::max(0.0f, (vita2d_texture_get_height(image_) * scale - 336) / 2);
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
    const Palette& palette() const { return screen_ == Screen::Reader && !interface_layer_ ? palettes[settings_.theme] : menu_palette; }
    unsigned reading_pixels() const { return 22 + settings_.font * 4; }
    reader::FontRole reading_font() const {
        return settings_.reading_font == 0 ? reader::FontRole::Serif :
            settings_.reading_font == 1 ? reader::FontRole::Sans : reader::FontRole::System;
    }
    int line_height() const { return 26 + settings_.font * 5 + settings_.spacing * 4; }

    std::string fit(std::string text, float scale, int width, reader::FontRole role) const {
        const auto pixels = std::max(22u,static_cast<unsigned>(std::lround(26 * scale)));
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
        fonts_.draw(role, std::max(22u,static_cast<unsigned>(std::lround(26 * scale))), x, y, color, clipped);
    }

    void heading(const std::string& title,const std::string& subtitle) const {
        text(24,43,palette().text,1.3f,title,776,reader::FontRole::Heading);
        text(24,96,palette().muted,0.85f,subtitle,912);
        if (screen_!=Screen::Reader) button({816,8,120,64},{"Options",SCE_CTRL_TRIANGLE});
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
                description_row_ = 0;
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
        const char* filters[]={"All","Reading","Finished"};
        if (importing_) heading("Import books",current_folder_.empty()?"Choose a folder or book":current_folder_);
        else {
            text(24,43,palette().text,1.3f,"Library",520,reader::FontRole::Heading);
            for (int i=0;i<3;++i) button({24+i*176,56,168,64},{filters[i]},i==static_cast<int>(filter_));
            const auto recent=reader::ui::recent_book(entries_);
            panel(568,8,368,104,recent<entries_.size());
            text(588,44,palette().text,1.0f,"Continue reading",328,reader::FontRole::Heading);
            text(588,80,palette().muted,0.85f,recent<entries_.size()?entries_[recent].title:"Open a book to begin",328);
        }
        const auto first=reader::ui::page_first(selected_);
        for (auto i=first;i<order_.size() && i<first+list_page;++i) {
            const auto& item=entries_[order_[i]];
            const int y=reader::ui::library_top+static_cast<int>(i-first)*reader::ui::library_pitch;
            panel(24,y,832,76,i==selected_);
            if (i==selected_ && image_) {
                const auto scale=fit_scale(52,64);
                vita2d_draw_texture_scale(image_,66-vita2d_texture_get_width(image_)*scale/2,y+38-vita2d_texture_get_height(image_)*scale/2,scale,scale);
            } else {
                vita2d_draw_rectangle(40,y+8,52,60,palette().background);
                vita2d_draw_rectangle(42,y+10,3,56,palette().accent);
                std::size_t initial=1;
                while (initial<item.title.size() && (static_cast<unsigned char>(item.title[initial])&0xc0)==0x80) ++initial;
                text(54,y+44,palette().accent,1.0f,item.directory?">":item.title.substr(0,initial),32);
            }
            const auto title=reader::wrap_text(item.title,640,[this](const std::string& s){return fonts_.width(reader::FontRole::Interface,26,s);});
            const bool two=title.size()>1;
            for (std::size_t line=0;line<std::min<std::size_t>(two?2:1,title.size());++line)
                text(108,y+(two?25:31)+static_cast<int>(line)*26,palette().text,1.0f,title[line].text,640);
            std::string status=item.directory?"Folder":item.guide?"Reader guide":reader::is_online_book(item.filename)?"Online novel":reader::is_epub_book(item.filename)?"EPUB":"Text book";
            if (!item.error.empty()) status+=" / Unavailable";
            else if (item.progress.percent>=0) status+=" / "+std::to_string(item.progress.percent)+"% read";
            else if (!item.directory && !item.guide) status+=" / New";
            text(108,y+(two?72:62),palette().muted,0.85f,status,640);
            if (!importing_) {vita2d_draw_line(776,y+12,776,y+64,palette().muted);text(788,y+46,palette().accent,0.85f,"Info",60);}
        }
        if (order_.empty()) {
            text(40,188,palette().text,1.0f,importing_?"No supported books here.":"No books in this filter.",800);
            text(40,234,palette().muted,0.85f,"Use Import or Online to add your next read.",800);
        }
        draw_pager(selected_,order_.size());
    }
    void draw_book() const {
        if (order_.empty()) return;
        const auto& item=entries_[order_[selected_]];
        heading("Book details",item.guide?"Bundled reader guide":"Your library");
        panel(24,124,180,308);
        if (image_) {
            const auto scale=fit_scale(164,292);
            vita2d_draw_texture_scale(image_,114-vita2d_texture_get_width(image_)*scale/2,278-vita2d_texture_get_height(image_)*scale/2,scale,scale);
        } else {
            const auto cover=reader::wrap_text(item.title,140,[this](const std::string& s){return fonts_.width(reader::FontRole::Heading,26,s);});
            for (std::size_t i=0;i<std::min<std::size_t>(cover.size(),6);++i) text(44,174+static_cast<int>(i)*32,palette().text,1.0f,cover[i].text,140,reader::FontRole::Heading);
        }
        const auto title=reader::wrap_text(item.title,704,[this](const std::string& s){return fonts_.width(reader::FontRole::Heading,30,s);});
        for (std::size_t i=0;i<std::min<std::size_t>(title.size(),3);++i) text(232,153+static_cast<int>(i)*34,palette().text,1.15f,title[i].text,704,reader::FontRole::Heading);
        text(232,272,palette().muted,1.0f,item.author.empty()?"Local book":item.author,704);
        text(232,312,palette().text,1.0f,std::to_string(item.chapters)+" chapters",704);
        text(232,352,palette().accent,1.0f,item.progress.percent>=0?std::to_string(item.progress.percent)+"% read / Chapter "+std::to_string(item.progress.chapter+1):"Ready to start",704);
        const auto info=reader::wrap_text(item.error.empty()?item.filename:item.error,704,[this](const std::string& s){return fonts_.width(reader::FontRole::Interface,22,s);});
        for (std::size_t i=0;i<std::min<std::size_t>(info.size(),2);++i) text(232,396+static_cast<int>(i)*28,palette().muted,0.85f,info[i].text,704);
    }
    void draw_reader() const {
        const auto end=visible_end();
        const int percent=end==lines_.size()?100:static_cast<int>(100*lines_[top_].offset/std::max<std::size_t>(1,book_.text.size()));
        if (!immersive_) heading(publication_.title,"Chapter "+std::to_string(chapter_+1)+" / "+std::to_string(publication_.chapters.size())+
            "   Page "+std::to_string(std::upper_bound(pages_.begin(),pages_.end(),top_)-pages_.begin())+" / "+std::to_string(pages_.size())+"   "+std::to_string(percent)+"%");
        const auto area=reading_area();int y=area.top;
        vita2d_enable_clipping();vita2d_set_clip_rectangle(48,area.top,912,area.top+area.height);
        for (auto i=top_;i<end;++i) {
            if (lines_[i].illustration==reader::no_illustration) draw_line(lines_[i],y+line_height()-8);
            else draw_inline_image(lines_[i],y);
            y+=lines_[i].height;
        }
        vita2d_disable_clipping();
        const int bar_y=immersive_?510:448;
        vita2d_draw_rectangle(48,bar_y,864,2,palette().panel);
        vita2d_draw_rectangle(48,bar_y,864.0f*percent/100,2,palette().accent);
        if (immersive_) {
            text(48,535,palette().muted,0.85f,"Chapter "+std::to_string(chapter_+1)+" / "+std::to_string(publication_.chapters.size())+"  /  "+std::to_string(percent)+"%",480);
            text(560,535,palette().muted,0.85f,"Page "+std::to_string(std::upper_bound(pages_.begin(),pages_.end(),top_)-pages_.begin())+" / "+std::to_string(pages_.size()),352);
        }
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
        heading("Contents",publication_.title);
        const auto first=reader::ui::page_first(contents_selected_);
        for (auto i=first;i<publication_.chapters.size() && i<first+list_page;++i) {
            const int y=reader::ui::library_top+static_cast<int>(i-first)*reader::ui::library_pitch;
            list_row(y,std::to_string(i+1)+". "+publication_.chapters[i].title,i==chapter_?"Currently reading":"Open chapter",i==contents_selected_,i==chapter_);
        }
        draw_pager(contents_selected_,publication_.chapters.size());
    }
    void draw_illustrations() const {
        heading("Illustrations",publication_.chapters[chapter_].title);
        const auto first=reader::ui::page_first(illustration_selected_);
        for (auto i=first;i<book_.illustrations.size() && i<first+list_page;++i) {
            const int y=reader::ui::library_top+static_cast<int>(i-first)*reader::ui::library_pitch;
            list_row(y,std::to_string(i+1)+". "+book_.illustrations[i].label,"View and zoom",i==illustration_selected_);
        }
        if (book_.illustrations.empty()) text(40,188,palette().muted,1.0f,"This chapter has no illustrations.",800);
        draw_pager(illustration_selected_,book_.illustrations.size());
    }
    void draw_image() const {
        heading(book_.illustrations[illustration_selected_].label,std::to_string(illustration_selected_+1)+" / "+std::to_string(book_.illustrations.size())+" images  /  "+std::to_string(static_cast<int>(zoom_*100))+"% zoom");
        if (image_) {
            const auto scale=fit_scale(864,336)*zoom_;
            vita2d_enable_clipping();vita2d_set_clip_rectangle(48,104,912,440);
            vita2d_draw_texture_scale(image_,480-vita2d_texture_get_width(image_)*scale/2+pan_x_,272-vita2d_texture_get_height(image_)*scale/2+pan_y_,scale,scale);
            vita2d_disable_clipping();
        }
    }
    void draw_settings() const {
        text(24,43,palette().text,1.3f,"Appearance",740,reader::FontRole::Heading);
        const bool display=setting_row_>=3;
        button({24,56,448,64},{"L   Text"},!display);button({488,56,448,64},{"Display   R"},display);
        const char* labels[]={"Text size","Reading font","Line spacing","Reading theme","Reading view"};
        const char* sizes[]={"Small","Medium","Large"}; const char* fonts[]={"Literata","Atkinson Next","Vita system"};
        const char* spacing[]={"Compact","Normal","Relaxed"};const char* themes[]={"Night","Paper","Sepia"};
        const std::string values[]={sizes[settings_.font],fonts[settings_.reading_font],spacing[settings_.spacing],themes[settings_.theme],settings_.immersive?"Page only":"With controls"};
        const int first=display?3:0,count=display?2:3;
        for (int i=0;i<count;++i) {
            const int row=first+i,y=128+i*100;
            panel(24,y,488,92,row==setting_row_);
            text(112,y+30,palette().muted,0.85f,labels[row],304);
            text(112,y+66,palette().text,1.0f,values[row],304);
            button({32,y+14,64,64},{"-"});button({440,y+14,64,64},{"+"});
        }
        const auto& paper=palettes[settings_.theme];
        vita2d_draw_rectangle(536,128,400,300,paper.background);
        text(556,163,paper.muted,0.85f,"Reading preview",360);
        const auto lines=reader::wrap_text("A quiet evening. The next adventure waits between the lines. Outside, the last light fades. You settle in and turn the page.",352,
            [this](const std::string& s){return fonts_.width(reading_font(),reading_pixels(),s);});
        int y=211;
        for (const auto& line:lines) {
            if (y>(display?330:412)) break;
            fonts_.draw(reading_font(),reading_pixels(),556,y,paper.text,line.text);y+=line_height();
        }
        if (display) for (int i=0;i<3;++i) {
            const int x=548+i*124;
            vita2d_draw_rectangle(x,352,116,64,palettes[i].background);
            text(x+8,391,palettes[i].text,0.85f,themes[i],100);
            if (i==settings_.theme) vita2d_draw_rectangle(x,412,116,4,palette().accent);
        }
    }


    reader::VitaFonts fonts_;
    bool interface_layer_ = false;
    std::string pressed_action_;
    std::uint64_t pressed_until_ = 0;
    std::size_t description_row_ = 0;
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
