#pragma once
#include <string>
#include <vector>

struct JellyfinItem {
    std::string id;
    std::string name;
    std::string type; // "CollectionFolder", "Folder", "Movie", "Series", "Episode", ...
};

// Everything needed to make HTTP requests against a media stream:
// http_client (and http_stream_io, for playback) work with host/port/path
// separately rather than a single URL string, so this is what
// buildVideoStreamUrl() below returns instead of a plain std::string.
struct StreamTarget {
    std::string host;
    int port = 0;
    std::string path; // includes leading "/" and the full query string
};

class JellyfinClient {
public:
    JellyfinClient(std::string host, int port);

    // Logs in with a username/password and stashes the access token +
    // user id for subsequent calls. Returns false on failure -- check
    // lastError() for details.
    bool authenticate(const std::string& username, const std::string& password);

    // Top-level libraries ("Movies", "TV Shows", "Music", ...).
    bool getViews(std::vector<JellyfinItem>& out);

    // Contents of a given library/folder.
    bool getItems(const std::string& parentId, std::vector<JellyfinItem>& out);

    // Builds a request target for streaming a video item, forcing
    // transcode to H.264 (Main/High) + AAC inside a progressive MP4
    // container -- the only combination our FFmpeg build (h264_wiiu +
    // aac decoders, mov demuxer, no HLS) can actually decode. Jellyfin
    // will transcode server-side if the source file isn't already in
    // this format; if it already is, Jellyfin may remux instead of
    // re-encoding, which is faster but still returns this same shape.
    //
    // Confirmed working against a real Jellyfin server on 2026-08-21;
    // if a future Jellyfin version changes this endpoint's behavior,
    // this is the first place to check.
    // Same idea as buildVideoStreamUrl, but for audio-only items
    // (Jellyfin's /Audio/ endpoint rather than /Videos/). Forces AAC so
    // it matches the audio codec our FFmpeg build actually decodes,
    // same reasoning as the video path.
    StreamTarget buildAudioStreamUrl(const std::string& itemId) const;

    StreamTarget buildVideoStreamUrl(const std::string& itemId) const;

    // Jellyfin's "Sessions" API -- reporting these is what makes the
    // server's own web UI show "Ufin is playing X" instead of nothing.
    // We don't track a real PTS-based playback clock, so positionTicks
    // here is an approximation from elapsed wall-clock time (Jellyfin
    // ticks = 100-nanosecond units, i.e. 10,000,000 per second) --
    // good enough for an approximate progress bar, not frame-accurate.
    bool reportPlaybackStart(const std::string& itemId);
    bool reportPlaybackProgress(const std::string& itemId, int64_t positionTicks);
    bool reportPlaybackStopped(const std::string& itemId, int64_t positionTicks);

    const std::string& lastError() const { return last_error_; }

private:
    std::string host_;
    int port_;
    std::string token_;
    std::string user_id_;
    std::string last_error_;

    std::string authHeader() const;
};
