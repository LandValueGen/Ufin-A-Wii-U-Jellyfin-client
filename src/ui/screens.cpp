#include "screens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {

// Row layout shared by every screen:
//   row 0            header band (title + location)
//   row 1            blank
//   row 2 .. rows-3  content
//   row rows-2       blank
//   row rows-1       footer band (button hints)
static const int CONTENT_FIRST_ROW = 2;

int listRowsAvailable(int rows) {
    int available = rows - 4;
    return available < 1 ? 1 : available;
}

ListWindow computeListWindow(int total, int selected, int visible) {
    ListWindow w;
    if (total <= 0 || visible <= 0) return w;
    if (selected < 0) selected = 0;
    if (selected >= total) selected = total - 1;

    if (total <= visible) {
        w.start = 0;
        w.end = total;
        return w;
    }

    int start = selected - visible / 2;
    if (start < 0) start = 0;
    if (start > total - visible) start = total - visible;
    w.start = start;
    w.end = start + visible;
    return w;
}

std::string fitText(const std::string& text, int maxColumns) {
    // Normalise to printable ASCII first: OSScreen's font only has the
    // 7-bit printable range, and would otherwise render each byte of a
    // UTF-8 sequence as a different wrong glyph.
    std::string ascii;
    ascii.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 0x80) {
            if (c >= 0xC0) ascii += '?'; // lead byte: one placeholder per code point
            // continuation bytes (0x80..0xBF) are dropped
        } else if (c < 0x20 || c == 0x7F) {
            ascii += ' ';
        } else {
            ascii += (char)c;
        }
    }

    if (maxColumns <= 0) return "";
    if ((int)ascii.size() <= maxColumns) return ascii;
    if (maxColumns <= 3) return ascii.substr(0, (size_t)maxColumns);
    return ascii.substr(0, (size_t)maxColumns - 3) + "...";
}

std::string formatTime(double seconds) {
    if (!(seconds >= 0.0) || std::isinf(seconds)) seconds = 0.0;
    long long total = (long long)(seconds + 0.5);
    long long h = total / 3600;
    long long m = (total / 60) % 60;
    long long s = total % 60;
    char buf[32];
    if (h > 0) {
        snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", h, m, s);
    } else {
        snprintf(buf, sizeof(buf), "%lld:%02lld", m, s);
    }
    return buf;
}

std::string justify(const std::string& left, const std::string& right, int columns) {
    if (columns <= 0) return "";
    std::string r = fitText(right, columns / 2);
    int leftMax = columns - (int)r.size() - (r.empty() ? 0 : 2);
    std::string l = fitText(left, leftMax < 0 ? 0 : leftMax);
    std::string out = l;
    int pad = columns - (int)l.size() - (int)r.size();
    if (pad < 0) pad = 0;
    out.append((size_t)pad, ' ');
    out += r;
    return out;
}

static void drawHeader(Surface& s, const std::string& title, const std::string& location, bool isError) {
    // The header band runs from the very top of the screen down through
    // row 0, so the title sits on a solid block rather than floating.
    int bandBottom = Surface::rowTop(0) + Surface::CELL_H;
    s.fillRect(0, 0, s.width(), bandBottom, isError ? color::HEADER_ERROR : color::HEADER);

    std::string text = title;
    if (!location.empty()) text += "  |  " + location;
    s.drawText(0, 0, fitText(text, s.columns()));
}

static void drawFooter(Surface& s, const std::string& hints) {
    int row = s.rows() - 1;
    int top = Surface::rowTop(row);
    s.fillRect(0, top, s.width(), s.height() - top, color::FOOTER);
    if (!hints.empty()) s.drawText(0, row, fitText(hints, s.columns()));
}

