// Exercise the native screen registry, input dispatch and return state. Only
// platform drawing, keyboard and directory calls are mocked; reader/layout,
// settings, library and online service logic are the normal host implementations.
#define NOVELREADER_FRONTEND_TESTS
#include "../src/main.cpp"
#include <fstream>
#include <iostream>

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
    static int focus(App& app){app.normalize_focus();return app.focus_memory_[app.focus_key()];}
    static void verify_graph(App& app) {
        const auto controls=app.widgets();
        std::vector<reader::ui::FocusTarget> targets;
        for(const auto& control:controls) {
            const auto& b=control.target.box;
            check(control.kind!=App::WidgetKind::Sidebar,"Sidebar entered the focus registry");
            check(b.y<reader::ui::toolbar_top,"Footer entered the focus registry");
            check(b.height>=56,"Native widget is below the minimum target size");
            check(b.width>=56,"Native widget is too narrow");
            check(b.x>=0 && b.x+b.width<=960 && b.y>=44 && b.y+b.height<=544,"Native widget is off screen");
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
        vita2d_pgf pgf;App app(&pgf);
        app.message_.clear();app.sidebar_focus_=false;app.focus_memory_.clear();
        verify_graph(app);
        for(const auto key:{SCE_CTRL_LEFT,SCE_CTRL_RIGHT,SCE_CTRL_UP,SCE_CTRL_DOWN})for(int i=0;i<8;++i)app.input(key);
        check(app.screen_==Screen::Home && app.nav_active_==0 && focus(app)==1000,"D-pad left Home content");
        reader::controls::Gesture tap;tap.kind=reader::controls::GestureKind::Tap;tap.x=60;tap.y=170;
        app.gesture(tap);check(app.screen_==Screen::Home,"Touch activated the sidebar");
        tap.y=520;app.gesture(tap);check(app.screen_==Screen::Home,"Touch activated the footer");
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
        app.focus_memory_[app.focus_key()]=304;
        check(focus(app)==1000,"Stale footer focus was not restored to content");
        app.focus_memory_[app.focus_key()]=100;
        check(focus(app)==1000,"Stale sidebar focus was not restored to content");
        for(const auto& control:app.widgets())check(control.action.command!=Command::Import,"Home still has Add Book");
        app.set_focus(1000);app.input(SCE_CTRL_CROSS);
        check(app.screen_==Screen::Library,"First-launch Home does not open Library");
        app.message_.clear();app.set_focus(200);app.input(SCE_CTRL_RIGHT);
        check(focus(app)==201,"Add Book does not lead to Search");
        app.input(SCE_CTRL_CROSS);
        check(app.ime_active_ && app.ime_library_,"Search cannot be activated by D-pad");
        app.ime_active_=false;app.input(SCE_CTRL_DOWN);
        check(focus(app)>=1000,"Search cannot enter the book row");
        app.input(SCE_CTRL_DOWN);
        check(focus(app)>=1000,"Down moved focus out of the book row");
        app.set_focus(200);app.input(SCE_CTRL_CROSS);
        check(app.importing_,"Library Add Book does not open import");
        verify_graph(app);app.input(SCE_CTRL_CIRCLE);
        check(!app.importing_ && app.screen_==Screen::Library && focus(app)==200,"Import cancel loses Library focus");
        app.message_.clear();app.library_query_="no matching book";app.refresh_library("");
        verify_graph(app);app.set_focus(204);app.input(SCE_CTRL_CROSS);
        check(app.library_query_.empty() && !app.order_.empty(),"Empty search cannot be cleared with D-pad");

        app.entries_.clear();
        for(int i=0;i<9;++i) {
            reader::LibraryItem item;item.title="Book "+std::to_string(i);item.filename="book"+std::to_string(i)+".txt";
            item.path="fixture-book.txt";app.entries_.push_back(item);
        }
        app.selected_=0;app.refresh_library("");
        verify_graph(app);app.set_focus(1003);app.input(SCE_CTRL_RIGHT);
        check(app.selected_==7,"Right at the grid edge does not change Library page");
        verify_graph(app);app.input(SCE_CTRL_RIGHT);
        check(app.selected_==8,"Partial final page is not reachable");verify_graph(app);
        app.set_focus(1008);app.input(SCE_CTRL_CROSS);
        check(app.screen_==Screen::Book,"Book cannot open details from the partial page");verify_graph(app);
        app.set_focus(201);app.input(SCE_CTRL_LEFT);check(focus(app)==200,"Bookmark cannot reach Read");
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
        check(app.screen_==Screen::Settings,"Touch applied Settings through the footer");
        app.input(SCE_CTRL_SQUARE);
        check(app.settings_.reading_font==1 && reader::load_settings(settings_path).reading_font==1,"Apply did not persist the reader font");
        app.show_more();verify_graph(app);app.input(SCE_CTRL_CIRCLE);
        check(app.menu_.empty() && focus(app)==1008,"Closing Options lost invoking focus");
        app.screen_=Screen::Book;app.perform({"Remove",0,Command::RemoveBook});app.normalize_focus();
        check(focus(app)==2001,"Remove confirmation does not default to Keep book");
        app.input(SCE_CTRL_CROSS);check(app.menu_.empty(),"Keep book does not close confirmation");

        {
            std::ofstream fixture("fixture-book.txt");fixture<<"A story\n\n";
            for(int i=0;i<80;++i)fixture<<"The first page of a quiet adventure follows a winding path beyond the mountains.\n\n";
        }
        app.message_.clear();app.open();check(app.screen_==Screen::Reader,"Fixture book did not open");
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
        app.input(SCE_CTRL_CROSS);check(!app.menu_.empty(),"Reader Cross does not open Reading controls");
        verify_graph(app);app.input(SCE_CTRL_CIRCLE);check(app.menu_.empty(),"Reader panel cannot close");
        for(int i=0;i<10;++i)app.input(SCE_CTRL_DOWN);
        const auto offset=app.lines_[app.top_].offset;
        check(offset>0,"Reading-anchor fixture never left the first line");
        check(app.entries_[active].progress.offset==offset,"Reading movement did not update Home's cached progress");
        app.perform({"Settings",0,Command::Appearance});app.set_focus(1000);app.input(SCE_CTRL_LEFT);
        app.input(SCE_CTRL_SQUARE);
        check(app.screen_==Screen::Reader && app.lines_[app.top_].offset<=offset &&
            (app.top_+1==app.lines_.size() || app.lines_[app.top_+1].offset>offset),"Reader font change lost reading anchor");
        app.message_="An operation failed";app.retry_available_=true;verify_graph(app);
        const auto controls=app.widgets();check(controls.size()==2,"Error modal exposes background controls");
        app.set_focus(4001);app.input(SCE_CTRL_CROSS);check(app.message_.empty(),"Dismiss cannot be selected with D-pad");

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
    }
};
}
int main(){try{FrontendTest::run();std::cout<<"Native frontend navigation tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
