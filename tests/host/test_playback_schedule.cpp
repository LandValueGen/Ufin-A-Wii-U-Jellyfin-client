// Regression test for "video plays, then audio, then video...":
// Jellyfin's fragmented MP4, read as a live (non-seekable) stream, hands
// out each multi-second fragment as a run of video packets followed by a
// run of audio packets. Decoding strictly in that order with Player's
// small decoded-frame queue starves audio while video waits (and the
// other way round).
//
// This drives the real HttpStreamReader -> HttpStreamIO -> Decoder chain
// over HTTP with a clip muxed that way (5 s fragments), and simulates
// Player: a 6-frame video queue, at most 1.5 s of audio decoded ahead, a
// playback clock that only advances when the decode side would wait.
// With the per-stream scheduling Player uses, audio must never run dry
// before the end; the old strict-order decoding is run too, to show the
// clip really does reproduce the problem.
#include "check.h"
#include "fake_http_server.h"
#include "media/decoder.h"
#include "media/http_stream_io.h"

#include <cmath>
#include <deque>
#include <fstream>
#include <sstream>

static const size_t VIDEO_QUEUE = 6;         // FrameQueue::MAX_SIZE
static const double AUDIO_AHEAD = 1.5;       // MAX_AUDIO_AHEAD_SECONDS
static const double FRAME = 1.0 / 30.0;
static const double AAC_FRAME = 1024.0 / 48000.0;

struct Sim {
    std::deque<double> videoQ;
    double clock = 0.0;
    double audioEnd = 0.0;  // stream time up to which audio is decoded
    int underruns = 0;      // playback ticks with no audio left to play
    int ticks = 0;
    int videoFrames = 0, audioFrames = 0;

    double audioAhead() const { return audioEnd - clock; }
    void tick(bool atEnd) {
        clock += FRAME;
        ticks++;
        while (!videoQ.empty() && videoQ.front() <= clock) videoQ.pop_front();
        if (!atEnd && audioEnd < clock) underruns++;
    }
    void gotAudio(double pts) {
        audioFrames++;
        if (!std::isnan(pts)) audioEnd = std::max(audioEnd, pts + AAC_FRAME);
    }
};

static bool openChain(HttpStreamIO& io, Decoder& decoder) {
    if (!io.open()) return false;
    return decoder.open(io.avioContext());
}

// Player's decode-thread scheduling (src/media/player.cpp).
static Sim runScheduled(int port) {
    Sim sim;
    HttpStreamIO io("127.0.0.1", port, "/long.mp4");
    Decoder decoder;
    CHECK(openChain(io, decoder));
    for (int guard = 0; guard < 200000; guard++) {
        bool progressed = false;
        AVFrame* frame = nullptr;
        bool audioWanted = sim.audioAhead() < AUDIO_AHEAD;
        bool videoWanted = sim.videoQ.size() < VIDEO_QUEUE;
        if (audioWanted && decoder.decodeAudioFrame(&frame) == DecodedFrameType::AUDIO) {
            sim.gotAudio(decoder.frameTimeSeconds(DecodedFrameType::AUDIO, frame));
            progressed = true;
        }
        if (videoWanted && decoder.decodeVideoFrame(&frame) == DecodedFrameType::VIDEO) {
            sim.videoQ.push_back(decoder.frameTimeSeconds(DecodedFrameType::VIDEO, frame));
            sim.videoFrames++;
            progressed = true;
        }
        if (progressed) continue;
        if (decoder.finished()) break;
        bool starved = (audioWanted && !decoder.hasQueuedAudioPackets()) ||
                       (videoWanted && !decoder.hasQueuedVideoPackets());
        if (starved && !decoder.demuxFinished()) {
            decoder.readPacket();
        } else {
            sim.tick(decoder.demuxFinished()); // decode thread would sleep: playback moves on
        }
    }
    CHECK(decoder.finished());
    return sim;
}

// The previous behaviour: frames strictly in stream order, blocking on a
// full video queue / too much audio ahead.
static Sim runStrictOrder(int port) {
    Sim sim;
    HttpStreamIO io("127.0.0.1", port, "/long.mp4");
    Decoder decoder;
    CHECK(openChain(io, decoder));
    while (true) {
        AVFrame* frame = nullptr;
        DecodedFrameType type = decoder.decodeNextFrame(&frame);
        if (type == DecodedFrameType::NONE) break;
        double pts = decoder.frameTimeSeconds(type, frame);
        if (type == DecodedFrameType::VIDEO) {
            while (sim.videoQ.size() >= VIDEO_QUEUE) sim.tick(false);
            sim.videoQ.push_back(pts);
            sim.videoFrames++;
        } else {
            while (sim.audioAhead() > AUDIO_AHEAD) sim.tick(false);
            sim.gotAudio(pts);
        }
    }
    return sim;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: test_playback_schedule <long-fragment.mp4>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    FakeHttpServer server;
    CHECK(server.start());
    FakeRoute route;
    route.contentType = "video/mp4";
    route.body = ss.str();
    route.chunked = true;
    route.chunkSize = 9000;
    server.addRoute("/long.mp4", route);

    Sim fixed = runScheduled(server.port());
    Sim old = runStrictOrder(server.port());
    printf("  scheduled: video=%d audio=%d ticks=%d underruns=%d\n", fixed.videoFrames,
           fixed.audioFrames, fixed.ticks, fixed.underruns);
    printf("  strict order: video=%d audio=%d ticks=%d underruns=%d\n", old.videoFrames,
           old.audioFrames, old.ticks, old.underruns);

    // Everything is decoded either way...
    CHECK(fixed.videoFrames >= 355 && fixed.videoFrames <= 362); // 12 s at 30 fps
    CHECK(fixed.audioFrames >= 555);                              // 12 s of AAC
    CHECK_EQ(fixed.videoFrames, old.videoFrames);
    // ...but only the scheduled version keeps audio flowing throughout.
    CHECK_EQ(fixed.underruns, 0);
    CHECK(old.underruns > 30); // the clip does reproduce the bug

    server.stop();
    return check::finish("test_playback_schedule");
}