void drawListScreen(Surface& s, const ListScreenModel& model) {
    s.clear(color::BACKGROUND);
    drawHeader(s, model.title, model.location, false);
    drawFooter(s, model.footer);

    const int columns = s.columns();
    const int visible = listRowsAvailable(s.rows());
    const int total = (int)model.items.size();

    if (total == 0) {
        s.drawText(2, CONTENT_FIRST_ROW, fitText(model.emptyMessage, columns - 2));
        return;
    }

    ListWindow window = computeListWindow(total, model.selectedIndex, visible);

    // Room on the right for the scrollbar when the list doesn't fit.
    const bool scrollable = total > visible;
    const int textColumns = scrollable ? columns - 2 : columns;

    for (int i = window.start; i < window.end; i++) {
        int row = CONTENT_FIRST_ROW + (i - window.start);
        bool selected = (i == model.selectedIndex);
        if (selected) {
            s.fillRowBand(row, color::SELECTION);
        }
        const ListEntry& e = model.items[(size_t)i];
        std::string line = std::string(selected ? "> " : "  ") + e.name;
        s.drawText(0, row, justify(line, e.tag, textColumns));
    }

    if (scrollable) {
        // Scrollbar: a track spanning the content rows, with a thumb whose
        // size and position mirror the visible window.
        int trackX = s.width() - Surface::GRID_ORIGIN_X + 4;
        int trackTop = Surface::rowTop(CONTENT_FIRST_ROW);
        int trackHeight = visible * Surface::CELL_H;
        int trackWidth = 6;
        s.fillRect(trackX, trackTop, trackWidth, trackHeight, color::TRACK);

        int thumbHeight = std::max(Surface::CELL_H / 2, (trackHeight * visible) / total);
        int maxThumbTop = trackHeight - thumbHeight;
        int thumbTop = (total - visible) > 0
            ? (maxThumbTop * window.start) / (total - visible)
            : 0;
        s.fillRect(trackX, trackTop + thumbTop, trackWidth, thumbHeight, color::SCROLL_THUMB);
    }
}

void drawMessageScreen(Surface& s, const MessageScreenModel& model) {
    s.clear(color::BACKGROUND);
    drawHeader(s, model.title, "", model.isError);
    drawFooter(s, model.footer);

    int row = CONTENT_FIRST_ROW;
    int lastRow = s.rows() - 3;
    for (const std::string& line : model.lines) {
        if (row > lastRow) break;
        if (!line.empty()) s.drawText(0, row, fitText(line, s.columns()));
        row++;
    }
}

void drawNowPlayingScreen(Surface& s, const NowPlayingModel& model) {
    s.clear(color::BACKGROUND);
    drawHeader(s, "Ufin", "Now Playing", false);
    drawFooter(s, model.footer);

    const int columns = s.columns();

    // Title block on a panel, then the progress bar and times below it.
    int titleRow = CONTENT_FIRST_ROW + 1;
    s.fillRowBand(titleRow, color::PANEL);
    s.fillRowBand(titleRow + 1, color::PANEL);
    s.drawText(1, titleRow, fitText(model.title, columns - 2));
    s.drawText(1, titleRow + 1, fitText(model.subtitle, columns - 2));

    int barRow = titleRow + 4;
    int barX = Surface::columnLeft(1);
    int barW = s.width() - 2 * barX;
    int barH = Surface::CELL_H / 2;
    int barY = Surface::rowTop(barRow) + (Surface::CELL_H - barH) / 2;
    s.fillRect(barX, barY, barW, barH, color::TRACK);

    std::string timeText;
    if (model.durationSeconds > 0.0) {
        double fraction = model.positionSeconds / model.durationSeconds;
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 1.0) fraction = 1.0;
        int fillW = (int)std::lround(barW * fraction);
        if (fillW > 0) s.fillRect(barX, barY, fillW, barH, color::PROGRESS);
        timeText = formatTime(model.positionSeconds) + " / " + formatTime(model.durationSeconds);
    } else {
        // Unknown duration: show elapsed time only, and a small marker
        // that moves so the screen visibly isn't frozen.
        int markerW = std::max(8, barW / 20);
        int span = barW - markerW;
        int cycle = 8; // seconds for one sweep
        double phase = std::fmod(model.positionSeconds, (double)cycle) / cycle;
        s.fillRect(barX + (int)(span * phase), barY, markerW, barH, color::PROGRESS);
        timeText = formatTime(model.positionSeconds);
    }
    s.drawText(1, barRow + 1, fitText(timeText, columns - 2));
}

} // namespace ui
