#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct JellyfinItem {
    std::string id;
    std::string name;
    std::string type;           // "CollectionFolder", "Folder", "Movie", "Series", "Episode", "TvChannel", ...
    std::string collectionType; // views only: "movies", "tvshows", "music", "livetv", "boxsets", ...
    int64_t runTimeTicks = 0;   // duration in Jellyfin ticks (100 ns), 0 if not provided
    int indexNumber = -1;       // episode / track / season number, -1 if not provided
    int parentIndexNumber = -1; // season number of an episode, -1 if not provided
    int productionYear = 0;     // 0 if not provided
    std::string channelNumber;  // TvChannel only, e.g. "5" or "5.1"
    std::string currentProgram; // TvChannel only: what's on right now, if the guide knows
    std::string seriesName;     // Episode / Season: the show it belongs to
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

// Knobs for the video transcode request (see buildVideoStreamUrl).
// Defaults are what's known to be safe for the Wii U's hardware decoder;
// config.json can override them (video_bitrate / video_profile).
struct VideoStreamOptions {
    int videoBitrate = 2500000;       // bits/s. Wii U Wi-Fi is 2.4 GHz 802.11n only.
    std::string profile = "baseline"; // H.264 profile: baseline | main | high

    // Optional, added to the URL when set. mediaSourceId picks one
    // version of a multi-version item; liveStreamId/playSessionId come
    // from openLiveStream() and are required for Live TV.
    std::string mediaSourceId;
    std::string liveStreamId;
    std::string playSessionId;

    // Where to start, in Jellyfin ticks (100 ns). Seeking restarts the
    // transcode here -- the stream itself can't be seeked.
    int64_t startTimeTicks = 0;
};

// What we need to know about a video before playing it -- fetched from
// the item's metadata rather than the stream, because the stream we ask
// Jellyfin for is deliberately not at the original aspect ratio.
struct VideoInfo {
    int width = 0;                // source video dimensions, 0 if unknown
    int height = 0;
    double displayAspect = 0.0;   // width/height the picture should be shown at, 0 if unknown
    int64_t runTimeTicks = 0;     // duration in Jellyfin ticks (100 ns), 0 if unknown
    std::string mediaSourceId;    // id of the first media source, empty if unknown
};

// An opened Live TV stream (see openLiveStream). Pass the ids into
// VideoStreamOptions and the playback reports, and hand liveStreamId to
// closeLiveStream() when playback ends so the tuner is released.
struct LiveStreamSession {
    std::string mediaSourceId;
    std::string liveStreamId;
    std::string playSessionId;
    VideoInfo info;
};

// Optional ids attached to the Sessions/Playing* reports.
struct PlaybackIds {
    std::string mediaSourceId;
    std::string liveStreamId;
    std::string playSessionId;
};

class JellyfinClient {
public:
    JellyfinClient(std::string host, int port);

    // Logs in with a username/password and stashes the access token +
    // user id for subsequent calls. Returns false on failure -- check
    // lastError() for details.
    bool authenticate(const std::string& username, const std::string& password);

    // Top-level libraries ("Movies", "TV Shows", "Music", "Live TV", ...).
    bool getViews(std::vector<JellyfinItem>& out);

    // Contents of a given library/folder.
    bool getItems(const std::string& parentId, std::vector<JellyfinItem>& out);

    // Live TV channel list (with what's currently on, when the guide
    // has data). This is what opening the "Live TV" view shows --
    // browsing it via getItems() doesn't return channels.
    bool getLiveTvChannels(std::vector<JellyfinItem>& out);

    // Library-wide search by name (movies, shows, episodes, music).
    bool search(const std::string& term, std::vector<JellyfinItem>& out);

    // Fetches the item's metadata to learn its real aspect ratio and
    // duration. Returns false (with lastError() set) if the request
    // fails; fields that couldn't be determined stay at their zero
    // defaults, so callers should fall back to 16:9.
    bool getVideoInfo(const std::string& itemId, VideoInfo& out);

    // Asks Jellyfin to tune a Live TV channel (PlaybackInfo with
    // AutoOpenLiveStream). On success the session's ids must be passed
    // to buildVideoStreamUrl (via VideoStreamOptions) -- the stream
    // endpoint refuses a channel without them.
    bool openLiveStream(const std::string& channelId, int maxBitrate, LiveStreamSession& out);

    // Releases a stream opened with openLiveStream(). Safe to call with
    // an empty id (does nothing).
    bool closeLiveStream(const std::string& liveStreamId);

    // Builds a request target for streaming an audio-only item
    // (Jellyfin's /Audio/ endpoint rather than /Videos/). Forces AAC in
    // MP4 so it matches the decoder + demuxer our FFmpeg build actually
    // has.
    StreamTarget buildAudioStreamUrl(const std::string& itemId, int64_t startTimeTicks = 0) const;

    // Builds a request target for streaming a video item (or an opened
    // Live TV channel), forcing a server-side transcode to H.264 + AAC
    // in a (fragmented) progressive MP4 -- the only combination our
    // FFmpeg build (h264_wiiu + aac decoders, mov demuxer, no HLS) can
    // decode. The exact parameters are dictated by the Wii U hardware
    // decoder wrapper; see the implementation for the reasoning.
    StreamTarget buildVideoStreamUrl(const std::string& itemId,
                                     const VideoStreamOptions& options = VideoStreamOptions()) const;

    // Jellyfin's "Sessions" API -- reporting these is what makes the
    // server's own web UI show "Ufin is playing X" instead of nothing.
    // positionTicks is in Jellyfin ticks (100-nanosecond units, i.e.
    // 10,000,000 per second).
    bool reportPlaybackStart(const std::string& itemId, const PlaybackIds& ids = PlaybackIds());
    bool reportPlaybackProgress(const std::string& itemId, int64_t positionTicks,
                                const PlaybackIds& ids = PlaybackIds(), bool isPaused = false);
    bool reportPlaybackStopped(const std::string& itemId, int64_t positionTicks,
                               const PlaybackIds& ids = PlaybackIds());

    const std::string& lastError() const { return last_error_; }
    const std::string& userId() const { return user_id_; }

private:
    std::string host_;
    int port_;
    std::string token_;
    std::string user_id_;
    std::string last_error_;

    std::string authHeader() const;
};
