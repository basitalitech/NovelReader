#include "speech.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace reader::speech;
namespace
{
    void check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    struct Capture
    {
        std::mutex mutex;
        std::condition_variable changed;
        std::vector<short> pcm;
        int writes = 0, closes = 0;
        bool fail = false;
    };
    class Sink final : public Audio
    {
        Capture &capture_;
        bool opened_ = false;

    public:
        explicit Sink(Capture &capture) : capture_(capture) {}
        bool open(int rate) override
        {
            opened_ = rate == 22050;
            return opened_;
        }
        bool write(const short *samples, std::size_t count) override
        {
            std::lock_guard<std::mutex> lock(capture_.mutex);
            ++capture_.writes;
            capture_.changed.notify_all();
            if (capture_.fail)
                return false;
            capture_.pcm.insert(capture_.pcm.end(), samples, samples + count);
            return true;
        }
        void close() override
        {
            std::lock_guard<std::mutex> lock(capture_.mutex);
            if (opened_)
            {
                ++capture_.closes;
                opened_ = false;
            }
        }
    };
    std::unique_ptr<Audio> make(Capture &capture)
    {
        return buffered_audio(std::unique_ptr<Audio>(new Sink(capture)));
    }
    bool wrote(Capture &capture, int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(capture.mutex);
        return capture.changed.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]
                                        { return capture.writes != 0; });
    }
}
int main()
{
    try
    {
        std::vector<short> source(25003);
        for (std::size_t i = 0; i < source.size(); ++i)
            source[i] = static_cast<short>((i * 71) % 32000 - 16000);
        Capture complete;
        auto audio = make(complete);
        check(audio->open(22050), "Queue did not open");
        for (std::size_t i = 0; i < source.size(); i += 37)
            check(audio->write(source.data() + i, std::min<std::size_t>(37, source.size() - i)), "Queue rejected PCM");
        audio->close();
        audio->close();
        check(audio->healthy() && complete.pcm == source && complete.closes == 1,
              "Queue dropped/reordered samples or padded between callbacks");

        Capture startup;
        audio = make(startup);
        check(audio->open(22050), "Prefill queue did not open");
        check(audio->write(source.data(), 3 * 1024), "Prefill write failed");
        check(!wrote(startup, 25), "Playback started without lookahead");
        check(audio->write(source.data() + 3 * 1024, 1024), "Prefill threshold write failed");
        check(wrote(startup, 1000), "Playback did not start after prefill");
        audio->close();
        check(startup.pcm == std::vector<short>(source.begin(), source.begin() + 4096), "Prefill lost samples");

        Capture short_clip;
        audio = make(short_clip);
        check(audio->open(22050), "Short clip queue did not open");
        check(audio->write(source.data(), 127), "Short clip write failed");
        audio->close();
        check(short_clip.pcm == std::vector<short>(source.begin(), source.begin() + 127), "Short EOF was lost");

        Capture paused;
        audio = make(paused);
        check(audio->open(22050), "Paused queue did not open");
        audio->pause(true);
        check(audio->write(source.data(), 4096), "Paused write failed");
        check(!wrote(paused, 25), "Paused queue still played");
        audio->pause(false);
        check(wrote(paused, 1000), "Resume did not wake playback");
        audio->close();
        check(paused.pcm == std::vector<short>(source.begin(), source.begin() + 4096), "Pause dropped samples");

        Capture canceled;
        audio = make(canceled);
        check(audio->open(22050), "Cancellation queue did not open");
        audio->pause(true);
        auto producer = std::async(std::launch::async, [&]
                                   { return audio->write(source.data(), source.size()); });
        const bool blocked = producer.wait_for(std::chrono::milliseconds(25)) == std::future_status::timeout;
        audio->cancel();
        const bool woke = producer.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
        check(blocked && woke && !producer.get(), "Cancel did not wake a producer blocked on a full queue");
        audio->close();
        check(canceled.pcm.empty() && canceled.closes == 1, "Cancel played queued speech");

        Capture broken;
        broken.fail = true;
        audio = make(broken);
        check(audio->open(22050), "Failure queue did not open");
        check(audio->write(source.data(), 127), "Failed before device consumed data");
        audio->close();
        check(!audio->healthy() && broken.closes == 1, "EOF device failure was hidden");
        std::cout << "Speech buffer tests passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}