#include "jellyfin_client.h"
#include "http_client.h"
#include "vendor/cJSON.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cctype>

JellyfinClient::JellyfinClient(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

std::string JellyfinClient::authHeader() const {
    // Jellyfin expects this identifying header on basically every request.
    // DeviceId should stay stable across launches so Jellyfin treats it as
    // the same device (matters for resume points / "continue watching").
    std::string h = "X-Emby-Authorization: MediaBrowser Client=\"Ufin\", "
                     "Device=\"WiiU\", DeviceId=\"wiiu-ufin-001\", Version=\"0.1.0\"";
    if (!token_.empty()) {
        h += ", Token=\"" + token_ + "\"";
    }
    h += "\r\n";
    return h;
}

bool JellyfinClient::authenticate(const std::string& username, const std::string& password) {
    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "Username", username.c_str());
    cJSON_AddStringToObject(body, "Pw", password.c_str());
    char* body_str = cJSON_PrintUnformatted(body);

    HttpResponse resp = http_post(host_, port_, "/Users/AuthenticateByName",
                                   body_str, "application/json", authHeader());
    free(body_str);
    cJSON_Delete(body);

    if (!resp.success) {
        last_error_ = "AuthenticateByName failed (status " +
                       std::to_string(resp.status_code) + "): " + resp.body;
        return false;
    }

    cJSON* json = cJSON_Parse(resp.body.c_str());
    if (!json) {
        last_error_ = "could not parse auth response JSON";
        return false;
    }

    cJSON* access_token = cJSON_GetObjectItem(json, "AccessToken");
    cJSON* user = cJSON_GetObjectItem(json, "User");
    cJSON* user_id = user ? cJSON_GetObjectItem(user, "Id") : nullptr;

    if (!cJSON_IsString(access_token) || !cJSON_IsString(user_id)) {
        last_error_ = "auth response missing AccessToken or User.Id";
        cJSON_Delete(json);
        return false;
    }

    token_ = access_token->valuestring;
    user_id_ = user_id->valuestring;
    cJSON_Delete(json);
    return true;
}

static void parseItemsArray(cJSON* items, std::vector<JellyfinItem>& out) {
    out.clear(); // was missing -- caller passes the same vector across
                 // calls (e.g. nav.currentList), so without this each
                 // new fetch appended onto whatever was already there
                 // instead of replacing it.
    if (!items) return;
    cJSON* item;
    cJSON_ArrayForEach(item, items) {
        JellyfinItem ji;
        cJSON* id = cJSON_GetObjectItem(item, "Id");
        cJSON* name = cJSON_GetObjectItem(item, "Name");
        cJSON* type = cJSON_GetObjectItem(item, "Type");
        cJSON* runTime = cJSON_GetObjectItem(item, "RunTimeTicks");
        if (cJSON_IsString(id)) ji.id = id->valuestring;
        if (cJSON_IsString(name)) ji.name = name->valuestring;
        if (cJSON_IsString(type)) ji.type = type->valuestring;
        if (cJSON_IsNumber(runTime)) ji.runTimeTicks = (int64_t)runTime->valuedouble;
        out.push_back(ji);
    }
}

bool JellyfinClient::getViews(std::vector<JellyfinItem>& out) {
    std::string path = "/Users/" + user_id_ + "/Views";
    HttpResponse resp = http_get(host_, port_, path, authHeader());
    if (!resp.success) {
        last_error_ = "getViews failed (status " + std::to_string(resp.status_code) + ")";
        return false;
    }
    cJSON* json = cJSON_Parse(resp.body.c_str());
    if (!json) {
        last_error_ = "could not parse views JSON";
        return false;
    }
    parseItemsArray(cJSON_GetObjectItem(json, "Items"), out);
    cJSON_Delete(json);
    return true;
}

bool JellyfinClient::getItems(const std::string& parentId, std::vector<JellyfinItem>& out) {
    std::string path = "/Users/" + user_id_ + "/Items?ParentId=" + parentId +
                        "&SortBy=SortName&SortOrder=Ascending";
    HttpResponse resp = http_get(host_, port_, path, authHeader());
    if (!resp.success) {
        last_error_ = "getItems failed (status " + std::to_string(resp.status_code) + ")";
        return false;
    }
    cJSON* json = cJSON_Parse(resp.body.c_str());
    if (!json) {
        last_error_ = "could not parse items JSON";
        return false;
    }
    parseItemsArray(cJSON_GetObjectItem(json, "Items"), out);
    cJSON_Delete(json);
    return true;
}

