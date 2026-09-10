// Ufin -- browse Jellyfin libraries with the GamePad and play video/audio
// via the media/ pipeline (HttpStreamIO -> Decoder -> VideoOutput/
// AudioOutput -> Player). Menus are drawn with the ui/ layer on top of
// OSScreen (ui/os_screen_display.h); video playback takes the display
// over with GX2 for its duration.

#include <whb/proc.h>
#include <vpad/input.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <vector>
#include <string>
#include <cstdio>
#include <cstdint>

#include "config.h"
#include "jellyfin_client.h"
#include "config_loader.h"
#include "media/player.h"
#include "media/video_output.h"
#include "ui/screens.h"
#include "ui/os_screen_display.h"

enum class Screen { CONNECTING, ERROR_SCREEN, VIEWS, ITEMS };

struct NavState {
    Screen screen = Screen::CONNECTING;
    std::vector<JellyfinItem> currentList;
    int selectedIndex = 0;
    std::string currentParentName;
    std::string errorMessage;
    bool usingDefaultConfig = false;
    std::string configLoadError;
    std::string configHost;
    int configPort = 0;
};

static OSScreenDisplay display;

static const char* LIST_HINTS = "Up/Down: move    A: open / play    B: back    ZR: GX2 test";

static void showMessage(const std::string& title, const std::vector<std::string>& lines,
                        const std::string& footer, bool isError = false) {
    ui::MessageScreenModel model;
    model.title = title;
    model.lines = lines;
    model.footer = footer;
    model.isError = isError;
    display.render([&](ui::Surface& s) { ui::drawMessageScreen(s, model); });
}

static void renderNav(const NavState& nav) {
    if (nav.screen == Screen::CONNECTING) {
        char buf[160];
        snprintf(buf, sizeof(buf), "Connecting to %s:%d ...", nav.configHost.c_str(), nav.configPort);
        std::vector<std::string> lines;
        lines.push_back(buf);
        if (nav.usingDefaultConfig) {
            lines.push_back("");
            lines.push_back(nav.configLoadError + " -- using the hardcoded config.h defaults.");
        }
        showMessage("Ufin", lines, "");
        return;
    }

    if (nav.screen == Screen::ERROR_SCREEN) {
        std::vector<std::string> lines;
        lines.push_back(nav.errorMessage);
        lines.push_back("");
        lines.push_back("Check config.json (host / port / credentials) and that the");
        lines.push_back("Wii U and the Jellyfin server are on the same network.");
        showMessage("Ufin  |  Error", lines, "B: back to libraries", true);
        return;
    }

    ui::ListScreenModel model;
    model.title = "Ufin";
    model.location = nav.currentParentName.empty() ? "Libraries" : nav.currentParentName;
    model.selectedIndex = nav.selectedIndex;
    model.footer = LIST_HINTS;
    model.items.reserve(nav.currentList.size());
    for (const JellyfinItem& item : nav.currentList) {
        ui::ListEntry e;
        e.name = item.name;
        e.tag = item.type;
        if (item.runTimeTicks > 0 && (item.type == "Audio" || item.type == "Movie" ||
                                      item.type == "Episode" || item.type == "Video")) {
            e.tag = ui::formatTime((double)item.runTimeTicks / 10000000.0) + "  " + item.type;
        }
        model.items.push_back(e);
    }
    display.render([&](ui::Surface& s) { ui::drawListScreen(s, model); });
}

// True if this item type is something Player can actually play, rather
// than a folder to browse into. "Audio" goes through
// buildAudioStreamUrl() (AAC-in-MP4, confirmed working); everything else
// through buildVideoStreamUrl() (H.264 baseline + AAC in fragmented MP4,
// decoded by the Wii U hardware decoder via h264_wiiu and drawn with the
// GX2 NV12 renderer).
static bool isPlayable(const std::string& type) {
    return type == "Movie" || type == "Episode" || type == "Video" || type == "Audio";
}

// Polled by Player many times a second. Also keeps ProcUI serviced --
// pressing HOME mid-playback otherwise leaves the system waiting on us.
static bool stopRequestedByUser() {
    if (!WHBProcIsRunning()) return true;
    VPADStatus playbackVpad;
    VPADReadError playbackErr;
    VPADRead(VPAD_CHAN_0, &playbackVpad, 1, &playbackErr);
    return (playbackErr == VPAD_READ_SUCCESS) && (playbackVpad.trigger & VPAD_BUTTON_B);
}

