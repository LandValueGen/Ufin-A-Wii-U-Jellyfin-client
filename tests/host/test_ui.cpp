// Renders the menu screens into an in-memory surface at both Wii U
// screen sizes and with both text-grid geometries we know about (the
// defaults, and Cemu's 16x24 grid at the screen edge), and checks the
// layout: bands where they should be, selection highlight under the
// selected row, text inside the grid (so the OSScreen font never wraps
// a line back over itself), progress bar proportions.
#include "check.h"
#include "ui/screens.h"

#include <vector>

struct TextCall {
    int column;
    int row;
    std::string text;
};

class MemorySurface : public ui::Surface {
public:
    MemorySurface(int w, int h) : w_(w), h_(h), pixels_((size_t)w * h, 0) {}
    int width() const override { return w_; }
    int height() const override { return h_; }
    void clear(uint32_t rgba) override {
        std::fill(pixels_.begin(), pixels_.end(), rgba);
        texts.clear();
        fills = 0;
    }
    void fillRect(int x, int y, int w, int h, uint32_t rgba) override {
        fills++;
        for (int py = std::max(0, y); py < std::min(h_, y + h); py++)
            for (int px = std::max(0, x); px < std::min(w_, x + w); px++)
                pixels_[(size_t)py * w_ + px] = rgba;
    }
    void drawText(int column, int row, const std::string& text) override {
        texts.push_back({column, row, text});
    }
    uint32_t at(int x, int y) const { return pixels_[(size_t)y * w_ + x]; }

    std::vector<TextCall> texts;
    int fills = 0;

private:
    int w_, h_;
    std::vector<uint32_t> pixels_;
};

static ui::GridMetrics defaultGrid() { return ui::GridMetrics(); }

static ui::GridMetrics cemuGrid() {
    ui::GridMetrics g;
    g.originX = 0;
    g.originY = 0;
    g.cellW = 16;
    g.cellH = 24;
    g.measured = true;
    return g;
}

// Every text call must stay inside the usable grid: starts at or after
// leftColumn(), ends before leftColumn()+columns(), and the last cell
// is still on screen.
static void checkTextInGrid(const MemorySurface& s) {
    for (const TextCall& t : s.texts) {
        CHECK(t.column >= s.leftColumn());
        CHECK(t.row >= 0 && t.row < s.rows());
        CHECK((int)t.text.size() + (t.column - s.leftColumn()) <= s.columns());
        CHECK(s.columnLeft(t.column + (int)t.text.size()) <= s.width());
        CHECK(s.rowTop(t.row) + s.cellH() <= s.height());
    }
}

