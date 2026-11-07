#pragma once

#include "reader.hpp"
#include <atomic>
#include <memory>

namespace reader { namespace online {

constexpr std::size_t max_response_bytes = 4 * 1024 * 1024;
struct Source { std::string id, name, home; };
struct Entry { std::string title, url; bool locked = false; };
struct Result {
    std::string title, author, description, next, text;
    std::vector<Entry> items;
};
const std::vector<Source>& sources();
bool allowed_url(const std::string& source, const std::string& url);
std::string stable_id(const std::string& value);
std::string read_file(const std::string& path, std::size_t limit);
bool atomic_write(const std::string& path, const std::string& bytes);
using Fetch = std::function<std::string(const std::string&)>;
// Each call owns a bounded Lua state. Only packaged adapters and safe helpers run.
Result run_adapter(const std::string& assets, const std::string& source,
                   const std::string& operation, const std::string& url,
                   const std::string& query, const Fetch& fetch, const std::atomic<bool>& cancel);
struct Novel {
    std::string source, title, author, url, description;
    std::vector<Entry> chapters;
};
std::string novel_path(const std::string& directory, const Novel& novel);
bool save_novel(const std::string& path, const Novel& novel);
bool load_novel(const std::string& path, Novel& novel, std::string& error);
std::string chapter_path(const std::string& manifest, const std::string& url);
bool cached(const std::string& manifest, const std::string& url);
bool open_cached(const std::string& path, Publication& publication, std::string& error);
void restore_progress(const Publication& publication, Progress& progress);

// Worker results are transferred to the UI only after completion.
enum class Action { Browse, Chapters, Read, Download };
struct Request {
    Action action = Action::Browse;
    std::string source, url, query, manifest;
    Novel novel;
    std::size_t chapter = 0;
    bool all = false;
};
struct Update {
    bool busy = false, done = false;
    std::size_t completed = 0, total = 0;
    std::string message, error, manifest;
    Request request;
    Result result;
    Novel novel;
};
class Service {
public:
    Service(const std::string& assets, const std::string& directory, Fetch transport = {});
    ~Service();
    bool start(const Request& request);
    void cancel();
    Update poll();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} }
