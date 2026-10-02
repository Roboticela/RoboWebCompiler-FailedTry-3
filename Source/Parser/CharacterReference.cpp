#include "Parser/CharacterReference.h"

#include "Core/Utf8.h"

namespace RoboWeb {

namespace {

struct NamedReference {
    const char* name;
    char32_t codePoint;
};

const NamedReference kNamed[] = {
    {"AElig", 0x00C6}, {"AMP", 0x0026}, {"Aacute", 0x00C1}, {"Acirc", 0x00C2}, {"Agrave", 0x00C0},
    {"Aring", 0x00C5}, {"Atilde", 0x00C3}, {"Auml", 0x00C4}, {"COPY", 0x00A9}, {"Ccedil", 0x00C7},
    {"ETH", 0x00D0}, {"Eacute", 0x00C9}, {"Ecirc", 0x00CA}, {"Egrave", 0x00C8}, {"Euml", 0x00CB},
    {"GT", 0x003E}, {"Iacute", 0x00CD}, {"Icirc", 0x00CE}, {"Igrave", 0x00CC}, {"Iuml", 0x00CF},
    {"LT", 0x003C}, {"Ntilde", 0x00D1}, {"Oacute", 0x00D3}, {"Ocirc", 0x00D4}, {"Ograve", 0x00D2},
    {"Oslash", 0x00D8}, {"Otilde", 0x00D5}, {"Ouml", 0x00D6}, {"QUOT", 0x0022}, {"REG", 0x00AE},
    {"THORN", 0x00DE}, {"Uacute", 0x00DA}, {"Ucirc", 0x00DB}, {"Ugrave", 0x00D9}, {"Uuml", 0x00DC},
    {"Yacute", 0x00DD}, {"aacute", 0x00E1}, {"acirc", 0x00E2}, {"acute", 0x00B4}, {"aelig", 0x00E6},
    {"agrave", 0x00E0}, {"amp", 0x0026}, {"apos", 0x0027}, {"aring", 0x00E5}, {"atilde", 0x00E3},
    {"auml", 0x00E4}, {"bdquo", 0x201E}, {"brvbar", 0x00A6}, {"bull", 0x2022}, {"ccedil", 0x00E7},
    {"cedil", 0x00B8}, {"cent", 0x00A2}, {"copy", 0x00A9}, {"curren", 0x00A4}, {"deg", 0x00B0},
    {"divide", 0x00F7}, {"eacute", 0x00E9}, {"ecirc", 0x00EA}, {"egrave", 0x00E8}, {"emsp", 0x2003},
    {"ensp", 0x2002}, {"eth", 0x00F0}, {"euml", 0x00EB}, {"euro", 0x20AC}, {"frac12", 0x00BD},
    {"frac14", 0x00BC}, {"frac34", 0x00BE}, {"gt", 0x003E}, {"hellip", 0x2026}, {"iacute", 0x00ED},
    {"icirc", 0x00EE}, {"iexcl", 0x00A1}, {"igrave", 0x00EC}, {"iquest", 0x00BF}, {"iuml", 0x00EF},
    {"laquo", 0x00AB}, {"ldquo", 0x201C}, {"lsquo", 0x2018}, {"lt", 0x003C}, {"macr", 0x00AF},
    {"mdash", 0x2014}, {"micro", 0x00B5}, {"middot", 0x00B7}, {"nbsp", 0x00A0}, {"ndash", 0x2013},
    {"not", 0x00AC}, {"ntilde", 0x00F1}, {"oacute", 0x00F3}, {"ocirc", 0x00F4}, {"ograve", 0x00F2},
    {"ordf", 0x00AA}, {"ordm", 0x00BA}, {"oslash", 0x00F8}, {"otilde", 0x00F5}, {"ouml", 0x00F6},
    {"para", 0x00B6}, {"plusmn", 0x00B1}, {"pound", 0x00A3}, {"quot", 0x0022}, {"raquo", 0x00BB},
    {"rdquo", 0x201D}, {"reg", 0x00AE}, {"rsquo", 0x2019}, {"sbquo", 0x201A}, {"sect", 0x00A7},
    {"shy", 0x00AD}, {"sup1", 0x00B9}, {"sup2", 0x00B2}, {"sup3", 0x00B3}, {"szlig", 0x00DF},
    {"thinsp", 0x2009}, {"thorn", 0x00FE}, {"times", 0x00D7}, {"trade", 0x2122}, {"uacute", 0x00FA},
    {"ucirc", 0x00FB}, {"ugrave", 0x00F9}, {"uml", 0x00A8}, {"uuml", 0x00FC}, {"yacute", 0x00FD},
    {"yen", 0x00A5}, {"yuml", 0x00FF},
};

const NamedReference* FindNamed(const std::string& name) {
    for (const NamedReference& reference : kNamed) {
        if (name == reference.name) {
            return &reference;
        }
    }
    return nullptr;
}

bool IsNameStart(char character) {
    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
}

bool IsNameChar(char character) {
    return IsNameStart(character) || (character >= '0' && character <= '9');
}

}  // namespace

bool DecodeCharacterReference(
    const std::string& source,
    std::size_t& index,
    std::string& output,
    Diagnostics* diagnostics,
    const std::string& path,
    int line,
    int column) {
    if (index >= source.size() || source[index] != '&') {
        return false;
    }
    const std::size_t start = index;
    ++index;
    if (index >= source.size()) {
        output.push_back('&');
        return true;
    }
    if (source[index] == '#') {
        ++index;
        int base = 10;
        if (index < source.size() && (source[index] == 'x' || source[index] == 'X')) {
            base = 16;
            ++index;
        }
        char32_t codePoint = 0;
        bool any = false;
        while (index < source.size()) {
            const char character = source[index];
            int digit = -1;
            if (character >= '0' && character <= '9') {
                digit = character - '0';
            } else if (base == 16 && character >= 'a' && character <= 'f') {
                digit = character - 'a' + 10;
            } else if (base == 16 && character >= 'A' && character <= 'F') {
                digit = character - 'A' + 10;
            }
            if (digit < 0) {
                break;
            }
            any = true;
            codePoint = codePoint * static_cast<char32_t>(base) + static_cast<char32_t>(digit);
            if (codePoint > 0x10FFFF) {
                codePoint = 0x110000;
            }
            ++index;
        }
        if (!any || index >= source.size() || source[index] != ';') {
            index = start + 1;
            output.push_back('&');
            if (diagnostics != nullptr) {
                diagnostics->Warning(path, line, column, "invalid numeric character reference");
            }
            return true;
        }
        ++index;
        if (codePoint == 0 || codePoint > 0x10FFFF) {
            codePoint = 0xFFFD;
            if (diagnostics != nullptr) {
                diagnostics->Warning(path, line, column, "numeric character reference is out of range");
            }
        }
        AppendUtf8(output, codePoint);
        return true;
    }
    if (!IsNameStart(source[index])) {
        output.push_back('&');
        return true;
    }
    const std::size_t nameStart = index;
    while (index < source.size() && IsNameChar(source[index])) {
        ++index;
    }
    const std::string name = source.substr(nameStart, index - nameStart);
    if (index >= source.size() || source[index] != ';') {
        index = start + 1;
        output.push_back('&');
        if (diagnostics != nullptr) {
            diagnostics->Warning(path, line, column, "character reference '" + name + "' is missing a semicolon");
        }
        return true;
    }
    ++index;
    const NamedReference* named = FindNamed(name);
    if (named == nullptr) {
        output.push_back('&');
        output.append(name);
        output.push_back(';');
        if (diagnostics != nullptr) {
            diagnostics->Warning(path, line, column, "unknown character reference '&" + name + ";'");
        }
        return true;
    }
    AppendUtf8(output, named->codePoint);
    return true;
}

}  // namespace RoboWeb