static void testHelpers() {
    ui::ListWindow w = ui::computeListWindow(5, 2, 10);
    CHECK_EQ(w.start, 0); CHECK_EQ(w.end, 5);
    w = ui::computeListWindow(100, 0, 24);
    CHECK_EQ(w.start, 0); CHECK_EQ(w.end, 24);
    w = ui::computeListWindow(100, 50, 24);
    CHECK_EQ(w.start, 38); CHECK_EQ(w.end, 62);
    w = ui::computeListWindow(100, 99, 24);
    CHECK_EQ(w.start, 76); CHECK_EQ(w.end, 100);
    w = ui::computeListWindow(100, 12, 24);
    CHECK_EQ(w.start, 0); CHECK_EQ(w.end, 24);
    w = ui::computeListWindow(0, 0, 24);
    CHECK_EQ(w.start, 0); CHECK_EQ(w.end, 0);
    w = ui::computeListWindow(30, 45, 10); // selection out of range is clamped
    CHECK_EQ(w.start, 20); CHECK_EQ(w.end, 30);

    CHECK_EQ(ui::listRowsAvailable(28), 23);
    CHECK_EQ(ui::listRowsAvailable(18), 13);
    CHECK_EQ(ui::listRowsAvailable(3), 1);

    CHECK_STR(ui::fitText("h\xc3\xa9llo", 20), "hello");             // Latin-1 e-acute folds to e
    CHECK_STR(ui::fitText("Citt\xc3\xa0 Perch\xc3\xa9", 20), "Citta Perche");
    CHECK_STR(ui::fitText("caf\xc3\xa9 \xe2\x99\xaa", 20), "cafe ?"); // 3-byte note -> ?
    CHECK_STR(ui::fitText("\xc5\x81\xc3\xb3" "d\xc5\xba", 20), "?od?");  // non-Latin-1 2-byte -> ?
    CHECK_STR(ui::fitText("tab\there", 20), "tab here");
    CHECK_STR(ui::fitText("abcdefghij", 6), "abc...");
    CHECK_STR(ui::fitText("abcdefghij", 3), "abc");
    CHECK_STR(ui::fitText("abcdef", 6), "abcdef");
    CHECK_STR(ui::fitText("abc", 0), "");

    CHECK_STR(ui::fitTextTail("Film > Saga > Part 2", 12), "... > Part 2");
    CHECK_STR(ui::fitTextTail("short", 12), "short");

    std::vector<std::string> lines = ui::wrapText("the quick brown fox jumps", 10);
    CHECK_EQ((int)lines.size(), 3);
    CHECK_STR(lines[0], "the quick");
    CHECK_STR(lines[1], "brown fox");
    CHECK_STR(lines[2], "jumps");
    lines = ui::wrapText("HttpStreamReader::open", 8);
    CHECK_EQ((int)lines.size(), 3);
    CHECK_STR(lines[0], "HttpStre");
    CHECK_STR(lines[2], "::open");
    lines = ui::wrapText("", 10);
    CHECK_EQ((int)lines.size(), 1);
    CHECK_STR(lines[0], "");
    for (const std::string& l : ui::wrapText("a b cc ddd eeee fffff gggggg", 5)) CHECK((int)l.size() <= 5);

    CHECK_STR(ui::formatTime(0), "0:00");
    CHECK_STR(ui::formatTime(65), "1:05");
    CHECK_STR(ui::formatTime(3725), "1:02:05");
    CHECK_STR(ui::formatTime(-4), "0:00");
    CHECK_STR(ui::formatTime(59.6), "1:00");

    CHECK_STR(ui::justify("left", "R", 10), "left     R");
    CHECK_EQ((int)ui::justify("a very long left side text", "Movie", 20).size(), 20);
    CHECK_STR(ui::justify("left", "", 6), "left  ");
}

static void testGridGeometry() {
    // Defaults: grid starts 50 px in, no extra margin column needed.
    MemorySurface tv(1280, 720);
    tv.setGrid(defaultGrid());
    CHECK_EQ(tv.leftColumn(), 0);
    CHECK_EQ(tv.columns(), 98);

    // Cemu: grid starts at the edge with 16 px glyphs -- skip columns for
    // a margin and never claim more columns than actually fit.
    MemorySurface cemu(1280, 720);
    cemu.setGrid(cemuGrid());
    CHECK_EQ(cemu.leftColumn(), 2);
    CHECK_EQ(cemu.columns(), 76);
    CHECK(cemu.contentRight() <= 1280 - 24);
    CHECK_EQ(cemu.rows(), 30);

    MemorySurface pad(854, 480);
    pad.setGrid(cemuGrid());
    CHECK_EQ(pad.columns(), 49);
    CHECK_EQ(pad.rows(), 20);
}

