// Decoder -- wraps FFmpeg's demuxer + decoders behind a simple
// "give me the next decoded frame" interface. Knows nothing about SDL2,
// the network, or Jellyfin -- it just turns an already-open AVIOContext
// into a stream of decoded AVFrames. video_output.cpp / audio_output.cpp
// are the ones that know what to do with those frames.

#pragma once
#include <cstdint>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

enum class DecodedFrameType {
    NONE,   // end of stream, nothing more to decode
    VIDEO,
    AUDIO,
};

class Decoder {
public:
    Decoder();
    ~Decoder();

    // avioCtx must already be open (see HttpStreamIO::open()) and stay
    // alive for as long as this Decoder is in use -- Decoder does not
    // own or free it.
    bool open(AVIOContext* avioCtx);

    // Decodes and returns the next available frame, whichever stream
    // (video or audio) it comes from first. The returned AVFrame* stays
    // owned by Decoder and is only valid until the next call to
    // decodeNextFrame() -- clone it (av_frame_clone) if it needs to
    // outlive that.
    DecodedFrameType decodeNextFrame(AVFrame** outFrame);

    // Presentation time of a frame just returned by decodeNextFrame(),
    // in seconds of stream time (i.e. comparable between the audio and
    // video streams). NAN if the container gave us no usable timestamp.
    double frameTimeSeconds(DecodedFrameType type, const AVFrame* frame) const;

    void close();

    // Video stream info (only meaningful once open() succeeds and a
    // video stream was found).
    bool hasVideo() const { return video_stream_index_ >= 0; }
    int videoWidth() const { return video_ctx_ ? video_ctx_->width : 0; }
    int videoHeight() const { return video_ctx_ ? video_ctx_->height : 0; }
    AVRational videoTimeBase() const;
    // Nominal seconds per frame from the container's frame rate, or 0 if
    // unknown. Used as a fallback pacing interval when frames arrive
    // without timestamps.
    double videoFrameDuration() const;
    const char* videoCodecName() const;

    // Audio stream info (only meaningful once open() succeeds and an
    // audio stream was found).
    bool hasAudio() const { return audio_stream_index_ >= 0; }
    int audioSampleRate() const { return audio_ctx_ ? audio_ctx_->sample_rate : 0; }
    int audioChannels() const;
    AVSampleFormat audioSampleFormat() const { return audio_ctx_ ? audio_ctx_->sample_fmt : AV_SAMPLE_FMT_NONE; }

    const char* lastError() const { return last_error_; }

private:
    AVFormatContext* fmt_ctx_ = nullptr;

    int video_stream_index_ = -1;
    int audio_stream_index_ = -1;
    AVCodecContext* video_ctx_ = nullptr;
    AVCodecContext* audio_ctx_ = nullptr;

    AVPacket* packet_ = nullptr;
    bool packet_pending_ = false; // packet_ holds a read-but-not-yet-accepted packet
    int send_retries_ = 0;
    AVFrame* frame_ = nullptr;

    bool reachedEof_ = false;
    int video_decode_errors_ = 0;
    char last_error_[256] = {0};

    bool openCodecForStream(int streamIndex, AVCodecContext** outCtx);
};
