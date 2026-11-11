#include "online.hpp"
#include "../src/online_html.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace reader;
using namespace reader::online;
namespace {
const std::string assets=NOVELREADER_ASSETS;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
std::string scratch() {
    char name[]="./online-tests-XXXXXX";
    char* result=mkdtemp(name); if(!result) throw std::runtime_error("Cannot create test directory."); return result;
}
Result parse(const std::string& source,const std::string& op,const std::string& html,const std::string& url="",const std::string& query="") {
    std::atomic<bool> cancel{false};
    return run_adapter(assets,source,op,url,query,[&](const std::string&){return html;},cancel);
}
Update finish(Service& service) {
    for(unsigned i=0;i<12000;++i) { auto u=service.poll(); if(u.done) return u; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    throw std::runtime_error("Worker did not finish.");
}
void parsers() {
    check(html_text("Chapter <!-- -->1")=="Chapter 1","HTML comments are not visible text");
    check(html_text("<p>A &amp; B &#x1F600;</p><script>ignore <a>evil</a></script><style>hide</style><p>C<br>D</p>")=="A & B 😀\nC\nD","HTML text and raw elements");
    auto nodes=html_select("<div data-x='a > b'><div>inside</div>outside</div>","div","data-x","a > b");
    check(nodes.size()==1 && nodes[0].text=="inside\noutside","Nested HTML and quoted tag delimiter");
    auto n=parse("novgo","chapters","<h1>Test novel</h1><meta property='og:novel:author' content='Author'><a href='/book/last.html'>Latest</a><div id='list-chapter'><ul><li><a title='Chapter 1' href='/book/1.html'>one</a></li></ul><a rel='next' href='/book.html?page=2'>Next</a></div>","https://novgo.net/book.html");
    check(n.items.size()==1 && n.items[0].title=="Chapter 1" && n.author=="Author" && n.next=="https://novgo.net/book.html?page=2","Novgo chapter scope and pagination");
    auto a=parse("asura","chapters","<h1>Novel</h1><a href='/novels/book/chapter/2'>Last</a><astro-island component-url='/NovelChapterList.hash.js'><a href='/novels/book/chapter/2'><span title='Locked chapter'>Chapter 2</span></a><a href='/novels/book/chapter/1'>Chapter 1</a></astro-island>","https://asurascans.com/novels/book");
    check(a.items.size()==2 && a.items[0].url.back()=='1' && a.items[1].locked,"Asura ordering and locked chapters");
    check(parse("asura","chapter","<p>Chrome</p><p data-para='1'>Story &amp; text</p><p data-para='2'>More.</p><p>Comments</p>","https://asurascans.com/novels/book/chapter/1").text=="Story & text\n\nMore.","Asura excludes reader chrome");
    auto search=parse("asura","browse","{\"data\":[{\"title\":\"A \\uD83D\\uDE00\",\"slug\":\"book\"}]}","","test");
    check(search.items.size()==1 && search.items[0].title=="A 😀","Asura JSON search");
    auto b=parse("beast","chapters","<h1>Novel</h1><div id='chapterList'><a data-premium='1' href='/series/book/2'>Chapter 2</a><a data-premium='0' href='/series/book/1'>Chapter 1</a></div>","https://beastnovels.com/series/book");
    check(b.items.size()==2 && b.items[1].locked,"Beast premium detection");
    check(parse("beast","chapter","<div class='reader'><div class='chapter-content'><p>Story</p><div>Another paragraph</div></div><p>Comments</p></div>","https://beastnovels.com/series/book/1").text=="Story\nAnother paragraph","Beast nested content");
    check(parse("novgo","chapter","<div id='chapter-content'><p>Words</p><p>Next</p></div>","https://novgo.net/book/1.html").text=="Words\nNext","Novgo text");
    check(parse("beast","browse","[{\"name\":\"Book\",\"url\":\"https:\\/\\/beastnovels.com\\/series\\/book\"}]","","book").items.size()==1,"Beast JSON search");
    for(const auto& s:sources()) {
        bool failed=false;
        try { parse(s.id,"chapter","<html><h1>Login required</h1></html>",s.home+"/missing"); } catch(...) { failed=true; }
        check(failed,"Missing chapter content must fail rather than cache login HTML");
        check(!allowed_url(s.id,s.home+".evil.test/path") && !allowed_url(s.id,"file:///etc/passwd") && !allowed_url(s.id,s.home+"/\r\nInjected"),"Host and scheme validation");
    }
    check(allowed_url("asura","https://api.asurascans.com/api/novel-series?search=a"),"Asura public API allowed");
    bool failed=false;
    try { parse("beast","browse","[{\"name\":\"Book\",\"url\":\"https://evil.test/book\"}]","","book"); } catch(...) { failed=true; }
    check(failed,"Remote URLs cannot escape source");
}
void updates() {
    std::atomic<bool> canceled{false};
    const auto json=[](const std::string& tag){return "{\"tag_name\":\""+tag+"\",\"draft\":false,\"prerelease\":false}";};
    check(release_info(assets,json("v0.8.0"),"0.7.0",canceled).text.find("Update available")!=std::string::npos,"New release was not detected");
    check(release_info(assets,json("v0.10"),"0.9.0",canceled).text.find("Update available")!=std::string::npos,"Version comparison is lexical");
    for(const auto* tag:{"v0.7","0.7.0","v0.6.99"})
        check(release_info(assets,json(tag),"0.7.0",canceled).text.find("up to date")!=std::string::npos,"Equal or older release offered as update");
    for(const auto* data:{"{}","[]","not JSON","{\"tag_name\":\"v0.8\",\"draft\":false,\"prerelease\":true}",
                         "{\"tag_name\":\"v0.8\",\"draft\":true,\"prerelease\":false}"}) {
        bool failed=false;try{release_info(assets,data,"0.7.0",canceled);}catch(...){failed=true;}
        check(failed,"Malformed or unpublished release accepted");
    }
    for(const auto* tag:{"latest","v0.8.0-beta","v1.2.3.4","v9999999.0","v1..2","v1.2x"}) {
        bool failed=false;try{release_info(assets,json(tag),"0.7.0",canceled);}catch(...){failed=true;}
        check(failed,"Unsupported version accepted");
    }
    const auto dir=scratch();unsigned calls=0;
    Service service(assets,dir,[&](const std::string& url){check(url==latest_release_url,"Update URL is not fixed");++calls;return json("v9.0.0");});
    Request request;request.action=Action::CheckUpdates;request.url="https://evil.test/";
    check(service.start(request),"Update worker did not start");
    auto result=finish(service);
    check(result.error.empty() && result.request.action==Action::CheckUpdates && calls==1 &&
          result.result.text.find("Update available")!=std::string::npos && result.manifest.empty(),"Update worker changed novel state or lost its result");
    Service failure(assets,dir,[](const std::string&)->std::string{throw std::runtime_error("Offline test");});
    check(failure.start(request),"Offline update worker did not start");result=finish(failure);
    check(result.error=="Offline test" && result.request.action==Action::CheckUpdates,"Update errors are not surfaced");
    std::atomic<bool> entered{false},release{false};
    Service cancel(assets,dir,[&](const std::string&){entered=true;while(!release)std::this_thread::yield();return json("v9.0.0");});
    check(cancel.start(request),"Cancelable update worker did not start");
    while(!entered)std::this_thread::yield();
    cancel.cancel();release=true;result=finish(cancel);
    check(!result.error.empty() && result.result.text.empty(),"Canceled update check reported success");
}
void persistence() {
    const auto dir=scratch(); mkdir((dir+"/novgo").c_str(),0777);
    Novel n{"novgo","A \"novel\"","Author","https://novgo.net/book.html","Description",{{"One","https://novgo.net/book/one.html",false},{"Two","https://novgo.net/book/two.html",false}}};
    const auto path=novel_path(dir,n);
    check(save_novel(path,n),"Save manifest");
    Novel loaded; std::string error;
    check(load_novel(path,loaded,error) && loaded.title==n.title && loaded.chapters.size()==2,"Manifest round trip");
    check(loaded.cover.empty(),"Legacy manifests must work without cover metadata");
    n.cover="https://cdn.example.org/cover.png";
    check(save_novel(path,n) && load_novel(path,loaded,error) && loaded.cover==n.cover,"Cover metadata round trip");
    Publication p; check(open_publication(path,p,error) && p.online,"Offline publication");
    Progress progress; progress.chapter=1; progress.offset=12; progress.has_bookmark=true; progress.bookmark_chapter=0; progress.bookmark=3;
    progress.chapter_id=p.chapters[1].path; progress.bookmark_id=p.chapters[0].path;
    check(save_progress(dir+"/save",progress),"Save online progress");
    progress=load_progress(dir+"/save");
    std::swap(p.chapters[0],p.chapters[1]); restore_progress(p,progress);
    check(progress.chapter==0 && progress.offset==12 && progress.bookmark_chapter==1 && progress.has_bookmark,"Progress follows stable chapter identity after reordering");
    p.chapters.pop_back(); restore_progress(p,progress); check(!progress.has_bookmark,"Removed bookmarks disabled");
    check(atomic_write(path,"NRONLINE1\n\"novgo\" \"https://evil.test/\"\n\"Title\" \"\" \"\" 1\n\"Chapter\" \"file:///etc/passwd\" 0\n"),"Write malformed manifest");
    check(!load_novel(path,loaded,error),"Untrusted manifest URLs rejected");
}
void artwork() {
    const auto png=read_file(assets+"/ui/icon0.png",max_response_bytes);
    const auto dir=scratch();const std::string image_url="https://cdn.example.org/cover.png";
    const std::string html="<h1>Artwork fixture</h1><meta property='og:image' content='"+image_url+"'><div id='list-chapter'><a href='/art/1.html'>One</a></div>";
    check(parse("novgo","chapters",html,"https://novgo.net/art.html").cover==image_url,"Novgo artwork extraction");
    check(parse("asura","chapters","<h1>A</h1><meta property='og:image' content='/cover.jpg'><astro-island component-url='NovelChapterList'><a href='/novels/a/chapter/1'>One</a></astro-island>","https://asurascans.com/novels/a").cover=="https://asurascans.com/cover.jpg","Asura relative artwork");
    check(parse("beast","chapters","<h1>B</h1><meta name='twitter:image' content='//cdn.example.org/b.png'><div id='chapterList'><a href='/series/b/1'>One</a></div>","https://beastnovels.com/series/b").cover=="https://cdn.example.org/b.png","Beast artwork extraction");
    for(const auto* url:{"file:///cover.png","http://cdn.example.org/a.png","https://127.0.0.1/a","https://user@cdn.example.org/a","https://cdn.example.org/\nheader"})
        check(!allowed_cover_url(url),"Unsafe image URL accepted");
    bool fail_art=false;unsigned images=0;
    Service service(assets,dir,[&](const std::string& url) {
        if(url==image_url){++images;if(fail_art)throw std::runtime_error("Image unavailable");return png;}
        return html;
    });
    Request request;request.source="novgo";request.url="https://novgo.net/art.html";request.action=Action::Chapters;
    check(service.start(request),"Start artwork metadata load");auto result=finish(service);
    check(result.error.empty() && images==1 && result.novel.cover==image_url,"Artwork download failed");
    Publication publication;std::string error,bytes;
    check(open_publication(result.manifest,publication,error) && publication.cover_path==cover_path(result.manifest),"Library metadata lost the cached cover");
    check(load_cover(result.manifest,bytes,error) && bytes==png,"Offline artwork cache bytes changed");
    fail_art=true;check(service.start(request),"Retry optional artwork");result=finish(service);
    check(result.error.empty() && load_cover(result.manifest,bytes,error) && bytes==png,"Failed artwork refresh broke the novel or its saved cover");
    request.url="https://novgo.net/no-cover.html";check(service.start(request),"Start novel with unavailable artwork");result=finish(service);
    check(result.error.empty() && open_publication(result.manifest,publication,error) && publication.cover_path.empty(),"Unavailable artwork must preserve readable novel metadata");
}
void adapter_limits() {
    const auto dir=scratch(); mkdir((dir+"/sources").c_str(),0777);
    for(const auto* name:{"common","json"}) check(atomic_write(dir+"/sources/"+name+".lua",read_file(assets+"/sources/"+name+".lua",128*1024)),"Prepare adapter fixture");
    std::atomic<bool> cancel{false};
    const Fetch fetch=[](const std::string&){return std::string();};
    check(atomic_write(dir+"/sources/novgo.lua","assert(not os and not io and not package and not debug and not load and not dofile); return function() return {} end"),"Prepare sandbox fixture");
    run_adapter(dir,"novgo","browse","","",fetch,cancel);
    for(const auto* script:{"return function() while true do end end","return function() return {text=string.rep('x',32*1024*1024)} end"}) {
        check(atomic_write(dir+"/sources/novgo.lua",script),"Prepare adapter limit fixture");
        bool failed=false;
        try { run_adapter(dir,"novgo","browse","","",fetch,cancel); } catch(...) { failed=true; }
        check(failed,"Runaway and oversized adapters must fail safely");
    }
}
void worker() {
    const auto dir=scratch();
    Novel n{"novgo","Test","","https://novgo.net/book.html","",{{"One","https://novgo.net/book/one.html",false},{"Two","https://novgo.net/book/two.html",false},{"Locked","https://novgo.net/book/three.html",true}}};
    std::atomic<unsigned> requests{0}; bool fail_second=true;
    Service service(assets,dir,[&](const std::string& url) {
        ++requests;
        if(url==n.chapters[1].url && fail_second) throw std::runtime_error("Interrupted transfer");
        return std::string("<div id='chapter-content'><p>Cached chapter text.</p></div>");
    });
    Request request; request.action=Action::Download; request.source=n.source; request.novel=n; request.all=true;
    check(service.start(request),"Start background download"); check(!service.start(request),"Reject overlapping jobs");
    auto result=finish(service); const auto manifest=novel_path(dir,n);
    check(!result.error.empty() && cached(manifest,n.chapters[0].url) && !cached(manifest,n.chapters[1].url),"Interrupted download preserves completed chapters");
    fail_second=false; check(service.start(request),"Retry download"); result=finish(service);
    check(result.error.empty() && requests==3 && cached(manifest,n.chapters[1].url) && !cached(manifest,n.chapters[2].url),"Retry skips saved chapters and excludes locked chapters");
    Publication p; Book b; std::string error;
    check(open_publication(manifest,p,error) && load_chapter(p,1,b,error) && b.text=="Cached chapter text.","Download opens through existing offline reader");
    request.action=Action::Read; request.all=false; request.chapter=1;
    check(service.start(request),"Start cached read"); result=finish(service);
    check(result.error.empty() && requests==3,"Offline read makes no HTTP request");
    request.action=Action::Download; request.all=true; check(service.start(request),"Start cancelable download"); service.cancel(); result=finish(service);
    check(!result.error.empty() && cached(manifest,n.chapters[0].url),"Cancellation retains completed chapters");
}
}
int main(int argc,char** argv) {
    try {
        if(argc==5 && std::string(argv[1])=="--sample") {
            std::string url;
            for(const auto& s:sources()) if(s.id==argv[2]) url=s.home+"/sample";
            auto r=parse(argv[2],argv[3],read_file(argv[4],max_response_bytes),url);
            unsigned locked=0; for(const auto& i:r.items) if(i.locked) ++locked;
            std::cout<<"items="<<r.items.size()<<" locked="<<locked<<" text_bytes="<<r.text.size()<<" next="<<r.next<<'\n'; return 0;
        }
        if(argc==5 && std::string(argv[1])=="--live-read") {
            Service service(assets,scratch()); Request r; r.source=argv[2]; r.action=Action::Read;
            r.novel={r.source,"Live test","",argv[3],"",{{"Chapter",argv[4],false}}};
            check(service.start(r),"Start live read"); const auto result=finish(service);
            if(!result.error.empty()) throw std::runtime_error(result.error);
            std::cout<<"Verified HTTPS download and cache: "<<read_file(chapter_path(result.manifest,argv[4]),max_chapter_bytes).size()<<" text bytes\n"; return 0;
        }
        parsers(); updates(); persistence(); artwork(); adapter_limits(); worker(); std::cout<<"Online tests passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
