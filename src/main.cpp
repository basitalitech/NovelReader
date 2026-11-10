#include "reader.hpp"
#include "vita_images.hpp"
#include "vita_fonts.hpp"
#include "controls.hpp"
#include "online.hpp"
#include "ui.hpp"
#include "vita_ui.hpp"
#include "vita_hints.hpp"
#include "speech.hpp"
#include "performance.hpp"

#ifdef NOVELREADER_FRONTEND_TESTS
#include "vita_frontend_stubs.hpp"
#else
#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/touch.h>
#include <psp2/ime_dialog.h>
#include <psp2/apputil.h>
#include <psp2/power.h>
#include <psp2/display.h>
#endif
#include <vita2d.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <cmath>
#include <ctime>
#include <cctype>
#include <memory>
#include <map>
#include <string>
#include <stdexcept>
#include <vector>
#include <tuple>
#include <unordered_set>
#include <unordered_map>

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
    {rgb(23, 23, 25), rgb(41, 38, 46), rgb(239, 234, 225),
     rgb(190, 183, 195), rgb(180, 125, 239)},
    {rgb(245, 245, 241), rgb(226, 232, 229), rgb(30, 40, 43),
     rgb(84, 97, 100), rgb(22, 109, 87)},
    {rgb(239, 226, 199), rgb(224, 207, 174), rgb(64, 49, 33),
     rgb(113, 90, 61), rgb(115, 66, 36)}
};
const Palette menu_palette = {reader::visual::background, reader::visual::surface,
    reader::visual::text, reader::visual::muted, reader::visual::accent};

constexpr std::size_t list_page = reader::ui::page_size;

enum class Screen { Home, Library, Book, Reader, Settings, Contents, Illustrations, Image, Sources, Catalog, Novel, OnlineChapters, Bookmarks, About };
enum class Command { Button, More, Sort, Jump, Immersive, Resume, Retry, Appearance, Details, DownloadAll, SortTitle, SortRecent, SortProgress,
    FilterAll, FilterReading, FilterFinished, Exit, Home, Library, Bookmarks, Import, DeleteBookmark, Credits, License, Search,
    BookBookmark, RemoveBook, ConfirmRemoveBook, Online, ClearSearch, PreviousPage, NextPage,
    ApplySettings, CancelSettings, ResetSettings, Illustrations, FullTitle, AddBook,
    Speech, SpeechToggle, SpeechStop, SpeechSlower, SpeechFaster, SaveBookmark, Dismiss,
    CloseMenu, ReadOnline, OnlineContents, LocalContents };
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
struct CoverTexture {
    std::string key;
    std::unique_ptr<vita2d_texture, TextureDeleter> texture;
};
struct BookmarkPreview { std::string key, quote; };

class App {
#ifdef NOVELREADER_FRONTEND_TESTS
    friend struct FrontendTest;
#endif
public:
    explicit App(vita2d_pgf* font, reader::speech::Player::AudioFactory audio = reader::speech::platform_audio)
        : fonts_(font), speech_(std::move(audio)), settings_(reader::load_settings(settings_path)) {
        sort_ = static_cast<reader::LibrarySort>(settings_.library_sort);
        filter_ = static_cast<reader::LibraryFilter>(settings_.library_filter);
        immersive_ = settings_.immersive != 0;
        {
            const reader::perf::Scope timing(reader::perf::Event::Resources);
            backdrop_.reset(vita2d_load_PNG_file("app0:/ui/moonlit.png"));
            logo_.reset(vita2d_load_PNG_file("app0:/ui/logo.png"));
        }
        draw_startup(0.15f);
        if (!reader::load_library(library_path, library_books_)) {
            SceIoStat state{};
            library_writable_ = sceIoGetstat(library_path.c_str(), &state) < 0;
        }
        draw_startup(0.35f);
        scan({},false,true);
        draw_startup(0.9f);
        sync_covers();
        draw_startup(1.f);
        if (!fonts_.complete()) message_ = "Some bundled fonts are unavailable. Using the system font as fallback.";
    }
    ~App() {
        speech_.stop();
        online_.cancel();
        if (ime_active_) { sceImeDialogAbort(); sceImeDialogTerm(); }
        release_image();
#if defined(NOVELREADER_PROFILE) && !defined(NOVELREADER_FRONTEND_TESTS)
        if(auto* log=std::fopen((root+"/performance.csv").c_str(),"w")) {
            reader::perf::dump(log);std::fclose(log);
        }
#endif
    }

    void draw() {
        draw_frame();
        redraw_=false;
        present();
#ifdef NOVELREADER_PROFILE
        reader::perf::sample_heap();
#endif
    }
    void draw_loading() {
        draw();
        // The operation will still change screen/content or report an error.
        redraw_=true;
    }
    void draw_startup(float fraction) const {
        using namespace reader::visual;
        vita2d_set_clear_color(background);
        vita2d_start_drawing();
        vita2d_clear_screen();
        if(backdrop_)
            vita2d_draw_texture_scale(backdrop_.get(),0,0,
                960.f/vita2d_texture_get_width(backdrop_.get()),544.f/vita2d_texture_get_height(backdrop_.get()));
        else gradient(0,0,960,544,background,surface);
        vita2d_draw_rectangle(0,0,960,544,0x30000000u);
        draw_logo(480,184,104,0xffffffffu);
        const auto centered=[&](const std::string& value,unsigned pixels,int baseline,unsigned tint) {
            const auto role=reader::FontRole::Heading;
            fonts_.draw(role,pixels,480-fonts_.width(role,pixels,value)/2,baseline,tint,value);
        };
        centered("NovelReader",40,283,0xffffffffu);
        centered("by basitalitech",22,321,reader::visual::text);
        progress(320,365,320,fraction,8);
        vita2d_end_drawing();
        vita2d_swap_buffers();
        vita2d_wait_rendering_done();
    }
    bool needs_redraw() {
        const auto now=sceKernelGetProcessTimeWide();
        if(pressed_until_ && now>=pressed_until_){pressed_until_=0;redraw_=true;}
        if(notice_until_ && message_==notice_ && now>=notice_until_) {
            message_.clear();notice_until_=0;redraw_=true;
        }
        // System dialogs require regular common-dialog updates even without input.
        return redraw_ || ime_active_;
    }
    void draw_frame() {
        const reader::perf::Scope frame(reader::perf::Event::FrameWork);
        tick_speech();
        if (message_ == notice_ && sceKernelGetProcessTimeWide() >= notice_until_) message_.clear();
        if (screen_ == Screen::Reader) sync_inline_images();
        normalize_focus();
        if(screen_==Screen::Novel || screen_==Screen::OnlineChapters || screen_==Screen::Contents)update_cover();
        sync_covers();
        sync_bookmark_previews();
        const auto& color = palette();
        vita2d_set_clear_color(color.background);
        vita2d_start_drawing();
        vita2d_clear_screen();
        if (screen_ != Screen::Reader && screen_ != Screen::Image) draw_background();
        if (has_sidebar()) draw_sidebar();
        if (screen_ == Screen::Home) draw_home();
        else if (screen_ == Screen::Bookmarks) draw_bookmarks();
        else if (screen_ == Screen::About) draw_about();
        else if (is_online_screen()) draw_online();
        else if (screen_ == Screen::Library) draw_library();
        else if (screen_ == Screen::Book) draw_book();
        else if (screen_ == Screen::Reader) draw_reader();
        else if (screen_ == Screen::Contents) draw_contents();
        else if (screen_ == Screen::Illustrations) draw_illustrations();
        else if (screen_ == Screen::Image) draw_image();
        else draw_settings();
        interface_layer_ = true;
        if (screen_ != Screen::Reader || !immersive_) draw_chrome();
        draw_toolbar();
        draw_feedback();
        if (!menu_.empty()) draw_menu();
        draw_focus();
        interface_layer_ = false;
        vita2d_end_drawing();
        if (ime_active_) vita2d_common_dialog_update();
    }

    void present() {
        const reader::perf::Scope timing(reader::perf::Event::FramePresent);
        vita2d_swap_buffers();
        vita2d_wait_rendering_done();
    }

