#include "speech.hpp"
#include "slt_engine.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <pthread.h>

namespace reader
{
    namespace speech
    {
        namespace
        {
            bool continuation(char c) { return (static_cast<unsigned char>(c) & 0xc0) == 0x80; }
            bool whitespace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
            bool next_segment(const Book &book, std::size_t &offset, Segment &result)
            {
                const auto size = book.text.size();
                offset = std::min(offset, size);
                while (offset < size && continuation(book.text[offset]))
                    ++offset;
                while (offset < size)
                {
                    const auto image = std::find_if(book.illustrations.begin(), book.illustrations.end(),
                                                    [offset](const Illustration &item)
                                                    { return item.offset <= offset && offset < item.end; });
                    if (image != book.illustrations.end())
                    {
                        offset = std::min(image->end, size);
                        continue;
                    }
                    if (whitespace(book.text[offset]))
                    {
                        ++offset;
                        continue;
                    }
                    const auto begin = offset;
                    auto limit = std::min(size, begin + max_segment_bytes);
                    for (const auto &item : book.illustrations)
                        if (item.offset > begin)
                            limit = std::min(limit, item.offset);
                    while (limit < size && continuation(book.text[limit]))
                        --limit;
                    auto end = begin;
                    while (end < limit)
                    {
                        const auto c = book.text[end++];
                        if (c == '\n' || ((c == '.' || c == '!' || c == '?') &&
                                          (end == size || whitespace(book.text[end]))))
                            break;
                    }
                    // Prefer a word boundary when bounding a long sentence.
                    if (end == limit && limit < size && !whitespace(book.text[limit]))
                    {
                        auto word = end;
                        while (word > begin && !whitespace(book.text[word - 1]))
                            --word;
                        if (word > begin)
                            end = word;
                    }
                    if (end == begin)
                    {
                        ++offset;
                        continue;
                    }
                    result = {begin, end, book.text.substr(begin, end - begin)};
                    offset = end;
                    return true;
                }
                return false;
            }
        }

        std::vector<Segment> segments(const Book &book, std::size_t offset)
        {
            std::vector<Segment> result;
            Segment chunk;
            while (next_segment(book, offset, chunk))
                result.push_back(std::move(chunk));
            return result;
        }