// Jellyfin reports aspect ratios as strings like "16:9" or "2.35:1".
static double parseAspectRatio(const char* s) {
    if (!s) return 0.0;
    double a = 0.0, b = 0.0;
    if (sscanf(s, "%lf:%lf", &a, &b) == 2 && a > 0.0 && b > 0.0) {
        return a / b;
    }
    return 0.0;
}

// Pulls Width/Height/AspectRatio out of the first video-type entry of a
// MediaStreams array, if there is one.
static void parseVideoStream(cJSON* mediaStreams, VideoInfo& out) {
    if (!cJSON_IsArray(mediaStreams)) return;
    cJSON* stream;
    cJSON_ArrayForEach(stream, mediaStreams) {
        cJSON* type = cJSON_GetObjectItem(stream, "Type");
        if (!cJSON_IsString(type) || strcmp(type->valuestring, "Video") != 0) continue;

        cJSON* w = cJSON_GetObjectItem(stream, "Width");
        cJSON* h = cJSON_GetObjectItem(stream, "Height");
        cJSON* ar = cJSON_GetObjectItem(stream, "AspectRatio");
        if (cJSON_IsNumber(w) && w->valueint > 0) out.width = w->valueint;
        if (cJSON_IsNumber(h) && h->valueint > 0) out.height = h->valueint;
        if (cJSON_IsString(ar) && out.displayAspect <= 0.0) {
            out.displayAspect = parseAspectRatio(ar->valuestring);
        }
        return;
    }
}

bool JellyfinClient::getVideoInfo(const std::string& itemId, VideoInfo& out) {
    out = VideoInfo{};

    std::string path = "/Users/" + user_id_ + "/Items/" + itemId;
    HttpResponse resp = http_get(host_, port_, path, authHeader());
    if (!resp.success) {
        last_error_ = "getVideoInfo failed (status " + std::to_string(resp.status_code) + ")";
        return false;
    }
    cJSON* json = cJSON_Parse(resp.body.c_str());
    if (!json) {
        last_error_ = "could not parse item JSON";
        return false;
    }

    cJSON* runTime = cJSON_GetObjectItem(json, "RunTimeTicks");
    if (cJSON_IsNumber(runTime)) out.runTimeTicks = (int64_t)runTime->valuedouble;

    // Item-level Width/Height are present for video items; the
    // MediaStreams entry carries the display aspect ratio (which for
    // anamorphic sources isn't just width/height).
    cJSON* w = cJSON_GetObjectItem(json, "Width");
    cJSON* h = cJSON_GetObjectItem(json, "Height");
    if (cJSON_IsNumber(w) && w->valueint > 0) out.width = w->valueint;
    if (cJSON_IsNumber(h) && h->valueint > 0) out.height = h->valueint;

    parseVideoStream(cJSON_GetObjectItem(json, "MediaStreams"), out);
    if (out.displayAspect <= 0.0) {
        cJSON* sources = cJSON_GetObjectItem(json, "MediaSources");
        if (cJSON_IsArray(sources) && cJSON_GetArraySize(sources) > 0) {
            parseVideoStream(cJSON_GetObjectItem(cJSON_GetArrayItem(sources, 0), "MediaStreams"), out);
        }
    }

    if (out.displayAspect <= 0.0 && out.width > 0 && out.height > 0) {
        out.displayAspect = (double)out.width / (double)out.height;
    }

    cJSON_Delete(json);
    return true;
}

StreamTarget JellyfinClient::buildAudioStreamUrl(const std::string& itemId) const {
    StreamTarget target;
    target.host = host_;
    target.port = port_;

    // .mp4 container, not .mp3 -- our FFmpeg build (see configure-wiiu)
    // only has the mov demuxer enabled, no MP3/ADTS demuxer, so an
    // actual .mp3-formatted stream would be undecodable by us even
    // though the URL would "work" against Jellyfin. AAC-in-MP4 is
    // exactly what the video path uses for its audio track, just
    // without a video stream here.
    char pathBuf[900];
    snprintf(pathBuf, sizeof(pathBuf),
        "/Audio/%s/stream.mp4?static=false&AudioCodec=aac&AudioBitrate=192000"
        "&api_key=%s",
        itemId.c_str(), token_.c_str());
    target.path = pathBuf;
    return target;
}

// Only letters end up in the URL, whatever the config file says.
static std::string sanitizeProfile(const std::string& profile) {
    std::string out;
    for (char c : profile) {
        if (isalpha((unsigned char)c)) out += (char)tolower((unsigned char)c);
    }
    if (out != "baseline" && out != "main" && out != "high") out = "baseline";
    return out;
}

