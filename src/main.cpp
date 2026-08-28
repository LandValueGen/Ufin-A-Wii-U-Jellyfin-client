// Ufin -- browse Jellyfin libraries with the GamePad and play video/audio
// via the media/ pipeline (HttpStreamIO -> Decoder -> VideoOutput/
// AudioOutput -> Player). Menu rendering uses OSScreen directly
// (ClearBuffer + PutFont + FlipBuffers) rather than WHBLogConsole, which
// turned out not to support actually clearing the display.

#include <whb/proc.h>
#include <coreinit/screen.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/cache.h>
#include <vpad/input.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <vector>
#include <string>
#include <cstdio>
#include <cstdint>

#include "config.h"
#include "jellyfin_client.h"
#include "media/player.h"
#include "media/video_output.h"
#include "config_loader.h"

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

// Conservative on-screen limits. If text looks cut off or misplaced on
// real hardware, these are the first numbers to tune.
static const int MAX_COLS = 78;
static const int MAX_VISIBLE_ITEMS = 14;

static void* tvBuffer = nullptr;
static void* drcBuffer = nullptr;

static void initScreen() {
    OSScreenInit();

    uint32_t tvSize = OSScreenGetBufferSizeEx(SCREEN_TV);
    uint32_t drcSize = OSScreenGetBufferSizeEx(SCREEN_DRC);

    tvBuffer = MEMAllocFromDefaultHeapEx(tvSize, 0x100);
    drcBuffer = MEMAllocFromDefaultHeapEx(drcSize, 0x100);

    OSScreenSetBufferEx(SCREEN_TV, tvBuffer);
    OSScreenSetBufferEx(SCREEN_DRC, drcBuffer);

    OSScreenEnableEx(SCREEN_TV, TRUE);
    OSScreenEnableEx(SCREEN_DRC, TRUE);
}

static void shutdownScreen() {
    OSScreenEnableEx(SCREEN_TV, FALSE);
    OSScreenEnableEx(SCREEN_DRC, FALSE);
    if (tvBuffer) MEMFreeToDefaultHeap(tvBuffer);
    if (drcBuffer) MEMFreeToDefaultHeap(drcBuffer);
    tvBuffer = nullptr;
    drcBuffer = nullptr;
}

static std::string truncate(const std::string& s, int maxLen) {
    if ((int)s.size() <= maxLen) return s;
    return s.substr(0, maxLen - 3) + "...";
}

static void present(const std::vector<std::string>& lines) {
    OSScreenClearBufferEx(SCREEN_TV, 0x000000FF);
    OSScreenClearBufferEx(SCREEN_DRC, 0x000000FF);

    for (size_t row = 0; row < lines.size(); row++) {
        OSScreenPutFontEx(SCREEN_TV, 0, (int32_t)row, lines[row].c_str());
        OSScreenPutFontEx(SCREEN_DRC, 0, (int32_t)row, lines[row].c_str());
    }

    DCFlushRange(tvBuffer, OSScreenGetBufferSizeEx(SCREEN_TV));
    DCFlushRange(drcBuffer, OSScreenGetBufferSizeEx(SCREEN_DRC));

    OSScreenFlipBuffersEx(SCREEN_TV);
    OSScreenFlipBuffersEx(SCREEN_DRC);
}

