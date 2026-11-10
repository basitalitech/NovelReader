#include "online.hpp"
#include "performance.hpp"
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <mbedtls/sha256.h>

namespace reader { namespace online {
const std::vector<Source>& sources() {
    static const std::vector<Source> value = {{"novgo", "Novgo", "https://novgo.net"}, {"asura", "Asura Novels", "https://asurascans.com"}, {"beast", "Beast Novels", "https://beastnovels.com"}};
    return value;
}
bool allowed_url(const std::string& source, const std::string& url) {
    if (url.size() > 2048 || url.find_first_of("\r\n\t \\") != std::string::npos || url.find('\0') != std::string::npos) return false;
    for (const auto& s : sources()) if (s.id == source)
        return url == s.home || url.compare(0, s.home.size() + 1, s.home + "/") == 0 ||
            (source == "asura" && url.find("https://api.asurascans.com/api/") == 0);
    return false;
}
std::string stable_id(const std::string& value) {
    unsigned char digest[32];
    if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(value.data()),value.size(),digest,0)!=0) throw std::runtime_error("Could not identify source URL.");
    std::ostringstream out; out << std::hex << std::setfill('0');
    for (auto byte : digest) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}
bool allowed_cover_url(const std::string& url) {
    if(url.size()>2048 || url.compare(0,8,"https://")!=0)return false;
    for(unsigned char c:url)if(c<=32 || c==127 || c=='\\')return false;
    const auto host=url.substr(8,url.find_first_of("/?#",8)-8);
    if(host.empty() || host.find_first_of("@:[]")!=std::string::npos || host.find('.')==std::string::npos)return false;
    // Image CDNs differ from chapter hosts; require a DNS name over HTTPS.
    if(host.find_first_not_of("0123456789.")==std::string::npos || host=="localhost" ||
       (host.size()>=6 && host.compare(host.size()-6,6,".local")==0))return false;
    return true;
}
std::string cover_path(const std::string& manifest) { return manifest+".cover"; }
bool load_cover(const std::string& manifest,std::string& bytes,std::string& error) {
    try {
        auto data=read_file(cover_path(manifest),max_response_bytes);
        ImageInfo info;if(!inspect_image(data,info,error))return false;
        bytes=std::move(data);error.clear();return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}
std::string read_file(const std::string& path, std::size_t limit) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("File unavailable. Download this chapter first.");
    auto size = in.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(limit)) throw std::runtime_error("File exceeds size limit.");
    std::string data(static_cast<std::size_t>(size), '\0'); in.seekg(0);
    if (!data.empty() && !in.read(&data[0], size)) throw std::runtime_error("Could not read complete file.");
    return data;
}
bool atomic_write(const std::string& path, const std::string& data) {
    const auto tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size())); out.close();
    if (!out || std::rename(tmp.c_str(), path.c_str()) != 0) { std::remove(tmp.c_str()); return false; }
    return true;
}
std::string novel_path(const std::string& directory, const Novel& novel) { return directory + '/' + novel.source + '/' + stable_id(novel.url) + ".nvr"; }
std::string chapter_path(const std::string& manifest, const std::string& url) {
    const auto slash = manifest.find_last_of('/');
    return manifest.substr(0, slash) + "/.chapters/" + manifest.substr(slash + 1) + '/' + stable_id(url) + ".txt";
}
bool cached(const std::string& manifest, const std::string& url) {
    const perf::Scope timing(perf::Event::OfflineProbe);
    std::ifstream in(chapter_path(manifest, url), std::ios::binary | std::ios::ate);
    return in && in.tellg() > 0 && in.tellg() <= static_cast<std::streamoff>(max_chapter_bytes);
}
bool save_novel(const std::string& path, const Novel& novel) {
    if (!allowed_url(novel.source, novel.url) || novel.title.empty() || novel.chapters.empty() || novel.chapters.size() > 20000) return false;
    // A collision or unrelated file must never be silently replaced.
    std::ifstream existing(path);
    if (existing) { Novel old; std::string error; if (!load_novel(path, old, error) || old.url != novel.url || old.source != novel.source) return false; }
    std::ostringstream out;
    out << "NRONLINE1\n" << std::quoted(novel.source) << ' ' << std::quoted(novel.url) << '\n'
        << std::quoted(novel.title) << ' ' << std::quoted(novel.author) << ' ' << std::quoted(novel.description) << '\n' << novel.chapters.size() << '\n';
    for (const auto& c : novel.chapters) {
        if (!allowed_url(novel.source, c.url)) return false;
        out << std::quoted(c.title) << ' ' << std::quoted(c.url) << ' ' << c.locked << '\n';
    }
    if(!novel.cover.empty() && allowed_cover_url(novel.cover))out<<"COVER "<<std::quoted(novel.cover)<<'\n';
    if (out.str().size() > max_response_bytes) return false;
    return atomic_write(path, out.str());
}
bool load_novel(const std::string& path, Novel& novel, std::string& error) {
    try {
        std::istringstream in(read_file(path, max_response_bytes));
        std::string magic; Novel n; std::size_t count = 0;
        if (!(in >> magic >> std::quoted(n.source) >> std::quoted(n.url) >> std::quoted(n.title) >> std::quoted(n.author) >> std::quoted(n.description) >> count) || magic != "NRONLINE1" || !allowed_url(n.source,n.url) || n.title.empty() || count == 0 || count > 20000) throw std::runtime_error("Invalid downloaded novel index.");
        for (std::size_t i = 0; i < count; ++i) {
            Entry c;
            if (!(in >> std::quoted(c.title) >> std::quoted(c.url) >> c.locked) || !allowed_url(n.source,c.url)) throw std::runtime_error("Invalid downloaded chapter index.");
            n.chapters.push_back(std::move(c));
        }
        // Optional trailing metadata keeps existing NRONLINE1 libraries valid.
        std::string tag;
        if(in>>tag) {
            if(tag!="COVER" || !(in>>std::quoted(n.cover)) || !allowed_cover_url(n.cover))throw std::runtime_error("Invalid cover metadata.");
        }
        novel = std::move(n); error.clear(); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
bool open_cached(const std::string& path, Publication& publication, std::string& error) {
    Novel n; if (!load_novel(path,n,error)) return false;
    Publication p; p.path = path; p.title = n.title; p.author = n.author; p.online = true;
    std::ifstream cover(cover_path(path),std::ios::binary|std::ios::ate);
    if(cover && cover.tellg()>0 && cover.tellg()<=static_cast<std::streamoff>(max_response_bytes))p.cover_path=cover_path(path);
    for (const auto& c : n.chapters) p.chapters.push_back({c.title,chapter_path(path,c.url)});
    publication = std::move(p); return true;
}
void restore_progress(const Publication& publication, Progress& progress) {
    auto find = [&](const std::string& id, std::size_t& chapter) {
        for (std::size_t i=0;i<publication.chapters.size();++i)
            if (publication.chapters[i].path==id) { chapter=i; return true; }
        return false;
    };
    if (!progress.chapter_id.empty() && !find(progress.chapter_id,progress.chapter)) { progress.chapter=0; progress.offset=0; }
    if (!progress.bookmark_id.empty() && !find(progress.bookmark_id,progress.bookmark_chapter)) progress.has_bookmark=false;
    if (progress.chapter>=publication.chapters.size()) { progress.chapter=0; progress.offset=0; }
    if (progress.bookmark_chapter>=publication.chapters.size()) progress.has_bookmark=false;
}
} }
