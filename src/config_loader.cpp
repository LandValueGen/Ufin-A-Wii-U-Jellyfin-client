#include "config_loader.h"
#include "vendor/cJSON.h"

#include <cstdio>
#include <vector>

// NOTE: this SD mount path (/vol/external01/...) is the standard
// convention for Wii U homebrew SD card access, but hasn't been
// compile/run-tested against this specific wut version -- if the file
// genuinely exists on the SD card but this still reports "not found",
// this path is the first thing to double check.
static const char* CONFIG_PATH = "/vol/external01/wiiu/apps/ufin/config.json";

bool loadConfigFromSD(UfinConfig& outConfig, std::string& outError) {
    return loadConfigFromFile(CONFIG_PATH, outConfig, outError);
}

bool loadConfigFromFile(const char* path, UfinConfig& outConfig, std::string& outError) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        outError = "config.json not found";
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        outError = "config.json is empty";
        return false;
    }

    std::vector<char> buf((size_t)size + 1);
    size_t readBytes = fread(buf.data(), 1, (size_t)size, f);
    fclose(f);
    buf[readBytes] = '\0';

    cJSON* json = cJSON_Parse(buf.data());
    if (!json) {
        outError = "config.json is not valid JSON";
        return false;
    }

    cJSON* host = cJSON_GetObjectItem(json, "host");
    cJSON* port = cJSON_GetObjectItem(json, "port");
    cJSON* username = cJSON_GetObjectItem(json, "username");
    cJSON* password = cJSON_GetObjectItem(json, "password");

    if (!cJSON_IsString(host) || !cJSON_IsNumber(port) ||
        !cJSON_IsString(username) || !cJSON_IsString(password)) {
        cJSON_Delete(json);
        outError = "config.json missing one of: host, port, username, password";
        return false;
    }

    outConfig.host = host->valuestring;
    outConfig.port = port->valueint;
    outConfig.username = username->valuestring;
    outConfig.password = password->valuestring;

    // Optional video settings -- absent keys keep the struct defaults.
    cJSON* videoBitrate = cJSON_GetObjectItem(json, "video_bitrate");
    if (cJSON_IsNumber(videoBitrate) && videoBitrate->valueint > 0) {
        outConfig.videoBitrate = videoBitrate->valueint;
    }
    cJSON* videoProfile = cJSON_GetObjectItem(json, "video_profile");
    if (cJSON_IsString(videoProfile) && videoProfile->valuestring[0] != '\0') {
        outConfig.videoProfile = videoProfile->valuestring;
    }

    cJSON_Delete(json);
    return true;
}
