// Player -- the only module that knows about all the other media/*
// pieces at once. Everything else (HttpStreamIO, Decoder, VideoOutput,
// AudioOutput) only needs to know about its own concern; Player wires
// them together and runs the actual playback loop.
//
// Deliberately knows nothing about VPAD/controller input or the Jellyfin
// API -- main.cpp passes in a `shouldStop` callback that Player polls
// frequently, and an `onTick` callback it calls about once a second
// with the current playback position, so this file (and the rest of
// media/) has no dependency on how input is read or progress reported.

#pragma once
#include <string>
#include <functional>

enum class PlayResult {
    Completed, // reached end of stream normally
    Stopped,   // shouldStop() returned true (user backed out)
    Error,     // something failed -- see lastError()
};

struct PlayOptions {
    // Aspect ratio (width / height) the video should be displayed at.
    // <= 0 means "whatever the decoded frames are". See
    // JellyfinClient::buildVideoStreamUrl for why this can differ from
    // the decoded frame dimensions.
    double displayAspect = 0.0;
};

class Player {
public:
    // host/port/path identify the media stream (see
    // JellyfinClient::buildVideoStreamUrl / buildAudioStreamUrl for how
    // these get built).
    //
    // shouldStop is polled many times per second; return true to abort
    // playback early. onTick, if provided, is called roughly once per
    // second with the current playback position in seconds (stream
    // time, so it starts near 0 and tracks what the user is actually
    // hearing/seeing) -- intended for updating a "Now Playing" screen
    // and/or reporting progress back to Jellyfin.
    PlayResult play(const std::string& host, int port, const std::string& path,
                     const std::function<bool()>& shouldStop,
                     const std::function<void(double positionSeconds)>& onTick = nullptr,
                     const PlayOptions& options = PlayOptions());

    // Last known playback position (seconds) -- valid after play()
    // returns, for the final progress report.
    double positionSeconds() const { return last_position_; }

    const std::string& lastError() const { return last_error_; }

private:
    std::string last_error_;
    double last_position_ = 0.0;
};
