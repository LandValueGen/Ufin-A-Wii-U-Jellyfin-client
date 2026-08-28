#include "audio_output.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

AudioOutput::AudioOutput() {}

AudioOutput::~AudioOutput() {
    shutdown();
}

bool AudioOutput::init(int sourceSampleRate, int sourceChannels, AVSampleFormat sourceFormat) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        snprintf(last_error_, sizeof(last_error_), "SDL_InitSubSystem(AUDIO) failed: %s",
                 SDL_GetError());
        return false;
    }

    SDL_AudioSpec want{};
    SDL_AudioSpec have{};
    want.freq = out_sample_rate_;
    want.format = AUDIO_S16SYS;
    want.channels = (Uint8)out_channels_;
    want.samples = 4096; // buffer size in samples per channel; on the
                         // smaller side balances latency vs. underrun risk

    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0 /* no format changes allowed */);
    if (device_ == 0) {
        snprintf(last_error_, sizeof(last_error_), "SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return false;
    }

    // Using the classic swr_alloc_set_opts (channel layout as a bitmask
    // via av_get_default_channel_layout) rather than the newer
    // swr_alloc_set_opts2/AVChannelLayout API -- consistent with the
    // rest of this FFmpeg build still exposing older-style deprecated
    // APIs rather than having removed them. If a future FFmpeg-wiiu
    // update changes this, this call is the first thing to fix.
    int64_t inLayout = av_get_default_channel_layout(sourceChannels);
    int64_t outLayout = av_get_default_channel_layout(out_channels_);

    swr_ctx_ = swr_alloc_set_opts(nullptr,
        outLayout, AV_SAMPLE_FMT_S16, out_sample_rate_,
        inLayout, sourceFormat, sourceSampleRate,
        0, nullptr);

    if (!swr_ctx_ || swr_init(swr_ctx_) < 0) {
        snprintf(last_error_, sizeof(last_error_), "swr_init failed");
        return false;
    }

    SDL_PauseAudioDevice(device_, 0); // start playback (unpause)
    return true;
}

void AudioOutput::queueFrame(AVFrame* frame) {
    if (!swr_ctx_ || device_ == 0) return;

    // Worst case output sample count (resampling can change the count,
    // e.g. going from 44.1kHz source to 48kHz output).
    int maxOutSamples = (int)av_rescale_rnd(
        swr_get_delay(swr_ctx_, frame->sample_rate) + frame->nb_samples,
        out_sample_rate_, frame->sample_rate, AV_ROUND_UP);

    if (maxOutSamples > convert_buffer_capacity_samples_) {
        av_freep(&convert_buffer_);
        int lineSize = 0;
        av_samples_alloc(&convert_buffer_, &lineSize, out_channels_, maxOutSamples,
                          AV_SAMPLE_FMT_S16, 0);
        convert_buffer_capacity_samples_ = maxOutSamples;
    }

    int convertedSamples = swr_convert(swr_ctx_, &convert_buffer_, maxOutSamples,
                                        (const uint8_t**)frame->data, frame->nb_samples);
    if (convertedSamples <= 0) return;

    int bytesPerSample = out_channels_ * (int)sizeof(int16_t);
    SDL_QueueAudio(device_, convert_buffer_, convertedSamples * bytesPerSample);
}

uint32_t AudioOutput::queuedBytes() const {
    if (device_ == 0) return 0;
    return SDL_GetQueuedAudioSize(device_);
}

void AudioOutput::shutdown() {
    if (swr_ctx_) { swr_free(&swr_ctx_); }
    if (convert_buffer_) { av_freep(&convert_buffer_); convert_buffer_capacity_samples_ = 0; }
    if (device_ != 0) { SDL_CloseAudioDevice(device_); device_ = 0; }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
