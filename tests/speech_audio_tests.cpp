#include "speech.hpp"
#include <psp2/audioout.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
int port_result = 7, releases = 0, drains = 0, rate = 0, length = 0;
bool fail_output = false;
std::vector<short> output;
const short* in_flight = nullptr;
std::vector<short> submitted;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<short> render(const std::vector<short>& source, bool split, int sample_rate = 22050) {
    output.clear();
    auto sink = reader::speech::platform_audio();
    check(sink->open(sample_rate), "Audio port failed to open");
    if (split) {
        for (std::size_t i = 0; i < source.size(); i += 37)
            check(sink->write(source.data() + i, std::min<std::size_t>(37, source.size() - i)), "Audio write failed");
    } else check(sink->write(source.data(), source.size()), "Audio write failed");
    sink->close(); sink->close();
    return output;
}
}
int sceAudioOutOpenPort(SceAudioOutPortType type, int len, int freq, SceAudioOutMode) {
    check(type == SCE_AUDIO_OUT_PORT_TYPE_MAIN, "Wrong audio port type");
    length = len; rate = freq; return port_result;
}
int sceAudioOutOutput(int port, const void* samples) {
    check(port == 7, "Wrong audio port");
    // Model the driver retaining the last submitted pointer until the next call.
    // The old single-buffer implementation overwrote it between these calls.
    if (in_flight) {
        check(std::equal(submitted.begin(), submitted.end(), in_flight),
              "PCM buffer was overwritten while the device was playing it");
        in_flight = nullptr;
    }
    if (!samples) { ++drains; return 0; }
    if (fail_output) return -1;
    const auto* pcm = static_cast<const short*>(samples);
    output.insert(output.end(), pcm, pcm + length);
    submitted.assign(pcm, pcm + length); in_flight = pcm;
    return 0;
}
int sceAudioOutReleasePort(int port) { check(port == 7, "Releasing wrong port"); ++releases; return 0; }
int main() {
    try {
        // Full-scale transitions exercise signed overflow in the interpolation.
        std::vector<short> source(22050);
        for (std::size_t i = 0; i < source.size(); ++i) source[i] = i % 2 ? 32767 : -32768;
        const auto single = render(source, false), split = render(source, true);
        check(rate == 48000 && length == 1024, "Vita output must use 48 kHz with bounded buffers");
        check(single == split, "Resampling lost phase across callbacks");
        check(single.size() == 48128, "Resampling changed speech duration");
        // At output sample 3 the interpolation crosses from -32768 to +32767.
        check(single[3] == -32768 + static_cast<long long>(65535) * 18150 / 48000,
              "Full-scale interpolation overflowed");
        check(releases == 2 && drains == 2, "Audio close was not idempotent");
        const std::vector<short> slt_source(source.begin(), source.begin() + 16000);
        const auto slt_single = render(slt_source, false, 16000), slt_split = render(slt_source, true, 16000);
        check(slt_single == slt_split && slt_single.size() == 48128, "SLT resampling lost PCM or changed duration");
        check(slt_single[3] == -32768 && slt_single[4] == -10923,
              "SLT full-scale interpolation was incorrect");
        check(releases == 4 && drains == 4, "SLT completion did not release/drain the device");
        auto sink = reader::speech::platform_audio();
        port_result = -1;
        check(!sink->open(22050), "Failed audio port was accepted"); sink->close();
        port_result = 7; check(sink->open(22050), "Audio could not recover");
        fail_output = true;
        check(!sink->write(source.data(), source.size()), "Device write failure was ignored");
        sink->close();
        std::cout << "Vita speech audio tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
