#include "screens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {

int listRowsAvailable(int rows) {
    // header + rule (2) and detail + spacer + footer (3)
    int available = rows - 5;
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

// Plain-ASCII stand-ins for U+00C0..U+00FF (Latin-1 letters), so
// "Citt\u00e0" shows as "Citta" instead of "Citt?". '?' where there's no
// sensible single letter.
static const char LATIN1_FOLD[64 + 1] =
    "AAAAAAACEEEEIIII"   // C0-CF
    "DNOOOOOxOUUUUYTs"   // D0-DF
    "aaaaaaaceeeeiiii"   // E0-EF
    "dnooooo/ouuuuyty";  // F0-FF

static std::string toAscii(const std::string& text) {
    std::string ascii;
    ascii.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 0x80) {
            if (c >= 0xC0) {
                // Lead byte of a sequence; fold 2-byte Latin-1 letters.
                if ((c == 0xC3) && i + 1 < text.size()) {
                    unsigned char c2 = (unsigned char)text[i + 1];
                    if (c2 >= 0x80 && c2 <= 0xBF) {
                        ascii += LATIN1_FOLD[c2 - 0x80];
                        i++;
                        continue;
                    }
                }
                ascii += '?'; // one placeholder per code point
            }
            // continuation bytes (0x80..0xBF) are dropped
        } else if (c < 0x20 || c == 0x7F) {
            ascii += ' ';
        } else {
            ascii += (char)c;
        }
    }
    return ascii;
}

std::string fitText(const std::string& text, int maxColumns) {
    std::string ascii = toAscii(text);
    if (maxColumns <= 0) return "";
    if ((int)ascii.size() <= maxColumns) return ascii;
    if (maxColumns <= 3) return ascii.substr(0, (size_t)maxColumns);
    return ascii.substr(0, (size_t)maxColumns - 3) + "...";
}

std::string fitTextTail(const std::string& text, int maxColumns) {
    std::string ascii = toAscii(text);
    if (maxColumns <= 0) return "";
    if ((int)ascii.size() <= maxColumns) return ascii;
    if (maxColumns <= 3) return ascii.substr(ascii.size() - (size_t)maxColumns);
    return "..." + ascii.substr(ascii.size() - (size_t)(maxColumns - 3));
}

std::vector<std::string> wrapText(const std::string& text, int columns) {
    std::vector<std::string> lines;
    std::string ascii = toAscii(text);
    if (columns <= 0) {
        lines.push_back("");
        return lines;
    }
    std::string line;
    size_t i = 0;
    while (i < ascii.size()) {
        // next word
        while (i < ascii.size() && ascii[i] == ' ') i++;
        size_t j = i;
        while (j < ascii.size() && ascii[j] != ' ') j++;
        if (i >= ascii.size()) break;
        std::string word = ascii.substr(i, j - i);
        i = j;

        while ((int)word.size() > columns) {
            // Too long for any line: flush, then hard-split.
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
            }
            lines.push_back(word.substr(0, (size_t)columns));
            word = word.substr((size_t)columns);
        }
        if (word.empty()) continue;
        if (line.empty()) {
            line = word;
        } else if ((int)(line.size() + 1 + word.size()) <= columns) {
            line += " " + word;
        } else {
            lines.push_back(line);
            line = word;
        }
    }
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
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

// --- shared chrome ---

static void drawHeader(Surface& s, const std::string& title, const std::string& location,
                       const std::string& counter, bool isError) {
    // Solid band from the very top of the screen through row 0, then a
    // thin accent rule where row 1 starts.
    int bandBottom = s.rowTop(1);
    s.fillRect(0, 0, s.width(), bandBottom, isError ? color::HEADER_ERROR : color::HEADER);
    s.fillRect(0, bandBottom, s.width(), 3, isError ? color::WHITE : color::ACCENT);

    const int columns = s.columns();
    std::string right = fitText(counter, columns / 3);
    int leftRoom = columns - (int)right.size() - (right.empty() ? 0 : 2);

    std::string left = toAscii(title);
    if (!location.empty() && leftRoom > (int)left.size() + 4) {
        left += "  |  " + fitTextTail(location, leftRoom - (int)left.size() - 5);
    }
    s.drawText(s.leftColumn(), 0, justify(left, right, columns));
}

static void drawFooter(Surface& s, const std::string& hints) {
    int row = s.rows() - 1;
    int top = s.rowTop(row);
    s.fillRect(0, top, s.width(), s.height() - top, color::FOOTER);
    s.fillRect(0, top, s.width(), 2, color::HEADER);
    if (!hints.empty()) s.drawText(s.leftColumn(), row, fitText(hints, s.columns()));
}

