#include "player.h"
#include "http_stream_io.h"
#include "decoder.h"
#include "video_output.h"
#include "audio_output.h"
#include "frame_queue.h"

#include <SDL2/SDL.h>
#include <thread>
#include <atomic>

PlayResult Player::play(const std::string& host, int port, const std::string& path,
                         const std::function<bool()>& shouldStop,
                         const std::function<void()>& onTick) {
    HttpStreamIO io(host, port, path);
    if (!io.open()) {
        last_error_ = "HttpStreamIO::open failed: " + std::string(io.lastError());
        return PlayResult::Error;
    }

    Decoder decoder;
    if (!decoder.open(io.avioContext())) {
        last_error_ = "Decoder::open failed: " + std::string(decoder.lastError());
        return PlayResult::Error;
    }

    bool hasVideo = decoder.hasVideo();
    bool hasAudio = decoder.hasAudio();

    if (!hasVideo && !hasAudio) {
        last_error_ = "stream has neither video nor audio we can decode";
        decoder.close();
        return PlayResult::Error;
    }

    VideoOutput video;
    if (hasVideo) {
        if (!video.init(decoder.videoWidth(), decoder.videoHeight())) {
            last_error_ = "VideoOutput::init failed: " + std::string(video.lastError());
            decoder.close();
            return PlayResult::Error;
        }
    }

    AudioOutput audio;
    if (hasAudio) {
        if (!audio.init(decoder.audioSampleRate(), decoder.audioChannels(),
                         decoder.audioSampleFormat())) {
            last_error_ = "AudioOutput::init failed: " + std::string(audio.lastError());
            if (hasVideo) video.shutdown();
            decoder.close();
            return PlayResult::Error;
        }
    }

    // Decode runs on its own thread, separate from rendering, feeding
    // decoded video frames to the render loop (this thread) via a
    // bounded queue -- audio frames are queued directly to SDL from the
    // decode thread, since SDL_QueueAudio is documented thread-safe for
    // exactly this. This matches the architecture a known-working Wii U
    // media player (CafeMP) uses (separate read/decode and render
    // threads). Our earlier single-threaded design -- decode and render
    // serially on one thread -- consistently produced a GX2 draw that
    // reported success at every step, with correct data confirmed all
    // the way into GPU-visible texture memory, yet nothing ever
    // displayed. We can't fully confirm the single thread was the actual
    // cause without lower-level tooling than we have, but it's the most
    // concrete remaining structural difference from a confirmed-working
    // reference, and everything else has been individually verified.
    FrameQueue videoQueue;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> decodeDone{false};

    std::thread decodeThread([&]() {
        while (!stopRequested) {
            AVFrame* frame = nullptr;
            DecodedFrameType type = decoder.decodeNextFrame(&frame);

            if (type == DecodedFrameType::NONE) {
                break;
            } else if (type == DecodedFrameType::AUDIO && hasAudio) {
                audio.queueFrame(frame);
            } else if (type == DecodedFrameType::VIDEO && hasVideo) {
                AVFrame* clone = av_frame_clone(frame);
                if (clone) {
                    videoQueue.push(clone);
                }
            }
        }
        decodeDone = true;
        videoQueue.stop();
    });

    const Uint32 videoFrameIntervalMs = 33;
    Uint32 lastVideoFrameTicks = SDL_GetTicks();

    PlayResult result = PlayResult::Completed;
    Uint32 lastTickCallMs = SDL_GetTicks();

    while (true) {
        if (shouldStop()) {
            stopRequested = true;
            videoQueue.stop();
            result = PlayResult::Stopped;
            break;
        }

        if (!hasVideo) {
            // Audio-only stream: the decode thread has no pacing (unlike
            // video, which is throttled via the bounded frame queue), so
            // on a fast connection it can finish decoding the whole
            // stream in a few seconds while SDL is still playing through
            // its queued audio. Don't declare completion -- and don't
            // let audio.shutdown() cut the device off -- until SDL has
            // actually finished playing everything that was queued.
            if (decodeDone && hasAudio && audio.queuedBytes() == 0) {
                result = PlayResult::Completed;
                break;
            }
            if (decodeDone && !hasAudio) {
                result = PlayResult::Completed;
                break;
            }

            if (onTick) {
                Uint32 nowTick = SDL_GetTicks();
                if (nowTick - lastTickCallMs >= 1000) {
                    onTick();
                    lastTickCallMs = nowTick;
                }
            }

            SDL_Delay(16);
            continue;
        }

        AVFrame* frame = videoQueue.pop();
        if (!frame) {
            // Queue stopped with nothing left -- decode thread is done.
            result = PlayResult::Completed;
            break;
        }

        Uint32 now = SDL_GetTicks();
        Uint32 elapsed = now - lastVideoFrameTicks;
        if (elapsed < videoFrameIntervalMs) {
            SDL_Delay(videoFrameIntervalMs - elapsed);
        }
        lastVideoFrameTicks = SDL_GetTicks();

        video.renderFrame(frame);
        av_frame_free(&frame);
    }

    // Make sure the decode thread actually exits before we tear down
    // decoder/audio/video out from under it. Worst case this can take up
    // to HttpStreamReader's own read timeout (15s) if it's mid-blocking-
    // read on the network when stop is requested -- acceptable for now,
    // a more aggressive cancellation would need HttpStreamReader itself
    // to check a shared stop flag inside its own wait loop.
    decodeThread.join();

    if (hasVideo) video.shutdown();
    if (hasAudio) audio.shutdown();
    decoder.close();

    return result;
}
