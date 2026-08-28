// AudioOutput -- takes decoded audio AVFrames (whatever internal format
// the source decoder produces -- AAC decode is typically planar float,
// FLTP) and converts + queues them for SDL2 to play. Decoder.cpp doesn't
// know or care about this conversion; it's entirely AudioOutput's job,
// keeping "decode" and "get audio out of the speakers" cleanly separate.

#pragma once
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}
#include <SDL2/SDL.h>

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    // sourceSampleRate/sourceChannels/sourceFormat should match
    // Decoder::audioSampleRate()/audioChannels()/audioSampleFormat().
    // Output is always resampled to interleaved 16-bit stereo, which is
    // what we ask SDL to open the audio device with -- simplest common
    // format, avoids needing to handle every possible source layout
    // downstream.
    bool init(int sourceSampleRate, int sourceChannels, AVSampleFormat sourceFormat);

    // Converts one decoded frame and queues the result for playback.
    void queueFrame(AVFrame* frame);

    // How much queued audio (in bytes, at the output format) SDL hasn't
    // played yet. Used by Player for rough audio/video pacing -- if this
    // gets too large we're decoding video faster than it needs to play.
    uint32_t queuedBytes() const;

    void shutdown();

    const char* lastError() const { return last_error_; }

private:
    SDL_AudioDeviceID device_ = 0;
    SwrContext* swr_ctx_ = nullptr;
    int out_channels_ = 2;
    int out_sample_rate_ = 48000;
    char last_error_[256] = {0};

    // Scratch buffer reused across calls to avoid reallocating on every
    // single frame.
    uint8_t* convert_buffer_ = nullptr;
    int convert_buffer_capacity_samples_ = 0;
};