// Diagnostic: draws a flat magenta picture via the exact same
// shader/upload/present pipeline as real video, with no decode or
// streaming involved. Isolates whether the GX2 path can put anything on
// screen at all. B exits.
static void runGx2TestPattern() {
    display.shutdown();
    VideoOutput testOutput;
    if (testOutput.init(1280, 720, 16.0 / 9.0)) {
        bool testDone = false;
        while (!testDone && WHBProcIsRunning()) {
            testOutput.renderTestPattern();
            OSSleepTicks(OSMillisecondsToTicks(33));
            VPADStatus testVpad;
            VPADReadError testErr;
            VPADRead(VPAD_CHAN_0, &testVpad, 1, &testErr);
            if (testErr == VPAD_READ_SUCCESS && (testVpad.trigger & VPAD_BUTTON_B)) {
                testDone = true;
            }
        }
        testOutput.shutdown();
    }
    display.init();
}

// Plays one item start to finish (or until B), handling the OSScreen/GX2
// hand-off and Jellyfin progress reporting. Returns the player result and
// fills errorMessage on PlayResult::Error.
static PlayResult playItem(JellyfinClient& client, const UfinConfig& cfg, const JellyfinItem& picked,
                           std::string& errorMessage) {
    const bool isAudio = (picked.type == "Audio");

    VideoStreamOptions videoOptions;
    videoOptions.videoBitrate = cfg.videoBitrate;
    videoOptions.profile = cfg.videoProfile;

    StreamTarget target = isAudio
        ? client.buildAudioStreamUrl(picked.id)
        : client.buildVideoStreamUrl(picked.id, videoOptions);

    double durationSeconds = picked.runTimeTicks > 0 ? picked.runTimeTicks / 10000000.0 : 0.0;

    PlayOptions playOptions;
    {
        std::vector<std::string> lines;
        lines.push_back(picked.name);
        lines.push_back("");
        lines.push_back("Waiting for the server to start streaming...");
        showMessage("Ufin  |  Loading", lines, "B: cancel");
    }

    if (!isAudio) {
        // Jellyfin is asked to encode every video at exactly 1280x720
        // (see buildVideoStreamUrl for why), so the real shape of the
        // picture has to come from the item's metadata. If we can't get
        // it, assume 16:9.
        VideoInfo info;
        if (client.getVideoInfo(picked.id, info) && info.displayAspect > 0.0) {
            playOptions.displayAspect = info.displayAspect;
            if (info.runTimeTicks > 0) durationSeconds = info.runTimeTicks / 10000000.0;
        } else {
            playOptions.displayAspect = 16.0 / 9.0;
        }

        // OSScreen and GX2 both drive the same display hardware; both
        // active at once caused a hard OSFatal hang on Cemu and real
        // hardware. Tear OSScreen down for the duration of video
        // playback and re-create it afterwards. Audio-only playback never
        // touches GX2, so the Now Playing screen can stay up.
        display.shutdown();
    }

    client.reportPlaybackStart(picked.id);

    Player player;
    PlayResult result = player.play(target.host, target.port, target.path,
        stopRequestedByUser,
        [&](double positionSeconds) {
            // Roughly once per second with the real playback position
            // (from the audio clock). Ping Jellyfin so its own UI shows
            // this as actively playing, and for audio redraw Now Playing.
            int64_t positionTicks = (int64_t)(positionSeconds * 10000000.0);
            client.reportPlaybackProgress(picked.id, positionTicks);

            if (isAudio) {
                ui::NowPlayingModel model;
                model.title = picked.name;
                model.subtitle = "Audio  |  AAC transcode";
                model.positionSeconds = positionSeconds;
                model.durationSeconds = durationSeconds;
                model.footer = "B: stop";
                display.render([&](ui::Surface& s) { ui::drawNowPlayingScreen(s, model); });
            }
        },
        playOptions);

    client.reportPlaybackStopped(picked.id, (int64_t)(player.positionSeconds() * 10000000.0));

    if (!isAudio) {
        display.init();
    }

    if (result == PlayResult::Error) {
        errorMessage = "Playback error: " + player.lastError();
    }
    return result;
}

