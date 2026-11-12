#pragma once
#include <mutex>
extern "C"
{
#include <flite.h>
}

namespace reader
{
    namespace speech
    {
        // Call get()/configure()/disconnect() only while holding mutex(). The linked
        // SLT voice is process-global, and remains registered between reading sessions.
        class SltEngine
        {
        public:
            static std::mutex &mutex();
            static SltEngine &get();
            cst_voice *voice() const { return voice_; }
            void configure(cst_audio_stream_callback callback, void *context, int words_per_minute);
            void disconnect();

        private:
            SltEngine();
            ~SltEngine();
            cst_voice *voice_ = nullptr;
            cst_audio_streaming_info *streaming_ = nullptr; // Owned by voice features.
        };
    }
}