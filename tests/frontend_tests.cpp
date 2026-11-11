// Exercise the native screen registry, input dispatch and return state. Only
// platform drawing, keyboard and directory calls are mocked; reader/layout,
// settings, library and online service logic are the normal host implementations.
#define NOVELREADER_FRONTEND_TESTS
#include "../src/main.cpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>

namespace reader {
struct FontDraw { FontRole role; unsigned pixels,color; int x,y; std::string text; };
bool capture_font_draws=false;
std::vector<FontDraw> font_draws;
struct VitaFonts::Impl {};
VitaFonts::VitaFonts(vita2d_pgf*):impl_(new Impl){}
VitaFonts::~VitaFonts()=default;
int VitaFonts::width(FontRole role,unsigned pixels,const std::string& text)const{return text.size()*pixels/(role==FontRole::Sans?3:2);}
void VitaFonts::draw(FontRole role,unsigned pixels,int x,int y,unsigned color,const std::string& text)const {
    if(capture_font_draws)font_draws.push_back({role,pixels,color,x,y,text});
}
bool VitaFonts::complete()const{return true;}
int VitaFonts::title_width(unsigned pixels,const std::string& text)const{return width(FontRole::Heading,pixels,text);}
void VitaFonts::draw_title(unsigned pixels,int x,int y,unsigned color,const std::string& text)const{draw(FontRole::Heading,pixels,x,y,color,text);}
}
vita2d_texture* decode_texture(const std::string&,std::string&){return nullptr;}
vita2d_texture* decode_thumbnail(const std::string&,unsigned,unsigned,std::string&){return nullptr;}

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct FrontendTest {
    static void control_regressions() {
        std::vector<std::string> names;
        for(int i=0;i<8;++i) {
            const auto name="input-"+std::to_string(i)+".txt";names.push_back(name);
            std::ofstream text(books_directory+'/'+name);
            for(int line=0;line<100;++line)text<<"A long reading fixture follows the path across the mountains.\n\n";
            reader::Progress saved;saved.has_bookmark=true;
            check(reader::save_progress(reader::progress_path(progress_directory,name),saved),"Input bookmark fixture");
        }
        check(reader::save_library(library_path,names) && reader::save_settings(settings_path,{}),"Input library fixture");
        vita2d_pgf font;App app(&font);app.message_.clear();
        const auto tap=[&](int x,int y,int start_x=-1,int start_y=-1) {
            return app.gesture({reader::controls::GestureKind::Tap,float(x),float(y),0,0,1,float(start_x),float(start_y)});
        };
        const auto footer=[&](unsigned key) {
            app.normalize_focus();
            const auto actions=app.toolbar();
            for(std::size_t i=0;i<actions.size();++i)if(actions[i].button==key) {
                const auto slot=app.menu_.empty()?app.toolbar_rect(actions,i):reader::ui::toolbar_button(i,actions.size());
                const auto hit=app.footer_touch_rect(slot,actions[i]);
                tap(hit.x+hit.width/2,520);return;
            }
            throw std::runtime_error("Missing footer action in input test");
        };
        // Touch-only entry, filtering, search and import; the same actions are
        // subsequently used by controller focus and shortcuts.
        tap(60,168);check(app.screen_==Screen::Library,"Touch-only Library entry failed");
        app.input(SCE_CTRL_TRIANGLE);check(app.menu_title_=="Sort and filter books","Triangle filter shortcut changed");
        const auto title=app.menu_title_;app.input(SCE_CTRL_START|SCE_CTRL_SQUARE);
        check(app.menu_.empty() && !app.ime_active_,"START did not close the popup or leaked Square");
        footer(SCE_CTRL_TRIANGLE);
        check(app.menu_title_==title,"Touch Filter does not share filter state");
        footer(SCE_CTRL_CIRCLE);app.input(SCE_CTRL_SQUARE);
        check(app.ime_active_ && app.ime_library_,"Square duplicates filters instead of searching");
        test_ime_status()=SCE_COMMON_DIALOG_STATUS_FINISHED;test_ime_button()=0;
        app.input(SCE_CTRL_CROSS|SCE_CTRL_CIRCLE);test_ime_status()=0;
        check(app.screen_==Screen::Library && !app.ime_active_,"Keyboard close leaked a button into Library");
        app.apply_library_search("input-0");footer(SCE_CTRL_CIRCLE);
        check(!app.library_search_active_ && app.library_query_.empty() && app.screen_==Screen::Library,
              "Touch Back did not preserve Circle's search-exit behavior");
        tap(800,48);const auto import_row=app.menu_layout().row(0);
        tap(import_row.x+60,import_row.y+17);check(app.importing_,"Touch-only import entry failed");
        app.input(SCE_CTRL_TRIANGLE);check(app.menu_.empty(),"Import refresh opened generic Options");
        const auto import_context=app.input_context();
        footer(SCE_CTRL_CIRCLE);check(!app.importing_ && app.input_context()!=import_context,"Import cancel did not change input context");

        // Hit targets exclude gutters, clipped cards, and tap drift crossing an
        // element boundary. Footer hit targets follow the visible hint text.
        const auto selected=app.selected_;
        tap(410,220);tap(740,520); // Cover gutter / between footer hints.
        check(app.screen_==Screen::Library && app.menu_.empty() && app.selected_==selected,"Empty space activated a control");
        footer(SCE_CTRL_SQUARE);check(app.ime_active_,"Touch footer Search did not open the keyboard");
        test_ime_status()=SCE_COMMON_DIALOG_STATUS_FINISHED;test_ime_button()=0;app.input(0);test_ime_status()=0;
        reader::controls::Touch touch;
        touch.sample({{1,300,320}},1000,false);touch.sample({{1,300,200}},101000,false);
        app.gesture(touch.sample({},201000,false));
        check(app.screen_==Screen::Library && app.selected_==4,"Library scrolling opened a book or lost focus");
        app.gesture(touch.sample({},202000,false));
        check(app.selected_==4,"A released scroll repeated");
        tap(300,300);check(app.screen_==Screen::Book && app.selected_==4,"Touch opened wrong scrolled card");
        app.normalize_focus();check(app.widgets().size()==2 && app.toolbar()[0].label=="Select" && app.toolbar()[0].enabled,
            "Book details retained Bookmark or lost the Select hint");
        app.show_more();
        check(app.menu_.size()==3 && app.menu_[0].command==Command::FullTitle &&
              app.menu_[1].command==Command::RemoveBook && app.menu_[2].button==SCE_CTRL_CIRCLE,
              "Book details Options retained reading, bookmark or exit actions");
        app.input(SCE_CTRL_CIRCLE);
        reader::font_draws.clear();reader::capture_font_draws=true;app.draw_frame();reader::capture_font_draws=false;
        int formats=0;
        for(const auto& draw:reader::font_draws)if(draw.text=="TXT") {++formats;check(draw.x==730,"Duplicate format badge remains below the author");}
        check(formats==1,"Local details do not show the format exactly once");
        const auto local_read_time=app.entries_[app.order_[app.selected_]].progress.last_read;
        const auto local_chapters=reader::ui::details_secondary(false);
        tap(local_chapters.x+50,local_chapters.y+28);
        check(app.screen_==Screen::Contents && app.lines_.empty() && app.progress_.last_read==local_read_time,
            "Local Chapters started reading instead of opening the chapter list");
        app.input(SCE_CTRL_CIRCLE);check(app.screen_==Screen::Book,"Local Chapters Back did not return to details");
        app.set_focus(200);tap(300,420);check(app.screen_==Screen::Reader,"Touch-only Read failed");
        app.message_.clear();const auto original_top=app.top_;
        tap(322,220,319,220);
        check(app.menu_.empty() && app.top_==original_top,"Reading tap crossed action zones");
        const auto next_page=app.visible_end();app.input(SCE_CTRL_RIGHT|SCE_CTRL_DOWN);
        check(app.top_==next_page,"One diagonal D-pad sample ran two reading actions");
        check(app.page_turn_until_ && app.page_turn_forward_,"Forward page turn has no effect");
        reader::font_draws.clear();reader::capture_font_draws=true;app.draw();reader::capture_font_draws=false;
        bool shifted=false;
        for(const auto& draw:reader::font_draws)if(draw.role==app.reading_font() && draw.x==112)shifted=true;
        check(shifted && app.needs_redraw(),"Page effect does not animate the reading content");
        test_process_time()+=App::page_turn_duration/2;
        check(app.page_turn_remaining()>0 && app.page_turn_remaining()<1,"Page effect does not advance with time");
        test_process_time()+=App::page_turn_duration;
        app.needs_redraw();app.draw();
        check(!app.page_turn_until_ && !app.needs_redraw(),"Completed page effect prevents idle rendering");
        app.input(SCE_CTRL_LEFT);
        check(app.page_turn_until_ && !app.page_turn_forward_,"Backward page effect has wrong direction");
        app.input(SCE_CTRL_LEFT);
        check(!app.page_turn_until_,"Blocked first-page turn animated");
        app.gesture({reader::controls::GestureKind::Swipe,700,200,-80,70,1,780,130});
        check(app.top_==original_top,"Diagonal reading gesture accidentally turned a page");
        app.gesture({reader::controls::GestureKind::Swipe,500,220,-250,0,1,750,220});
        check(app.top_>original_top,"Reading swipe did not turn a page");
        const auto page_top=app.top_;app.input(SCE_CTRL_CROSS);
        check(app.screen_==Screen::Contents,"Reader Cross is not Contents");
        app.input(SCE_CTRL_CIRCLE);check(app.top_==page_top,"Contents Back lost reading position");
        footer(SCE_CTRL_START);check(app.menu_title_=="Reading controls","Touch-only Reading controls failed");
        reader::font_draws.clear();reader::capture_font_draws=true;app.draw_frame();reader::capture_font_draws=false;
        bool select_hint=false,close_hint=false;
        for(const auto& draw:reader::font_draws) {
            select_hint=select_hint || (draw.text=="Select" && draw.y==527);
            close_hint=close_hint || (draw.text=="Close" && draw.y==527);
        }
        check(select_hint && close_hint,"Popup does not show Select and Close in the bottom bar");
        footer(SCE_CTRL_CROSS);check(app.menu_title_=="Read aloud","Bottom-bar Select did not activate the popup selection");
        footer(SCE_CTRL_CIRCLE);check(app.menu_title_=="Reading controls","Bottom-bar Close did not return from submenu");
        check(app.widgets().size()==app.menu_.size(),"Reader popup hides actions behind pages");
        // Save bookmark through the menu, then through a long press. Neither
        // action should accidentally open Reading Settings.
        const auto bookmark_row=app.menu_layout().row(4);
        tap(bookmark_row.x+60,bookmark_row.y+17);check(app.menu_.empty() && app.screen_==Screen::Reader && app.progress_.has_bookmark,
                          "Menu bookmark action did not share the bookmark operation");
        app.gesture({reader::controls::GestureKind::Hold,500,220,0,0,1,500,220});
        check(app.screen_==Screen::Reader && app.progress_.bookmark==app.lines_[app.top_].offset,"Hold saved wrong bookmark");
        app.input(SCE_CTRL_TRIANGLE);check(app.screen_==Screen::Bookmarks,"Reader bookmarks shortcut failed");
        app.gesture({reader::controls::GestureKind::Swipe,300,180,0,-120,1,300,300});
        check(app.bookmark_selected_>=3,"Bookmarks cannot be browsed by touch");
        footer(SCE_CTRL_CIRCLE);
        check(app.screen_==Screen::Reader && app.nav_active_==1 && app.top_==page_top,"Bookmarks Back restored wrong context");
        app.input(SCE_CTRL_SQUARE);app.set_focus(1000);app.input(SCE_CTRL_RIGHT);
        app.input(SCE_CTRL_CIRCLE|SCE_CTRL_CROSS);
        check(app.screen_==Screen::Reader && app.settings_.reading_font==0,"Simultaneous cancel/confirm applied Settings");
        app.input(SCE_CTRL_SQUARE);tap(590,101);footer(SCE_CTRL_CROSS);
        check(app.screen_==Screen::Reader && app.settings_.reading_font==1,"Touch-only Settings Apply failed");
        app.input(SCE_CTRL_SQUARE);app.input(SCE_CTRL_SQUARE);
        check(app.screen_==Screen::Settings,"Settings still has duplicate Apply buttons");
        // Direct controls choose their displayed value, including repeated taps.
        const auto size_track=reader::ui::settings_slider(1);
        tap(size_track.x+size_track.width-1,size_track.y+16);
        check(app.settings_.font==2 && app.setting_row_==1,"Font slider did not select its last step");
        tap(size_track.x,size_track.y+16);
        check(app.settings_.font==0,"Font slider did not select its first step");
        const auto spacing_track=reader::ui::settings_slider(2);
        tap(spacing_track.x+spacing_track.width/2,spacing_track.y+16);
        check(app.settings_.spacing==1,"Spacing slider did not select its middle step");
        for(int theme=0;theme<3;++theme) {
            const auto swatch=reader::ui::settings_swatch(theme);
            tap(swatch.x+24,swatch.y+14);tap(swatch.x+24,swatch.y+14);
            check(app.settings_.theme==theme && app.setting_row_==3,"Theme swatch cycled instead of selecting");
        }
        const auto size_row=reader::ui::settings_row(1);
        tap(300,size_row.y+14);
        check(app.settings_.font==0 && app.setting_row_==1,"Tapping a setting label changed its value");
        for(int immersive=0;immersive<2;++immersive) {
            app.settings_.immersive=immersive;
            reader::font_draws.clear();reader::capture_font_draws=true;
            app.draw_frame();reader::capture_font_draws=false;
            bool preview_controls=false;
            for(const auto& draw:reader::font_draws)preview_controls=preview_controls || draw.text=="Chapter 1";
            check(!preview_controls,"Reading preview still shows reading-view controls");
        }
        check(app.widgets().size()==4,"Settings still exposes Reading View");
        app.set_focus(1003);app.input(SCE_CTRL_DOWN);
        check(app.setting_row_==3,"Settings focus moved below Page theme");
        footer(SCE_CTRL_SELECT);check(app.settings_.reading_font==0,"Touch Settings reset failed");
        footer(SCE_CTRL_CIRCLE);check(app.settings_.reading_font==1,"Touch Settings cancel did not discard reset");

        // Modal isolation also covers START, sidebars, gestures and shortcuts.
        app.message_="Modal failure";const auto modal_context=app.input_context();const auto modal_top=app.top_;
        for(const auto key:{SCE_CTRL_START,SCE_CTRL_TRIANGLE,SCE_CTRL_SQUARE,SCE_CTRL_SELECT,SCE_CTRL_RTRIGGER})app.input(key);
        tap(60,170);app.gesture({reader::controls::GestureKind::Swipe,500,220,-250,0,1,750,220});
        check(app.input_context()==modal_context && app.top_==modal_top,"Error modal leaked input to Reader");
        footer(SCE_CTRL_CIRCLE);check(app.message_.empty(),"Touch-only error dismissal failed");
        if(!app.immersive_)app.toggle_immersive();
        const auto hidden_anchor=app.lines_[app.top_].offset;
        tap(480,490);
        check(!app.immersive_ && app.menu_.empty() && app.lines_[app.top_].offset==hidden_anchor,
              "Center tap did not show reading bars at the same anchor");
        const auto visible_anchor=app.lines_[app.top_].offset;
        tap(480,220);
        check(app.immersive_ && app.menu_.empty() && app.lines_[app.top_].offset==visible_anchor &&
              reader::load_settings(settings_path).immersive==1,"Center tap did not hide and persist reading bars");
        reader::font_draws.clear();reader::capture_font_draws=true;app.draw_frame();reader::capture_font_draws=false;
        for(const auto& draw:reader::font_draws)
            check(draw.role!=reader::FontRole::Interface || draw.text.find("Chapter ")!=0,
                  "Hidden reading bars still draw chapter information");
        tap(480,220);check(!app.immersive_ && app.menu_.empty(),"Repeated center tap opened options");

        // Exercise held state through the actual screen dispatcher, including
        // a press which opens a dialog while a navigation key is still down.
        reader::controls::Repeat buttons;std::uint64_t now=1000;
        const auto poll=[&](unsigned held) {
            const auto context=app.input_context();
            app.input(buttons.sample(held,app.repeatable_buttons(),now));
            if(context!=app.input_context())buttons.cancel_until_release(held);
            now+=400000;
        };
        poll(SCE_CTRL_START|SCE_CTRL_DOWN);const auto first_tile=app.menu_selected_;
        poll(SCE_CTRL_START|SCE_CTRL_DOWN);
        check(app.menu_selected_==first_tile,"Held navigation leaked into a new modal");
        poll(0);poll(SCE_CTRL_CIRCLE);poll(SCE_CTRL_CIRCLE);
        check(app.screen_==Screen::Reader && app.menu_.empty(),"Held modal Back exited Reader");
        poll(0);poll(SCE_CTRL_CIRCLE);check(app.screen_==Screen::Library,"Fresh Back was delayed after release");
        app.navigate(0);reader::controls::Repeat sidebar;
        check(sidebar.sample(SCE_CTRL_RTRIGGER,app.repeatable_buttons(),1000)==SCE_CTRL_RTRIGGER &&
              sidebar.sample(SCE_CTRL_RTRIGGER,app.repeatable_buttons(),900000)==0,"Held sidebar trigger cycles destinations");

        // Contextual Triangle shortcuts must reach their implementation.
        app.publication_.chapters.push_back({"Chapter 1",""});app.screen_=Screen::Contents;app.input(SCE_CTRL_TRIANGLE);
        check(app.ime_active_ && app.menu_.empty(),"Contents Triangle was intercepted by Options");
        app.ime_active_=false;app.screen_=Screen::Image;app.zoom_=3;app.pan_x_=30;app.pan_y_=20;
        app.input(SCE_CTRL_TRIANGLE);check(app.zoom_==1 && app.pan_x_==0 && app.pan_y_==0,"Viewer Triangle does not fit image");
        app.zoom_=2;app.gesture({reader::controls::GestureKind::Transform,400,200,10,10,1.25f,400,500});
        check(app.zoom_==2,"Viewer transformed a gesture starting in the footer");
        app.image_return_=Screen::Reader;footer(SCE_CTRL_CIRCLE);check(app.screen_==Screen::Reader,"Viewer touch Back lost its origin");
        app.image_return_=Screen::Illustrations;app.screen_=Screen::Image;footer(SCE_CTRL_CIRCLE);
        check(app.screen_==Screen::Illustrations,"Viewer touch Back skipped the illustration list");
        app.input(SCE_CTRL_CIRCLE);app.input(SCE_CTRL_CIRCLE);footer(SCE_CTRL_CIRCLE);
        check(app.screen_==Screen::Library,"Touch Reader Back failed");
        tap(60,360);check(app.screen_==Screen::About,"Touch-only About entry failed");
        const auto credits=reader::ui::about_action(1);
        tap(credits.x+credits.width/2,credits.y+credits.height/2);
        check(app.feedback_active(),"Touch-only credits failed");footer(SCE_CTRL_CIRCLE);
        tap(60,104);check(app.screen_==Screen::Home,"Touch-only Home return failed");
        app.navigate(1);app.input(SCE_CTRL_SELECT);
        check(app.screen_==Screen::Library,"Removed Library Select shortcut is still active");
        app.perform({"Browse Online",0,Command::Online});
        check(app.screen_==Screen::Sources,"Browse Online action failed");footer(SCE_CTRL_CIRCLE);
        check(app.screen_==Screen::Library,"Touch-only Sources Back failed");
        // An unknown source fails before HTTP. Inspect the actual dispatched
        // request without contacting a catalog or changing the service API.
        for(const auto screen:{Screen::Catalog,Screen::Novel,Screen::OnlineChapters}) {
            App online(&font);online.message_.clear();online.screen_=screen;online.online_source_="input-test-no-source";
            online.online_catalog_.next="https://invalid.invalid/next";
            online.input(SCE_CTRL_TRIANGLE);
            check(online.menu_.empty(),"Online Triangle was intercepted by generic Options");
            check(online.retry_request_.action==(screen==Screen::Catalog?reader::online::Action::Browse:reader::online::Action::Chapters),
                  "Online Triangle dispatched the wrong request");
            if(screen==Screen::Catalog)check(online.retry_request_.url==online.online_catalog_.next,"Catalog Triangle lost next page");
        }
    }
    static void cache_regressions() {
        std::vector<std::string> names;
        for(int i=0;i<30;++i) {
            const auto name="cache-"+std::to_string(i)+".txt";names.push_back(name);
            std::ofstream book(books_directory+'/'+name);book<<"Original bookmarked passage.\n\nAnother page.\n";
            reader::Progress saved;saved.has_bookmark=true;
            check(reader::save_progress(reader::progress_path(progress_directory,name),saved),"Cache progress fixture");
        }
        check(reader::save_library(library_path,names),"Cache library fixture");
        check(reader::save_settings(settings_path,{}),"Cache settings fixture");
        vita2d_pgf font;App app(&font);app.message_.clear();
        app.navigate(1);app.draw();app.navigate(2);app.draw();
        check(app.bookmark_previews_.size()==3,"Visible bookmark previews were not cached");
        const auto first_preview=app.bookmark_previews_.front().quote;
#ifdef NOVELREADER_PROFILE
        reader::perf::reset();
#endif
        for(int i=0;i<10;++i) {
            app.navigate(0);app.draw();app.navigate(1);app.draw();app.navigate(2);app.draw();
        }
        check(app.bookmark_previews_.front().quote==first_preview,"Warm navigation lost preview contents");
#ifdef NOVELREADER_PROFILE
        for(const auto event:{reader::perf::Event::Scan,reader::perf::Event::Metadata,reader::perf::Event::Chapter,
                             reader::perf::Event::SettingsWrite,reader::perf::Event::LibraryOrder})
            check(reader::perf::samples()[static_cast<unsigned>(event)].calls==0,"Warm navigation repeated I/O or sorting");
#endif
        for(std::size_t page=0;page<10;++page){app.bookmark_selected_=page*3;app.sync_bookmark_previews();}
        check(app.bookmark_previews_.size()==24,"Bookmark preview cache is not bounded");
        app.bookmark_selected_=0;const auto bookmark_count=app.bookmarks().size();
        app.input(SCE_CTRL_SQUARE);check(app.bookmarks().size()==bookmark_count,"Square still deletes bookmarks");
        app.show_more();check(app.menu_.size()==2 && app.menu_[0].label=="Open" && app.menu_[1].label=="Remove","Bookmarks Options contains unrelated actions");
        app.input(SCE_CTRL_DOWN);app.input(SCE_CTRL_CROSS);app.normalize_focus();
        check(app.menu_selected_==1 && app.menu_parents_.size()==1 && app.bookmarks().size()==bookmark_count,"Bookmark removal skipped confirmation");
        app.input(SCE_CTRL_CIRCLE);check(app.menu_.size()==2 && app.menu_[1].command==Command::DeleteBookmark,"Cancel did not return to Bookmark Options");
        app.input(SCE_CTRL_CROSS);app.set_focus(2000);app.input(SCE_CTRL_CROSS);
        check(app.bookmarks().size()==29 && !reader::load_progress(reader::progress_path(progress_directory,names[0])).has_bookmark,
            "Bookmark deletion did not invalidate membership/persist");
        app.message_.clear();app.navigate(1);
        for(int run=0;run<6;++run) {
            const auto name=names[run%3];app.refresh_library(name);app.open();
            check(app.screen_==Screen::Reader,"Repeated book open failed");
            check(app.needs_redraw(),"Loading draw consumed the completed reader redraw");
            app.message_.clear();app.perform({"Save bookmark",0,Command::SaveBookmark});app.input(SCE_CTRL_CIRCLE);
            check(app.screen_==Screen::Library && app.book_.text.empty() && app.lines_.empty() && app.publication_.chapters.empty(),
                "Reader exit retained book buffers or changed destination");
            check(app.selected_filename()==name,"Reader exit lost selected book");
        }
        app.message_.clear();app.begin_import();
        check(app.collection_suspended_ && app.collection_entries_.size()==31,"Import discarded the cached collection");
        app.input(SCE_CTRL_CIRCLE);
        check(!app.collection_suspended_ && app.entries_.size()==31,"Import cancel did not restore cached collection");
        app.message_.clear();app.begin_import();app.perform({"Online",0,Command::Online});app.input(SCE_CTRL_CIRCLE);
        check(app.screen_==Screen::Library && !app.collection_suspended_ && app.entries_.size()==31,
            "Online shortcut from import did not restore collection");
        // Refresh is the explicit invalidation boundary for externally replaced files.
        {std::ofstream changed(books_directory+'/'+names[1]);changed<<"Replacement passage for the same book.\n";}
        app.begin_import();app.scan({},true);app.input(SCE_CTRL_CIRCLE);app.message_.clear();
        app.navigate(2);app.bookmark_selected_=0;app.sync_bookmark_previews();
        bool replacement=false;
        for(const auto& preview:app.bookmark_previews_)replacement=replacement || preview.quote.find("Replacement passage")!=std::string::npos;
        check(replacement,"Import refresh kept a stale bookmark quote");
        app.navigate(1);app.refresh_library(names[1]);
        app.perform({"Remove",0,Command::ConfirmRemoveBook});app.message_.clear();
        check(app.entries_.size()==30 && app.bookmarks().size()==29,"Removal left stale collection/bookmark membership");
        check(app.add_to_library(names[1]),"Could not reimport removed book");app.navigate(1);
        check(app.entries_.size()==31 && app.bookmarks().size()==29 &&
              !reader::load_progress(reader::progress_path(progress_directory,names[1])).has_bookmark,"Reimport restored deleted progress");
        for(auto& item:app.entries_)if(!item.guide)item.cover_path="missing.png";
        for(std::size_t i=0;i<app.order_.size();i+=4){app.selected_=i;app.ensure_library_visible();app.sync_covers();}
        check(app.covers_.size()==12,"Cover cache is not bounded");
        const auto count=app.covers_.size();app.navigate(2);app.sync_covers();
        check(app.covers_.size()==count,"Leaving Library discarded its covers");
        app.update_cover();check(app.cover_key_.empty(),"Bookmarks decoded an invisible full-size cover");
        app.screen_=Screen::Library;app.importing_=true;app.update_cover();
        check(app.cover_key_.empty(),"Import rows decoded an invisible full-size cover");
        app.importing_=false;app.navigate(0);app.update_cover();
        check(app.cover_key_.empty(),"Home decoded an invisible full-size cover");
        app.message_.clear();app.notice_until_=0;app.pressed_until_=0;app.draw();
        for(int i=0;i<120;++i){app.input(0);check(!app.needs_redraw(),"Idle polling invalidates an unchanged screen");}
        app.input(SCE_CTRL_RTRIGGER);check(app.needs_redraw(),"Navigation failed to request redraw");app.draw();
        app.notify("Temporary notice");check(app.needs_redraw(),"Notice failed to request redraw");app.draw();
        test_process_time()+=2800001;
        check(app.needs_redraw() && app.message_.empty(),"Notice expiry failed to request redraw");app.draw();
        app.perform({"Feedback"});app.draw();test_process_time()+=160001;
        check(app.needs_redraw(),"Pressed feedback expiry failed to request redraw");app.draw();
        app.ime_active_=true;check(app.needs_redraw(),"System keyboard updates were suspended");app.ime_active_=false;
        test_process_time()=1000000;

        reader::online::Novel novel{"novgo","Cached online novel","Author","https://novgo.net/cache-test.html","Description",{}};
        for(int i=0;i<1024;++i)novel.chapters.push_back({"Chapter "+std::to_string(i),"https://novgo.net/cache-test/"+std::to_string(i)+".html",false});
        const std::string basename=reader::online::stable_id(novel.url)+".nvr";
        const std::string identity="Online/novgo/"+basename,manifest=books_directory+'/'+identity;
        mkdir((books_directory+"/Online").c_str(),0777);mkdir((books_directory+"/Online/novgo").c_str(),0777);
        check(reader::online::save_novel(manifest,novel),"Online cache fixture");
        mkdir((books_directory+"/Online/novgo/.chapters").c_str(),0777);mkdir((books_directory+"/Online/novgo/.chapters/"+basename).c_str(),0777);
        check(reader::online::atomic_write(reader::online::chapter_path(manifest,novel.chapters[0].url),"An offline chapter.\n"),"Offline chapter fixture");
        reader::Progress progress;progress.has_bookmark=true;
        check(reader::save_progress(reader::progress_path(progress_directory,identity),progress),"Online progress fixture");
        check(reader::save_library(library_path,{identity}),"Online library fixture");
        App online(&font);online.message_.clear();online.navigate(1);online.refresh_library(identity);online.sync_covers();
        std::string png("\x89PNG\r\n\x1a\n",8);png.resize(33);png[11]=13;png.replace(12,4,"IHDR");png[19]=1;png[23]=1;
        check(reader::online::atomic_write(reader::online::cover_path(manifest),png),"Online artwork fixture");
        online.refresh_online_library_item(manifest);
        const auto& artwork_item=online.entries_[online.order_[online.selected_]];
        std::string artwork_bytes,artwork_error;
        check(artwork_item.cover_path==reader::online::cover_path(manifest) &&
              App::load_cover_bytes(artwork_item,artwork_bytes,artwork_error) && artwork_bytes==png,
              "Library artwork was sent to the EPUB loader or lost from metadata");
        for(const auto& cached:online.covers_)check(cached.key!=manifest,"Cover refresh retained a missing-artwork cache entry");
        online.open();
        check(online.screen_==Screen::Novel && online.offline_count_==1,"Online summary fixture");
        online.normalize_focus();check(focus(online)==200,"Online details do not focus the reading CTA");
        check(online.toolbar()[0].label=="Select" && online.toolbar()[0].enabled &&
              online.toolbar()[2].label=="Add to Library" && !online.toolbar()[2].enabled,"Details footer labels or saved-book dimming changed");
        const auto enlarged=reader::ui::details_cover();
        check(enlarged.width>198 && enlarged.y==72 && enlarged.y+enlarged.height==452,
              "Shared details artwork does not align with the progress card");
        online.show_more();
        for(const auto& action:online.menu_)
            check(action.button!=SCE_CTRL_SELECT && action.command!=Command::Exit,
                  "Online details Options retained reading or exit actions");
        online.input(SCE_CTRL_CIRCLE);
        check(online.widgets().size()==5,"Unified online details lost description or actions");
        const auto chapters=reader::ui::details_secondary(true);
        online.gesture({reader::controls::GestureKind::Tap,float(chapters.x+50),float(chapters.y+28)});
        check(online.screen_==Screen::OnlineChapters,"Unified details Chapters CTA failed");
        check(online.page_limit()==5 && online.widgets().size()==5,"Online chapter layout does not expose five rows");
        online.set_focus(1004);online.input(SCE_CTRL_DOWN);
        check(online.online_selected_==5 && focus(online)==1005 && online.widgets().front().target.box.x==288,
            "Chapter navigation did not cross into the next page beside the cover");
        online.draw_frame();online.input(SCE_CTRL_LEFT);
        check(online.online_selected_==0,"Chapter page-back did not restore the first page");
        online.input(SCE_CTRL_CIRCLE);check(online.screen_==Screen::Novel,"Chapters Back lost online details");
        online.online_novel_.description=std::string(1000,'a');online.set_focus(1000);online.input(SCE_CTRL_DOWN);
        check(online.toolbar()[0].label=="Select" && !online.toolbar()[0].enabled,"Select is not dimmed on the summary");
        online.set_focus(201);check(online.toolbar()[0].label=="Select" && online.toolbar()[0].enabled,"Select label changes with focused actions");online.set_focus(1000);
        check(online.description_row_==1,"Unified description does not scroll");
        online.input(SCE_CTRL_CROSS);check(online.screen_==Screen::Novel,"Description activation unexpectedly navigated away");
        reader::font_draws.clear();reader::capture_font_draws=true;online.draw_frame();reader::capture_font_draws=false;
        bool artwork_title=false,reading_cta=false,offline_stat=false;
        for(const auto& draw:reader::font_draws) {
            artwork_title=artwork_title || (draw.text==novel.title && draw.x==288 && draw.y==90);
            reading_cta=reading_cta || draw.text=="Start Reading";
            offline_stat=offline_stat || draw.text=="Available offline";
        }
        check(artwork_title && reading_cta && offline_stat,"Online details do not share the downloaded layout");
        online.set_focus(200);online.input(SCE_CTRL_CROSS);
        for(int i=0;i<500 && online.online_busy_;++i) {std::this_thread::sleep_for(std::chrono::milliseconds(2));online.input(0);}
        check(online.screen_==Screen::Reader && !online.online_busy_,"Unified online Read CTA cannot open an offline chapter");
#ifdef NOVELREADER_PROFILE
        reader::perf::reset();
#endif
        online.input(SCE_CTRL_CIRCLE);
        check(online.screen_==Screen::OnlineChapters && online.online_saved_chapter_==0 && online.offline_count_==1,
            "Online exit lost destination/progress/availability");
#ifdef NOVELREADER_PROFILE
        check(reader::perf::samples()[static_cast<unsigned>(reader::perf::Event::Metadata)].calls==0 &&
              reader::perf::samples()[static_cast<unsigned>(reader::perf::Event::OfflineProbe)].calls==0,
              "Online exit reopened the manifest or probed every chapter");
#endif
        std::swap(novel.chapters[0],novel.chapters[1]);
        check(reader::online::save_novel(manifest,novel),"Cannot reorder online fixture");
        online.refresh_online_library_item(manifest);
        check(online.entries_[0].progress.bookmark_chapter==1 && online.bookmark_previews_.empty(),
            "Online refresh retained stale bookmark identity/quote");
        check(online.add_to_library(names[0]),"Cannot add unrelated book for removal regression");
        online.message_.clear();online.refresh_library(names[0]);
        check(online.selected_filename()==names[0],"Removal regression did not select an unrelated library book");
        const auto remove_option=[&]() {
            for(std::size_t i=0;i<online.menu_.size();++i)if(online.menu_[i].command==Command::RemoveBook)return static_cast<int>(i);
            return -1;
        };
        online.screen_=Screen::OnlineChapters;online.show_more();check(remove_option()>=0,"Saved online chapter list has no Remove option");online.close_menu();
        online.screen_=Screen::Novel;online.show_more();const auto remove=remove_option();
        check(remove>=0,"Saved online details have no Remove option");
        online.set_focus(2000+remove);online.input(SCE_CTRL_CROSS);online.normalize_focus();
        check(online.menu_selected_==1,"Online removal does not default to Keep");
        online.input(SCE_CTRL_CROSS);check(online.in_library(),"Cancel removed an online favorite");
        const auto chapter_file=reader::online::chapter_path(manifest,novel.chapters[0].url);
        const auto download_folder=chapter_file.substr(0,chapter_file.find_last_of('/'));
        check(reader::online::atomic_write(download_folder+"/orphan.txt","Old chapter") &&
              reader::online::atomic_write(download_folder+"/interrupted.txt.tmp","Partial chapter"),"Cannot create cleanup regression files");
        online.show_more();online.set_focus(2000+remove_option());online.input(SCE_CTRL_CROSS);
        online.set_focus(2000);online.input(SCE_CTRL_CROSS);
        for(int i=0;i<2000 && online.online_busy_;++i) {std::this_thread::sleep_for(std::chrono::milliseconds(2));online.input(0);}
        check(!online.online_busy_ && !online.retry_available_,"Downloaded book cleanup did not complete");
        check(online.screen_==Screen::Library && !online.in_library() && online.library_books_==std::vector<std::string>{names[0]},
            "Online removal removed the selected background book or retained the favorite");
        std::vector<std::string> persisted;
        check(reader::load_library(library_path,persisted) && persisted==online.library_books_,"Online removal was not persisted");
        reader::online::Novel kept;
        check(!reader::online::load_novel(manifest,kept,artwork_error) && !reader::online::cached(manifest,novel.chapters[1].url) &&
              !reader::online::load_cover(manifest,artwork_bytes,artwork_error) &&
              reader::load_progress(reader::progress_path(progress_directory,identity)).last_read==0,
              "Removing an online favorite retained downloads, artwork, or progress");
        SceIoStat deleted_folder{};
        check(sceIoGetstat(download_folder.c_str(),&deleted_folder)<0,"Cleanup retained orphaned or partial chapter files");
        online.message_.clear();online.screen_=Screen::Novel;online.show_more();
        check(remove_option()==-1,"Unsaved online novel still offers removal");online.close_menu();
    }
#ifdef NOVELREADER_PROFILE
    static void benchmark() {
        // Real files, progress and layout; Vita font/GPU/input APIs are mocked.
        std::vector<std::string> names;
        const std::string paragraph="A quiet adventure follows a winding path beyond the mountains.\n\n";
        for(int i=0;i<512;++i) {
            const auto name="perf-"+std::to_string(i)+".txt";names.push_back(name);
            std::ofstream book(books_directory+'/'+name);
            if(i<3)for(int j=0;j<32768;++j)book<<paragraph;
            else book<<paragraph;
            reader::Progress progress;progress.has_bookmark=i<3;progress.bookmark=100;
            check(reader::save_progress(reader::progress_path(progress_directory,name),progress),"Benchmark progress fixture");
        }
        check(reader::save_library(library_path,names),"Benchmark collection fixture");
        check(reader::save_settings(settings_path,{}),"Benchmark settings fixture");
        vita2d_pgf font;App app(&font);app.message_.clear();
        reader::perf::reset();
        for(int i=0;i<12;++i) {
            app.navigate(1);app.draw();app.navigate(0);app.draw();
            app.navigate(2);app.draw();app.navigate(1);app.draw();
        }
        std::cout<<"Warm navigation (512 TXT, three 2 MiB bookmarks, 12 cycles)\n";
        reader::perf::dump(stdout);reader::perf::reset();
        app.refresh_library("perf-511.txt");app.open();app.message_.clear();
        for(int i=0;i<12;++i) {
            app.input(SCE_CTRL_CIRCLE);app.draw();
            if(i+1<12){app.open();app.message_.clear();}
        }
        std::cout<<"Reader open/exit (12 cycles, small TXT)\n";reader::perf::dump(stdout);
    }
#endif
    static int focus(App& app){app.normalize_focus();return app.focus_memory_[app.focus_key()];}
    static void verify_graph(App& app) {
        const auto controls=app.widgets();
        std::vector<reader::ui::FocusTarget> targets;
        for(const auto& control:controls) {
            const auto& b=control.target.box;
            check(control.kind!=App::WidgetKind::Sidebar,"Sidebar entered the focus registry");
            check(b.y<reader::ui::toolbar_top,"Footer entered the focus registry");
            check(b.height>=(control.kind==App::WidgetKind::Menu?34:56),"Native widget is below the minimum target size");
            check(b.width>=56,"Native widget is too narrow");
            check(b.x>=0 && b.x+b.width<=960 && b.y>=20 && b.y+b.height<=544,"Native widget is off screen");
            for(const auto& target:targets) {
                check(target.id!=control.target.id,"Duplicate control identity");
                const auto& a=target.box;
                check(!(a.x<b.x+b.width && b.x<a.x+a.width && a.y<b.y+b.height && b.y<a.y+a.height),
                      "Native interactive controls overlap");
            }
            targets.push_back(control.target);
        }
        for(const auto& start:targets) {
            if(!start.enabled)continue;
            std::vector<int> reached{start.id};
            for(std::size_t i=0;i<reached.size();++i)for(const int next:reader::ui::focus_neighbors(targets,reached[i]))
                if(std::find(reached.begin(),reached.end(),next)==reached.end())reached.push_back(next);
            for(const auto& target:targets)if(target.enabled)
                if(std::find(reached.begin(),reached.end(),target.id)==reached.end())
                    throw std::runtime_error("Unreachable control "+std::to_string(target.id)+" from "+std::to_string(start.id)+
                        " on screen "+std::to_string(static_cast<int>(app.screen_))+" / "+app.menu_title_);
        }
        app.draw();
    }
    static void run() {
        check(reader::save_settings(settings_path,reader::Settings{}),"Cannot initialize fixture preferences");
        check(reader::save_library(library_path,{}),"Cannot reset fixture library");
        check(reader::save_progress(reader::progress_path(progress_directory,"@welcome"),{}),"Cannot reset guide history");
        reader::font_draws.clear();reader::capture_font_draws=true;
        vita2d_pgf pgf;App app(&pgf);
        reader::capture_font_draws=false;
        int startup_titles=0,startup_subtitles=0;
        for(const auto& draw:reader::font_draws) {
            if(draw.text=="NovelReader" && draw.y==283)++startup_titles;
            if(draw.text=="by basitalitech" && draw.y==321)++startup_subtitles;
            check(draw.text!="Options","Startup must not show the Home footer");
        }
        check(startup_titles==4 && startup_subtitles==4,"Startup stages must show the title and author");
        check(app.screen_==Screen::Home && app.needs_redraw(),"Startup did not hand off to Home");
        app.message_.clear();app.sidebar_focus_=false;app.focus_memory_.clear();
        check(app.toolbar().size()==1 && app.toolbar()[0].button==SCE_CTRL_CROSS,"Home retains the Options hint");
        app.input(SCE_CTRL_START);check(app.menu_.empty(),"Home START still opens Options");
        verify_graph(app);
        for(const auto key:{SCE_CTRL_LEFT,SCE_CTRL_RIGHT,SCE_CTRL_UP,SCE_CTRL_DOWN})for(int i=0;i<8;++i)app.input(key);
        check(app.screen_==Screen::Home && app.nav_active_==0 && focus(app)==1000,"D-pad left Home content");
        reader::controls::Gesture tap;tap.kind=reader::controls::GestureKind::Tap;tap.x=60;tap.y=170;
        app.gesture(tap);check(app.screen_==Screen::Library,"Touch cannot activate the sidebar");
        tap.x=230;tap.y=520;app.gesture(tap);check(app.screen_==Screen::Home,"Touch cannot activate footer Back");
        app.input(SCE_CTRL_RTRIGGER);
        check(app.screen_==Screen::Library && app.nav_active_==1 && !app.sidebar_focus_,"R did not switch sidebar destination into content");
        app.input(SCE_CTRL_LTRIGGER);
        check(app.screen_==Screen::Home && app.nav_active_==0 && focus(app)==1000,"L did not return to Home content");
        const Screen destinations[]={Screen::Home,Screen::Library,Screen::Bookmarks,Screen::Settings,Screen::About};
        for(int i=1;i<=reader::ui::sidebar_count;++i) {
            app.input(SCE_CTRL_RTRIGGER);
            check(app.screen_==destinations[i%reader::ui::sidebar_count] && app.nav_active_==i%reader::ui::sidebar_count,
                "R sidebar cycle contains a removed or incorrect destination");
        }
        for(int i=reader::ui::sidebar_count-1;i>=0;--i) {
            app.input(SCE_CTRL_LTRIGGER);
            check(app.screen_==destinations[i] && app.nav_active_==i,
                "L sidebar cycle contains a removed or incorrect destination");
        }
        app.navigate(4);app.normalize_focus();verify_graph(app);
        const auto about_controls=app.widgets();
        check(about_controls.size()==3 && !about_controls[0].target.enabled &&
              about_controls[0].action.label=="Check for Updates","About update placeholder is missing or connected");
        check(focus(app)==1001,"About did not skip the unconnected update button");
        const auto update=reader::ui::about_action(0);
        app.gesture({reader::controls::GestureKind::Tap,float(update.x+update.width/2),float(update.y+update.height/2)});
        check(app.message_.empty() && !app.online_busy_ && focus(app)==1001,"Update placeholder performed an action");
        app.input(SCE_CTRL_RIGHT);check(focus(app)==1002,"About License cannot be reached");
        app.input(SCE_CTRL_CROSS);check(!app.message_.empty(),"About License no longer opens");
        app.input(SCE_CTRL_CIRCLE);app.input(SCE_CTRL_LEFT);
        check(focus(app)==1001,"About Credits cannot be reached");
        app.input(SCE_CTRL_CROSS);check(!app.message_.empty(),"About Credits no longer opens");
        app.input(SCE_CTRL_CIRCLE);app.navigate(0);
        app.focus_memory_[app.focus_key()]=304;
        check(focus(app)==1000,"Stale footer focus was not restored to content");
        app.focus_memory_[app.focus_key()]=100;
        check(focus(app)==1000,"Stale sidebar focus was not restored to content");
        for(const auto& control:app.widgets())check(control.action.command!=Command::Import,"Home still has Add Book");
        app.set_focus(1000);app.input(SCE_CTRL_CROSS);
        check(app.screen_==Screen::Library,"First-launch Home does not open Library");
        app.message_.clear();app.set_focus(200);app.input(SCE_CTRL_RIGHT);
        check(focus(app)==200,"Removed Search target is still focusable");
        app.input(SCE_CTRL_SQUARE);
        check(app.ime_active_ && app.ime_library_,"Square Search cannot be activated");
        app.ime_active_=false;app.input(SCE_CTRL_DOWN);
        check(focus(app)>=1000,"Search cannot enter the book row");
        app.input(SCE_CTRL_DOWN);
        check(focus(app)>=1000,"Down moved focus out of the book row");
        app.set_focus(200);app.input(SCE_CTRL_CROSS);
        check(app.add_book_menu_ && app.menu_.size()==2,"Add Book does not show two import choices");
        const auto selection=app.selected_;verify_graph(app);
        app.input(SCE_CTRL_DOWN);check(app.menu_selected_==1,"Add Book cannot navigate down");
        app.input(SCE_CTRL_CIRCLE);
        check(app.menu_.empty() && app.selected_==selection && focus(app)==200,"Popup cancel changed Library state");
        app.input(SCE_CTRL_CROSS);app.input(SCE_CTRL_CROSS);
        check(app.importing_,"Import from Device does not open import");
        verify_graph(app);app.input(SCE_CTRL_CIRCLE);
        check(!app.importing_ && app.screen_==Screen::Library && focus(app)==200,"Import cancel loses Library focus");
        app.input(SCE_CTRL_CROSS);app.input(SCE_CTRL_DOWN);app.input(SCE_CTRL_CROSS);
        check(app.screen_==Screen::Sources && app.menu_.empty(),"Browse Online does not open sources");
        app.input(SCE_CTRL_CIRCLE);app.message_.clear();app.set_focus(200);
        app.input(SCE_CTRL_CROSS);
        const auto add_row=app.menu_layout().row(0);
        tap.x=add_row.x+60;tap.y=add_row.y+17;app.gesture(tap);
        check(app.importing_,"Touch cannot import through Add Book popup");app.input(SCE_CTRL_CIRCLE);
        const auto old_filter=app.filter_;const auto old_sort=app.sort_;
        app.input(SCE_CTRL_TRIANGLE);verify_graph(app);
        check(app.menu_title_=="Sort and filter books","Triangle does not open Sort/Filter");
        app.input(SCE_CTRL_DOWN);app.input(SCE_CTRL_CIRCLE);
        check(app.filter_==old_filter && app.sort_==old_sort,"Cancel applied unconfirmed Sort/Filter");
        app.input(SCE_CTRL_TRIANGLE);app.set_focus(2004);app.input(SCE_CTRL_CROSS);
        check(app.filter_==reader::LibraryFilter::Reading && app.menu_.empty(),"Cross did not apply Library filter");
        app.input(SCE_CTRL_TRIANGLE);app.set_focus(2003);app.input(SCE_CTRL_CROSS);
        app.message_.clear();app.library_query_="no matching book";app.refresh_library("");
        verify_graph(app);app.set_focus(204);app.input(SCE_CTRL_CROSS);
        check(app.library_query_.empty() && !app.order_.empty(),"Empty search cannot be cleared with D-pad");

        app.entries_.clear();
        for(int i=0;i<9;++i) {
            reader::LibraryItem item;item.title="Book "+std::to_string(i);item.filename="book"+std::to_string(i)+".txt";
            item.path="fixture-book.txt";app.entries_.push_back(item);
        }
        app.selected_=0;app.refresh_library("");
        reader::capture_font_draws=true;reader::font_draws.clear();app.draw();reader::capture_font_draws=false;
        bool saw_count=false,saw_pages=false,saw_filter_hint=false;
        for(const auto& draw:reader::font_draws) {
            saw_count=saw_count || draw.text.find("9 books")!=std::string::npos;
            saw_pages=saw_pages || draw.text.find("Page ")!=std::string::npos;
            saw_filter_hint=saw_filter_hint || (draw.y>496 && draw.text=="Filter");
        }
        check(saw_count && !saw_pages && saw_filter_hint,"Library header contains pagination or misses count/hints");
        check(reader::ui::library_action(0).x+reader::ui::library_action(0).width==936,
              "Add Book is not aligned to the right header edge");
        const auto enter_query=[&](const std::u16string& query) {
            app.input(SCE_CTRL_SQUARE);
            check(app.ime_active_,"Reopened search has no keyboard");
            std::copy(query.begin(),query.end(),app.ime_input_);app.ime_input_[query.size()]=0;
            test_ime_status()=SCE_COMMON_DIALOG_STATUS_FINISHED;test_ime_button()=SCE_IME_DIALOG_BUTTON_ENTER;
            app.input(0);test_ime_status()=0;
            check(app.library_search_active_ && !app.ime_active_,"IME submission did not activate search");
        };
        app.sort_=reader::LibrarySort::Recent;
        app.entries_[0].progress.percent=30;app.entries_[1].progress.percent=40;
        app.entries_[0].progress.last_read=1;app.entries_[1].progress.last_read=2;
        app.filter_=reader::LibraryFilter::Reading;app.refresh_library("");
        enter_query(u"Book");
        check(app.order_.size()==2,"Search discarded the active filter");
        app.input(SCE_CTRL_RIGHT);app.input(SCE_CTRL_CIRCLE);
        check(!app.library_search_active_ && app.library_query_.empty() && app.order_.size()==2 && focus(app)==200,
              "Circle did not exit navigated results in one press");
        check(app.sort_==reader::LibrarySort::Recent && app.filter_==reader::LibraryFilter::Reading,
              "Search cancellation reset filter or sorting");
        app.filter_=reader::LibraryFilter::All;app.refresh_library("");
        for(const auto query:{u"missing",u"Boo",u""}) {
            enter_query(query);verify_graph(app);
            app.input(SCE_CTRL_CIRCLE);
            check(!app.library_search_active_ && app.library_query_.empty() && app.order_.size()==9 && focus(app)==200,
                  "Zero, partial or empty search did not clear in one press");
        }
        app.input(SCE_CTRL_SQUARE);app.input(SCE_CTRL_CIRCLE);
        check(app.ime_active_,"App intercepted keyboard cancellation");
        test_ime_status()=SCE_COMMON_DIALOG_STATUS_FINISHED;test_ime_button()=0;
        app.input(SCE_CTRL_CIRCLE);test_ime_status()=0;
        check(app.screen_==Screen::Library && !app.library_search_active_,"Keyboard cancel leaked into Library Back");
        enter_query(u"Book 5");app.input(SCE_CTRL_CROSS);
        check(app.screen_==Screen::Book && app.selected_filename()=="book5.txt","Search result opened the wrong book");
        app.input(SCE_CTRL_CIRCLE);app.input(SCE_CTRL_CIRCLE);
        check(app.screen_==Screen::Library && app.order_.size()==9,"Back from searched book cannot exit search");
        const auto search_actions=app.toolbar();const auto search_hit=app.footer_touch_rect(app.toolbar_rect(search_actions,2),search_actions[2]);
        tap.x=search_hit.x+search_hit.width/2;tap.y=520;app.gesture(tap);
        check(app.ime_active_,"Touch cannot open footer Search");
        test_ime_status()=SCE_COMMON_DIALOG_STATUS_FINISHED;test_ime_button()=0;app.input(0);test_ime_status()=0;
        enter_query(u"Book 5");
        reader::capture_font_draws=true;reader::font_draws.clear();app.draw();reader::capture_font_draws=false;
        for(const auto& draw:reader::font_draws)check(draw.text.find("Page 1 of 1")==std::string::npos,"Single result displays single-page indicator");
        check(app.toolbar()[1].label=="Clear search","Search footer did not update");
        app.set_focus(200);app.input(SCE_CTRL_CROSS);app.input(SCE_CTRL_CIRCLE);
        check(app.library_search_active_ && app.order_.size()==1,"Popup cancel changed search results");
        app.input(SCE_CTRL_CIRCLE);
        check(app.toolbar().size()==5 && app.toolbar()[1].label=="Back" && app.toolbar()[2].button==SCE_CTRL_SQUARE && app.toolbar()[3].button==SCE_CTRL_TRIANGLE,
              "Search exit did not restore normal Library hints");
        app.sort_=reader::LibrarySort::Title;app.selected_=0;app.refresh_library("");
        verify_graph(app);app.set_focus(1003);app.input(SCE_CTRL_RIGHT);
        check(app.selected_==3 && app.library_scroll_==0,"Right crossed the grid row boundary");
        app.input(SCE_CTRL_DOWN);
        check(app.selected_==7 && app.library_scroll_>0,"Down did not scroll to the next grid row");
        check(app.library_first()==0,"Scrolling discarded the partially visible previous row");
        verify_graph(app);app.input(SCE_CTRL_DOWN);
        check(app.selected_==8,"Partial final grid row is not reachable");verify_graph(app);
        app.input(SCE_CTRL_DOWN);check(app.selected_==8,"Down went beyond the final grid row");
        app.input(SCE_CTRL_UP);check(app.selected_==4,"Up did not preserve the grid column");
        app.input(SCE_CTRL_UP);check(app.selected_==0 && app.library_scroll_==0,"Up did not restore the grid top");
        tap.kind=reader::controls::GestureKind::Swipe;tap.x=300;tap.y=220;tap.dx=0;tap.dy=-100;
        app.gesture(tap);check(app.selected_==4 && app.library_scroll_>0,"Vertical swipe did not scroll Library");
        tap.dy=-100;app.gesture(tap);check(app.selected_==8,"Swipe cannot reach the final grid row");
        tap.kind=reader::controls::GestureKind::Tap;tap.x=300;tap.y=300;
        app.gesture(tap);check(app.screen_==Screen::Book && app.selected_==8,"Touch opened the wrong scrolled book");
        app.input(SCE_CTRL_CIRCLE);
        check(app.screen_==Screen::Library && app.selected_==8 && app.library_scroll_>0,"Book return lost grid scroll");
        app.set_focus(1008);app.input(SCE_CTRL_CROSS);
        check(app.screen_==Screen::Book,"Book cannot open details from the partial page");verify_graph(app);
        app.set_focus(200);app.input(SCE_CTRL_LEFT);check(focus(app)==200,"Details Read focus escaped its boundary");
        app.input(SCE_CTRL_CIRCLE);check(app.screen_==Screen::Library && app.selected_==8 && focus(app)==1008,"Details return lost page or focus");
        app.perform({"Settings",0,Command::Appearance});app.normalize_focus();
        const auto before=app.settings_;app.set_focus(1000);app.input(SCE_CTRL_LEFT);
        check(app.settings_.reading_font==0 && focus(app)==1000,"Typeface boundary unexpectedly moves focus");
        app.input(SCE_CTRL_RIGHT);
        check(app.settings_.reading_font==1,"Typeface row does not select Atkinson");
        app.input(SCE_CTRL_UP);check(focus(app)==1000,"Up entered the sidebar from Settings");
        verify_graph(app);app.input(SCE_CTRL_CIRCLE);
        check(app.screen_==Screen::Library && app.settings_.reading_font==before.reading_font,"Circle did not cancel Settings");
        app.perform({"Settings",0,Command::Appearance});app.set_focus(1000);app.input(SCE_CTRL_RIGHT);
        tap.x=60;tap.y=520;app.gesture(tap);
        check(app.screen_==Screen::Library,"Touch could not apply Settings through the footer");
        check(app.settings_.reading_font==1 && reader::load_settings(settings_path).reading_font==1,"Apply did not persist the reader font");
        app.show_more();verify_graph(app);app.input(SCE_CTRL_CIRCLE);
        check(app.menu_.empty() && focus(app)==1008,"Closing Options lost invoking focus");
        const auto popup_selected=app.selected_;const auto popup_scroll=app.library_scroll_;
        app.show_more();app.input(SCE_CTRL_DOWN);app.input(SCE_CTRL_DOWN);
        check(app.menu_selected_==2,"Popup Down is not vertical");
        app.input(SCE_CTRL_UP);check(app.menu_selected_==1,"Popup Up skipped an enabled action");
        app.set_focus(2002);app.input(SCE_CTRL_CROSS);
        check(app.menu_title_=="Sort and filter books" && app.menu_parents_.size()==1,"Sort submenu lost its parent");
        check(app.menu_option_active(Command::SortTitle)==(app.sort_==reader::LibrarySort::Title) &&
              app.menu_option_active(Command::FilterAll)==(app.filter_==reader::LibraryFilter::All),"Active choices are not marked");
        const auto back=app.menu_layout().back();
        tap.x=back.x+16;tap.y=back.y+16;app.gesture(tap);
        check(app.menu_title_=="Options" && app.menu_selected_==2,"Submenu arrow did not restore parent selection");
        check(app.menu_.size()==5 && app.menu_[0].command==Command::Details && app.menu_[1].command==Command::Search &&
              app.menu_[2].command==Command::Sort && app.menu_[3].command==Command::AddBook && app.menu_[4].command==Command::Exit,
              "Library Options order changed");
        app.set_focus(2003);app.input(SCE_CTRL_CROSS);
        check(app.menu_title_=="Add a new book" && app.menu_parents_.size()==1 && app.menu_.size()==2 &&
              app.menu_[0].command==Command::Import && app.menu_[1].command==Command::Online,
              "Library Add a new book did not open the import/online submenu");
        app.input(SCE_CTRL_CIRCLE);
        check(app.menu_title_=="Options" && app.menu_selected_==3,"Add book Back did not restore Library Options");
        tap.x=300;tap.y=200;app.gesture(tap);
        check(app.menu_.empty() && app.screen_==Screen::Library && app.selected_==popup_selected && app.library_scroll_==popup_scroll,
              "Outside dismissal activated a book or lost Library state");
        app.show_more();app.gesture({reader::controls::GestureKind::Swipe,300,180,0,-120,1,300,300});
        app.input(SCE_CTRL_TRIANGLE|SCE_CTRL_SQUARE);check(app.menu_title_=="Options" && !app.ime_active_,"Popup leaked shortcuts or a swipe");
        app.input(SCE_CTRL_START);check(app.menu_.empty(),"START toggle failed");
        app.screen_=Screen::Book;app.perform({"Remove",0,Command::RemoveBook});app.normalize_focus();
        check(focus(app)==2001,"Remove confirmation does not default to Keep book");
        app.input(SCE_CTRL_CROSS);check(app.menu_.empty(),"Keep book does not close confirmation");

        {
            std::ofstream fixture("fixture-book.txt");fixture<<"A story\n\n";
            for(int i=0;i<80;++i)fixture<<"The first page of a quiet adventure follows a winding path beyond the mountains.\n\n";
        }
        app.message_.clear();app.perform({"Chapters",0,Command::LocalContents});
        check(app.screen_==Screen::Contents && app.lines_.empty(),"Local chapter list loaded an unselected chapter");
        app.input(SCE_CTRL_CROSS);check(app.screen_==Screen::Reader && !app.lines_.empty(),"Selecting a local chapter did not open the reader");
        const auto active=reader::ui::recent_book(app.entries_);
        check(active<app.entries_.size(),"Opening a book did not populate Continue Reading");
        const auto same_second=static_cast<std::uint64_t>(std::time(nullptr))+60;
        for(auto& item:app.entries_)item.progress.last_read=same_second;
        app.progress_.last_read=same_second;
        app.entries_[active].chapters=99;
        app.publication_.title="A much longer book title that should wrap into two lines on the Home widget";
        app.save();
        check(reader::ui::recent_book(app.entries_)==active,"Tied or backward-clock timestamps left Home on an older book");
        check(app.entries_[active].chapters==app.publication_.chapters.size(),"Continue Reading retained stale chapter metadata");
        const auto saved=reader::load_progress(reader::progress_path(progress_directory,app.active_filename_));
        check(saved.last_read==app.progress_.last_read && saved.last_read>same_second,"Newest reading order did not persist");
        reader::font_draws.clear();reader::capture_font_draws=true;
        app.draw_home();reader::capture_font_draws=false;
        int title_lines=0;unsigned chapter_color=0,percent_color=0;
        for(const auto& draw:reader::font_draws) {
            if(draw.role==reader::FontRole::Heading && draw.pixels==25)++title_lines;
            if(draw.text.find("Chapter ")==0) {chapter_color=draw.color;check(draw.pixels==21,"Chapter text size differs from percentage");}
            if(draw.text.find('%')!=std::string::npos) {percent_color=draw.color;check(draw.pixels==21,"Percentage size changed");}
        }
        check(title_lines==2,"Home title did not wrap at the reduced font size");
        check(chapter_color && chapter_color==percent_color,"Chapter text should share percentage contrast");
        app.input(SCE_CTRL_CROSS);check(app.screen_==Screen::Contents && app.menu_.empty(),"Reader Cross does not open Contents");
        app.input(SCE_CTRL_CIRCLE);
        app.input(SCE_CTRL_START);check(!app.menu_.empty(),"Reader START does not open Reading controls");
        verify_graph(app);app.input(SCE_CTRL_CIRCLE);check(app.menu_.empty(),"Reader panel cannot close");
        app.perform({"Read aloud",0,Command::Speech});
        check(app.menu_title_=="Read aloud" && app.menu_[0].label=="Start here","Narration controls unavailable");
        verify_graph(app);
        const auto old_rate=app.settings_.speech_rate;
        app.perform({"Faster",0,Command::SpeechFaster});
        check(app.settings_.speech_rate==old_rate+25 && reader::load_settings(settings_path).speech_rate==old_rate+25,
              "Narration speed did not persist");
        app.perform({"Slower",0,Command::SpeechSlower});
        app.close_menu();
        {
            std::size_t narrated_samples = 0;
            class NarrationSink final : public reader::speech::Audio {
                std::size_t& count_;
            public:
                explicit NarrationSink(std::size_t& count) : count_(count) {}
                bool open(int rate) override { return rate == 16000; }
                bool write(const short*, std::size_t count) override { count_ += count; return true; }
                void close() override {}
            };
            App narrated(&pgf, [&] { return std::unique_ptr<reader::speech::Audio>(new NarrationSink(narrated_samples)); });
            narrated.screen_ = Screen::Reader; narrated.message_.clear();
            narrated.book_.text = "A quiet adventure. The rain has stopped.";
            narrated.publication_.title = "Narration completion";
            narrated.publication_.chapters.push_back({"Chapter 1", ""});
            narrated.reflow(0);
            for (int run = 0; run < 6; ++run) {
                narrated.show_speech(); narrated.input(SCE_CTRL_CROSS);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
                while (narrated.speech_.status().state != reader::speech::State::Finished &&
                       narrated.speech_.status().state != reader::speech::State::Error) {
                    check(std::chrono::steady_clock::now() < deadline, "Reader narration completion timed out");
                    narrated.draw(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                narrated.draw();
                check(narrated.speech_.status().state == reader::speech::State::Finished,
                      "Reader narration failed at completion");
                check(narrated.screen_ == Screen::Reader && narrated.menu_title_ == "Read aloud" &&
                      narrated.menu_[0].label == "Start here", "Narration EOF did not leave a usable reader/menu");
                narrated.input(SCE_CTRL_CIRCLE);
            }
            check(narrated_samples > 1000, "Reader completion test did not generate real SLT audio");
        }
        for(int i=0;i<10;++i)app.input(SCE_CTRL_DOWN);
        const auto offset=app.lines_[app.top_].offset;
        check(offset>0,"Reading-anchor fixture never left the first line");
        check(app.entries_[active].progress.offset==offset,"Reading movement did not update Home's cached progress");
        app.perform({"Settings",0,Command::Appearance});app.set_focus(1000);app.input(SCE_CTRL_LEFT);
        app.input(SCE_CTRL_CROSS);
        check(app.screen_==Screen::Reader && app.lines_[app.top_].offset<=offset &&
            (app.top_+1==app.lines_.size() || app.lines_[app.top_+1].offset>offset),"Reader font change lost reading anchor");
        app.message_="An operation failed";app.retry_available_=true;verify_graph(app);
        const auto controls=app.widgets();check(controls.size()==2,"Error modal exposes background controls");
        const auto dialog=app.feedback_box();
        check(dialog.width*9==dialog.height*16 && dialog.x+dialog.width/2==480 && dialog.y+dialog.height/2==272,"Feedback popup is not 16:9 and centered");
        for(const auto& control:controls)
            check(control.target.box.y==dialog.y+dialog.height-72,"Dialog actions are not along the bottom");
        app.set_focus(4001);app.input(SCE_CTRL_CROSS);check(app.message_.empty(),"Dismiss cannot be selected with D-pad");
        app.online_busy_=true;app.message_="Loading artwork...";
        check(app.widgets().size()==1 && app.widgets()[0].action.label=="Cancel","Loading popup has no cancel CTA");
        const auto cancel=app.feedback_action(false);
        app.gesture({reader::controls::GestureKind::Tap,float(cancel.x+cancel.width/2),float(cancel.y+28)});
        check(app.message_=="Canceling...","Loading Cancel touch does not cancel the worker");
        app.online_busy_=false;app.message_.clear();

        app.screen_=Screen::Contents;verify_graph(app);
        app.screen_=Screen::Illustrations;verify_graph(app);
        app.screen_=Screen::Bookmarks;verify_graph(app);
        app.screen_=Screen::About;verify_graph(app);
        app.screen_=Screen::Sources;verify_graph(app);
        app.screen_=Screen::Catalog;verify_graph(app);
        app.screen_=Screen::OnlineChapters;verify_graph(app);
        app.screen_=Screen::Novel;verify_graph(app);
        app.book_.illustrations.push_back({0,"Test image","test.png",0,100,160});
        app.screen_=Screen::Image;app.show_more();verify_graph(app);

        // Use the real library index, TXT path and progress files rather than
        // synthetic in-memory entries. Home must survive a scan and restart.
        const std::string txt="continue_story.TXT";
        {
            std::ofstream fixture(books_directory+'/'+txt);
            fixture<<"A TXT reading fixture\n\n";
            for(int i=0;i<100;++i)fixture<<"The reader returns to this story after closing and reopening the application.\n\n";
        }
        check(reader::save_library(library_path,{txt}),"Cannot prepare TXT library index");
        check(reader::save_progress(reader::progress_path(progress_directory,txt),{}),"Cannot reset TXT history");
        std::size_t txt_offset=0;
        {
            App fresh(&pgf);fresh.message_.clear();
            fresh.input(SCE_CTRL_RTRIGGER);fresh.set_focus(1000);fresh.input(SCE_CTRL_CROSS);
            check(fresh.screen_==Screen::Book,"Imported TXT did not open details");
            fresh.set_focus(200);fresh.input(SCE_CTRL_CROSS);
            check(fresh.screen_==Screen::Reader,"Imported TXT did not open for reading");
            fresh.message_.clear();for(int i=0;i<10;++i)fresh.input(SCE_CTRL_DOWN);
            txt_offset=fresh.progress_.offset;
            check(txt_offset>0,"TXT progress never advanced");
            fresh.input(SCE_CTRL_CIRCLE);fresh.input(SCE_CTRL_LTRIGGER);
            check(fresh.screen_==Screen::Home,"TXT test did not return Home");
            const auto recent=reader::ui::recent_book(fresh.entries_);
            check(recent<fresh.entries_.size() && fresh.entries_[recent].filename==txt &&
                fresh.entries_[recent].progress.offset==txt_offset,"TXT did not update Continue Reading");
        }
        {
            App restarted(&pgf);restarted.message_.clear();
            const auto recent=reader::ui::recent_book(restarted.entries_);
            check(recent<restarted.entries_.size() && restarted.entries_[recent].filename==txt,
                "TXT disappeared from Continue Reading after restart");
            restarted.input(SCE_CTRL_CROSS);
            check(restarted.screen_==Screen::Reader && restarted.progress_.offset==txt_offset,
                "Continue Reading did not resume TXT at the saved position");
        }
        {
            // Old TXT saves have positions but no last-read timestamp.
            std::ofstream legacy(reader::progress_path(progress_directory,txt));
            legacy<<"NRP1\n"<<txt_offset<<" 0 0\n";legacy.close();
            App migrated(&pgf);migrated.message_.clear();
            const auto recent=reader::ui::recent_book(migrated.entries_);
            check(recent<migrated.entries_.size() && migrated.entries_[recent].filename==txt,
                "Legacy TXT history is ignored without a timestamp");
            migrated.input(SCE_CTRL_CROSS);
            check(migrated.screen_==Screen::Reader && migrated.progress_.offset==txt_offset,
                "Legacy TXT did not resume through the Home widget");
        }
        {
            App guide(&pgf);guide.message_.clear();guide.navigate(1);
            guide.filter_=reader::LibraryFilter::All;guide.library_query_.clear();guide.refresh_library("@welcome");
            guide.open();check(guide.screen_==Screen::Reader,"Welcome TXT fixture did not open");
            guide.input(SCE_CTRL_CIRCLE);guide.navigate(0);
            const auto recent=reader::ui::recent_book(guide.entries_);
            check(recent<guide.entries_.size() && guide.entries_[recent].guide,"Read welcome TXT was excluded from Home");
            guide.input(SCE_CTRL_CROSS);check(guide.screen_==Screen::Reader && guide.active_filename_=="@welcome",
                "Continue Reading cannot reopen the welcome TXT");
        }
        control_regressions();cache_regressions();
    }
};
}
int main(int argc,char** argv){try{
#ifdef NOVELREADER_PROFILE
    if(argc>1 && std::string(argv[1])=="--benchmark"){FrontendTest::benchmark();return 0;}
#else
    (void)argc;(void)argv;
#endif
    FrontendTest::run();std::cout<<"Native frontend navigation tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
