// Player -- the only module that knows about all the other media/*
// pieces at once. Everything else (HttpStreamIO, Decoder, VideoOutput,
// AudioOutput) only needs to know about its own concern; Player wires
// them together and runs the actual playback loop.
//
// Deliberately knows nothing about VPAD/controller input -- main.cpp
// passes in a `shouldStop` callback that Player polls each loop
// iteration, so this file (and the rest of media/) has no dependency on
// how input is read. Keeps this reusable if input handling ever changes.

#pragma once
#include <string>
#include <functional>

enum class PlayResult {
    Completed, // reached end of stream normally
    Stopped,   // shouldStop() returned true (user backed out)
    Error,     // something failed -- see lastError()
};

class Player {
public:
    // host/port/path identify the media stream (see
    // JellyfinClient::buildVideoStreamUrl for how these get built).
    // shouldStop is polled once per decoded frame; return true to abort
    // playback early. onTick, if provided, is called roughly once per
    // second during audio-only playback (not on every loop iteration) --
    // intended for updating a "Now Playing" screen and/or reporting
    // progress back to Jellyfin, without Player needing to know
    // anything about screen rendering or the Jellyfin API itself.
    PlayResult play(const std::string& host, int port, const std::string& path,
                     const std::function<bool()>& shouldStop,
                     const std::function<void()>& onTick = nullptr);

    const std::string& lastError() const { return last_error_; }

private:
    std::string last_error_;
};
