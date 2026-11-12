#pragma once

// Included after the platform I/O headers (also exercised by frontend stubs).
#include "reader.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <functional>
#include <memory>
#include <unordered_set>
#include <cstdio>

namespace
{
    class FileBrowser
    {
    public:
        struct Result
        {
            bool importing = false, saved = false, refresh = false;
            std::string path, restore, error;
            std::vector<reader::LibraryItem> items;
            std::vector<std::string> books;
            std::vector<std::size_t> order;
            std::function<void(reader::LibraryItem &)> metadata;
            std::size_t skipped = 0, failed = 0;
        };
        FileBrowser(std::string directory, std::string index)
            : directory_(std::move(directory)), index_(std::move(index)), worker_([this]
                                                                                  { run(); }) {}
        ~FileBrowser()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_ = true;
                ++generation_;
            }
            wake_.notify_one();
            worker_.join();
        }
        void list(const std::string &path, const std::string &restore,
                  std::vector<std::string> books, std::function<void(reader::LibraryItem &)> metadata)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++generation_;
            pending_ = {};
            pending_.path = path;
            pending_.restore = restore;
            pending_.refresh = !books.empty();
            pending_.books = std::move(books);
            pending_.metadata = std::move(metadata);
            pending_generation_ = generation_;
            has_pending_ = true;
            result_.reset();
            wake_.notify_one();
        }
        void cancel()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++generation_;
            has_pending_ = false;
            result_.reset();
        }
        void import(std::vector<reader::LibraryItem> items, std::vector<std::string> books,
                    std::function<void(reader::LibraryItem &)> metadata)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++generation_;
            pending_ = {};
            pending_.importing = true;
            pending_.items = std::move(items);
            pending_.books = std::move(books);
            pending_.metadata = std::move(metadata);
            completed = 0;
            total = pending_.items.size();
            pending_generation_ = generation_;
            has_pending_ = true;
            result_.reset();
            wake_.notify_one();
        }
        std::unique_ptr<Result> poll()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return std::move(result_);
        }
        std::atomic<std::size_t> completed{0}, total{0};

    private:
        bool canceled(unsigned generation) const { return generation_.load() != generation; }
        void listing(Result &job, unsigned generation)
        {
            if (job.path.empty())
            {
                // User storage and ordinary read-only content mounts. No privileged
                // mount calls, system partition probes, or permission changes.
                for (const char *mount : {"ux0:", "uma0:", "imc0:", "xmc0:", "ur0:", "grw0:", "gro0:", "app0:"})
                {
                    if (canceled(generation))
                        return;
                    const std::string path = std::string(mount) + "/";
                    const auto handle = sceIoDopen(path.c_str());
                    if (handle < 0)
                        continue;
                    sceIoDclose(handle);
                    reader::LibraryItem item;
                    item.directory = true;
                    item.filename = item.path = path;
                    item.title = mount;
                    job.items.push_back(std::move(item));
                }
                if (job.items.empty())
                    job.error = "No accessible storage. Connect storage and refresh.";
                return;
            }
            const auto handle = sceIoDopen(job.path.c_str());
            if (handle < 0)
            {
                job.error = "Cannot open this folder. Go Back or check storage and refresh.";
                return;
            }
            // Close even when allocation or decoding fails.
            struct Close
            {
                int handle;
                ~Close() { sceIoDclose(handle); }
            } close{handle};
            int read = 0;
            while (!canceled(generation))
            {
                SceIoDirent entry{};
                read = sceIoDread(handle, &entry);
                if (read <= 0)
                    break;
                const std::string name(entry.d_name);
                // VitaSDK st_attr is SceIoFileMode, not DOS/FAT attributes: bit 2
                // is a permission bit, NOT a hidden flag. Do not misinterpret it.
                // Dot entries are hidden on every supported backend.
                if (!reader::valid_library_component(name))
                    continue;
                const bool folder = SCE_S_ISDIR(entry.d_stat.st_mode);
                if (!folder && !(SCE_S_ISREG(entry.d_stat.st_mode) && reader::is_supported_book(name)))
                    continue;
                const auto path = job.path + (job.path.back() == '/' ? "" : "/") + name;
                if (!reader::valid_device_path(path))
                    continue;
                if (job.items.size() >= 20000)
                {
                    job.error = "Folder limit reached (20,000 entries). Browse a smaller folder.";
                    break;
                }
                reader::LibraryItem item;
                item.path = path;
                item.directory = folder;
                item.filename = folder ? path : reader::library_identity(path, directory_);
                item.title = name;
                job.items.push_back(std::move(item));
            }
            if (read < 0)
                job.error = "Folder read interrupted. Check storage and refresh.";
            const std::unordered_set<std::string> known(job.books.begin(), job.books.end());
            for (auto &item : job.items)
            {
                if (canceled(generation))
                    return;
                if (!item.directory && known.count(item.filename))
                {
                    item.title = reader::title_from_filename(item.path);
                    job.metadata(item);
                }
            }
        }
        void importing(Result &job, unsigned generation)
        {
            std::unordered_set<std::string> known(job.books.begin(), job.books.end());
            std::vector<reader::LibraryItem> added;
            for (auto &item : job.items)
            {
                if (canceled(generation))
                    return;
                if (known.count(item.filename))
                {
                    ++job.skipped;
                    ++completed;
                    continue;
                }
                SceIoStat stat{};
                if (item.directory || !reader::is_supported_book(item.path) ||
                    sceIoGetstat(item.path.c_str(), &stat) < 0 || !SCE_S_ISREG(stat.st_mode))
                {
                    ++job.failed;
                    ++completed;
                    continue;
                }
                auto *source = std::fopen(item.path.c_str(), "rb");
                if (!source)
                {
                    ++job.failed;
                    ++completed;
                    continue;
                }
                std::fclose(source);
                item.title = reader::title_from_filename(item.path);
                job.metadata(item);
                if (!item.error.empty())
                {
                    ++job.failed;
                    ++completed;
                    continue;
                }
                if (job.books.size() >= 20000)
                {
                    ++job.failed;
                    ++completed;
                    continue;
                }
                known.insert(item.filename);
                job.books.push_back(item.filename);
                added.push_back(std::move(item));
                ++completed;
            }
            job.items = std::move(added);
            if (canceled(generation))
                return;
            // References only: no source writes/copies, including saved .nvr indexes
            // whose chapter caches must remain beside their original files.
            job.saved = job.items.empty() || reader::save_library(index_, job.books);
            if (!job.saved)
                job.error = "Could not save library. Check free storage; sources are unchanged.";
        }
        void run()
        {
            for (;;)
            {
                Result job;
                unsigned generation;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    wake_.wait(lock, [this]
                               { return stop_ || has_pending_; });
                    if (stop_)
                        return;
                    job = std::move(pending_);
                    generation = pending_generation_;
                    has_pending_ = false;
                }
                try
                {
                    if (job.importing)
                        importing(job, generation);
                    else
                    {
                        listing(job, generation);
                        if (!canceled(generation))
                            job.order = reader::library_order(job.items, reader::LibrarySort::Title, reader::LibraryFilter::All);
                    }
                }
                catch (const std::exception &)
                {
                    job.saved = false;
                    job.error = "Could not load files. Check storage or choose a smaller folder.";
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!canceled(generation))
                        result_.reset(new Result(std::move(job)));
                }
            }
        }
        const std::string directory_, index_;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::atomic<unsigned> generation_{0};
        unsigned pending_generation_ = 0;
        bool stop_ = false, has_pending_ = false;
        Result pending_;
        std::unique_ptr<Result> result_;
        std::thread worker_;
    };
}