static void renderList(const NavState& nav) {
    std::vector<std::string> lines;

    if (nav.screen == Screen::CONNECTING) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Ufin - connecting to %s:%d ...",
                 nav.configHost.c_str(), nav.configPort);
        lines.push_back(buf);
        if (nav.usingDefaultConfig) {
            lines.push_back(truncate("(" + nav.configLoadError +
                             " -- using hardcoded config.h defaults)", MAX_COLS));
        }
    } else if (nav.screen == Screen::ERROR_SCREEN) {
        lines.push_back("Ufin - error:");
        lines.push_back(truncate(nav.errorMessage, MAX_COLS));
        lines.push_back("");
        lines.push_back("Check config.h (host/port/credentials) and that");
        lines.push_back("the Wii U and Jellyfin server are on the same network.");
        lines.push_back("");
        lines.push_back("Press B to go back.");
    } else {
        std::string header = "Ufin  |  " + (nav.currentParentName.empty() ?
                              std::string("Libraries") : nav.currentParentName);
        lines.push_back(truncate(header, MAX_COLS));
        lines.push_back("D-pad Up/Down: move   A: open/play   B: back");
        lines.push_back("--------------------------------------------------------------");

        if (nav.currentList.empty()) {
            lines.push_back("(empty)");
        } else {
            int total = (int)nav.currentList.size();
            int windowStart = 0;
            if (total > MAX_VISIBLE_ITEMS) {
                windowStart = nav.selectedIndex - MAX_VISIBLE_ITEMS / 2;
                if (windowStart < 0) windowStart = 0;
                if (windowStart > total - MAX_VISIBLE_ITEMS) {
                    windowStart = total - MAX_VISIBLE_ITEMS;
                }
            }
            int windowEnd = std::min(total, windowStart + MAX_VISIBLE_ITEMS);

            if (windowStart > 0) {
                lines.push_back("  ^ more above ^");
            }

            for (int i = windowStart; i < windowEnd; i++) {
                const char* cursor = (i == nav.selectedIndex) ? ">" : " ";
                std::string entry = std::string(cursor) + " " + nav.currentList[i].name +
                                     "  [" + nav.currentList[i].type + "]";
                lines.push_back(truncate(entry, MAX_COLS));
            }

            if (windowEnd < total) {
                lines.push_back("  v more below v");
            }
        }
    }

    present(lines);
}

// True if this item type is something Player can actually play, rather
// than a folder to browse into.
static bool isPlayable(const std::string& type) {
    return type == "Movie" || type == "Episode" || type == "Video" || type == "Audio";
    // "Audio" now routed to buildAudioStreamUrl() in the A-handler
    // below (AAC-in-MP4, matching what our FFmpeg build can decode) --
    // audio-only playback confirmed working; video remains unresolved.
}

