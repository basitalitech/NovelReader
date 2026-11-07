#include "online.hpp"
#include "online_html.hpp"
#include "lua_cpp.hpp"
#include <cstdlib>
#include <stdexcept>

namespace reader { namespace online {
namespace {
struct Context {
    const Fetch& fetch;
    const std::atomic<bool>& cancel;
    std::string source;
    std::size_t memory = 0, instructions = 0, requests = 0;
};
void* allocate(void* ud, void* p, std::size_t old, std::size_t n) {
    auto& c = *static_cast<Context*>(ud);
    if (!p) old = 0;
    if (!n) { c.memory -= old; std::free(p); return nullptr; }
    if (n > 24 * 1024 * 1024 - (c.memory - old)) return nullptr;
    auto* result = std::realloc(p,n); if (result) c.memory = c.memory - old + n;
    return result;
}
Context& context(lua_State* L) { return **static_cast<Context**>(lua_getextraspace(L)); }
void hook(lua_State* L, lua_Debug*) {
    auto& c = context(L); c.instructions += 10000;
    if (c.cancel || c.instructions > 30000000) luaL_error(L, "Canceled or adapter execution limit reached.");
}
int fetch(lua_State* L) {
    auto& c=context(L);
    bool ok=false;
    {
        std::string data;
        try {
            const auto* url=luaL_checkstring(L,1);
            if(c.cancel) throw std::runtime_error("Canceled.");
            if(!allowed_url(c.source,url) || ++c.requests>2) throw std::runtime_error("Unsupported adapter request.");
            data=c.fetch(url);
            if(data.size()>max_response_bytes) throw std::runtime_error("Response exceeds 4 MiB.");
            ok=true;
        } catch(const std::exception& e) { data=e.what(); }
        if(!ok) lua_pushnil(L);
        lua_pushlstring(L,data.data(),data.size());
    }
    return ok?1:2;
}
int select(lua_State* L) {
    const char* h=luaL_checkstring(L,1), *tag=luaL_optstring(L,2,""), *attr=luaL_optstring(L,3,""), *val=luaL_optstring(L,4,"");
    bool ok=false;
    {
        std::string failure;
        try {
            const auto nodes=html_select(h,tag,attr,val);
            lua_createtable(L,static_cast<int>(nodes.size()),0);
            int i=0;
            for(const auto& n:nodes) {
                lua_createtable(L,0,3);
                lua_pushlstring(L,n.html.data(),n.html.size()); lua_setfield(L,-2,"html");
                lua_pushlstring(L,n.text.data(),n.text.size()); lua_setfield(L,-2,"text");
                lua_createtable(L,0,static_cast<int>(n.attrs.size()));
                for(const auto& a:n.attrs) { lua_pushlstring(L,a.second.data(),a.second.size()); lua_setfield(L,-2,a.first.c_str()); }
                lua_setfield(L,-2,"attrs"); lua_rawseti(L,-2,++i);
            }
            ok=true;
        } catch(const std::exception& e) { failure=e.what(); }
        if(!ok) { lua_pushnil(L); lua_pushlstring(L,failure.data(),failure.size()); }
    }
    return ok?1:2;
}
std::string field(lua_State* L, const char* name, std::size_t limit=4096) {
    lua_getfield(L,-1,name); std::size_t size=0;
    const char* value=lua_tolstring(L,-1,&size);
    if(size>limit) throw std::runtime_error("Adapter field exceeds limit.");
    std::string result=value?std::string(value,size):""; lua_pop(L,1);
    if(!valid_utf8(result) || result.find('\0')!=std::string::npos) throw std::runtime_error("Adapter returned invalid UTF-8.");
    return result;
}
void execute(lua_State* L,const std::string& script,const std::string& name) {
    if(luaL_loadbufferx(L,script.data(),script.size(),name.c_str(),"t") || lua_pcall(L,0,1,0)) {
        const auto* e=lua_tostring(L,-1); throw std::runtime_error(e?e:"Lua adapter failed.");
    }
}
}
Result run_adapter(const std::string& assets,const std::string& source,const std::string& operation,const std::string& url,const std::string& query,const Fetch& fetcher,const std::atomic<bool>& cancel) {
    bool known=false; for(const auto& s:sources()) if(s.id==source) known=true;
    if(!known || query.size()>256 || (!url.empty() && !allowed_url(source,url))) throw std::runtime_error("Unsupported source request.");
    Context c{fetcher,cancel,source};
    std::unique_ptr<lua_State,decltype(&lua_close)> state(lua_newstate(allocate,&c),lua_close);
    if(!state) throw std::runtime_error("Not enough memory for source adapter.");
    auto* L=state.get(); *static_cast<Context**>(lua_getextraspace(L))=&c;
    const struct { const char* name; lua_CFunction fn; } libraries[]={{"_G",luaopen_base},{LUA_TABLIBNAME,luaopen_table},{LUA_STRLIBNAME,luaopen_string},{LUA_MATHLIBNAME,luaopen_math},{LUA_UTF8LIBNAME,luaopen_utf8}};
    for(const auto& lib:libraries) { luaL_requiref(L,lib.name,lib.fn,1); lua_pop(L,1); }
    for(const auto* name:{"dofile","loadfile","load","collectgarbage"}) { lua_pushnil(L); lua_setglobal(L,name); }
    lua_pushcfunction(L,fetch); lua_setglobal(L,"_fetch");
    lua_pushcfunction(L,select); lua_setglobal(L,"_select");
    lua_sethook(L,hook,LUA_MASKCOUNT,10000);
    execute(L,"function get(url) local x,e=_fetch(url); assert(x,e); return x end\nfunction select_html(...) local x,e=_select(...); assert(x,e); return x end\nreturn true","helpers"); lua_pop(L,1);
    execute(L,read_file(assets+"/sources/json.lua",128*1024),"json"); lua_setglobal(L,"json");
    execute(L,read_file(assets+"/sources/common.lua",128*1024),"common"); lua_setglobal(L,"common");
    execute(L,read_file(assets+"/sources/"+source+".lua",128*1024),source);
    lua_pushstring(L,operation.c_str()); lua_pushstring(L,url.c_str()); lua_pushstring(L,query.c_str());
    if(lua_pcall(L,3,1,0)) { const auto* e=lua_tostring(L,-1); throw std::runtime_error(e?e:"Source adapter failed."); }
    if(!lua_istable(L,-1)) throw std::runtime_error("Source returned no result.");
    Result r; r.title=field(L,"title"); r.author=field(L,"author"); r.description=field(L,"description",16384); r.next=field(L,"next",2048); r.text=field(L,"text",max_chapter_bytes);
    if(!r.next.empty() && !allowed_url(source,r.next)) throw std::runtime_error("Invalid next-page URL.");
    lua_getfield(L,-1,"items");
    if(lua_istable(L,-1)) {
        auto count=lua_rawlen(L,-1); if(count>20000) throw std::runtime_error("Source has too many entries.");
        for(std::size_t i=1;i<=count;++i) {
            lua_rawgeti(L,-1,i); if(!lua_istable(L,-1)) throw std::runtime_error("Invalid source entry.");
            Entry e; e.title=field(L,"title"); e.url=field(L,"url",2048);
            lua_getfield(L,-1,"locked"); e.locked=lua_toboolean(L,-1); lua_pop(L,1);
            if(!allowed_url(source,e.url)) throw std::runtime_error("Invalid entry URL.");
            r.items.push_back(std::move(e)); lua_pop(L,1);
        }
    }
    return r;
}
} }
