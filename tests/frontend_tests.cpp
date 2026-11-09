// Exercise the native screen registry, input dispatch and return state. Only
// platform drawing, keyboard and directory calls are mocked; reader/layout,
// settings, library and online service logic are the normal host implementations.
#define NOVELREADER_FRONTEND_TESTS
#include "../src/main.cpp"
#include <fstream>
#include <iostream>

namespace reader {
struct VitaFonts::Impl {};
VitaFonts::VitaFonts(vita2d_pgf*):impl_(new Impl){}
VitaFonts::~VitaFonts()=default;
int VitaFonts::width(FontRole role,unsigned pixels,const std::string& text)const{return text.size()*pixels/(role==FontRole::Sans?3:2);}
void VitaFonts::draw(FontRole,unsigned,int,int,unsigned,const std::string&)const{}
bool VitaFonts::complete()const{return true;}
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
        vita2d_pgf pgf;App app(&pgf);
        app.message_.clear();app.sidebar_focus_=false;app.focus_memory_.clear();
        verify_graph(app);
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
        check(focus(app)>=300 && focus(app)<305,"Book row does not reach the footer");
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
        verify_graph(app);app.set_focus(303);app.input(SCE_CTRL_CROSS);
        check(app.selected_==4,"Next footer action does not change Library page");
        verify_graph(app);app.input(SCE_CTRL_CROSS);
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
        verify_graph(app);app.input(SCE_CTRL_CIRCLE);
        check(app.screen_==Screen::Library && app.settings_.reading_font==before.reading_font,"Circle did not cancel Settings");
        app.perform({"Settings",0,Command::Appearance});app.set_focus(1000);app.input(SCE_CTRL_RIGHT);
        app.set_focus(300);app.input(SCE_CTRL_CROSS);
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
        app.input(SCE_CTRL_CROSS);check(!app.menu_.empty(),"Reader Cross does not open Reading controls");
        verify_graph(app);app.input(SCE_CTRL_CIRCLE);check(app.menu_.empty(),"Reader panel cannot close");
        for(int i=0;i<10;++i)app.input(SCE_CTRL_DOWN);
        const auto offset=app.lines_[app.top_].offset;
        check(offset>0,"Reading-anchor fixture never left the first line");
        app.perform({"Settings",0,Command::Appearance});app.set_focus(1000);app.input(SCE_CTRL_LEFT);
        app.set_focus(300);app.input(SCE_CTRL_CROSS);
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
    }
};
}
int main(){try{FrontendTest::run();std::cout<<"Native frontend navigation tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