StreamTarget JellyfinClient::buildVideoStreamUrl(const std::string& itemId,
                                                 const VideoStreamOptions& options) const {
    StreamTarget target;
    target.host = host_;
    target.port = port_;

    int bitrate = options.videoBitrate;
    if (bitrate < 300000) bitrate = 300000;
    if (bitrate > 20000000) bitrate = 20000000;
    std::string profile = sanitizeProfile(options.profile);

    // Every parameter here is load-bearing for the Wii U hardware
    // decoder wrapper (h264_wiiu in the FFmpeg-wiiu fork):
    //
    // Width=1280&Height=720 (exact, not MaxWidth/MaxHeight):
    //   h264_wiiu sizes its decode framebuffer as width*height*1.5, but
    //   the hardware writes rows at a 256-pixel-aligned pitch and a
    //   16-row-aligned height. The two only agree when width is a
    //   multiple of 256 and height a multiple of 16 -- 1280x720 is, but
    //   the 1280x536 that MaxWidth/MaxHeight would produce for a 2.39:1
    //   movie, or 960x720 for 4:3 content, are not, and overflow the
    //   heap. So we ask Jellyfin to scale *everything* to exactly
    //   1280x720 (its scale filter is "scale=1280:720" when both are
    //   given), which squeezes non-16:9 pictures anamorphically, and
    //   VideoOutput un-squeezes them at draw time using the aspect
    //   ratio from the item's metadata (getVideoInfo).
    //
    // Profile=baseline (default):
    //   With the "output per frame" setting h264_wiiu uses, the hardware
    //   hands back exactly one picture per H264DECExecute call, in
    //   decode order. B-frames (Main/High profile) would come out in the
    //   wrong order with the wrong timestamps. Baseline has no B-frames,
    //   so decode order == display order and every frame lines up with
    //   its packet's timestamp. Costs some compression efficiency; can
    //   be overridden via config.json (video_profile) for experiments.
    //
    // MaxFramerate=30: 60 fps sources get decimated to 30 -- the CPU
    //   side (frame copy + upload) is sized for 720p30, per the README.
    //
    // MaxAudioChannels=2: downmix on the server so we don't pay for 5.1
    //   over Wi-Fi only to downmix it ourselves in AudioOutput.
    //
    // AllowVideoStreamCopy=false / AllowAudioStreamCopy=false:
    //   Without these Jellyfin remuxes an already-H.264 source unchanged,
    //   ignoring every constraint above.
    char pathBuf[1024];
    snprintf(pathBuf, sizeof(pathBuf),
        "/Videos/%s/stream.mp4?static=false&VideoCodec=h264&AudioCodec=aac"
        "&Width=1280&Height=720&VideoBitrate=%d&AudioBitrate=192000"
        "&Profile=%s&Level=41&MaxFramerate=30&MaxAudioChannels=2"
        "&AllowVideoStreamCopy=false&AllowAudioStreamCopy=false"
        "&api_key=%s",
        itemId.c_str(), bitrate, profile.c_str(), token_.c_str());
    target.path = pathBuf;
    return target;
}

static bool postSessionEvent(const std::string& host, int port, const std::string& authHeader,
                              const std::string& endpoint, const std::string& itemId,
                              int64_t positionTicks, bool includePosition) {
    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "ItemId", itemId.c_str());
    cJSON_AddBoolToObject(body, "CanSeek", false);
    cJSON_AddStringToObject(body, "PlayMethod", "Transcode");
    if (includePosition) {
        // cJSON's number type is a double -- fine here, ticks values in
        // our use (elapsed playback seconds * 10,000,000) stay well
        // within double's exact-integer range for any realistic
        // playback duration.
        cJSON_AddNumberToObject(body, "PositionTicks", (double)positionTicks);
    }
    char* bodyStr = cJSON_PrintUnformatted(body);

    HttpResponse resp = http_post(host, port, endpoint, bodyStr, "application/json", authHeader);
    free(bodyStr);
    cJSON_Delete(body);
    return resp.success;
}

bool JellyfinClient::reportPlaybackStart(const std::string& itemId) {
    return postSessionEvent(host_, port_, authHeader(), "/Sessions/Playing", itemId, 0, false);
}

bool JellyfinClient::reportPlaybackProgress(const std::string& itemId, int64_t positionTicks) {
    return postSessionEvent(host_, port_, authHeader(), "/Sessions/Playing/Progress", itemId,
                             positionTicks, true);
}

bool JellyfinClient::reportPlaybackStopped(const std::string& itemId, int64_t positionTicks) {
    return postSessionEvent(host_, port_, authHeader(), "/Sessions/Playing/Stopped", itemId,
                             positionTicks, true);
}