int main(int argc, char** argv) {
    WHBProcInit();
    initScreen();

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

    renderList(nav);

    if (!client.authenticate(cfg.username, cfg.password)) {
        nav.screen = Screen::ERROR_SCREEN;
        nav.errorMessage = client.lastError();
        renderList(nav);
    } else if (!client.getViews(nav.currentList)) {
        nav.screen = Screen::ERROR_SCREEN;
        nav.errorMessage = client.lastError();
        renderList(nav);
    } else {
        nav.screen = Screen::VIEWS;
        nav.selectedIndex = 0;
        renderList(nav);
    }

    VPADStatus vpad;
    VPADReadError vpadError;

    while (WHBProcIsRunning()) {
        VPADRead(VPAD_CHAN_0, &vpad, 1, &vpadError);
        if (vpadError != VPAD_READ_SUCCESS) {
            OSSleepTicks(OSMillisecondsToTicks(16));
            continue;
        }

        // Diagnostic: hold ZR to run a standalone GX2 test -- draws a
        // solid magenta fullscreen quad via the exact same shader/draw/
        // present pipeline as real video, but with no decode, no
        // streaming, no swscale involved at all. Isolates whether the
        // fundamental GX2 pipeline can put anything on screen, decoupled
        // from every video-specific complexity. Press B to exit it.
        if (vpad.trigger & VPAD_BUTTON_ZR) {
            shutdownScreen();
            VideoOutput testOutput;
            if (testOutput.init(1280, 720)) {
                bool testDone = false;
                while (!testDone && WHBProcIsRunning()) {
                    testOutput.renderTestPattern();
                    // Diagnostic: pace to match real video's ~33ms
                    // cadence instead of running unthrottled, to test
                    // whether rapid unpaced draw calls were masking a
                    // timing/sync requirement real (paced) playback
                    // exposes.
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
            initScreen();
            renderList(nav);
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
                    const JellyfinItem& picked = nav.currentList[nav.selectedIndex];

                    if (isPlayable(picked.type)) {
                        bool isAudio = (picked.type == "Audio");
                        StreamTarget target = isAudio
                            ? client.buildAudioStreamUrl(picked.id)
                            : client.buildVideoStreamUrl(picked.id);

                        // Only video needs the OSScreen/GX2 hand-off --
                        // audio-only playback never touches GX2 at all,
                        // so OSScreen can safely stay up and show a Now
                        // Playing screen throughout.
                        if (!isAudio) {
                            // OSScreen and SDL2 both ultimately drive the
                            // same GX2 display hardware. Leaving OSScreen's
                            // buffers active while SDL2 tries to claim the
                            // display for video playback caused a hard
                            // OSFatal hang on both Cemu and real hardware --
                            // tearing OSScreen down first, then
                            // reinitializing it once playback ends, is the
                            // fix for that.
                            shutdownScreen();
                        }

                        client.reportPlaybackStart(picked.id);
                        uint64_t playbackStartMs = OSGetTime() / OSMillisecondsToTicks(1);

                        Player player;
                        PlayResult result = player.play(target.host, target.port, target.path,
                            [&]() {
                                // Polled once per decoded frame by Player --
                                // re-read the GamePad here so B can stop
                                // playback immediately rather than waiting
                                // for the outer menu loop's next iteration.
                                VPADStatus playbackVpad;
                                VPADReadError playbackErr;
                                VPADRead(VPAD_CHAN_0, &playbackVpad, 1, &playbackErr);
                                return (playbackErr == VPAD_READ_SUCCESS) &&
                                       (playbackVpad.trigger & VPAD_BUTTON_B);
                            },
                            [&]() {
                                // Called roughly once per second during
                                // audio-only playback -- redraw the Now
                                // Playing screen and ping Jellyfin so its
                                // own UI shows this as actively playing.
                                uint64_t elapsedMs = OSGetTime() / OSMillisecondsToTicks(1) - playbackStartMs;
                                int64_t positionTicks = (int64_t)elapsedMs * 10000; // ms -> 100ns ticks
                                client.reportPlaybackProgress(picked.id, positionTicks);

                                std::vector<std::string> lines2;
                                lines2.push_back("Ufin  |  Now Playing");
                                lines2.push_back("--------------------------------------------------------------");
                                lines2.push_back(truncate(picked.name, MAX_COLS));
                                lines2.push_back("");
                                char timeBuf[64];
                                snprintf(timeBuf, sizeof(timeBuf), "%llu:%02llu elapsed",
                                         (unsigned long long)(elapsedMs / 60000),
                                         (unsigned long long)((elapsedMs / 1000) % 60));
                                lines2.push_back(timeBuf);
                                lines2.push_back("");
                                lines2.push_back("Press B to stop");
                                present(lines2);
                            });

                        uint64_t finalElapsedMs = OSGetTime() / OSMillisecondsToTicks(1) - playbackStartMs;
                        client.reportPlaybackStopped(picked.id, (int64_t)finalElapsedMs * 10000);

                        if (!isAudio) {
                            initScreen();
                        }

                        if (result == PlayResult::Error) {
                            nav.screen = Screen::ERROR_SCREEN;
                            nav.errorMessage = "Playback error: " + player.lastError();
                        }
                        // Completed/Stopped: nav.screen/currentList are
                        // untouched, so we're still looking at whatever
                        // list we picked this item from -- just redraw it.
                        changed = true;
                    } else {
                        std::vector<JellyfinItem> nextList;
                        if (client.getItems(picked.id, nextList)) {
                            navStack.push_back({picked.id, nav.currentParentName});
                            nav.currentParentName = picked.name;
                            nav.currentList = nextList;
                            nav.selectedIndex = 0;
                            nav.screen = Screen::ITEMS;
                            changed = true;
                        } else {
                            nav.screen = Screen::ERROR_SCREEN;
                            nav.errorMessage = client.lastError();
                            changed = true;
                        }
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
            renderList(nav);
        }

        OSSleepTicks(OSMillisecondsToTicks(16));
    }

    shutdownScreen();
    WHBProcShutdown();
    return 0;
}