// --- screens ---

void drawListScreen(Surface& s, const ListScreenModel& model) {
    s.clear(color::BACKGROUND);

    const int total = (int)model.items.size();
    char counter[32] = "";
    if (total > 0) {
        snprintf(counter, sizeof(counter), "%d / %d", model.selectedIndex + 1, total);
    }
    drawHeader(s, model.title, model.location, counter, false);
    drawFooter(s, model.footer);

    const int col0 = s.leftColumn();
    const int columns = s.columns();
    const int visible = listRowsAvailable(s.rows());
    const int detailRow = CONTENT_FIRST_ROW + visible;

    if (total == 0) {
        s.drawText(col0 + 2, CONTENT_FIRST_ROW + 1, fitText(model.emptyMessage, columns - 2));
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
        s.drawText(col0, row, justify(line, e.tag, textColumns));
    }

    if (scrollable) {
        // Scrollbar: a track spanning the content rows, with a thumb whose
        // size and position mirror the visible window.
        int trackWidth = 6;
        int trackX = s.contentRight() - trackWidth;
        int trackTop = s.rowTop(CONTENT_FIRST_ROW);
        int trackHeight = visible * s.cellH();
        s.fillRect(trackX, trackTop, trackWidth, trackHeight, color::TRACK);

        int thumbHeight = std::max(s.cellH() / 2, (trackHeight * visible) / total);
        int maxThumbTop = trackHeight - thumbHeight;
        int thumbTop = (total - visible) > 0
            ? (maxThumbTop * window.start) / (total - visible)
            : 0;
        s.fillRect(trackX, trackTop + thumbTop, trackWidth, thumbHeight, color::SCROLL_THUMB);
    }

    // Detail line for the selection, on a panel just above the footer.
    const ListEntry& sel = model.items[(size_t)std::min(std::max(model.selectedIndex, 0), total - 1)];
    if (!sel.detail.empty() && detailRow < s.rows() - 1) {
        s.fillRowBand(detailRow, color::PANEL);
        s.drawText(col0, detailRow, fitText(sel.detail, columns));
    }
}

void drawMessageScreen(Surface& s, const MessageScreenModel& model) {
    s.clear(color::BACKGROUND);
    drawHeader(s, model.title, "", "", model.isError);
    drawFooter(s, model.footer);

    const int col0 = s.leftColumn();
    const int columns = s.columns();
    int row = CONTENT_FIRST_ROW + 1;
    const int lastRow = s.rows() - 3;
    for (const std::string& line : model.lines) {
        if (line.empty()) {
            row++;
            continue;
        }
        for (const std::string& wrapped : wrapText(line, columns - 2)) {
            if (row > lastRow) return;
            s.drawText(col0 + 1, row, wrapped);
            row++;
        }
    }
}

void drawNowPlayingScreen(Surface& s, const NowPlayingModel& model) {
    s.clear(color::BACKGROUND);
    drawHeader(s, "Ufin", "Now Playing", "", false);
    drawFooter(s, model.footer);

    const int col0 = s.leftColumn();
    const int columns = s.columns();

    // Title block on a panel, then the progress bar and times below it.
    int titleRow = CONTENT_FIRST_ROW + 1;
    s.fillRowBand(titleRow, color::PANEL);
    s.fillRowBand(titleRow + 1, color::PANEL);
    s.drawText(col0 + 1, titleRow, fitText(model.title, columns - 2));
    s.drawText(col0 + 1, titleRow + 1, fitText(model.subtitle, columns - 2));

    int barRow = titleRow + 3;
    int barX = s.columnLeft(col0 + 1);
    int barW = s.contentRight() - s.cellW() - barX;
    if (barW < 16) barW = 16;
    int barH = s.cellH() / 2;
    int barY = s.rowTop(barRow) + (s.cellH() - barH) / 2;
    s.fillRect(barX, barY, barW, barH, color::TRACK);

    std::string timeText;
    if (model.durationSeconds > 0.0) {
        double fraction = model.positionSeconds / model.durationSeconds;
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 1.0) fraction = 1.0;
        int fillW = (int)std::lround(barW * fraction);
        if (fillW > 0) s.fillRect(barX, barY, fillW, barH, color::PROGRESS);
        timeText = justify(formatTime(model.positionSeconds), formatTime(model.durationSeconds), columns - 2);
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
    s.drawText(col0 + 1, barRow + 1, fitText(timeText, columns - 2));
}

} // namespace ui
