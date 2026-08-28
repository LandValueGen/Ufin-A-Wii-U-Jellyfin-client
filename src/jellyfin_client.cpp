#include "jellyfin_client.h"
#include "http_client.h"
#include "vendor/cJSON.h"

#include <cstdlib>

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
        if (cJSON_IsString(id)) ji.id = id->valuestring;
        if (cJSON_IsString(name)) ji.name = name->valuestring;
        if (cJSON_IsString(type)) ji.type = type->valuestring;
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

StreamTarget JellyfinClient::buildAudioStreamUrl(const std::string& itemId) const {
    StreamTarget target;
    target.host = host_;
    target.port = port_;

    // .mp4 container, not .mp3 -- our FFmpeg build (see configure-wiiu)
    // only has the mov demuxer enabled, no MP3/ADTS demuxer, so an
    // actual .mp3-formatted stream would be undecodable by us even
    // though the URL would "work" against Jellyfin. AAC-in-MP4 is
    // exactly what the video path already uses successfully for audio
    // tracks, just without a video stream here.
    char pathBuf[900];
    snprintf(pathBuf, sizeof(pathBuf),
        "/Audio/%s/stream.mp4?static=false&AudioCodec=aac&AudioBitrate=192000"
        "&api_key=%s",
        itemId.c_str(), token_.c_str());
    target.path = pathBuf;
    return target;
}

StreamTarget JellyfinClient::buildVideoStreamUrl(const std::string& itemId) const {
    StreamTarget target;
    target.host = host_;
    target.port = port_;

    char pathBuf[900];
    snprintf(pathBuf, sizeof(pathBuf),
        "/Videos/%s/stream.mp4?static=false&VideoCodec=h264&AudioCodec=aac"
        "&MaxWidth=1280&MaxHeight=720&VideoBitrate=2500000&AudioBitrate=192000"
        // Without these, Jellyfin appears to "copy" the video stream
        // unchanged when the source is already H.264 -- ignoring our
        // resolution/bitrate caps above and sending the original
        // (possibly much higher resolution/profile) stream instead,
        // which the Wii U's hardware decoder can't handle. Forcing this
        // off makes Jellyfin actually re-encode every time, regardless
        // of whether the source codec already matches what we asked for.
        "&AllowVideoStreamCopy=false&AllowAudioStreamCopy=false"
        "&api_key=%s",
        itemId.c_str(), token_.c_str());
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