    bool input(unsigned pressed, bool physical = true) {
        if(pressed)redraw_=true;
        const auto context=input_context();
        tick_speech();
        const bool keyboard_was_active=ime_active_;
        tick_online();
        if (keyboard_was_active) return true; // Never reuse the keyboard's dismissing press.
        if (ime_active_ || input_context()!=context) return true;
        // One action per sample. Cancel wins even when confirm is also pressed.
        if(physical) {
            for(const unsigned key:{SCE_CTRL_CIRCLE,SCE_CTRL_START,SCE_CTRL_CROSS,
                    SCE_CTRL_TRIANGLE,SCE_CTRL_SQUARE,SCE_CTRL_SELECT,SCE_CTRL_LTRIGGER,SCE_CTRL_RTRIGGER,
                    SCE_CTRL_LEFT,SCE_CTRL_RIGHT,SCE_CTRL_UP,SCE_CTRL_DOWN})
                if(pressed&key){pressed=key;break;}
        }
        // Active layers own all input, including START and contextual shortcuts.
        if(online_busy_) {
            if(pressed&(SCE_CTRL_CIRCLE|SCE_CTRL_CROSS)){pending_bookmark_=false;online_.cancel();message_="Canceling...";}
            return true;
        }
        if(physical && (!menu_.empty() || feedback_active())) {
            focus_input(pressed);return focus_result_;
        }
        if (physical && focus_input(pressed)) return focus_result_;
        if (pressed & SCE_CTRL_START) { show_more(); return true; }
        if (!menu_.empty()) return true;
        if (physical && has_sidebar() && (pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER))) {
            const int step=pressed&SCE_CTRL_RTRIGGER?1:reader::ui::sidebar_count-1;
            navigate((nav_active_+step)%reader::ui::sidebar_count);sidebar_focus_=false;normalize_focus();return true;
        }
        if (screen_==Screen::Reader) {
            if (pressed & SCE_CTRL_TRIANGLE) return perform({"Bookmarks",0,Command::Bookmarks});
            if (pressed & SCE_CTRL_SQUARE) return perform({"Font",0,Command::Appearance});
        }
        if ((pressed & SCE_CTRL_TRIANGLE) && screen_==Screen::Library && !importing_) {
            show_sort();
            return true;
        }
        if (screen_ == Screen::Home) {
            if (pressed & SCE_CTRL_LEFT) sidebar_focus_=true;

            if (pressed & SCE_CTRL_CROSS) {
                if (reader::ui::recent_book(entries_)<entries_.size()) resume_recent();
                else {navigate(1);sidebar_focus_=false;}
            }
            if (pressed & SCE_CTRL_CIRCLE) sidebar_focus_=true;
            return true;
        }
        if (screen_ == Screen::Bookmarks) {
            const auto& marks=bookmarks();
            if (pressed & SCE_CTRL_LEFT) sidebar_focus_=true;
            if ((pressed & SCE_CTRL_UP) && bookmark_selected_) --bookmark_selected_;
            if ((pressed & SCE_CTRL_DOWN) && bookmark_selected_+1<marks.size()) ++bookmark_selected_;
            if (pressed & SCE_CTRL_SQUARE) delete_bookmark();
            if (pressed & SCE_CTRL_CIRCLE) {
                screen_=bookmarks_return_;nav_active_=nav_selected_=bookmarks_nav_return_;
                sidebar_focus_=false;return true;
            }
            if ((pressed & SCE_CTRL_CROSS) && bookmark_selected_<marks.size()) open_bookmark(marks[bookmark_selected_]);
            return true;
        }
        if (screen_ == Screen::About) {
            if (pressed & SCE_CTRL_LEFT) sidebar_focus_=true;
            if (pressed & SCE_CTRL_UP) about_selected_=std::max(0,about_selected_-1);
            if (pressed & SCE_CTRL_DOWN) about_selected_=std::min(1,about_selected_+1);
            if (pressed & SCE_CTRL_CROSS) perform({"",0,about_selected_==0?Command::Credits:Command::License});
            if (pressed & SCE_CTRL_CIRCLE) navigate(0);
            return true;
        }
        if (screen_ == Screen::Book) {
            if (pressed & SCE_CTRL_CIRCLE) { screen_ = Screen::Library; message_.clear(); }
            else if (pressed & SCE_CTRL_CROSS) { open(); if (screen_ == Screen::Novel) resume_online(); }
            return true;
        }
        if (is_online_screen()) return input_online(pressed);
        if (screen_ == Screen::Library) {
            if (pressed & SCE_CTRL_SELECT) {
                const bool restore_collection=importing_;importing_=false;current_folder_.clear();
                if(restore_collection)scan();
                release_image();screen_=Screen::Sources;online_selected_=0;message_.clear();return true;
            }
            if (pressed & SCE_CTRL_CIRCLE) {
                if (!importing_ && library_search_active_) { exit_library_search(); }
                else if (importing_ && !current_folder_.empty()) {
                    const auto child = current_folder_;
                    browse_folder(reader::parent_library_folder(current_folder_), child);
                } else if (importing_) {
                    importing_=false;current_folder_.clear();
                    filter_=import_filter_;library_query_=import_query_;scan();
                    selected_=order_.empty()?0:std::min(import_selected_,order_.size()-1);
                    focus_memory_[static_cast<int>(Screen::Library)]=200;update_cover();
                } else { navigate(0); sidebar_focus_=true; }
                return true;
            }
            if (!importing_) {
                if ((pressed & SCE_CTRL_UP) && selected_>=4) selected_-=4;
                if ((pressed & SCE_CTRL_DOWN) && (selected_/4+1)*4<order_.size()) selected_=std::min(selected_+4,order_.size()-1);
                if (pressed & SCE_CTRL_LEFT) { if (selected_%4==0) sidebar_focus_=true; else --selected_; }
                if ((pressed & SCE_CTRL_RIGHT) && selected_+1<order_.size()) ++selected_;
            } else {
                if ((pressed & SCE_CTRL_UP) && selected_ > 0) --selected_;
                if ((pressed & SCE_CTRL_DOWN) && selected_ + 1 < order_.size()) ++selected_;
                if ((pressed & SCE_CTRL_LEFT) && !order_.empty()) selected_ = selected_ > list_page ? selected_ - list_page : 0;
                if ((pressed & SCE_CTRL_RIGHT) && !order_.empty()) selected_ = std::min(selected_ + list_page, order_.size() - 1);
            }
            if (pressed & SCE_CTRL_SQUARE) {
                if(importing_)show_sort();else perform({"Search",0,Command::Search});
                return true;
            }
            if (pressed & SCE_CTRL_TRIANGLE) {
                if (!importing_) { importing_ = true; browse_folder(""); }
                else scan({},true);
            }
            update_cover();
            if ((pressed & SCE_CTRL_CROSS) && !order_.empty()) {
                if (importing_) open(); else perform({"Details",0,Command::Details});
            }
        } else if (screen_ == Screen::Reader) {
            if (pressed & SCE_CTRL_CIRCLE) {
                const reader::perf::Scope timing(reader::perf::Event::ReaderExit);
                speech_.stop();
                if (publication_.online) online_selected_ = chapter_;
                save(); screen_ = publication_.online ? Screen::OnlineChapters : Screen::Library;
                if(screen_==Screen::Library) {nav_active_=nav_selected_=1;sidebar_focus_=false;}
                // The open chapter and offline summary already describe this
                // book. Closing it must not probe every downloaded chapter.
                if (publication_.online) { online_saved_chapter_=chapter_;online_started_=true; }
                const auto keep=active_filename_;
                clear_reading();
                ensure_library_order(keep);
                if (screen_ == Screen::Library) update_cover();
            }
            else if (pressed & SCE_CTRL_CROSS) {
                contents_from_details_=false;contents_selected_ = chapter_; screen_ = Screen::Contents; message_.clear();
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
                if (pressed & SCE_CTRL_UP) step_reading(false);
                if (pressed & SCE_CTRL_DOWN) step_reading(true);
                bool changed = previous != top_ || previous_chapter != chapter_;
                if (changed) message_.clear();
                if (pressed & SCE_CTRL_SELECT) {
                    if (progress_.has_bookmark) {
                        if (progress_.bookmark_chapter == chapter_) {
                            top_ = reader::block_at_offset(lines_, progress_.bookmark); changed = true;
                        } else changed = change_chapter(progress_.bookmark_chapter, progress_.bookmark) || changed;
                    } else message_ = "Open Reading controls to save a bookmark.";
                }
                if (changed) save();
            }
        } else if (screen_ == Screen::Contents) {
            if (pressed & SCE_CTRL_TRIANGLE) { jump_to_chapter(); return true; }
            if ((pressed & SCE_CTRL_UP) && contents_selected_ > 0) --contents_selected_;
            if ((pressed & SCE_CTRL_DOWN) && contents_selected_ + 1 < publication_.chapters.size()) ++contents_selected_;
            if (pressed & SCE_CTRL_LEFT) contents_selected_ = contents_selected_ >= 5 ? contents_selected_ - 5 : 0;
            if ((pressed & SCE_CTRL_RIGHT) && !publication_.chapters.empty()) contents_selected_ = std::min(contents_selected_ + 5, publication_.chapters.size() - 1);
            if (pressed & SCE_CTRL_CIRCLE) {
                if(contents_from_details_){contents_from_details_=false;screen_=Screen::Book;clear_reading();update_cover();}
                else screen_=Screen::Reader;
                message_.clear();
            }
            if (pressed & SCE_CTRL_SQUARE) {
                if(lines_.empty() && !change_chapter(chapter_,progress_.offset))return true;
                illustration_selected_ = 0;
                // Start near the currently visible illustration, if any.
                for (std::size_t i = 0; i < book_.illustrations.size(); ++i)
                    if (book_.illustrations[i].offset <= lines_[top_].offset) illustration_selected_ = i;
                screen_ = Screen::Illustrations; message_.clear();
            }
            if (pressed & SCE_CTRL_CROSS) {
                if ((!lines_.empty() && contents_selected_ == chapter_) ||
                    change_chapter(contents_selected_,contents_selected_==progress_.chapter?progress_.offset:0)) {
                    contents_from_details_=false;screen_ = Screen::Reader; save();
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
        } else if(screen_==Screen::Settings) {
            if(pressed&SCE_CTRL_SELECT)perform({"Reset",0,Command::ResetSettings});
            if(pressed&SCE_CTRL_CIRCLE)finish_settings(false);
        }
        return true;
    }

    Screen screen() const { return screen_; }
    std::uint64_t input_context() const {
        // Content identity plus exclusive input layer; no strings or allocation.
        return folder_revision_*512 + static_cast<unsigned>(screen_)*32 + (importing_?16:0) +
            (!menu_.empty()?1:0) + (online_busy_?2:0) + (feedback_active()?4:0) + (ime_active_?8:0);
    }
    unsigned repeatable_buttons() const {
        const unsigned directions=SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT;
        return directions | ((!menu_.empty() || screen_==Screen::Reader || screen_==Screen::Image)?
            SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER:0);
    }
    bool can_pan_image() const { return screen_ == Screen::Image && menu_.empty() && !ime_active_ && !feedback_active() && !online_busy_; }
    std::size_t folder_revision() const { return folder_revision_; }
    void pan_image(float dx, float dy) {
        if (screen_ != Screen::Image) return;
        const auto before_x=pan_x_,before_y=pan_y_;
        pan_x_ += dx; pan_y_ += dy; clamp_pan();
        if(pan_x_!=before_x || pan_y_!=before_y)redraw_=true;
    }
    void transform_image(const reader::controls::Gesture& gesture) {
        redraw_=true;
        pan_x_ += gesture.dx; pan_y_ += gesture.dy;
        const auto next = std::max(1.0f, std::min(4.0f, zoom_ * gesture.scale));
        const auto ratio = next / zoom_;
        // Keep the image point beneath the pinch midpoint stationary.
        pan_x_ = gesture.x - 480 - (gesture.x - 480 - pan_x_) * ratio;
        pan_y_ = gesture.y - reader::ui::image_center_y - (gesture.y - reader::ui::image_center_y - pan_y_) * ratio;
        zoom_ = next; clamp_pan();
    }

    bool gesture(const reader::controls::Gesture& gesture) {
        using reader::controls::GestureKind;
        if (gesture.kind == GestureKind::None || ime_active_) return true;
        redraw_=true;
        const int x = static_cast<int>(gesture.x), y = static_cast<int>(gesture.y);
        const int start_x=gesture.start_x<0?x:static_cast<int>(gesture.start_x);
        const int start_y=gesture.start_y<0?y:static_cast<int>(gesture.start_y);
        if(!menu_.empty()) {
            // A modal owns the entire gesture, including dismissal. Never reuse
            // an outside tap on the screen that becomes visible underneath it.
            if(gesture.kind==GestureKind::Tap) {
                normalize_focus();
                for(const auto& control:widgets())
                    if(control.target.enabled && control.target.box.contains(x,y) && control.target.box.contains(start_x,start_y)) {
                        set_focus(control.target.id);return activate(control);
                    }
                const auto popup=menu_layout();
                if(!menu_parents_.empty() && popup.back().contains(x,y) && popup.back().contains(start_x,start_y)) {
                    back_menu();return true;
                }
                const auto actions=menu_toolbar();
                for(std::size_t i=0;i<actions.size();++i) {
                    const auto hit=footer_touch_rect(toolbar_rect(actions,i),actions[i]);
                    if(hit.contains(x,y) && hit.contains(start_x,start_y)) {
                        if(actions[i].button==SCE_CTRL_CIRCLE)back_menu();
                        else for(const auto& control:widgets())
                            if(control.target.id==2000+static_cast<int>(menu_selected_) && control.target.enabled)return activate(control);
                        return true;
                    }
                }
                if(!popup.box.contains(x,y) && !popup.box.contains(start_x,start_y))close_menu();
            }
            return true;
        }
        if (gesture.kind==GestureKind::Tap) {
            normalize_focus();
            const auto controls=widgets();
            for(const auto& control:controls) if(control.target.enabled && control.target.box.contains(x,y) && control.target.box.contains(start_x,start_y)) {
                set_focus(control.target.id);
                if(control.kind==WidgetKind::Setting) {
                    adjust_setting(control.index,x<control.target.box.x+control.target.box.width/2?-1:1);
                    return true;
                }
                return activate(control);
            }
            // The existing legend and sidebar also serve as touch controls;
            // keep them out of the D-pad content focus graph.
            if(!(screen_==Screen::Reader && immersive_ && menu_.empty() && !feedback_active() && !online_busy_)) {
                const auto actions=!menu_.empty()?menu_toolbar():toolbar();
                for(std::size_t i=0;i<actions.size();++i) {
                    auto box=!menu_.empty()?reader::ui::toolbar_button(i,actions.size()):toolbar_rect(actions,i);
                    box=footer_touch_rect(box,actions[i]);
                    if(box.contains(x,y) && box.contains(start_x,start_y) && actions[i].enabled) {
                        if(actions[i].command==Command::Button)return input(actions[i].button);
                        return perform(actions[i]);
                    }
                }
            }
            if(!menu_.empty() || feedback_active() || online_busy_) return true;
            if(has_sidebar())for(int i=0;i<reader::ui::sidebar_count;++i) {
                const auto box=reader::ui::sidebar_item(i);
                if(box.contains(x,y) && box.contains(start_x,start_y)){navigate(i);normalize_focus();return true;}
            }
            if(screen_!=Screen::Reader && screen_!=Screen::Image) return true;
        }
        if(!menu_.empty())return true;
        if(feedback_active()) return true;
        if (online_busy_) return true;
        if(gesture.kind==GestureKind::Tap && y>=reader::visual::hint_bar_top && !(screen_==Screen::Reader && immersive_))return true;
        if (gesture.kind == GestureKind::Transform) {
            const reader::ui::Rect image_area{48,reader::ui::image_top,864,reader::ui::image_height};
            if (screen_ == Screen::Image && image_area.contains(start_x,start_y) && image_area.contains(x,y)) transform_image(gesture);
            return true;
        }
        if (gesture.kind == GestureKind::Hold) {
            if (screen_ == Screen::Reader && x >= 48 && x < 912 && y >= reading_area().top &&
                y < reading_area().top + reading_area().height &&
                reader::ui::Rect{48,reading_area().top,864,reading_area().height}.contains(start_x,start_y))
                return perform({"Save bookmark",0,Command::SaveBookmark});
            return true;
        }
        if (gesture.kind == GestureKind::Swipe) {
            const int origin_x=gesture.start_x<0?static_cast<int>(gesture.x-gesture.dx):start_x;
            const int origin_y=gesture.start_y<0?static_cast<int>(gesture.y-gesture.dy):start_y;
            const int left=(screen_==Screen::Contents || screen_==Screen::OnlineChapters)?288:has_sidebar()?232:screen_==Screen::Reader?48:24;
            const int top=screen_==Screen::Reader?reading_area().top:screen_==Screen::Settings?80:(screen_==Screen::Contents || screen_==Screen::OnlineChapters)?136:
                (screen_==Screen::Bookmarks || (screen_==Screen::Library && !importing_))?100:144;
            const int bottom=screen_==Screen::Reader?reading_area().top+reading_area().height:480;
            if(!reader::ui::Rect{left,top,936-left,bottom-top}.contains(origin_x,origin_y))return true;
            const bool horizontal = std::abs(gesture.dx) > std::abs(gesture.dy);
            const bool forward = horizontal ? gesture.dx < 0 : gesture.dy < 0;
            if (screen_ == Screen::Reader) {
                if(std::max(std::abs(gesture.dx),std::abs(gesture.dy)) < 1.5f*std::min(std::abs(gesture.dx),std::abs(gesture.dy)))return true;
                if (horizontal) return input(forward ? SCE_CTRL_RIGHT : SCE_CTRL_LEFT);
                const auto amount = static_cast<std::size_t>(std::max(1,std::min(6,static_cast<int>(std::abs(gesture.dy)/40))));
                step_reading(forward,amount);
                message_.clear(); save();
            } else if (screen_ == Screen::Novel) {
                return input(forward ? SCE_CTRL_DOWN : SCE_CTRL_UP);
            } else if (screen_ == Screen::Settings) {
                if(horizontal) {
                    bool row_hit=false;
                    for(int i=0;i<5;++i)if(reader::ui::settings_row(i).contains(origin_x,origin_y)){set_focus(1000+i);row_hit=true;break;}
                    if(!row_hit)return true;
                }
                return input(horizontal ? (forward ? SCE_CTRL_RIGHT : SCE_CTRL_LEFT) : (forward ? SCE_CTRL_DOWN : SCE_CTRL_UP));
            } else if(screen_==Screen::Library && !importing_) {
                if(!horizontal)move_library_row(forward);
            } else if (screen_ == Screen::Library || screen_ == Screen::Contents || screen_ == Screen::Illustrations ||
                       screen_ == Screen::Catalog || screen_ == Screen::OnlineChapters || screen_ == Screen::Sources || screen_==Screen::Bookmarks) {
                turn_collection_page(forward);
            }
            return true;
        }
        if (gesture.kind != GestureKind::Tap) return true;
        if (screen_ == Screen::Reader) {
            const auto area = reading_area();
            if (!immersive_ && y < area.top && start_y<area.top) { show_more(); return true; }
            const reader::ui::Rect body{48,area.top,864,area.height};
            if(!body.contains(x,y) || !body.contains(start_x,start_y))return true;
            int block_y = area.top;
            for (auto at=top_;at<visible_end();++at) {
                const auto& block = lines_[at];
                if (block.illustration != reader::no_illustration && y >= block_y && y < block_y+block.height &&
                    (!block.image_width || std::abs(x-480) <= block.image_width/2)) {
                    if(start_y<block_y || start_y>=block_y+block.height || (block.image_width && std::abs(start_x-480)>block.image_width/2))return true;
                    show_illustration(block.illustration,Screen::Reader); return true;
                }
                block_y += block.height;
            }
            const auto zone=[](int px){return px<320?0:px<640?1:2;};
            if(zone(x)!=zone(start_x))return true;
            if (x >= 320 && x < 640) { show_more(); return true; }
            return input(x < 320 ? SCE_CTRL_LEFT : SCE_CTRL_RIGHT);
        }
        if (screen_ == Screen::Image) {
            const reader::ui::Rect body{48,reader::ui::image_top,864,reader::ui::image_height};
            if (body.contains(x,y) && body.contains(start_x,start_y)) {
                auto zoom=gesture; zoom.scale=zoom_>1 ? 1/zoom_ : 2; transform_image(zoom);
            }
            return true;
        }
        return true;
    }


private:

    enum class WidgetKind { Action, Sidebar, Item, Setting, Menu, Close, Dismiss, Description };
    struct Widget {
        reader::ui::FocusTarget target;
        Action action;
        WidgetKind kind;
        int index;
    };
    int focus_key() const {
        if(!menu_.empty()) return 100;
        if(online_busy_) return 101;
        if(feedback_active()) return 102;
        return static_cast<int>(screen_)+(importing_?32:0);
    }
    bool feedback_active() const { return !message_.empty() && message_!=notice_ && !online_busy_; }
    std::size_t page_count() const {
        if(screen_==Screen::Library)return order_.size();
        if(screen_==Screen::Contents)return publication_.chapters.size();
        if(screen_==Screen::Illustrations)return book_.illustrations.size();
        if(screen_==Screen::Bookmarks)return bookmarks().size();
        if(is_online_screen() && screen_!=Screen::Novel)return online_size();
        return 0;
    }
    std::size_t page_limit() const {
        return (screen_==Screen::Contents || screen_==Screen::OnlineChapters)?reader::ui::contents_page_size:screen_==Screen::Bookmarks?3:4;
    }
    std::size_t page_selection() const {
        if(screen_==Screen::Library)return selected_;
        if(screen_==Screen::Contents)return contents_selected_;
        if(screen_==Screen::Illustrations)return illustration_selected_;
        if(screen_==Screen::Bookmarks)return bookmark_selected_;
        return online_selected_;
    }
    bool paged_screen() const {
        return screen_==Screen::Library || screen_==Screen::Contents || screen_==Screen::Illustrations ||
            screen_==Screen::Bookmarks || (is_online_screen() && screen_!=Screen::Novel);
    }
    void ensure_library_visible() {
        if(importing_)return;
        if(order_.empty()){library_scroll_=0;return;}
        const int top=static_cast<int>(selected_/reader::ui::grid_columns)*reader::ui::grid_pitch;
        const int height=reader::ui::grid_bottom-reader::ui::grid_top;
        if(top<library_scroll_)library_scroll_=top;
        if(top+reader::ui::grid_height>library_scroll_+height)library_scroll_=top+reader::ui::grid_height-height;
        const int content=static_cast<int>((order_.size()+reader::ui::grid_columns-1)/reader::ui::grid_columns-1)*reader::ui::grid_pitch+reader::ui::grid_height;
        library_scroll_=std::max(0,std::min(library_scroll_,content-height));
    }
    std::size_t library_first() const { return library_scroll_/reader::ui::grid_pitch*reader::ui::grid_columns; }
    std::size_t library_end() const {
        const auto rows=(library_scroll_+reader::ui::grid_bottom-reader::ui::grid_top+reader::ui::grid_pitch-1)/reader::ui::grid_pitch;
        return std::min(order_.size(),static_cast<std::size_t>(rows*reader::ui::grid_columns));
    }
    void move_library_row(bool forward) {
        if(order_.empty())return;
        const auto columns=static_cast<std::size_t>(reader::ui::grid_columns);
        auto next=selected_;
        if(forward && (selected_/columns+1)*columns<order_.size())next=std::min(selected_+columns,order_.size()-1);
        if(!forward && selected_>=columns)next=selected_-columns;
        set_item(next);focus_memory_[static_cast<int>(Screen::Library)]=1000+selected_;
    }
    void set_item(std::size_t index) {
        if(screen_==Screen::Library){selected_=index;ensure_library_visible();update_cover();}
        else if(screen_==Screen::Contents)contents_selected_=index;
        else if(screen_==Screen::Illustrations)illustration_selected_=index;
        else if(screen_==Screen::Bookmarks)bookmark_selected_=index;
        else online_selected_=index;
    }
    void turn_collection_page(bool next) {
        if(screen_==Screen::Library && !importing_){move_library_row(next);return;}
        const auto count=page_count(),limit=page_limit(),selected=page_selection();
        if(!count)return;
        const auto first=selected/limit*limit,column=selected%limit;
        if(next && first+limit<count)set_item(std::min(first+limit+column,count-1));
        else if(!next && first>=limit)set_item(first-limit+column);
        // Keep focus on the selected book when paging from a shoulder shortcut.
        auto& id=focus_memory_[focus_key()];
        if(id>=1000)id=1000+static_cast<int>(page_selection());
    }
    std::vector<Widget> widgets() const {
        std::vector<Widget> result;
        const auto add=[&](int id,reader::ui::Rect box,Action action,WidgetKind kind=WidgetKind::Action,int index=0) {
            result.push_back({{id,box,action.enabled},std::move(action),kind,index});
        };
        if(!menu_.empty()) {
            const auto popup=menu_layout();
            for(int i=popup.first;i<popup.first+popup.visible && i<static_cast<int>(menu_.size());++i)
                add(2000+i,popup.row(i),menu_[i],WidgetKind::Menu,i);
            return result;
        }
        if(online_busy_) {
            add(4002,feedback_action(false),{"Cancel",SCE_CTRL_CIRCLE});
            return result;
        }
        if(feedback_active()) {
            if(retry_available_)add(4000,feedback_action(true),{"Retry",0,Command::Retry});
            add(4001,feedback_action(false),{"Dismiss"},WidgetKind::Dismiss);
            return result;
        }
        if(screen_==Screen::Reader || screen_==Screen::Image)return result;
        if(screen_==Screen::Home)
            add(1000,reader::ui::home_continue(),{"Continue",0,reader::ui::recent_book(entries_)<entries_.size()?Command::Resume:Command::Library});
        else if(screen_==Screen::Library && !importing_) {
            add(200,reader::ui::library_action(0),{"Add Book",0,Command::AddBook});
            for(auto i=library_first();i<library_end();++i) {
                auto box=reader::ui::grid_book(i,library_scroll_);
                const auto bottom=std::min(box.y+box.height,reader::ui::grid_bottom);
                box.y=std::max(box.y,reader::ui::grid_top);box.height=bottom-box.y;
                add(1000+i,box,{"Open",0,Command::Details},WidgetKind::Item,i);
            }
            if(order_.empty() && (!library_query_.empty() || filter_!=reader::LibraryFilter::All))
                add(204,{400,328,336,56},{library_search_active_?"Clear search":"Show all books",0,Command::ClearSearch});
        } else if(screen_==Screen::Book) {
            const bool available=!order_.empty() && entries_[order_[selected_]].error.empty();
            add(200,reader::ui::details_primary(false),{"Read",SCE_CTRL_CROSS,Command::Button,available});
            add(201,reader::ui::details_secondary(false),{"Chapters",0,Command::LocalContents,available});
        } else if(screen_==Screen::Settings) {
            for(int i=0;i<5;++i)add(1000+i,reader::ui::settings_row(i),{"Adjust"},WidgetKind::Setting,i);
        } else if(screen_==Screen::About) {
            for(int i=0;i<2;++i)add(1000+i,reader::ui::about_action(i),{"",0,i==0?Command::Credits:Command::License});
        } else if(screen_==Screen::Novel) {
            add(1000,details_description(online_novel_.title,true),{"Description"},WidgetKind::Description);
            add(200,reader::ui::details_primary(true),{online_started_?"Continue Reading":"Start Reading",0,Command::ReadOnline});
            add(201,reader::ui::details_secondary(true),{"Chapters",0,Command::OnlineContents});
            add(202,reader::ui::details_utility(0),{"Download all",0,Command::DownloadAll});
            add(203,reader::ui::details_utility(1),{"Refresh chapters",SCE_CTRL_TRIANGLE});
        } else {
            const auto limit=page_limit(),selected=page_selection(),count=page_count(),first=selected/limit*limit;
            for(auto i=first;i<count && i<first+limit;++i) {
                const int row=static_cast<int>(i-first);
                const auto box=(screen_==Screen::Contents || screen_==Screen::OnlineChapters)?reader::ui::contents_row(row):
                    screen_==Screen::Bookmarks?reader::ui::bookmark_row(row):
                    reader::ui::Rect{24,reader::ui::library_top+row*reader::ui::library_pitch,832,76};
                add(1000+i,box,{"Open",SCE_CTRL_CROSS},WidgetKind::Item,i);
            }
        }
        return result;
    }
    void normalize_focus() {
        sidebar_focus_=false;
        const auto controls=widgets();
        auto& id=focus_memory_[focus_key()];
        for(const auto& control:controls)if(control.target.id==id && control.target.enabled){set_focus(id);return;}
        int preferred=1000;
        if(!menu_.empty())preferred=2000+menu_selected_;
        else if(screen_==Screen::Novel)preferred=200;
        else if(paged_screen())preferred=1000+page_selection();
        for(const auto& control:controls)if(control.target.id==preferred && control.target.enabled){id=preferred;return;}
        for(const auto& control:controls)if(control.target.enabled && control.kind!=WidgetKind::Sidebar){id=control.target.id;if(!menu_.empty())set_focus(id);return;}
        id=controls.empty()?-1:controls.front().target.id;
    }
    void set_focus(int id) {
        focus_memory_[focus_key()]=id;
        if(!menu_.empty()) {
            if(id>=2000)menu_selected_=id-2000;
            return;
        }
        if(feedback_active() || online_busy_)return;
        sidebar_focus_=id>=100 && id<100+reader::ui::sidebar_count;
        if(sidebar_focus_)nav_selected_=id-100;
        const auto controls=widgets();
        for(const auto& control:controls)if(control.target.id==id) {
            if(control.kind==WidgetKind::Item)set_item(control.index);
            if(control.kind==WidgetKind::Setting)setting_row_=control.index;
            if(screen_==Screen::About && id>=1000)about_selected_=id-1000;
        }
    }
    void adjust_setting(int row,int delta) {
        int* value=row==0?&settings_.reading_font:row==1?&settings_.font:row==2?&settings_.spacing:
            row==3?&settings_.theme:&settings_.immersive;
        *value=std::max(0,std::min(row==4?1:2,*value+delta));
    }
    void cancel_settings() { settings_=previous_settings_; immersive_=settings_.immersive!=0; }
    void finish_settings(bool apply) {
        if(apply)apply_settings();else cancel_settings();
        screen_=settings_return_;nav_active_=nav_selected_=settings_nav_return_;
        sidebar_focus_=settings_sidebar_return_;
    }
    bool activate(const Widget& control) {
        switch(control.kind) {
        case WidgetKind::Sidebar: {
            navigate(control.index);sidebar_focus_=false;
            auto& id=focus_memory_[focus_key()];
            if(id>=100 && id<100+reader::ui::sidebar_count)id=0;
            normalize_focus();return true;
        }
        case WidgetKind::Item: set_item(control.index);return perform(control.action);
        case WidgetKind::Setting: return true;
        case WidgetKind::Menu: {
            const auto action=control.action;
            if(menu_opens_submenu(action.command)) {
                menu_parents_.push_back({menu_,menu_title_,menu_selected_,add_book_menu_});
                menu_.clear();focus_memory_.erase(100);
            } else close_menu();
            return perform(action);
        }
        case WidgetKind::Close: close_menu();return true;
        case WidgetKind::Dismiss: return perform({"Dismiss",0,Command::Dismiss});
        case WidgetKind::Description: return true;
        case WidgetKind::Action: return perform(control.action);
        }
        return true;
    }
    bool focus_input(unsigned pressed) {
        focus_result_=true;
        if(!menu_.empty()) {
            if(pressed&SCE_CTRL_START){close_menu();return true;}
            if(pressed&SCE_CTRL_CIRCLE){back_menu();return true;}
            normalize_focus();
            if(pressed&SCE_CTRL_CROSS) {
                for(const auto& control:widgets())if(control.target.id==2000+static_cast<int>(menu_selected_) && control.target.enabled){focus_result_=activate(control);break;}
            } else if(pressed&(SCE_CTRL_UP|SCE_CTRL_DOWN)) {
                const int step=pressed&SCE_CTRL_UP?-1:1;
                for(int i=static_cast<int>(menu_selected_)+step;i>=0 && i<static_cast<int>(menu_.size());i+=step)
                    if(menu_[i].enabled){set_focus(2000+i);break;}
            }
            return true;
        }
        if(screen_==Screen::Settings && menu_.empty() && !feedback_active() && !online_busy_ && (pressed&SCE_CTRL_CROSS)) {
            return perform({"Apply",SCE_CTRL_CROSS,Command::ApplySettings});
        }
        if(pressed&SCE_CTRL_CIRCLE) {
            if(!menu_.empty()){close_menu();return true;}
            if(feedback_active()){message_.clear();return true;}
            if(screen_==Screen::Settings){finish_settings(false);return true;}
        }
        const auto mask=SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_CROSS;
        if(!(pressed&mask))return feedback_active();
        normalize_focus();
        const auto controls=widgets();
        if(controls.empty())return false; // Reader turns pages; viewer pans.
        const int id=focus_memory_[focus_key()];
        const Widget* current=nullptr;
        std::vector<reader::ui::FocusTarget> targets;
        for(const auto& control:controls){targets.push_back(control.target);if(control.target.id==id)current=&control;}
        if(!current)return true;
        if(pressed&SCE_CTRL_CROSS){focus_result_=activate(*current);return true;}
        if(current->kind==WidgetKind::Setting && (pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))) {
            adjust_setting(current->index,pressed&SCE_CTRL_LEFT?-1:1);return true;
        }
        if(current->kind==WidgetKind::Description && (pressed&(SCE_CTRL_UP|SCE_CTRL_DOWN))) {
            if((pressed&SCE_CTRL_UP) && description_row_){--description_row_;return true;}
            if((pressed&SCE_CTRL_DOWN) && description_row_+description_visible()<description_lines().size()){++description_row_;return true;}
        }
        const auto direction=pressed&SCE_CTRL_LEFT?reader::ui::Direction::Left:pressed&SCE_CTRL_RIGHT?reader::ui::Direction::Right:
            pressed&SCE_CTRL_UP?reader::ui::Direction::Up:reader::ui::Direction::Down;
        const int next=reader::ui::focus_neighbor(targets,id,direction);
        if(current->kind==WidgetKind::Item && paged_screen()) {
            const bool grid=screen_==Screen::Library && !importing_;
            if(grid) {
                const auto column=selected_%reader::ui::grid_columns;
                if(pressed&SCE_CTRL_DOWN){move_library_row(true);return true;}
                if((pressed&SCE_CTRL_UP) && selected_>=reader::ui::grid_columns){move_library_row(false);return true;}
                if(pressed&SCE_CTRL_LEFT) {
                    if(column)set_focus(1000+selected_-1);
                    return true;
                }
                if(pressed&SCE_CTRL_RIGHT) {
                    if(column+1<reader::ui::grid_columns && selected_+1<order_.size())set_focus(1000+selected_+1);
                    return true;
                }
            }
            const auto first=page_selection()/page_limit()*page_limit();
            const auto end=std::min(first+page_limit(),page_count());
            if((pressed&SCE_CTRL_RIGHT) && (!grid || current->index+1==static_cast<int>(end))) {
                turn_collection_page(true);normalize_focus();return true;
            }
            if((pressed&SCE_CTRL_LEFT) && (!grid || current->index==static_cast<int>(first))) {
                turn_collection_page(false);normalize_focus();return true;
            }
            if(!grid && next==id && (pressed&(SCE_CTRL_UP|SCE_CTRL_DOWN))) {
                const bool forward=pressed&SCE_CTRL_DOWN;
                if((forward && end<page_count()) || (!forward && first>0)) {
                    set_item(forward?end:first-1);focus_memory_[focus_key()]=1000+static_cast<int>(page_selection());
                    return true;
                }
            }
        }
        set_focus(next);
        return true;
    }
    void draw_focus() const {
        if(!menu_.empty())return; // Popup rows already draw the selected state.
        const auto found=focus_memory_.find(focus_key());
        if(found==focus_memory_.end())return;
        for(const auto& control:widgets())if(control.target.id==found->second && control.target.enabled) {
            const auto& b=control.target.box;
            if(b.y>=reader::ui::toolbar_top)
                reader::visual::button_focus(b.x,reader::visual::hint_bar_top+5,b.width,38);
            else reader::visual::button_focus(b.x,b.y,b.width,b.height);
        }
    }