        struct Player::Impl
        {
            AudioFactory factory;
            pthread_t worker{};
            bool worker_started = false;
            std::atomic<bool> canceled{false}, paused{false};
            std::atomic<int> speed{175};
            mutable std::mutex mutex;
            std::condition_variable wake;
            Status current{State::Idle, 0, {}};
            // Shared lifetime lets UI cancellation wake a producer blocked on a full queue.
            // Access to this handle is guarded by mutex; only the worker closes it.
            std::shared_ptr<Audio> audio;
            explicit Impl(AudioFactory f) : factory(std::move(f)) {}
            struct Work
            {
                Impl *self;
                Book book;
                std::size_t offset;
            };
            static void *entry(void *argument)
            {
                std::unique_ptr<Work> work(static_cast<Work *>(argument));
                work->self->run(std::move(work->book), work->offset);
                return nullptr;
            }
            void launch(Book book, std::size_t offset)
            {
                std::unique_ptr<Work> work(new Work{this, std::move(book), offset});
                pthread_attr_t attributes;
                auto error = pthread_attr_init(&attributes);
                if (!error)
                {
                    // The SLT probe on Vita uses this explicit 512 KiB stack budget.
                    error = pthread_attr_setstacksize(&attributes, worker_stack_bytes);
                    if (!error)
                        error = pthread_create(&worker, &attributes, entry, work.get());
                    pthread_attr_destroy(&attributes);
                }
                if (error)
                {
                    fail("Could not create the narration worker (error " + std::to_string(error) + ").");
                    return;
                }
                work.release(); // The worker owns its snapshot after successful creation.
                worker_started = true;
            }
            void fail(const std::string &error)
            {
                std::lock_guard<std::mutex> lock(mutex);
                current.state = State::Error;
                current.error = error;
            }
            bool ready()
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this]
                          { return canceled || !paused; });
                return !canceled;
            }
            static int callback(const cst_wave *wave, int start, int count, int,
                                cst_audio_streaming_info *info) noexcept
            {
                auto &self = *static_cast<Impl *>(info->userdata);
                try
                {
                    if (!self.ready())
                        return CST_AUDIO_STREAM_STOP;
                    if (count > 0 && !self.audio->write(wave->samples + start, static_cast<std::size_t>(count)))
                    {
                        if (!self.canceled)
                            self.fail("Audio playback failed.");
                        return CST_AUDIO_STREAM_STOP;
                    }
                    return CST_AUDIO_STREAM_CONT;
                }
                catch (...)
                {
                    self.fail("Speech streaming failed.");
                    return CST_AUDIO_STREAM_STOP;
                }
            }
            void run(Book book, std::size_t offset)
            {
                // Cancellation must remain responsive if another player owns SLT.
                std::unique_lock<std::mutex> engine(SltEngine::mutex(), std::defer_lock);
                while (!engine.try_lock())
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    if (wake.wait_for(lock, std::chrono::milliseconds(10), [this]
                                      { return canceled.load(); }))
                        return;
                }
                if (canceled)
                    return;
                SltEngine *slt = nullptr;
                try
                {
                    slt = &SltEngine::get();
                    {
                        auto output = std::shared_ptr<Audio>(buffered_audio(factory()));
                        if (!output || !output->open(feat_int(slt->voice()->features, "sample_rate")))
                            fail("Could not open the audio output.");
                        else
                        {
                            {
                                std::lock_guard<std::mutex> lock(mutex);
                                audio = output;
                                output->pause(paused);
                                if (canceled)
                                    output->cancel();
                            }
                            Segment chunk;
                            while (next_segment(book, offset, chunk))
                            {
                                if (!ready())
                                    break;
                                {
                                    std::lock_guard<std::mutex> lock(mutex);
                                    current.offset = chunk.begin;
                                }
                                slt->configure(callback, this, speed);
                                std::unique_ptr<cst_utterance, decltype(&delete_utterance)> utterance(
                                    flite_synth_text(chunk.text.c_str(), slt->voice()), delete_utterance);
                                if (canceled || !output->healthy())
                                    break;
                                if (status_error())
                                    break;
                            }
                            // Natural EOF drains queued speech; cancel discards queued speech.
                            output->close();
                            if (!canceled && !output->healthy())
                                fail("Audio playback failed.");
                            std::lock_guard<std::mutex> lock(mutex);
                            audio.reset();
                        }
                    }
                }
                catch (const std::exception &)
                {
                    fail("Speech could not start. Check available memory.");
                }
                std::shared_ptr<Audio> remaining;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    remaining = std::move(audio);
                }
                if (remaining)
                {
                    remaining->cancel();
                    remaining->close();
                }
                if (slt)
                    slt->disconnect();
                // Publish completion only after draining, releasing all utterances and
                // disconnecting callback pointers. The UI may immediately start again.
                std::lock_guard<std::mutex> lock(mutex);
                if (!canceled && current.state != State::Error)
                    current.state = State::Finished;
            }
            bool status_error() const
            {
                std::lock_guard<std::mutex> lock(mutex);
                return current.state == State::Error;
            }
        };

        Player::Player(AudioFactory audio) : impl_(new Impl(std::move(audio))) {}
        Player::~Player() { stop(); }
        void Player::stop()
        {
            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                impl_->canceled = true;
                if (impl_->audio)
                    impl_->audio->cancel();
            }
            impl_->wake.notify_all();
            if (impl_->worker_started)
            {
                pthread_join(impl_->worker, nullptr);
                impl_->worker_started = false;
            }
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->current.state = State::Idle;
            impl_->current.error.clear();
        }
        void Player::start(const Book &book, std::size_t offset, int words_per_minute)
        {
            stop();
            try
            {
                // Keep one immutable text snapshot, then split lazily on the worker.
                // A chapter with many tiny sentences must not allocate millions of chunks.
                Book snapshot;
                snapshot.text = book.text;
                snapshot.illustrations = book.illustrations;
                impl_->canceled = false;
                impl_->paused = false;
                rate(words_per_minute);
                {
                    std::lock_guard<std::mutex> lock(impl_->mutex);
                    impl_->current = {State::Speaking, offset, {}};
                }
                impl_->launch(std::move(snapshot), offset);
            }
            catch (const std::exception &)
            {
                impl_->fail("Speech could not start. Check available memory.");
            }
        }
        void Player::pause(bool paused)
        {
            // Use the same lock as ready() to avoid a lost cancellation/resume wakeup.
            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                if (impl_->current.state != State::Speaking && impl_->current.state != State::Paused)
                    return;
                impl_->paused = paused;
                if (impl_->audio)
                    impl_->audio->pause(paused);
                impl_->current.state = paused ? State::Paused : State::Speaking;
            }
            impl_->wake.notify_all();
        }
        void Player::rate(int words_per_minute) { impl_->speed = std::max(100, std::min(350, words_per_minute)); }
        Status Player::status() const
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            return impl_->current;
        }

    }
}