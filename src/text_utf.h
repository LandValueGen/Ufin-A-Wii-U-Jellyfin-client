// UTF-16 -> UTF-8, for text coming back from the Wii U software keyboard
// (nn::swkbd hands out char16_t strings; Jellyfin wants UTF-8).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

inline std::string utf16ToUtf8(const char16_t* in, size_t maxUnits = 4096) {
    std::string out;
    if (!in) return out;
    for (size_t i = 0; i < maxUnits && in[i]; i++) {
        uint32_t cp = in[i];
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            // High surrogate: needs a following low surrogate.
            if (i + 1 < maxUnits && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (in[i + 1] - 0xDC00);
                i++;
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD; // stray low surrogate
        }
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

// Trims leading/trailing spaces (swkbd happily returns "  ").
inline std::string trimSpaces(const std::string& s) {
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(' ');
    return s.substr(a, b - a + 1);
}
