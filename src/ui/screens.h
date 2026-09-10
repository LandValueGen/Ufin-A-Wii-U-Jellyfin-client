// ui screens -- the actual layouts Ufin shows: the library/item list,
// message screens (connecting / loading / error), and the audio Now
// Playing screen. Each takes a plain data model and draws it onto any
// ui::Surface, so the same code lays out the TV and the GamePad (which
// have different grid sizes) and can be tested on a desktop.
//
// Nothing in here knows about Jellyfin, VPAD, or OSScreen.

#pragma once
#include "surface.h"

#include <string>
#include <vector>

namespace ui {

struct ListEntry {
    std::string name;
    std::string tag;   // shown right-aligned, e.g. the Jellyfin item type
};

struct ListScreenModel {
    std::string title;             // header, left ("Ufin")
    std::string location;          // header, after the title ("Movies")
    std::vector<ListEntry> items;
    int selectedIndex = 0;
    std::string emptyMessage = "(empty)";
    std::string footer;            // button hints
};

struct MessageScreenModel {
    std::string title;
    std::vector<std::string> lines;
    std::string footer;
    bool isError = false;          // red header band
};

struct NowPlayingModel {
    std::string title;             // track / item name
    std::string subtitle;          // e.g. "Audio  |  Transcode"
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;  // 0 = unknown, progress bar hidden
    std::string footer;
};

void drawListScreen(Surface& s, const ListScreenModel& model);
void drawMessageScreen(Surface& s, const MessageScreenModel& model);
void drawNowPlayingScreen(Surface& s, const NowPlayingModel& model);

// --- layout helpers, exposed for tests ---

// Which slice [start, end) of `total` items is visible when `selected`
// must be on screen and `visible` rows are available. Keeps the
// selection roughly centred and never scrolls past either end.
struct ListWindow {
    int start = 0;
    int end = 0;
};
ListWindow computeListWindow(int total, int selected, int visible);

// Number of list rows a surface of `rows` text rows can show once the
// header, spacing and footer are accounted for.
int listRowsAvailable(int rows);

// Makes a string safe and short enough for the OSScreen font: each
// non-ASCII UTF-8 sequence becomes a single '?', control characters a
// space, and anything longer than maxColumns is cut with "...".
std::string fitText(const std::string& text, int maxColumns);

// "m:ss" or "h:mm:ss".
std::string formatTime(double seconds);

// Left text and right text on one row, padded to `columns`.
std::string justify(const std::string& left, const std::string& right, int columns);

} // namespace ui
