#pragma once

// Optional aggregate diagnostics: no allocations or file writes in timed paths.
#include <cstdint>
#ifdef NOVELREADER_PROFILE
#include <array>
#include <cstdio>
#include <mutex>
#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#include <malloc.h>
#else
#include <chrono>
#endif
#endif

namespace reader
{
    namespace perf
    {
        enum class Event
        {
            Navigation,
            ReaderExit,
            Cleanup,
            Scan,
            Metadata,
            Chapter,
            TextRead,
            BookmarkPreview,
            CoverDecode,
            LibraryOrder,
            ProgressWrite,
            SettingsWrite,
            Layout,
            FrameWork,
            FramePresent,
            FrameInterval,
            IdleWait,
            FontInit,
            Resources,
            OfflineProbe,
            Count
        };
#ifdef NOVELREADER_PROFILE
        struct Sample
        {
            std::uint64_t calls = 0, total_us = 0, max_us = 0;
        };
        inline std::array<Sample, static_cast<unsigned>(Event::Count)> &samples()
        {
            static std::array<Sample, static_cast<unsigned>(Event::Count)> values{};
            return values;
        }
        inline std::mutex &mutex()
        {
            static std::mutex value;
            return value;
        }
        struct Heap
        {
            std::uint64_t calls = 0, current = 0, maximum = 0;
        };
        inline Heap &heap_sample()
        {
            static Heap value;
            return value;
        }
        inline void sample_heap()
        {
#ifdef __vita__
            const auto info = mallinfo();
            const std::lock_guard<std::mutex> lock(mutex());
            auto &sample = heap_sample();
            ++sample.calls;
            sample.current = info.uordblks;
            if (sample.current > sample.maximum)
                sample.maximum = sample.current;
#endif
        }
        inline std::uint64_t now()
        {
#ifdef __vita__
            return sceKernelGetProcessTimeWide();
#else
            return std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
#endif
        }
        inline void record(Event event, std::uint64_t elapsed)
        {
            const std::lock_guard<std::mutex> lock(mutex());
            auto &sample = samples()[static_cast<unsigned>(event)];
            ++sample.calls;
            sample.total_us += elapsed;
            if (elapsed > sample.max_us)
                sample.max_us = elapsed;
        }
        inline void reset()
        {
            const std::lock_guard<std::mutex> lock(mutex());
            samples() = {};
        }
        inline void dump(FILE *out)
        {
            if (!out)
                return;
            const std::lock_guard<std::mutex> lock(mutex());
            const char *names[] = {"navigation", "reader_exit", "cleanup", "scan", "metadata", "chapter", "text_read",
                                   "bookmark_preview", "cover_decode", "library_order", "progress_write", "settings_write", "layout",
                                   "frame_work", "frame_present", "frame_interval", "idle_wait", "font_init", "resources", "offline_probe"};
            std::fprintf(out, "event,calls,total_us,max_us\n");
            for (unsigned i = 0; i < samples().size(); ++i)
            {
                const auto &s = samples()[i];
                if (s.calls)
                    std::fprintf(out, "%s,%llu,%llu,%llu\n", names[i],
                                 static_cast<unsigned long long>(s.calls), static_cast<unsigned long long>(s.total_us),
                                 static_cast<unsigned long long>(s.max_us));
            }
            const auto &heap = heap_sample();
            if (heap.calls)
                std::fprintf(out, "# heap_bytes,samples=%llu,current=%llu,max=%llu\n",
                             static_cast<unsigned long long>(heap.calls), static_cast<unsigned long long>(heap.current),
                             static_cast<unsigned long long>(heap.maximum));
        }
        struct Scope
        {
            Event event;
            std::uint64_t start;
            explicit Scope(Event value) : event(value), start(now()) {}
            ~Scope() { record(event, now() - start); }
        };
#else
        struct Scope
        {
            explicit Scope(Event) {}
        };
#endif
    }
}