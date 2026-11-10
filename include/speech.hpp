#pragma once

#include "reader.hpp"
#include <functional>
#include <memory>

namespace reader { namespace speech {

// eSpeak's nested phoneme/translation frames exceed Vita's 32 KiB default.
constexpr std::size_t worker_stack_bytes = 256 * 1024;

struct Segment { std::size_t begin, end; std::string text; };
// UTF-8 byte offsets remain in the original Book, including skipped illustrations.
std::vector<Segment> segments(const Book& book, std::size_t offset);

enum class State { Idle, Speaking, Paused, Finished, Error };
struct Status { State state; std::size_t offset; std::string error; };

class Audio {
public:
    virtual ~Audio() = default;
    virtual bool open(int sample_rate) = 0;
    virtual bool write(const short* samples, std::size_t count) = 0;
    virtual void close() = 0;
    virtual void pause(bool) {}
    virtual void cancel() {}
    virtual bool healthy() const { return true; }
};

class Player {
public:
    using AudioFactory = std::function<std::unique_ptr<Audio>()>;
    Player(std::string data_path, AudioFactory audio);
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    void start(const Book& book, std::size_t offset, int words_per_minute = 175);
    void stop();
    void pause(bool paused);
    void rate(int words_per_minute);
    Status status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Host builds return a sink that reports unavailable instead of playing audio.
std::unique_ptr<Audio> platform_audio();
// A bounded queue separates synthesis from the device's playback clock.
std::unique_ptr<Audio> buffered_audio(std::unique_ptr<Audio> device);

} }
