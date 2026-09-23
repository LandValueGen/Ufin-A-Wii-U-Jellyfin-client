// ui screens -- the actual layouts Ufin shows: the library/item list,
// message screens (connecting / loading / error), and the audio Now
// Playing screen. Each takes a plain data model and draws it onto any
// ui::Surface, so the same code lays out the TV and the GamePad (which
// have different grid sizes) and can be tested on a desktop.
//
// Layout shared by every screen (text rows):
//   row 0              header band: app name, breadcrumb, counter
//   row 1              accent rule under the header (no text)
//   rows 2 .. rows-4   content
//   row rows-3         detail line (list screen: info on the selection)
//   row rows-2         spacer
//   row rows-1         footer band: button hints
//
// Nothing in here knows about Jellyfin, VPAD, or OSScreen.

#pragma once
#include "surface.h"

#include <string>
#include <vector>

namespace ui {

struct ListEntry {
    std::string name;
    std::string tag;     // right-aligned, e.g. "1:52:00" or "S2E1"
    std::string detail;  // shown on the detail line while selected
};

struct ListScreenModel {
    std::string title;             // header, left ("Ufin")
    std::string location;          // breadcrumb ("Film > Saga")
    std::vector<ListEntry> items;
    int selectedIndex = 0;
    std::string emptyMessage = "Nothing here yet.";
    std::string footer;            // button hints
};

struct MessageScreenModel {
    std::string title;
    std::vector<std::string> lines; // word-wrapped to the screen; "" = blank line
    std::string footer;
    bool isError = false;           // red header band
};

struct NowPlayingModel {
    std::string title;             // track / item name
    std::string subtitle;          // e.g. "Audio  |  Transcode"
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;  // 0 = unknown, progress bar becomes a moving marker
    std::string footer;
};

void drawListScreen(Surface& s, const ListScreenModel& model);
void drawMessageScreen(Surface& s, const MessageScreenModel& model);
void drawNowPlayingScreen(Surface& s, const NowPlayingModel& model);

// --- layout helpers, exposed for tests ---

// First content row, and how many list rows fit on a surface of `rows`
// text rows once header, rule, detail line, spacer and footer are
// accounted for.
static const int CONTENT_FIRST_ROW = 2;
int listRowsAvailable(int rows);

// Which slice [start, end) of `total` items is visible when `selected`
// must be on screen and `visible` rows are available. Keeps the
// selection roughly centred and never scrolls past either end.
struct ListWindow {
    int start = 0;
    int end = 0;
};
ListWindow computeListWindow(int total, int selected, int visible);

// Makes a string safe and short enough for the OSScreen font: each
// non-ASCII UTF-8 sequence becomes a single '?' (common accented Latin
// letters become their plain letter), control characters a space, and
// anything longer than maxColumns is cut with "...".
std::string fitText(const std::string& text, int maxColumns);

// Same, but keeps the END of the text ("...Saga > Part 2") -- for
// breadcrumbs, where the deepest level matters most.
std::string fitTextTail(const std::string& text, int maxColumns);

// Word-wraps text into lines of at most `columns` characters. Words
// longer than a line are split. Empty input gives one empty line.
std::vector<std::string> wrapText(const std::string& text, int columns);

// "m:ss" or "h:mm:ss".
std::string formatTime(double seconds);

// Left text and right text on one row, padded to `columns`.
std::string justify(const std::string& left, const std::string& right, int columns);

} // namespace ui