int main(int argc, char** argv) {
    WHBProcInit();
    display.init();

    UfinConfig cfg;
    std::string configError;
    bool usingDefaults = !loadConfigFromSD(cfg, configError);
    if (usingDefaults) {
        cfg.host = UFIN_SERVER_HOST;
        cfg.port = UFIN_SERVER_PORT;
        cfg.username = UFIN_USERNAME;
        cfg.password = UFIN_PASSWORD;
    }

    JellyfinClient client(cfg.host, cfg.port);

    NavState nav;
    nav.usingDefaultConfig = usingDefaults;
    nav.configLoadError = configError;
    nav.configHost = cfg.host;
    nav.configPort = cfg.port;
    std::vector<std::pair<std::string, std::string>> navStack;

    renderNav(nav);

    if (!client.authenticate(cfg.username, cfg.password)) {
        nav.screen = Screen::ERROR_SCREEN;
        nav.errorMessage = client.lastError();
    } else if (!client.getViews(nav.currentList)) {
        nav.screen = Screen::ERROR_SCREEN;
        nav.errorMessage = client.lastError();
    } else {
        nav.screen = Screen::VIEWS;
        nav.selectedIndex = 0;
    }
    renderNav(nav);

    VPADStatus vpad;
    VPADReadError vpadError;

    while (WHBProcIsRunning()) {
        VPADRead(VPAD_CHAN_0, &vpad, 1, &vpadError);
        if (vpadError != VPAD_READ_SUCCESS) {
            OSSleepTicks(OSMillisecondsToTicks(16));
            continue;
        }

        if (vpad.trigger & VPAD_BUTTON_ZR) {
            runGx2TestPattern();
            renderNav(nav);
            continue;
        }

        bool changed = false;

        if (nav.screen == Screen::VIEWS || nav.screen == Screen::ITEMS) {
            if (vpad.trigger & VPAD_BUTTON_DOWN) {
                if (!nav.currentList.empty()) {
                    nav.selectedIndex = (nav.selectedIndex + 1) % (int)nav.currentList.size();
                    changed = true;
                }
            } else if (vpad.trigger & VPAD_BUTTON_UP) {
                if (!nav.currentList.empty()) {
                    nav.selectedIndex--;
                    if (nav.selectedIndex < 0) nav.selectedIndex = (int)nav.currentList.size() - 1;
                    changed = true;
                }
            } else if (vpad.trigger & VPAD_BUTTON_A) {
                if (!nav.currentList.empty()) {
                    // Copy: playItem() may take minutes and nav is redrawn after.
                    const JellyfinItem picked = nav.currentList[(size_t)nav.selectedIndex];

                    if (isPlayable(picked.type)) {
                        std::string error;
                        if (playItem(client, cfg, picked, error) == PlayResult::Error) {
                            nav.screen = Screen::ERROR_SCREEN;
                            nav.errorMessage = error;
                        }
                        // Completed/Stopped: nav is untouched, so we're
                        // still looking at the list we picked from.
                        changed = true;
                    } else {
                        std::vector<JellyfinItem> nextList;
                        if (client.getItems(picked.id, nextList)) {
                            navStack.push_back({picked.id, nav.currentParentName});
                            nav.currentParentName = picked.name;
                            nav.currentList = nextList;
                            nav.selectedIndex = 0;
                            nav.screen = Screen::ITEMS;
                        } else {
                            nav.screen = Screen::ERROR_SCREEN;
                            nav.errorMessage = client.lastError();
                        }
                        changed = true;
                    }
                }
            } else if (vpad.trigger & VPAD_BUTTON_B) {
                if (!navStack.empty()) {
                    auto parent = navStack.back();
                    navStack.pop_back();

                    if (navStack.empty()) {
                        if (client.getViews(nav.currentList)) {
                            nav.currentParentName = "";
                            nav.screen = Screen::VIEWS;
                        } else {
                            nav.screen = Screen::ERROR_SCREEN;
                            nav.errorMessage = client.lastError();
                        }
                    } else {
                        if (client.getItems(parent.first, nav.currentList)) {
                            nav.currentParentName = parent.second;
                            nav.screen = Screen::ITEMS;
                        } else {
                            nav.screen = Screen::ERROR_SCREEN;
                            nav.errorMessage = client.lastError();
                        }
                    }
                    nav.selectedIndex = 0;
                    changed = true;
                }
            }
        } else if (nav.screen == Screen::ERROR_SCREEN) {
            if (vpad.trigger & VPAD_BUTTON_B) {
                if (client.getViews(nav.currentList)) {
                    nav.screen = Screen::VIEWS;
                    nav.currentParentName = "";
                    navStack.clear();
                    nav.selectedIndex = 0;
                    changed = true;
                }
            }
        }

        if (changed) {
            renderNav(nav);
        }

        OSSleepTicks(OSMillisecondsToTicks(16));
    }

    display.shutdown();
    WHBProcShutdown();
    return 0;
}