    bool has_sidebar() const {
        return screen_==Screen::Home || (screen_==Screen::Library && !importing_) ||
            screen_==Screen::Bookmarks || screen_==Screen::Settings || screen_==Screen::About;
    }
    void begin_import() {
        if (screen_==Screen::Settings) cancel_settings();
        import_selected_=selected_; import_filter_=filter_; import_query_=library_query_;
        save(); clear_reading(); release_image();
        screen_=Screen::Library; importing_=true; sidebar_focus_=false; browse_folder("");
    }
    void apply_settings() {
        immersive_=settings_.immersive!=0;
        if (!lines_.empty()) {
            const auto offset=lines_[top_].offset;
            try { reflow(offset); }
            catch (const std::exception&) {
                settings_=previous_settings_; immersive_=settings_.immersive!=0;
                message_="Layout too large. Previous settings restored.";
            }
            save();
        }
        if (!reader::save_settings(settings_path,settings_)) message_="Could not save settings. Check storage.";
    }
    void navigate(int destination) {
        const reader::perf::Scope timing(reader::perf::Event::Navigation);
        redraw_=true;
        const auto origin=screen_;const int origin_nav=nav_active_;
        if(origin==Screen::Settings && destination==3)return;
        if(screen_==Screen::Library && !importing_)
            library_views_[nav_active_]={selected_filename(),library_query_,filter_,library_search_active_};
        if (screen_==Screen::Settings) cancel_settings();
        save();if(destination!=3)clear_reading();release_image();message_.clear();nav_selected_=nav_active_=destination;
        if (importing_) { importing_=false; scan(); }
        if (destination==0) screen_=Screen::Home;
        else if (destination==1) {
            screen_=Screen::Library;
            const auto view=library_views_.find(destination);
            library_query_=view==library_views_.end()?"":view->second.query;
            library_search_active_=view!=library_views_.end() && view->second.search_active;
            filter_=view==library_views_.end()?reader::LibraryFilter::All:view->second.filter;
            selected_=0;
            const auto keep=view==library_views_.end()?"":view->second.filename;
            if(collection_dirty_)scan(keep);else ensure_library_order(keep);
            update_cover();save_library_preferences();

        } else if (destination==2) {
            screen_=Screen::Bookmarks; bookmarks_return_=Screen::Home;bookmarks_nav_return_=0; bookmark_selected_=0;
        } else if (destination==3) {
            previous_settings_=settings_;settings_return_=origin==Screen::Settings?Screen::Home:origin;
            settings_nav_return_=origin_nav;settings_sidebar_return_=true;setting_row_=0;screen_=Screen::Settings;
        } else screen_=Screen::About;
        ++folder_revision_;
    }
    void clear_reading() {
        const reader::perf::Scope timing(reader::perf::Event::Cleanup);
        speech_.stop();
        bookmarks_return_=Screen::Home;
        inline_images_.clear();book_={};publication_={};
        std::vector<reader::LayoutBlock>().swap(lines_);pages_.clear();active_filename_.clear();
        persisted_filename_.clear();
    }
    const std::vector<std::size_t>& bookmarks() const {
        if(bookmarks_dirty_) {
            bookmark_indices_.clear();
            for (std::size_t i=0;i<entries_.size();++i)
                if (!entries_[i].directory && entries_[i].progress.has_bookmark) bookmark_indices_.push_back(i);
            bookmarks_dirty_=false;
        }
        return bookmark_indices_;
    }
    void open_bookmark(std::size_t entry) {
        const auto filename=entries_[entry].filename;
        library_query_.clear();
        library_search_active_=false;
        filter_=reader::LibraryFilter::All; refresh_library(filename); update_cover();
        if (!active_filename_.empty()) save();
        open();
        if (screen_==Screen::Reader) input(SCE_CTRL_SELECT,false);
        else if (screen_==Screen::Novel) { pending_bookmark_=true; resume_online(); }
    }
    void delete_bookmark() {
        const auto& marks=bookmarks();
        if (bookmark_selected_>=marks.size()) return;
        auto& item=entries_[marks[bookmark_selected_]];
        auto saved=item.progress; saved.has_bookmark=false;
        if (!reader::save_progress(reader::progress_path(progress_directory,item.filename),saved)) {
            message_="Could not remove bookmark. Check storage."; return;
        }
        item.progress=saved;
        bookmarks_dirty_=true;
        if (active_filename_==item.filename) progress_.has_bookmark=false;
        if (bookmark_selected_ && bookmark_selected_+1==marks.size()) --bookmark_selected_;
        notify("Bookmark removed.");
    }
    std::string bookmark_key(const reader::LibraryItem& item) const {
        return item.path+":"+std::to_string(item.progress.bookmark_chapter)+":"+std::to_string(item.progress.bookmark);
    }
    std::string bookmark_quote(const reader::Book& book,std::size_t offset,const std::string& fallback) const {
        auto at=std::min(offset,book.text.size());
        while(at<book.text.size() && (static_cast<unsigned char>(book.text[at])&0xc0)==0x80)++at;
        auto end=std::min(at+240,book.text.size());
        while(end<book.text.size() && end>at && (static_cast<unsigned char>(book.text[end])&0xc0)==0x80)--end;
        auto quote=book.text.substr(at,end-at);
        for(auto& c:quote)if(c=='\n' || c=='\r' || c=='\t')c=' ';
        return quote.empty()?fallback:"“"+quote+"”";
    }
    void remember_bookmark(const reader::LibraryItem& item) {
        if(!progress_.has_bookmark || progress_.bookmark_chapter!=chapter_)return;
        const auto key=bookmark_key(item);
        for(auto& preview:bookmark_previews_)if(preview.key==key) {
            preview.quote=bookmark_quote(book_,progress_.bookmark,item.title);return;
        }
        if(bookmark_previews_.size()>=24)bookmark_previews_.erase(bookmark_previews_.begin());
        bookmark_previews_.push_back({key,bookmark_quote(book_,progress_.bookmark,item.title)});
    }
    void sync_bookmark_previews() {
        if(screen_!=Screen::Bookmarks)return;
        const auto& marks=bookmarks(); const auto first=bookmark_selected_/3*3;
        for(auto i=first;i<marks.size() && i<first+3;++i) {
            const auto& item=entries_[marks[i]];const auto key=bookmark_key(item);
            auto found=std::find_if(bookmark_previews_.begin(),bookmark_previews_.end(),[&](const BookmarkPreview& p){return p.key==key;});
            if(found!=bookmark_previews_.end()) {
                std::rotate(found,found+1,bookmark_previews_.end());continue;
            }
            BookmarkPreview preview{key,item.title};
            const reader::perf::Scope timing(reader::perf::Event::BookmarkPreview);
            try {
                reader::Publication publication;reader::Book book;std::string error;auto saved=item.progress;
                if(reader::open_publication(item.path,publication,error)) {
                    if(publication.online)reader::online::restore_progress(publication,saved);
                    if(saved.has_bookmark && saved.bookmark_chapter<publication.chapters.size() && reader::load_chapter(publication,saved.bookmark_chapter,book,error)) {
                        preview.quote=bookmark_quote(book,saved.bookmark,item.title);
                    }
                }
            } catch(const std::exception&) { /* A saved position is still navigable. */ }
            if(bookmark_previews_.size()>=24)bookmark_previews_.erase(bookmark_previews_.begin());
            bookmark_previews_.push_back(std::move(preview));
        }
    }
    void sync_covers() {
        std::vector<const reader::LibraryItem*> wanted;
        if (screen_==Screen::Home) {
            const auto recent=reader::ui::recent_book(entries_);
            if (recent<entries_.size()) wanted.push_back(&entries_[recent]);
        } else if (screen_==Screen::Library && !importing_) {
            for (auto i=library_first();i<library_end();++i) wanted.push_back(&entries_[order_[i]]);
        }
        for (const auto* entry:wanted) {
            const auto found=std::find_if(covers_.begin(),covers_.end(),[&](const CoverTexture& cover){return cover.key==entry->path;});
            if(found!=covers_.end()) {std::rotate(found,found+1,covers_.end());continue;}
            CoverTexture cover{entry->path,{}};
            if (!entry->cover_path.empty()) {
                const reader::perf::Scope timing(reader::perf::Event::CoverDecode);
                try {
                    std::string bytes,error;
                    if (load_cover_bytes(*entry,bytes,error))
                        cover.texture.reset(decode_thumbnail(bytes,144,200,error));
                } catch (const std::exception&) { /* A title card remains available. */ }
            }
            // 12 RGBA thumbnails of at most 144x200: <= 1.4 MiB plus padding.
            if(covers_.size()>=12)covers_.erase(covers_.begin());
            covers_.push_back(std::move(cover));
        }
    }
    void draw_cover(const reader::LibraryItem& item,int x,int y,int width,int height,bool selected=false) const {
        using namespace reader::visual;
        panel(x,y,width,height,selected);
        vita2d_texture* texture=nullptr;
        for (const auto& cover:covers_) if (cover.key==item.path) {texture=cover.texture.get();break;}
        if ((screen_==Screen::Book || screen_==Screen::Novel || screen_==Screen::Contents || screen_==Screen::OnlineChapters) && image_) texture=image_;
        const bool grid=screen_==Screen::Library && !importing_;
        vita2d_enable_clipping(); vita2d_set_clip_rectangle(x+3,grid?std::max(y+3,reader::ui::grid_top):y+3,
            x+width-3,grid?std::min(y+height-3,reader::ui::grid_bottom):y+height-3);
        if (texture) {
            const float scale=std::max(float(width-6)/vita2d_texture_get_width(texture),float(height-6)/vita2d_texture_get_height(texture));
            vita2d_draw_texture_scale(texture,x+width/2-vita2d_texture_get_width(texture)*scale/2,
                y+height/2-vita2d_texture_get_height(texture)*scale/2,scale,scale);
        } else {
            const int pixels=width>100?24:20,pitch=pixels+2;
            const auto rows=ui_wrap(item.title,width-20,pixels,reader::FontRole::Interface);
            const auto count=std::min<std::size_t>(rows.size(),3);
            int ty=y+(height-(static_cast<int>(count)-1)*pitch)/2+pixels/3;
            for (std::size_t i=0;i<count;++i) {
                const auto w=fonts_.width(reader::FontRole::Interface,pixels,rows[i].text);
                fonts_.draw(reader::FontRole::Interface,pixels,x+(width-w)/2,ty,reader::visual::text,rows[i].text);ty+=pitch;
            }
        }
        if(grid)vita2d_set_clip_rectangle(232,reader::ui::grid_top,936,reader::ui::grid_bottom);
        else vita2d_disable_clipping();
    }
    void draw_background() const {
        const int top=has_sidebar()?0:44;
        reader::visual::gradient(0,top,960,544-top,rgb(22,20,31),rgb(32,27,46));
        if (screen_==Screen::Home && backdrop_) {
            vita2d_draw_texture_scale(backdrop_.get(),0,0,960.f/vita2d_texture_get_width(backdrop_.get()),544.f/vita2d_texture_get_height(backdrop_.get()));
        }
    }
    void draw_sidebar() const {
        using namespace reader::visual;
        vita2d_draw_rectangle(0,0,208,hint_bar_top,rgb(24,21,32));
        vita2d_draw_line(207,0,207,hint_bar_top,rgb(47,39,62));
        this->text(16,40,palette().text,0.92f,"NovelReader",176,reader::FontRole::Heading);
        const char* names[]={"Home","Library","Bookmarks","Settings","About"};
        const Icon icons[]={Icon::Home,Icon::Library,Icon::Bookmark,Icon::Settings,Icon::Info};
        for (int i=0;i<reader::ui::sidebar_count;++i) {
            const auto box=reader::ui::sidebar_item(i);
            const auto memory=focus_memory_.find(focus_key());
            const bool focused=memory!=focus_memory_.end() && memory->second==100+i && menu_.empty() && !feedback_active();
            if(i==nav_active_ || focused)
                reader::visual::button(box.x,box.y,box.width,box.height,
                    focused?ButtonState::Focused:ButtonState::Normal);
            icon(icons[i],42,box.y+28,reader::visual::text,0.85f);
            this->text(70,box.y+36,palette().text,0.92f,names[i],120);
        }
    }
    void draw_chrome() const {
        if(has_sidebar())return;
        using namespace reader::visual;
        gradient(0,0,960,44,rgb(40,35,51),rgb(29,26,38));
        this->text(16,31,palette().text,0.92f,"NovelReader",202,reader::FontRole::Heading);
        if (screen_==Screen::Reader) {
            const auto title=fit(publication_.title,0.96f,520,reader::FontRole::Interface);
            const int width=fonts_.width(reader::FontRole::Interface,25,title);
            this->text(480-width/2,31,palette().text,0.96f,title,520,reader::FontRole::Interface);
        }
        vita2d_draw_line(0,43,960,43,rgb(60,50,73));
    }
    void draw_home() const {
        using namespace reader::visual;
        this->text(232,64,palette().text,1.3f,"Welcome Back",664,reader::FontRole::Heading);
        this->text(232,96,palette().muted,0.85f,"Pick up your story where you left off.",664);
        const auto box=reader::ui::home_continue();
        reading_card(box.x,box.y,box.width,box.height);
        const auto recent=reader::ui::recent_book(entries_);
        icon(Icon::PlayCircle,box.x+28,box.y+30,reader::visual::text,0.9f);
        this->text(box.x+54,box.y+38,palette().text,0.92f,recent<entries_.size()?"Continue Reading":"Browse Library",box.width-74,reader::FontRole::Heading);
        if(recent<entries_.size()) {
            const auto& item=entries_[recent];
            draw_cover(item,box.x+20,box.y+64,100,152);
            const int left=box.x+140,width=box.width-160;
            const auto rows=ui_wrap(item.title,width,25,reader::FontRole::Heading,true);
            for(std::size_t i=0;i<std::min<std::size_t>(2,rows.size());++i) {
                auto line=i==1?item.title.substr(rows[i].offset):rows[i].text;
                if(fonts_.title_width(25,line)>width) {
                    while(!line.empty() && fonts_.title_width(25,line+"...")>width) {
                        auto last=line.size()-1;
                        while(last && (static_cast<unsigned char>(line[last])&0xc0)==0x80)--last;
                        line.resize(last);
                    }
                    line+="...";
                }
                fonts_.draw_title(25,left,box.y+94+static_cast<int>(i)*29,palette().text,line);
            }
            this->text(left,box.y+174,palette().text,21.f/26,"Chapter "+std::to_string(item.progress.chapter+1)+" of "+std::to_string(item.chapters),width);
            const auto percent=item.progress.percent>=0?std::to_string(item.progress.percent)+"%":"Started";
            const int percent_width=fonts_.width(reader::FontRole::Interface,21,percent);
            const int percent_x=box.x+box.width-20-percent_width;
            progress(left,box.y+200,percent_x-left-14,std::max(0,item.progress.percent)/100.f,6);
            this->text(percent_x,box.y+210,palette().text,0.80f,percent,percent_width);
        } else {
            this->text(box.x+24,box.y+98,palette().text,1.0f,"Your next chapter awaits.",box.width-48,reader::FontRole::Heading);
            this->text(box.x+24,box.y+138,palette().muted,0.85f,"Open Library to find or add your first book.",box.width-48);
            this->text(box.x+24,box.y+192,palette().accent,0.92f,"Browse Library  ›",box.width-48,reader::FontRole::Heading);
        }
    }
    void draw_bookmarks() const {
        using namespace reader::visual;
        this->text(232,64,palette().text,1.15f,"Bookmarks",580,reader::FontRole::Heading);
        const auto& marks=bookmarks();
        this->text(232,88,palette().muted,0.68f,std::to_string(marks.size())+" saved passages",560);
        const auto first=bookmark_selected_/3*3;
        for (auto i=first;i<marks.size() && i<first+3;++i) {
            const auto& item=entries_[marks[i]]; const int y=reader::ui::bookmark_row(i-first).y;
            panel(232,y,704,96,!sidebar_focus_ && i==bookmark_selected_);
            icon(Icon::Bookmark,254,y+29,palette().accent,0.65f);
            std::string quote=item.title;const auto key=bookmark_key(item);
            for(const auto& preview:bookmark_previews_)if(preview.key==key){quote=preview.quote;break;}
            this->text(278,y+29,palette().text,0.79f,quote,625,reader::FontRole::Interface);
            this->text(278,y+57,palette().muted,0.74f,item.title,618);
            this->text(278,y+80,palette().accent,0.65f,"Chapter "+std::to_string(item.progress.bookmark_chapter+1)+(item.error.empty()?" · Continue from this bookmark":" · Book unavailable"),618);
        }
        if (marks.empty()) {
            icon(Icon::Bookmark,576,255,palette().accent,2);
            this->text(395,325,palette().text,1.f,"Keep a favorite passage.",450,reader::FontRole::Heading);
            this->text(296,363,palette().muted,0.8f,"Reading controls → Save bookmark keeps your place.",616);
        }
    }
    void draw_about() const {
        using namespace reader::visual;
        this->text(232,64,palette().text,1.15f,"About",600,reader::FontRole::Heading);
        draw_logo(393,172,86,0xffffffffu);
        this->text(302,258,palette().text,1.15f,"NovelReader",282,reader::FontRole::Heading);
        this->text(369,285,palette().muted,0.72f,"v0.7.0",140);
        this->text(272,324,palette().text,0.73f,"A simple and comfortable novel reading",330);
        this->text(325,347,palette().text,0.73f,"experience for PS Vita.",240);
        const char* labels[]={"Credits","License"}; const Icon icons[]={Icon::Info,Icon::Document};
        for(int i=0;i<2;++i) {
            const auto box=reader::ui::about_action(i);
            panel(box.x,box.y,box.width,box.height,!sidebar_focus_ && about_selected_==i);
            icon(icons[i],663,box.y+28,palette().text,0.72f);
            this->text(689,box.y+36,palette().text,0.84f,labels[i],205);
            icon(Icon::Chevron,903,box.y+28,palette().muted,0.8f);
        }
    }
    bool is_online_screen() const {
        return screen_ == Screen::Sources || screen_ == Screen::Catalog || screen_ == Screen::Novel || screen_ == Screen::OnlineChapters;
    }
    std::size_t online_size() const {
        return screen_ == Screen::Sources ? reader::online::sources().size() : screen_ == Screen::Catalog ? online_catalog_.items.size() : online_novel_.chapters.size();
    }
    reader::ui::ReadingArea reading_area() const { return reader::ui::reading_area(immersive_); }
    void notify(const std::string& value) {
        redraw_=true;
        message_ = notice_ = value;
        notice_until_ = sceKernelGetProcessTimeWide() + 2800000;
    }
    reader::ui::Popup menu_layout() const { return {static_cast<int>(menu_.size()),static_cast<int>(menu_selected_)}; }
    void close_menu() { menu_.clear(); menu_parents_.clear(); add_book_menu_=false; focus_memory_.erase(100); ++folder_revision_; }
    void back_menu() {
        if(menu_parents_.empty()){close_menu();return;}
        auto parent=std::move(menu_parents_.back());menu_parents_.pop_back();
        menu_=std::move(parent.actions);menu_title_=std::move(parent.title);menu_selected_=parent.selected;add_book_menu_=parent.add_book;
        focus_memory_[100]=2000+menu_selected_;++folder_revision_;
    }
    void show_add_book() {
        focus_memory_.erase(100);
        add_book_menu_=true;
        menu_title_="Add Book";
        menu_={{"Import from Device",0,Command::Import},{"Browse Online",0,Command::Online}};
        menu_selected_=0;++folder_revision_;
    }
    void show_sort() {
        add_book_menu_=false;
        focus_memory_.erase(100);
        menu_title_ = "Sort and filter books";
        menu_ = {{"Sort: Title",0,Command::SortTitle},{"Sort: Recently read",0,Command::SortRecent},
            {"Sort: Reading progress",0,Command::SortProgress},{"Filter: All books",0,Command::FilterAll},
            {"Filter: Reading",0,Command::FilterReading},{"Filter: Finished",0,Command::FilterFinished}};
        menu_selected_ = static_cast<std::size_t>(sort_); ++folder_revision_;
    }
    void show_speech() {
        focus_memory_.erase(100);
        menu_title_ = "Read aloud";
        const auto state = speech_.status().state;
        const bool playing = state == reader::speech::State::Speaking;
        const bool paused = state == reader::speech::State::Paused;
        menu_ = {{playing ? "Pause narration" : paused ? "Resume narration" : "Start here",0,Command::SpeechToggle,!lines_.empty()},
            {"Stop narration",0,Command::SpeechStop,playing || paused},
            {"Slower ("+std::to_string(settings_.speech_rate)+" wpm)",0,Command::SpeechSlower,settings_.speech_rate>100},
            {"Faster ("+std::to_string(settings_.speech_rate)+" wpm)",0,Command::SpeechFaster,settings_.speech_rate<350}};
        menu_selected_=0; ++folder_revision_;
    }
    void tick_speech() {
        using reader::speech::State;
        auto status = speech_.status();
        if (screen_ != Screen::Reader) {
            if (status.state != State::Idle) speech_.stop();
            speech_seen_ = State::Idle; return;
        }
        if ((status.state == State::Speaking || status.state == State::Paused || status.state == State::Finished) && !lines_.empty()) {
            // Follow the spoken sentence only when it advances, allowing manual page browsing.
            if (status.offset != speech_offset_) {
                redraw_=true;
                speech_offset_ = status.offset;
                top_ = reader::block_at_offset(lines_, status.offset);
            }
        }
        if (status.state != speech_seen_) {
            redraw_=true;
            if (status.state == State::Error) notify(status.error);
            if (status.state == State::Finished) { save(); notify("End of narration for this chapter."); }
            speech_seen_ = status.state;
            if (!menu_.empty() && menu_title_ == "Read aloud") show_speech();
        }
    }
    void show_more() {
        add_book_menu_=false;
        focus_memory_.erase(100);
        menu_title_ = screen_ == Screen::Reader ? "Reading controls" : "Options";
        if (screen_ == Screen::Library && importing_) {
            menu_={{"Back",SCE_CTRL_CIRCLE},{"Refresh folder",SCE_CTRL_TRIANGLE},{"Sort files",0,Command::Sort},{"Save and exit",SCE_CTRL_START}};
        } else if (screen_ == Screen::Library) {
            menu_={{"Continue reading",0,Command::Resume,reader::ui::recent_book(entries_)<entries_.size()},
                {"Book details",0,Command::Details,!order_.empty()},
                {"Import books",0,Command::Import},{"Online novels",SCE_CTRL_SELECT},{"Sort books",0,Command::Sort},{"Search library",0,Command::Search}};
        } else if (screen_ == Screen::Reader) {
            menu_={{"Read aloud",0,Command::Speech},{"Contents",SCE_CTRL_CROSS},{"Illustrations",0,Command::Illustrations},{"Reading settings",0,Command::Appearance},{"Save bookmark",0,Command::SaveBookmark},
                {"Go to bookmark",SCE_CTRL_SELECT,Command::Button,progress_.has_bookmark},{"All bookmarks",0,Command::Bookmarks},{publication_.online?"Back to chapters":"Back to library",SCE_CTRL_CIRCLE},
                {immersive_ ? "Show reading bar" : "Hide reading bar",0,Command::Immersive}};
        } else if (screen_ == Screen::Catalog) {
            menu_={{"Search",SCE_CTRL_SQUARE},{online_catalog_.next.empty()?"Reload catalog":"Next catalog page",SCE_CTRL_TRIANGLE},{"Initial catalog",SCE_CTRL_SELECT}};
        } else if (screen_ == Screen::Novel) {
            menu_={{online_started_?"Resume reading":"Start reading",SCE_CTRL_SELECT},{"Chapters",SCE_CTRL_CROSS},
                {in_library()?"In your library":"Add to library",SCE_CTRL_SQUARE,Command::Button,!in_library()},
                {"Download all",0,Command::DownloadAll},{"Refresh chapters",SCE_CTRL_TRIANGLE}};
            if(in_library())menu_.push_back({"Remove from library",0,Command::RemoveBook});
        } else if (screen_ == Screen::OnlineChapters) {
            menu_={{"Read chapter",SCE_CTRL_CROSS},{"Download chapter",SCE_CTRL_SQUARE},{"Download all",SCE_CTRL_SELECT},
                {"Jump to chapter",0,Command::Jump},{"Refresh chapters",SCE_CTRL_TRIANGLE}};
            if(in_library())menu_.push_back({"Remove from library",0,Command::RemoveBook});
        } else if (screen_ == Screen::Contents) {
            menu_={{"Jump to chapter",0,Command::Jump},{"Illustrations",SCE_CTRL_SQUARE},{"Back to reading",SCE_CTRL_CIRCLE}};
        } else if (screen_ == Screen::Image) {
            menu_={{"Zoom in",SCE_CTRL_CROSS,Command::Button,zoom_<4},{"Zoom out",SCE_CTRL_SQUARE,Command::Button,zoom_>1},{"Fit image",SCE_CTRL_TRIANGLE},
                {"Previous image",SCE_CTRL_LTRIGGER,Command::Button,illustration_selected_>0},{"Next image",SCE_CTRL_RTRIGGER,Command::Button,illustration_selected_+1<book_.illustrations.size()},{"Back",SCE_CTRL_CIRCLE}};
        } else if(screen_==Screen::Book) {
            const bool available=!order_.empty() && entries_[order_[selected_]].error.empty();
            menu_={{"Read book",SCE_CTRL_CROSS,Command::Button,available},{"Full title",0,Command::FullTitle},
                {"Save bookmark",0,Command::BookBookmark,available},
                {"Remove from library",0,Command::RemoveBook,!order_.empty() && !entries_[order_[selected_]].guide},
                {"Back to library",SCE_CTRL_CIRCLE}};
        } else if(screen_==Screen::Settings) {
            menu_={{"Apply changes",0,Command::ApplySettings},{"Cancel changes",0,Command::CancelSettings},
                {"Reset reading settings",0,Command::ResetSettings}};
        } else if (screen_==Screen::Home || screen_==Screen::About) {
            menu_={{"Library",0,Command::Library},
                {"Reading settings",0,Command::Appearance},{"Bookmarks",0,Command::Bookmarks}};
        } else if (screen_==Screen::Bookmarks) {
            menu_={{"Open bookmark",SCE_CTRL_CROSS},{"Remove bookmark",0,Command::DeleteBookmark,!bookmarks().empty()},
                {"Library",0,Command::Library},{"Home",0,Command::Home}};
        } else { menu_={{"Back",SCE_CTRL_CIRCLE},{"Home",0,Command::Home}}; }
        for (auto& action:menu_) if (action.button==SCE_CTRL_START) {action.button=0;action.command=Command::Exit;}
        if (std::none_of(menu_.begin(),menu_.end(),[](const Action& action){return action.command==Command::Exit;}))
            menu_.push_back({"Save and exit",0,Command::Exit});
        if (retry_available_ && (is_online_screen() || screen_ == Screen::Reader)) menu_.insert(menu_.begin(),{"Retry request",0,Command::Retry});
        menu_selected_=0; ++folder_revision_;
    }
    bool perform(const Action& action) {
        if (!action.enabled) return true;
        redraw_=true;
        pressed_action_ = action.label; pressed_until_ = sceKernelGetProcessTimeWide()+160000;
        switch (action.command) {
        case Command::CloseMenu: close_menu();break;
        case Command::ReadOnline: resume_online();break;
        case Command::LocalContents: open_local_contents();break;
        case Command::OnlineContents: screen_=Screen::OnlineChapters;online_selected_=online_saved_chapter_;message_.clear();break;
        case Command::Dismiss: message_.clear();break;
        case Command::SaveBookmark:
            if(!lines_.empty()) {
                progress_.has_bookmark=true;progress_.bookmark=lines_[top_].offset;progress_.bookmark_chapter=chapter_;
                save();notify("Bookmark saved. SELECT returns here.");
            }
            break;
        case Command::Speech: show_speech(); break;
        case Command::SpeechToggle: {
            const auto state = speech_.status().state;
            if (state == reader::speech::State::Speaking) speech_.pause(true);
            else if (state == reader::speech::State::Paused) speech_.pause(false);
            else if (!lines_.empty()) {
                speech_offset_ = lines_[top_].offset;
                speech_seen_ = reader::speech::State::Speaking;
                speech_.start(book_, lines_[top_].offset, settings_.speech_rate);
            }
            show_speech(); break;
        }
        case Command::SpeechStop: speech_.stop(); save(); show_speech(); break;
        case Command::SpeechSlower: case Command::SpeechFaster:
            settings_.speech_rate = std::max(100, std::min(350, settings_.speech_rate + (action.command == Command::SpeechSlower ? -25 : 25)));
            speech_.rate(settings_.speech_rate);
            if (!reader::save_settings(settings_path, settings_)) notify("Could not save narration speed. Check storage.");
            show_speech(); break;
        case Command::AddBook: show_add_book();break;
        case Command::Online: return input(SCE_CTRL_SELECT,false);
        case Command::ClearSearch:
            if(library_search_active_){exit_library_search();break;}
            library_query_.clear();filter_=reader::LibraryFilter::All;selected_=0;
            refresh_library("");update_cover();save_library_preferences();break;
        case Command::PreviousPage: turn_collection_page(false);break;
        case Command::NextPage: turn_collection_page(true);break;
        case Command::ApplySettings: finish_settings(true);break;
        case Command::CancelSettings: finish_settings(false);break;
        case Command::ResetSettings: {
            const int sort=settings_.library_sort,filter=settings_.library_filter;
            settings_=reader::Settings{};settings_.library_sort=sort;settings_.library_filter=filter;break;
        }
        case Command::FullTitle:
            if(!order_.empty())message_=entries_[order_[selected_]].title;
            break;
        case Command::Illustrations:
            illustration_selected_=0;screen_=Screen::Illustrations;message_.clear();break;

        case Command::BookBookmark:
            if(!order_.empty()) {
                auto& item=entries_[order_[selected_]];auto saved=item.progress;
                if(!item.error.empty()){message_=item.error;break;}
                saved.has_bookmark=true;saved.bookmark=saved.offset;saved.bookmark_chapter=saved.chapter;saved.bookmark_id=saved.chapter_id;
                if(reader::save_progress(reader::progress_path(progress_directory,item.filename),saved)) {
                    item.progress=saved;bookmarks_dirty_=true;if(item.filename==active_filename_)progress_=saved;notify("Reading position bookmarked.");
                } else message_="Could not save bookmark. Check storage.";
            }
            break;
        case Command::RemoveBook:
            if(!removal_filename().empty()) {
                menu_title_="Are you sure?";
                menu_={{"Yes, remove reference",0,Command::ConfirmRemoveBook},{"No, keep it",0}};menu_selected_=1;focus_memory_.erase(100);++folder_revision_;
            }
            break;
        case Command::ConfirmRemoveBook:
            if(!library_writable_){message_="Library index is damaged. Restore it before changing the collection.";break;}
            if(!removal_filename().empty()) {
                auto books=library_books_;const auto filename=removal_filename();
                books.erase(std::remove(books.begin(),books.end(),filename),books.end());
                if(reader::save_library(library_path,books)) {
                    library_books_=std::move(books);navigate(1);sidebar_focus_=false;scan();notify("Removed from Library. The book file is kept.");
                } else message_="Could not update Library. Check storage.";
            }
            break;
        case Command::Search: search_library(); break;
        case Command::Exit: online_.cancel(); if (screen_==Screen::Settings) cancel_settings(); save(); return false;
        case Command::Home: navigate(0); sidebar_focus_=true; break;
        case Command::Library: navigate(1); sidebar_focus_=false; break;
        case Command::Import: begin_import(); break;
        case Command::Bookmarks:
            bookmarks_return_=screen_;bookmarks_nav_return_=nav_active_; save(); screen_=Screen::Bookmarks; bookmark_selected_=0;
            nav_active_=nav_selected_=2; sidebar_focus_=false; break;
        case Command::DeleteBookmark: delete_bookmark(); break;
        case Command::Credits:
            message_="Built with VitaSDK and vita2d. Fonts: Source Sans 3, Literata and Atkinson. EPUB: TinyXML2 and Minizip. Online: Lua, curl and Mbed TLS. Offline speech: Flite 2.2, English SLT voice."; break;
        case Command::License:
            message_="Flite / SLT: CMU permissive license; notices included in app0:/licenses. Fonts: SIL OFL 1.1. Third-party notices and sources are in the source distribution. Books retain their original copyrights."; break;
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
            if (screen_!=Screen::Settings) { settings_return_=screen_; previous_settings_=settings_;settings_nav_return_=nav_active_;settings_sidebar_return_=sidebar_focus_; }
            setting_row_=0; screen_=Screen::Settings; nav_active_=nav_selected_=3; sidebar_focus_=false; break;
        case Command::Retry: online_read_in_place_=retry_in_place_; start_online(retry_request_); break;
        case Command::SortTitle: case Command::SortRecent: case Command::SortProgress: {
            const auto keep=selected_filename();
            sort_=action.command==Command::SortTitle?reader::LibrarySort::Title:action.command==Command::SortRecent?reader::LibrarySort::Recent:reader::LibrarySort::Progress;
            refresh_library(keep); update_cover(); save_library_preferences(); break;
        }
        case Command::FilterAll: case Command::FilterReading: case Command::FilterFinished:
            filter_=action.command==Command::FilterAll?reader::LibraryFilter::All:
                action.command==Command::FilterReading?reader::LibraryFilter::Reading:reader::LibraryFilter::Finished;
            selected_=0;refresh_library("");update_cover();save_library_preferences();break;
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
    std::string removal_filename() const {
        const auto filename=(screen_==Screen::Novel || screen_==Screen::OnlineChapters)?
            (in_library()?online_manifest_.substr(books_directory.size()+1):std::string()):selected_filename();
        for(const auto& item:entries_)if(item.filename==filename && item.guide)return {};
        return filename;
    }
    void resume_recent() {
        const auto recent = reader::ui::recent_book(entries_);
        if (recent == entries_.size()) { notify("Open a book to start your reading journey."); return; }
        const auto filename = entries_[recent].filename;
        library_query_.clear();
        library_search_active_=false;
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
        ime_jump_ = true; ime_library_=false;
        if (sceImeDialogInit(&param) >= 0) ime_active_ = true;
        else message_ = "Could not open chapter keyboard. Use left/right to browse chapters.";
    }
    std::vector<Action> menu_toolbar() const {
        return {{"Select",SCE_CTRL_CROSS},{"Close",SCE_CTRL_CIRCLE,Command::CloseMenu}};
    }
    std::vector<Action> toolbar() const {
        if(!menu_.empty())return menu_toolbar();
        if(ime_active_)return {};
        if(online_busy_)return {{"Cancel",SCE_CTRL_CIRCLE}};
        if(feedback_active())return {{"Select",SCE_CTRL_CROSS},{"Close",SCE_CTRL_CIRCLE}};
        const Action options{"Options",SCE_CTRL_START,Command::More};
        const auto confirm=[&](const std::string& fallback) {
            const auto focus=focus_memory_.find(focus_key());
            if(focus!=focus_memory_.end())for(const auto& control:widgets())if(control.target.id==focus->second && control.kind==WidgetKind::Action)
                return Action{control.action.label,SCE_CTRL_CROSS,Command::Button,control.action.enabled};
            return Action{fallback,SCE_CTRL_CROSS};
        };
        if(screen_==Screen::Reader || screen_==Screen::Image)return {{screen_==Screen::Reader?"Contents":"Zoom in",SCE_CTRL_CROSS},
            {"Back",SCE_CTRL_CIRCLE},{screen_==Screen::Reader?"Controls":"Options",SCE_CTRL_START,Command::More}};
        if(screen_==Screen::Library && !importing_)return {confirm("Open"),
            {library_search_active_?"Clear search":"Back",SCE_CTRL_CIRCLE},{"Search",SCE_CTRL_SQUARE,Command::Search},
            {"Filter",SCE_CTRL_TRIANGLE,Command::Sort},options};
        if(screen_==Screen::Settings)return {{"Apply",SCE_CTRL_CROSS,Command::ApplySettings},{"Cancel",SCE_CTRL_CIRCLE,Command::CancelSettings},
            {"Reset",SCE_CTRL_SELECT,Command::ResetSettings},options};
        if(paged_screen()) {
            const auto count=page_count();
            return {{screen_==Screen::Library && importing_?"Import":"Open",SCE_CTRL_CROSS,Command::Button,count>0},
                {"Back",SCE_CTRL_CIRCLE},options};
        }
        if(screen_==Screen::Book || screen_==Screen::Novel) {
            bool selectable=false;
            const auto focus=focus_memory_.find(focus_key());
            if(focus!=focus_memory_.end())for(const auto& control:widgets())
                if(control.target.id==focus->second)selectable=control.kind==WidgetKind::Action && control.target.enabled;
            const Action select{"Select",SCE_CTRL_CROSS,Command::Button,selectable};
            if(screen_==Screen::Book)return {select,{"Back",SCE_CTRL_CIRCLE},options};
            return {select,{"Back",SCE_CTRL_CIRCLE},
                {"Add to Library",SCE_CTRL_SQUARE,Command::Button,!in_library()},options};
        }
        if(screen_==Screen::Home)return {{reader::ui::recent_book(entries_)<entries_.size()?"Continue":"Library",SCE_CTRL_CROSS},options};
        return {{screen_==Screen::About?"Select":"Open",SCE_CTRL_CROSS},{"Back",SCE_CTRL_CIRCLE},options};
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
        ime_jump_ = false; ime_library_=false;
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
    void exit_library_search() {
        const auto keep=selected_filename();
        library_search_active_=false;library_query_.clear();
        refresh_library(keep);update_cover();
        focus_memory_[static_cast<int>(Screen::Library)]=200;
        ++folder_revision_;
    }
    void apply_library_search(const std::string& query) {
        library_search_active_=true;library_query_=query;selected_=0;
        refresh_library("");update_cover();
        focus_memory_[static_cast<int>(Screen::Library)]=order_.empty()?200:1000;
    }
    void search_library() {
        ime_jump_=false; ime_library_=true;
        SceImeDialogParam param; sceImeDialogParamInit(&param);
        param.supportedLanguages=SCE_IME_LANGUAGE_ENGLISH;param.type=SCE_IME_TYPE_DEFAULT;
        param.dialogMode=SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
        param.title=reinterpret_cast<const SceWChar16*>(u"Search library (empty clears search)");
        param.maxTextLength=64;ime_input_[0]=ime_initial_[0]=0;
        param.initialText=ime_initial_;param.inputTextBuffer=ime_input_;
        if(sceImeDialogInit(&param)>=0)ime_active_=true;
        else message_="Could not open search keyboard.";
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
        if (pending_bookmark_) { pending_bookmark_=false; input(SCE_CTRL_SELECT,false); }
    }
    void refresh_online_summary() {
        offline_chapters_.clear(); offline_count_ = 0;
        for (const auto& chapter : online_novel_.chapters) {
            const bool available = reader::online::cached(online_manifest_, chapter.url);
            offline_chapters_.push_back(available);
            if (available) ++offline_count_;
        }
        online_saved_chapter_ = 0; online_started_ = false;
        online_detail_item_={};online_detail_item_.path=online_manifest_;
        online_detail_item_.title=online_novel_.title;online_detail_item_.author=online_novel_.author;
        online_detail_item_.chapters=online_novel_.chapters.size();online_detail_item_.filename="Online";
        reader::Publication publication;
        std::string error;
        if (!online_manifest_.empty() && reader::open_publication(online_manifest_, publication, error)) {
            auto saved = reader::load_progress(reader::progress_path(progress_directory, online_manifest_.substr(books_directory.size() + 1)));
            reader::online::restore_progress(publication, saved);
            online_started_ = reader::reading_started(saved);
            online_saved_chapter_ = saved.chapter;
            online_detail_item_.progress=saved;online_detail_item_.cover_path=publication.cover_path;
        }
    }
    void tick_online() {
        if (ime_active_) {
            if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return;
            redraw_=true;
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
                        focus_memory_[static_cast<int>(screen_)]=1000+index;
                        message_.clear();
                    } else message_ = "Enter a chapter number from 1 to " + std::to_string(count) + ".";
                } else if (ime_library_) {apply_library_search(query);}
                else browse_online("",query);
            }
        }
        auto update = online_.poll();
        if (update.busy) {
            if(!online_busy_ || message_!=update.message || download_completed_!=update.completed || download_total_!=update.total)redraw_=true;
            online_busy_ = true; message_ = update.message;
            download_completed_ = update.completed; download_total_ = update.total; return;
        }
        if (!update.done) return;
        redraw_=true;
        online_busy_ = false;
        // A chapter refresh can reorder stable bookmark paths; a download can
        // replace chapter text. Refresh only the affected collection member.
        if(update.request.action!=reader::online::Action::Browse)
            refresh_online_library_item(update.manifest.empty()?online_manifest_:update.manifest);
        download_completed_ = update.completed; download_total_ = update.total;
        if (!update.error.empty()) {
            pending_bookmark_=false;
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
            if (screen_ == Screen::Sources) { screen_ = Screen::Library; if(collection_dirty_)scan(); }
            else if (screen_ == Screen::Catalog) { screen_ = Screen::Sources; online_selected_ = source_selected_; }
            else if (screen_ == Screen::Novel) { screen_ = online_from_library_ ? Screen::Library : Screen::Catalog; online_selected_ = catalog_selected_; if (online_from_library_ && collection_dirty_) scan(); }
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
            if (pressed & SCE_CTRL_LEFT) online_selected_ = online_selected_>page_limit() ? online_selected_-page_limit() : 0;
            if (pressed & SCE_CTRL_RIGHT) online_selected_ = std::min(online_selected_+page_limit(),size-1);
        }
        if (screen_ == Screen::Sources) {
            if (pressed & SCE_CTRL_CROSS) { source_selected_=online_selected_;online_source_ = reader::online::sources()[online_selected_].id; browse_online(); }
        } else if (screen_ == Screen::Catalog) {
            if (pressed & SCE_CTRL_SQUARE) search_online();
            else if (pressed & SCE_CTRL_SELECT) browse_online();
            else if (pressed & SCE_CTRL_TRIANGLE) browse_online(online_catalog_.next.empty()?online_browse_url_:online_catalog_.next,online_query_);
            else if ((pressed & SCE_CTRL_CROSS) && size) {
                catalog_selected_=online_selected_;online_from_library_ = false;
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
    void refresh_online_library_item(const std::string& path) {
        if(path.empty())return;
        if(path==online_manifest_)cover_key_.clear();
        for(auto& item:entries_)if(item.path==path) {
            invalidate_book_assets(item);
            item.error.clear();load_library_item(item);
            library_order_dirty_=true;bookmarks_dirty_=true;
            break;
        }
    }
    std::vector<reader::Line> description_lines() const {
        return ui_wrap(online_novel_.description.empty()?"No description provided.":online_novel_.description,412,22,reader::FontRole::Interface);
    }
    reader::ui::Rect details_description(const std::string& title,bool online) const {
        const auto rows=std::min<std::size_t>(ui_wrap(title,412,30,reader::FontRole::Heading).size(),3);
        const int top=112+static_cast<int>(rows)*34;
        return {288,top,412,(online?338:382)-top};
    }
    std::size_t description_visible() const { return std::max(1,(details_description(online_novel_.title,true).height-20)/26); }
    void panel(int x,int y,int width,int height,bool selected=false,bool enabled=true,bool pressed=false) const {
        bool interactive=false;
        const auto memory=focus_memory_.find(focus_key());
        bool focused=false;
        for(const auto& control:widgets()) {
            const auto& box=control.target.box;
            if(box.x==x && box.y==y && box.width==width && box.height==height) {
                interactive=true;enabled=enabled && control.target.enabled;
                focused=memory!=focus_memory_.end() && control.target.id==memory->second;
                pressed=pressed || (control.action.label==pressed_action_ && sceKernelGetProcessTimeWide()<pressed_until_);
                break;
            }
        }
        if(interactive) {
            using reader::visual::ButtonState;
            reader::visual::button(x,y,width,height,!enabled?ButtonState::Disabled:pressed?ButtonState::Pressed:
                focused || selected?ButtonState::Focused:ButtonState::Normal);
        } else reader::visual::panel(x,y,width,height,false);
    }
    int button_hint_width(unsigned button) const {
        return reader::visual::controller_icon_width(controller_key(button));
    }
    void draw_logo(int x,int y,int size,unsigned tint) const {
        if (logo_) {
            const float scale=static_cast<float>(size)/vita2d_texture_get_width(logo_.get());
            vita2d_draw_texture_tint_scale(logo_.get(),x-size/2.f,y-size/2.f,scale,scale,tint);
        } else reader::visual::icon(reader::visual::Icon::Book,x,y,tint,size/28.f);
    }
    void button_hint(int x,int y,const Action& action) const {
        const auto c=action.enabled?palette().text:rgb(118,108,131);
        if(action.button==SCE_CTRL_CROSS || action.button==SCE_CTRL_CIRCLE || action.button==SCE_CTRL_TRIANGLE || action.button==SCE_CTRL_SQUARE) {
            for(int i=0;i<32;++i){const float a=i*6.2831853f/32,b=(i+1)*6.2831853f/32;
                vita2d_draw_line(x+13*std::cos(a),y+13*std::sin(a),x+13*std::cos(b),y+13*std::sin(b),c);}
        }
        if (action.button==SCE_CTRL_CROSS) {
            vita2d_draw_line(x-8,y-8,x+8,y+8,c); vita2d_draw_line(x-8,y+8,x+8,y-8,c);
        } else if (action.button==SCE_CTRL_CIRCLE) {
            for (int i=0;i<20;++i) {
                const float a=i*6.2831853f/20,b=(i+1)*6.2831853f/20;
                vita2d_draw_line(x+8*std::cos(a),y+8*std::sin(a),x+8*std::cos(b),y+8*std::sin(b),c);
            }
        } else if (action.button==SCE_CTRL_TRIANGLE) {
            vita2d_draw_line(x,y-11,x-11,y+9,c); vita2d_draw_line(x-11,y+9,x+11,y+9,c); vita2d_draw_line(x+11,y+9,x,y-11,c);
        } else if (action.button==SCE_CTRL_SQUARE) {
            vita2d_draw_line(x-9,y-9,x+9,y-9,c); vita2d_draw_line(x+9,y-9,x+9,y+9,c);
            vita2d_draw_line(x+9,y+9,x-9,y+9,c); vita2d_draw_line(x-9,y+9,x-9,y-9,c);
        } else if (action.button==SCE_CTRL_SELECT) {
            text(x-21,y+5,c,0.48f,"SELECT",48);
        } else if (action.button==SCE_CTRL_START) {
            reader::visual::rounded(x-24,y-11,48,22,c,10);
            reader::visual::rounded(x-22,y-9,44,18,palette().panel,8);
            text(x-20,y+5,c,0.55f,"START",42);
        } else if (action.button==SCE_CTRL_LTRIGGER || action.button==SCE_CTRL_RTRIGGER) {
            text(x-8,y+6,c,0.65f,action.button==SCE_CTRL_LTRIGGER?"L":"R",20);
        }
    }
    void button(const reader::ui::Rect& box,const Action& action,bool focused=false) const {
        const bool pressed=action.label==pressed_action_ && sceKernelGetProcessTimeWide()<pressed_until_;
        panel(box.x,box.y,box.width,box.height,focused,action.enabled,pressed);
        auto c=!action.enabled?reader::visual::mix(reader::visual::surface,reader::visual::muted,0.6f):palette().text;
        const bool hint=action.button && box.width>=fonts_.width(reader::FontRole::Interface,24,action.label)+button_hint_width(action.button)+40;
        const int hint_width=hint?button_hint_width(action.button):0;
        if (hint) button_hint(box.x+12+hint_width/2,box.y+box.height/2,action);
        const int left=box.x+(hint?hint_width+20:12),available=box.width-(hint?hint_width+28:24);
        const auto lines=ui_wrap(action.label,available,24,reader::FontRole::Interface);
        const auto count=std::min<std::size_t>(2,lines.size());
        for (std::size_t i=0;i<count;++i) {
            const auto& line=lines[i].text;
            const auto width=fonts_.width(reader::FontRole::Interface,24,line);
            text(left+(available-width)/2,box.y+box.height/2+8+(static_cast<int>(i)*26-static_cast<int>(count-1)*13),c,24.f/26,line,available);
        }
    }
    void draw_pager(std::size_t selected,std::size_t count) const {
        const auto limit=page_limit();
        text(752,102,palette().muted,0.8f,"Page "+std::to_string(count?selected/limit+1:0)+" / "+std::to_string((count+limit-1)/limit),184);
    }
    void list_row(int y,const std::string& title,const std::string& subtitle,bool selected,bool reading=false) const {
        panel(24,y,832,76,selected);
        if (reading) vita2d_draw_fill_circle(42,y+24,5,palette().accent);
        text(56,y+31,palette().text,1.0f,title,780);
        text(56,y+62,reading?palette().accent:palette().muted,0.85f,subtitle,780);
    }
    void draw_online() const {
        if(screen_==Screen::Novel){draw_book_details(online_detail_item_,true);return;}
        if(screen_==Screen::OnlineChapters){draw_chapters(true);return;}
        const auto title=screen_==Screen::Sources?"Online novels":screen_==Screen::Catalog?"Browse / "+online_source_:online_novel_.title;
        heading(title,screen_==Screen::Novel?online_novel_.author:screen_==Screen::OnlineChapters?
            std::to_string(offline_count_)+" chapters available offline":screen_==Screen::Catalog && !online_query_.empty()?"Search: "+online_query_:"Choose your next read");
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
    reader::ui::Rect toolbar_rect(const std::vector<Action>& actions,int index) const {
        const int count=static_cast<int>(actions.size());
        if((screen_==Screen::Reader || screen_==Screen::Image) && !feedback_active() && !online_busy_ && count==3) {
            const reader::ui::Rect boxes[]={{28,484,170,56},{228,484,150,56},{776,484,156,56}};
            return boxes[index];
        }
        if(screen_==Screen::Library && !importing_ && menu_.empty() && !online_busy_ && count==5) {
            const reader::ui::Rect boxes[]={{28,484,132,56},{168,484,232,56},{408,484,152,56},{568,484,152,56},{776,484,156,56}};
            return boxes[index];
        }
        if(screen_==Screen::Novel && menu_.empty() && !feedback_active() && !online_busy_ && count==4) {
            const reader::ui::Rect boxes[]={{28,484,152,56},{200,484,150,56},{384,484,270,56},{776,484,156,56}};
            return boxes[index];
        }
        return count>1 && actions.back().command==Command::More ?
            reader::ui::action_toolbar_button(index,count) : reader::ui::toolbar_button(index,count);
    }
    void draw_toolbar() const {
        using namespace reader::visual;
        if(screen_==Screen::Reader && immersive_ && menu_.empty() && !feedback_active() && !online_busy_)return;
        const auto actions=toolbar();
        if(actions.empty())return;
        hint_bar();
        for(std::size_t i=0;i<actions.size();++i)draw_footer_action(toolbar_rect(actions,i),actions[i]);
    }
    reader::visual::ControllerButton controller_key(unsigned button) const {
        using namespace reader::visual;
        return button==SCE_CTRL_CROSS?ControllerButton::Cross:
            button==SCE_CTRL_CIRCLE?ControllerButton::Circle:
            button==SCE_CTRL_SQUARE?ControllerButton::Square:
            button==SCE_CTRL_TRIANGLE?ControllerButton::Triangle:
            button==SCE_CTRL_START?ControllerButton::Start:
            button==SCE_CTRL_SELECT?ControllerButton::Select:
            button==SCE_CTRL_LTRIGGER?ControllerButton::L:
            button==SCE_CTRL_RTRIGGER?ControllerButton::R:ControllerButton::None;
    }
    reader::ui::Rect footer_hint_rect(const reader::ui::Rect& box,const Action& action) const {
        using namespace reader::visual;
        const auto key=controller_key(action.button);
        const int desired=controller_icon_width(key)+(key==ControllerButton::None?0:10)+fonts_.width(reader::FontRole::Interface,22,action.label);
        const bool right=action.button==SCE_CTRL_START;
        const int edge=box.x+box.width-16;
        const int left=right?std::max(box.x,edge-desired):box.x+16;
        return {left,hint_bar_top,std::min(desired,edge-left),544-hint_bar_top};
    }
    reader::ui::Rect footer_touch_rect(const reader::ui::Rect& box,const Action& action) const {
        auto hit=footer_hint_rect(box,action);
        hit.x-=8;hit.width+=16;
        return hit;
    }
    void draw_footer_action(const reader::ui::Rect& box,const Action& action) const {
        using namespace reader::visual;
        const auto key=controller_key(action.button);
        const auto bounds=footer_hint_rect(box,action);
        const int edge=box.x+box.width-16;
        const bool pressed=action.enabled && action.label==pressed_action_ && sceKernelGetProcessTimeWide()<pressed_until_;
        const auto tint=!action.enabled?mix(surface,muted,0.55f):pressed?accent:reader::visual::text;
        controller_hint(fonts_,key,action.label,bounds.x,hint_bar_center,edge-bounds.x,tint);
    }
    void draw_menu() const {
        using namespace reader::visual;
        const auto layout=menu_layout();const auto& box=layout.box;
        popup(box);
        const bool submenu=!menu_parents_.empty();
        if(submenu) {
            const auto back=layout.back();const int cx=back.x+16,cy=back.y+16;
            vita2d_draw_line(cx+7,cy,cx-7,cy,accent);
            vita2d_draw_line(cx-7,cy,cx-1,cy-6,accent);
            vita2d_draw_line(cx-7,cy,cx-1,cy+6,accent);
        }
        const int title_left=submenu?44:14;
        text(box.x+title_left,box.y+25,reader::visual::text,0.82f,menu_title_,box.width-title_left-14,reader::FontRole::Heading);
        for(int i=layout.first;i<layout.first+layout.visible && i<static_cast<int>(menu_.size());++i) {
            const auto row=layout.row(i);const auto& action=menu_[i];
            const bool selected=i==static_cast<int>(menu_selected_);
            if(selected)reader::visual::button(row.x,row.y,row.width,row.height,ButtonState::Focused);
            const bool active=menu_option_active(action.command);
            const auto tint=action.enabled?reader::visual::text:muted;
            if(active)icon(Icon::Check,row.x+16,row.y+17,accent,0.55f);
            const bool child=menu_opens_submenu(action.command);
            if(child)icon(Icon::Chevron,row.x+row.width-16,row.y+17,action.enabled?accent:muted,0.8f);
            text(row.x+32,row.y+23,tint,0.78f,action.label,row.width-(child?64:42));
        }
    }
    static bool menu_opens_submenu(Command command) {
        return command==Command::Sort || command==Command::Speech || command==Command::RemoveBook;
    }
    bool menu_option_active(Command command) const {
        return (command==Command::SortTitle && sort_==reader::LibrarySort::Title) ||
            (command==Command::SortRecent && sort_==reader::LibrarySort::Recent) ||
            (command==Command::SortProgress && sort_==reader::LibrarySort::Progress) ||
            (command==Command::FilterAll && filter_==reader::LibraryFilter::All) ||
            (command==Command::FilterReading && filter_==reader::LibraryFilter::Reading) ||
            (command==Command::FilterFinished && filter_==reader::LibraryFilter::Finished);
    }
    std::vector<reader::Line> feedback_rows() const {
        return ui_wrap(message_,feedback_box().width-48,24,reader::FontRole::Interface);
    }
    reader::ui::Rect feedback_box() const { return {224,128,512,288}; }
    reader::ui::Rect feedback_action(bool retry) const {
        const auto box=feedback_box();
        if(retry_available_ && !online_busy_)return {box.x+16+(retry?0:248),box.y+box.height-72,232,56};
        return {box.x+16,box.y+box.height-72,box.width-32,56};
    }
    void draw_feedback() const {
        if(message_.empty())return;
        if(message_==notice_) {
            panel(24,44,912,36);
            text(40,70,palette().text,0.80f,message_,880);return;
        }
        const auto rows=feedback_rows();const auto count=std::min<std::size_t>(rows.size(),online_busy_?4:6);
        const auto box=feedback_box();const auto bottom=box.y+box.height;
        if(feedback_active() || online_busy_) {
            const int top=has_sidebar()?0:44;
            vita2d_draw_rectangle(0,top,960,reader::visual::hint_bar_top-top,0xb0000000);
        }
        reader::visual::popup(box);
        for(std::size_t i=0;i<count;++i)text(box.x+24,box.y+36+i*28,palette().text,0.92f,rows[i].text,box.width-48);
        if(retry_available_ && !online_busy_)button(feedback_action(true),{"Retry",0,Command::Retry});
        if(feedback_active())button(feedback_action(false),{"Dismiss"});
        if(online_busy_) {
            reader::visual::progress(box.x+24,bottom-120,box.width-48,download_total_?float(download_completed_)/download_total_:0,6);
            text(box.x+24,bottom-92,palette().muted,0.8f,download_total_?std::to_string(download_completed_)+" / "+
                std::to_string(download_total_)+" completed":"Connecting...",box.width-48);
            button(feedback_action(false),{"Cancel",SCE_CTRL_CIRCLE});
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
    void step_reading(bool forward,std::size_t amount=1) {
        if(lines_.empty())return;
        top_=forward?std::min(top_+amount,lines_.size()-1):top_>amount?top_-amount:0;
    }

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
        if(!library_preferences_pending_ && settings_.library_sort==static_cast<int>(sort_) && settings_.library_filter==static_cast<int>(filter_))return;
        settings_.library_sort = static_cast<int>(sort_);
        settings_.library_filter = static_cast<int>(filter_);
        library_preferences_pending_=!reader::save_settings(settings_path, settings_);
        if(library_preferences_pending_)message_ = "Could not save library preferences.";
    }
    void release_image() {
        if (image_) { vita2d_wait_rendering_done(); vita2d_free_texture(image_); image_ = nullptr; }
        cover_key_.clear();
    }
    std::string selected_filename() const {
        return order_.empty() ? "" : entries_[order_[selected_]].filename;
    }
    void refresh_library(const std::string& keep) {
        const reader::perf::Scope timing(reader::perf::Event::LibraryOrder);
        bookmarks_dirty_=true;
        order_ = reader::library_order(entries_, sort_, importing_ ? reader::LibraryFilter::All : filter_);
        if(!importing_ && !library_query_.empty()) {
            auto query=library_query_;
            const auto lower=[](unsigned char c){return static_cast<char>(std::tolower(c));};
            std::transform(query.begin(),query.end(),query.begin(),lower);
            order_.erase(std::remove_if(order_.begin(),order_.end(),[&](std::size_t i){
                auto title=entries_[i].title; std::transform(title.begin(),title.end(),title.begin(),lower);
                return title.find(query)==std::string::npos;
            }),order_.end());
        }
        ordered_sort_=sort_;ordered_filter_=filter_;ordered_query_=library_query_;ordered_importing_=importing_;
        library_order_dirty_=false;
        restore_library_selection(keep);
        release_image();
    }
    void ensure_library_order(const std::string& keep) {
        if(library_order_dirty_ || ordered_sort_!=sort_ || ordered_filter_!=filter_ ||
           ordered_query_!=library_query_ || ordered_importing_!=importing_)refresh_library(keep);
        else restore_library_selection(keep);
    }
    void restore_library_selection(const std::string& keep) {
        selected_ = order_.empty() ? 0 : std::min(selected_, order_.size() - 1);
        for (std::size_t i = 0; i < order_.size(); ++i)
            if (entries_[order_[i]].filename == keep) { selected_ = i; break; }
        ensure_library_visible();
    }

    void update_cover() {
        // Details and chapter lists share one full-size cover texture. Import,
        // Home and Reader never decode it on focus/activation.
        if(screen_!=Screen::Book && screen_!=Screen::Novel && screen_!=Screen::OnlineChapters && screen_!=Screen::Contents){release_image();return;}
        if (screen_==Screen::Book && order_.empty()) { release_image(); cover_message_.clear(); return; }
        const reader::LibraryItem* item=nullptr;
        if(screen_==Screen::Novel || screen_==Screen::OnlineChapters)item=&online_detail_item_;
        else if(screen_==Screen::Contents) {
            for(const auto& candidate:entries_)if(candidate.filename==active_filename_){item=&candidate;break;}
            if(!item && publication_.online)item=&online_detail_item_;
        } else item=&entries_[order_[selected_]];
        if(!item){release_image();return;}
        const auto& entry=*item;
        const auto key=entry.path+":"+entry.cover_path;
        if (cover_key_ == key) return;
        release_image();
        cover_key_ = key;
        cover_message_.clear();
        if (entry.cover_path.empty()) return;
        const reader::perf::Scope timing(reader::perf::Event::CoverDecode);
        try {
            std::string bytes;
            if (load_cover_bytes(entry,bytes,cover_message_))
                image_ = decode_texture(bytes, cover_message_);
        } catch (const std::exception&) { cover_message_ = "Cover could not be loaded."; }
    }
    static bool load_cover_bytes(const reader::LibraryItem& item,std::string& bytes,std::string& error) {
        return reader::is_online_book(item.path)?reader::online::load_cover(item.path,bytes,error):
            reader::load_epub_image(item.path,item.cover_path,bytes,error);
    }

    float fit_scale(float width, float height) const {
        if (!image_) return 1;
        return std::min(width / vita2d_texture_get_width(image_), height / vita2d_texture_get_height(image_));
    }
    void clamp_pan() {
        if (!image_) return;
        const auto scale = fit_scale(864, reader::ui::image_height) * zoom_;
        const auto limit_x = std::max(0.0f, (vita2d_texture_get_width(image_) * scale - 864) / 2);
        const auto limit_y = std::max(0.0f, (vita2d_texture_get_height(image_) * scale - reader::ui::image_height) / 2);
        pan_x_ = std::max(-limit_x, std::min(limit_x, pan_x_));
        pan_y_ = std::max(-limit_y, std::min(limit_y, pan_y_));
    }

    void show_illustration(std::size_t index, Screen return_to) {
        message_ = "Opening illustration..."; draw_loading();
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
        return settings_.reading_font==0?reader::FontRole::Serif:
            settings_.reading_font==1?reader::FontRole::Sans:reader::FontRole::System;
    }
    int line_height() const { return static_cast<int>(std::lround(reading_pixels()*(1.2f+settings_.spacing*0.3f))); }

    std::string fit(std::string text, float scale, int width, reader::FontRole role) const {
        const auto pixels = std::max(20u,static_cast<unsigned>(std::lround(26 * scale)));
        const auto key=std::make_tuple(width,pixels,role,text);
        const auto cached=fit_cache_.find(key);
        if(cached!=fit_cache_.end())return cached->second;
        if (fonts_.width(role, pixels, text) > width) {
            while (!text.empty() && fonts_.width(role, pixels, text + "...") > width) {
                auto last = text.size() - 1;
                while (last > 0 && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80) --last;
                text.resize(last);
            }
            text+="...";
        }
        if(std::get<3>(key).size()<=1024) {
            if(fit_cache_.size()>=128)fit_cache_.clear();
            fit_cache_.emplace(key,text);
        }
        return text;
    }
    std::vector<reader::Line> ui_wrap(const std::string& value,int width,unsigned pixels,reader::FontRole role,bool title=false) const {
        const auto key=std::make_tuple(width,pixels,role,title,value);
        const auto cached=wrap_cache_.find(key);
        if(cached!=wrap_cache_.end())return cached->second;
        auto lines=reader::wrap_text(value,width,[&](const std::string& text) {
            return title?fonts_.title_width(pixels,text):fonts_.width(role,pixels,text);
        });
        if(value.size()<=4096) {
            if(wrap_cache_.size()>=32)wrap_cache_.clear();
            wrap_cache_.emplace(key,lines);
        }
        return lines;
    }

    void text(int x, int y, unsigned color, float scale, const std::string& value, int width = 864,
              reader::FontRole role = reader::FontRole::Interface) const {
        const auto clipped = fit(value, scale, width, role);
        fonts_.draw(role, std::max(20u,static_cast<unsigned>(std::lround(26 * scale))), x, y, color, clipped);
    }

    void heading(const std::string& title,const std::string& subtitle) const {
        text(24,96,palette().text,1.15f,title,688,reader::FontRole::Heading);
        text(24,125,palette().muted,0.80f,subtitle,912);
    }


    void browse_folder(const std::string& folder, const std::string& restore = {}) {
        release_image(); order_.clear(); selected_ = 0;
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
        collection_dirty_=true;
        if(!importing_)scan();
        notify("Added to library.");
        return true;
    }

    void scan(const std::string& restore = {},bool force_refresh=false,bool startup=false) {
        const reader::perf::Scope timing(reader::perf::Event::Scan);
        const auto keep = restore.empty() ? selected_filename() : restore;
        release_image();
        order_.clear();
        bookmarks_dirty_=true;
        library_order_dirty_=true;
        message_.clear();
        if (importing_) {
            // The browser owns a temporary list. Keep the collection and its
            // progress alive so canceling import needs no metadata reload.
            if(!collection_suspended_) {
                collection_entries_=std::move(entries_);collection_suspended_=true;
            }
            entries_.clear();
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
            if(collection_suspended_) {
                entries_=std::move(collection_entries_);collection_suspended_=false;
            }
            const std::unordered_set<std::string> membership(library_books_.begin(),library_books_.end());
            entries_.erase(std::remove_if(entries_.begin(),entries_.end(),[&](const reader::LibraryItem& item) {
                if(item.guide || membership.count(item.filename))return false;
                invalidate_book_assets(item);return true;
            }),entries_.end());
            std::unordered_set<std::string> known;
            for(const auto& item:entries_)known.insert(item.filename);
            std::size_t loaded=0;
            auto last_progress=sceKernelGetProcessTimeWide();
            for (const auto& filename : library_books_) {
                if(known.count(filename))continue;
                reader::LibraryItem item;
                item.filename = filename;
                item.path = books_directory + '/' + filename;
                item.title = reader::title_from_filename(filename.substr(filename.find_last_of('/') + 1));
                SceIoStat state{};
                if (sceIoGetstat(item.path.c_str(), &state) < 0 || !SCE_S_ISREG(state.st_mode))
                    item.error = "File missing. Restore it to its imported location.";
                load_library_item(item);
                entries_.push_back(std::move(item));
                ++loaded;
                if(startup) {
                    const auto now=sceKernelGetProcessTimeWide();
                    // Keep large libraries responsive without adding a frame per book.
                    if(now-last_progress>=100000) {
                        draw_startup(0.35f+0.5f*loaded/library_books_.size());
                        last_progress=now;
                    }
                }
            }
            if (!library_writable_) message_ = "Library index is unreadable. Restore library.txt from backup.";
        }
        // Bundled guide is always available without modifying the user's library.
        if (!importing_ && std::none_of(entries_.begin(),entries_.end(),[](const reader::LibraryItem& item){return item.guide;})) {
            reader::LibraryItem guide;
            guide.filename = "@welcome"; guide.title = "Welcome to NovelReader"; guide.path = "app0:/welcome.txt"; guide.guide = true;
            load_library_item(guide);
            entries_.push_back(std::move(guide));
        }
        std::unordered_map<std::string,reader::LibraryItem*> collection;
        if(importing_)for(auto& item:collection_entries_)collection.emplace(item.filename,&item);
        if(importing_)for (auto& item : entries_) {
            if (item.directory) continue;
            const auto cached=collection.find(item.filename);
            if(!force_refresh && cached!=collection.end() && cached->second->error.empty()) {
                item=*cached->second;continue;
            }
            // Refresh explicitly rereads listed files, including replacements.
            load_library_item(item);
            if(cached!=collection.end()) {
                invalidate_book_assets(*cached->second);*cached->second=item;
            }
        }
        if(!importing_)collection_dirty_=false;
        refresh_library(keep);
        if (!library_initialized_ && !importing_) {
            const auto recent = reader::ui::recent_book(entries_);
            if (recent < entries_.size()) refresh_library(entries_[recent].filename);
            library_initialized_ = true;
        }
        update_cover();
    }
    void invalidate_book_assets(const reader::LibraryItem& item) {
        covers_.erase(std::remove_if(covers_.begin(),covers_.end(),[&](const CoverTexture& cover){return cover.key==item.path;}),covers_.end());
        const auto prefix=item.path+":";
        bookmark_previews_.erase(std::remove_if(bookmark_previews_.begin(),bookmark_previews_.end(),[&](const BookmarkPreview& preview){
            return preview.key.compare(0,prefix.size(),prefix)==0;
        }),bookmark_previews_.end());
    }
    void load_library_item(reader::LibraryItem& item) {
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

    void open_local_contents() {
        if(order_.empty())return;
        const auto& item=entries_[order_[selected_]];
        reader::Publication candidate;
        if(!reader::open_publication(item.path,candidate,message_) || candidate.chapters.empty())return;
        const auto filename=item.filename;
        clear_reading();publication_=std::move(candidate);active_filename_=filename;
        progress_=reader::load_progress(reader::progress_path(progress_directory,filename));
        chapter_=std::min(progress_.chapter,publication_.chapters.size()-1);contents_selected_=chapter_;
        contents_from_details_=true;screen_=Screen::Contents;message_.clear();++folder_revision_;
        focus_memory_.erase(static_cast<int>(Screen::Contents));
    }
    void open() {
        speech_.stop();
        if(order_.empty())return;
        const auto entry_index = order_[selected_];
        if (entries_[entry_index].directory) {
            const auto folder = entries_[entry_index].filename;
            browse_folder(folder);
            return;
        }
        if (importing_) {
            const auto filename = entries_[entry_index].filename;
            if (add_to_library(filename)) {
                if(collection_suspended_ && std::none_of(collection_entries_.begin(),collection_entries_.end(),[&](const reader::LibraryItem& item){return item.filename==filename;}))
                    collection_entries_.push_back(entries_[entry_index]);
                importing_ = false; current_folder_.clear();
                filter_=reader::LibraryFilter::All;library_query_.clear();library_search_active_=false;save_library_preferences();
                scan(filename);focus_memory_[static_cast<int>(Screen::Library)]=1000+selected_;
                if (message_.empty()) notify("Added to library.");
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
        draw_loading();
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
            if(entries_[entry_index].title!=publication_.title)library_order_dirty_=true;
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
        speech_.stop();
        if (publication_.online && index < online_novel_.chapters.size() && !reader::online::cached(online_manifest_,online_novel_.chapters[index].url)) {
            pending_offset_ = offset; pending_end_ = at_end; online_read_in_place_ = true;
            begin_online_read(index); return false;
        }
        message_ = "Opening chapter...";
        draw_loading();
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
        if (now > 0) progress_.last_read = std::max(progress_.last_read,static_cast<std::uint64_t>(now));
        // Progress files store seconds. Make the active book strictly newer than
        // the other books even when two are opened in one second or the clock
        // moves backward, so Home and Recent sorting agree after a restart.
        for(const auto& item:entries_)
            if(!item.directory && item.error.empty() && item.filename!=active_filename_ &&
               item.progress.last_read>=progress_.last_read && item.progress.last_read<UINT64_C(32503680000))
                progress_.last_read=item.progress.last_read+1;
        if(!progress_.last_read)progress_.last_read=1;
        if(publication_.online && publication_.path==online_manifest_) {
            online_detail_item_.progress=progress_;
            online_started_=reader::reading_started(progress_);online_saved_chapter_=chapter_;
        }
        const auto same=[](const reader::Progress& a,const reader::Progress& b) {
            return std::tie(a.offset,a.has_bookmark,a.bookmark,a.chapter,a.bookmark_chapter,a.percent,a.last_read,a.chapter_id,a.bookmark_id)==
                std::tie(b.offset,b.has_bookmark,b.bookmark,b.chapter,b.bookmark_chapter,b.percent,b.last_read,b.chapter_id,b.bookmark_id);
        };
        if ((persisted_filename_!=active_filename_ || !same(progress_,persisted_progress_)) &&
            !reader::save_progress(reader::progress_path(progress_directory, active_filename_), progress_)) {
            message_ = "Could not save progress. Check free space.";
        } else {
            persisted_filename_=active_filename_;persisted_progress_=progress_;
            for (auto& item : entries_)
                if (item.filename == active_filename_) {
                    if(item.cover_path!=publication_.cover_path)invalidate_book_assets(item);
                    if(item.progress.has_bookmark!=progress_.has_bookmark)bookmarks_dirty_=true;
                    if(item.title!=publication_.title ||
                       (sort_==reader::LibrarySort::Recent && item.progress.last_read!=progress_.last_read) ||
                       (sort_==reader::LibrarySort::Progress && item.progress.percent!=progress_.percent) ||
                       (filter_!=reader::LibraryFilter::All &&
                        (reader::reading_started(item.progress)!=reader::reading_started(progress_) ||
                         (item.progress.percent==100)!=(progress_.percent==100))))library_order_dirty_=true;
                    item.progress = progress_;
                    item.chapters = publication_.chapters.size();
                    item.title = publication_.title;
                    item.author = publication_.author;
                    item.cover_path = publication_.cover_path;
                    remember_bookmark(item);
                }
        }
    }

    void draw_library() const {
        if(importing_) {
            heading("Import books",current_folder_.empty()?"Choose a folder or book":current_folder_);
            const auto first=reader::ui::page_first(selected_);
            for(auto i=first;i<order_.size() && i<first+list_page;++i) {
                const auto& item=entries_[order_[i]];
                list_row(reader::ui::library_top+static_cast<int>(i-first)*reader::ui::library_pitch,item.title,
                    item.directory?"Folder":item.error.empty()?"Import into your library":"Unavailable",false);
            }
            if(order_.empty())text(40,196,palette().muted,0.92f,"No supported books in this folder.",800);
            draw_pager(selected_,order_.size());return;
        }
        const char* filters[]={"All books","Reading","Finished"};
        text(232,44,palette().text,1.23f,"Library",440,reader::FontRole::Heading);
        std::string subtitle=std::to_string(entries_.size())+" books  /  "+filters[static_cast<int>(filter_)];
        if(library_search_active_)subtitle+="  /  Search: "+library_query_;
        text(232,66,palette().muted,0.78f,subtitle,440);
        const auto add=reader::ui::library_action(0);
        panel(add.x,add.y,add.width,add.height);
        vita2d_draw_line(add.x+17,add.y+28,add.x+33,add.y+28,palette().text);
        vita2d_draw_line(add.x+25,add.y+20,add.x+25,add.y+36,palette().text);
        text(add.x+45,add.y+36,palette().text,0.92f,"Add Book",add.width-53);
        vita2d_enable_clipping();vita2d_set_clip_rectangle(232,reader::ui::grid_top,936,reader::ui::grid_bottom);
        for(auto i=library_first();i<library_end();++i) {
            const auto box=reader::ui::grid_book(i,library_scroll_);
            const auto& item=entries_[order_[i]];
            panel(box.x,box.y,box.width,box.height);
            draw_cover(item,box.x,box.y,box.width,reader::ui::grid_cover_height);
            const auto title=ui_wrap(item.title,box.width-16,22,reader::FontRole::Heading);
            for(std::size_t line=0;line<std::min<std::size_t>(title.size(),2);++line) {
                const auto label=title[line].text+(line==1 && title.size()>2?"...":"");
                text(box.x+8,box.y+232+static_cast<int>(line)*24,palette().text,22.f/26,label,box.width-16,reader::FontRole::Heading);
            }
            text(box.x+8,box.y+288,palette().muted,0.80f,item.progress.percent>=0?std::to_string(item.progress.percent)+"% read":"Not started",box.width-16);
        }
        vita2d_disable_clipping();
        const int content_height=order_.empty()?0:static_cast<int>((order_.size()+3)/4-1)*reader::ui::grid_pitch+reader::ui::grid_height;
        const int viewport=reader::ui::grid_bottom-reader::ui::grid_top;
        if(content_height>viewport) {
            const int thumb=std::max(24,viewport*viewport/content_height);
            const int offset=library_scroll_*(viewport-thumb)/(content_height-viewport);
            reader::visual::rounded(946,reader::ui::grid_top,3,viewport,reader::visual::border,1);
            reader::visual::rounded(946,reader::ui::grid_top+offset,3,thumb,palette().accent,1);
        }
        if(order_.empty()) {
            text(280,260,palette().text,1.05f,library_query_.empty() && filter_==reader::LibraryFilter::All?
                "A new story starts here.":"No matching books.",620,reader::FontRole::Heading);
            text(280,299,palette().muted,0.85f,"Add a book or explore online novels above.",620);
            if(!library_query_.empty() || filter_!=reader::LibraryFilter::All)
                button({400,328,336,56},{library_search_active_?"Clear search":"Show all books",0,Command::ClearSearch});
        }
    }
    void draw_book() const {
        if (order_.empty()) return;
        draw_book_details(entries_[order_[selected_]],false);
    }
    void draw_book_details(const reader::LibraryItem& item,bool online) const {
        const auto cover=reader::ui::details_cover();draw_cover(item,cover.x,cover.y,cover.width,cover.height);
        const auto title=ui_wrap(item.title,412,30,reader::FontRole::Heading);
        int y=90;
        for(std::size_t i=0;i<std::min<std::size_t>(title.size(),3);++i){text(288,y,palette().text,1.15f,title[i].text,412,reader::FontRole::Heading);y+=34;}
        text(288,y+2,palette().muted,0.82f,item.author.empty()?(online?"Online novel":"Local book"):item.author,412);
        const auto type=online?"Online":reader::is_epub_book(item.filename)?"EPUB":"TXT";
        const auto area=details_description(item.title,online);
        const auto description=item.error.empty()?"Your place is saved automatically. Settle in and enjoy your next chapter.":item.error;
        const auto rows=online?description_lines():ui_wrap(description,area.width,22,reader::FontRole::Interface);
        const auto first=online?std::min(description_row_,rows.size()):0;
        const auto visible=online?description_visible():static_cast<std::size_t>(area.height/26);
        for(std::size_t i=first;i<rows.size() && i<first+visible;++i)
            text(area.x,area.y+22+static_cast<int>(i-first)*26,palette().text,22.f/26,rows[i].text,area.width);
        if(online && rows.size()>visible)text(area.x,area.y+area.height,palette().muted,0.7f,"Up/Down: scroll description",area.width);
        button(reader::ui::details_primary(online),{(online?online_started_:reader::reading_started(item.progress))?"Continue Reading":"Start Reading",0,online?Command::ReadOnline:Command::Button,item.error.empty()});
        if(online) {
            button(reader::ui::details_secondary(true),{"Chapters",0,Command::OnlineContents,item.error.empty()});
            button(reader::ui::details_utility(0),{"Download all",0,Command::DownloadAll});
            button(reader::ui::details_utility(1),{"Refresh chapters",SCE_CTRL_TRIANGLE});
        } else button(reader::ui::details_secondary(false),{"Chapters",0,Command::LocalContents,item.error.empty()});
        panel(710,72,222,380);
        text(730,107,palette().muted,0.7f,"Progress",120);
        text(867,107,palette().text,0.7f,std::to_string(std::max(0,item.progress.percent))+"%",58);
        reader::visual::progress(730,123,180,std::max(0,item.progress.percent)/100.f,6);
        vita2d_draw_line(730,150,912,150,rgb(62,50,77));
        text(730,181,palette().muted,0.68f,"Last Read",180);
        std::string last="Not started";
        if(item.progress.last_read) {
            const auto stamp=static_cast<std::time_t>(item.progress.last_read);const auto* date=std::localtime(&stamp);char buffer[48]{};
            if(date) {std::strftime(buffer,sizeof(buffer),"%b %d, %I:%M %p",date);last=buffer;}
        }
        text(730,207,palette().text,0.68f,last,180);
        text(730,251,palette().muted,0.68f,"Chapters",180);
        text(730,279,palette().text,0.84f,std::to_string(item.chapters),180);
        text(730,322,palette().muted,0.68f,online?"Available offline":"Format",180);
        text(730,350,palette().accent,0.8f,online?std::to_string(offline_count_):type,180);
        if(online)text(730,421,palette().muted,0.7f,in_library()?"In library":"Not saved",180);
        else text(24,475,palette().muted,0.66f,item.filename,880);
    }
    void draw_reader() const {
        const auto end=visible_end();
        const int percent=reader::reading_percent(chapter_,publication_.chapters.size(),lines_[top_].offset,book_.text.size(),end==lines_.size());
        const auto area=reading_area(); int y=area.top;
        vita2d_enable_clipping();vita2d_set_clip_rectangle(48,area.top,912,area.top+area.height);
        for (auto i=top_;i<end;++i) {
            if (lines_[i].illustration==reader::no_illustration) draw_line(lines_[i],y+line_height()-8);
            else draw_inline_image(lines_[i],y);
            y+=lines_[i].height;
        }
        vita2d_disable_clipping();
        const int bar_y=immersive_?510:432;
        reader::visual::progress(40,bar_y,880,std::max(0,percent)/100.f,3);
        const auto speech_state = speech_.status().state;
        const std::string narration = speech_state == reader::speech::State::Speaking ? " · Speaking" :
            speech_state == reader::speech::State::Paused ? " · Paused" : "";
        text(40,immersive_?536:464,palette().text,0.76f,"Chapter "+std::to_string(chapter_+1)+" / "+std::to_string(publication_.chapters.size())+narration,620,reader::FontRole::Interface);
        const auto page=std::to_string(std::upper_bound(pages_.begin(),pages_.end(),top_)-pages_.begin())+" / "+std::to_string(pages_.size());
        const auto width=fonts_.width(reader::FontRole::Interface,20,page);
        text(920-width,immersive_?536:464,palette().text,0.76f,page,250,reader::FontRole::Interface);
        if (!immersive_ && progress_.has_bookmark) {
            reader::visual::gradient(910,72,24,28,rgb(190,148,232),rgb(130,83,180));
            // Forked bookmark ribbon, drawn in horizontal spans.
            for(int row=0;row<10;++row) {
                const auto tint=rgb(130,83,180);
                vita2d_draw_rectangle(910,100+row,12-row,1,tint);
                vita2d_draw_rectangle(922+row,100+row,12-row,1,tint);
            }
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
            const auto role=reading_font();
            fonts_.draw(role, reading_pixels(), x, y, color, run);
            if(style&reader::Strong) fonts_.draw(role,reading_pixels(),x+1,y,color,run);
            const auto width = fonts_.width(role, reading_pixels(), run);
            if (style & reader::Emphasis) vita2d_draw_rectangle(x, y + 3, width, 1, color);
            x += width; at = end;
        }
    }

    void draw_contents() const {
        draw_chapters(false);
    }
    void draw_chapters(bool online) const {
        using reader::visual::Icon;
        const auto count=online?online_novel_.chapters.size():publication_.chapters.size();
        const auto selected=online?online_selected_:contents_selected_,current=online?online_saved_chapter_:chapter_;
        const bool network=online || publication_.online,started=online?online_started_:reader::reading_started(progress_);
        const auto& saved=online?online_detail_item_.progress:progress_;
        const reader::LibraryItem* entry=online?&online_detail_item_:nullptr;
        if(!online)for(const auto& item:entries_)if(item.filename==active_filename_){entry=&item;break;}
        if(!entry && publication_.online)entry=&online_detail_item_;
        const auto cover=reader::ui::details_cover();
        if(entry)draw_cover(*entry,cover.x,cover.y,cover.width,cover.height);
        else {
            reader::LibraryItem fallback;fallback.title=publication_.title;
            draw_cover(fallback,cover.x,cover.y,cover.width,cover.height);
        }
        text(288,96,palette().text,1.15f,online?"Chapters":"Table of Contents",440,reader::FontRole::Heading);
        text(288,122,palette().muted,0.8f,online?online_novel_.title:publication_.title,648);
        const auto first=selected/reader::ui::contents_page_size*reader::ui::contents_page_size;
        for(auto i=first;i<count && i<first+reader::ui::contents_page_size;++i) {
            const auto row=reader::ui::contents_row(static_cast<int>(i-first));const int center=row.y+row.height/2;
            panel(row.x,row.y,row.width,row.height,i==selected);
            const bool reading=started && i==current && saved.percent<100;
            const bool read=started && (i<current || (i==current && saved.percent>=100));
            const bool downloaded=!network || (i<offline_chapters_.size() && offline_chapters_[i]);
            const bool locked=network && i<online_novel_.chapters.size() && online_novel_.chapters[i].locked;
            if(reading)reader::visual::icon(Icon::Play,row.x+22,center,palette().accent,0.65f);
            text(row.x+44,center+8,palette().text,0.92f,online?online_novel_.chapters[i].title:publication_.chapters[i].title,row.width-116);
            reader::visual::icon(downloaded?Icon::Downloaded:locked?Icon::Lock:Icon::Download,
                row.x+row.width-52,center,downloaded?palette().accent:palette().muted,0.65f);
            if(read)reader::visual::icon(Icon::Check,row.x+row.width-20,center,palette().accent,0.7f);
        }
        text(288,481,palette().muted,0.76f,std::to_string(count)+" chapters",400);
        text(746,481,palette().muted,0.7f,"Page "+std::to_string(count?selected/reader::ui::contents_page_size+1:0)+" / "+
            std::to_string((count+reader::ui::contents_page_size-1)/reader::ui::contents_page_size),190);
        if(!count)text(310,190,palette().muted,0.92f,"No chapters available.",600);
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
            const auto scale=fit_scale(864,reader::ui::image_height)*zoom_;
            vita2d_enable_clipping();vita2d_set_clip_rectangle(48,reader::ui::image_top,912,reader::ui::image_top+reader::ui::image_height);
            vita2d_draw_texture_scale(image_,480-vita2d_texture_get_width(image_)*scale/2+pan_x_,reader::ui::image_center_y-vita2d_texture_get_height(image_)*scale/2+pan_y_,scale,scale);
            vita2d_disable_clipping();
        }
    }
    void draw_settings() const {
        text(232,54,palette().text,1.23f,"Reading Settings",704,reader::FontRole::Heading);
        const char* names[]={"Literata","Atkinson","Vita system"};
        const char* sizes[]={"22 px","26 px","30 px"};
        const char* spacing[]={"1.2","1.5","1.8"};
        const char* themes[]={"Night","Paper","Sepia"};
        const char* labels[]={"Typeface","Font size","Line spacing","Page theme","Reading view"};
        const std::string values[]={names[settings_.reading_font],sizes[settings_.font],spacing[settings_.spacing],
            themes[settings_.theme],settings_.immersive?"Page only":"With controls"};
        for(int i=0;i<5;++i) {
            const auto box=reader::ui::settings_row(i);
            panel(box.x,box.y,box.width,box.height);
            text(box.x+16,box.y+22,palette().muted,0.78f,labels[i],220);
            text(box.x+16,box.y+47,palette().text,0.92f,values[i],260,reader::FontRole::Heading);
            text(box.x+box.width-76,box.y+38,palette().text,0.92f,"‹    ›",64);
        }
        const auto& paper=palettes[settings_.theme];
        reader::visual::rounded(632,80,304,340,paper.background,8);
        text(648,107,paper.muted,0.80f,"Reading preview",272);
        const std::string sample="A quiet evening. The next adventure awaits beyond the familiar path. Every story begins with a single page.";
        const auto rows=ui_wrap(sample,272,reading_pixels(),reading_font());
        vita2d_enable_clipping();vita2d_set_clip_rectangle(648,124,920,404);
        int y=124+reading_pixels();
        for(const auto& row:rows){if(y>404)break;fonts_.draw(reading_font(),reading_pixels(),648,y,paper.text,row.text);y+=line_height();}
        vita2d_disable_clipping();
        text(232,455,palette().muted,0.80f,"Left / Right adjusts. Apply saves; Cancel discards.",704);
    }

    struct LibraryView { std::string filename,query; reader::LibraryFilter filter; bool search_active; };
    std::map<int,LibraryView> library_views_;
    std::map<int,int> focus_memory_;
    bool focus_result_=true,settings_sidebar_return_=false;
    int settings_nav_return_=0,bookmarks_nav_return_=0;
    std::size_t import_selected_=0,source_selected_=0,catalog_selected_=0;
    reader::LibraryFilter import_filter_=reader::LibraryFilter::All;
    std::string import_query_;
    reader::VitaFonts fonts_;
    mutable std::map<std::tuple<int,unsigned,reader::FontRole,std::string>,std::string> fit_cache_;
    mutable std::map<std::tuple<int,unsigned,reader::FontRole,bool,std::string>,std::vector<reader::Line>> wrap_cache_;
    std::unique_ptr<vita2d_texture,TextureDeleter> backdrop_;
    std::unique_ptr<vita2d_texture,TextureDeleter> logo_;
    std::vector<CoverTexture> covers_;
    reader::LibraryItem online_detail_item_;
    std::vector<BookmarkPreview> bookmark_previews_;
    mutable std::vector<std::size_t> bookmark_indices_;
    mutable bool bookmarks_dirty_=true;
    bool library_order_dirty_=true,ordered_importing_=false;
    reader::LibrarySort ordered_sort_=reader::LibrarySort::Title;
    reader::LibraryFilter ordered_filter_=reader::LibraryFilter::All;
    std::string ordered_query_;
    int nav_selected_=0, nav_active_=0, about_selected_=0;
    bool sidebar_focus_=true, pending_bookmark_=false;
    std::size_t bookmark_selected_=0;
    bool ime_library_=false;
    bool library_search_active_=false,add_book_menu_=false;
    int library_scroll_=0;
    std::string library_query_;
    Screen settings_return_=Screen::Home, bookmarks_return_=Screen::Home;
    bool interface_layer_ = false;
    bool redraw_=true;
    std::string pressed_action_;
    std::uint64_t pressed_until_ = 0;
    std::size_t description_row_ = 0;
    bool immersive_ = true, library_initialized_ = false;
    std::vector<Action> menu_;
    struct MenuParent { std::vector<Action> actions; std::string title; std::size_t selected; bool add_book; };
    std::vector<MenuParent> menu_parents_;
    std::string menu_title_, notice_;
    std::size_t menu_selected_ = 0;
    std::uint64_t notice_until_ = 0;
    bool ime_jump_ = false, retry_available_ = false, retry_in_place_ = false, online_started_ = false;
    reader::online::Request retry_request_;
    std::vector<bool> offline_chapters_;
    std::size_t offline_count_ = 0, online_saved_chapter_ = 0, download_completed_ = 0, download_total_ = 0;
    reader::speech::Player speech_{reader::speech::platform_audio};
    reader::speech::State speech_seen_ = reader::speech::State::Idle;
    std::size_t speech_offset_ = 0;
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
    std::vector<reader::LibraryItem> collection_entries_;
    bool collection_suspended_=false,collection_dirty_=false;
    bool library_preferences_pending_=false;
    reader::Progress persisted_progress_;
    std::string persisted_filename_;
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
    bool contents_from_details_=false;
    int setting_row_ = 0;
    Screen screen_ = Screen::Home;
    Screen image_return_ = Screen::Illustrations;
};
} // namespace

#ifndef NOVELREADER_FRONTEND_TESTS
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
        bool running = true;
        while (running) {
            SceCtrlData pad{};
            pad.lx = pad.ly = pad.rx = pad.ry = 128;
            const bool pad_ready = sceCtrlPeekBufferPositive(0, &pad, 1) > 0;
            const auto now = sceKernelGetProcessTimeWide();
#ifdef NOVELREADER_PROFILE
            if(previous_time && now>=previous_time)reader::perf::record(reader::perf::Event::FrameInterval,now-previous_time);
#endif
            const auto seconds = previous_time && now >= previous_time ? std::min<std::uint64_t>(now - previous_time, 50000) / 1000000.0f : 0;
            previous_time = now;
            const auto before = app.screen();
            const auto context_before = app.input_context();
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
            const unsigned shoulders = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;
            if((held&shoulders)==shoulders)repeat.cancel_until_release(shoulders);
            const auto pressed = pad_ready ? repeat.sample(held, app.repeatable_buttons(), now) : 0;
            reader::controls::Gesture gesture;
            if (touch_enabled) {
                SceTouchData data{};
                if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &data, 1) > 0 && !pad.buttons && !left && !right) {
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
            if (app.input_context() == context_before) running = app.gesture(gesture);
            if (app.input_context() != context_before) {
                repeat.cancel_until_release(held);
                left_stick.cancel_until_neutral(); right_stick.cancel_until_neutral(); touch.cancel_until_release();
            }
            if(app.needs_redraw())app.draw();
            else {
                const reader::perf::Scope timing(reader::perf::Event::IdleWait);
                sceDisplayWaitVblankStart();
            }
        }
    }
    vita2d_free_pgf(font);
    vita2d_fini();
    sceAppUtilShutdown();
    if (touch_enabled) sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_STOP);
    sceKernelExitProcess(0);
    return 0;
}
#endif
