#pragma once

#include "vita_images.hpp"
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    struct LibraryAsset
    {
        enum class Kind
        {
            Cover,
            Bookmark,
            CacheOnly
        };
        Kind kind;
        std::string key;
        std::uint64_t revision;
        std::string quote = {};
        ThumbnailPixels pixels = {};
        bool discarded = false;
    };

    // One decode at a time. Publish each result independently so the UI can
    // upload thumbnails at most once per frame, even for a batch of cache hits.
    class LibraryAssets
    {
    public:
        using Load = std::function<void(LibraryAsset &)>;
        struct Task
        {
            LibraryAsset asset;
            Load load;
        };
        static constexpr std::size_t batch_size = 4;

        ~LibraryAssets() { stop(); }

        void stop()
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
            }
            wake_.notify_one();
            if (worker_.joinable())
                worker_.join();
        }

        bool busy()
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return outstanding_ != 0;
        }

        void discard_queued_covers()
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            ++cover_request_;
        }

        void start(const std::vector<Task> &tasks)
        {
            if (tasks.empty() || tasks.size() > batch_size)
                return;
            std::vector<Job> pending;
            for (const auto &task : tasks)
                pending.push_back({std::make_unique<LibraryAsset>(task.asset), task.load});
            const std::lock_guard<std::mutex> lock(mutex_);
            ready_.reserve(batch_size);
            if (!worker_.joinable())
                worker_ = std::thread([this]
                                      { run(); });
            pending_ = std::move(pending);
            pending_cover_request_ = cover_request_;
            outstanding_ = tasks.size();
            wake_.notify_one();
        }

        std::unique_ptr<LibraryAsset> poll()
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (ready_.empty())
                return {};
            auto asset = std::move(ready_.front());
            ready_.erase(ready_.begin());
            --outstanding_;
            return asset;
        }

    private:
        void run()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while (true)
            {
                wake_.wait(lock, [this]
                           { return stopping_ || !pending_.empty(); });
                if (stopping_)
                    return;
                auto batch = std::move(pending_);
                const auto cover_request = pending_cover_request_;
                pending_.clear();
                for (auto &job : batch)
                {
                    if (stopping_)
                        return;
                    if (job.asset->kind == LibraryAsset::Kind::Cover && cover_request != cover_request_)
                    {
                        job.asset->discarded = true;
                        ready_.push_back(std::move(job.asset));
                        continue;
                    }
                    lock.unlock();
                    try
                    {
                        job.load(*job.asset);
                    }
                    catch (const std::exception &)
                    { /* The UI can keep its placeholder if an asset is unavailable. */
                    }
                    lock.lock();
                    ready_.push_back(std::move(job.asset));
                }
            }
        }

        std::mutex mutex_;
        std::condition_variable wake_;
        std::thread worker_;
        struct Job
        {
            std::unique_ptr<LibraryAsset> asset;
            Load load;
        };
        std::vector<Job> pending_;
        std::vector<std::unique_ptr<LibraryAsset>> ready_;
        std::size_t outstanding_ = 0;
        std::uint64_t cover_request_ = 0, pending_cover_request_ = 0;
        bool stopping_ = false;
    };
}