#include "speech.hpp"
#include <array>
#include <algorithm>
#ifdef __vita__
#include <psp2/audioout.h>
#endif

namespace reader { namespace speech {
#ifdef __vita__
namespace {
class VitaAudio final : public Audio {
    int port_ = -1, input_rate_ = 0;
    std::array<std::array<short, 1024>, 2> buffers_{};
    std::size_t buffer_index_ = 0;
    std::size_t used_ = 0;
    int phase_ = 0;
    short previous_ = 0;
public:
    ~VitaAudio() override { close(); }
    bool open(int sample_rate) override {
        close(); phase_ = 0; previous_ = 0; buffer_index_ = 0;
        if (sample_rate <= 0) return false;
        input_rate_ = sample_rate;
        port_ = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, buffers_[0].size(), 48000, SCE_AUDIO_OUT_MODE_MONO);
        return port_ >= 0;
    }
    bool write(const short* samples, std::size_t count) override {
        // Linear streaming resampler: retain phase across synthesis callbacks.
        for (std::size_t i = 0; i < count; ++i) {
            while (phase_ < 48000) {
                buffers_[buffer_index_][used_++] = static_cast<short>(previous_ +
                    (static_cast<std::int64_t>(samples[i]) - previous_) * phase_ / 48000);
                phase_ += input_rate_;
                if (used_ == buffers_[buffer_index_].size()) {
                    if (sceAudioOutOutput(port_, buffers_[buffer_index_].data()) < 0) return false;
                    // The accepted buffer may still be playing. Fill its partner.
                    buffer_index_ ^= 1;
                    used_ = 0;
                }
            }
            phase_ -= 48000; previous_ = samples[i];
        }
        return true;
    }
    void close() override {
        if (port_ < 0) return;
        if (used_) {
            auto& buffer = buffers_[buffer_index_];
            std::fill(buffer.begin() + used_, buffer.end(), 0);
            sceAudioOutOutput(port_, buffer.data());
        }
        sceAudioOutOutput(port_, nullptr);
        sceAudioOutReleasePort(port_); port_ = -1; used_ = 0;
    }
};
}
std::unique_ptr<Audio> platform_audio() { return std::unique_ptr<Audio>(new VitaAudio); }
#else
std::unique_ptr<Audio> platform_audio() { return {}; }
#endif
} }
