#include "decoder.h"
#include <cstdio>
#include <cstring>
#include <coreinit/debug.h>

Decoder::Decoder() {
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
}

Decoder::~Decoder() {
    close();
    av_packet_free(&packet_);
    av_frame_free(&frame_);
}

bool Decoder::openCodecForStream(int streamIndex, AVCodecContext** outCtx) {
    AVCodecParameters* params = fmt_ctx_->streams[streamIndex]->codecpar;

    // avcodec_find_decoder() picks whatever decoder is registered for
    // this codec ID. For H.264 that's h264_wiiu specifically -- our
    // FFmpeg build (see configure-wiiu) never registered the generic
    // software h264 decoder, only h264_wiiu, so there's no ambiguity
    // here even though this call doesn't name it explicitly.
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        snprintf(last_error_, sizeof(last_error_),
                 "no decoder registered for codec id %d", params->codec_id);
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        snprintf(last_error_, sizeof(last_error_), "avcodec_alloc_context3 failed");
        return false;
    }

    if (avcodec_parameters_to_context(ctx, params) < 0) {
        snprintf(last_error_, sizeof(last_error_), "avcodec_parameters_to_context failed");
        avcodec_free_context(&ctx);
        return false;
    }

    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        snprintf(last_error_, sizeof(last_error_), "avcodec_open2 failed for codec id %d",
                 params->codec_id);
        avcodec_free_context(&ctx);
        return false;
    }

    *outCtx = ctx;
    return true;
}

bool Decoder::open(AVIOContext* avioCtx) {
    fmt_ctx_ = avformat_alloc_context();
    if (!fmt_ctx_) {
        snprintf(last_error_, sizeof(last_error_), "avformat_alloc_context failed");
        return false;
    }

    fmt_ctx_->pb = avioCtx;
    fmt_ctx_->flags |= AVFMT_FLAG_CUSTOM_IO;

    // The filename argument is only used by FFmpeg for format-guessing
    // hints when no custom IO is attached -- since pb is already set,
    // a non-null placeholder is enough; format detection here happens
    // by probing the actual bytes via avioCtx.
    int ret = avformat_open_input(&fmt_ctx_, "stream", nullptr, nullptr);
    if (ret < 0) {
        snprintf(last_error_, sizeof(last_error_), "avformat_open_input failed (%d)", ret);
        OSReport("Ufin: avformat_open_input failed (%d)\n", ret);
        return false;
    }
    OSReport("Ufin: avformat_open_input ok\n");

    if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0) {
        snprintf(last_error_, sizeof(last_error_), "avformat_find_stream_info failed");
        OSReport("Ufin: avformat_find_stream_info failed\n");
        return false;
    }
    OSReport("Ufin: avformat_find_stream_info ok\n");

    video_stream_index_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audio_stream_index_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    OSReport("Ufin: stream detection -- video_index=%d audio_index=%d (nb_streams=%d)\n",
             video_stream_index_, audio_stream_index_, fmt_ctx_->nb_streams);

    if (video_stream_index_ < 0 && audio_stream_index_ < 0) {
        snprintf(last_error_, sizeof(last_error_), "no video or audio stream found");
        return false;
    }

    if (video_stream_index_ >= 0) {
        if (!openCodecForStream(video_stream_index_, &video_ctx_)) {
            return false; // last_error_ already set by openCodecForStream
        }
    }
    if (audio_stream_index_ >= 0) {
        if (!openCodecForStream(audio_stream_index_, &audio_ctx_)) {
            return false;
        }
    }

    return true;
}

DecodedFrameType Decoder::decodeNextFrame(AVFrame** outFrame) {
    while (true) {
        // First, see if either open codec already has a decoded frame
        // sitting ready (can happen because one input packet sometimes
        // yields multiple output frames, or during end-of-stream flush).
        if (video_ctx_) {
            int ret = avcodec_receive_frame(video_ctx_, frame_);
            if (ret == 0) {
                *outFrame = frame_;
                return DecodedFrameType::VIDEO;
            }
            // AVERROR(EAGAIN) just means "no frame yet, feed me more
            // packets" -- not an error, fall through to read more.
            // AVERROR_EOF means this stream is fully drained.
        }
        if (audio_ctx_) {
            int ret = avcodec_receive_frame(audio_ctx_, frame_);
            if (ret == 0) {
                *outFrame = frame_;
                return DecodedFrameType::AUDIO;
            }
        }

        if (reachedEof_) {
            // We've already sent the flush packets (nullptr) to both
            // codecs; once receive_frame stops producing anything after
            // that, there's genuinely nothing left.
            return DecodedFrameType::NONE;
        }

        int readRet = av_read_frame(fmt_ctx_, packet_);
        if (readRet < 0) {
            // End of stream (or a network error we can't distinguish
            // from EOF here) -- flush both decoders so any frames
            // they're internally holding onto get pushed out.
            reachedEof_ = true;
            if (video_ctx_) avcodec_send_packet(video_ctx_, nullptr);
            if (audio_ctx_) avcodec_send_packet(audio_ctx_, nullptr);
            continue;
        }

        if (packet_->stream_index == video_stream_index_ && video_ctx_) {
            avcodec_send_packet(video_ctx_, packet_);
        } else if (packet_->stream_index == audio_stream_index_ && audio_ctx_) {
            avcodec_send_packet(audio_ctx_, packet_);
        }
        // Packets from any other stream (e.g. a subtitle track we never
        // opened a codec for) are just dropped here.

        av_packet_unref(packet_);
        // Loop back around to try receive_frame() again now that we've
        // fed in a new packet.
    }
}

AVRational Decoder::videoTimeBase() const {
    if (video_stream_index_ >= 0 && fmt_ctx_) {
        return fmt_ctx_->streams[video_stream_index_]->time_base;
    }
    return AVRational{1, 1};
}

int Decoder::audioChannels() const {
    // NOTE: using the classic `channels` field rather than the newer
    // AVChannelLayout (ch_layout) API -- this FFmpeg fork still emits
    // deprecation warnings for similarly-aged APIs (refcounted_frames,
    // av_oformat_next) rather than having removed them, which is why
    // this field should still be valid here. If a future FFmpeg-wiiu
    // update pulls in a newer FFmpeg release, this is the first thing
    // to check if audio channel count comes back wrong.
    return audio_ctx_ ? audio_ctx_->channels : 0;
}

void Decoder::close() {
    if (video_ctx_) avcodec_free_context(&video_ctx_);
    if (audio_ctx_) avcodec_free_context(&audio_ctx_);
    if (fmt_ctx_) {
        // Do NOT let this free our AVIOContext -- we don't own it
        // (HttpStreamIO does), and fmt_ctx_->pb was assigned rather
        // than created by avformat_open_input. Clear pb first so
        // avformat_close_input's internal cleanup leaves it alone.
        fmt_ctx_->pb = nullptr;
        avformat_close_input(&fmt_ctx_);
    }
    video_stream_index_ = -1;
    audio_stream_index_ = -1;
    reachedEof_ = false;
}
