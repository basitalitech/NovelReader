#include "reader.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <fstream>
#include <cstdio>
#include <set>

namespace reader {
bool valid_device_path(const std::string& path) {
    const auto colon = path.find(':');
    if (colon == std::string::npos || colon < 2 || colon > 8 || path.size() > 768 ||
        path.size() <= colon + 2 || path[colon + 1] != '/') return false;
    for (std::size_t i=0;i<colon;++i)
        if (!std::isalnum(static_cast<unsigned char>(path[i]))) return false;
    std::size_t at=colon+2;
    while (at<path.size()) {
        const auto end=path.find('/',at);
        if (!valid_library_component(path.substr(at,end==std::string::npos?end:end-at))) return false;
        if (end==std::string::npos) return true;
        at=end+1;
    }
    return false;
}
std::string library_identity(const std::string& path, const std::string& directory) {
    const auto prefix=directory+'/';
    if(path.compare(0,prefix.size(),prefix)==0)return path.substr(prefix.size());
    // Vita accepts both ux0:data and ux0:/data. Keep pre-existing save IDs.
    auto absolute=prefix;
    const auto colon=absolute.find(':');
    if(colon!=std::string::npos && absolute[colon+1]!='/')absolute.insert(colon+1,"/");
    return path.compare(0,absolute.size(),absolute)==0 ? path.substr(absolute.size()) : path;
}
std::string library_book_path(const std::string& identity, const std::string& directory) {
    return valid_device_path(identity) ? identity : directory+'/'+identity;
}
bool valid_library_component(const std::string& name) {
    if (name.empty() || name.front() == '.' || name.find_first_of("/\\:") != std::string::npos || !valid_utf8(name)) return false;
    for (unsigned char ch : name) if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}
namespace {
void validate_folder(const std::string& folder) {
    if (folder.size() > 768) throw std::invalid_argument("Folder path is too long.");
    if (folder.empty()) return;
    std::size_t at = 0;
    while (at < folder.size()) {
        const auto end = folder.find('/', at);
        if (!valid_library_component(folder.substr(at, end == std::string::npos ? end : end - at)))
            throw std::invalid_argument("Invalid library folder path.");
        if (end == std::string::npos) return;
        at = end + 1;
        if (at == folder.size()) throw std::invalid_argument("Invalid library folder path.");
    }
}
}
std::string child_library_folder(const std::string& folder, const std::string& name) {
    validate_folder(folder);
    if (!valid_library_component(name)) throw std::invalid_argument("Invalid library folder name.");
    auto result = folder.empty() ? name : folder + '/' + name;
    validate_folder(result);
    return result;
}
std::string parent_library_folder(const std::string& folder) {
    validate_folder(folder);
    const auto slash = folder.find_last_of('/');
    return slash == std::string::npos ? "" : folder.substr(0, slash);
}

namespace {
bool valid_book_identity(const std::string& name) {
    if (valid_device_path(name)) return is_supported_book(name);
    try { validate_folder(name); }
    catch (const std::invalid_argument&) { return false; }
    return !name.empty() && is_supported_book(name);
}
}
bool load_library(const std::string& path, std::vector<std::string>& books) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string line;
    if (!std::getline(in, line) || line != "NRLIBRARY1") return false;
    std::vector<std::string> result;
    std::set<std::string> seen;
    while (std::getline(in, line)) {
        if (result.size() >= 20000 || !valid_book_identity(line)) return false;
        if (seen.insert(line).second) result.push_back(line);
    }
    if (!in.eof()) return false;
    books = std::move(result);
    return true;
}
bool save_library(const std::string& path, const std::vector<std::string>& books) {
    if (books.size() > 20000) return false;
    for (const auto& book : books) if (!valid_book_identity(book)) return false;
    const auto temporary = path + ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << "NRLIBRARY1\n";
    std::set<std::string> seen;
    for (const auto& book : books) if (seen.insert(book).second) out << book << '\n';
    out.close();
    if (!out || std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str()); return false;
    }
    return true;
}

bool reading_started(const Progress& progress) {
    return progress.last_read > 0 || progress.percent >= 0 || progress.offset > 0 || progress.chapter > 0;
}

int reading_percent(std::size_t chapter, std::size_t chapters, std::size_t offset,
                    std::size_t text_size, bool final_page) {
    if (!chapters) return 0;
    chapter = std::min(chapter, chapters - 1);
    if (final_page && chapter + 1 == chapters) return 100;
    const auto fraction = final_page ? 1.0 : text_size ? static_cast<double>(std::min(offset, text_size)) / text_size : 0.0;
    return std::min(99, static_cast<int>(100.0 * (chapter + fraction) / chapters));
}

std::vector<std::size_t> library_order(const std::vector<LibraryItem>& items, LibrarySort sort, LibraryFilter filter) {
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].directory) { order.push_back(i); continue; }
        if (filter == LibraryFilter::Reading && (items[i].guide || !reading_started(items[i].progress) || items[i].progress.percent == 100)) continue;
        if (filter == LibraryFilter::Finished && (items[i].guide || items[i].progress.percent != 100)) continue;
        order.push_back(i);
    }
    const auto folded = [](std::string value) {
        for (auto& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return value;
    };
    // A comparator runs O(n log n) times; normalize each title only once.
    std::vector<std::string> titles(items.size());
    for(const auto i:order)titles[i]=folded(items[i].title);
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        const auto& a = items[left]; const auto& b = items[right];
        if (a.directory != b.directory) return a.directory;
        if (a.guide != b.guide) return !a.guide;
        if (!a.directory && sort == LibrarySort::Recent && a.progress.last_read != b.progress.last_read)
            return a.progress.last_read > b.progress.last_read;
        if (!a.directory && sort == LibrarySort::Progress && a.progress.percent != b.progress.percent)
            return a.progress.percent > b.progress.percent;
        const auto& title_a = titles[left]; const auto& title_b = titles[right];
        return title_a == title_b ? a.filename < b.filename : title_a < title_b;
    });
    return order;
}
} // namespace reader
