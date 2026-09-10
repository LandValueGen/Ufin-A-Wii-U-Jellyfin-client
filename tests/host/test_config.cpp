#include "check.h"
#include "config_loader.h"

#include <cstdio>
#include <string>

static std::string writeTemp(const char* name, const std::string& content) {
    std::string path = std::string("/tmp/ufin_test_") + name + ".json";
    FILE* f = fopen(path.c_str(), "wb");
    fwrite(content.data(), 1, content.size(), f);
    fclose(f);
    return path;
}

int main() {
    UfinConfig cfg;
    std::string err;

    // Missing file.
    CHECK(!loadConfigFromFile("/tmp/ufin_definitely_missing.json", cfg, err));
    CHECK_STR(err, "config.json not found");

    // Full config with optional video keys.
    std::string full = writeTemp("full",
        "{\"host\":\"10.0.0.5\",\"port\":8096,\"username\":\"u\",\"password\":\"p\","
        "\"video_bitrate\":4000000,\"video_profile\":\"main\"}");
    CHECK(loadConfigFromFile(full.c_str(), cfg, err));
    CHECK_STR(cfg.host, "10.0.0.5");
    CHECK_EQ(cfg.port, 8096);
    CHECK_STR(cfg.username, "u");
    CHECK_STR(cfg.password, "p");
    CHECK_EQ(cfg.videoBitrate, 4000000);
    CHECK_STR(cfg.videoProfile, "main");

    // Minimal config keeps video defaults.
    UfinConfig minimal;
    std::string min = writeTemp("min", "{\"host\":\"h\",\"port\":1,\"username\":\"u\",\"password\":\"p\"}");
    CHECK(loadConfigFromFile(min.c_str(), minimal, err));
    CHECK_EQ(minimal.videoBitrate, 2500000);
    CHECK_STR(minimal.videoProfile, "baseline");

    // Bad optional values are ignored, not fatal.
    UfinConfig odd;
    std::string oddPath = writeTemp("odd",
        "{\"host\":\"h\",\"port\":1,\"username\":\"u\",\"password\":\"p\",\"video_bitrate\":\"fast\",\"video_profile\":\"\"}");
    CHECK(loadConfigFromFile(oddPath.c_str(), odd, err));
    CHECK_EQ(odd.videoBitrate, 2500000);
    CHECK_STR(odd.videoProfile, "baseline");

    // Missing required key.
    std::string missing = writeTemp("missing", "{\"host\":\"h\",\"port\":1,\"username\":\"u\"}");
    CHECK(!loadConfigFromFile(missing.c_str(), cfg, err));
    CHECK(err.find("missing one of") != std::string::npos);

    // Invalid JSON and empty file.
    std::string invalid = writeTemp("invalid", "{host: nope");
    CHECK(!loadConfigFromFile(invalid.c_str(), cfg, err));
    CHECK_STR(err, "config.json is not valid JSON");
    std::string empty = writeTemp("empty", "");
    CHECK(!loadConfigFromFile(empty.c_str(), cfg, err));
    CHECK_STR(err, "config.json is empty");

    return check::finish("test_config");
}
