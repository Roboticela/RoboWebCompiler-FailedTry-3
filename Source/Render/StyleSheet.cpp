#include "Render/StyleSheet.h"

#include "Core/Utf8.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wininet.h>

namespace RoboWeb {

namespace {

const char* kFontAgent =
    "Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_8; de-at) AppleWebKit/533.21.1 (KHTML, like Gecko) Version/5.0.5 Safari/533.21.1";

std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\n' || text[begin] == '\r')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' || text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string StripComments(const std::string& css) {
    std::string clean;
    for (std::size_t index = 0; index < css.size(); ++index) {
        if (css[index] == '/' && index + 1 < css.size() && css[index + 1] == '*') {
            index += 2;
            while (index + 1 < css.size() && !(css[index] == '*' && css[index + 1] == '/')) {
                ++index;
            }
            if (index + 1 < css.size()) {
                ++index;
            }
            continue;
        }
        clean.push_back(css[index]);
    }
    return clean;
}

std::string Unquote(std::string text) {
    text = Trim(text);
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

bool IsGenericFamily(const std::string& name) {
    return name == "serif" || name == "sans-serif" || name == "monospace" || name == "cursive" || name == "fantasy" ||
           name == "system-ui" || name == "ui-sans-serif" || name == "ui-serif" || name == "ui-monospace";
}

std::string FirstFamily(const std::string& value) {
    std::string chosen;
    std::size_t start = 0;
    const std::string text = value + ",";
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != ',') {
            continue;
        }
        const std::string name = ToLowerAscii(Unquote(text.substr(start, index - start)));
        start = index + 1;
        if (name.empty()) {
            continue;
        }
        if (chosen.empty()) {
            chosen = name;
        }
        if (!IsGenericFamily(name)) {
            return name;
        }
    }
    return chosen;
}

bool ParseHexColor(const std::string& text, std::uint8_t& red, std::uint8_t& green, std::uint8_t& blue) {
    std::string value = ToLowerAscii(Trim(text));
    if (value == "black") {
        red = 0;
        green = 0;
        blue = 0;
        return true;
    }
    if (value == "white") {
        red = 255;
        green = 255;
        blue = 255;
        return true;
    }
    if (value == "red") {
        red = 255;
        green = 0;
        blue = 0;
        return true;
    }
    if (value == "green") {
        red = 0;
        green = 128;
        blue = 0;
        return true;
    }
    if (value == "blue") {
        red = 0;
        green = 0;
        blue = 255;
        return true;
    }
    if (value == "gray" || value == "grey") {
        red = 128;
        green = 128;
        blue = 128;
        return true;
    }
    if (value.empty() || value[0] != '#') {
        return false;
    }
    std::string hex = value.substr(1);
    if (hex.size() == 3) {
        hex = std::string{hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
    }
    if (hex.size() != 6) {
        return false;
    }
    char* end = nullptr;
    const long number = std::strtol(hex.c_str(), &end, 16);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    red = static_cast<std::uint8_t>((number >> 16) & 0xFF);
    green = static_cast<std::uint8_t>((number >> 8) & 0xFF);
    blue = static_cast<std::uint8_t>(number & 0xFF);
    return true;
}

int ParseWeight(const std::string& value) {
    const std::string text = ToLowerAscii(Trim(value));
    if (text == "bold" || text == "bolder") {
        return 700;
    }
    if (text == "normal" || text == "lighter") {
        return 400;
    }
    return std::atoi(text.c_str());
}

int ParseSizePx(const std::string& value) {
    const std::string text = ToLowerAscii(Trim(value));
    if (text == "0") {
        return 0;
    }
    if (text.size() > 2 && text.substr(text.size() - 2) == "px") {
        return std::max(0, std::atoi(text.c_str()));
    }
    if (!text.empty() && text.find_first_not_of("0123456789") == std::string::npos) {
        return std::atoi(text.c_str());
    }
    return -1;
}

void ApplyBoxEdges(ElementStyle& rule, const std::string& value, bool margin) {
    std::vector<int> edges;
    std::size_t index = 0;
    const std::string text = Trim(value);
    while (index < text.size()) {
        while (index < text.size() && text[index] == ' ') {
            ++index;
        }
        if (index >= text.size()) {
            break;
        }
        const std::size_t start = index;
        while (index < text.size() && text[index] != ' ') {
            ++index;
        }
        const int parsed = ParseSizePx(text.substr(start, index - start));
        if (parsed >= 0) {
            edges.push_back(parsed);
        }
    }
    if (edges.empty()) {
        return;
    }
    if (edges.size() == 1) {
        edges.push_back(edges[0]);
    }
    if (edges.size() == 2) {
        edges.push_back(edges[0]);
    }
    if (edges.size() == 3) {
        edges.push_back(edges[1]);
    }
    if (margin) {
        rule.marginTopPx = edges[0];
        rule.marginRightPx = edges[1];
        rule.marginBottomPx = edges[2];
        rule.marginLeftPx = edges[3];
    } else {
        rule.paddingTopPx = edges[0];
        rule.paddingRightPx = edges[1];
        rule.paddingBottomPx = edges[2];
        rule.paddingLeftPx = edges[3];
    }
}

void ApplyDeclaration(ElementStyle& rule, const std::string& property, const std::string& rawValue) {
    const std::string name = ToLowerAscii(Trim(property));
    const std::string value = Trim(rawValue);
    if (name == "font-family") {
        rule.fontFamily = FirstFamily(value);
    } else if (name == "font-weight") {
        const int weight = ParseWeight(value);
        if (weight > 0) {
            rule.fontWeight = weight;
        }
    } else if (name == "font-style") {
        const std::string style = ToLowerAscii(value);
        if (style == "italic" || style == "oblique") {
            rule.fontStyle = 1;
        } else if (style == "normal") {
            rule.fontStyle = 0;
        }
    } else if (name == "font-size") {
        rule.fontSizePx = ParseSizePx(value);
    } else if (name == "color") {
        rule.hasColor = ParseHexColor(value, rule.colorRed, rule.colorGreen, rule.colorBlue);
    } else if (name == "background-color" || name == "background") {
        rule.hasBackground = ParseHexColor(value, rule.backgroundRed, rule.backgroundGreen, rule.backgroundBlue);
    } else if (name == "display") {
        const std::string display = ToLowerAscii(value);
        if (display == "none") {
            rule.displayKind = 0;
        } else if (display == "block") {
            rule.displayKind = 1;
        } else if (display == "inline") {
            rule.displayKind = 2;
        } else if (display == "inline-block") {
            rule.displayKind = 3;
        } else if (display == "list-item") {
            rule.displayKind = 4;
        }
    } else if (name == "width") {
        rule.widthPx = ParseSizePx(value);
    } else if (name == "height") {
        rule.heightPx = ParseSizePx(value);
    } else if (name == "margin") {
        ApplyBoxEdges(rule, value, true);
    } else if (name == "margin-top") {
        rule.marginTopPx = ParseSizePx(value);
    } else if (name == "margin-right") {
        rule.marginRightPx = ParseSizePx(value);
    } else if (name == "margin-bottom") {
        rule.marginBottomPx = ParseSizePx(value);
    } else if (name == "margin-left") {
        rule.marginLeftPx = ParseSizePx(value);
    } else if (name == "padding") {
        ApplyBoxEdges(rule, value, false);
    } else if (name == "padding-top") {
        rule.paddingTopPx = ParseSizePx(value);
    } else if (name == "padding-right") {
        rule.paddingRightPx = ParseSizePx(value);
    } else if (name == "padding-bottom") {
        rule.paddingBottomPx = ParseSizePx(value);
    } else if (name == "padding-left") {
        rule.paddingLeftPx = ParseSizePx(value);
    } else if (name == "text-align") {
        const std::string align = ToLowerAscii(value);
        if (align == "left" || align == "start") {
            rule.textAlign = 0;
        } else if (align == "center") {
            rule.textAlign = 1;
        } else if (align == "right" || align == "end") {
            rule.textAlign = 2;
        }
    } else if (name == "text-decoration" || name == "text-decoration-line") {
        const std::string decoration = ToLowerAscii(value);
        if (decoration == "none") {
            rule.decoration = 0;
        } else if (decoration.find("underline") != std::string::npos) {
            rule.decoration = 1;
        } else if (decoration.find("line-through") != std::string::npos) {
            rule.decoration = 2;
        }
    } else if (name == "white-space") {
        const std::string space = ToLowerAscii(value);
        if (space == "normal") {
            rule.whiteSpace = 0;
        } else if (space == "nowrap") {
            rule.whiteSpace = 1;
        } else if (space == "pre" || space == "pre-wrap") {
            rule.whiteSpace = 2;
        }
    } else if (name == "border" || name == "border-width") {
        rule.borderPx = ParseSizePx(value);
        if (rule.borderPx < 0) {
            const std::size_t space = ToLowerAscii(value).find(' ');
            rule.borderPx = ParseSizePx(space == std::string::npos ? value : value.substr(0, space));
        }
    }
}

void ParseDeclarations(const std::string& body, ElementStyle& rule) {
    std::size_t start = 0;
    for (std::size_t index = 0; index <= body.size(); ++index) {
        if (index != body.size() && body[index] != ';') {
            continue;
        }
        const std::string piece = body.substr(start, index - start);
        start = index + 1;
        const std::size_t colon = piece.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        ApplyDeclaration(rule, piece.substr(0, colon), piece.substr(colon + 1));
    }
}

std::vector<unsigned char> Download(const std::string& url) {
    std::vector<unsigned char> bytes;
    HINTERNET internet = InternetOpenA(kFontAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (internet == nullptr) {
        return bytes;
    }
    HINTERNET remote = InternetOpenUrlA(internet, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (remote == nullptr) {
        InternetCloseHandle(internet);
        return bytes;
    }
    unsigned char buffer[8192];
    DWORD read = 0;
    while (InternetReadFile(remote, buffer, sizeof(buffer), &read) && read > 0) {
        if (bytes.size() + read > 8 * 1024 * 1024) {
            break;
        }
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    InternetCloseHandle(remote);
    InternetCloseHandle(internet);
    return bytes;
}

std::string DeclarationValue(const std::string& body, const char* property) {
    const std::string lower = ToLowerAscii(body);
    const std::string name = property;
    std::size_t cursor = 0;
    while (cursor < lower.size()) {
        const std::size_t found = lower.find(name, cursor);
        if (found == std::string::npos) {
            return "";
        }
        const bool boundary = found == 0 || lower[found - 1] == ';' || lower[found - 1] == ' ' || lower[found - 1] == '\n' ||
                              lower[found - 1] == '{';
        cursor = found + name.size();
        if (!boundary) {
            continue;
        }
        while (cursor < body.size() && (body[cursor] == ' ' || body[cursor] == '\t')) {
            ++cursor;
        }
        if (cursor >= body.size() || body[cursor] != ':') {
            continue;
        }
        ++cursor;
        const std::size_t end = body.find(';', cursor);
        return Trim(body.substr(cursor, end == std::string::npos ? std::string::npos : end - cursor));
    }
    return "";
}

std::string PreferredFontUrl(const std::string& src) {
    std::string fallback;
    std::size_t cursor = 0;
    while (cursor < src.size()) {
        const std::size_t url = src.find("url(", cursor);
        if (url == std::string::npos) {
            break;
        }
        std::size_t begin = url + 4;
        while (begin < src.size() && (src[begin] == ' ' || src[begin] == '\'' || src[begin] == '"')) {
            ++begin;
        }
        std::size_t end = begin;
        while (end < src.size() && src[end] != ')' && src[end] != ' ' && src[end] != '\'' && src[end] != '"') {
            ++end;
        }
        const std::string address = src.substr(begin, end - begin);
        const std::size_t format = ToLowerAscii(src.substr(end, std::min<std::size_t>(40, src.size() - end))).find("truetype");
        const bool ttf = address.find(".ttf") != std::string::npos || format != std::string::npos;
        if (fallback.empty()) {
            fallback = address;
        }
        if (ttf) {
            return address;
        }
        cursor = end;
    }
    return fallback;
}

struct StyleJob {
    std::vector<CompiledWebFont>* fonts = nullptr;
    bool downloadRemote = false;
    std::string note;
    std::string error;
    bool failed = false;
};

bool LooksLikeTrueType(const std::vector<unsigned char>& bytes) {
    if (bytes.size() < 12) {
        return false;
    }
    if (bytes[0] == 0x00 && bytes[1] == 0x01 && bytes[2] == 0x00 && bytes[3] == 0x00) {
        return true;
    }
    return bytes[0] == 't' && bytes[1] == 'r' && bytes[2] == 'u' && bytes[3] == 'e';
}

void RememberFamily(StyleJob& job, const std::string& family) {
    if (job.note.find(family) != std::string::npos) {
        return;
    }
    if (!job.note.empty()) {
        job.note += ", ";
    }
    job.note += family;
}

int HexValue(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool CoversCodePoint(const std::string& range, unsigned long codePoint) {
    if (Trim(range).empty()) {
        return true;
    }
    const std::string text = ToLowerAscii(range);
    std::size_t index = 0;
    while (index < text.size()) {
        const std::size_t mark = text.find("u+", index);
        if (mark == std::string::npos) {
            break;
        }
        index = mark + 2;
        unsigned long start = 0;
        int digits = 0;
        while (index < text.size()) {
            const int value = HexValue(text[index]);
            if (value < 0) {
                break;
            }
            start = start * 16 + static_cast<unsigned long>(value);
            ++digits;
            ++index;
        }
        if (digits == 0) {
            continue;
        }
        unsigned long end = start;
        if (index < text.size() && text[index] == '-') {
            ++index;
            end = 0;
            digits = 0;
            while (index < text.size()) {
                const int value = HexValue(text[index]);
                if (value < 0) {
                    break;
                }
                end = end * 16 + static_cast<unsigned long>(value);
                ++digits;
                ++index;
            }
            if (digits == 0) {
                end = start;
            }
        }
        if (codePoint >= start && codePoint <= end) {
            return true;
        }
    }
    return false;
}

void LoadFontFace(const std::string& body, StyleJob& job) {
    const std::string family = FirstFamily(DeclarationValue(body, "font-family"));
    const std::string source = DeclarationValue(body, "src");
    const std::string address = PreferredFontUrl(source);
    if (family.empty() || address.empty() || address.find("://") == std::string::npos) {
        return;
    }
    if (!CoversCodePoint(DeclarationValue(body, "unicode-range"), 0x41)) {
        return;
    }
    if (!job.downloadRemote || job.fonts == nullptr) {
        return;
    }
    const int weight = ParseWeight(DeclarationValue(body, "font-weight"));
    const std::string style = ToLowerAscii(DeclarationValue(body, "font-style"));
    const bool italic = style == "italic" || style == "oblique";
    const int storedWeight = weight > 0 ? weight : 400;
    for (const CompiledWebFont& existing : *job.fonts) {
        if (existing.family == family && existing.weight == storedWeight && existing.italic == italic) {
            return;
        }
    }
    const std::vector<unsigned char> bytes = Download(address);
    if (!LooksLikeTrueType(bytes)) {
        job.failed = true;
        job.error = "font file is not a TrueType font: " + address;
        return;
    }
    CompiledWebFont font;
    font.family = family;
    font.weight = storedWeight;
    font.italic = italic;
    font.bytes = bytes;
    job.fonts->push_back(std::move(font));
    RememberFamily(job, family);
}

void AddRule(Document& document, const std::string& selector, const std::string& body) {
    const std::string name = ToLowerAscii(Trim(selector));
    if (name.empty() || name[0] == '@') {
        return;
    }
    ElementStyle rule;
    rule.selector = name;
    ParseDeclarations(body, rule);
    if (!rule.fontFamily.empty() || rule.fontWeight >= 0 || rule.fontStyle >= 0 || rule.fontSizePx > 0 || rule.hasColor ||
        rule.hasBackground || rule.displayKind >= 0 || rule.widthPx >= 0 || rule.heightPx >= 0 || rule.marginTopPx >= 0 ||
        rule.marginRightPx >= 0 || rule.marginBottomPx >= 0 || rule.marginLeftPx >= 0 || rule.paddingTopPx >= 0 ||
        rule.paddingRightPx >= 0 || rule.paddingBottomPx >= 0 || rule.paddingLeftPx >= 0 || rule.textAlign >= 0 ||
        rule.decoration >= 0 || rule.whiteSpace >= 0 || rule.borderPx >= 0) {
        document.styleRules.push_back(rule);
    }
}

void ParseStylesheet(Document& document, const std::string& css, StyleJob& job) {
    const std::string text = StripComments(css);
    std::size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() && (text[index] == ' ' || text[index] == '\t' || text[index] == '\n' || text[index] == '\r')) {
            ++index;
        }
        if (index >= text.size()) {
            break;
        }
        if (text.compare(index, 10, "@font-face") == 0) {
            const std::size_t open = text.find('{', index);
            const std::size_t close = open == std::string::npos ? std::string::npos : text.find('}', open);
            if (open == std::string::npos || close == std::string::npos) {
                break;
            }
            LoadFontFace(text.substr(open + 1, close - open - 1), job);
            index = close + 1;
            continue;
        }
        if (text[index] == '@') {
            const std::size_t open = text.find('{', index);
            const std::size_t semi = text.find(';', index);
            if (open != std::string::npos && (semi == std::string::npos || open < semi)) {
                const std::size_t close = text.find('}', open);
                if (close == std::string::npos) {
                    break;
                }
                index = close + 1;
            } else if (semi != std::string::npos) {
                index = semi + 1;
            } else {
                break;
            }
            continue;
        }
        const std::size_t open = text.find('{', index);
        if (open == std::string::npos) {
            break;
        }
        const std::size_t close = text.find('}', open);
        if (close == std::string::npos) {
            break;
        }
        const std::string selectors = text.substr(index, open - index);
        const std::string body = text.substr(open + 1, close - open - 1);
        std::size_t start = 0;
        for (std::size_t cursor = 0; cursor <= selectors.size(); ++cursor) {
            if (cursor == selectors.size() || selectors[cursor] == ',') {
                AddRule(document, selectors.substr(start, cursor - start), body);
                start = cursor + 1;
            }
        }
        index = close + 1;
    }
}

bool RelHas(const std::string& rel, const char* token) {
    std::size_t start = 0;
    const std::string text = rel + " ";
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != ' ' && text[index] != '\t') {
            continue;
        }
        if (ToLowerAscii(text.substr(start, index - start)) == token) {
            return true;
        }
        start = index + 1;
    }
    return false;
}

void Walk(Document& document, Node& node, StyleJob& job) {
    if (node.kind == ElementKind::Link) {
        const std::string rel = AttributeValue(node, "rel");
        const std::string href = AttributeValue(node, "href");
        if (job.downloadRemote && RelHas(rel, "stylesheet") && href.find("://") != std::string::npos) {
            const std::vector<unsigned char> bytes = Download(href);
            if (bytes.empty()) {
                job.failed = true;
                job.error = "stylesheet was not loaded: " + href;
            } else {
                ParseStylesheet(document, std::string(bytes.begin(), bytes.end()), job);
            }
        }
    } else if (node.kind == ElementKind::Style) {
        ParseStylesheet(document, GatherText(node), job);
    }
    if (job.failed) {
        return;
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        Walk(document, *child, job);
        if (job.failed) {
            return;
        }
    }
}

void ApplyStyleTextInternal(ElementStyle& rule, const std::string& declarations) {
    ParseDeclarations(declarations, rule);
}

}  // namespace

void ApplyStyleText(ElementStyle& rule, const std::string& declarations) {
    ApplyStyleTextInternal(rule, declarations);
}

void LoadDocumentStyles(Document& document) {
    if (document.stylesLoaded || document.root == nullptr) {
        document.stylesLoaded = true;
        return;
    }
    document.stylesLoaded = true;
    StyleJob job;
    Walk(document, *document.root, job);
}

bool CompileLinkedStyles(Document& document, std::vector<CompiledWebFont>& fonts, std::string& error) {
    fonts.clear();
    if (document.root == nullptr) {
        document.stylesLoaded = true;
        return true;
    }
    StyleJob job;
    job.fonts = &fonts;
    job.downloadRemote = true;
    Walk(document, *document.root, job);
    document.stylesLoaded = true;
    if (job.failed) {
        error = job.error.empty() ? "stylesheet was not loaded" : job.error;
        return false;
    }
    if (!job.note.empty() && document.status.empty()) {
        document.status = "font " + job.note;
    }
    return true;
}

}
