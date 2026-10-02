#pragma once

#include <cstdint>
#include <string>

namespace RoboWeb {

inline void AppendUtf8(std::string& text, char32_t codePoint) {
    if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
        codePoint = 0xFFFD;
    }
    if (codePoint < 0x80) {
        text.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800) {
        text.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint < 0x10000) {
        text.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        text.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        text.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        text.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

inline bool DecodeUtf8(const std::string& text, std::size_t& index, char32_t& codePoint) {
    if (index >= text.size()) {
        return false;
    }
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80) {
        codePoint = lead;
        ++index;
        return true;
    }
    int need = 0;
    char32_t value = 0;
    if ((lead & 0xE0) == 0xC0) {
        need = 1;
        value = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        need = 2;
        value = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        need = 3;
        value = lead & 0x07;
    } else {
        codePoint = 0xFFFD;
        ++index;
        return false;
    }
    if (index + static_cast<std::size_t>(need) >= text.size()) {
        codePoint = 0xFFFD;
        index = text.size();
        return false;
    }
    for (int extra = 0; extra < need; ++extra) {
        const unsigned char next = static_cast<unsigned char>(text[index + 1 + static_cast<std::size_t>(extra)]);
        if ((next & 0xC0) != 0x80) {
            codePoint = 0xFFFD;
            ++index;
            return false;
        }
        value = (value << 6) | (next & 0x3F);
    }
    index += static_cast<std::size_t>(need) + 1;
    codePoint = value;
    return true;
}

inline int Utf8Length(const std::string& text) {
    int length = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        char32_t codePoint = 0;
        DecodeUtf8(text, index, codePoint);
        ++length;
    }
    return length;
}

inline std::string ToLowerAscii(std::string text) {
    for (char& character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return text;
}

inline bool EqualsAsciiIgnoreCase(const std::string& left, const char* right) {
    std::size_t index = 0;
    while (right[index] != '\0') {
        if (index >= left.size()) {
            return false;
        }
        char character = left[index];
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        if (character != right[index]) {
            return false;
        }
        ++index;
    }
    return index == left.size();
}

}
