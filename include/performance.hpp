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
            StartupHome,
            FirstLibrary,
            FirstLibraryCovers,
            Count
        };
        enum class LoadKind
        {
            Book,
            Chapter
        };
        struct ZipWork
        {
            std::uint64_t opens = 0, entries = 0, probe_bytes = 0;
        };
        struct LayoutWork
        {
            std::uint64_t total_us = 0, wrap_us = 0, measure_us = 0, measure_calls = 0, measured_bytes = 0;
        };
        enum class FrameStage
        {
            Draw,
            Present
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
        inline ZipWork &zip_work()
        {
            static ZipWork value;
            return value;
        }
        inline void add_zip_work(ZipWork delta)
        {
            const std::lock_guard<std::mutex> lock(mutex());
            auto &work = zip_work();
            work.opens += delta.opens;
            work.entries += delta.entries;
            work.probe_bytes += delta.probe_bytes;
        }
        inline ZipWork zip_snapshot()
        {
            const std::lock_guard<std::mutex> lock(mutex());
            return zip_work();
        }
        struct LoadSample
        {
            LoadKind kind;
            std::uint64_t elapsed_us;
            ZipWork work;
            LayoutWork layout;
            std::uint64_t frame_draw_us, frame_present_us;
        };
        struct LoadSamples
        {
            std::array<LoadSample, 128> items{};
            std::uint64_t count = 0;
        };
        inline LoadSamples &load_samples()
        {
            static LoadSamples value;
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
        struct Duration
        {
            explicit Duration(std::uint64_t *total) : total_(total), start_(total ? now() : 0) {}
            ~Duration()
            {
                if (total_)
                    *total_ += now() - start_;
            }
            Duration(const Duration &) = delete;
            Duration &operator=(const Duration &) = delete;

        private:
            std::uint64_t *total_;
            std::uint64_t start_;
        };
        struct InputTimer
        {
            std::uint64_t &slot;
            std::uint64_t previous;
            InputTimer(std::uint64_t &time, bool active) : slot(time), previous(time)
            {
                if (active && !time)
                    slot = now();
            }
            ~InputTimer() { slot = previous; }
        };
        struct ReaderLoad
        {
            bool pending = false;
            LoadKind kind = LoadKind::Book;
            std::uint64_t start = 0;
            ZipWork before;
            LayoutWork layout;
            std::uint64_t frame_draw_us = 0, frame_present_us = 0;

            ReaderLoad() = default;
            ReaderLoad(bool epub, LoadKind type, std::uint64_t input_time) : pending(epub), kind(type)
            {
                if (pending)
                {
                    start = input_time ? input_time : now();
                    before = zip_snapshot();
                }
            }
            LayoutWork *layout_work() { return pending ? &layout : nullptr; }
            std::uint64_t *frame_time(FrameStage stage, bool reader_frame)
            {
                if (!pending || !reader_frame)
                    return nullptr;
                return stage == FrameStage::Draw ? &frame_draw_us : &frame_present_us;
            }
            void presented()
            {
                if (!pending)
                    return;
                const auto elapsed = now() - start;
                const std::lock_guard<std::mutex> lock(mutex());
                auto &samples = load_samples();
                const auto &after = zip_work();
                samples.items[samples.count++ % samples.items.size()] = {
                    kind, elapsed, {after.opens - before.opens, after.entries - before.entries, after.probe_bytes - before.probe_bytes}, layout, frame_draw_us, frame_present_us};
                pending = false;
            }
        };
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
            zip_work() = {};
            load_samples() = {};
        }
        inline void dump(FILE *out)
        {
            if (!out)
                return;
            const std::lock_guard<std::mutex> lock(mutex());
            const char *names[] = {"navigation", "reader_exit", "cleanup", "scan", "metadata", "chapter", "text_read",
                                   "bookmark_preview", "cover_decode", "library_order", "progress_write", "settings_write", "layout",
                                   "frame_work", "frame_present", "frame_interval", "idle_wait", "font_init", "resources", "offline_probe",
                                   "startup_home", "first_library", "first_library_covers"};
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
            const auto &loads = load_samples();
            if (loads.count)
            {
                std::fprintf(out, "# epub_load,kind,elapsed_us,zip_opens,directory_entries_scanned,image_probe_bytes,"
                                  "layout_us,wrap_us,measure_us,measure_calls,measured_bytes,first_frame_draw_us,first_frame_present_us\n");
                const auto first = loads.count > loads.items.size() ? loads.count - loads.items.size() : 0;
                for (auto i = first; i < loads.count; ++i)
                {
                    const auto &item = loads.items[i % loads.items.size()];
                    std::fprintf(out, "# epub_load,%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                                 item.kind == LoadKind::Book ? "book" : "chapter",
                                 static_cast<unsigned long long>(item.elapsed_us), static_cast<unsigned long long>(item.work.opens),
                                 static_cast<unsigned long long>(item.work.entries), static_cast<unsigned long long>(item.work.probe_bytes),
                                 static_cast<unsigned long long>(item.layout.total_us), static_cast<unsigned long long>(item.layout.wrap_us),
                                 static_cast<unsigned long long>(item.layout.measure_us), static_cast<unsigned long long>(item.layout.measure_calls),
                                 static_cast<unsigned long long>(item.layout.measured_bytes), static_cast<unsigned long long>(item.frame_draw_us),
                                 static_cast<unsigned long long>(item.frame_present_us));
                }
            }
        }
        struct Scope
        {
            Event event;
            std::uint64_t start;
            explicit Scope(Event value) : event(value), start(now()) {}
            ~Scope() { record(event, now() - start); }
        };
#else
        inline void add_zip_work(ZipWork) {}
        struct InputTimer
        {
            InputTimer(std::uint64_t &, bool) {}
        };
        struct Duration
        {
            explicit Duration(std::uint64_t *) {}
        };
        struct ReaderLoad
        {
            ReaderLoad() = default;
            ReaderLoad(bool, LoadKind, std::uint64_t) {}
            LayoutWork *layout_work() { return nullptr; }
            std::uint64_t *frame_time(FrameStage, bool) { return nullptr; }
            void presented() {}
        };
        struct Scope
        {
            explicit Scope(Event) {}
        };
#endif
    }
}