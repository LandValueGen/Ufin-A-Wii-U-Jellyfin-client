// End-to-end: the real HttpStreamReader -> HttpStreamIO -> Decoder chain
// pulling a fragmented MP4 (H.264 baseline + AAC, muxed the way Jellyfin
// streams it) from a local HTTP server, both chunked (as Jellyfin's
// transcode endpoint sends it) and with Content-Length.
//
// Uses the host build of the same FFmpeg 4.3 fork Ufin ships with, minus
// the Wii U hardware decoder -- the software h264 decoder produces
// yuv420p instead of NV12, which is the one expected difference.
#include "check.h"
#include "fake_http_server.h"
#include "media/decoder.h"
#include "media/http_stream_io.h"

#include <cmath>
#include <fstream>
#include <sstream>

struct RunStats {
    int videoFrames = 0;
    int audioFrames = 0;
    double firstVideoPts = NAN, lastVideoPts = NAN;
    double firstAudioPts = NAN, lastAudioPts = NAN;
    bool videoMonotonic = true;
    bool audioMonotonic = true;
    int width = 0, height = 0;
    double frameDuration = 0;
    bool openOk = false;
};

static RunStats run(int port, const char* path) {
    RunStats st;
    HttpStreamIO io("127.0.0.1", port, path);
    if (!io.open()) {
        fprintf(stderr, "  HttpStreamIO::open failed: %s\n", io.lastError().c_str());
        return st;
    }
    Decoder decoder;
    if (!decoder.open(io.avioContext())) {
        fprintf(stderr, "  Decoder::open failed: %s\n", decoder.lastError());
        return st;
    }
    st.openOk = true;
    st.width = decoder.videoWidth();
    st.height = decoder.videoHeight();
    st.frameDuration = decoder.videoFrameDuration();

    while (true) {
        AVFrame* frame = nullptr;
        DecodedFrameType type = decoder.decodeNextFrame(&frame);
        if (type == DecodedFrameType::NONE) break;
        double pts = decoder.frameTimeSeconds(type, frame);
        if (type == DecodedFrameType::VIDEO) {
            if (std::isnan(st.firstVideoPts)) st.firstVideoPts = pts;
            if (!std::isnan(st.lastVideoPts) && !(pts > st.lastVideoPts)) st.videoMonotonic = false;
            st.lastVideoPts = pts;
            st.videoFrames++;
        } else {
            if (std::isnan(st.firstAudioPts)) st.firstAudioPts = pts;
            if (!std::isnan(st.lastAudioPts) && !(pts > st.lastAudioPts)) st.audioMonotonic = false;
            st.lastAudioPts = pts;
            st.audioFrames++;
        }
    }
    // A second call after NONE must stay at NONE.
    AVFrame* frame = nullptr;
    if (decoder.decodeNextFrame(&frame) != DecodedFrameType::NONE) st.openOk = false;
    decoder.close();
    return st;
}

static void checkStats(const RunStats& st, const char* label) {
    printf("  [%s] video=%d (%.3f..%.3f) audio=%d (%.3f..%.3f) size=%dx%d dur=%.4f\n", label,
           st.videoFrames, st.firstVideoPts, st.lastVideoPts, st.audioFrames, st.firstAudioPts,
           st.lastAudioPts, st.width, st.height, st.frameDuration);
    CHECK(st.openOk);
    CHECK_EQ(st.width, 320);
    CHECK_EQ(st.height, 240);
    // Without avformat_find_stream_info() (which crashed h264_wiiu, see
    // Decoder::open) a fragmented MP4 with an empty moov has no frame
    // rate yet: 0 = unknown, and Player falls back to 1/30 s -- the
    // rate Ufin asks Jellyfin for. If it IS known it must be right.
    CHECK(st.frameDuration == 0.0 || (st.frameDuration > 1.0 / 30.0 - 0.002 && st.frameDuration < 1.0 / 30.0 + 0.002));
    // 3 seconds at 30 fps; allow the encoder to trim a frame or two.
    CHECK(st.videoFrames >= 85 && st.videoFrames <= 92);
    // 3 seconds of AAC at 1024 samples / 48 kHz ~= 140 frames (+ priming).
    CHECK(st.audioFrames >= 130 && st.audioFrames <= 150);
    CHECK(st.videoMonotonic);
    CHECK(st.audioMonotonic);
    CHECK_NEAR(st.firstVideoPts, 0.0, 0.1);
    CHECK_NEAR(st.lastVideoPts, 3.0 - 1.0 / 30.0, 0.1);
    CHECK_NEAR(st.firstAudioPts, 0.0, 0.1);
    CHECK_NEAR(st.lastAudioPts, 3.0, 0.1);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: test_decoder_stream <fragmented.mp4>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string mp4 = ss.str();
    CHECK(mp4.size() > 10000);

    FakeHttpServer server;
    CHECK(server.start());

    FakeRoute chunked;
    chunked.contentType = "video/mp4";
    chunked.body = mp4;
    chunked.chunked = true;
    chunked.chunkSize = 7000; // odd size so chunk boundaries fall mid-atom
    server.addRoute("/chunked.mp4", chunked);

    FakeRoute plain;
    plain.contentType = "video/mp4";
    plain.body = mp4;
    server.addRoute("/plain.mp4", plain);

    checkStats(run(server.port(), "/chunked.mp4?static=false&api_key=x"), "chunked");
    checkStats(run(server.port(), "/plain.mp4"), "content-length");

    // A 404 must surface as an open failure, not a hang or a crash.
    {
        HttpStreamIO io("127.0.0.1", server.port(), "/nope.mp4");
        bool opened = io.open();
        if (opened) {
            Decoder decoder;
            CHECK(!decoder.open(io.avioContext()));
        } else {
            CHECK(!io.lastError().empty());
        }
    }

    server.stop();
    return check::finish("test_decoder_stream");
}
