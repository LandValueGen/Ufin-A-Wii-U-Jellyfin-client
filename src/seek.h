// Where a skip lands: position + delta, kept inside the item. The upper
// limit stays a few seconds short of the end, so skipping forward near
// the end still shows the last moments instead of a stream that ends
// before its first frame.
#pragma once
#include <cmath>

inline double clampSeek(double position, double delta, double duration) {
    double target = position + delta;
    if (target < 0.0) target = 0.0;
    if (duration > 0.0) {
        double latest = duration - 5.0;
        if (latest < 0.0) latest = 0.0;
        if (target > latest) target = latest;
    }
    return target;
}

// Converts a stream-clock time to a position in the item. A transcode
// started at StartTimeTicks usually has timestamps starting at 0 (so the
// offset has to be added), but some FFmpeg setups keep the original
// timestamps; the first timestamp seen tells them apart.
inline double itemPositionFromStreamTime(double streamTime, double startOffset, double firstStreamTime) {
    if (std::isnan(streamTime)) return NAN;
    if (startOffset <= 0.0) return streamTime;
    if (!std::isnan(firstStreamTime) && firstStreamTime >= startOffset - 2.0) return streamTime;
    return startOffset + streamTime;
}
