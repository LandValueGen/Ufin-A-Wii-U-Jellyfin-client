// Loads server/login config from sd:/wiiu/apps/ufin/config.json at
// startup, so credentials don't need to be hardcoded and rebuilt every
// time. Falls back to config.h's UFIN_SERVER_HOST/etc constants if the
// file is missing, unreadable, or malformed -- callers should treat a
// false return as "use the hardcoded defaults", not a fatal error.
//
// This is real-hardware-only in practice: Cemu doesn't emulate an SD
// card by default, so config.h's hardcoded values remain the practical
// way to configure a Cemu test build.

#pragma once
#include <string>

struct UfinConfig {
    std::string host;
    int port = 0;
    std::string username;
    std::string password;
};

// Tries to load and parse the config file. On success, fills outConfig
// and returns true. On failure, returns false and sets outError to a
// short human-readable reason (missing file, invalid JSON, missing
// field) -- meant to be shown on screen, not just logged.
bool loadConfigFromSD(UfinConfig& outConfig, std::string& outError);
