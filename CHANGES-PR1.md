# Fixes and additions on top of "Add video playback and frontend UI"

Found while testing the PR in Cemu 2.6 (Linux) against Jellyfin 12.

## Build
- **CMakeLists.txt:** `src/config_loader.cpp` was missing from `add_executable`, so the
  Wii U build failed to link (`undefined reference to loadConfigFromSD`). The host
  tests compiled it, so they didn't catch this.
- **FFmpeg-wiiu:** new `patches/ffmpeg-wiiu-fixes.patch`, see "Playback" and the README.
- Removed a stray `FETCH_HEAD` that was committed by accident.

## Jellyfin 12 compatibility
- Login now sends `Authorization: MediaBrowser ...` instead of `X-Emby-Authorization`.
  Jellyfin 12 disables legacy authorization by default, ignores the old header, and
  rejects the login with `400 Error processing request`.
- Stream URLs use `ApiKey=` instead of `api_key=`, for the same reason.
- Both forms have been accepted since Jellyfin 10.8, so older servers keep working.

## Playback
- **Crash on starting a movie:** `Decoder::open` called `avformat_find_stream_info()`.
  For H.264 it opens its own throwaway decoder (`h264_wiiu`, the only one in the build)
  and crashed inside `h264_wiiu_decode_frame` (seen via `try_decode_frame` in the Cemu
  crash dump). The mov demuxer already provides everything the decoders need, so the
  call is gone; the stream parameters are logged instead. The frame rate is unknown
  without it, and Player's existing 1/30 s fallback matches the rate requested from
  Jellyfin.
- **Crash while decoding** (pointer overwritten with `0x80808080`, i.e. grey NV12 pixels):
  `h264_wiiu` allocated `width*height*1.5` bytes, but the hardware writes a 256-pixel
  pitch and 16-row aligned height. Fixed in the FFmpeg-wiiu patch, which also reads the
  packet from `avpkt->data/size` and checks its allocations.
- **Picture and sound taking turns (every ~5 s):** a live fragmented MP4 read front to
  back delivers each fragment as all its video packets, then all its audio packets.
  Decoding in that order with a 6-frame video queue meant audio ran dry while video
  waited for the clock, and the other way round. The decoder now demuxes into
  per-stream queues of *compressed* packets (capped at 24 MB), and the decode thread
  decodes whichever stream needs a frame, reading from the network only when that
  stream has nothing buffered. `test_playback_schedule` reproduces this with 5 s
  fragments: the old order let audio run dry on 342 of 354 ticks, the new one on 0.
- Playback progress reports are sent from a background thread every 10 s. They were
  blocking HTTP calls on the render loop, once a second.
- Non-200 stream responses now log (and show) the start of the server's error body.
- `MediaSourceId` is passed for movies, so multi-version items pick a source.
- The non-streaming HTTP client has a 20 s timeout: an unreachable-but-accepting
  server used to hang the "Connecting..." screen forever.

## Live TV (new)
- Opening the Live TV view lists channels (`/LiveTv/Channels`) with channel numbers and
  what's on now.
- Playing a channel opens the live stream via `PlaybackInfo` with `AutoOpenLiveStream`,
  streams it through the same 720p H.264 transcode as movies (with `MediaSourceId`,
  `LiveStreamId` and `PlaySessionId`), reports playback with those ids, and closes the
  live stream afterwards so the tuner is released.

## Pause, skip and search (new)
- **Pause:** A pauses and resumes playback. The audio device is paused and the clock
  freezes, so the picture holds. The decoder fills its buffers and then stops reading,
  and Jellyfin sees `IsPaused` in the progress reports.
- **Skip:** Left goes back 10 s and Right forward 30 s. Presses within 0.6 s add up,
  so Right three times is one +90 s restart. The progressive transcode can't be
  seeked, so Jellyfin restarts it at `StartTimeTicks`. Positions stay item-relative
  whether the new stream's timestamps restart at 0 or keep the original ones.
  Reports now say `CanSeek` (false for Live TV, which has only B).
- **Search:** X opens the Wii U software keyboard (`nn::swkbd`, drawn with GX2, so
  OSScreen steps aside as it does for video). It searches movies, shows, episodes and
  music across all libraries. Episode results name their series.
- **Tests:** new `test_playback_controls`; `test_jellyfin` covers search,
  `StartTimeTicks`, `IsPaused` and `CanSeek`; `test_audio_clock` covers pause.

## UI overhaul
- **Measured text grid:** OSScreen text sits on different grids on hardware and in Cemu.
  With the old hardcoded 12x24 grid at (50,32), Cemu (16x24 from the edge) wrapped long
  lines back over the start of the row and drew the selection band a row below the
  cursor. `OSScreenDisplay` now measures the grid at start-up by drawing probes into its
  own framebuffer (`ui/grid_probe.h`, host-tested against fake screens) and logs the
  result. If measurement fails it falls back to the old values.
- **Header:** breadcrumb (keeps the deepest level when long), `n / total` counter, and an
  accent rule.
- **Detail line:** shows information on the selection (e.g. "Now: News - A: watch",
  "Season 2, Episode 5 - 42:00").
- **Friendly tags** (`src/item_labels.cpp`): year and duration for movies, `S2E5` for
  episodes, library kind for views, `LIVE` for channels.
- **Errors** are word-wrapped instead of cut off.
- **Accented Latin letters** show as their plain letter ("Citta") instead of "?".
- **Navigation:** fixed going back from two levels deep, which reloaded the folder you
  were in instead of its parent. Every level now keeps its list and selection, so B is
  instant and returns you to where you were.
- **Input:** hold up/down to scroll, L/R (or left/right) to page, Y to refresh, and the
  left stick works. On a login error, B retries.

## Tests
- `test_ui` runs every screen at both sizes with both grids, and checks that no text
  can exceed the grid.
- New `test_grid_probe` and `test_item_labels`.
- `test_jellyfin` covers the new auth header and `ApiKey`, Live TV, media sources and
  reporting ids.
- `test_decoder_stream` accepts an unknown frame rate.
- All pass: `tests/host/run_tests.sh`, and with `FFMPEG_HOST` set.

## Not verified yet
The Wii U build is syntax-checked against the wut headers and the host tests pass, but
none of this has run on a real console. The measured grid values printed at start-up
(`Ufin: TV text grid measured: ...`) are worth including in a hardware test report.