static void testListScreen(int width, int height, const ui::GridMetrics& grid) {
    MemorySurface s(width, height);
    s.setGrid(grid);
    ui::ListScreenModel model;
    model.title = "Ufin";
    model.location = "Movies";
    model.footer = "hints";
    for (int i = 0; i < 50; i++) {
        model.items.push_back({"Movie number " + std::to_string(i), "1:52:00",
                               "Movie number " + std::to_string(i) + "  -  details"});
    }
    model.selectedIndex = 30;
    ui::drawListScreen(s, model);

    const int visible = ui::listRowsAvailable(s.rows());
    ui::ListWindow w = ui::computeListWindow(50, 30, visible);
    const int probeX = s.contentLeft() + 4;

    // Header band covers the top-left corner and the first text row,
    // with the accent rule right under it.
    CHECK_EQ(s.at(5, 5), ui::color::HEADER);
    CHECK_EQ(s.at(width / 2, s.rowTop(0) + 5), ui::color::HEADER);
    CHECK_EQ(s.at(width / 2, s.rowTop(1)), ui::color::ACCENT);
    // Footer band covers the bottom edge.
    CHECK_EQ(s.at(width / 2, height - 1), ui::color::FOOTER);

    // Selection band sits exactly under the selected row's cell, other
    // rows are plain.
    int selectedRow = ui::CONTENT_FIRST_ROW + (30 - w.start);
    CHECK_EQ(s.at(probeX, s.rowTop(selectedRow) + 1), ui::color::SELECTION);
    CHECK_EQ(s.at(probeX, s.rowTop(selectedRow) + s.cellH() - 2), ui::color::SELECTION);
    CHECK_EQ(s.at(probeX, s.rowTop(selectedRow - 1) + 5), ui::color::BACKGROUND);
    CHECK_EQ(s.at(probeX, s.rowTop(selectedRow + 1) + 5), ui::color::BACKGROUND);

    // Scrollbar present at the right edge of the text area.
    int trackX = s.contentRight() - 3;
    int trackY = s.rowTop(ui::CONTENT_FIRST_ROW) + 2;
    bool scrollbar = s.at(trackX, trackY) == ui::color::TRACK || s.at(trackX, trackY) == ui::color::SCROLL_THUMB;
    CHECK(scrollbar);

    // Detail line for the selection, on its panel, above the footer.
    int detailRow = ui::CONTENT_FIRST_ROW + visible;
    CHECK(detailRow < s.rows() - 1);
    CHECK_EQ(s.at(probeX, s.rowTop(detailRow) + 2), ui::color::PANEL);

    // Text: header, one per visible row, detail, footer.
    CHECK_EQ((int)s.texts.size(), 3 + visible);
    checkTextInGrid(s);
    int selectedTexts = 0, detailTexts = 0;
    for (const TextCall& t : s.texts) {
        if (t.text.rfind("> ", 0) == 0) {
            selectedTexts++;
            CHECK_EQ(t.row, selectedRow);
            CHECK(t.text.find("Movie number 30") != std::string::npos);
            CHECK(t.text.find("1:52:00") != std::string::npos);
        }
        if (t.row == detailRow) {
            detailTexts++;
            CHECK(t.text.find("Movie number 30  -  details") == 0);
        }
    }
    CHECK_EQ(selectedTexts, 1);
    CHECK_EQ(detailTexts, 1);
    int headerTexts = 0, footerTexts = 0;
    for (const TextCall& t : s.texts) {
        if (t.row == 0) {
            headerTexts++;
            CHECK(t.text.rfind("Ufin  |  Movies", 0) == 0);
            CHECK(t.text.find("31 / 50") != std::string::npos); // counter, right side
            CHECK_EQ((int)t.text.size(), s.columns());
        }
        if (t.row == s.rows() - 1) { footerTexts++; CHECK_STR(t.text, "hints"); }
    }
    CHECK_EQ(headerTexts, 1);
    CHECK_EQ(footerTexts, 1);

    // A deep breadcrumb keeps its end.
    model.location = std::string(200, 'x') + " > Last";
    ui::drawListScreen(s, model);
    for (const TextCall& t : s.texts) {
        if (t.row == 0) CHECK(t.text.find("> Last") != std::string::npos);
    }
    checkTextInGrid(s);
    model.location = "Movies";

    // Short list: no scrollbar (probe a non-selected row so the selection
    // band can't be mistaken for it), selection on the first row.
    model.items.resize(3);
    model.selectedIndex = 0;
    ui::drawListScreen(s, model);
    CHECK_EQ(s.at(trackX, s.rowTop(ui::CONTENT_FIRST_ROW + 1) + 2), ui::color::BACKGROUND);
    CHECK_EQ(s.at(probeX, s.rowTop(ui::CONTENT_FIRST_ROW) + 5), ui::color::SELECTION);
    CHECK_EQ((int)s.texts.size(), 3 + 3);
    checkTextInGrid(s);

    // Empty list shows the placeholder, no counter.
    model.items.clear();
    ui::drawListScreen(s, model);
    bool placeholder = false;
    for (const TextCall& t : s.texts) {
        if (t.text == model.emptyMessage) placeholder = true;
        if (t.row == 0) CHECK(t.text.find(" / ") == std::string::npos);
    }
    CHECK(placeholder);
    checkTextInGrid(s);
}

