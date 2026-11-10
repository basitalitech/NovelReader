#include "speech.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <pthread.h>

using namespace reader;
using namespace reader::speech;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Capture {
    std::atomic<std::size_t> samples{0}, nonzero{0};
    std::atomic<int> opens{0}, closes{0}, rate{0};
    std::atomic<bool> slow{false}, broken{false};
    std::atomic<std::size_t> stack_bytes{0};
};
class Sink final : public Audio {
    Capture& capture;
    bool opened = false;
public:
    explicit Sink(Capture& c) : capture(c) {}
    bool open(int rate) override {
#ifdef __linux__
        pthread_attr_t attributes;
        if (pthread_getattr_np(pthread_self(), &attributes) != 0) return false;
        std::size_t bytes = 0;
        const auto error = pthread_attr_getstacksize(&attributes, &bytes);
        pthread_attr_destroy(&attributes);
        if (error) return false;
        capture.stack_bytes = bytes;
#endif
        capture.rate = rate; ++capture.opens; opened = true; return true;
    }
    bool write(const short* data, std::size_t count) override {
        capture.samples += count;
        for (std::size_t i = 0; i < count; ++i) if (data[i]) ++capture.nonzero;
        if (capture.slow) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return !capture.broken;
    }
    void close() override { if (opened) { ++capture.closes; opened = false; } }
};
template<class Predicate> void until(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < end, "Timed out waiting for speech worker");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
}
int main() {
    try {
        Book book; book.text = "First sentence.\n[Image: dragon]\nCaf\xc3\xa9 is open! Last line.";
        const auto image = book.text.find("[Image");
        book.illustrations.push_back({image, "dragon", "image.png", book.text.find(']', image) + 1});
        auto chunks = segments(book, 0);
        check(chunks.size() == 3, "Sentence splitting or illustration exclusion failed");
        for (const auto& chunk : chunks) {
            check(chunk.text == book.text.substr(chunk.begin, chunk.end - chunk.begin), "Lost source offsets");
            check(chunk.text.find("Image") == std::string::npos, "Illustration marker was spoken");
            check(valid_utf8(chunk.text), "Split UTF-8");
        }
        check(segments(book, book.text.size()).empty(), "End offset must produce no speech");
        check(segments(book, image + 3).front().text.find("Caf") == 0, "Starting inside image did not skip it");
        Book long_book; for (int i = 0; i < 1600; ++i) long_book.text += "\xc3\xa9";
        chunks = segments(long_book, 1);
        for (const auto& chunk : chunks) check(valid_utf8(chunk.text) && chunk.text.size() <= 600, "Unbounded/invalid UTF-8 chunk");

        Capture capture;
        auto factory = [&capture] { return std::unique_ptr<Audio>(new Sink(capture)); };
        Player player(NOVELREADER_SPEECH_DATA, factory);
        Book short_book; short_book.text = "Hello from NovelReader. This speech works offline.";
        player.start(short_book, 0);
        until([&] { const auto s = player.status().state; return s == State::Finished || s == State::Error; });
        check(player.status().state == State::Finished, player.status().error.c_str());
        check(capture.rate == 22050 && capture.samples > 1000 && capture.nonzero > 100, "Real eSpeak produced no audible PCM");
        check(capture.opens == 1 && capture.closes == 1, "Audio not released on completion");
#ifdef __linux__
        check(capture.stack_bytes == worker_stack_bytes && capture.stack_bytes >= 256 * 1024,
              "Narration did not use its explicit stack allocation");
#endif

        Book lengthy; for (int i = 0; i < 80; ++i) lengthy.text += "A long sentence to test stopping and pausing the narrator. ";
        capture.slow = true;
        auto baseline = capture.samples.load();
        player.start(lengthy, 0);
        until([&] { return capture.samples > baseline + 5000; });
        player.pause(true);
        check(player.status().state == State::Paused, "Pause state missing");
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
        baseline = capture.samples;
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
        check(capture.samples == baseline, "Audio kept streaming while paused");
        player.rate(225); player.pause(false);
        until([&] { return capture.samples > baseline; });
        player.pause(true);
        player.stop(); // Must wake a paused callback and join promptly.
        check(player.status().state == State::Idle && capture.opens == capture.closes, "Stop leaked audio");
        capture.slow = false; capture.broken = true;
        player.start(short_book, 0);
        until([&] { return player.status().state == State::Error; });
        player.stop(); capture.broken = false;
        player.start(short_book, 0); // Reinitialize after cancellation/error.
        until([&] { return player.status().state == State::Finished; });
        player.stop();
        Player missing("/nonexistent/novelreader-speech-data", factory);
        missing.start(short_book, 0);
        until([&] { return missing.status().state == State::Error; });
        check(!missing.status().error.empty(), "Missing voice data needs a useful error");
        std::cout << "Offline speech tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
