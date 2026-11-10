#include "speech.hpp"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <mutex>
#include <pthread.h>

namespace reader { namespace speech {
namespace {
class BufferedAudio final : public Audio {
    // At SLT's 16 kHz, start with 256 ms queued and cap lookahead at 512 ms.
    static constexpr std::size_t frames = 1024, capacity = 8, prefill = 4;
    struct Block { std::array<short, frames> pcm{}; std::size_t count = 0; };
    std::unique_ptr<Audio> device_;
    std::array<Block, capacity> queue_;
    Block pending_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::size_t read_ = 0, count_ = 0;
    bool paused_ = false, canceled_ = false, ending_ = false, failed_ = false;
    bool worker_started_ = false;
    pthread_t worker_{};

    bool enqueue() {
        if (!pending_.count) return healthy();
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this] { return count_ < capacity || canceled_ || failed_; });
        if (canceled_ || failed_) return false;
        queue_[(read_ + count_) % capacity] = pending_;
        ++count_; pending_.count = 0;
        changed_.notify_all();
        return true;
    }
    static void* entry(void* argument) {
        static_cast<BufferedAudio*>(argument)->play();
        return nullptr;
    }
    void play() {
        try {
            bool primed = false;
            for (;;) {
                Block block;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    if (!count_) primed = false;
                    changed_.wait(lock, [this, &primed] {
                        return canceled_ || failed_ ||
                            (!paused_ && (ending_ || (count_ && (primed || count_ >= prefill))));
                    });
                    if (canceled_ || failed_ || (ending_ && !count_)) break;
                    block = queue_[read_];
                    read_ = (read_ + 1) % capacity; --count_;
                    primed = true;
                    changed_.notify_all();
                }
                if (!device_->write(block.pcm.data(), block.count)) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    failed_ = true; changed_.notify_all(); break;
                }
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            failed_ = true; changed_.notify_all();
        }
        device_->close();
    }
public:
    explicit BufferedAudio(std::unique_ptr<Audio> device) : device_(std::move(device)) {}
    ~BufferedAudio() override { cancel(); close(); }
    bool open(int sample_rate) override {
        close();
        { std::lock_guard<std::mutex> lock(mutex_);
          paused_ = canceled_ = ending_ = failed_ = false; read_ = count_ = 0; }
        pending_.count = 0;
        if (!device_->open(sample_rate)) return false;
        pthread_attr_t attributes;
        auto error = pthread_attr_init(&attributes);
        if (!error) {
            // This worker only copies/resamples PCM; it never calls the synthesizer.
            error = pthread_attr_setstacksize(&attributes, 64 * 1024);
            if (!error) error = pthread_create(&worker_, &attributes, entry, this);
            pthread_attr_destroy(&attributes);
        }
        if (error) { device_->close(); return false; }
        worker_started_ = true;
        return true;
    }
    bool write(const short* samples, std::size_t count) override {
        if (!worker_started_ || !healthy()) return false;
        while (count) {
            const auto take = std::min(count, frames - pending_.count);
            std::copy(samples, samples + take, pending_.pcm.begin() + pending_.count);
            pending_.count += take; samples += take; count -= take;
            if (pending_.count == frames && !enqueue()) return false;
        }
        return true;
    }
    void close() override {
        if (!worker_started_) return;
        enqueue(); // Flush only at EOF; never pad between synthesis callbacks.
        { std::lock_guard<std::mutex> lock(mutex_); ending_ = true; }
        changed_.notify_all();
        pthread_join(worker_, nullptr); worker_started_ = false;
    }
    void pause(bool paused) override {
        { std::lock_guard<std::mutex> lock(mutex_); paused_ = paused; }
        changed_.notify_all();
    }
    void cancel() override {
        { std::lock_guard<std::mutex> lock(mutex_); canceled_ = true; }
        changed_.notify_all();
    }
    bool healthy() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return !failed_ && !canceled_;
    }
};
}
std::unique_ptr<Audio> buffered_audio(std::unique_ptr<Audio> device) {
    if (!device) return {};
    return std::unique_ptr<Audio>(new BufferedAudio(std::move(device)));
}
} }
