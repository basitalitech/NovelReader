#include "online.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <cerrno>
#include <mutex>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#endif

namespace reader
{
    namespace online
    {
        namespace
        {
            void directories(const std::string &path)
            {
                for (std::size_t i = 1; i <= path.size(); ++i)
                    if (i == path.size() || path[i] == '/')
                    {
                        const auto part = path.substr(0, i);
                        if (part.empty() || part.back() == ':')
                            continue;
                        if (mkdir(part.c_str(), 0777) != 0 && errno != EEXIST)
                            throw std::runtime_error("Could not create download folder. Check free space.");
                    }
            }
            struct Network
            {
                std::atomic<bool> control_ready{false};
#ifdef __vita__
                std::vector<unsigned char> memory;
                bool net = false, ctl = false, module = false;
#endif
                bool initialized = false;
                void init()
                {
                    if (initialized)
                        return;
#ifdef __vita__
                    if (!module)
                    {
                        if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0)
                            throw std::runtime_error("Could not load networking.");
                        module = true;
                    }
                    if (!net)
                    {
                        memory.resize(2 * 1024 * 1024);
                        SceNetInitParam p{memory.data(), static_cast<int>(memory.size()), 0};
                        if (sceNetInit(&p) < 0)
                            throw std::runtime_error("Could not initialize networking.");
                        net = true;
                    }
                    if (!ctl)
                    {
                        if (sceNetCtlInit() < 0)
                            throw std::runtime_error("Could not initialize Wi-Fi control.");
                        ctl = true;
                        control_ready.store(true, std::memory_order_release);
                    }
#endif
                    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
                        throw std::runtime_error("Could not initialize HTTPS.");
                    initialized = true;
                }
                int connection_state() const
                {
#ifdef __vita__
                    // Calling a NET export before its sysmodule and NetCtl are initialized
                    // can abort that first frame. Publish readiness from the worker before
                    // querying.
                    if (!control_ready.load(std::memory_order_acquire))
                        return -1;
                    int state = 0;
                    return sceNetCtlInetGetState(&state) >= 0 ? state : -1;
#else
                    return -1;
#endif
                }
                ~Network()
                {
                    if (initialized)
                        curl_global_cleanup();
#ifdef __vita__
                    if (ctl)
                        sceNetCtlTerm();
                    if (net)
                        sceNetTerm();
                    if (module)
                        sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
#endif
                }
            };
            struct Transfer
            {
                std::string bytes;
                const std::atomic<bool> &cancel;
                bool oversized = false, failed = false;
            };
            size_t write(char *data, size_t size, size_t count, void *ptr)
            {
                auto &t = *static_cast<Transfer *>(ptr);
                if (t.cancel || (count && size > max_response_bytes / count) || size * count > max_response_bytes - t.bytes.size())
                {
                    t.oversized = !t.cancel;
                    return 0;
                }
                try
                {
                    t.bytes.append(data, size * count);
                }
                catch (...)
                {
                    t.failed = true;
                    return 0;
                }
                return size * count;
            }
            int progress(void *ptr, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return static_cast<Transfer *>(ptr)->cancel ? 1 : 0; }
            std::string http(Network &network, const std::string &assets, const std::string &source, std::string url, const std::atomic<bool> &cancel, bool artwork = false, bool updates = false)
            {
                network.init();
#ifdef __vita__
                int state = 0;
                if (sceNetCtlInetGetState(&state) < 0 || state != SCE_NETCTL_STATE_CONNECTED)
                    throw std::runtime_error("Connect Wi-Fi in Vita Settings, then retry.");
#endif
                for (int redirect = 0; redirect < 6; ++redirect)
                {
                    if (cancel)
                        throw std::runtime_error("Canceled.");
                    if (!(updates ? url == latest_release_url : artwork ? allowed_cover_url(url)
                                                                        : allowed_url(source, url)))
                        throw std::runtime_error("Redirect left the allowed HTTPS hosts.");
                    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
                    if (!curl)
                        throw std::runtime_error("Not enough memory for HTTPS.");
                    auto *c = curl.get();
                    Transfer transfer{{}, cancel};
                    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
                    const std::string agent = std::string("NovelReader/") + app_version + " (PS Vita)";
                    curl_easy_setopt(c, CURLOPT_USERAGENT, agent.c_str());
                    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
                    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
                    curl_easy_setopt(c, CURLOPT_PROXY, "");
                    curl_easy_setopt(c, CURLOPT_CAINFO, (assets + "/cacert.pem").c_str());
                    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
                    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
                    curl_easy_setopt(c, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
                    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, artwork ? 5L : 15L);
                    curl_easy_setopt(c, CURLOPT_TIMEOUT, artwork ? 10L : 40L);
                    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
                    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write);
                    curl_easy_setopt(c, CURLOPT_WRITEDATA, &transfer);
                    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
                    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
                    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &transfer);
                    const auto code = curl_easy_perform(c);
                    if (cancel)
                        throw std::runtime_error(updates ? "Update check canceled." : "Canceled. Completed chapters are kept.");
                    if (transfer.oversized)
                        throw std::runtime_error("Response exceeds 4 MiB.");
                    if (transfer.failed)
                        throw std::runtime_error("Not enough memory for response.");
                    if (code != CURLE_OK)
                        throw std::runtime_error(std::string("HTTPS: ") + curl_easy_strerror(code));
                    long status = 0;
                    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
                    if (status >= 300 && status < 400)
                    {
                        char *target = nullptr;
                        curl_easy_getinfo(c, CURLINFO_REDIRECT_URL, &target);
                        if (!target)
                            throw std::runtime_error("Invalid source redirect.");
                        url = target;
                        continue;
                    }
                    if (updates && status == 404)
                        throw std::runtime_error("No published release found, or the repository is unavailable.");
                    if (updates && (status == 403 || status == 429))
                        throw std::runtime_error("GitHub denied the update check or its request limit was reached. Try later.");
                    if (status != 200)
                        throw std::runtime_error((updates ? "Update server" : "Source") + std::string(" returned HTTP ") + std::to_string(status) + ". Try again later.");
                    if (transfer.bytes.find("Just a moment...") != std::string::npos || transfer.bytes.find("cf-chl-") != std::string::npos)
                        throw std::runtime_error("Source requires browser verification. Try another source.");
                    return std::move(transfer.bytes);
                }
                throw std::runtime_error("Too many source redirects.");
            }
        }
        struct Service::Impl
        {
            std::string assets, directory;
            std::mutex mutex;
            std::thread thread;
            std::atomic<bool> canceled{false};
            Update state;
            Network network;
            Fetch transport;
            Impl(std::string a, std::string d, Fetch t) : assets(std::move(a)), directory(std::move(d)), transport(std::move(t)) {}
            ~Impl()
            {
                canceled = true;
                if (thread.joinable())
                    thread.join();
            }
            void status(std::size_t done, std::size_t total, const std::string &message)
            {
                std::lock_guard<std::mutex> lock(mutex);
                state.completed = done;
                state.total = total;
                state.message = message;
            }
            void work(Request request)
            {
                Update result;
                result.request = request;
                try
                {
                    auto fetch = [&](const std::string &url)
                    { return transport ? transport(url) : http(network, assets, request.source, url, canceled); };
                    auto run = [&](const std::string &op, const std::string &url)
                    { return run_adapter(assets, request.source, op, url, request.query, fetch, canceled); };
                    if (request.action == Action::CheckUpdates)
                    {
                        status(0, 0, "Checking GitHub for updates...");
                        const auto bytes = transport ? transport(latest_release_url) : http(network, assets, "", latest_release_url, canceled, false, true);
                        if (canceled)
                            throw std::runtime_error("Update check canceled.");
                        result.result = release_info(assets, bytes, app_version, canceled);
                    }
                    else if (request.action == Action::Browse)
                    {
                        status(0, 0, "Loading catalog...");
                        result.result = run("browse", request.url);
                    }
                    else if (request.action == Action::Remove)
                    {
                        status(0, 0, "Deleting downloaded book data...");
                        if (!remove_downloads(request.manifest, result.error))
                            throw std::runtime_error(result.error);
                        result.manifest = request.manifest;
                    }
                    else if (request.action == Action::Chapters)
                    {
                        Novel novel;
                        novel.source = request.source;
                        novel.url = request.url;
                        std::set<std::string> pages, chapters;
                        auto url = request.url;
                        while (!url.empty())
                        {
                            if (canceled)
                                throw std::runtime_error("Canceled.");
                            if (pages.size() >= 400 || !pages.insert(url).second)
                                throw std::runtime_error("Chapter pagination is too large or repeats.");
                            status(pages.size(), 0, "Loading chapter list, page " + std::to_string(pages.size()) + "...");
                            auto page = run("chapters", url);
                            if (novel.title.empty())
                            {
                                novel.title = page.title;
                                novel.author = page.author;
                                novel.description = page.description;
                            }
                            if (novel.cover.empty())
                                novel.cover = page.cover;
                            for (auto &c : page.items)
                                if (chapters.insert(c.url).second)
                                    novel.chapters.push_back(std::move(c));
                            if (novel.chapters.size() > 20000)
                                throw std::runtime_error("Novel exceeds 20,000 chapters.");
                            url = page.next;
                        }
                        if (novel.title.empty() || novel.chapters.empty())
                            throw std::runtime_error("Novel details or chapters unavailable. Source layout may have changed.");
                        auto path = novel_path(directory, novel);
                        directories(directory + '/' + novel.source);
                        if (!save_novel(path, novel))
                            throw std::runtime_error("Could not save novel index. Check free space.");
                        if (!novel.cover.empty())
                        {
                            status(0, 0, "Loading cover artwork...");
                            // Optional artwork is downloaded on this worker only. A
                            // failed image must never prevent opening a novel.
                            try
                            {
                                auto bytes = transport ? transport(novel.cover) : http(network, assets, request.source, novel.cover, canceled, true);
                                ImageInfo info;
                                std::string error;
                                if (!canceled && bytes.size() <= max_response_bytes && inspect_image(bytes, info, error))
                                    atomic_write(cover_path(path), bytes);
                            }
                            catch (const std::exception &)
                            {
                            }
                            if (canceled)
                                throw std::runtime_error("Canceled.");
                        }
                        result.novel = std::move(novel);
                        result.manifest = path;
                    }
                    else
                    {
                        auto novel = request.novel;
                        if (novel.source != request.source || !allowed_url(novel.source, novel.url) || request.chapter >= novel.chapters.size())
                            throw std::runtime_error("Invalid download request.");
                        const auto path = novel_path(directory, novel);
                        directories(directory + '/' + novel.source);
                        directories(chapter_path(path, novel.url).substr(0, chapter_path(path, novel.url).find_last_of('/')));
                        if (!save_novel(path, novel))
                            throw std::runtime_error("Could not save novel index.");
                        const auto begin = request.all ? 0 : request.chapter, end = request.all ? novel.chapters.size() : request.chapter + 1;
                        std::size_t done = 0, total = 0;
                        for (auto i = begin; i < end; ++i)
                            if (!novel.chapters[i].locked)
                                ++total;
                        for (auto i = begin; i < end; ++i)
                        {
                            if (canceled)
                                throw std::runtime_error("Canceled. Completed chapters are kept.");
                            const auto &chapter = novel.chapters[i];
                            if (cached(path, chapter.url))
                            {
                                Book book;
                                std::string error;
                                if (load_book(chapter_path(path, chapter.url), book, error) && !book.text.empty())
                                {
                                    if (!chapter.locked)
                                        ++done;
                                    continue;
                                }
                            }
                            if (chapter.locked)
                            {
                                if (!request.all)
                                    throw std::runtime_error("This chapter requires login or purchase.");
                                continue;
                            }
                            status(done, total, "Downloading " + std::to_string(done + 1) + " / " + std::to_string(total));
                            auto page = run("chapter", chapter.url);
                            if (page.text.empty() || page.text.size() > max_chapter_bytes)
                                throw std::runtime_error("Chapter text is empty or exceeds 2 MiB.");
                            if (!atomic_write(chapter_path(path, chapter.url), page.text))
                                throw std::runtime_error("Could not save chapter. Check free space.");
                            ++done;
                            status(done, total, "Saved " + std::to_string(done) + " / " + std::to_string(total));
                            // Sequential requests with a short cancelable pause avoid flooding a source.
                            if (request.all)
                                for (int n = 0; n < 5 && !canceled; ++n)
                                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                        result.novel = std::move(novel);
                        result.manifest = path;
                    }
                    result.message = "Complete.";
                }
                catch (const std::exception &e)
                {
                    result.error = e.what();
                }
                catch (...)
                {
                    result.error = "Online operation failed.";
                }
                std::lock_guard<std::mutex> lock(mutex);
                result.completed = state.completed;
                result.total = state.total;
                result.done = true;
                state = std::move(result);
            }
        };
        Service::Service(const std::string &assets, const std::string &directory, Fetch transport) : impl_(new Impl(assets, directory, std::move(transport))) {}
        Service::~Service() = default;
        bool Service::start(const Request &request)
        {
            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                if (impl_->state.busy)
                    return false;
            }
            if (impl_->thread.joinable())
                impl_->thread.join();
            impl_->canceled = false;
            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                impl_->state = {};
                impl_->state.busy = true;
            }
            try
            {
                impl_->thread = std::thread([this, request]
                                            { impl_->work(request); });
            }
            catch (const std::exception &e)
            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                impl_->state = {};
                impl_->state.done = true;
                impl_->state.request = request;
                impl_->state.error = std::string("Worker start failed: ") + e.what();
                return false;
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                impl_->state = {};
                impl_->state.done = true;
                impl_->state.request = request;
                impl_->state.error = "Worker start failed: unknown exception.";
                return false;
            }
            return true;
        }
        void Service::cancel() { impl_->canceled = true; }
        int Service::connection_state() const { return impl_->network.connection_state(); }
        Update Service::poll()
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (impl_->state.done)
            {
                auto result = std::move(impl_->state);
                impl_->state = {};
                return result;
            }
            Update result;
            result.busy = impl_->state.busy;
            result.completed = impl_->state.completed;
            result.total = impl_->state.total;
            result.message = impl_->state.message;
            return result;
        }
    }
}