static void testMessageScreen(const ui::GridMetrics& grid) {
    MemorySurface s(1280, 720);
    s.setGrid(grid);
    ui::MessageScreenModel model;
    model.title = "Ufin  |  Error";
    model.lines = {"Something broke", "", "Check the config"};
    model.footer = "B: back";
    model.isError = true;
    ui::drawMessageScreen(s, model);
    CHECK_EQ(s.at(5, 5), ui::color::HEADER_ERROR);
    CHECK_EQ((int)s.texts.size(), 1 + 2 + 1); // header, 2 non-empty lines, footer
    std::string first, second;
    int firstRow = -1, secondRow = -1;
    for (const TextCall& t : s.texts) {
        if (t.text == "Something broke") { first = t.text; firstRow = t.row; }
        if (t.text == "Check the config") { second = t.text; secondRow = t.row; }
    }
    CHECK_EQ(firstRow, ui::CONTENT_FIRST_ROW + 1);
    CHECK_EQ(secondRow, firstRow + 2); // the blank line is a gap, not a draw call
    checkTextInGrid(s);

    // A long error is wrapped over several rows instead of running off
    // the edge (which on Cemu wrapped back over the start of the row).
    std::string longError = "Playback error: HttpStreamIO::open failed: HttpStreamReader::open failed: "
                            "server returned status 500 (expected 200 for a live transcode stream)";
    model.lines = {longError};
    ui::drawMessageScreen(s, model);
    checkTextInGrid(s);
    std::string joined;
    int bodyRows = 0;
    for (const TextCall& t : s.texts) {
        if (t.row > 0 && t.row < s.rows() - 1) {
            bodyRows++;
            joined += (joined.empty() ? "" : " ") + t.text;
        }
    }
    CHECK(bodyRows >= 2);
    CHECK(joined.find("status 500") != std::string::npos);

    model.isError = false;
    ui::drawMessageScreen(s, model);
    CHECK_EQ(s.at(5, 5), ui::color::HEADER);

    // More lines than rows: never draws into the footer.
    model.lines.assign(60, "line");
    ui::drawMessageScreen(s, model);
    for (const TextCall& t : s.texts) {
        if (t.text == "line") CHECK(t.row <= s.rows() - 3);
    }
}

static void testNowPlaying(const ui::GridMetrics& grid) {
    MemorySurface s(854, 480);
    s.setGrid(grid);
    ui::NowPlayingModel model;
    model.title = "Song";
    model.subtitle = "Audio";
    model.positionSeconds = 25;
    model.durationSeconds = 100;
    model.footer = "B: stop";
    ui::drawNowPlayingScreen(s, model);

    int barRow = ui::CONTENT_FIRST_ROW + 1 + 3;
    int barX = s.columnLeft(s.leftColumn() + 1);
    int barW = s.contentRight() - s.cellW() - barX;
    int barY = s.rowTop(barRow) + s.cellH() / 2;
    CHECK_EQ(s.at(barX + 2, barY), ui::color::PROGRESS);
    CHECK_EQ(s.at(barX + barW / 4 - 4, barY), ui::color::PROGRESS);
    CHECK_EQ(s.at(barX + barW / 4 + 4, barY), ui::color::TRACK);
    CHECK_EQ(s.at(barX + barW - 2, barY), ui::color::TRACK);
    checkTextInGrid(s);

    bool timeText = false;
    for (const TextCall& t : s.texts) {
        if (t.text.rfind("0:25", 0) == 0 && t.text.find("1:40") != std::string::npos) timeText = true;
    }
    CHECK(timeText);

    // Unknown duration: elapsed only, a marker somewhere on the track.
    model.durationSeconds = 0;
    ui::drawNowPlayingScreen(s, model);
    timeText = false;
    for (const TextCall& t : s.texts) if (t.text == "0:25") timeText = true;
    CHECK(timeText);
    int markers = 0;
    for (int x = barX; x < barX + barW; x++) if (s.at(x, barY) == ui::color::PROGRESS) markers++;
    CHECK(markers > 0 && markers < barW / 4);

    // Position past the end clamps to a full bar.
    model.durationSeconds = 10;
    model.positionSeconds = 50;
    ui::drawNowPlayingScreen(s, model);
    CHECK_EQ(s.at(barX + barW - 2, barY), ui::color::PROGRESS);
}

int main() {
    testHelpers();
    testGridGeometry();
    for (const ui::GridMetrics& g : {defaultGrid(), cemuGrid()}) {
        testListScreen(1280, 720, g); // TV
        testListScreen(854, 480, g);  // GamePad
        testMessageScreen(g);
        testNowPlaying(g);
    }
    return check::finish("test_ui");
}
