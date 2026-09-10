// Renders the menu screens into an in-memory surface at both Wii U
// screen sizes and checks the layout: bands where they should be,
// selection highlight under the selected row, text within the grid,
// progress bar proportions.
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

    CHECK_EQ(ui::listRowsAvailable(28), 24);
    CHECK_EQ(ui::listRowsAvailable(18), 14);
    CHECK_EQ(ui::listRowsAvailable(3), 1);

    CHECK_STR(ui::fitText("h\xc3\xa9llo", 20), "h?llo");          // UTF-8 e-acute -> ?
    CHECK_STR(ui::fitText("caf\xc3\xa9 \xe2\x99\xaa", 20), "caf? ?"); // 2- and 3-byte sequences
    CHECK_STR(ui::fitText("tab\there", 20), "tab here");
    CHECK_STR(ui::fitText("abcdefghij", 6), "abc...");
    CHECK_STR(ui::fitText("abcdefghij", 3), "abc");
    CHECK_STR(ui::fitText("abcdef", 6), "abcdef");
    CHECK_STR(ui::fitText("abc", 0), "");

    CHECK_STR(ui::formatTime(0), "0:00");
    CHECK_STR(ui::formatTime(65), "1:05");
    CHECK_STR(ui::formatTime(3725), "1:02:05");
    CHECK_STR(ui::formatTime(-4), "0:00");
    CHECK_STR(ui::formatTime(59.6), "1:00");

    CHECK_STR(ui::justify("left", "R", 10), "left     R");
    CHECK_EQ((int)ui::justify("a very long left side text", "Movie", 20).size(), 20);
    CHECK_STR(ui::justify("left", "", 6), "left  ");
}

static void testListScreen(int width, int height) {
    MemorySurface s(width, height);
    ui::ListScreenModel model;
    model.title = "Ufin";
    model.location = "Movies";
    model.footer = "hints";
    for (int i = 0; i < 50; i++) {
        model.items.push_back({"Movie number " + std::to_string(i), "Movie"});
    }
    model.selectedIndex = 30;
    ui::drawListScreen(s, model);

    const int columns = s.columns();
    const int visible = ui::listRowsAvailable(s.rows());
    ui::ListWindow w = ui::computeListWindow(50, 30, visible);

    // Header band covers the top-left corner and the first text row.
    CHECK_EQ(s.at(5, 5), ui::color::HEADER);
    CHECK_EQ(s.at(width / 2, ui::Surface::rowTop(0) + 5), ui::color::HEADER);
    // Footer band covers the bottom edge.
    CHECK_EQ(s.at(width / 2, height - 1), ui::color::FOOTER);

    // Selection band sits under the selected row, other rows are plain.
    int selectedRow = 2 + (30 - w.start);
    CHECK_EQ(s.at(60, ui::Surface::rowTop(selectedRow) + 5), ui::color::SELECTION);
    CHECK_EQ(s.at(60, ui::Surface::rowTop(selectedRow - 1) + 5), ui::color::BACKGROUND);
    CHECK_EQ(s.at(60, ui::Surface::rowTop(selectedRow + 1) + 5), ui::color::BACKGROUND);

    // Scrollbar present near the right edge, within the content rows.
    int trackX = width - ui::Surface::GRID_ORIGIN_X + 4 + 2;
    int trackY = ui::Surface::rowTop(2) + 2;
    bool scrollbar = s.at(trackX, trackY) == ui::color::TRACK || s.at(trackX, trackY) == ui::color::SCROLL_THUMB;
    CHECK(scrollbar);

    // Text: header, one per visible row, footer -- and none wider than
    // the grid or outside the row range.
    CHECK_EQ((int)s.texts.size(), 2 + visible);
    int selectedTexts = 0;
    for (const TextCall& t : s.texts) {
        CHECK(t.column >= 0);
        CHECK(t.row >= 0 && t.row < s.rows());
        CHECK((int)t.text.size() + t.column <= columns);
        if (t.text.rfind("> ", 0) == 0) {
            selectedTexts++;
            CHECK_EQ(t.row, selectedRow);
            CHECK(t.text.find("Movie number 30") != std::string::npos);
            CHECK(t.text.find("Movie") != std::string::npos);
        }
    }
    CHECK_EQ(selectedTexts, 1);
    int headerTexts = 0, footerTexts = 0;
    for (const TextCall& t : s.texts) {
        if (t.row == 0) { headerTexts++; CHECK_STR(t.text, "Ufin  |  Movies"); }
        if (t.row == s.rows() - 1) { footerTexts++; CHECK_STR(t.text, "hints"); }
    }
    CHECK_EQ(headerTexts, 1);
    CHECK_EQ(footerTexts, 1);

    // Short list: no scrollbar (probe a non-selected row so the selection
    // band can't be mistaken for it), selection on the first row.
    model.items.resize(3);
    model.selectedIndex = 0;
    ui::drawListScreen(s, model);
    CHECK_EQ(s.at(trackX, ui::Surface::rowTop(3) + 2), ui::color::BACKGROUND);
    CHECK_EQ(s.at(60, ui::Surface::rowTop(2) + 5), ui::color::SELECTION);
    CHECK_EQ((int)s.texts.size(), 2 + 3);

    // Empty list shows the placeholder.
    model.items.clear();
    ui::drawListScreen(s, model);
    bool placeholder = false;
    for (const TextCall& t : s.texts) if (t.text == "(empty)") placeholder = true;
    CHECK(placeholder);
}

static void testMessageScreen() {
    MemorySurface s(1280, 720);
    ui::MessageScreenModel model;
    model.title = "Ufin  |  Error";
    model.lines = {"Something broke", "", "Check the config"};
    model.footer = "B: back";
    model.isError = true;
    ui::drawMessageScreen(s, model);
    CHECK_EQ(s.at(5, 5), ui::color::HEADER_ERROR);
    CHECK_EQ((int)s.texts.size(), 1 + 2 + 1); // header, 2 non-empty lines, footer
    std::string row2, row4;
    for (const TextCall& t : s.texts) {
        if (t.row == 2) row2 = t.text;
        if (t.row == 4) row4 = t.text;
        CHECK(t.row != 3); // the blank line produces no draw call
    }
    CHECK_STR(row2, "Something broke");
    CHECK_STR(row4, "Check the config");

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

static void testNowPlaying() {
    MemorySurface s(854, 480);
    ui::NowPlayingModel model;
    model.title = "Song";
    model.subtitle = "Audio";
    model.positionSeconds = 25;
    model.durationSeconds = 100;
    model.footer = "B: stop";
    ui::drawNowPlayingScreen(s, model);

    int barRow = 2 + 1 + 4;
    int barX = ui::Surface::columnLeft(1);
    int barW = 854 - 2 * barX;
    int barY = ui::Surface::rowTop(barRow) + (ui::Surface::CELL_H - ui::Surface::CELL_H / 2) / 2 + 2;
    CHECK_EQ(s.at(barX + 2, barY), ui::color::PROGRESS);
    CHECK_EQ(s.at(barX + barW / 4 - 4, barY), ui::color::PROGRESS);
    CHECK_EQ(s.at(barX + barW / 4 + 4, barY), ui::color::TRACK);
    CHECK_EQ(s.at(barX + barW - 2, barY), ui::color::TRACK);

    bool timeText = false;
    for (const TextCall& t : s.texts) if (t.text == "0:25 / 1:40") timeText = true;
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
    testListScreen(1280, 720); // TV
    testListScreen(854, 480);  // GamePad
    testMessageScreen();
    testNowPlaying();
    return check::finish("test_ui");
}
