#include "slt_engine.hpp"
#include <algorithm>
#include <stdexcept>
extern "C"
{
    cst_voice *register_cmu_us_slt(const char *);
    void unregister_cmu_us_slt(cst_voice *);
}

namespace reader
{
    namespace speech
    {
        std::mutex &SltEngine::mutex()
        {
            static std::mutex value;
            return value;
        }
        SltEngine &SltEngine::get()
        {
            static SltEngine value;
            return value;
        }
        SltEngine::SltEngine()
        {
            flite_init();
            voice_ = register_cmu_us_slt(nullptr);
            if (!voice_)
                throw std::runtime_error("Could not initialize the SLT voice.");
            streaming_ = new_audio_streaming_info();
            streaming_->min_buffsize = 1024;
            feat_set(voice_->features, "streaming_info", audio_streaming_info_val(streaming_));
        }
        SltEngine::~SltEngine() { unregister_cmu_us_slt(voice_); }
        void SltEngine::configure(cst_audio_stream_callback callback, void *context, int words_per_minute)
        {
            streaming_->asc = callback;
            streaming_->userdata = context;
            feat_set_float(voice_->features, "duration_stretch", 175.f / std::max(100, std::min(350, words_per_minute)));
        }
        void SltEngine::disconnect()
        {
            // Drop borrowed pointers after every utterance has been released. Preserve
            // registration so EOF does not repeatedly tear down shared voice features.
            streaming_->userdata = nullptr;
            streaming_->asc = nullptr;
            streaming_->utt = nullptr;
            streaming_->item = nullptr;
        }
    }
}