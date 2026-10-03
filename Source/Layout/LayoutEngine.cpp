#include "Layout/LayoutEngine.h"

#include "Catalog/SvgCatalog.h"
#include "Core/Utf8.h"
#include "Render/BitmapFont.h"
#include "Render/ImageDecoder.h"
#include "Render/StyleSheet.h"
#include "Runtime/MiniScript.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace RoboWeb {

namespace {

double gSvgAnimationTime = 0;

int Collapse(int first, int second) {
    if (first < 0 || second < 0) {
        const int negative = std::min(first, second);
        const int positive = std::max(first, 0) > std::max(second, 0) ? std::max(first, 0) : std::max(second, 0);
        return positive + (negative < 0 ? negative : 0);
    }
    return std::max(first, second);
}

std::string TrimSpaces(const std::string& text) {
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

std::uint8_t ChannelByte(double value) {
    if (!(value > 0)) {
        return 0;
    }
    if (value >= 255) {
        return 255;
    }
    return static_cast<std::uint8_t>(value + 0.5);
}

std::uint8_t AlphaByte(double value) {
    if (!(value > 0)) {
        return 0;
    }
    if (value >= 1) {
        return 255;
    }
    return static_cast<std::uint8_t>(value * 255.0 + 0.5);
}

// Reads a CSS number, skipping separators used by the color and transform
// grammars (spaces, commas, and the rgba() slash).
bool ReadCssNumber(const std::string& text, std::size_t& index, double& number) {
    while (index < text.size() && (text[index] == ' ' || text[index] == ',' || text[index] == '/' || text[index] == '\t' ||
                                    text[index] == '\n' || text[index] == '\r')) {
        ++index;
    }
    if (index >= text.size()) {
        return false;
    }
    const std::size_t start = index;
    if (text[index] == '+' || text[index] == '-') {
        ++index;
    }
    bool digits = false;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        ++index;
        digits = true;
    }
    if (index < text.size() && text[index] == '.') {
        ++index;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
            ++index;
            digits = true;
        }
    }
    if (!digits) {
        index = start;
        return false;
    }
    if (index < text.size() && (text[index] == 'e' || text[index] == 'E')) {
        std::size_t cursor = index + 1;
        if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-')) {
            ++cursor;
        }
        bool exponentDigits = false;
        while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
            ++cursor;
            exponentDigits = true;
        }
        if (exponentDigits) {
            index = cursor;
        }
    }
    number = std::atof(text.substr(start, index - start).c_str());
    return true;
}

bool ReadCssNumberEx(const std::string& text, std::size_t& index, double& number, bool& percent) {
    if (!ReadCssNumber(text, index, number)) {
        return false;
    }
    percent = false;
    std::size_t probe = index;
    while (probe < text.size() && (text[probe] == ' ' || text[probe] == '\t')) {
        ++probe;
    }
    if (probe < text.size() && text[probe] == '%') {
        percent = true;
        index = probe + 1;
    }
    return true;
}

int HexDigit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

bool ParseHexColor(const std::string& value, Color& color) {
    std::string hex = value.substr(1);
    if (hex.size() == 3 || hex.size() == 4) {
        std::string expanded;
        for (char character : hex) {
            expanded.push_back(character);
            expanded.push_back(character);
        }
        hex = expanded;
    }
    if (hex.size() != 6 && hex.size() != 8) {
        return false;
    }
    int channels[4] = {0, 0, 0, 255};
    for (std::size_t index = 0; index < hex.size(); index += 2) {
        const int high = HexDigit(hex[index]);
        const int low = HexDigit(hex[index + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        channels[index / 2] = high * 16 + low;
    }
    color = Color{static_cast<std::uint8_t>(channels[0]), static_cast<std::uint8_t>(channels[1]),
                  static_cast<std::uint8_t>(channels[2]), static_cast<std::uint8_t>(channels[3])};
    return true;
}

bool ParseHue(const std::string& body, std::size_t& index, double& degrees) {
    if (!ReadCssNumber(body, index, degrees)) {
        return false;
    }
    if (body.compare(index, 3, "deg") == 0) {
        index += 3;
    } else if (body.compare(index, 4, "grad") == 0) {
        degrees = degrees * 0.9;
        index += 4;
    } else if (body.compare(index, 3, "rad") == 0) {
        degrees = degrees * 180.0 / std::acos(-1.0);
        index += 3;
    } else if (body.compare(index, 4, "turn") == 0) {
        degrees = degrees * 360.0;
        index += 4;
    }
    while (degrees < 0) {
        degrees += 360.0;
    }
    while (degrees >= 360.0) {
        degrees -= 360.0;
    }
    return true;
}

bool ParseColorFunction(const std::string& value, Color& color) {
    const std::size_t open = value.find('(');
    const std::size_t close = value.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open) {
        return false;
    }
    const std::string body = value.substr(open + 1, close - open - 1);
    if (value.compare(0, 4, "hsl(") == 0 || value.compare(0, 5, "hsla(") == 0) {
        std::size_t index = 0;
        double hue = 0;
        double saturation = 0;
        double lightness = 0;
        bool ignoredPercent = false;
        if (!ParseHue(body, index, hue) || !ReadCssNumberEx(body, index, saturation, ignoredPercent) ||
            !ReadCssNumberEx(body, index, lightness, ignoredPercent)) {
            return false;
        }
        // Both the percentage and number forms are interpreted as percentages.
        saturation /= 100.0;
        lightness /= 100.0;
        saturation = std::max(0.0, std::min(1.0, saturation));
        lightness = std::max(0.0, std::min(1.0, lightness));
        double alpha = 1.0;
        while (index < body.size() && (body[index] == ' ' || body[index] == '\t')) {
            ++index;
        }
        if (index < body.size() && (body[index] == ',' || body[index] == '/')) {
            std::size_t alphaIndex = index;
            bool alphaPercent = false;
            if (ReadCssNumberEx(body, alphaIndex, alpha, alphaPercent)) {
                if (alphaPercent) {
                    alpha /= 100.0;
                }
                index = alphaIndex;
            }
        }
        for (std::size_t cursor = index; cursor < body.size(); ++cursor) {
            if (body[cursor] != ' ' && body[cursor] != '\t' && body[cursor] != ',' && body[cursor] != '/') {
                return false;
            }
        }
        const double chroma = (1.0 - std::abs(2.0 * lightness - 1.0)) * saturation;
        const double segment = hue / 60.0;
        const double second = chroma * (1.0 - std::abs(std::fmod(segment, 2.0) - 1.0));
        double red = 0;
        double green = 0;
        double blue = 0;
        if (segment < 1) {
            red = chroma;
            green = second;
        } else if (segment < 2) {
            red = second;
            green = chroma;
        } else if (segment < 3) {
            green = chroma;
            blue = second;
        } else if (segment < 4) {
            green = second;
            blue = chroma;
        } else if (segment < 5) {
            red = second;
            blue = chroma;
        } else {
            red = chroma;
            blue = second;
        }
        const double base = lightness - chroma / 2.0;
        color = Color{ChannelByte((red + base) * 255.0), ChannelByte((green + base) * 255.0), ChannelByte((blue + base) * 255.0),
                      AlphaByte(alpha)};
        return true;
    }
    std::size_t index = 0;
    double channels[3] = {0, 0, 0};
    bool percent[3] = {false, false, false};
    for (int slot = 0; slot < 3; ++slot) {
        if (!ReadCssNumberEx(body, index, channels[slot], percent[slot])) {
            return false;
        }
    }
    double alpha = 1.0;
    std::size_t probe = index;
    while (probe < body.size() && (body[probe] == ' ' || body[probe] == '\t')) {
        ++probe;
    }
    if (probe < body.size() && (body[probe] == ',' || body[probe] == '/')) {
        std::size_t alphaIndex = probe;
        double parsedAlpha = 1.0;
        bool alphaPercent = false;
        if (ReadCssNumberEx(body, alphaIndex, parsedAlpha, alphaPercent)) {
            if (alphaPercent) {
                parsedAlpha /= 100.0;
            }
            alpha = parsedAlpha;
            index = alphaIndex;
        }
    }
    for (std::size_t cursor = index; cursor < body.size(); ++cursor) {
        if (body[cursor] != ' ' && body[cursor] != '\t' && body[cursor] != ',' && body[cursor] != '/') {
            return false;
        }
    }
    for (int slot = 0; slot < 3; ++slot) {
        if (percent[slot]) {
            channels[slot] = channels[slot] * 255.0 / 100.0;
        }
    }
    color = Color{ChannelByte(channels[0]), ChannelByte(channels[1]), ChannelByte(channels[2]), AlphaByte(alpha)};
    return true;
}

bool ParseColor(const std::string& text, Color& color) {
    const std::string value = ToLowerAscii(TrimSpaces(text));
    if (value.empty()) {
        return false;
    }
    if (value == "transparent") {
        color = Color{0, 0, 0, 0};
        return true;
    }
    if (value[0] == '#') {
        return ParseHexColor(value, color);
    }
    if (value.compare(0, 4, "rgb(") == 0 || value.compare(0, 5, "rgba(") == 0 || value.compare(0, 4, "hsl(") == 0 ||
        value.compare(0, 5, "hsla(") == 0) {
        return ParseColorFunction(value, color);
    }
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    if (LookupSvgColorKeyword(value, red, green, blue)) {
        color = Color{red, green, blue, 255};
        return true;
    }
    return false;
}

int ParsePx(const std::string& text, int containing, bool& ok) {
    ok = false;
    if (text.empty()) {
        return 0;
    }
    std::string value = ToLowerAscii(text);
    bool percent = false;
    if (!value.empty() && value.back() == '%') {
        percent = true;
        value.pop_back();
    } else if (value.size() > 2 && value.substr(value.size() - 2) == "px") {
        value.resize(value.size() - 2);
    }
    try {
        std::size_t used = 0;
        const int number = std::stoi(value, &used);
        if (used != value.size()) {
            return 0;
        }
        ok = true;
        if (percent) {
            return containing * number / 100;
        }
        return number;
    } catch (...) {
        return 0;
    }
}

std::string InputType(const Node& node) {
    std::string type = ToLowerAscii(AttributeValue(node, "type"));
    if (type.empty()) {
        return "text";
    }
    const char* types =
        "button checkbox color date datetime-local email file hidden image month number password radio range reset "
        "search submit tel text time url week";
    std::string token;
    for (const char* cursor = types; *cursor != '\0'; ++cursor) {
        if (*cursor == ' ') {
            if (token == type) {
                return type;
            }
            token.clear();
        } else {
            token.push_back(*cursor);
        }
    }
    if (token == type) {
        return type;
    }
    return "text";
}

bool IsFieldEditor(const Node& node) {
    if (node.kind == ElementKind::Textarea) {
        return true;
    }
    if (node.kind != ElementKind::Input) {
        return false;
    }
    const std::string type = InputType(node);
    return type != "checkbox" && type != "radio" && type != "button" && type != "submit" && type != "reset" && type != "file" &&
           type != "image" && type != "hidden" && type != "color" && type != "range";
}

std::string FieldEditedText(const Node& node) {
    if (node.kind == ElementKind::Textarea) {
        return node.liveValue;
    }
    if (node.kind == ElementKind::Input && InputType(node) == "password") {
        return std::string(node.liveValue.size(), '*');
    }
    if (node.kind == ElementKind::Input) {
        return node.liveValue;
    }
    return "";
}

struct FieldLine {
    std::string text;
    int start = 0;
};

std::vector<FieldLine> SplitFieldLines(const std::string& text) {
    std::vector<FieldLine> lines;
    int start = 0;
    for (int index = 0; index <= static_cast<int>(text.size()); ++index) {
        if (index == static_cast<int>(text.size()) || text[static_cast<std::size_t>(index)] == '\n') {
            FieldLine line;
            line.start = start;
            line.text = text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(index - start));
            lines.push_back(line);
            start = index + 1;
        }
    }
    if (lines.empty()) {
        lines.push_back(FieldLine{});
    }
    return lines;
}

int NormalizeCaretByte(const std::string& text, int caretByte) {
    if (caretByte < 0 || caretByte > static_cast<int>(text.size())) {
        return static_cast<int>(text.size());
    }
    return caretByte;
}

int CaretLineIndex(const std::vector<FieldLine>& lines, int caretByte) {
    for (int index = 0; index < static_cast<int>(lines.size()); ++index) {
        const int end = lines[static_cast<std::size_t>(index)].start + static_cast<int>(lines[static_cast<std::size_t>(index)].text.size());
        const bool last = index + 1 == static_cast<int>(lines.size());
        if (last || caretByte <= end) {
            return index;
        }
    }
    return 0;
}

bool IsHidden(const Node& node) {
    if (node.type != Node::Type::Element) {
        return false;
    }
    if (node.hiddenByScript || HasAttribute(node, "hidden")) {
        return true;
    }
    if (HasAttribute(node, "popover") && !node.popoverOpen) {
        return true;
    }
    if (node.kind == ElementKind::Dialog && !node.open) {
        return true;
    }
    if (node.kind == ElementKind::Input && InputType(node) == "hidden") {
        return true;
    }
    if (node.kind == ElementKind::Rp && node.parent != nullptr && node.parent->kind == ElementKind::Ruby) {
        return true;
    }
    return SpecFor(node.kind).display == DisplayKind::None;
}

std::string CollapsedText(const std::string& text, bool pre) {
    if (pre) {
        std::string result;
        for (char character : text) {
            if (character == '\r') {
                continue;
            }
            if (character == '\t') {
                result.append(TAB_WIDTH_COLUMNS, ' ');
            } else {
                result.push_back(character);
            }
        }
        return result;
    }
    std::string flat;
    for (char character : text) {
        if (character == '\n' || character == '\r' || character == '\t' || character == '\f') {
            flat.push_back(' ');
        } else {
            flat.push_back(character);
        }
    }
    std::string collapsed;
    bool previousSpace = false;
    for (char character : flat) {
        if (character == ' ') {
            if (!previousSpace) {
                collapsed.push_back(' ');
            }
            previousSpace = true;
        } else {
            collapsed.push_back(character);
            previousSpace = false;
        }
    }
    return collapsed;
}

std::string ApplyTextTransform(const std::string& text, int transform) {
    if (transform == 0 || text.empty()) {
        return text;
    }
    std::string result;
    bool wordStart = true;
    for (unsigned char character : text) {
        char letter = static_cast<char>(character);
        if (letter == ' ' || letter == '\n' || letter == '\t') {
            wordStart = true;
        } else if (transform == 3) {
            if (wordStart && letter >= 'a' && letter <= 'z') {
                letter = static_cast<char>(letter - 'a' + 'A');
            }
            wordStart = false;
        }
        if (transform == 1 && letter >= 'a' && letter <= 'z') {
            letter = static_cast<char>(letter - 'a' + 'A');
        } else if (transform == 2 && letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter - 'A' + 'a');
        }
        result.push_back(letter);
    }
    return result;
}

Node* SelectedOption(Node& selectNode) {
    Node* first = nullptr;
    std::vector<Node*> stack{&selectNode};
    while (!stack.empty()) {
        Node* current = stack.back();
        stack.pop_back();
        for (int index = static_cast<int>(current->children.size()) - 1; index >= 0; --index) {
            Node* child = current->children[static_cast<std::size_t>(index)].get();
            if (child->kind == ElementKind::Option) {
                if (first == nullptr) {
                    first = child;
                }
                if (child->selected || HasAttribute(*child, "selected")) {
                    return child;
                }
            } else if (child->kind == ElementKind::Optgroup) {
                stack.push_back(child);
            }
        }
    }
    return first;
}

std::string ControlLabel(Node& node) {
    if (node.kind == ElementKind::Input) {
        const std::string type = InputType(node);
        if (type == "submit") {
            const std::string value = node.liveValue.empty() ? AttributeValue(node, "value") : node.liveValue;
            return value.empty() ? "Submit" : value;
        }
        if (type == "reset") {
            return node.liveValue.empty() ? "Reset" : node.liveValue;
        }
        if (type == "button") {
            return node.liveValue.empty() ? "" : node.liveValue;
        }
        if (type == "file") {
            if (!node.liveValue.empty()) {
                return node.liveValue;
            }
            return "Choose file";
        }
        if (type == "password" || IsFieldEditor(node)) {
            return FieldEditedText(node);
        }
        return node.liveValue;
    }
    if (node.kind == ElementKind::Textarea) {
        return FieldEditedText(node);
    }
    if (node.kind == ElementKind::Button || node.kind == ElementKind::Geolocation) {
        if (node.kind == ElementKind::Geolocation) {
            return "Use location";
        }
        const std::string text = GatherText(node);
        if (!text.empty()) {
            return CollapsedText(text, false);
        }
        return node.liveValue;
    }
    if (node.kind == ElementKind::Select || node.kind == ElementKind::SelectedContent) {
        Node* selectNode = node.kind == ElementKind::Select ? &node : FindAncestor(node, ElementKind::Select);
        if (selectNode == nullptr) {
            return "";
        }
        Node* option = SelectedOption(*selectNode);
        if (option == nullptr) {
            return "";
        }
        const std::string label = AttributeValue(*option, "label");
        if (!label.empty()) {
            return label;
        }
        return CollapsedText(GatherText(*option), false);
    }
    return "";
}

struct Piece {
    Node* node = nullptr;
    std::string text;
    ResolvedStyle style;
    int width = 0;
    int height = 0;
    bool lineBreak = false;
    bool atomic = false;
    bool marker = false;
};

std::vector<Node*> FlowChildren(Node& node) {
    std::vector<Node*> flow;
    Node* shadow = nullptr;
    for (const std::unique_ptr<Node>& child : node.children) {
        if (child->kind == ElementKind::Template && HasAttribute(*child, "shadowrootmode")) {
            shadow = child.get();
        }
    }
    if (shadow != nullptr) {
        for (const std::unique_ptr<Node>& child : shadow->children) {
            flow.push_back(child.get());
        }
        return flow;
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        flow.push_back(child.get());
    }
    return flow;
}

struct Engine {
    struct CssColorFilter {
        int brightnessMilli = 1000;
        int contrastMilli = 1000;
        int grayscaleMilli = 0;
        int invertMilli = 0;
        int saturateMilli = 1000;
        int sepiaMilli = 0;
        int hueRotateDeg = 0;
        int opacityMilli = 1000;
    };

    Document* document = nullptr;
    LayoutResult* result = nullptr;
    int viewportWidth = 0;
    int viewportHeight = 0;
    std::vector<Node*> overlays;
    std::vector<CssColorFilter> colorFilters;
    int paintHidden = 0;

    static int ClampByte(int value) {
        if (value < 0) {
            return 0;
        }
        if (value > 255) {
            return 255;
        }
        return value;
    }

    void ApplyCssFilters(Color& color) {
        if (paintHidden > 0) {
            color.alpha = 0;
            return;
        }
        for (int index = static_cast<int>(colorFilters.size()) - 1; index >= 0; --index) {
            const CssColorFilter& filter = colorFilters[static_cast<std::size_t>(index)];
            int red = color.red;
            int green = color.green;
            int blue = color.blue;
            if (filter.brightnessMilli != 1000) {
                red = red * filter.brightnessMilli / 1000;
                green = green * filter.brightnessMilli / 1000;
                blue = blue * filter.brightnessMilli / 1000;
            }
            if (filter.contrastMilli != 1000) {
                red = (red - 128) * filter.contrastMilli / 1000 + 128;
                green = (green - 128) * filter.contrastMilli / 1000 + 128;
                blue = (blue - 128) * filter.contrastMilli / 1000 + 128;
            }
            if (filter.grayscaleMilli > 0) {
                const int gray = (ClampByte(red) * 2126 + ClampByte(green) * 7152 + ClampByte(blue) * 722) / 10000;
                const int weight = filter.grayscaleMilli > 1000 ? 1000 : filter.grayscaleMilli;
                red = (red * (1000 - weight) + gray * weight) / 1000;
                green = (green * (1000 - weight) + gray * weight) / 1000;
                blue = (blue * (1000 - weight) + gray * weight) / 1000;
            }
            if (filter.sepiaMilli > 0) {
                const int weight = filter.sepiaMilli > 1000 ? 1000 : filter.sepiaMilli;
                const int sepiaRed = (ClampByte(red) * 393 + ClampByte(green) * 769 + ClampByte(blue) * 189) / 1000;
                const int sepiaGreen = (ClampByte(red) * 349 + ClampByte(green) * 686 + ClampByte(blue) * 168) / 1000;
                const int sepiaBlue = (ClampByte(red) * 272 + ClampByte(green) * 534 + ClampByte(blue) * 131) / 1000;
                red = (red * (1000 - weight) + sepiaRed * weight) / 1000;
                green = (green * (1000 - weight) + sepiaGreen * weight) / 1000;
                blue = (blue * (1000 - weight) + sepiaBlue * weight) / 1000;
            }
            if (filter.saturateMilli != 1000) {
                const int amount = filter.saturateMilli < 0 ? 0 : filter.saturateMilli;
                const int nextRed = (red * (213 + 787 * amount / 1000) + green * (715 - 715 * amount / 1000) + blue * (72 - 72 * amount / 1000)) / 1000;
                const int nextGreen = (red * (213 - 213 * amount / 1000) + green * (715 + 285 * amount / 1000) + blue * (72 - 72 * amount / 1000)) / 1000;
                const int nextBlue = (red * (213 - 213 * amount / 1000) + green * (715 - 715 * amount / 1000) + blue * (72 + 928 * amount / 1000)) / 1000;
                red = nextRed;
                green = nextGreen;
                blue = nextBlue;
            }
            if (filter.hueRotateDeg % 360 != 0) {
                const double radians = static_cast<double>(filter.hueRotateDeg) * 3.141592653589793 / 180.0;
                const double cosine = std::cos(radians);
                const double sine = std::sin(radians);
                const double nextRed = red * (0.2126 + cosine * 0.7874 - sine * 0.2126) + green * (0.7152 - cosine * 0.7152 - sine * 0.7152) +
                    blue * (0.0722 - cosine * 0.0722 + sine * 0.9278);
                const double nextGreen = red * (0.2126 - cosine * 0.2126 + sine * 0.143) + green * (0.7152 + cosine * 0.2848 + sine * 0.14) +
                    blue * (0.0722 - cosine * 0.0722 - sine * 0.283);
                const double nextBlue = red * (0.2126 - cosine * 0.2126 - sine * 0.7874) + green * (0.7152 - cosine * 0.7152 + sine * 0.7152) +
                    blue * (0.0722 + cosine * 0.9278 + sine * 0.0722);
                red = static_cast<int>(std::lround(nextRed));
                green = static_cast<int>(std::lround(nextGreen));
                blue = static_cast<int>(std::lround(nextBlue));
            }
            if (filter.invertMilli > 0) {
                const int weight = filter.invertMilli > 1000 ? 1000 : filter.invertMilli;
                red = (ClampByte(red) * (1000 - weight) + (255 - ClampByte(red)) * weight) / 1000;
                green = (ClampByte(green) * (1000 - weight) + (255 - ClampByte(green)) * weight) / 1000;
                blue = (ClampByte(blue) * (1000 - weight) + (255 - ClampByte(blue)) * weight) / 1000;
            }
            color.red = static_cast<std::uint8_t>(ClampByte(red));
            color.green = static_cast<std::uint8_t>(ClampByte(green));
            color.blue = static_cast<std::uint8_t>(ClampByte(blue));
            if (filter.opacityMilli < 1000) {
                const int faded = color.alpha * std::max(0, filter.opacityMilli) / 1000;
                color.alpha = static_cast<std::uint8_t>(ClampByte(faded));
            }
        }
    }

    struct PaintScope {
        Engine& owner;
        std::size_t depth;
        int hidden;

        PaintScope(Engine& owner, const ResolvedStyle& style) : owner(owner), depth(owner.colorFilters.size()), hidden(owner.paintHidden) {
            const int opacity = style.opacityMilli * style.filterOpacityMilli / 1000;
            if (style.hasColorFilter || opacity < 1000) {
                CssColorFilter filter;
                filter.brightnessMilli = style.brightnessMilli;
                filter.contrastMilli = style.contrastMilli;
                filter.grayscaleMilli = style.grayscaleMilli;
                filter.invertMilli = style.invertMilli;
                filter.saturateMilli = style.saturateMilli;
                filter.sepiaMilli = style.sepiaMilli;
                filter.hueRotateDeg = style.hueRotateDeg;
                filter.opacityMilli = opacity;
                owner.colorFilters.push_back(filter);
            }
            if (style.visibilityHidden) {
                owner.paintHidden = 1;
            } else if (style.visibilityVisible) {
                owner.paintHidden = 0;
            }
        }

        ~PaintScope() {
            owner.colorFilters.resize(depth);
            owner.paintHidden = hidden;
        }
    };

    void AddBox(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed) {
        if (width < 0) {
            width = 0;
        }
        if (height < 0) {
            height = 0;
        }
        ElementBox box;
        box.node = &node;
        box.tag = node.tagName;
        box.x = x;
        box.y = y;
        box.width = width;
        box.height = height;
        box.fixed = fixed;
        box.fontSizePx = style.fontSizePx;
        box.bold = style.bold;
        box.italic = style.italic;
        box.monospace = style.monospace;
        box.underline = style.underline;
        box.strike = style.strike;
        box.display = style.display;
        box.fontFamily = style.fontFamily;
        result->boxes.push_back(box);
    }

    void EmitFill(int x, int y, int width, int height, Color color, bool fixed, bool clipped, int clipX, int clipY, int clipWidth,
        int clipHeight) {
        DrawOp op;
        op.kind = DrawOp::Kind::Fill;
        op.x = x;
        op.y = y;
        op.width = width;
        op.height = height;
        op.color = color;
        op.fixed = fixed;
        op.clipped = clipped;
        op.clipX = clipX;
        op.clipY = clipY;
        op.clipWidth = clipWidth;
        op.clipHeight = clipHeight;
        result->ops.push_back(op);
    }

    void Fill(int x, int y, int width, int height, Color color, bool fixed, bool clipped = false, int clipX = 0, int clipY = 0,
        int clipWidth = 0, int clipHeight = 0) {
        ApplyCssFilters(color);
        if (color.alpha == 0 || width <= 0 || height <= 0) {
            return;
        }
        const bool clipActive = activeClip != nullptr && !activeClip->clipScopes.empty();
        if (!clipActive && activeMask == nullptr) {
            EmitFill(x, y, width, height, color, fixed, clipped, clipX, clipY, clipWidth, clipHeight);
            return;
        }
        if (!clipActive) {
            // Only a mask is in play: each pixel keeps the fraction of the
            // paint its mask value allows.
            for (int row = 0; row < height; ++row) {
                for (int column = 0; column < width; ++column) {
                    const Color masked = MaskedColor(color, *activeMask, x + column + 0.5, y + row + 0.5);
                    if (masked.alpha == 0) {
                        continue;
                    }
                    EmitFill(x + column, y + row, 1, 1, masked, fixed, clipped, clipX, clipY, clipWidth, clipHeight);
                }
            }
            return;
        }
        // A clip-path masks the shape pixel by pixel, so every span is cut into
        // the runs that survive all of the enclosing clip scopes.
        for (int row = 0; row < height; ++row) {
            int column = 0;
            while (column < width) {
                if (!SvgClipAllows(*activeClip, x + column + 0.5, y + row + 0.5)) {
                    ++column;
                    continue;
                }
                if (activeMask != nullptr) {
                    const Color masked = MaskedColor(color, *activeMask, x + column + 0.5, y + row + 0.5);
                    if (masked.alpha != 0) {
                        EmitFill(x + column, y + row, 1, 1, masked, fixed, clipped, clipX, clipY, clipWidth, clipHeight);
                    }
                    ++column;
                    continue;
                }
                int run = 1;
                while (column + run < width && SvgClipAllows(*activeClip, x + column + run + 0.5, y + row + 0.5)) {
                    ++run;
                }
                EmitFill(x + column, y + row, run, 1, color, fixed, clipped, clipX, clipY, clipWidth, clipHeight);
                column += run;
            }
        }
    }

    void FillCoverage(double x, double y, double width, double height, Color color, bool fixed) {
        if (color.alpha == 0 || width <= 0 || height <= 0) {
            return;
        }
        const int left = static_cast<int>(std::floor(x));
        const int top = static_cast<int>(std::floor(y));
        const int right = static_cast<int>(std::ceil(x + width));
        const int bottom = static_cast<int>(std::ceil(y + height));
        for (int row = top; row < bottom; ++row) {
            const double rowTop = std::max(y, static_cast<double>(row));
            const double rowBottom = std::min(y + height, static_cast<double>(row + 1));
            const double rowCover = std::max(0.0, rowBottom - rowTop);
            for (int column = left; column < right; ++column) {
                const double columnLeft = std::max(x, static_cast<double>(column));
                const double columnRight = std::min(x + width, static_cast<double>(column + 1));
                const double cover = rowCover * std::max(0.0, columnRight - columnLeft);
                if (cover <= 0.001) {
                    continue;
                }
                Color pixel = color;
                pixel.alpha = static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, static_cast<double>(color.alpha) * cover)));
                Fill(column, row, 1, 1, pixel, fixed);
            }
        }
    }

    void Stroke(int x, int y, int width, int height, Color color, bool fixed) {
        ApplyCssFilters(color);
        if (color.alpha == 0) {
            return;
        }
        DrawOp op;
        op.kind = DrawOp::Kind::Stroke;
        op.x = x;
        op.y = y;
        op.width = width;
        op.height = height;
        op.color = color;
        op.fixed = fixed;
        result->ops.push_back(op);
    }

    void Text(int x, int y, const std::string& text, const ResolvedStyle& style, bool fixed, bool clipped = false, int clipX = 0,
        int clipY = 0, int clipWidth = 0, int clipHeight = 0) {
        if (text.empty()) {
            return;
        }
        const std::string shown = ApplyTextTransform(text, style.textTransform);
        if (style.mark || style.background.alpha != 0) {
            Color background = style.mark ? Color{255, 255, 0, 255} : style.background;
            Fill(x, y, MeasureTextPx(shown, style.fontSizePx, style.bold, style.italic, style.fontFamily, style.letterSpacingPx, style.wordSpacingPx), LineHeightPx(style.fontSizePx), background, fixed,
                clipped, clipX, clipY, clipWidth, clipHeight);
        }
        DrawOp op;
        op.kind = DrawOp::Kind::Text;
        op.x = x;
        op.y = y + style.baselineShift;
        op.text = shown;
        op.fontSizePx = style.fontSizePx;
        op.letterSpacingPx = style.letterSpacingPx;
        op.wordSpacingPx = style.wordSpacingPx;
        op.color = style.color;
        ApplyCssFilters(op.color);
        if (op.color.alpha == 0) {
            return;
        }
        op.bold = style.bold;
        op.italic = style.italic;
        op.monospace = style.monospace;
        op.underline = style.underline;
        op.strike = style.strike;
        op.decorationColor = style.decorationColor;
        op.hasDecorationColor = style.hasDecorationColor;
        op.underlineOffsetPx = style.underlineOffsetPx;
        op.fixed = fixed;
        op.fontFamily = style.fontFamily;
        op.clipped = clipped;
        op.clipX = clipX;
        op.clipY = clipY;
        op.clipWidth = clipWidth;
        op.clipHeight = clipHeight;
        result->ops.push_back(op);
    }

    void PaintFieldValue(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed) {
        const int padX = CONTROL_BORDER_PX + CONTROL_PADDING_X_PX;
        const int padY = CONTROL_BORDER_PX + CONTROL_PADDING_Y_PX;
        const int innerX = x + padX;
        const int innerY = y + padY;
        const int innerW = std::max(1, width - padX * 2);
        const int innerH = std::max(1, height - padY * 2);
        const bool editing = IsFieldEditor(node);
        const std::string edited = editing ? FieldEditedText(node) : std::string();
        bool placeholder = false;
        std::string shown = ControlLabel(node);
        if (editing && edited.empty() && HasAttribute(node, "placeholder")) {
            shown = AttributeValue(node, "placeholder");
            placeholder = true;
        } else if (editing) {
            shown = edited;
        }
        const bool focused = editing && document->focus == &node;
        const int caretByte = focused && !placeholder ? NormalizeCaretByte(edited, document->caretOffset) : 0;
        const int shift = editing ? TextScrollShift(placeholder ? std::string() : edited, caretByte, innerW, style.fontSizePx, style.bold,
                                          style.italic, style.fontFamily)
                                  : 0;
        ResolvedStyle paint = style;
        if (placeholder) {
            paint.color = Color{117, 117, 117, 255};
        }
        const std::vector<FieldLine> lines = SplitFieldLines(shown);
        const int lineHeight = LineHeightPx(style.fontSizePx);
        for (int index = 0; index < static_cast<int>(lines.size()); ++index) {
            const int lineY = innerY + index * lineHeight;
            if (lineY >= innerY + innerH) {
                break;
            }
            Text(innerX - shift, lineY, lines[static_cast<std::size_t>(index)].text, paint, fixed, true, innerX, innerY, innerW, innerH);
        }
        if (!focused || !document->caretOn) {
            return;
        }
        const std::vector<FieldLine> caretLines = SplitFieldLines(placeholder ? std::string() : edited);
        const int lineIndex = CaretLineIndex(caretLines, caretByte);
        const FieldLine& line = caretLines[static_cast<std::size_t>(lineIndex)];
        int column = caretByte - line.start;
        if (column < 0) {
            column = 0;
        }
        if (column > static_cast<int>(line.text.size())) {
            column = static_cast<int>(line.text.size());
        }
        const int prefix = MeasureTextPx(line.text.substr(0, static_cast<std::size_t>(column)), style.fontSizePx, style.bold, style.italic,
            style.fontFamily);
        Fill(innerX - shift + prefix, innerY + lineIndex * lineHeight, 1, lineHeight, Color{0, 0, 0, 255}, fixed, true, innerX, innerY, innerW,
            innerH);
    }

    int ReplacedSize(Node& node, const ResolvedStyle& style, int containing, int& width, int& height) {
        bool widthOk = false;
        bool heightOk = false;
        width = ParsePx(AttributeValue(node, "width"), containing, widthOk);
        height = ParsePx(AttributeValue(node, "height"), viewportHeight, heightOk);
        if (node.kind == ElementKind::Img || node.kind == ElementKind::Input) {
            std::string source = AttributeValue(node, "src");
            if (source.empty() && node.parent != nullptr && node.parent->kind == ElementKind::Picture) {
                for (const std::unique_ptr<Node>& sibling : node.parent->children) {
                    if (sibling.get() == &node) {
                        break;
                    }
                    if (sibling->kind != ElementKind::Source) {
                        continue;
                    }
                    source = AttributeValue(*sibling, "src");
                    if (source.empty()) {
                        source = AttributeValue(*sibling, "srcset");
                        const std::size_t space = source.find_first_of(" \t,");
                        if (space != std::string::npos) {
                            source = source.substr(0, space);
                        }
                    }
                    if (!source.empty()) {
                        break;
                    }
                }
            }
            if (source.empty()) {
                source = AttributeValue(node, "srcset");
                const std::size_t space = source.find_first_of(" \t,");
                if (space != std::string::npos) {
                    source = source.substr(0, space);
                }
            }
            const std::string path = ResolveUrl(*document, source);
            DecodedImage image = path.find("://") == std::string::npos ? DecodeImageFile(path) : DecodedImage();
            if (!widthOk) {
                width = image.ok ? image.width : 0;
            }
            if (!heightOk) {
                height = image.ok ? image.height : 0;
            }
            if (!image.ok) {
                const std::string alt = AttributeValue(node, "alt");
                if (!alt.empty() && !widthOk && !heightOk) {
                    width = MeasureTextPx(alt, style.fontSizePx, style.bold, style.italic, style.fontFamily);
                    height = LineHeightPx(style.fontSizePx);
                } else if (!widthOk && !heightOk && !source.empty()) {
                    width = 16;
                    height = 16;
                }
            }
            return image.ok ? 1 : 0;
        }
        if (!widthOk) {
            width = node.kind == ElementKind::Audio ? DEFAULT_REPLACED_WIDTH_PX : DEFAULT_REPLACED_WIDTH_PX;
        }
        if (!heightOk) {
            if (node.kind == ElementKind::Audio) {
                height = HasAttribute(node, "controls") ? AUDIO_CONTROL_HEIGHT_PX : 0;
                if (height == 0) {
                    width = 0;
                }
            } else if (node.kind == ElementKind::Meter || node.kind == ElementKind::Progress) {
                width = widthOk ? width : 160;
                height = 16;
            } else {
                height = DEFAULT_REPLACED_HEIGHT_PX;
            }
        }
        return 0;
    }

    void PaintReplaced(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed) {
        const Color border{118, 118, 118, 255};
        const Color face{240, 240, 240, 255};
        if (node.kind == ElementKind::Img || (node.kind == ElementKind::Input && InputType(node) == "image")) {
            std::string source = AttributeValue(node, "src");
            if (source.empty()) {
                source = AttributeValue(node, "srcset");
                const std::size_t space = source.find_first_of(" \t,");
                if (space != std::string::npos) {
                    source = source.substr(0, space);
                }
            }
            const std::string path = ResolveUrl(*document, source);
            DecodedImage image = path.find("://") == std::string::npos ? DecodeImageFile(path) : DecodedImage();
            if (image.ok) {
                DrawOp op;
                op.kind = DrawOp::Kind::Image;
                op.x = x;
                op.y = y;
                op.width = width;
                op.height = height;
                op.imagePath = path;
                op.deferImage = ToLowerAscii(AttributeValue(node, "decoding")) == "async";
                op.fixed = fixed;
                result->ops.push_back(op);
                std::string fetchMode = "omit";
                if (HasAttribute(node, "crossorigin")) {
                    fetchMode = ToLowerAscii(AttributeValue(node, "crossorigin")) == "use-credentials" ? "include" : "anonymous";
                }
                NoteResourceFetch(source, ToLowerAscii(AttributeValue(node, "fetchpriority")), fetchMode);
                if (style.border > 0) {
                    Stroke(x, y, width, height, Color{0, 0, 0, 255}, fixed);
                }
            } else {
                Stroke(x, y, width, height, border, fixed);
                const std::string alt = AttributeValue(node, "alt");
                if (!alt.empty()) {
                    Text(x, y, alt, style, fixed);
                }
            }
            return;
        }
        if (node.kind == ElementKind::Hr) {
            const int thickness = height > 0 ? height : HR_THICKNESS_PX;
            const bool colored = HasAttribute(node, "color") || AttributeValue(node, "style").find("color") != std::string::npos;
            Fill(x, y, width, thickness, colored ? style.color : Color{128, 128, 128, 255}, fixed);
            return;
        }
        if (node.kind == ElementKind::Meter || node.kind == ElementKind::Progress) {
            Fill(x, y, width, height, face, fixed);
            Stroke(x, y, width, height, border, fixed);
            const double maximum = std::max(1.0, AttributeValue(node, "max").empty() ? (node.kind == ElementKind::Progress ? 1.0 : 1.0) : std::atof(AttributeValue(node, "max").c_str()));
            const double value = AttributeValue(node, "value").empty() ? 0 : std::atof(AttributeValue(node, "live").c_str()) + std::atof(AttributeValue(node, "value").c_str());
            const double clamped = std::max(0.0, std::min(maximum, AttributeValue(node, "value").empty() && node.kind == ElementKind::Progress ? 0 : std::atof(AttributeValue(node, "value").c_str())));
            const int fillWidth = static_cast<int>(width * (clamped / maximum));
            Color fillColor{0, 128, 0, 255};
            if (node.kind == ElementKind::Meter) {
                const double low = std::atof(AttributeValue(node, "low").c_str());
                const double high = AttributeValue(node, "high").empty() ? maximum : std::atof(AttributeValue(node, "high").c_str());
                const double optimum = AttributeValue(node, "optimum").empty() ? (low + high) / 2.0 : std::atof(AttributeValue(node, "optimum").c_str());
                if (std::abs(clamped - optimum) <= std::max(0.001, (high - low) / 2.0)) {
                    fillColor = Color{0, 128, 0, 255};
                } else if (clamped < low || clamped > high) {
                    fillColor = Color{255, 165, 0, 255};
                }
            }
            Fill(x + 1, y + 1, std::max(0, fillWidth - 2), std::max(0, height - 2), fillColor, fixed);
            (void)value;
            return;
        }
        Fill(x, y, width, height, face, fixed);
        Stroke(x, y, width, height, border, fixed);
        if (node.kind == ElementKind::Video) {
            const std::string poster = ResolveUrl(*document, AttributeValue(node, "poster"));
            DecodedImage image = poster.find("://") == std::string::npos ? DecodeImageFile(poster) : DecodedImage();
            if (image.ok) {
                DrawOp op;
                op.kind = DrawOp::Kind::Image;
                op.x = x;
                op.y = y;
                op.width = width;
                op.height = height;
                op.imagePath = poster;
                op.fixed = fixed;
                result->ops.push_back(op);
                return;
            }
        }
        std::string label = AttributeValue(node, "srcdoc");
        if (label.empty()) {
            label = AttributeValue(node, "src");
        }
        if (label.empty()) {
            label = AttributeValue(node, "data");
        }
        if (node.kind == ElementKind::Canvas) {
            const std::string fallback = CollapsedText(GatherText(node), false);
            if (!fallback.empty()) {
                label = fallback;
            }
        }
        if (label.empty()) {
            label = node.tagName;
        }
        if (HasAttribute(node, "controls")) {
            label = "controls " + label;
        }
        Text(x + 4, y + 4, label, style, fixed);
    }

    void MeasureAtomic(Node& node, const ResolvedStyle& style, int containing, int& width, int& height) {
        if (node.kind == ElementKind::Input) {
            const std::string type = InputType(node);
            if (type == "checkbox" || type == "radio") {
                width = 13;
                height = 13;
                return;
            }
            if (type == "range") {
                width = 160;
                height = 16;
                return;
            }
            if (type == "color") {
                width = 32;
                height = 24;
                return;
            }
            if (type == "image") {
                ReplacedSize(node, style, containing, width, height);
                return;
            }
            if (type == "hidden") {
                width = 0;
                height = 0;
                return;
            }
            bool sizeOk = false;
            int columns = ParsePx(AttributeValue(node, "size"), containing, sizeOk);
            if (!sizeOk || columns <= 0) {
                columns = DEFAULT_INPUT_SIZE;
            }
            width = CharAdvancePx(style.fontSizePx, style.fontFamily) * columns + CONTROL_PADDING_X_PX * 2 + CONTROL_BORDER_PX * 2;
            height = LineHeightPx(style.fontSizePx) + CONTROL_PADDING_Y_PX * 2 + CONTROL_BORDER_PX * 2;
            return;
        }
        if (node.kind == ElementKind::Textarea) {
            bool colsOk = false;
            bool rowsOk = false;
            int cols = ParsePx(AttributeValue(node, "cols"), containing, colsOk);
            int rows = ParsePx(AttributeValue(node, "rows"), containing, rowsOk);
            if (!colsOk || cols <= 0) {
                cols = DEFAULT_TEXTAREA_COLS;
            }
            if (!rowsOk || rows <= 0) {
                rows = DEFAULT_TEXTAREA_ROWS;
            }
            width = CharAdvancePx(style.fontSizePx, style.fontFamily) * cols + CONTROL_PADDING_X_PX * 2 + CONTROL_BORDER_PX * 2;
            height = LineHeightPx(style.fontSizePx) * rows + CONTROL_PADDING_Y_PX * 2 + CONTROL_BORDER_PX * 2;
            return;
        }
        if (node.kind == ElementKind::Button || node.kind == ElementKind::Geolocation) {
            const std::string label = ControlLabel(node);
            width = MeasureTextPx(label, style.fontSizePx, style.bold, style.italic, style.fontFamily) + CONTROL_PADDING_X_PX * 2 +
                    CONTROL_BORDER_PX * 2 + 8;
            height = LineHeightPx(style.fontSizePx) + CONTROL_PADDING_Y_PX * 2 + CONTROL_BORDER_PX * 2;
            return;
        }
        if (node.kind == ElementKind::Select) {
            const std::string label = ControlLabel(node);
            width = MeasureTextPx(label, style.fontSizePx, style.bold, style.italic, style.fontFamily) + 28;
            height = LineHeightPx(style.fontSizePx) + CONTROL_PADDING_Y_PX * 2 + CONTROL_BORDER_PX * 2;
            bool sizeOk = false;
            const int listSize = ParsePx(AttributeValue(node, "size"), containing, sizeOk);
            if (sizeOk && listSize > 1) {
                height = LineHeightPx(style.fontSizePx) * listSize;
            }
            return;
        }
        if (node.kind == ElementKind::Marquee) {
            width = style.widthPx > 0 ? style.widthPx : containing;
            height = style.heightPx > 0 ? style.heightPx : LineHeightPx(style.fontSizePx);
            return;
        }
        if (node.kind == ElementKind::Hr) {
            width = style.widthPx > 0 ? style.widthPx : containing;
            height = style.heightPx > 0 ? style.heightPx : HR_THICKNESS_PX;
            return;
        }
        if (node.kind == ElementKind::Svg) {
            width = style.widthPx > 0 ? style.widthPx : 300;
            height = style.heightPx > 0 ? style.heightPx : 150;
            return;
        }
        if (node.kind == ElementKind::Br) {
            width = 0;
            height = LineHeightPx(style.fontSizePx);
            return;
        }
        ReplacedSize(node, style, containing, width, height);
    }

    // SVG 2 promotes the shape geometry to real CSS properties (cx, cy, r, rx,
    // ry, x, y, width, height, d, pathLength, textLength). The cascade beats the
    // presentation attribute, exactly like it does for every other property.
    bool IsSvgGeometryProperty(const char* name) {
        static const char* const GEOMETRY_PROPERTIES[] = {"cx", "cy", "r",  "rx",        "ry",        "x",
                                                          "y",  "width", "height", "d", "pathlength", "textlength"};
        for (const char* property : GEOMETRY_PROPERTIES) {
            if (std::string(property) == name) {
                return true;
            }
        }
        return false;
    }

    std::string SvgGeometryValue(const Node& node, const char* name) {
        if (IsSvgGeometryProperty(name)) {
            const std::string css = SvgCssProperty(node, name);
            if (!css.empty()) {
                return css;
            }
        }
        return AttributeValue(node, name);
    }

    // The CSS form of the geometry property wraps path data in path("..."),
    // while the attribute form carries it bare; both reach the renderer.
    std::string SvgPathData(const Node& node) {
        std::string data = SvgGeometryValue(node, "d");
        const std::string trimmed = TrimSpaces(data);
        if (ToLowerAscii(trimmed).compare(0, 5, "path(") == 0 && trimmed.back() == ')') {
            data = TrimSpaces(trimmed.substr(5, trimmed.size() - 6));
            if (data.size() >= 2 && (data.front() == '"' || data.front() == '\'') && data.back() == data.front()) {
                data = data.substr(1, data.size() - 2);
            }
        }
        return data;
    }

    int SvgNumber(const Node& node, const char* name, int fallback) {
        const std::string text = SvgGeometryValue(node, name);
        if (text.empty()) {
            return fallback;
        }
        return std::atoi(text.c_str());
    }

    double SvgReal(const Node& node, const char* name, double fallback) {
        const std::string text = SvgGeometryValue(node, name);
        if (text.empty()) {
            return fallback;
        }
        return std::strtod(text.c_str(), nullptr);
    }

    // Gaussian edge coverage: the fraction of a half-plane that a 1-D Gaussian
    // blur leaves at a distance from its edge. A blurred box is the product of
    // its two edge profiles, and a blurred ring is the difference of two, so a
    // real normal curve replaces the old linear ramp everywhere.
    double SvgGaussianEdge(double distance, double sigma) {
        if (!(sigma > 0)) {
            return distance >= 0 ? 1.0 : 0.0;
        }
        return 0.5 * std::erfc(distance / (sigma * std::sqrt(2.0)));
    }

    // The Gaussian cumulative distribution; a blurred box edge is the difference
    // of two of them, which is exact for any pixel position, inside or out.
    double SvgGaussianCdf(double z) {
        return 0.5 * std::erfc(-z / std::sqrt(2.0));
    }

    // Coverage of a Gaussian-blurred axis-aligned box at one pixel center.
    double SvgBlurredBoxCoverage(double pixelX, double pixelY, int boxX, int boxY, int boxWidth, int boxHeight, double sigma) {
        if (!(sigma > 0)) {
            const double centerX = pixelX + 0.5;
            const double centerY = pixelY + 0.5;
            const bool inside = centerX >= boxX && centerX < boxX + boxWidth && centerY >= boxY && centerY < boxY + boxHeight;
            return inside ? 1.0 : 0.0;
        }
        const double centerX = pixelX + 0.5;
        const double centerY = pixelY + 0.5;
        const double spanX = SvgGaussianCdf((boxX + boxWidth - centerX) / sigma) - SvgGaussianCdf((boxX - centerX) / sigma);
        const double spanY = SvgGaussianCdf((boxY + boxHeight - centerY) / sigma) - SvgGaussianCdf((boxY - centerY) / sigma);
        return spanX * spanY;
    }

    void FillEllipseShadow(int centerX, int centerY, int radiusX, int radiusY, int blur, Color color, bool fixed) {
        if (blur <= 0) {
            FillEllipse(centerX, centerY, radiusX, radiusY, color, fixed);
            return;
        }
        // The shadow is a Gaussian-blurred disc, so each co-centric ring carries
        // the slice of the normal curve between its two radii.
        const double sigma = static_cast<double>(blur);
        const int extraMax = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
        for (int extra = extraMax; extra >= 0; --extra) {
            Color layer = color;
            const double outer = SvgGaussianEdge(static_cast<double>(extra) - 0.5, sigma);
            const double inner = SvgGaussianEdge(static_cast<double>(extra) + 0.5, sigma);
            const int alpha = static_cast<int>(color.alpha * std::max(0.0, outer - inner) + 0.5);
            layer.alpha = static_cast<std::uint8_t>(alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha));
            if (layer.alpha == 0) {
                continue;
            }
            FillEllipse(centerX, centerY, radiusX + extra, radiusY + extra, layer, fixed);
        }
    }

    void FillEllipse(int centerX, int centerY, int radiusX, int radiusY, Color color, bool fixed) {
        if (radiusX < 1 || radiusY < 1 || color.alpha == 0) {
            return;
        }
        // The analytic chord of the ellipse at every sub-scanline gives exact
        // horizontal coverage, so the rim blends like a browser paints it
        // instead of snapping to whole pixels.
        const double radiusRealX = radiusX;
        const double radiusRealY = radiusY;
        for (int row = -radiusY - 1; row <= radiusY; ++row) {
            SvgCoverageRow coverage;
            for (int sub = 0; sub < SVG_AA_SUBROWS; ++sub) {
                const double sampleY = static_cast<double>(row) + (static_cast<double>(sub) + 0.5) / SVG_AA_SUBROWS;
                const double normalized = sampleY / radiusRealY;
                if (normalized <= -1.0 || normalized >= 1.0) {
                    continue;
                }
                const double half = radiusRealX * std::sqrt(1.0 - normalized * normalized);
                coverage.AddSpan(centerX - half, centerX + half, 1.0 / SVG_AA_SUBROWS);
            }
            EmitCoverageRow(coverage, 0, 0, centerY + row, color, fixed);
        }
    }

    struct SvgPoint {
        double x = 0;
        double y = 0;
    };

    // Antialiased coverage rasterization. Every pixel row is sampled on
    // SVG_AA_SUBROWS sub-scanlines; the horizontal overlap each span has with a
    // pixel column is its area contribution, exactly the coverage a browser
    // multiplies the paint by. Edges therefore blend with the backdrop instead
    // of snapping to whole pixels.
    struct SvgCoverageSpan {
        double left = 0;
        double right = 0;
    };

    struct SvgCoverageRow {
        int first = 0;
        int last = -1;
        std::vector<double> cover;

        void AddSpan(double left, double right, double weight) {
            if (!(right > left) || !(weight > 0)) {
                return;
            }
            const int firstColumn = static_cast<int>(std::floor(left));
            const int lastColumn = static_cast<int>(std::ceil(right)) - 1;
            if (cover.empty()) {
                first = firstColumn;
                last = lastColumn;
                cover.assign(static_cast<std::size_t>(lastColumn - firstColumn + 1), 0.0);
            } else if (firstColumn < first || lastColumn > last) {
                // Spans may arrive from any contour, so the buffer grows when a
                // wider span appears. This keeps one row's coverage contiguous.
                const int newFirst = std::min(first, firstColumn);
                const int newLast = std::max(last, lastColumn);
                std::vector<double> grown(static_cast<std::size_t>(newLast - newFirst + 1), 0.0);
                for (std::size_t index = 0; index < cover.size(); ++index) {
                    grown[static_cast<std::size_t>(first + static_cast<int>(index) - newFirst)] = cover[index];
                }
                cover.swap(grown);
                first = newFirst;
                last = newLast;
            }
            for (int column = firstColumn; column <= lastColumn; ++column) {
                const double overlap = std::min(right, static_cast<double>(column + 1)) - std::max(left, static_cast<double>(column));
                if (overlap > 0) {
                    cover[static_cast<std::size_t>(column - first)] += overlap * weight;
                }
            }
        }

        bool Empty() const {
            return last < first;
        }
    };

    // Crossing positions of every contour at one sample line, resolved by the
    // fill rule. Crossings keep their fractional x so edges stay smooth.
    void CollectContourSpans(const std::vector<std::vector<SvgPoint>>& contours, double sampleY, bool evenOdd,
        std::vector<SvgCoverageSpan>& spans) {
        struct Crossing {
            double x = 0;
            int wind = 0;
        };
        std::vector<Crossing> crossings;
        for (const std::vector<SvgPoint>& contour : contours) {
            if (contour.size() < 2) {
                continue;
            }
            for (std::size_t index = 0; index < contour.size(); ++index) {
                const SvgPoint& start = contour[index];
                const SvgPoint& end = contour[(index + 1) % contour.size()];
                const bool crosses = (start.y <= sampleY && end.y > sampleY) || (end.y <= sampleY && start.y > sampleY);
                if (!crosses || start.y == end.y) {
                    continue;
                }
                const double distance = (sampleY - start.y) / (end.y - start.y);
                crossings.push_back(Crossing{start.x + distance * (end.x - start.x), end.y > start.y ? 1 : -1});
            }
        }
        std::sort(crossings.begin(), crossings.end(), [](const Crossing& left, const Crossing& right) { return left.x < right.x; });
        spans.clear();
        if (evenOdd) {
            for (std::size_t index = 0; index + 1 < crossings.size(); index += 2) {
                if (crossings[index + 1].x > crossings[index].x) {
                    spans.push_back(SvgCoverageSpan{crossings[index].x, crossings[index + 1].x});
                }
            }
            return;
        }
        int wind = 0;
        double cursor = crossings.empty() ? 0 : crossings.front().x;
        for (const Crossing& crossing : crossings) {
            if (wind != 0 && crossing.x > cursor) {
                spans.push_back(SvgCoverageSpan{cursor, crossing.x});
            }
            wind += crossing.wind;
            cursor = crossing.x;
        }
    }

    int SvgCoverageAlpha(int colorAlpha, double coverage) {
        if (!(coverage > 0)) {
            return 0;
        }
        if (coverage >= 1) {
            return colorAlpha;
        }
        const int scaled = static_cast<int>(colorAlpha * coverage + 0.5);
        return scaled > 255 ? 255 : scaled;
    }

    // Paints one row of coverage as horizontal runs; neighbouring pixels that
    // share a blended alpha are merged so a solid shape costs a few ops a row.
    void EmitCoverageRow(const SvgCoverageRow& row, int originX, int originY, int pixelRow, Color color, bool fixed) {
        if (row.Empty() || color.alpha == 0) {
            return;
        }
        int runStart = row.first;
        int runAlpha = SvgCoverageAlpha(color.alpha, row.cover[0]);
        for (std::size_t index = 1; index <= row.cover.size(); ++index) {
            const bool atEnd = index == row.cover.size();
            const int alpha = atEnd ? -1 : SvgCoverageAlpha(color.alpha, row.cover[index]);
            if (atEnd || alpha != runAlpha) {
                if (runAlpha > 0) {
                    Color paint = color;
                    paint.alpha = static_cast<std::uint8_t>(runAlpha);
                    const int runLength = static_cast<int>(index) - (runStart - row.first);
                    Fill(originX + runStart, originY + pixelRow, runLength, 1, paint, fixed);
                }
                runStart = row.first + static_cast<int>(index);
                runAlpha = alpha;
            }
        }
    }

    void FillPolygon(const std::vector<SvgPoint>& points, int originX, int originY, Color color, bool fixed, bool evenOdd = true) {
        if (points.size() < 3 || color.alpha == 0) {
            return;
        }
        std::vector<std::vector<SvgPoint>> contours;
        contours.push_back(points);
        FillContours(contours, originX, originY, color, fixed, evenOdd);
    }

    // Splits a stroke-dasharray value into dash lengths. Negative values and an
    // all-zero pattern mean "none", exactly like the SVG stroke-dasharray rules.
    bool ParseSvgDashArray(const std::string& text, std::vector<double>& dashes) {
        dashes.clear();
        const std::string value = ToLowerAscii(TrimSpaces(text));
        if (value.empty() || value == "none") {
            return false;
        }
        std::size_t index = 0;
        double number = 0;
        while (ReadSvgNumber(value, index, number)) {
            if (number < 0) {
                dashes.clear();
                return false;
            }
            dashes.push_back(number);
        }
        if (dashes.empty()) {
            return false;
        }
        if (dashes.size() % 2 == 1) {
            const std::size_t count = dashes.size();
            for (std::size_t slot = 0; slot < count; ++slot) {
                dashes.push_back(dashes[slot]);
            }
        }
        double sum = 0;
        for (double dash : dashes) {
            sum += dash;
        }
        if (!(sum > 0)) {
            dashes.clear();
            return false;
        }
        return true;
    }

    bool SvgHasDashArray(const std::string& text) {
        std::vector<double> dashes;
        return ParseSvgDashArray(text, dashes);
    }

    // --- Exact SVG stroke geometry --------------------------------------------
    // A stroke is rasterized as the union of the pieces the SVG painting model
    // describes: one rectangle per segment, a wedge or disc at every join and
    // caps at the subpath ends. Every contour carries the same winding
    // direction, so a single nonzero fill unions the overlapping pieces the
    // same way a browser's stroke outline covers them.

    void SvgAppendContour(std::vector<std::vector<SvgPoint>>& contours, std::vector<SvgPoint> contour) {
        if (contour.size() < 3) {
            return;
        }
        double area = 0;
        for (std::size_t index = 0; index < contour.size(); ++index) {
            const SvgPoint& start = contour[index];
            const SvgPoint& end = contour[(index + 1) % contour.size()];
            area += start.x * end.y - end.x * start.y;
        }
        if (area < 0) {
            std::reverse(contour.begin(), contour.end());
        }
        contours.push_back(std::move(contour));
    }

    void SvgAppendRoundBlob(std::vector<std::vector<SvgPoint>>& contours, double centerX, double centerY, double radius, double shiftX, double shiftY) {
        if (!(radius > 0)) {
            return;
        }
        const double pi = std::acos(-1.0);
        // One step per pixel of circumference keeps the polygon within a
        // fraction of a pixel of the true disc at every radius.
        const int steps = std::max(SVG_STROKE_ROUND_MIN_STEPS,
            std::min(SVG_STROKE_ROUND_MAX_STEPS, static_cast<int>(std::ceil(radius * 2 * pi))));
        std::vector<SvgPoint> blob;
        blob.reserve(static_cast<std::size_t>(steps));
        for (int step = 0; step < steps; ++step) {
            const double angle = 2.0 * pi * static_cast<double>(step) / static_cast<double>(steps);
            blob.push_back(SvgPoint{centerX + shiftX + std::cos(angle) * radius, centerY + shiftY + std::sin(angle) * radius});
        }
        SvgAppendContour(contours, std::move(blob));
    }

    // Odd stroke widths keep the crisp rasterizer's convention: the band lands
    // on the pixel row or column the line runs through instead of straddling it.
    void SvgAxisShift(int thickness, const SvgPoint& from, const SvgPoint& to, double& shiftX, double& shiftY) {
        shiftX = 0;
        shiftY = 0;
        if (thickness % 2 == 0) {
            return;
        }
        const double deltaX = to.x - from.x;
        const double deltaY = to.y - from.y;
        const double length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        if (!(length > 0)) {
            return;
        }
        const double unitX = deltaX / length;
        const double unitY = deltaY / length;
        if (std::fabs(unitY) < SVG_STROKE_FLAT_EPSILON) {
            shiftY = 0.5;
        } else if (std::fabs(unitX) < SVG_STROKE_FLAT_EPSILON) {
            shiftX = 0.5;
        }
    }

    void SvgAppendSegmentQuad(std::vector<std::vector<SvgPoint>>& contours, const SvgPoint& from, const SvgPoint& to, double halfWidth, double extendFrom,
        double extendTo, double shiftX, double shiftY) {
        const double deltaX = to.x - from.x;
        const double deltaY = to.y - from.y;
        const double length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        if (!(length > 0)) {
            return;
        }
        const double unitX = deltaX / length;
        const double unitY = deltaY / length;
        const double normalX = -unitY * halfWidth;
        const double normalY = unitX * halfWidth;
        std::vector<SvgPoint> quad;
        quad.push_back(SvgPoint{from.x - unitX * extendFrom + normalX + shiftX, from.y - unitY * extendFrom + normalY + shiftY});
        quad.push_back(SvgPoint{to.x + unitX * extendTo + normalX + shiftX, to.y + unitY * extendTo + normalY + shiftY});
        quad.push_back(SvgPoint{to.x + unitX * extendTo - normalX + shiftX, to.y + unitY * extendTo - normalY + shiftY});
        quad.push_back(SvgPoint{from.x - unitX * extendFrom - normalX + shiftX, from.y - unitY * extendFrom - normalY + shiftY});
        SvgAppendContour(contours, std::move(quad));
    }

    void SvgAppendJoin(std::vector<std::vector<SvgPoint>>& contours, const SvgPoint& previous, const SvgPoint& vertex, const SvgPoint& next, double halfWidth,
        const std::string& join, double miterLimit, double shiftX, double shiftY) {
        const double ax = vertex.x - previous.x;
        const double ay = vertex.y - previous.y;
        const double bx = next.x - vertex.x;
        const double by = next.y - vertex.y;
        const double aLength = std::sqrt(ax * ax + ay * ay);
        const double bLength = std::sqrt(bx * bx + by * by);
        if (!(aLength > 0) || !(bLength > 0)) {
            return;
        }
        const double ux = ax / aLength;
        const double uy = ay / aLength;
        const double vx = bx / bLength;
        const double vy = by / bLength;
        const double cross = ux * vy - uy * vx;
        if (std::fabs(cross) < SVG_STROKE_FLAT_EPSILON) {
            // A doubling-back vertex only shows the round join's disc.
            if (join == "round") {
                SvgAppendRoundBlob(contours, vertex.x, vertex.y, halfWidth, shiftX, shiftY);
            }
            return;
        }
        const double side = cross > 0 ? 1.0 : -1.0;
        const double startX = vertex.x + side * uy * halfWidth + shiftX;
        const double startY = vertex.y - side * ux * halfWidth + shiftY;
        const double endX = vertex.x + side * vy * halfWidth + shiftX;
        const double endY = vertex.y - side * vx * halfWidth + shiftY;
        if (join == "round") {
            SvgAppendRoundBlob(contours, vertex.x, vertex.y, halfWidth, shiftX, shiftY);
            return;
        }
        if (join == "miter" || join == "miter-clip") {
            const double limit = miterLimit > 0 ? miterLimit : 4;
            const double parameter = ((endX - startX) * vy - (endY - startY) * vx) / cross;
            const double tipX = startX + ux * parameter;
            const double tipY = startY + uy * parameter;
            const double tipDistance = std::sqrt((tipX - vertex.x) * (tipX - vertex.x) + (tipY - vertex.y) * (tipY - vertex.y));
            if (tipDistance <= limit * halfWidth) {
                SvgAppendContour(contours, std::vector<SvgPoint>{SvgPoint{startX, startY}, SvgPoint{tipX, tipY}, SvgPoint{vertex.x + shiftX, vertex.y + shiftY}});
                SvgAppendContour(contours, std::vector<SvgPoint>{SvgPoint{tipX, tipY}, SvgPoint{endX, endY}, SvgPoint{vertex.x + shiftX, vertex.y + shiftY}});
                return;
            }
            if (join == "miter-clip") {
                const double bisectorX = startX + endX - 2 * (vertex.x + shiftX);
                const double bisectorY = startY + endY - 2 * (vertex.y + shiftY);
                const double bisectorLength = std::sqrt(bisectorX * bisectorX + bisectorY * bisectorY);
                if (bisectorLength > SVG_STROKE_FLAT_EPSILON) {
                    const double unitBisectorX = bisectorX / bisectorLength;
                    const double unitBisectorY = bisectorY / bisectorLength;
                    const double cutX = vertex.x + shiftX + unitBisectorX * limit * halfWidth;
                    const double cutY = vertex.y + shiftY + unitBisectorY * limit * halfWidth;
                    auto clipTip = [&](double fromX, double fromY) {
                        const double alongX = tipX - fromX;
                        const double alongY = tipY - fromY;
                        const double denominator = alongX * unitBisectorX + alongY * unitBisectorY;
                        if (std::fabs(denominator) < SVG_STROKE_FLAT_EPSILON) {
                            return SvgPoint{fromX, fromY};
                        }
                        const double ratio = ((cutX - fromX) * unitBisectorX + (cutY - fromY) * unitBisectorY) / denominator;
                        return SvgPoint{fromX + alongX * ratio, fromY + alongY * ratio};
                    };
                    const SvgPoint clippedStart = clipTip(startX, startY);
                    const SvgPoint clippedEnd = clipTip(endX, endY);
                    SvgAppendContour(contours,
                        std::vector<SvgPoint>{SvgPoint{startX, startY}, clippedStart, clippedEnd, SvgPoint{endX, endY}});
                    return;
                }
            }
        }
        // Bevel joins, and miter joins past their limit, cut straight across.
        SvgAppendContour(contours, std::vector<SvgPoint>{SvgPoint{startX, startY}, SvgPoint{endX, endY}, SvgPoint{vertex.x + shiftX, vertex.y + shiftY}});
    }

    void SvgStrokeContours(const std::vector<SvgPoint>& run, int thickness, const std::string& join, const std::string& cap, double miterLimit,
        bool closed, std::vector<std::vector<SvgPoint>>& contours, bool snapAxis = false) {
        if (run.empty() || thickness <= 0) {
            return;
        }
        const double halfWidth = std::max(1, thickness) / 2.0;
        // shape-rendering="crispEdges" is the only mode that snaps an odd-width
        // axis-aligned stroke onto one pixel row or column; the default mode
        // antialiases it, exactly like a browser.
        auto axisShift = [&](const SvgPoint& from, const SvgPoint& to, double& shiftX, double& shiftY) {
            shiftX = 0;
            shiftY = 0;
            if (snapAxis) {
                SvgAxisShift(thickness, from, to, shiftX, shiftY);
            }
        };
        if (run.size() == 1) {
            if (cap == "round") {
                SvgAppendRoundBlob(contours, run[0].x, run[0].y, halfWidth, 0, 0);
            } else if (cap == "square") {
                std::vector<SvgPoint> square;
                square.push_back(SvgPoint{run[0].x - halfWidth, run[0].y - halfWidth});
                square.push_back(SvgPoint{run[0].x + halfWidth, run[0].y - halfWidth});
                square.push_back(SvgPoint{run[0].x + halfWidth, run[0].y + halfWidth});
                square.push_back(SvgPoint{run[0].x - halfWidth, run[0].y + halfWidth});
                SvgAppendContour(contours, std::move(square));
            }
            return;
        }
        std::size_t count = run.size();
        if (closed && count > 2) {
            const double deltaX = run.front().x - run[count - 1].x;
            const double deltaY = run.front().y - run[count - 1].y;
            if (std::sqrt(deltaX * deltaX + deltaY * deltaY) < SVG_STROKE_FLAT_EPSILON) {
                --count;
            }
        }
        for (std::size_t index = 0; index + 1 < count; ++index) {
            const bool firstSegment = index == 0 && !closed;
            const bool lastSegment = index + 2 == count && !closed;
            const double extendFrom = firstSegment && cap == "square" ? halfWidth : 0;
            const double extendTo = lastSegment && cap == "square" ? halfWidth : 0;
            double shiftX = 0;
            double shiftY = 0;
            axisShift(run[index], run[index + 1], shiftX, shiftY);
            SvgAppendSegmentQuad(contours, run[index], run[index + 1], halfWidth, extendFrom, extendTo, shiftX, shiftY);
        }
        if (closed) {
            SvgAppendSegmentQuad(contours, run[count - 1], run[0], halfWidth, 0, 0, 0, 0);
        }
        for (std::size_t index = 1; index + 1 < count; ++index) {
            double shiftX = 0;
            double shiftY = 0;
            axisShift(run[index - 1], run[index], shiftX, shiftY);
            SvgAppendJoin(contours, run[index - 1], run[index], run[index + 1], halfWidth, join, miterLimit, shiftX, shiftY);
        }
        if (closed && count > 2) {
            SvgAppendJoin(contours, run[count - 2], run[count - 1], run[0], halfWidth, join, miterLimit, 0, 0);
            SvgAppendJoin(contours, run[count - 1], run[0], run[1], halfWidth, join, miterLimit, 0, 0);
        }
        if (!closed && cap == "round") {
            double shiftX = 0;
            double shiftY = 0;
            axisShift(run[1], run[0], shiftX, shiftY);
            SvgAppendRoundBlob(contours, run.front().x, run.front().y, halfWidth, shiftX, shiftY);
            axisShift(run[count - 2], run[count - 1], shiftX, shiftY);
            SvgAppendRoundBlob(contours, run.back().x, run.back().y, halfWidth, shiftX, shiftY);
        }
    }

    // Splits a subpath into the polyline pieces a dash pattern leaves visible.
    // The walk happens in Euclidean length so diagonal dashes measure like a
    // browser's; each run keeps the vertices it crosses so joins and caps land
    // exactly where the outline turns.
    std::vector<std::vector<SvgPoint>> SvgDashRuns(const std::vector<SvgPoint>& points, bool close, const std::vector<double>& dashes,
        double dashOffset, double pathLengthValue) {
        std::vector<std::vector<SvgPoint>> runs;
        std::size_t count = points.size();
        if (count < 2) {
            return runs;
        }
        while (close && count > 2 && std::fabs(points.front().x - points[count - 1].x) < SVG_STROKE_FLAT_EPSILON &&
               std::fabs(points.front().y - points[count - 1].y) < SVG_STROKE_FLAT_EPSILON) {
            --count;
        }
        struct SvgOutlineSegment {
            SvgPoint from;
            SvgPoint to;
            double start = 0;
            double length = 0;
        };
        std::vector<SvgOutlineSegment> segments;
        double totalLength = 0;
        const std::size_t segmentCount = close ? count : count - 1;
        for (std::size_t index = 0; index < segmentCount; ++index) {
            const SvgPoint& from = points[index];
            const SvgPoint& to = points[(index + 1) % count];
            const double deltaX = to.x - from.x;
            const double deltaY = to.y - from.y;
            const double segmentLength = std::sqrt(deltaX * deltaX + deltaY * deltaY);
            if (!(segmentLength > 0)) {
                continue;
            }
            SvgOutlineSegment segment;
            segment.from = from;
            segment.to = to;
            segment.start = totalLength;
            segment.length = segmentLength;
            segments.push_back(segment);
            totalLength += segmentLength;
        }
        if (segments.empty() || !(totalLength > 0)) {
            return runs;
        }
        double dashScale = 1;
        if (pathLengthValue > 0) {
            dashScale = totalLength / pathLengthValue;
        }
        std::vector<double> pattern = dashes;
        for (double& dash : pattern) {
            dash *= dashScale;
        }
        double offset = dashOffset * dashScale;
        double patternLength = 0;
        for (double dash : pattern) {
            patternLength += dash;
        }
        if (!(patternLength > 0)) {
            return runs;
        }
        offset = std::fmod(offset, patternLength);
        if (offset < 0) {
            offset += patternLength;
        }
        std::vector<double> cumulative(pattern.size() + 1, 0);
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            cumulative[index + 1] = cumulative[index] + pattern[index];
        }
        std::size_t slot = 0;
        while (slot + 1 < pattern.size() && offset >= cumulative[slot + 1]) {
            ++slot;
        }
        // A zero-length dash that begins exactly on the phase boundary still
        // paints its dot (round and square caps), so point the walk at it.
        for (std::size_t candidate = 0; candidate + 1 < pattern.size(); ++candidate) {
            if (candidate % 2 == 0 && !(pattern[candidate] > 0) && cumulative[candidate] == offset) {
                slot = candidate;
                break;
            }
        }
        bool on = slot % 2 == 0;
        double remaining = cumulative[slot + 1] - offset;
        double cursor = 0;
        auto pointAt = [&](double distance) {
            if (!(distance > 0)) {
                return segments.front().from;
            }
            if (distance >= totalLength) {
                return segments.back().to;
            }
            for (const SvgOutlineSegment& segment : segments) {
                if (distance >= segment.start && distance <= segment.start + segment.length) {
                    const double ratio = (distance - segment.start) / segment.length;
                    return SvgPoint{segment.from.x + (segment.to.x - segment.from.x) * ratio,
                                    segment.from.y + (segment.to.y - segment.from.y) * ratio};
                }
            }
            return segments.back().to;
        };
        auto appendRun = [&](double start, double end) {
            if (!(end - start > 0)) {
                return;
            }
            std::vector<SvgPoint> run;
            run.push_back(pointAt(start));
            for (const SvgOutlineSegment& segment : segments) {
                const double vertexDistance = segment.start + segment.length;
                if (vertexDistance > start + SVG_STROKE_FLAT_EPSILON && vertexDistance < end - SVG_STROKE_FLAT_EPSILON) {
                    run.push_back(segment.to);
                }
            }
            run.push_back(pointAt(end));
            runs.push_back(std::move(run));
        };
        while (cursor < totalLength - SVG_STROKE_FLAT_EPSILON) {
            double step = remaining;
            if (step > totalLength - cursor) {
                step = totalLength - cursor;
            }
            if (on) {
                if (step > SVG_STROKE_FLAT_EPSILON) {
                    appendRun(cursor, cursor + step);
                } else {
                    // A zero-length dash still paints a dot with round or square caps.
                    std::vector<SvgPoint> dot;
                    dot.push_back(pointAt(cursor));
                    runs.push_back(std::move(dot));
                }
            }
            cursor += step;
            remaining -= step;
            if (remaining <= SVG_STROKE_FLAT_EPSILON) {
                int guard = 0;
                do {
                    slot = (slot + 1) % pattern.size();
                    remaining = pattern[slot];
                    on = slot % 2 == 0;
                    if (on && !(remaining > 0)) {
                        // Zero-length dashes still paint a dot with round or
                        // square caps, including a dash sitting on the path end.
                        std::vector<SvgPoint> dot;
                        dot.push_back(pointAt(cursor));
                        runs.push_back(std::move(dot));
                    }
                    ++guard;
                } while (!(remaining > 0) && guard <= static_cast<int>(pattern.size()));
            }
        }
        return runs;
    }

    void StrokePolyline(const std::vector<SvgPoint>& points, int originX, int originY, Color color, bool fixed, bool close, int thickness = 1,
                        const std::string& dashArray = std::string(), double dashOffset = 0, double pathLengthValue = 0,
                        const std::string& join = std::string("miter"), const std::string& cap = std::string("butt"), double miterLimit = 4,
                        bool snapAxis = false) {
        if (points.size() < 2 || color.alpha == 0 || thickness <= 0) {
            return;
        }
        std::vector<double> dashes;
        if (!ParseSvgDashArray(dashArray, dashes)) {
            std::vector<std::vector<SvgPoint>> contours;
            SvgStrokeContours(points, thickness, join, cap, miterLimit, close, contours, snapAxis);
            if (!contours.empty()) {
                FillContours(contours, originX, originY, color, fixed, false, true);
            }
            return;
        }
        const std::vector<std::vector<SvgPoint>> runs = SvgDashRuns(points, close, dashes, dashOffset, pathLengthValue);
        for (const std::vector<SvgPoint>& run : runs) {
            std::vector<std::vector<SvgPoint>> contours;
            SvgStrokeContours(run, thickness, join, cap, miterLimit, false, contours, snapAxis);
            if (!contours.empty()) {
                FillContours(contours, originX, originY, color, fixed, false, true);
            }
        }
    }

    double SvgRectRadiusValue(const Node& node, const char* name, bool& present) {
        present = HasAttribute(node, name);
        if (!present) {
            return 0;
        }
        return std::atof(AttributeValue(node, name).c_str());
    }

    // Resolves the rect rx/ry pair using the SVG auto rules and clamps both
    // radii to half of the rectangle's width and height.
    void SvgRectRadii(const Node& node, double width, double height, double& outRx, double& outRy) {
        bool hasRx = false;
        bool hasRy = false;
        double rx = SvgRectRadiusValue(node, "rx", hasRx);
        double ry = SvgRectRadiusValue(node, "ry", hasRy);
        if (rx < 0) {
            hasRx = false;
        }
        if (ry < 0) {
            hasRy = false;
        }
        if (hasRx && !hasRy) {
            ry = rx;
        } else if (hasRy && !hasRx) {
            rx = ry;
        } else if (!hasRx && !hasRy) {
            rx = 0;
            ry = 0;
        }
        outRx = std::max(0.0, std::min(rx, width / 2));
        outRy = std::max(0.0, std::min(ry, height / 2));
    }

    std::string SvgRectPath(double x, double y, double width, double height, double rx, double ry) {
        if (!(width > 0) || !(height > 0)) {
            return std::string();
        }
        if (!(rx > 0) || !(ry > 0)) {
            return "M" + std::to_string(x) + " " + std::to_string(y) + "H" + std::to_string(x + width) + "V" +
                   std::to_string(y + height) + "H" + std::to_string(x) + "Z";
        }
        const std::string arc = "A" + std::to_string(rx) + " " + std::to_string(ry) + " 0 0 1 ";
        std::string path = "M" + std::to_string(x + rx) + " " + std::to_string(y);
        path += "H" + std::to_string(x + width - rx);
        path += arc + std::to_string(x + width) + " " + std::to_string(y + ry);
        path += "V" + std::to_string(y + height - ry);
        path += arc + std::to_string(x + width - rx) + " " + std::to_string(y + height);
        path += "H" + std::to_string(x + rx);
        path += arc + std::to_string(x) + " " + std::to_string(y + height - ry);
        path += "V" + std::to_string(y + ry);
        path += arc + std::to_string(x + rx) + " " + std::to_string(y);
        path += "Z";
        return path;
    }

    bool ReadSvgNumber(const std::string& text, std::size_t& index, double& number) {
        while (index < text.size() && (text[index] == ' ' || text[index] == '\t' || text[index] == '\n' || text[index] == '\r' ||
                                           text[index] == ',')) {
            ++index;
        }
        if (index >= text.size()) {
            return false;
        }
        const std::size_t start = index;
        if (text[index] == '+' || text[index] == '-') {
            ++index;
        }
        bool sawDigit = false;
        bool sawDot = false;
        while (index < text.size()) {
            const char character = text[index];
            if (character >= '0' && character <= '9') {
                sawDigit = true;
                ++index;
                continue;
            }
            if (character == '.' && !sawDot) {
                sawDot = true;
                ++index;
                continue;
            }
            break;
        }
        if (index < text.size() && (text[index] == 'e' || text[index] == 'E')) {
            std::size_t exponent = index + 1;
            if (exponent < text.size() && (text[exponent] == '+' || text[exponent] == '-')) {
                ++exponent;
            }
            if (exponent < text.size() && text[exponent] >= '0' && text[exponent] <= '9') {
                index = exponent + 1;
                while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
                    ++index;
                }
            }
        }
        if (!sawDigit) {
            index = start;
            return false;
        }
        number = std::atof(text.c_str() + start);
        return true;
    }

    std::vector<SvgPoint> ParsePointList(const std::string& text) {
        std::vector<SvgPoint> points;
        std::size_t index = 0;
        while (index < text.size()) {
            double x = 0;
            double y = 0;
            if (!ReadSvgNumber(text, index, x) || !ReadSvgNumber(text, index, y)) {
                break;
            }
            points.push_back(SvgPoint{x, y});
        }
        return points;
    }

    double SvgVectorAngle(double ux, double uy, double vx, double vy) {
        const double length = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
        if (!(length > 0)) {
            return 0;
        }
        double cosine = (ux * vx + uy * vy) / length;
        if (cosine < -1) {
            cosine = -1;
        }
        if (cosine > 1) {
            cosine = 1;
        }
        double angle = std::acos(cosine);
        if (ux * vy - uy * vx < 0) {
            angle = -angle;
        }
        return angle;
    }

    void AddQuadratic(std::vector<SvgPoint>& points, double x0, double y0, double x1, double y1, double x2, double y2,
        double pixelScale = 1.0) {
        // The control polygon bounds how far the curve can bow away from the
        // chord, so one segment per SVG_FLATTEN_UNITS_PER_STEP of its length
        // keeps the flattened outline within a fraction of a pixel.
        const double controlLength = std::hypot(x1 - x0, y1 - y0) + std::hypot(x2 - x1, y2 - y1);
        const int wanted = static_cast<int>(std::ceil(controlLength * std::fabs(pixelScale) / SVG_FLATTEN_UNITS_PER_STEP));
        const int steps = std::max(8, std::min(SVG_FLATTEN_MAX_STEPS, wanted));
        for (int step = 1; step <= steps; ++step) {
            const double t = static_cast<double>(step) / steps;
            const double u = 1.0 - t;
            SvgPoint point;
            point.x = u * u * x0 + 2 * u * t * x1 + t * t * x2;
            point.y = u * u * y0 + 2 * u * t * y1 + t * t * y2;
            points.push_back(point);
        }
    }

    void AddArc(std::vector<SvgPoint>& points, double x1, double y1, double radiusX, double radiusY, double rotationDeg, bool largeArc, bool sweep,
        double x2, double y2, double pixelScale = 1.0) {
        if (std::abs(x1 - x2) < 0.00001 && std::abs(y1 - y2) < 0.00001) {
            return;
        }
        radiusX = std::abs(radiusX);
        radiusY = std::abs(radiusY);
        if (!(radiusX > 0) || !(radiusY > 0)) {
            points.push_back(SvgPoint{x2, y2});
            return;
        }
        const double pi = std::acos(-1.0);
        const double phi = rotationDeg * pi / 180.0;
        const double cosPhi = std::cos(phi);
        const double sinPhi = std::sin(phi);
        const double midX = (x1 - x2) / 2;
        const double midY = (y1 - y2) / 2;
        const double primeX = cosPhi * midX + sinPhi * midY;
        const double primeY = -sinPhi * midX + cosPhi * midY;
        double radiusXSq = radiusX * radiusX;
        double radiusYSq = radiusY * radiusY;
        const double primeXSq = primeX * primeX;
        const double primeYSq = primeY * primeY;
        const double lambda = primeXSq / radiusXSq + primeYSq / radiusYSq;
        if (lambda > 1) {
            const double scale = std::sqrt(lambda);
            radiusX *= scale;
            radiusY *= scale;
            radiusXSq = radiusX * radiusX;
            radiusYSq = radiusY * radiusY;
        }
        const double denominator = radiusXSq * primeYSq + radiusYSq * primeXSq;
        if (!(denominator > 0)) {
            points.push_back(SvgPoint{x2, y2});
            return;
        }
        double numerator = radiusXSq * radiusYSq - radiusXSq * primeYSq - radiusYSq * primeXSq;
        if (numerator < 0) {
            numerator = 0;
        }
        double coefficient = std::sqrt(numerator / denominator);
        if (largeArc == sweep) {
            coefficient = -coefficient;
        }
        const double centerPrimeX = coefficient * radiusX * primeY / radiusY;
        const double centerPrimeY = coefficient * -radiusY * primeX / radiusX;
        const double centerX = cosPhi * centerPrimeX - sinPhi * centerPrimeY + (x1 + x2) / 2;
        const double centerY = sinPhi * centerPrimeX + cosPhi * centerPrimeY + (y1 + y2) / 2;
        const double theta1 = SvgVectorAngle(1, 0, (primeX - centerPrimeX) / radiusX, (primeY - centerPrimeY) / radiusY);
        double delta = SvgVectorAngle((primeX - centerPrimeX) / radiusX, (primeY - centerPrimeY) / radiusY, (-primeX - centerPrimeX) / radiusX,
            (-primeY - centerPrimeY) / radiusY);
        if (!sweep && delta > 0) {
            delta -= 2 * pi;
        } else if (sweep && delta < 0) {
            delta += 2 * pi;
        }
        const double portion = std::abs(delta) / (2 * pi);
        int steps = portion < 0.05 ? 4 : (portion > 1 ? 24 : static_cast<int>(portion * 24 + 0.5));
        const double arcLength = std::abs(delta) * std::max(radiusX, radiusY);
        const int wanted = static_cast<int>(std::ceil(arcLength * std::fabs(pixelScale) / SVG_FLATTEN_UNITS_PER_STEP));
        steps = std::max(steps, std::min(SVG_FLATTEN_MAX_STEPS, wanted));
        for (int step = 1; step <= steps; ++step) {
            const double theta = theta1 + delta * (static_cast<double>(step) / steps);
            const double cosTheta = std::cos(theta);
            const double sinTheta = std::sin(theta);
            SvgPoint point;
            point.x = cosPhi * radiusX * cosTheta - sinPhi * radiusY * sinTheta + centerX;
            point.y = sinPhi * radiusX * cosTheta + cosPhi * radiusY * sinTheta + centerY;
            points.push_back(point);
        }
    }

    void AddCurve(std::vector<SvgPoint>& points, double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
        double pixelScale = 1.0) {
        const double controlLength = std::hypot(x1 - x0, y1 - y0) + std::hypot(x2 - x1, y2 - y1) + std::hypot(x3 - x2, y3 - y2);
        const int wanted = static_cast<int>(std::ceil(controlLength * std::fabs(pixelScale) / SVG_FLATTEN_UNITS_PER_STEP));
        const int steps = std::max(8, std::min(SVG_FLATTEN_MAX_STEPS, wanted));
        for (int step = 1; step <= steps; ++step) {
            const double t = static_cast<double>(step) / steps;
            const double u = 1.0 - t;
            SvgPoint point;
            point.x = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
            point.y = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
            points.push_back(point);
        }
    }

    struct SvgSpace;

    void PaintPath(const std::string& data, int originX, int originY, Color fill, Color stroke, bool fixed, int thickness = 1,
        double scaleX = 1, double scaleY = 1, double minX = 0, double minY = 0, bool evenOdd = false, Node* gradientNode = nullptr,
        const SvgSpace* gradientSpace = nullptr, const std::string& dashArray = std::string(), double dashOffset = 0,
        double pathLengthValue = 0) {
        std::vector<SvgPoint> figure;
        std::vector<std::vector<SvgPoint>> contours;
        double currentX = 0;
        double currentY = 0;
        double startX = 0;
        double startY = 0;
        double lastControlX = 0;
        double lastControlY = 0;
        bool lastWasCubic = false;
        bool lastWasQuadratic = false;
        std::size_t index = 0;
        char command = 0;
        // Curves flatten at the scale they are finally painted at, so an
        // element blown up by a transform or a viewBox gets more segments.
        double flattenScale = std::max(std::fabs(scaleX), std::fabs(scaleY));
        if (gradientSpace != nullptr) {
            const SvgMatrix& flattenMatrix = gradientSpace->matrix;
            flattenScale *= std::sqrt(std::fabs(flattenMatrix.a * flattenMatrix.d - flattenMatrix.b * flattenMatrix.c));
        }
        auto mapPoint = [&](const SvgPoint& point) {
            double userX = point.x;
            double userY = point.y;
            if (gradientSpace != nullptr) {
                const SvgMatrix& matrix = gradientSpace->matrix;
                const double mappedX = matrix.a * userX + matrix.c * userY + matrix.e;
                const double mappedY = matrix.b * userX + matrix.d * userY + matrix.f;
                userX = mappedX;
                userY = mappedY;
            }
            return SvgPoint{(userX - minX) * scaleX, (userY - minY) * scaleY};
        };
        auto finish = [&](bool close) {
            std::vector<SvgPoint> mapped;
            for (const SvgPoint& point : figure) {
                mapped.push_back(mapPoint(point));
            }
            if (mapped.size() >= 3) {
                contours.push_back(mapped);
            }
            if (stroke.alpha != 0) {
                StrokePolyline(mapped, originX, originY, stroke, fixed, close, thickness, dashArray, dashOffset, pathLengthValue,
                    gradientSpace != nullptr ? gradientSpace->lineJoin : std::string("miter"),
                    gradientSpace != nullptr ? gradientSpace->lineCap : std::string("butt"),
                    gradientSpace != nullptr ? gradientSpace->miterLimit : 4,
                    gradientSpace != nullptr && gradientSpace->shapeRendering == 2);
            } else if (gradientNode != nullptr && gradientSpace != nullptr && !gradientSpace->strokeUrl.empty() && mapped.size() >= 2) {
                std::vector<std::vector<SvgPoint>> gradientRuns;
                std::vector<double> gradientDashes;
                if (ParseSvgDashArray(dashArray, gradientDashes)) {
                    gradientRuns = SvgDashRuns(mapped, close, gradientDashes, dashOffset, pathLengthValue);
                } else {
                    gradientRuns.push_back(mapped);
                }
                std::vector<std::vector<SvgPoint>> strokeContours;
                for (std::size_t runIndex = 0; runIndex < gradientRuns.size(); ++runIndex) {
                    SvgStrokeContours(gradientRuns[runIndex], thickness, gradientSpace->lineJoin, gradientSpace->lineCap, gradientSpace->miterLimit,
                        close && runIndex == 0 && gradientRuns.size() == 1, strokeContours, gradientSpace->shapeRendering == 2);
                }
                if (!strokeContours.empty()) {
                    int left = 1000000;
                    int top = 1000000;
                    int right = -1000000;
                    int bottom = -1000000;
                    for (const std::vector<SvgPoint>& contour : strokeContours) {
                        for (const SvgPoint& point : contour) {
                            left = std::min(left, static_cast<int>(std::floor(point.x)));
                            top = std::min(top, static_cast<int>(std::floor(point.y)));
                            right = std::max(right, static_cast<int>(std::ceil(point.x)));
                            bottom = std::max(bottom, static_cast<int>(std::ceil(point.y)));
                        }
                    }
                    FillContoursGradient(strokeContours, originX, originY, *gradientNode, *gradientSpace, originX + left, originY + top,
                        std::max(1, right - left), std::max(1, bottom - top), fixed, false);
                }
            }
            figure.clear();
        };
        while (index < data.size()) {
            while (index < data.size() && (data[index] == ' ' || data[index] == '\t' || data[index] == '\n' || data[index] == '\r' ||
                                              data[index] == ',')) {
                ++index;
            }
            if (index >= data.size()) {
                break;
            }
            if ((data[index] >= 'A' && data[index] <= 'Z') || (data[index] >= 'a' && data[index] <= 'z')) {
                command = data[index];
                ++index;
            }
            if (command == 0) {
                break;
            }
            const bool relative = command >= 'a' && command <= 'z';
            const char operation = relative ? static_cast<char>(command - 'a' + 'A') : command;
            if (operation == 'Z') {
                if (!figure.empty()) {
                    figure.push_back(SvgPoint{startX, startY});
                    finish(true);
                }
                currentX = startX;
                currentY = startY;
                lastWasCubic = false;
                lastWasQuadratic = false;
                command = 0;
                continue;
            }
            double first = 0;
            double second = 0;
            if (operation == 'H' || operation == 'V') {
                if (!ReadSvgNumber(data, index, first)) {
                    break;
                }
                if (operation == 'H') {
                    currentX = relative ? currentX + first : first;
                } else {
                    currentY = relative ? currentY + first : first;
                }
                figure.push_back(SvgPoint{currentX, currentY});
                lastWasCubic = false;
                lastWasQuadratic = false;
                continue;
            }
            if (operation == 'A') {
                double radiusX = 0;
                double radiusY = 0;
                double rotation = 0;
                double largeArc = 0;
                double sweep = 0;
                double endX = 0;
                double endY = 0;
                if (!ReadSvgNumber(data, index, radiusX) || !ReadSvgNumber(data, index, radiusY) || !ReadSvgNumber(data, index, rotation) ||
                    !ReadSvgNumber(data, index, largeArc) || !ReadSvgNumber(data, index, sweep) || !ReadSvgNumber(data, index, endX) ||
                    !ReadSvgNumber(data, index, endY)) {
                    break;
                }
                if (relative) {
                    endX += currentX;
                    endY += currentY;
                }
                AddArc(figure, currentX, currentY, radiusX, radiusY, rotation, largeArc >= 0.5, sweep >= 0.5, endX, endY, flattenScale);
                currentX = endX;
                currentY = endY;
                lastWasCubic = false;
                lastWasQuadratic = false;
                continue;
            }
            if (!ReadSvgNumber(data, index, first) || !ReadSvgNumber(data, index, second)) {
                break;
            }
            if (operation == 'T') {
                const double controlX = lastWasQuadratic ? currentX * 2 - lastControlX : currentX;
                const double controlY = lastWasQuadratic ? currentY * 2 - lastControlY : currentY;
                const double x = relative ? currentX + first : first;
                const double y = relative ? currentY + second : second;
                AddQuadratic(figure, currentX, currentY, controlX, controlY, x, y, flattenScale);
                lastControlX = controlX;
                lastControlY = controlY;
                lastWasQuadratic = true;
                lastWasCubic = false;
                currentX = x;
                currentY = y;
                continue;
            }
            if (operation == 'M' || operation == 'L') {
                const double x = relative ? currentX + first : first;
                const double y = relative ? currentY + second : second;
                if (operation == 'M') {
                    if (figure.size() >= 2) {
                        finish(false);
                    } else {
                        figure.clear();
                    }
                    startX = x;
                    startY = y;
                    command = relative ? 'l' : 'L';
                }
                currentX = x;
                currentY = y;
                figure.push_back(SvgPoint{currentX, currentY});
                lastWasCubic = false;
                lastWasQuadratic = false;
                continue;
            }
            double third = 0;
            double fourth = 0;
            if (!ReadSvgNumber(data, index, third) || !ReadSvgNumber(data, index, fourth)) {
                break;
            }
            if (operation == 'Q') {
                const double controlX = relative ? currentX + first : first;
                const double controlY = relative ? currentY + second : second;
                const double x = relative ? currentX + third : third;
                const double y = relative ? currentY + fourth : fourth;
                AddQuadratic(figure, currentX, currentY, controlX, controlY, x, y, flattenScale);
                lastControlX = controlX;
                lastControlY = controlY;
                lastWasQuadratic = true;
                lastWasCubic = false;
                currentX = x;
                currentY = y;
                continue;
            }
            if (operation == 'S') {
                const double controlX = relative ? currentX + first : first;
                const double controlY = relative ? currentY + second : second;
                const double x = relative ? currentX + third : third;
                const double y = relative ? currentY + fourth : fourth;
                const double reflectedX = lastWasCubic ? currentX * 2 - lastControlX : currentX;
                const double reflectedY = lastWasCubic ? currentY * 2 - lastControlY : currentY;
                AddCurve(figure, currentX, currentY, reflectedX, reflectedY, controlX, controlY, x, y, flattenScale);
                lastControlX = controlX;
                lastControlY = controlY;
                lastWasCubic = true;
                lastWasQuadratic = false;
                currentX = x;
                currentY = y;
                continue;
            }
            double fifth = 0;
            double sixth = 0;
            if (!ReadSvgNumber(data, index, fifth) || !ReadSvgNumber(data, index, sixth)) {
                break;
            }
            const double x1 = relative ? currentX + first : first;
            const double y1 = relative ? currentY + second : second;
            const double x2 = relative ? currentX + third : third;
            const double y2 = relative ? currentY + fourth : fourth;
            const double x = relative ? currentX + fifth : fifth;
            const double y = relative ? currentY + sixth : sixth;
            AddCurve(figure, currentX, currentY, x1, y1, x2, y2, x, y, flattenScale);
            lastControlX = x2;
            lastControlY = y2;
            lastWasCubic = true;
            lastWasQuadratic = false;
            currentX = x;
            currentY = y;
        }
        if (figure.size() >= 2) {
            finish(false);
        }
        if (contours.empty()) {
            return;
        }
        int boxLeft = 1000000;
        int boxTop = 1000000;
        int boxRight = -1000000;
        int boxBottom = -1000000;
        for (const std::vector<SvgPoint>& contour : contours) {
            for (const SvgPoint& point : contour) {
                boxLeft = std::min(boxLeft, static_cast<int>(std::floor(point.x)));
                boxTop = std::min(boxTop, static_cast<int>(std::floor(point.y)));
                boxRight = std::max(boxRight, static_cast<int>(std::ceil(point.x)));
                boxBottom = std::max(boxBottom, static_cast<int>(std::ceil(point.y)));
            }
        }
        if (fill.alpha != 0) {
            FillContours(contours, originX, originY, fill, fixed, evenOdd);
        } else if (gradientNode != nullptr && gradientSpace != nullptr && !gradientSpace->fillUrl.empty() && boxRight > boxLeft && boxBottom > boxTop) {
            FillContoursGradient(contours, originX, originY, *gradientNode, *gradientSpace, originX + boxLeft, originY + boxTop, boxRight - boxLeft, boxBottom - boxTop, fixed, evenOdd);
        }
    }

    struct SvgClipShape {
        std::vector<std::vector<SvgPoint>> contours;
        bool evenOdd = false;
    };

    // One painted piece of a <mask>'s content: the contours it covers, its
    // paint, and how it composits over the pieces before it.
    struct SvgMaskPiece {
        std::vector<std::vector<SvgPoint>> contours;
        bool evenOdd = false;
        Color color{0, 0, 0, 255};
        double opacity = 1;
    };

    // A <mask> reduced to what the painter needs per pixel: its content pieces,
    // the mask region, and whether luminance (default) or alpha values it.
    struct SvgMaskContent {
        std::vector<SvgMaskPiece> pieces;
        bool luminance = true;
        bool hasRegion = false;
        double regionX = 0;
        double regionY = 0;
        double regionWidth = 0;
        double regionHeight = 0;
    };

    bool SvgPointInContour(const std::vector<SvgPoint>& points, double x, double y, bool evenOdd) {
        if (points.size() < 3) {
            return false;
        }
        if (evenOdd) {
            bool inside = false;
            for (std::size_t index = 0, previous = points.size() - 1; index < points.size(); previous = index++) {
                const SvgPoint& a = points[previous];
                const SvgPoint& b = points[index];
                if (((a.y > y) != (b.y > y)) && (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)) {
                    inside = !inside;
                }
            }
            return inside;
        }
        double winding = 0;
        for (std::size_t index = 0, previous = points.size() - 1; index < points.size(); previous = index++) {
            const SvgPoint& a = points[previous];
            const SvgPoint& b = points[index];
            const double side = (b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y);
            if (a.y <= y) {
                if (b.y > y && side > 0) {
                    winding += 1;
                }
            } else if (b.y <= y && side < 0) {
                winding -= 1;
            }
        }
        return winding != 0;
    }

    bool SvgClipAllows(const SvgSpace& space, double x, double y) {
        for (const std::vector<SvgClipShape>& scope : space.clipScopes) {
            bool allowed = false;
            for (const SvgClipShape& shape : scope) {
                for (const std::vector<SvgPoint>& contour : shape.contours) {
                    if (SvgPointInContour(contour, x, y, shape.evenOdd)) {
                        allowed = true;
                        break;
                    }
                }
                if (allowed) {
                    break;
                }
            }
            if (!allowed) {
                return false;
            }
        }
        return true;
    }

    // The bounding box of everything the clip-path scopes allow. Text and other
    // draw operations that cannot be masked per pixel use it as a conservative
    // rectangle: it never removes a pixel a browser would keep.
    bool SvgClipBounds(const SvgSpace& space, double& left, double& top, double& right, double& bottom) {
        bool any = false;
        for (const std::vector<SvgClipShape>& scope : space.clipScopes) {
            bool seen = false;
            double scopeLeft = 0;
            double scopeTop = 0;
            double scopeRight = 0;
            double scopeBottom = 0;
            for (const SvgClipShape& shape : scope) {
                for (const std::vector<SvgPoint>& contour : shape.contours) {
                    for (const SvgPoint& point : contour) {
                        if (!seen) {
                            scopeLeft = point.x;
                            scopeRight = point.x;
                            scopeTop = point.y;
                            scopeBottom = point.y;
                            seen = true;
                            continue;
                        }
                        scopeLeft = std::min(scopeLeft, point.x);
                        scopeRight = std::max(scopeRight, point.x);
                        scopeTop = std::min(scopeTop, point.y);
                        scopeBottom = std::max(scopeBottom, point.y);
                    }
                }
            }
            if (!seen) {
                continue;
            }
            if (!any) {
                left = scopeLeft;
                top = scopeTop;
                right = scopeRight;
                bottom = scopeBottom;
                any = true;
                continue;
            }
            left = std::max(left, scopeLeft);
            top = std::max(top, scopeTop);
            right = std::min(right, scopeRight);
            bottom = std::min(bottom, scopeBottom);
        }
        return any;
    }

    // A 2D affine matrix laid out like the SVG transform matrix:
    //   [ a c e ]
    //   [ b d f ]
    struct SvgMatrix {
        double a = 1;
        double b = 0;
        double c = 0;
        double d = 1;
        double e = 0;
        double f = 0;
    };

    SvgMatrix SvgMatrixMultiply(const SvgMatrix& left, const SvgMatrix& right) {
        SvgMatrix result;
        result.a = left.a * right.a + left.c * right.b;
        result.b = left.b * right.a + left.d * right.b;
        result.c = left.a * right.c + left.c * right.d;
        result.d = left.b * right.c + left.d * right.d;
        result.e = left.a * right.e + left.c * right.f + left.e;
        result.f = left.b * right.e + left.d * right.f + left.f;
        return result;
    }

    SvgMatrix SvgMatrixTranslate(double x, double y) {
        SvgMatrix matrix;
        matrix.e = x;
        matrix.f = y;
        return matrix;
    }

    SvgMatrix SvgMatrixScale(double x, double y) {
        SvgMatrix matrix;
        matrix.a = x;
        matrix.d = y;
        return matrix;
    }

    SvgMatrix SvgMatrixRotate(double degrees, double centerX, double centerY) {
        const double radians = degrees * std::acos(-1.0) / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        SvgMatrix matrix = SvgMatrixTranslate(centerX, centerY);
        SvgMatrix rotation;
        rotation.a = cosine;
        rotation.b = sine;
        rotation.c = -sine;
        rotation.d = cosine;
        matrix = SvgMatrixMultiply(matrix, rotation);
        return SvgMatrixMultiply(matrix, SvgMatrixTranslate(-centerX, -centerY));
    }

    SvgMatrix SvgMatrixSkewX(double degrees) {
        SvgMatrix matrix;
        matrix.c = std::tan(degrees * std::acos(-1.0) / 180.0);
        return matrix;
    }

    SvgMatrix SvgMatrixSkewY(double degrees) {
        SvgMatrix matrix;
        matrix.b = std::tan(degrees * std::acos(-1.0) / 180.0);
        return matrix;
    }

    bool SvgMatrixIsIdentity(const SvgMatrix& matrix) {
        return matrix.a == 1 && matrix.b == 0 && matrix.c == 0 && matrix.d == 1 && matrix.e == 0 && matrix.f == 0;
    }

    bool SvgMatrixIsAxisAligned(const SvgMatrix& matrix) {
        return matrix.b == 0 && matrix.c == 0;
    }

    double SvgMatrixScaleFactor(const SvgMatrix& matrix) {
        const double determinant = matrix.a * matrix.d - matrix.b * matrix.c;
        if (!(determinant > 0)) {
            return 0;
        }
        return std::sqrt(determinant);
    }

    // Parses the SVG transform attribute grammar. Every function in the list is
    // composed in order, so "translate(10) scale(2)" scales before translating,
    // exactly like the browser's presentation of the transform attribute.
    bool ParseSvgTransformList(const std::string& text, SvgMatrix& matrix) {
        std::size_t index = 0;
        bool any = false;
        while (index < text.size()) {
            while (index < text.size() &&
                   (text[index] == ' ' || text[index] == ',' || text[index] == '\t' || text[index] == '\n' || text[index] == '\r')) {
                ++index;
            }
            if (index >= text.size()) {
                break;
            }
            const std::size_t nameStart = index;
            while (index < text.size() &&
                   ((text[index] >= 'a' && text[index] <= 'z') || (text[index] >= 'A' && text[index] <= 'Z'))) {
                ++index;
            }
            if (index == nameStart) {
                return false;
            }
            const std::string name = ToLowerAscii(text.substr(nameStart, index - nameStart));
            while (index < text.size() && (text[index] == ' ' || text[index] == '\t')) {
                ++index;
            }
            if (index >= text.size() || text[index] != '(') {
                return false;
            }
            ++index;
            double values[6] = {0, 0, 0, 0, 0, 0};
            int count = 0;
            while (index < text.size() && text[index] != ')') {
                if (count >= 6) {
                    return false;
                }
                const std::size_t before = index;
                if (!ReadSvgNumber(text, index, values[count])) {
                    if (text[index] == ',' || text[index] == ' ' || text[index] == '\t' || text[index] == '\n' || text[index] == '\r') {
                        ++index;
                        continue;
                    }
                    return false;
                }
                if (index == before) {
                    return false;
                }
                ++count;
            }
            if (index >= text.size() || text[index] != ')') {
                return false;
            }
            ++index;
            SvgMatrix local;
            if (name == "matrix") {
                if (count != 6) {
                    return false;
                }
                local.a = values[0];
                local.b = values[1];
                local.c = values[2];
                local.d = values[3];
                local.e = values[4];
                local.f = values[5];
            } else if (name == "translate") {
                if (count < 1) {
                    return false;
                }
                local = SvgMatrixTranslate(values[0], count >= 2 ? values[1] : 0);
            } else if (name == "scale") {
                if (count < 1) {
                    return false;
                }
                local = SvgMatrixScale(values[0], count >= 2 ? values[1] : values[0]);
            } else if (name == "rotate") {
                if (count < 1) {
                    return false;
                }
                local = SvgMatrixRotate(values[0], count >= 3 ? values[1] : 0, count >= 3 ? values[2] : 0);
            } else if (name == "skewx") {
                if (count < 1) {
                    return false;
                }
                local = SvgMatrixSkewX(values[0]);
            } else if (name == "skewy") {
                if (count < 1) {
                    return false;
                }
                local = SvgMatrixSkewY(values[0]);
            } else {
                return false;
            }
            matrix = SvgMatrixMultiply(matrix, local);
            any = true;
        }
        return any;
    }

    struct SvgSpace {
        double originX = 0;
        double originY = 0;
        double scaleX = 1;
        double scaleY = 1;
        double minX = 0;
        double minY = 0;
        double pixelWidth = 0;
        double pixelHeight = 0;
        // True once a viewport (the root <svg>) owns this space: a nested <svg>
        // then sets up its own viewport from x/y/width/height.
        bool hasViewport = false;
        double strokeWidth = 1;
        std::string lineJoin = "miter";
        std::string lineCap = "butt";
        double miterLimit = 4;
        double opacity = 1;
        Color fill{0, 0, 0, 255};
        Color stroke{0, 0, 0, 0};
        std::string fillUrl;
        std::string strokeUrl;
        bool hidden = false;
        bool clipped = false;
        int clipX = 0;
        int clipY = 0;
        int clipWidth = 0;
        int clipHeight = 0;
        bool fillEvenOdd = false;
        SvgMatrix matrix;
        // Each clip-path in scope contributes one entry; a point must survive
        // every scope, while several shapes inside a single clipPath are unioned
        // exactly the way a browser unions the clipPath's children.
        std::vector<std::vector<SvgClipShape>> clipScopes;
        std::string textAnchor = "start";
        double fillOpacity = 1;
        double strokeOpacity = 1;
        Color currentColor{0, 0, 0, 255};
        int shapeRendering = 0;
        int textRendering = 0;
        std::string filterUrl;
    };

    // The factor that turns one user unit into one device pixel in a space: the
    // viewBox/length scale multiplied by the average of the affine matrix. It
    // drives adaptive curve flattening so transformed shapes keep smooth edges.
    double SvgDeviceScale(const SvgSpace& space) {
        const SvgMatrix& matrix = space.matrix;
        const double matrixScale = std::sqrt(std::fabs(matrix.a * matrix.d - matrix.b * matrix.c));
        return matrixScale * std::max(std::fabs(space.scaleX), std::fabs(space.scaleY));
    }

    // The clip-path currently guarding every pixel the painter emits. It lives
    // here so markers, patterns, referenced content and gradients painted while
    // a clipped element renders are masked exactly like the element itself.
    const SvgSpace* activeClip = nullptr;

    // The mask guarding every pixel the painter emits, exactly like activeClip:
    // shapes, strokes, gradients and referenced content all pass through it.
    const SvgMaskContent* activeMask = nullptr;

    struct SvgMaskScope {
        Engine& owner;
        const SvgMaskContent* previous;
        SvgMaskScope(Engine& engine, const SvgMaskContent* mask) : owner(engine), previous(engine.activeMask) {
            owner.activeMask = mask;
        }
        SvgMaskScope(const SvgMaskScope&) = delete;
        SvgMaskScope& operator=(const SvgMaskScope&) = delete;
        ~SvgMaskScope() {
            owner.activeMask = previous;
        }
    };

    struct SvgClipScope {
        Engine& owner;
        const SvgSpace* previous;
        SvgClipScope(Engine& engine, const SvgSpace* clip) : owner(engine), previous(engine.activeClip) {
            owner.activeClip = clip;
        }
        SvgClipScope(const SvgClipScope&) = delete;
        SvgClipScope& operator=(const SvgClipScope&) = delete;
        ~SvgClipScope() {
            owner.activeClip = previous;
        }
    };

    __attribute__((noinline, noclone, cdecl)) Color WithOpacity(Color color, double opacity) {
        if (!(opacity > 0)) {
            color.alpha = 0;
            return color;
        }
        if (opacity >= 1) {
            return color;
        }
        int thousandths = 0;
        if (opacity > 0 && opacity < 1) {
            thousandths = static_cast<int>(opacity * 1000.0);
        }
        if (thousandths < 0) {
            thousandths = 0;
        }
        if (thousandths > 1000) {
            thousandths = 1000;
        }
        const int alpha = static_cast<int>(color.alpha) * thousandths / 1000;
        color.alpha = static_cast<std::uint8_t>(alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha));
        return color;
    }

    bool SvgSelectorMatches(const Node& node, const std::string& rawSelector) {
        const std::string selector = ToLowerAscii(TrimSpaces(rawSelector));
        if (selector.empty()) {
            return false;
        }
        std::string head = selector;
        std::string wantedLanguage;
        const std::size_t langAt = selector.find(":lang(");
        if (langAt != std::string::npos) {
            head = selector.substr(0, langAt);
            const std::size_t close = selector.find(')', langAt);
            wantedLanguage = selector.substr(langAt + 6, close == std::string::npos ? std::string::npos : close - (langAt + 6));
        }
        if (!wantedLanguage.empty()) {
            bool languageOk = false;
            for (const Node* cursor = &node; cursor != nullptr; cursor = cursor->parent) {
                std::string spoken = ToLowerAscii(AttributeValue(*cursor, "xml:lang"));
                if (spoken.empty()) {
                    spoken = ToLowerAscii(AttributeValue(*cursor, "lang"));
                }
                if (spoken == wantedLanguage || (spoken.size() > wantedLanguage.size() &&
                        spoken.compare(0, wantedLanguage.size(), wantedLanguage) == 0 && spoken[wantedLanguage.size()] == '-')) {
                    languageOk = true;
                    break;
                }
            }
            if (!languageOk) {
                return false;
            }
        }
        const std::string classes = " " + ToLowerAscii(AttributeValue(node, "class")) + " ";
        const std::string id = ToLowerAscii(AttributeValue(node, "id"));
        const std::string tag = ToLowerAscii(node.tagName);
        if (!head.empty() && head[0] == '.') {
            return classes.find(" " + head.substr(1) + " ") != std::string::npos;
        }
        if (!head.empty() && head[0] == '#') {
            return head.substr(1) == id;
        }
        if (head.empty()) {
            return true;
        }
        const std::size_t dot = head.find('.');
        if (dot != std::string::npos && head.substr(0, dot) == tag) {
            return classes.find(" " + head.substr(dot + 1) + " ") != std::string::npos;
        }
        return dot == std::string::npos && head == tag;
    }

    // Resolves a CSS property on an SVG element through the full cascade:
    // presentation attributes are weakest, then stylesheet rules, then inline
    // style declarations; !important raises a declaration above normal ones.
    std::string SvgPropertyCore(const Node& node, const char* name, bool includeAttribute) {
        const std::string property = name;
        std::string best;
        int bestRank = -1;
        auto consider = [&](const std::string& rawValue, int rank) {
            if (rawValue.empty()) {
                return;
            }
            std::string value = TrimSpaces(rawValue);
            if (value.empty()) {
                return;
            }
            const std::string lower = ToLowerAscii(value);
            const std::size_t bang = lower.rfind("!important");
            if (bang != std::string::npos && bang + 10 == lower.size()) {
                value = TrimSpaces(value.substr(0, bang));
                rank += 2;
            }
            if (!value.empty() && rank >= bestRank) {
                bestRank = rank;
                best = value;
            }
        };
        if (includeAttribute) {
            consider(AttributeValue(node, property.c_str()), 0);
        }
        if (document != nullptr) {
            for (const ElementStyle& styleRule : document->styleRules) {
                if (!SvgSelectorMatches(node, styleRule.selector)) {
                    continue;
                }
                for (const StyleDeclaration& declaration : styleRule.declarations) {
                    if (declaration.name == property) {
                        consider(declaration.value, 1);
                    }
                }
            }
        }
        const std::string inlineStyle = AttributeValue(node, "style");
        if (!inlineStyle.empty()) {
            std::size_t start = 0;
            for (std::size_t index = 0; index <= inlineStyle.size(); ++index) {
                if (index != inlineStyle.size() && inlineStyle[index] != ';') {
                    continue;
                }
                const std::string piece = inlineStyle.substr(start, index - start);
                start = index + 1;
                const std::size_t colon = piece.find(':');
                if (colon == std::string::npos) {
                    continue;
                }
                if (ToLowerAscii(TrimSpaces(piece.substr(0, colon))) == property) {
                    consider(piece.substr(colon + 1), 2);
                }
            }
        }
        return best;
    }

    std::string SvgProperty(const Node& node, const char* name) {
        return SvgPropertyCore(node, name, true);
    }

    std::string SvgCssProperty(const Node& node, const char* name) {
        return SvgPropertyCore(node, name, false);
    }

    // The union of the geometry of an element and its shape descendants, in the
    // element's own user space: the browser's getBBox() box that
    // objectBoundingBox units and clipPathUnits resolve against.
    bool SvgGeometryBounds(const Node& node, double& left, double& top, double& right, double& bottom) {
        bool any = false;
        auto grow = [&](double x, double y) {
            if (!any) {
                left = x;
                right = x;
                top = y;
                bottom = y;
                any = true;
                return;
            }
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        };
        auto growPath = [&](const std::vector<SvgPoint>& points) {
            for (const SvgPoint& point : points) {
                grow(point.x, point.y);
            }
        };
        const std::string tag = ToLowerAscii(node.tagName);
        if (tag == "rect") {
            const double width = SvgReal(node, "width", 0);
            const double height = SvgReal(node, "height", 0);
            double radiusX = 0;
            double radiusY = 0;
            SvgRectRadii(node, width, height, radiusX, radiusY);
            growPath(StraightPath(SvgRectPath(SvgReal(node, "x", 0), SvgReal(node, "y", 0), width, height, radiusX, radiusY)));
        } else if (tag == "circle" || tag == "ellipse") {
            const double centerX = SvgReal(node, "cx", 0);
            const double centerY = SvgReal(node, "cy", 0);
            const double radiusX = tag == "circle" ? SvgReal(node, "r", 0) : SvgReal(node, "rx", 0);
            const double radiusY = tag == "circle" ? radiusX : SvgReal(node, "ry", 0);
            grow(centerX - radiusX, centerY - radiusY);
            grow(centerX + radiusX, centerY + radiusY);
        } else if (tag == "line") {
            grow(SvgReal(node, "x1", 0), SvgReal(node, "y1", 0));
            grow(SvgReal(node, "x2", 0), SvgReal(node, "y2", 0));
        } else if (tag == "polyline" || tag == "polygon") {
            growPath(ParsePointList(AttributeValue(node, "points")));
        } else if (tag == "path") {
            growPath(StraightPath(SvgPathData(node)));
        } else if (tag == "image" || tag == "foreignobject" || tag == "video") {
            grow(SvgReal(node, "x", 0), SvgReal(node, "y", 0));
            grow(SvgReal(node, "x", 0) + SvgReal(node, "width", 0), SvgReal(node, "y", 0) + SvgReal(node, "height", 0));
        }
        for (const std::unique_ptr<Node>& child : node.children) {
            if (child->type != Node::Type::Element) {
                continue;
            }
            const std::string childTag = ToLowerAscii(child->tagName);
            if (childTag == "defs" || childTag == "clippath" || childTag == "mask" || childTag == "lineargradient" ||
                childTag == "radialgradient" || childTag == "pattern" || childTag == "marker" || childTag == "filter" ||
                childTag == "symbol" || childTag == "title" || childTag == "desc" || childTag == "metadata" || childTag == "style" ||
                childTag == "script") {
                continue;
            }
            double childLeft = 0;
            double childTop = 0;
            double childRight = 0;
            double childBottom = 0;
            if (SvgGeometryBounds(*child, childLeft, childTop, childRight, childBottom)) {
                grow(childLeft, childTop);
                grow(childRight, childBottom);
            }
        }
        return any;
    }

    // Builds the pixel-space clip region of a clipPath reference. Every basic
    // shape child contributes a contour (or several, for paths with subpaths),
    // clip-rule selects the fill rule, and clipPathUnits plus the transforms on
    // the clipPath and its children are all honored.
    void BuildSvgClipShapes(const Node& node, const Node& clipPathNode, SvgSpace& space) {
        const bool objectBox = ToLowerAscii(AttributeValue(clipPathNode, "clippathunits")) == "objectboundingbox";
        double elementX = SvgReal(node, "x", 0);
        double elementY = SvgReal(node, "y", 0);
        double elementWidth = SvgReal(node, "width", 0);
        double elementHeight = SvgReal(node, "height", 0);
        if (objectBox) {
            // objectBoundingBox resolves against the element's geometry box, so
            // groups and shapes without width/height still clip correctly.
            double boundsLeft = 0;
            double boundsTop = 0;
            double boundsRight = 0;
            double boundsBottom = 0;
            if (SvgGeometryBounds(node, boundsLeft, boundsTop, boundsRight, boundsBottom)) {
                elementX = boundsLeft;
                elementY = boundsTop;
                elementWidth = boundsRight - boundsLeft;
                elementHeight = boundsBottom - boundsTop;
            }
        }
        SvgSpace clipSpace = space;
        ApplySvgTransform(AttributeValue(clipPathNode, "transform"), clipSpace);
        std::vector<SvgClipShape> shapes;
        auto toUser = [&](double& x, double& y) {
            if (objectBox) {
                x = elementX + x * elementWidth;
                y = elementY + y * elementHeight;
            }
        };
        for (const std::unique_ptr<Node>& child : clipPathNode.children) {
            if (child->type != Node::Type::Element) {
                continue;
            }
            const std::string childTag = ToLowerAscii(child->tagName);
            SvgSpace childSpace = clipSpace;
            ApplySvgTransform(AttributeValue(*child, "transform"), childSpace);
            std::vector<SvgPoint> contour;
            if (childTag == "rect") {
                const double rectX = SvgReal(*child, "x", 0);
                const double rectY = SvgReal(*child, "y", 0);
                const double rectWidth = SvgReal(*child, "width", 0);
                const double rectHeight = SvgReal(*child, "height", 0);
                double radiusX = 0;
                double radiusY = 0;
                SvgRectRadii(*child, rectWidth, rectHeight, radiusX, radiusY);
                contour = StraightPath(SvgRectPath(rectX, rectY, rectWidth, rectHeight, radiusX, radiusY), SvgDeviceScale(childSpace));
                for (SvgPoint& point : contour) {
                    toUser(point.x, point.y);
                }
            } else if (childTag == "circle" || childTag == "ellipse") {
                const double centerX = SvgReal(*child, "cx", 0);
                const double centerY = SvgReal(*child, "cy", 0);
                const double radiusX = childTag == "circle" ? SvgReal(*child, "r", 0) : SvgReal(*child, "rx", 0);
                const double radiusY = childTag == "circle" ? radiusX : SvgReal(*child, "ry", 0);
                // A curved clip is sampled finely enough that the polygon never
                // deviates from the true outline by more than a pixel or so.
                const double planetRadius = std::max(std::abs(radiusX), std::abs(radiusY));
                const int steps = std::max(SVG_CLIP_CIRCLE_STEPS,
                    std::min(SVG_CLIP_CIRCLE_MAX_STEPS, static_cast<int>(std::ceil(planetRadius * 4.0))));
                for (int step = 0; step < steps; ++step) {
                    const double angle = 2.0 * std::acos(-1.0) * static_cast<double>(step) / steps;
                    double pointX = centerX + radiusX * std::cos(angle);
                    double pointY = centerY + radiusY * std::sin(angle);
                    toUser(pointX, pointY);
                    contour.push_back(SvgPoint{pointX, pointY});
                }
            } else if (childTag == "polygon" || childTag == "polyline") {
                contour = ParsePointList(AttributeValue(*child, "points"));
                for (SvgPoint& point : contour) {
                    toUser(point.x, point.y);
                }
            } else if (childTag == "path") {
                contour = StraightPath(SvgPathData(*child), SvgDeviceScale(childSpace));
                for (SvgPoint& point : contour) {
                    toUser(point.x, point.y);
                }
            } else {
                continue;
            }
            if (contour.size() < 3) {
                continue;
            }
            SvgClipShape shape;
            shape.evenOdd = ToLowerAscii(SvgProperty(*child, "clip-rule")) == "evenodd";
            shape.contours.push_back(MapSvgPoints(contour, childSpace));
            shapes.push_back(shape);
        }
        // An empty clipPath clips everything away, exactly like a browser.
        space.clipScopes.push_back(shapes);
    }

    // font-size accepts plain user units plus the px, em, and % forms used by
    // CSS; the em form resolves against the inherited size.
    double SvgFontSize(const Node& node, double fallback) {
        const std::string value = ToLowerAscii(TrimSpaces(SvgProperty(node, "font-size")));
        if (value.empty()) {
            return fallback;
        }
        if (value.size() > 2 && value.compare(value.size() - 2, 2, "px") == 0) {
            return std::atof(value.c_str());
        }
        if (value.size() > 2 && value.compare(value.size() - 2, 2, "em") == 0) {
            return fallback * std::atof(value.c_str());
        }
        if (value.back() == '%') {
            return fallback * std::atof(value.c_str()) / 100.0;
        }
        return std::atof(value.c_str());
    }

    void ReadSvgPaint(const Node& node, SvgSpace& space) {
        const std::string visibility = ToLowerAscii(SvgProperty(node, "visibility"));
        if (visibility == "hidden" || visibility == "collapse") {
            space.hidden = true;
        } else if (visibility == "visible") {
            space.hidden = false;
        }
        if (!SvgProperty(node, "opacity").empty()) {
            space.opacity *= std::atof(SvgProperty(node, "opacity").c_str());
        }
        if (!SvgProperty(node, "color").empty()) {
            ParseColor(SvgProperty(node, "color"), space.currentColor);
        }
        std::string fillText = ToLowerAscii(SvgProperty(node, "fill"));
        if (fillText.empty()) {
            // The runtime maps a stylesheet color onto SVG shapes the way the
            // HTML side maps it onto text, so author rules like
            // "rect.hot { color: green }" tint the shape's fill.
            const std::string styledFill = ToLowerAscii(SvgCssProperty(node, "color"));
            if (!styledFill.empty()) {
                fillText = styledFill;
            }
        }
        if (fillText == "currentcolor") {
            space.fill = space.currentColor;
            space.fillUrl.clear();
        } else if (fillText == "none") {
            space.fill.alpha = 0;
            space.fillUrl.clear();
        } else if (fillText.find("url(") != std::string::npos) {
            space.fillUrl = fillText;
            space.fill.alpha = 0;
        } else if (!fillText.empty()) {
            ParseColor(fillText, space.fill);
            space.fillUrl.clear();
        }
        const std::string strokeText = ToLowerAscii(SvgProperty(node, "stroke"));
        if (strokeText == "currentcolor") {
            space.stroke = space.currentColor;
            space.strokeUrl.clear();
        } else if (strokeText == "none") {
            space.stroke.alpha = 0;
            space.strokeUrl.clear();
        } else if (strokeText.find("url(") != std::string::npos) {
            space.strokeUrl = strokeText;
            space.stroke.alpha = 0;
        } else if (!strokeText.empty()) {
            ParseColor(strokeText, space.stroke);
            space.strokeUrl.clear();
        }
        if (!SvgProperty(node, "stroke-width").empty()) {
            space.strokeWidth = std::atof(SvgProperty(node, "stroke-width").c_str());
        }
        if (!SvgProperty(node, "stroke-linejoin").empty()) {
            space.lineJoin = ToLowerAscii(SvgProperty(node, "stroke-linejoin"));
        }
        if (!SvgProperty(node, "stroke-linecap").empty()) {
            space.lineCap = ToLowerAscii(SvgProperty(node, "stroke-linecap"));
        }
        if (!SvgProperty(node, "stroke-miterlimit").empty()) {
            space.miterLimit = std::atof(SvgProperty(node, "stroke-miterlimit").c_str());
        }
        if (!SvgProperty(node, "fill-opacity").empty()) {
            space.fillOpacity = std::max(0.0, std::min(1.0, std::atof(SvgProperty(node, "fill-opacity").c_str())));
        }
        if (!SvgProperty(node, "stroke-opacity").empty()) {
            space.strokeOpacity = std::max(0.0, std::min(1.0, std::atof(SvgProperty(node, "stroke-opacity").c_str())));
        }
        const std::string rule = ToLowerAscii(SvgProperty(node, "fill-rule"));
        if (rule == "evenodd") {
            space.fillEvenOdd = true;
        } else if (rule == "nonzero") {
            space.fillEvenOdd = false;
        }
        const std::string shape = ToLowerAscii(SvgProperty(node, "shape-rendering"));
        if (shape == "geometricprecision") {
            space.shapeRendering = 1;
        } else if (shape == "crispedges") {
            space.shapeRendering = 2;
        } else if (shape == "auto" || shape == "optimizespeed") {
            space.shapeRendering = 0;
        }
        const std::string textHint = ToLowerAscii(SvgProperty(node, "text-rendering"));
        if (textHint == "optimizelegibility") {
            space.textRendering = 2;
        } else if (textHint == "geometricprecision" || textHint == "auto" || textHint == "optimizespeed") {
            space.textRendering = 0;
        }
        const std::string anchor = ToLowerAscii(SvgProperty(node, "text-anchor"));
        if (anchor == "start" || anchor == "middle" || anchor == "end") {
            space.textAnchor = anchor;
        }
        const std::string clip = SvgProperty(node, "clip-path");
        if (!clip.empty()) {
            const std::string id = SvgPaintId(clip);
            Node* root = const_cast<Node*>(&node);
            while (root->parent != nullptr) {
                root = root->parent;
            }
            Node* clipNode = id.empty() ? nullptr : FindSvgById(*root, id);
            if (clipNode != nullptr && ToLowerAscii(clipNode->tagName) == "clippath") {
                BuildSvgClipShapes(node, *clipNode, space);
            }
        }
        const std::string clipRect = ToLowerAscii(SvgProperty(node, "clip"));
        const std::size_t rectAt = clipRect.find("rect");
        if (rectAt != std::string::npos) {
            std::size_t index = clipRect.find('(', rectAt);
            double top = 0;
            double right = 0;
            double bottom = 0;
            double left = 0;
            if (index != std::string::npos) {
                ++index;
                if (ReadSvgNumber(clipRect, index, top) && ReadSvgNumber(clipRect, index, right) && ReadSvgNumber(clipRect, index, bottom) &&
                    ReadSvgNumber(clipRect, index, left) && right > left && bottom > top) {
                    const double elementX = SvgNumber(node, "x", 0);
                    const double elementY = SvgNumber(node, "y", 0);
                    space.clipped = true;
                    space.clipX = SvgX(space, elementX + left);
                    space.clipY = SvgY(space, elementY + top);
                    space.clipWidth = SvgLength(space, right - left, true);
                    space.clipHeight = SvgLength(space, bottom - top, false);
                }
            }
        }
    }

    bool SvgSwitchMatches(const Node& child) {
        if (HasAttribute(child, "requiredextensions") || HasAttribute(child, "requiredfeatures")) {
            return false;
        }
        const std::string languages = ToLowerAscii(AttributeValue(child, "systemlanguage"));
        if (languages.empty()) {
            return true;
        }
        const std::string user = "en";
        std::size_t start = 0;
        while (start < languages.size()) {
            const std::size_t comma = languages.find(',', start);
            std::string tag = languages.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            while (!tag.empty() && tag.front() == ' ') {
                tag.erase(tag.begin());
            }
            while (!tag.empty() && tag.back() == ' ') {
                tag.pop_back();
            }
            if (tag == user || (tag.size() > user.size() && tag.compare(0, user.size(), user) == 0 && tag[user.size()] == '-')) {
                return true;
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
        return false;
    }

    Node* SvgLink(Node& node) {
        for (Node* cursor = &node; cursor != nullptr; cursor = cursor->parent) {
            if (ToLowerAscii(cursor->tagName) == "a" &&
                (!AttributeValue(*cursor, "href").empty() || !AttributeValue(*cursor, "xlink:href").empty())) {
                return cursor;
            }
            if (cursor->kind == ElementKind::Svg) {
                break;
            }
        }
        return nullptr;
    }

    void RememberSvgLink(Node& node, int x, int y, int width, int height, bool fixed) {
        Node* link = SvgLink(node);
        if (link == nullptr || width <= 0 || height <= 0) {
            return;
        }
        if (ToLowerAscii(AttributeValue(node, "pointer-events")) == "none" || ToLowerAscii(AttributeValue(*link, "pointer-events")) == "none") {
            return;
        }
        ResolvedStyle style;
        AddBox(*link, x, y, width, height, style, fixed);
    }

    void StrokeEllipse(int centerX, int centerY, int radiusX, int radiusY, int thickness, Color color, bool fixed, const std::string& join,
        const std::string& cap, double miterLimit) {
        if (color.alpha == 0 || radiusX < 1 || radiusY < 1 || thickness <= 0) {
            return;
        }
        const double pi = std::acos(-1.0);
        const int steps = std::max(24, (radiusX + radiusY) * 2);
        std::vector<SvgPoint> outline;
        outline.reserve(static_cast<std::size_t>(steps));
        for (int step = 0; step < steps; ++step) {
            const double angle = 2.0 * pi * static_cast<double>(step) / static_cast<double>(steps);
            outline.push_back(SvgPoint{centerX + std::cos(angle) * radiusX, centerY + std::sin(angle) * radiusY});
        }
        StrokePolyline(outline, 0, 0, color, fixed, true, thickness, std::string(), 0, 0, join, cap, miterLimit);
    }

    void ApplySvgTransform(const std::string& text, SvgSpace& space, bool hasOrigin = false, double originX = 0, double originY = 0) {
        if (text.empty()) {
            return;
        }
        SvgMatrix local;
        if (!ParseSvgTransformList(text, local)) {
            return;
        }
        if (hasOrigin) {
            local = SvgMatrixMultiply(SvgMatrixTranslate(originX, originY),
                                     SvgMatrixMultiply(local, SvgMatrixTranslate(-originX, -originY)));
        }
        space.matrix = SvgMatrixMultiply(space.matrix, local);
    }

    void UserToPixelReal(const SvgSpace& space, double x, double y, double& outX, double& outY) {
        const double mappedX = space.matrix.a * x + space.matrix.c * y + space.matrix.e;
        const double mappedY = space.matrix.b * x + space.matrix.d * y + space.matrix.f;
        outX = space.originX + (mappedX - space.minX) * space.scaleX;
        outY = space.originY + (mappedY - space.minY) * space.scaleY;
    }

    void UserToPixel(const SvgSpace& space, double x, double y, int& outX, int& outY) {
        double realX = 0;
        double realY = 0;
        UserToPixelReal(space, x, y, realX, realY);
        outX = static_cast<int>(realX);
        outY = static_cast<int>(realY);
    }

    void ApplyViewBox(const Node& node, SvgSpace& space) {
        const std::string box = AttributeValue(node, "viewbox");
        if (box.empty() || space.pixelWidth <= 0 || space.pixelHeight <= 0) {
            return;
        }
        std::size_t index = 0;
        double minX = 0;
        double minY = 0;
        double boxWidth = 0;
        double boxHeight = 0;
        if (!ReadSvgNumber(box, index, minX) || !ReadSvgNumber(box, index, minY) || !ReadSvgNumber(box, index, boxWidth) ||
            !ReadSvgNumber(box, index, boxHeight) || boxWidth == 0 || boxHeight == 0) {
            return;
        }
        space.scaleX = space.pixelWidth / boxWidth;
        space.scaleY = space.pixelHeight / boxHeight;
        space.minX = minX;
        space.minY = minY;
        std::string aspect = ToLowerAscii(AttributeValue(node, "preserveaspectratio"));
        if (aspect.empty()) {
            aspect = "xmidymid meet";
        }
        if (aspect.find("none") == std::string::npos) {
            const bool slice = aspect.find("slice") != std::string::npos;
            const double uniform = slice ? std::max(space.scaleX, space.scaleY) : std::min(space.scaleX, space.scaleY);
            const double extraX = space.pixelWidth - boxWidth * uniform;
            const double extraY = space.pixelHeight - boxHeight * uniform;
            if (aspect.find("xmid") != std::string::npos) {
                space.originX += extraX / 2;
            } else if (aspect.find("xmax") != std::string::npos) {
                space.originX += extraX;
            }
            if (aspect.find("ymid") != std::string::npos) {
                space.originY += extraY / 2;
            } else if (aspect.find("ymax") != std::string::npos) {
                space.originY += extraY;
            }
            space.scaleX = uniform;
            space.scaleY = uniform;
        }
    }

    // SvgX and SvgY map a single coordinate through the affine matrix, so they
    // stay exact for axis-aligned transforms. Rotated or skewed geometry is
    // mapped with UserToPixel/MapSvgPoints instead.
    int SvgX(const SvgSpace& space, double x) {
        const double mapped = space.matrix.a * x + space.matrix.e;
        return static_cast<int>(space.originX + (mapped - space.minX) * space.scaleX);
    }

    int SvgY(const SvgSpace& space, double y) {
        const double mapped = space.matrix.d * y + space.matrix.f;
        return static_cast<int>(space.originY + (mapped - space.minY) * space.scaleY);
    }

    int SvgLength(const SvgSpace& space, double length, bool horizontal) {
        const double factor = horizontal ? space.matrix.a : space.matrix.d;
        return std::max(0, static_cast<int>(length * factor * (horizontal ? space.scaleX : space.scaleY)));
    }

    // Maps user-space geometry to document pixels without rounding: keeping the
    // fractional coordinates is what lets polygon fills, clipPath contours and
    // motion paths match the shape the browser rasterizes.
    std::vector<SvgPoint> MapSvgPoints(const std::vector<SvgPoint>& points, const SvgSpace& space) {
        std::vector<SvgPoint> mapped;
        mapped.reserve(points.size());
        for (const SvgPoint& point : points) {
            double pixelX = 0;
            double pixelY = 0;
            UserToPixelReal(space, point.x, point.y, pixelX, pixelY);
            mapped.push_back(SvgPoint{pixelX, pixelY});
        }
        return mapped;
    }

    Node* FindSvgById(Node& node, const std::string& id) {
        if (AttributeValue(node, "id") == id) {
            return &node;
        }
        for (const std::unique_ptr<Node>& child : node.children) {
            if (Node* found = FindSvgById(*child, id)) {
                return found;
            }
        }
        return nullptr;
    }

    std::string SvgPaintId(const std::string& reference) {
        const std::size_t hash = reference.find('#');
        if (hash == std::string::npos) {
            return "";
        }
        std::string id = reference.substr(hash + 1);
        while (!id.empty() && (id.back() == ')' || id.back() == '\'' || id.back() == '"' || id.back() == ' ')) {
            id.pop_back();
        }
        return id;
    }

    double SvgOffset(const std::string& text) {
        if (text.find('%') != std::string::npos) {
            return std::atof(text.c_str()) / 100.0;
        }
        return std::atof(text.c_str());
    }

    struct SvgStop {
        double offset = 0;
        Color color{0, 0, 0, 255};
    };

    void CollectStops(Node& gradient, std::vector<SvgStop>& stops, int depth) {
        if (depth > 4) {
            return;
        }
        for (const std::unique_ptr<Node>& child : gradient.children) {
            if (ToLowerAscii(child->tagName) != "stop") {
                continue;
            }
            SvgStop stop;
            stop.offset = SvgOffset(AttributeValue(*child, "offset"));
            const std::string colorText = SvgProperty(*child, "stop-color");
            if (!colorText.empty()) {
                ParseColor(colorText, stop.color);
            }
            if (!SvgProperty(*child, "stop-opacity").empty()) {
                stop.color.alpha = static_cast<std::uint8_t>(
                    std::max(0.0, std::min(255.0, std::atof(SvgProperty(*child, "stop-opacity").c_str()) * 255.0)));
            }
            stops.push_back(stop);
        }
        if (!stops.empty()) {
            std::sort(stops.begin(), stops.end(), [](const SvgStop& left, const SvgStop& right) { return left.offset < right.offset; });
            return;
        }
        std::string href = AttributeValue(gradient, "href");
        if (href.empty()) {
            href = AttributeValue(gradient, "xlink:href");
        }
        const std::string id = SvgPaintId(href);
        if (id.empty()) {
            return;
        }
        Node* root = &gradient;
        while (root->parent != nullptr) {
            root = root->parent;
        }
        if (Node* next = FindSvgById(*root, id)) {
            CollectStops(*next, stops, depth + 1);
        }
    }

    int ScaleByte(double value) {
        if (!(value > 0)) {
            return 0;
        }
        if (value >= 1) {
            return 255;
        }
        return static_cast<int>(value * 255.0 + 0.5);
    }

    double SrgbToLinear(int channel) {
        const double encoded = static_cast<double>(channel) / 255.0;
        if (encoded <= 0.04045) {
            return encoded / 12.92;
        }
        return std::pow((encoded + 0.055) / 1.055, 2.4);
    }

    double LinearToSrgb(double linear) {
        if (!(linear > 0)) {
            return 0;
        }
        if (linear >= 1) {
            return 1;
        }
        if (linear <= 0.0031308) {
            return linear * 12.92;
        }
        return 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    }

    __attribute__((noinline, noclone, cdecl)) Color SampleStops(const std::vector<SvgStop>& stops, double t, bool linear) {
        if (stops.empty()) {
            return Color{0, 0, 0, 255};
        }
        if (!(t > 0)) {
            t = 0;
        }
        if (t > 1) {
            t = 1;
        }
        if (stops.size() == 1 || t <= stops.front().offset) {
            return stops.front().color;
        }
        if (t >= stops.back().offset) {
            return stops.back().color;
        }
        for (std::size_t index = 1; index < stops.size(); ++index) {
            if (t > stops[index].offset) {
                continue;
            }
            const SvgStop& left = stops[index - 1];
            const SvgStop& right = stops[index];
            const double span = right.offset - left.offset;
            double mix = span <= 0 ? 0 : (t - left.offset) / span;
            if (!(mix > 0)) {
                mix = 0;
            }
            if (mix > 1) {
                mix = 1;
            }
            if (!linear) {
                const int weight = mix <= 0 ? 0 : (mix >= 1 ? 256 : static_cast<int>(mix * 256.0));
                Color color;
                color.red = static_cast<std::uint8_t>(left.color.red + (static_cast<int>(right.color.red) - left.color.red) * weight / 256);
                color.green = static_cast<std::uint8_t>(left.color.green + (static_cast<int>(right.color.green) - left.color.green) * weight / 256);
                color.blue = static_cast<std::uint8_t>(left.color.blue + (static_cast<int>(right.color.blue) - left.color.blue) * weight / 256);
                color.alpha = static_cast<std::uint8_t>(left.color.alpha + (static_cast<int>(right.color.alpha) - left.color.alpha) * weight / 256);
                return color;
            }
            Color color;
            color.red = static_cast<std::uint8_t>(ScaleByte(LinearToSrgb(SrgbToLinear(left.color.red) * (1 - mix) + SrgbToLinear(right.color.red) * mix)));
            color.green = static_cast<std::uint8_t>(ScaleByte(LinearToSrgb(SrgbToLinear(left.color.green) * (1 - mix) + SrgbToLinear(right.color.green) * mix)));
            color.blue = static_cast<std::uint8_t>(ScaleByte(LinearToSrgb(SrgbToLinear(left.color.blue) * (1 - mix) + SrgbToLinear(right.color.blue) * mix)));
            const int weight = mix <= 0 ? 0 : (mix >= 1 ? 256 : static_cast<int>(mix * 256.0));
            color.alpha = static_cast<std::uint8_t>(left.color.alpha + (static_cast<int>(right.color.alpha) - left.color.alpha) * weight / 256);
            return color;
        }
        return stops.back().color;
    }

    struct GradientPaint {
        bool radial = false;
        bool linearRgb = false;
        double opacity = 1;
        std::string spread;
        std::vector<SvgStop> stops;
        double startX = 0;
        double startY = 0;
        double deltaX = 0;
        double deltaY = 0;
        double length = 0;
        double centerX = 0;
        double centerY = 0;
        double radiusX = 1;
        double radiusY = 1;
        double focusX = 0;
        double focusY = 0;
        double focalRadius = 0;
        bool adjustFocus = false;
        bool clipped = false;
        int clipX = 0;
        int clipY = 0;
        int clipWidth = 0;
        int clipHeight = 0;
    };

    // Gradient attributes follow the href/xlink:href template chain: a value a
    // gradient does not define itself comes from the gradient it references.
    std::string SvgGradientAttribute(Node& gradient, const char* name, int depth = 0) {
        const std::string own = AttributeValue(gradient, name);
        if (!own.empty() || depth > 4) {
            return own;
        }
        std::string href = AttributeValue(gradient, "href");
        if (href.empty()) {
            href = AttributeValue(gradient, "xlink:href");
        }
        const std::string id = SvgPaintId(href);
        if (id.empty()) {
            return own;
        }
        Node* root = &gradient;
        while (root->parent != nullptr) {
            root = root->parent;
        }
        if (Node* next = FindSvgById(*root, id)) {
            return SvgGradientAttribute(*next, name, depth + 1);
        }
        return own;
    }

    bool SvgGradientMatrix(Node& gradient, SvgMatrix& matrix) {
        const std::string text = SvgGradientAttribute(gradient, "gradienttransform");
        if (text.empty()) {
            return false;
        }
        return ParseSvgTransformList(text, matrix);
    }

    void SvgApplyGradientMatrix(const SvgMatrix& matrix, double& x, double& y) {
        if (SvgMatrixIsIdentity(matrix)) {
            return;
        }
        const double mappedX = matrix.a * x + matrix.c * y + matrix.e;
        const double mappedY = matrix.b * x + matrix.d * y + matrix.f;
        x = mappedX;
        y = mappedY;
    }

    bool PrepareGradient(Node& shape, const SvgSpace& space, const std::string& paintUrl, int x, int y, int width, int height, GradientPaint& paint) {
        const std::string id = SvgPaintId(paintUrl.empty() ? space.fillUrl : paintUrl);
        if (id.empty() || width <= 0 || height <= 0) {
            return false;
        }
        Node* root = &shape;
        while (root->parent != nullptr) {
            root = root->parent;
        }
        Node* gradient = FindSvgById(*root, id);
        const std::string gradientTag = gradient == nullptr ? std::string() : ToLowerAscii(gradient->tagName);
        if (gradient == nullptr || (gradientTag != "lineargradient" && gradientTag != "radialgradient")) {
            return false;
        }
        CollectStops(*gradient, paint.stops, 0);
        paint.linearRgb = ToLowerAscii(SvgGradientAttribute(*gradient, "color-interpolation")) == "linearrgb";
        paint.opacity = space.opacity * space.fillOpacity;
        paint.clipped = space.clipped;
        paint.clipX = space.clipX;
        paint.clipY = space.clipY;
        paint.clipWidth = space.clipWidth;
        paint.clipHeight = space.clipHeight;
        const bool objectBox = ToLowerAscii(SvgGradientAttribute(*gradient, "gradientunits")) != "userspaceonuse";
        paint.spread = ToLowerAscii(SvgGradientAttribute(*gradient, "spreadmethod"));
        SvgMatrix gradientMatrix;
        const bool hasGradientMatrix = SvgGradientMatrix(*gradient, gradientMatrix);
        if (gradientTag == "radialgradient") {
            paint.radial = true;
            double cx = SvgGradientAttribute(*gradient, "cx").empty() ? 0.5 : SvgOffset(SvgGradientAttribute(*gradient, "cx"));
            double cy = SvgGradientAttribute(*gradient, "cy").empty() ? 0.5 : SvgOffset(SvgGradientAttribute(*gradient, "cy"));
            double radius = SvgGradientAttribute(*gradient, "r").empty() ? 0.5 : SvgOffset(SvgGradientAttribute(*gradient, "r"));
            double radiusXUnits = radius;
            double radiusYUnits = radius;
            if (hasGradientMatrix) {
                double edgeX = cx + radius;
                double edgeY = cy;
                double edgeDownX = cx;
                double edgeDownY = cy + radius;
                SvgApplyGradientMatrix(gradientMatrix, cx, cy);
                SvgApplyGradientMatrix(gradientMatrix, edgeX, edgeY);
                SvgApplyGradientMatrix(gradientMatrix, edgeDownX, edgeDownY);
                radiusXUnits = std::sqrt((edgeX - cx) * (edgeX - cx) + (edgeY - cy) * (edgeY - cy));
                radiusYUnits = std::sqrt((edgeDownX - cx) * (edgeDownX - cx) + (edgeDownY - cy) * (edgeDownY - cy));
            }
            paint.centerX = objectBox ? x + cx * width : SvgX(space, cx);
            paint.centerY = objectBox ? y + cy * height : SvgY(space, cy);
            paint.radiusX = std::max(0.001, objectBox ? radiusXUnits * width : radiusXUnits * std::abs(space.matrix.a) * space.scaleX);
            paint.radiusY = std::max(0.001, objectBox ? radiusYUnits * height : radiusYUnits * std::abs(space.matrix.d) * space.scaleY);
            double focalUserX = SvgGradientAttribute(*gradient, "fx").empty() ? cx : SvgOffset(SvgGradientAttribute(*gradient, "fx"));
            double focalUserY = SvgGradientAttribute(*gradient, "fy").empty() ? cy : SvgOffset(SvgGradientAttribute(*gradient, "fy"));
            if (hasGradientMatrix) {
                SvgApplyGradientMatrix(gradientMatrix, focalUserX, focalUserY);
            }
            const double focalX = objectBox ? x + focalUserX * width : SvgX(space, focalUserX);
            const double focalY = objectBox ? y + focalUserY * height : SvgY(space, focalUserY);
            paint.focusX = (focalX - paint.centerX) / paint.radiusX;
            paint.focusY = (focalY - paint.centerY) / paint.radiusY;
            if (!SvgGradientAttribute(*gradient, "fr").empty() && radius > 0) {
                paint.adjustFocus = true;
                paint.focalRadius = SvgOffset(SvgGradientAttribute(*gradient, "fr")) / radius;
            }
            return true;
        }
        double x1 = AttributeValue(*gradient, "x1").empty() ? 0 : SvgOffset(AttributeValue(*gradient, "x1"));
        double y1 = AttributeValue(*gradient, "y1").empty() ? 0 : SvgOffset(AttributeValue(*gradient, "y1"));
        double x2 = AttributeValue(*gradient, "x2").empty() ? (objectBox ? 1 : static_cast<double>(width)) : SvgOffset(AttributeValue(*gradient, "x2"));
        double y2 = AttributeValue(*gradient, "y2").empty() ? 0 : SvgOffset(AttributeValue(*gradient, "y2"));
        if (hasGradientMatrix) {
            SvgApplyGradientMatrix(gradientMatrix, x1, y1);
            SvgApplyGradientMatrix(gradientMatrix, x2, y2);
        }
        paint.startX = objectBox ? x + x1 * width : SvgX(space, x1);
        paint.startY = objectBox ? y + y1 * height : SvgY(space, y1);
        const double endX = objectBox ? x + x2 * width : SvgX(space, x2);
        const double endY = objectBox ? y + y2 * height : SvgY(space, y2);
        paint.deltaX = endX - paint.startX;
        paint.deltaY = endY - paint.startY;
        paint.length = paint.deltaX * paint.deltaX + paint.deltaY * paint.deltaY;
        return true;
    }

    // spreadMethod = repeat | reflect folds the parametric gradient offset back
    // into [0,1]; the default pad leaves it for the final clamp.
    double SvgSpreadOffset(const std::string& spread, double value) {
        if (spread == "repeat") {
            return value - std::floor(value);
        }
        if (spread == "reflect") {
            if (value < 0) {
                value = -value;
            }
            const double period = std::floor(value);
            const double fraction = value - period;
            int turns = 0;
            if (period > 0 && period < 1000000) {
                turns = static_cast<int>(period);
            }
            return turns % 2 == 0 ? fraction : 1 - fraction;
        }
        return value;
    }

    Color SampleGradient(const GradientPaint& paint, int pixelX, int pixelY) {
        if (paint.clipped && (pixelX < paint.clipX || pixelY < paint.clipY || pixelX >= paint.clipX + paint.clipWidth || pixelY >= paint.clipY + paint.clipHeight)) {
            return Color{0, 0, 0, 0};
        }
        double offset = 0;
        if (paint.radial) {
            const double pointX = (pixelX - paint.centerX) / paint.radiusX;
            const double pointY = (pixelY - paint.centerY) / paint.radiusY;
            const double vectorX = pointX - paint.focusX;
            const double vectorY = pointY - paint.focusY;
            const double quadraticA = paint.focusX * paint.focusX + paint.focusY * paint.focusY - 1;
            const double quadraticB = 2 * (paint.focusX * vectorX + paint.focusY * vectorY);
            const double quadraticC = vectorX * vectorX + vectorY * vectorY;
            const double discriminant = quadraticB * quadraticB - 4 * quadraticA * quadraticC;
            if (std::abs(quadraticA) < 0.000001) {
                offset = std::abs(quadraticB) < 0.000001 ? 0 : -quadraticC / quadraticB;
            } else if (discriminant >= 0) {
                offset = (-quadraticB - std::sqrt(discriminant)) / (2 * quadraticA);
                const double other = (-quadraticB + std::sqrt(discriminant)) / (2 * quadraticA);
                if (offset < 0 || (other >= 0 && other < offset)) {
                    offset = other;
                }
            }
            if (paint.adjustFocus) {
                if (paint.focalRadius >= 1) {
                    offset = offset <= 1 ? 0 : offset;
                } else if (paint.focalRadius > 0) {
                    offset = (offset - paint.focalRadius) / (1 - paint.focalRadius);
                }
            }
            offset = SvgSpreadOffset(paint.spread, offset);
        } else if (paint.length > 0) {
            offset = ((pixelX - paint.startX) * paint.deltaX + (pixelY - paint.startY) * paint.deltaY) / paint.length;
            offset = SvgSpreadOffset(paint.spread, offset);
        }
        if (!(offset >= 0)) {
            offset = 0;
        }
        if (offset > 1) {
            offset = 1;
        }
        return WithOpacity(SampleStops(paint.stops, offset, paint.linearRgb), paint.opacity);
    }

    bool GradientColorAt(Node& shape, const SvgSpace& space, int boxX, int boxY, int boxWidth, int boxHeight, int pixelX, int pixelY, Color& color, const std::string& paintUrl = "") {
        GradientPaint paint;
        if (!PrepareGradient(shape, space, paintUrl, boxX, boxY, boxWidth, boxHeight, paint)) {
            return false;
        }
        color = SampleGradient(paint, pixelX, pixelY);
        return color.alpha != 0;
    }

    // `centerSampling` maps a span back to the pixel columns whose centers the
    // span actually covers. Fill geometry keeps the historic floor mapping,
    // while stroke outlines use it so sub-pixel boundaries keep the pixels a
    // browser paints at the edge of a join, cap or dash.
    void FillContours(const std::vector<std::vector<SvgPoint>>& contours, int originX, int originY, Color color, bool fixed, bool evenOdd,
        bool centerSampling = false) {
        // Coverage antialiasing supersedes the old half-pixel snapping, so the
        // centerSampling flag is accepted for compatibility and no longer used.
        (void)centerSampling;
        if (color.alpha == 0) {
            return;
        }
        int minY = 1000000;
        int maxY = -1000000;
        for (const std::vector<SvgPoint>& contour : contours) {
            for (const SvgPoint& point : contour) {
                minY = std::min(minY, static_cast<int>(std::floor(point.y)));
                maxY = std::max(maxY, static_cast<int>(std::ceil(point.y)));
            }
        }
        std::vector<SvgCoverageSpan> spans;
        for (int row = minY; row <= maxY; ++row) {
            SvgCoverageRow coverage;
            for (int sub = 0; sub < SVG_AA_SUBROWS; ++sub) {
                const double sampleY = static_cast<double>(row) + (static_cast<double>(sub) + 0.5) / SVG_AA_SUBROWS;
                CollectContourSpans(contours, sampleY, evenOdd, spans);
                for (const SvgCoverageSpan& span : spans) {
                    coverage.AddSpan(span.left, span.right, 1.0 / SVG_AA_SUBROWS);
                }
            }
            EmitCoverageRow(coverage, originX, originY, row, color, fixed);
        }
    }

    void FillContoursGradient(const std::vector<std::vector<SvgPoint>>& contours, int originX, int originY, Node& shape, const SvgSpace& space, int boxX, int boxY, int boxWidth, int boxHeight, bool fixed, bool evenOdd) {
        GradientPaint paint;
        if (!PrepareGradient(shape, space, "", boxX, boxY, boxWidth, boxHeight, paint)) {
            return;
        }
        int minY = 1000000;
        int maxY = -1000000;
        for (const std::vector<SvgPoint>& contour : contours) {
            for (const SvgPoint& point : contour) {
                minY = std::min(minY, static_cast<int>(std::floor(point.y)));
                maxY = std::max(maxY, static_cast<int>(std::ceil(point.y)));
            }
        }
        std::vector<SvgCoverageSpan> spans;
        for (int row = minY; row <= maxY; ++row) {
            SvgCoverageRow coverage;
            for (int sub = 0; sub < SVG_AA_SUBROWS; ++sub) {
                const double sampleY = static_cast<double>(row) + (static_cast<double>(sub) + 0.5) / SVG_AA_SUBROWS;
                CollectContourSpans(contours, sampleY, evenOdd, spans);
                for (const SvgCoverageSpan& span : spans) {
                    coverage.AddSpan(span.left, span.right, 1.0 / SVG_AA_SUBROWS);
                }
            }
            EmitCoverageGradientRow(coverage, originX, originY, row, paint, fixed);
        }
    }

    // Gradient rows cannot merge neighbouring pixels blindly, so a run ends
    // wherever the sampled color or the coverage-derived alpha changes.
    void EmitCoverageGradientRow(const SvgCoverageRow& row, int originX, int originY, int pixelRow, const GradientPaint& paint, bool fixed) {
        if (row.Empty()) {
            return;
        }
        int runStart = -1;
        Color runColor;
        for (std::size_t index = 0; index <= row.cover.size(); ++index) {
            const bool atEnd = index == row.cover.size();
            Color next;
            int alpha = 0;
            if (!atEnd) {
                next = SampleGradient(paint, originX + row.first + static_cast<int>(index), originY + pixelRow);
                alpha = SvgCoverageAlpha(next.alpha, row.cover[index]);
                next.alpha = static_cast<std::uint8_t>(alpha);
            }
            const bool sameRun = runStart >= 0 && !atEnd && alpha > 0 && next.red == runColor.red && next.green == runColor.green &&
                                 next.blue == runColor.blue && next.alpha == runColor.alpha;
            if (sameRun) {
                continue;
            }
            if (runStart >= 0) {
                const int runEndColumn = row.first + static_cast<int>(index);
                Fill(originX + runStart, originY + pixelRow, runEndColumn - runStart, 1, runColor, fixed);
                runStart = -1;
            }
            if (!atEnd && alpha > 0) {
                runStart = row.first + static_cast<int>(index);
                runColor = next;
            }
        }
    }

    bool FillLinearGradient(Node& node, const SvgSpace& space, int x, int y, int width, int height, bool fixed) {
        const std::string id = SvgPaintId(space.fillUrl);
        if (id.empty() || width <= 0 || height <= 0) {
            return false;
        }
        Node* root = &node;
        while (root->parent != nullptr) {
            root = root->parent;
        }
        Node* gradient = FindSvgById(*root, id);
        const std::string gradientTag = gradient == nullptr ? std::string() : ToLowerAscii(gradient->tagName);
        if (gradient == nullptr || (gradientTag != "lineargradient" && gradientTag != "radialgradient")) {
            return false;
        }
        std::vector<SvgStop> stops;
        CollectStops(*gradient, stops, 0);
        const bool objectBox = ToLowerAscii(SvgGradientAttribute(*gradient, "gradientunits")) != "userspaceonuse";
        const std::string spread = ToLowerAscii(SvgGradientAttribute(*gradient, "spreadmethod"));
        auto spreadOffset = [&spread](double value) {
            if (spread == "repeat") {
                return value - std::floor(value);
            }
            if (spread == "reflect") {
                if (value < 0) {
                    value = -value;
                }
                const double period = std::floor(value);
                const double fraction = value - period;
                int turns = 0;
                if (period > 0 && period < 1000000) {
                    turns = static_cast<int>(period);
                }
                return turns % 2 == 0 ? fraction : 1 - fraction;
            }
            return value;
        };
        SvgMatrix gradientMatrix;
        const bool hasGradientMatrix = SvgGradientMatrix(*gradient, gradientMatrix);
        if (gradientTag == "radialgradient") {
            double cx = SvgGradientAttribute(*gradient, "cx").empty() ? 0.5 : SvgOffset(SvgGradientAttribute(*gradient, "cx"));
            double cy = SvgGradientAttribute(*gradient, "cy").empty() ? 0.5 : SvgOffset(SvgGradientAttribute(*gradient, "cy"));
            double radius = SvgGradientAttribute(*gradient, "r").empty() ? 0.5 : SvgOffset(SvgGradientAttribute(*gradient, "r"));
            double radiusXUnits = radius;
            double radiusYUnits = radius;
            if (hasGradientMatrix) {
                double edgeX = cx + radius;
                double edgeY = cy;
                double edgeDownX = cx;
                double edgeDownY = cy + radius;
                SvgApplyGradientMatrix(gradientMatrix, cx, cy);
                SvgApplyGradientMatrix(gradientMatrix, edgeX, edgeY);
                SvgApplyGradientMatrix(gradientMatrix, edgeDownX, edgeDownY);
                radiusXUnits = std::sqrt((edgeX - cx) * (edgeX - cx) + (edgeY - cy) * (edgeY - cy));
                radiusYUnits = std::sqrt((edgeDownX - cx) * (edgeDownX - cx) + (edgeDownY - cy) * (edgeDownY - cy));
            }
            const double centerX = objectBox ? x + cx * width : SvgX(space, cx);
            const double centerY = objectBox ? y + cy * height : SvgY(space, cy);
            const double radiusX = std::max(0.001, objectBox ? radiusXUnits * width : radiusXUnits * std::abs(space.matrix.a) * space.scaleX);
            const double radiusY = std::max(0.001, objectBox ? radiusYUnits * height : radiusYUnits * std::abs(space.matrix.d) * space.scaleY);
            double focalUserX = SvgGradientAttribute(*gradient, "fx").empty() ? cx : SvgOffset(SvgGradientAttribute(*gradient, "fx"));
            double focalUserY = SvgGradientAttribute(*gradient, "fy").empty() ? cy : SvgOffset(SvgGradientAttribute(*gradient, "fy"));
            if (hasGradientMatrix) {
                SvgApplyGradientMatrix(gradientMatrix, focalUserX, focalUserY);
            }
            const double focalX = objectBox ? x + focalUserX * width : SvgX(space, focalUserX);
            const double focalY = objectBox ? y + focalUserY * height : SvgY(space, focalUserY);
            const double focusX = (focalX - centerX) / radiusX;
            const double focusY = (focalY - centerY) / radiusY;
            for (int row = 0; row < height; ++row) {
                for (int column = 0; column < width; ++column) {
                    if (space.clipped && (x + column < space.clipX || y + row < space.clipY || x + column >= space.clipX + space.clipWidth ||
                                             y + row >= space.clipY + space.clipHeight)) {
                        continue;
                    }
                    const double pointX = (x + column - centerX) / radiusX;
                    const double pointY = (y + row - centerY) / radiusY;
                    const double vectorX = pointX - focusX;
                    const double vectorY = pointY - focusY;
                    double offset = 0;
                    const double quadraticA = focusX * focusX + focusY * focusY - 1;
                    const double quadraticB = 2 * (focusX * vectorX + focusY * vectorY);
                    const double quadraticC = vectorX * vectorX + vectorY * vectorY;
                    const double discriminant = quadraticB * quadraticB - 4 * quadraticA * quadraticC;
                    if (std::abs(quadraticA) < 0.000001) {
                        offset = std::abs(quadraticB) < 0.000001 ? 0 : -quadraticC / quadraticB;
                    } else if (discriminant >= 0) {
                        offset = (-quadraticB - std::sqrt(discriminant)) / (2 * quadraticA);
                        const double other = (-quadraticB + std::sqrt(discriminant)) / (2 * quadraticA);
                        if (offset < 0 || (other >= 0 && other < offset)) {
                            offset = other;
                        }
                    }
                    if (!SvgGradientAttribute(*gradient, "fr").empty() && radius > 0) {
                        const double focalRadius = SvgOffset(SvgGradientAttribute(*gradient, "fr")) / radius;
                        if (focalRadius >= 1) {
                            offset = offset <= 1 ? 0 : offset;
                        } else if (focalRadius > 0) {
                            offset = (offset - focalRadius) / (1 - focalRadius);
                        }
                    }
                    offset = spreadOffset(offset);
                    Fill(x + column, y + row, 1, 1, WithOpacity(SampleStops(stops, offset, ToLowerAscii(SvgGradientAttribute(*gradient, "color-interpolation")) == "linearrgb"), space.opacity * space.fillOpacity), fixed);
                }
            }
            return true;
        }
        double x1 = SvgGradientAttribute(*gradient, "x1").empty() ? 0 : SvgOffset(SvgGradientAttribute(*gradient, "x1"));
        double y1 = SvgGradientAttribute(*gradient, "y1").empty() ? 0 : SvgOffset(SvgGradientAttribute(*gradient, "y1"));
        double x2 = SvgGradientAttribute(*gradient, "x2").empty() ? (objectBox ? 1 : static_cast<double>(width))
                                                               : SvgOffset(SvgGradientAttribute(*gradient, "x2"));
        double y2 = SvgGradientAttribute(*gradient, "y2").empty() ? 0 : SvgOffset(SvgGradientAttribute(*gradient, "y2"));
        if (hasGradientMatrix) {
            SvgApplyGradientMatrix(gradientMatrix, x1, y1);
            SvgApplyGradientMatrix(gradientMatrix, x2, y2);
        }
        const double startX = objectBox ? x + x1 * width : SvgX(space, x1);
        const double startY = objectBox ? y + y1 * height : SvgY(space, y1);
        const double endX = objectBox ? x + x2 * width : SvgX(space, x2);
        const double endY = objectBox ? y + y2 * height : SvgY(space, y2);
        const double deltaX = endX - startX;
        const double deltaY = endY - startY;
        const double length = deltaX * deltaX + deltaY * deltaY;
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                if (space.clipped && (x + column < space.clipX || y + row < space.clipY || x + column >= space.clipX + space.clipWidth ||
                                         y + row >= space.clipY + space.clipHeight)) {
                    continue;
                }
                double t = 0;
                if (length > 0) {
                    t = ((x + column - startX) * deltaX + (y + row - startY) * deltaY) / length;
                }
                t = spreadOffset(t);
                Fill(x + column, y + row, 1, 1, WithOpacity(SampleStops(stops, t, ToLowerAscii(SvgGradientAttribute(*gradient, "color-interpolation")) == "linearrgb"), space.opacity * space.fillOpacity), fixed);
            }
        }
        return true;
    }

    std::vector<SvgPoint> StraightPath(const std::string& data, double pixelScale = 1.0) {
        std::vector<SvgPoint> points;
        std::size_t index = 0;
        char command = 0;
        double x = 0;
        double y = 0;
        double startX = 0;
        double startY = 0;
        double lastControlX = 0;
        double lastControlY = 0;
        bool lastWasCubic = false;
        bool lastWasQuadratic = false;
        while (index < data.size()) {
            while (index < data.size() && (data[index] == ' ' || data[index] == '\t' || data[index] == '\n' || data[index] == '\r' || data[index] == ',')) {
                ++index;
            }
            if (index >= data.size()) {
                break;
            }
            if ((data[index] >= 'A' && data[index] <= 'Z') || (data[index] >= 'a' && data[index] <= 'z')) {
                command = data[index];
                ++index;
            }
            if (command == 0) {
                break;
            }
            const bool relative = command >= 'a' && command <= 'z';
            const char operation = relative ? static_cast<char>(command - 'a' + 'A') : command;
            if (operation == 'Z') {
                points.push_back(SvgPoint{startX, startY});
                x = startX;
                y = startY;
                lastWasCubic = false;
                lastWasQuadratic = false;
                command = 0;
                continue;
            }
            double first = 0;
            double second = 0;
            if (operation == 'H' || operation == 'V') {
                if (!ReadSvgNumber(data, index, first)) {
                    break;
                }
                if (operation == 'H') {
                    x = relative ? x + first : first;
                } else {
                    y = relative ? y + first : first;
                }
                points.push_back(SvgPoint{x, y});
                lastWasCubic = false;
                lastWasQuadratic = false;
                continue;
            }
            if (operation == 'A') {
                double radiusX = 0;
                double radiusY = 0;
                double rotation = 0;
                double largeArc = 0;
                double sweep = 0;
                double endX = 0;
                double endY = 0;
                if (!ReadSvgNumber(data, index, radiusX) || !ReadSvgNumber(data, index, radiusY) || !ReadSvgNumber(data, index, rotation) ||
                    !ReadSvgNumber(data, index, largeArc) || !ReadSvgNumber(data, index, sweep) || !ReadSvgNumber(data, index, endX) ||
                    !ReadSvgNumber(data, index, endY)) {
                    break;
                }
                if (relative) {
                    endX += x;
                    endY += y;
                }
                AddArc(points, x, y, radiusX, radiusY, rotation, largeArc >= 0.5, sweep >= 0.5, endX, endY, pixelScale);
                x = endX;
                y = endY;
                lastWasCubic = false;
                lastWasQuadratic = false;
                continue;
            }
            if (!ReadSvgNumber(data, index, first) || !ReadSvgNumber(data, index, second)) {
                break;
            }
            if (operation == 'T') {
                const double controlX = lastWasQuadratic ? x * 2 - lastControlX : x;
                const double controlY = lastWasQuadratic ? y * 2 - lastControlY : y;
                const double nextX = relative ? x + first : first;
                const double nextY = relative ? y + second : second;
                AddQuadratic(points, x, y, controlX, controlY, nextX, nextY, pixelScale);
                lastControlX = controlX;
                lastControlY = controlY;
                lastWasQuadratic = true;
                lastWasCubic = false;
                x = nextX;
                y = nextY;
                continue;
            }
            if (operation == 'M' || operation == 'L') {
                x = relative ? x + first : first;
                y = relative ? y + second : second;
                if (operation == 'M') {
                    startX = x;
                    startY = y;
                    command = relative ? 'l' : 'L';
                }
                points.push_back(SvgPoint{x, y});
                lastWasCubic = false;
                lastWasQuadratic = false;
                continue;
            }
            double third = 0;
            double fourth = 0;
            if (!ReadSvgNumber(data, index, third) || !ReadSvgNumber(data, index, fourth)) {
                break;
            }
            if (operation == 'Q') {
                const double controlX = relative ? x + first : first;
                const double controlY = relative ? y + second : second;
                const double nextX = relative ? x + third : third;
                const double nextY = relative ? y + fourth : fourth;
                AddQuadratic(points, x, y, controlX, controlY, nextX, nextY, pixelScale);
                lastControlX = controlX;
                lastControlY = controlY;
                lastWasQuadratic = true;
                lastWasCubic = false;
                x = nextX;
                y = nextY;
                continue;
            }
            if (operation == 'S') {
                const double controlX = relative ? x + first : first;
                const double controlY = relative ? y + second : second;
                const double nextX = relative ? x + third : third;
                const double nextY = relative ? y + fourth : fourth;
                const double reflectedX = lastWasCubic ? x * 2 - lastControlX : x;
                const double reflectedY = lastWasCubic ? y * 2 - lastControlY : y;
                AddCurve(points, x, y, reflectedX, reflectedY, controlX, controlY, nextX, nextY, pixelScale);
                lastControlX = controlX;
                lastControlY = controlY;
                lastWasCubic = true;
                lastWasQuadratic = false;
                x = nextX;
                y = nextY;
                continue;
            }
            double fifth = 0;
            double sixth = 0;
            if (!ReadSvgNumber(data, index, fifth) || !ReadSvgNumber(data, index, sixth)) {
                break;
            }
            const double x1 = relative ? x + first : first;
            const double y1 = relative ? y + second : second;
            const double x2 = relative ? x + third : third;
            const double y2 = relative ? y + fourth : fourth;
            const double nextX = relative ? x + fifth : fifth;
            const double nextY = relative ? y + sixth : sixth;
            AddCurve(points, x, y, x1, y1, x2, y2, nextX, nextY, pixelScale);
            lastControlX = x2;
            lastControlY = y2;
            lastWasCubic = true;
            lastWasQuadratic = false;
            x = nextX;
            y = nextY;
        }
        return points;
    }

    bool PointAlong(const std::vector<SvgPoint>& points, double distance, double& x, double& y) {
        if (points.empty()) {
            return false;
        }
        x = points.front().x;
        y = points.front().y;
        for (std::size_t index = 1; index < points.size(); ++index) {
            const double deltaX = points[index].x - points[index - 1].x;
            const double deltaY = points[index].y - points[index - 1].y;
            const double length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
            if (distance <= length || index + 1 == points.size()) {
                const double mix = length <= 0 ? 0 : std::min(1.0, distance / length);
                x = points[index - 1].x + deltaX * mix;
                y = points[index - 1].y + deltaY * mix;
                return true;
            }
            distance -= length;
        }
        return true;
    }

    void PaintMarkerAt(Node& node, const std::string& reference, double pixelX, double pixelY, const SvgSpace& space, bool fixed,
                       double autoAngle = 0, bool reverseAuto = false) {
        const std::string id = SvgPaintId(reference);
        if (id.empty()) {
            return;
        }
        Node* root = &node;
        while (root->parent != nullptr) {
            root = root->parent;
        }
        Node* marker = FindSvgById(*root, id);
        if (marker == nullptr || ToLowerAscii(marker->tagName) != "marker") {
            return;
        }
        const double refX = AttributeValue(*marker, "refx").empty() ? 0 : std::atof(AttributeValue(*marker, "refx").c_str());
        const double refY = AttributeValue(*marker, "refy").empty() ? 0 : std::atof(AttributeValue(*marker, "refy").c_str());
        const double markerWidth = AttributeValue(*marker, "markerwidth").empty() ? 3.0 : std::atof(AttributeValue(*marker, "markerwidth").c_str());
        const double markerHeight = AttributeValue(*marker, "markerheight").empty() ? 3.0 : std::atof(AttributeValue(*marker, "markerheight").c_str());
        double strokeScale = 1.0;
        if (ToLowerAscii(AttributeValue(*marker, "markerunits")) != "userspaceonuse") {
            strokeScale = std::max(0.0, space.strokeWidth);
        }
        SvgMatrix markerMatrix;
        const std::string orient = ToLowerAscii(AttributeValue(*marker, "orient"));
        if (orient == "auto" || orient == "auto-start-reverse") {
            double angle = autoAngle;
            if (orient == "auto-start-reverse" && reverseAuto) {
                angle += 180.0;
            }
            markerMatrix = SvgMatrixRotate(angle, 0, 0);
        } else if (!orient.empty()) {
            markerMatrix = SvgMatrixRotate(std::atof(orient.c_str()), 0, 0);
        }
        markerMatrix = SvgMatrixMultiply(markerMatrix, SvgMatrixScale(markerWidth / 3.0 * strokeScale, markerHeight / 3.0 * strokeScale));
        markerMatrix = SvgMatrixMultiply(markerMatrix, SvgMatrixTranslate(-refX, -refY));
        SvgSpace local = space;
        local.matrix = SvgMatrixMultiply(local.matrix, markerMatrix);
        local.originX = pixelX;
        local.originY = pixelY;
        local.minX = 0;
        local.minY = 0;
        PaintSvgTree(*marker, local, fixed, true);
    }

    double SvgDirectionDegrees(double fromX, double fromY, double toX, double toY) {
        const double deltaX = toX - fromX;
        const double deltaY = toY - fromY;
        if (deltaX == 0 && deltaY == 0) {
            return 0;
        }
        return std::atan2(deltaY, deltaX) * 180.0 / std::acos(-1.0);
    }

    void PaintLineMarkers(Node& node, const SvgSpace& space, int x1, int y1, int x2, int y2, bool fixed) {
        const double angle = SvgDirectionDegrees(x1, y1, x2, y2);
        PaintMarkerAt(node, SvgProperty(node, "marker-start"), x1, y1, space, fixed, angle, true);
        PaintMarkerAt(node, SvgProperty(node, "marker-end"), x2, y2, space, fixed, angle, false);
    }

    bool FillPattern(Node& node, const SvgSpace& space, int x, int y, int width, int height, bool fixed) {
        const std::string id = SvgPaintId(space.fillUrl);
        if (id.empty() || width <= 0 || height <= 0) {
            return false;
        }
        Node* root = &node;
        while (root->parent != nullptr) {
            root = root->parent;
        }
        Node* pattern = FindSvgById(*root, id);
        if (pattern == nullptr || ToLowerAscii(pattern->tagName) != "pattern") {
            return false;
        }
        const bool userUnits = ToLowerAscii(AttributeValue(*pattern, "patternunits")) == "userspaceonuse";
        const double rawWidth = AttributeValue(*pattern, "width").empty() ? 0 : SvgOffset(AttributeValue(*pattern, "width"));
        const double rawHeight = AttributeValue(*pattern, "height").empty() ? 0 : SvgOffset(AttributeValue(*pattern, "height"));
        const int tileWidth = std::max(0, static_cast<int>(userUnits ? rawWidth * space.scaleX : rawWidth * width));
        const int tileHeight = std::max(0, static_cast<int>(userUnits ? rawHeight * space.scaleY : rawHeight * height));
        if (tileWidth < 1 || tileHeight < 1) {
            return false;
        }
        const int shiftX = static_cast<int>(SvgOffset(AttributeValue(*pattern, "x")) * (userUnits ? space.scaleX : width));
        const int shiftY = static_cast<int>(SvgOffset(AttributeValue(*pattern, "y")) * (userUnits ? space.scaleY : height));
        for (int tileY = y + shiftY; tileY < y + height; tileY += tileHeight) {
            for (int tileX = x + shiftX; tileX < x + width; tileX += tileWidth) {
                SvgSpace tile = space;
                tile.originX = tileX;
                tile.originY = tileY;
                tile.minX = 0;
                tile.minY = 0;
                tile.scaleX = space.scaleX;
                tile.scaleY = space.scaleY;
                if (ToLowerAscii(AttributeValue(*pattern, "patterncontentunits")) == "objectboundingbox") {
                    tile.scaleX = tileWidth;
                    tile.scaleY = tileHeight;
                }
                const std::string patternTransform = AttributeValue(*pattern, "patterntransform");
                if (!patternTransform.empty()) {
                    ApplySvgTransform(patternTransform, tile);
                }
                tile.clipped = true;
                tile.clipX = tileX;
                tile.clipY = tileY;
                tile.clipWidth = std::min(tileWidth, x + width - tileX);
                tile.clipHeight = std::min(tileHeight, y + height - tileY);
                if (space.clipped) {
                    const int right = std::min(tile.clipX + tile.clipWidth, space.clipX + space.clipWidth);
                    const int bottom = std::min(tile.clipY + tile.clipHeight, space.clipY + space.clipHeight);
                    tile.clipX = std::max(tile.clipX, space.clipX);
                    tile.clipY = std::max(tile.clipY, space.clipY);
                    tile.clipWidth = right - tile.clipX;
                    tile.clipHeight = bottom - tile.clipY;
                }
                if (tile.clipWidth <= 0 || tile.clipHeight <= 0) {
                    continue;
                }
                for (const std::unique_ptr<Node>& child : pattern->children) {
                    if (child->type == Node::Type::Element) {
                        PaintSvgTree(*child, tile, fixed, true);
                    }
                }
            }
        }
        return true;
    }

    // Builds the user-space outline of one mask shape. The mask evaluator
    // supports every basic shape, so a <circle> or <path> masks exactly like it
    // does in a browser instead of only rectangles.
    bool SvgMaskChildContour(Node& child, const std::string& tag, std::vector<SvgPoint>& contour, double pixelScale = 1.0) {
        if (tag == "rect") {
            double radiusX = 0;
            double radiusY = 0;
            const double width = SvgReal(child, "width", 0);
            const double height = SvgReal(child, "height", 0);
            SvgRectRadii(child, width, height, radiusX, radiusY);
            contour = StraightPath(SvgRectPath(SvgReal(child, "x", 0), SvgReal(child, "y", 0), width, height, radiusX, radiusY), pixelScale);
            return !contour.empty();
        }
        if (tag == "circle" || tag == "ellipse") {
            const double centerX = SvgReal(child, "cx", 0);
            const double centerY = SvgReal(child, "cy", 0);
            const double radiusX = tag == "circle" ? SvgReal(child, "r", 0) : SvgReal(child, "rx", 0);
            const double radiusY = tag == "circle" ? radiusX : SvgReal(child, "ry", 0);
            const double span = std::max(std::abs(radiusX), std::abs(radiusY));
            const int steps = std::max(SVG_CLIP_CIRCLE_STEPS,
                std::min(SVG_CLIP_CIRCLE_MAX_STEPS, static_cast<int>(std::ceil(span * 4.0))));
            for (int step = 0; step < steps; ++step) {
                const double angle = 2.0 * std::acos(-1.0) * static_cast<double>(step) / static_cast<double>(steps);
                contour.push_back(SvgPoint{centerX + std::cos(angle) * radiusX, centerY + std::sin(angle) * radiusY});
            }
            return !contour.empty();
        }
        if (tag == "polygon" || tag == "polyline") {
            contour = ParsePointList(AttributeValue(child, "points"));
            return contour.size() >= 3;
        }
        if (tag == "path") {
            contour = StraightPath(SvgPathData(child), pixelScale);
            return !contour.empty();
        }
        return false;
    }

    void BuildSvgMaskPieces(Node& container, const SvgSpace& contentSpace, bool objectBox, double elementX, double elementY, double elementWidth,
        double elementHeight, SvgMaskContent& content) {
        for (const std::unique_ptr<Node>& child : container.children) {
            if (child->type != Node::Type::Element) {
                continue;
            }
            const std::string childTag = ToLowerAscii(child->tagName);
            if (childTag == "defs" || childTag == "title" || childTag == "desc" || childTag == "metadata" || childTag == "style" ||
                childTag == "script" || childTag == "clippath" || childTag == "mask" || childTag == "filter" || childTag == "lineargradient" ||
                childTag == "radialgradient" || childTag == "pattern" || childTag == "marker" || childTag == "symbol") {
                continue;
            }
            SvgSpace childSpace = contentSpace;
            ApplySvgTransform(SvgProperty(*child, "transform"), childSpace);
            if (childTag == "g" || childTag == "a" || childTag == "switch" || childTag == "svg") {
                BuildSvgMaskPieces(*child, childSpace, objectBox, elementX, elementY, elementWidth, elementHeight, content);
                continue;
            }
            std::vector<SvgPoint> contour;
            if (!SvgMaskChildContour(*child, childTag, contour, SvgDeviceScale(childSpace))) {
                continue;
            }
            if (objectBox) {
                for (SvgPoint& point : contour) {
                    point.x = elementX + point.x * elementWidth;
                    point.y = elementY + point.y * elementHeight;
                }
            }
            const std::string paint = SvgProperty(*child, "fill");
            if (ToLowerAscii(paint) == "none") {
                continue;
            }
            SvgMaskPiece piece;
            piece.evenOdd = ToLowerAscii(SvgProperty(*child, "fill-rule")) == "evenodd";
            if (!paint.empty() && paint.find("url(") == std::string::npos) {
                ParseColor(paint, piece.color);
            }
            if (!SvgProperty(*child, "fill-opacity").empty()) {
                piece.opacity *= std::max(0.0, std::min(1.0, std::atof(SvgProperty(*child, "fill-opacity").c_str())));
            }
            if (!SvgProperty(*child, "opacity").empty()) {
                piece.opacity *= std::max(0.0, std::min(1.0, std::atof(SvgProperty(*child, "opacity").c_str())));
            }
            piece.contours.push_back(MapSvgPoints(contour, childSpace));
            content.pieces.push_back(std::move(piece));
        }
    }

    bool BuildSvgMask(Node& node, const std::string& maskId, const SvgSpace& space, SvgMaskContent& content) {
        Node* root = &node;
        while (root->parent != nullptr) {
            root = root->parent;
        }
        Node* mask = FindSvgById(*root, maskId);
        if (mask == nullptr || ToLowerAscii(mask->tagName) != "mask") {
            return false;
        }
        content = SvgMaskContent();
        content.luminance = ToLowerAscii(SvgProperty(*mask, "mask-type")) != "alpha";
        const bool objectBox = ToLowerAscii(AttributeValue(*mask, "maskunits")) != "userspaceonuse";
        double elementX = SvgReal(node, "x", 0);
        double elementY = SvgReal(node, "y", 0);
        double elementWidth = SvgReal(node, "width", 0);
        double elementHeight = SvgReal(node, "height", 0);
        if (objectBox) {
            double boundsLeft = 0;
            double boundsTop = 0;
            double boundsRight = 0;
            double boundsBottom = 0;
            if (SvgGeometryBounds(node, boundsLeft, boundsTop, boundsRight, boundsBottom)) {
                elementX = boundsLeft;
                elementY = boundsTop;
                elementWidth = boundsRight - boundsLeft;
                elementHeight = boundsBottom - boundsTop;
            }
        }
        // Missing x/y/width/height default to -10%, -10%, 120%, 120% resolved
        // against the bounding box (objectBoundingBox) or the viewport.
        auto regionValue = [&objectBox](const std::string& text, double fallback, double reference) {
            if (text.empty()) {
                return fallback * reference;
            }
            // objectBoundingBox lengths are fractions of the box. A percent is
            // that same fraction written with a percent sign.
            if (objectBox || text.find('%') != std::string::npos) {
                double number = std::atof(text.c_str());
                if (text.find('%') != std::string::npos) {
                    number /= 100.0;
                }
                return number * reference;
            }
            return std::atof(text.c_str());
        };
        const double viewportX = space.scaleX != 0 ? space.pixelWidth / space.scaleX : space.pixelWidth;
        const double viewportY = space.scaleY != 0 ? space.pixelHeight / space.scaleY : space.pixelHeight;
        const double referenceX = objectBox ? elementWidth : viewportX;
        const double referenceY = objectBox ? elementHeight : viewportY;
        const double regionX = regionValue(AttributeValue(*mask, "x"), -0.1, referenceX);
        const double regionY = regionValue(AttributeValue(*mask, "y"), -0.1, referenceY);
        const double regionWidth = std::max(0.0, regionValue(AttributeValue(*mask, "width"), 1.2, referenceX));
        const double regionHeight = std::max(0.0, regionValue(AttributeValue(*mask, "height"), 1.2, referenceY));
        content.hasRegion = true;
        content.regionX = SvgX(space, objectBox ? elementX + regionX : regionX);
        content.regionY = SvgY(space, objectBox ? elementY + regionY : regionY);
        content.regionWidth = SvgLength(space, regionWidth, true);
        content.regionHeight = SvgLength(space, regionHeight, false);
        const bool contentBox = ToLowerAscii(AttributeValue(*mask, "maskcontentunits")) == "objectboundingbox";
        BuildSvgMaskPieces(*mask, space, contentBox, elementX, elementY, elementWidth, elementHeight, content);
        return true;
    }

    // The mask value at one pixel: the content is composited source-over, then
    // turned into a coverage by the luminance rule (default) or alpha rule.
    // Shapes outside the mask region contribute nothing.
    double SvgMaskValueAt(const SvgMaskContent& mask, double sampleX, double sampleY) {
        if (mask.hasRegion &&
            (sampleX < mask.regionX || sampleY < mask.regionY || sampleX >= mask.regionX + mask.regionWidth ||
             sampleY >= mask.regionY + mask.regionHeight)) {
            return 0;
        }
        double red = 0;
        double green = 0;
        double blue = 0;
        double alpha = 0;
        for (const SvgMaskPiece& piece : mask.pieces) {
            bool inside = false;
            for (const std::vector<SvgPoint>& contour : piece.contours) {
                if (SvgPointInContour(contour, sampleX, sampleY, piece.evenOdd)) {
                    inside = true;
                    break;
                }
            }
            if (!inside) {
                continue;
            }
            const double sourceAlpha = (piece.color.alpha / 255.0) * piece.opacity;
            const double outAlpha = sourceAlpha + alpha * (1 - sourceAlpha);
            if (!(outAlpha > 0)) {
                alpha = 0;
                red = 0;
                green = 0;
                blue = 0;
                continue;
            }
            const double share = sourceAlpha / outAlpha;
            const double keep = alpha * (1 - sourceAlpha) / outAlpha;
            red = piece.color.red / 255.0 * share + red * keep;
            green = piece.color.green / 255.0 * share + green * keep;
            blue = piece.color.blue / 255.0 * share + blue * keep;
            alpha = outAlpha;
        }
        if (!(alpha > 0)) {
            return 0;
        }
        if (!mask.luminance) {
            return std::min(1.0, alpha);
        }
        auto toLinear = [](double encoded) {
            return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
        };
        const double luminance = 0.2126 * toLinear(red) + 0.7152 * toLinear(green) + 0.0722 * toLinear(blue);
        return std::max(0.0, std::min(1.0, luminance * alpha));
    }

    // The fraction of `color` that survives the active mask at one pixel.
    Color MaskedColor(Color color, const SvgMaskContent& mask, double sampleX, double sampleY) {
        const double value = SvgMaskValueAt(mask, sampleX, sampleY);
        if (value >= 1) {
            return color;
        }
        if (!(value > 0)) {
            color.alpha = 0;
            return color;
        }
        const double scaled = static_cast<double>(color.alpha) * value + 0.5;
        color.alpha = static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, scaled)));
        return color;
    }

    struct AnimationScope {
        struct Saved {
            Node* node = nullptr;
            std::string name;
            std::string value;
            bool added = false;
        };
        std::vector<Saved> saved;

        explicit AnimationScope(Node& node) {
            auto clock = [](const std::string& text) {
                std::string value = ToLowerAscii(text);
                double scale = 1;
                if (value.size() > 2 && value.substr(value.size() - 2) == "ms") {
                    scale = 0.001;
                    value.resize(value.size() - 2);
                } else if (!value.empty() && value.back() == 's') {
                    value.pop_back();
                }
                return std::atof(value.c_str()) * scale;
            };
            auto interpolate = [](const std::string& from, const std::string& to, double t, bool linear = false) {
                Color start{0, 0, 0, 255};
                Color end{0, 0, 0, 255};
                if (ParseColor(from, start) && ParseColor(to, end)) {
                    const auto channel = [t, linear](int left, int right) {
                        if (!linear) {
                            return static_cast<int>(left + (right - left) * t);
                        }
                        auto decode = [](int value) {
                            const double encoded = value / 255.0;
                            return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
                        };
                        const double decoded = decode(left) + (decode(right) - decode(left)) * t;
                        const double encoded = decoded <= 0.0031308 ? 12.92 * decoded : 1.055 * std::pow(decoded, 1.0 / 2.4) - 0.055;
                        return static_cast<int>(std::max(0.0, std::min(255.0, encoded * 255.0 + 0.5)));
                    };
                    char text[8];
                    std::snprintf(text, sizeof(text), "#%02x%02x%02x", channel(start.red, end.red), channel(start.green, end.green),
                        channel(start.blue, end.blue));
                    return std::string(text);
                }
                const double number = std::atof(from.c_str()) + (std::atof(to.c_str()) - std::atof(from.c_str())) * t;
                return std::to_string(static_cast<int>(number + 0.5));
            };
            auto splineMix = [](const std::string& splines, int interval, double mix) {
                std::vector<double> numbers;
                std::size_t index = 0;
                while (index < splines.size()) {
                    while (index < splines.size() && (splines[index] == ' ' || splines[index] == ',' || splines[index] == ';')) {
                        ++index;
                    }
                    if (index >= splines.size()) {
                        break;
                    }
                    char* end = nullptr;
                    const double number = std::strtod(splines.c_str() + index, &end);
                    if (end == splines.c_str() + index) {
                        break;
                    }
                    numbers.push_back(number);
                    index = static_cast<std::size_t>(end - splines.c_str());
                }
                const int base = interval * 4;
                if (base < 0 || base + 3 >= static_cast<int>(numbers.size())) {
                    return mix;
                }
                const double x1 = numbers[static_cast<std::size_t>(base)];
                const double y1 = numbers[static_cast<std::size_t>(base + 1)];
                const double x2 = numbers[static_cast<std::size_t>(base + 2)];
                const double y2 = numbers[static_cast<std::size_t>(base + 3)];
                double t = mix;
                for (int step = 0; step < 8; ++step) {
                    const double u = 1.0 - t;
                    const double curve = 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t;
                    const double slope = 3 * u * u * x1 + 6 * u * t * (x2 - x1) + 3 * t * t * (1.0 - x2);
                    if (std::abs(slope) < 1e-6) {
                        break;
                    }
                    t = std::max(0.0, std::min(1.0, t - (curve - mix) / slope));
                }
                const double u = 1.0 - t;
                return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t;
            };
            for (const std::unique_ptr<Node>& child : node.children) {
                const std::string tag = ToLowerAscii(child->tagName);
                if (tag != "animate" && tag != "set" && tag != "animatetransform") {
                    continue;
                }
                std::string attribute = AttributeValue(*child, "attributename");
                if (tag == "animatetransform" && attribute.empty()) {
                    attribute = "transform";
                }
                if (attribute.empty() || ToLowerAscii(AttributeValue(*child, "attributetype")) == "css") {
                    continue;
                }
                const double duration = std::max(0.0, clock(AttributeValue(*child, "dur")));
                std::vector<double> begins;
                const std::string beginText = AttributeValue(*child, "begin");
                std::size_t beginCursor = 0;
                while (beginCursor < beginText.size()) {
                    const std::size_t semi = beginText.find(';', beginCursor);
                    const std::string piece = beginText.substr(beginCursor, semi == std::string::npos ? std::string::npos : semi - beginCursor);
                    if (!piece.empty()) {
                        begins.push_back(clock(piece));
                    }
                    if (semi == std::string::npos) {
                        break;
                    }
                    beginCursor = semi + 1;
                }
                if (begins.empty()) {
                    begins.push_back(0);
                }
                const std::string restartMode = ToLowerAscii(AttributeValue(*child, "restart"));
                double begin = begins.front();
                for (std::size_t index = 1; index < begins.size(); ++index) {
                    if (begins[index] > gSvgAnimationTime) {
                        break;
                    }
                    if (restartMode == "never") {
                        break;
                    }
                    if (restartMode == "whennotactive" && begins[index] < begin + duration) {
                        continue;
                    }
                    begin = begins[index];
                }
                if (gSvgAnimationTime < begin || duration <= 0) {
                    continue;
                }
                if (!AttributeValue(*child, "end").empty() && gSvgAnimationTime >= clock(AttributeValue(*child, "end"))) {
                    continue;
                }
                double local = gSvgAnimationTime - begin;
                const double elapsed = local;
                const std::string repeat = ToLowerAscii(AttributeValue(*child, "repeatcount"));
                if (repeat == "indefinite") {
                    local = std::fmod(local, duration);
                } else if (!repeat.empty()) {
                    const double cycles = std::atof(repeat.c_str());
                    if (local >= duration * std::max(1.0, cycles)) {
                        continue;
                    }
                    local = std::fmod(local, duration);
                } else {
                    const double repeatDuration = clock(AttributeValue(*child, "repeatdur"));
                    if (repeatDuration > 0) {
                        if (local >= repeatDuration) {
                            continue;
                        }
                        local = std::fmod(local, duration);
                    } else {
                        double activeEnd = duration;
                        const double minimum = clock(AttributeValue(*child, "min"));
                        const double maximum = clock(AttributeValue(*child, "max"));
                        if (maximum > 0) {
                            activeEnd = std::min(activeEnd, maximum);
                        }
                        if (minimum > 0) {
                            activeEnd = std::max(activeEnd, minimum);
                        }
                        if (local >= activeEnd) {
                            continue;
                        }
                        if (local > duration) {
                            local = duration;
                        }
                    }
                }
                const std::string calcMode = ToLowerAscii(AttributeValue(*child, "calcmode"));
                double progress = calcMode == "discrete" && local / duration < 0.5 ? 0 : (calcMode == "discrete" ? 1 : local / duration);
                if (calcMode == "spline") {
                    progress = splineMix(AttributeValue(*child, "keysplines"), 0, local / duration);
                }
                std::string value;
                if (tag == "animatetransform") {
                    const std::string type = ToLowerAscii(AttributeValue(*child, "type"));
                    auto component = [](const std::string& text, int which) {
                        std::size_t index = 0;
                        double number = 0;
                        for (int count = 0; count <= which; ++count) {
                            while (index < text.size() && (text[index] == ' ' || text[index] == ',')) {
                                ++index;
                            }
                            if (index >= text.size()) {
                                return number;
                            }
                            char* end = nullptr;
                            number = std::strtod(text.c_str() + index, &end);
                            if (end == text.c_str() + index) {
                                return number;
                            }
                            index = static_cast<std::size_t>(end - text.c_str());
                        }
                        return number;
                    };
                    const std::string from = AttributeValue(*child, "from");
                    const std::string to = AttributeValue(*child, "to");
                    const double mix = local / duration;
                    const double first = component(from, 0) + (component(to, 0) - component(from, 0)) * mix;
                    double second = component(from, 1) + (component(to, 1) - component(from, 1)) * mix;
                    const double third = component(from, 2) + (component(to, 2) - component(from, 2)) * mix;
                    char formatted[96];
                    if (type == "scale") {
                        if (from.find_first_of(" ,") == std::string::npos) {
                            second = first;
                        }
                        std::snprintf(formatted, sizeof(formatted), "scale(%g,%g)", first, second);
                    } else if (type == "rotate") {
                        std::snprintf(formatted, sizeof(formatted), "rotate(%g %g %g)", first, second, third);
                    } else {
                        std::snprintf(formatted, sizeof(formatted), "translate(%g,%g)", first, second);
                    }
                    value = formatted;
                } else if (tag == "set") {
                    value = AttributeValue(*child, "to");
                } else {
                    const std::string values = AttributeValue(*child, "values");
                    if (!values.empty()) {
                        std::vector<std::string> stops;
                        std::size_t start = 0;
                        for (std::size_t index = 0; index <= values.size(); ++index) {
                            if (index == values.size() || values[index] == ';') {
                                if (index > start) {
                                    stops.push_back(values.substr(start, index - start));
                                }
                                start = index + 1;
                            }
                        }
                        if (stops.size() == 1) {
                            value = stops.front();
                        } else if (!stops.empty()) {
                            std::vector<double> keyTimes;
                            const std::string keyText = AttributeValue(*child, "keytimes");
                            std::size_t keyIndex = 0;
                            while (keyIndex < keyText.size()) {
                                while (keyIndex < keyText.size() && (keyText[keyIndex] == ' ' || keyText[keyIndex] == ';' || keyText[keyIndex] == ',')) {
                                    ++keyIndex;
                                }
                                if (keyIndex >= keyText.size()) {
                                    break;
                                }
                                char* end = nullptr;
                                const double keyTime = std::strtod(keyText.c_str() + keyIndex, &end);
                                if (end == keyText.c_str() + keyIndex) {
                                    break;
                                }
                                keyTimes.push_back(keyTime);
                                keyIndex = static_cast<std::size_t>(end - keyText.c_str());
                            }
                            if (keyTimes.size() == stops.size()) {
                                const double paced = local / duration;
                                if (paced >= keyTimes.back()) {
                                    value = stops.back();
                                } else {
                                    std::size_t index = 0;
                                    while (index + 1 < keyTimes.size() && paced > keyTimes[index + 1]) {
                                        ++index;
                                    }
                                    const double span = keyTimes[index + 1] - keyTimes[index];
                                    double mix = span <= 0 ? 0 : (paced - keyTimes[index]) / span;
                                    if (calcMode == "spline") {
                                        mix = splineMix(AttributeValue(*child, "keysplines"), static_cast<int>(index), mix);
                                    }
                                    value = interpolate(stops[index], stops[index + 1], mix);
                                }
                            } else {
                                const double scaled = local / duration * static_cast<double>(stops.size() - 1);
                                const int index = std::min(static_cast<int>(stops.size()) - 2, static_cast<int>(scaled));
                                double mix = scaled - index;
                                if (calcMode == "spline") {
                                    mix = splineMix(AttributeValue(*child, "keysplines"), index, mix);
                                }
                                value = interpolate(stops[static_cast<std::size_t>(index)], stops[static_cast<std::size_t>(index + 1)], mix);
                            }
                        }
                    } else {
                        std::string from = AttributeValue(*child, "from");
                        std::string to = AttributeValue(*child, "to");
                        std::string base = "0";
                        for (const Attribute& attributeValue : node.attributes) {
                            if (ToLowerAscii(attributeValue.name) == ToLowerAscii(attribute)) {
                                base = attributeValue.value;
                                break;
                            }
                        }
                        if (from.empty() && !AttributeValue(*child, "by").empty()) {
                            from = base;
                            to = std::to_string(static_cast<int>(std::atof(from.c_str()) + std::atof(AttributeValue(*child, "by").c_str()) + 0.5));
                        }
                        value = interpolate(from, to, progress, ToLowerAscii(AttributeValue(*child, "color-interpolation")) == "linearrgb");
                        if (ToLowerAscii(AttributeValue(*child, "accumulate")) == "sum" && duration > 0) {
                            const int finishedCycles = static_cast<int>(elapsed / duration);
                            const double delta = std::atof(to.c_str()) - std::atof(from.c_str());
                            value = std::to_string(static_cast<int>(std::atof(value.c_str()) + finishedCycles * delta + 0.5));
                        }
                        if (ToLowerAscii(AttributeValue(*child, "additive")) == "sum") {
                            value = std::to_string(static_cast<int>(std::atof(base.c_str()) + std::atof(value.c_str()) + 0.5));
                        }
                    }
                }
                if (value.empty()) {
                    continue;
                }
                bool found = false;
                for (Attribute& attributeValue : node.attributes) {
                    if (ToLowerAscii(attributeValue.name) == ToLowerAscii(attribute)) {
                        saved.push_back(Saved{&node, attributeValue.name, attributeValue.value, false});
                        attributeValue.value = value;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    Attribute created;
                    created.name = ToLowerAscii(attribute);
                    created.value = value;
                    node.attributes.push_back(created);
                    saved.push_back(Saved{&node, created.name, "", true});
                }
            }
        }

        ~AnimationScope() {
            for (std::size_t index = saved.size(); index > 0; --index) {
                const Saved& item = saved[index - 1];
                if (item.node == nullptr) {
                    continue;
                }
                if (item.added) {
                    for (std::size_t attributeIndex = 0; attributeIndex < item.node->attributes.size(); ++attributeIndex) {
                        if (ToLowerAscii(item.node->attributes[attributeIndex].name) == ToLowerAscii(item.name)) {
                            item.node->attributes.erase(item.node->attributes.begin() + static_cast<std::ptrdiff_t>(attributeIndex));
                            break;
                        }
                    }
                } else {
                    for (Attribute& attributeValue : item.node->attributes) {
                        if (ToLowerAscii(attributeValue.name) == ToLowerAscii(item.name)) {
                            attributeValue.value = item.value;
                            break;
                        }
                    }
                }
            }
        }
    };

    struct SvgFilter {
        bool flood = false;
        int dx = 0;
        int dy = 0;
        int blur = 0;
        double blurSigma = 0;
        bool dropShadow = false;
        int shadowDx = 0;
        int shadowDy = 0;
        int dilate = 0;
        int erode = 0;
        bool saturate = false;
        double saturateAmount = 1;
        bool colorMatrix = false;
        int colorMatrixKind = 0;
        double hueAngle = 0;
        double matrix[20] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
        bool transfer = false;
        bool linearFilters = false;
        double slope[4] = {1, 1, 1, 1};
        double intercept[4] = {0, 0, 0, 0};
        bool transferChannel[4] = {false, false, false, false};
        bool gamma[4] = {false, false, false, false};
        double exponent[4] = {1, 1, 1, 1};
        double amplitude[4] = {1, 1, 1, 1};
        double gammaOffset[4] = {0, 0, 0, 0};
        int tableCount[4] = {0, 0, 0, 0};
        double tableValue[4][8] = {};
        bool blend = false;
        int blendMode = 0;
        Color blendColor{255, 255, 255, 255};
        bool arithmetic = false;
        double k1 = 0;
        double k2 = 0;
        double k3 = 0;
        double k4 = 0;
        bool tile = false;
        bool convolve = false;
        bool convolveSample = false;
        bool convolvePreserveAlpha = true;
        double convolveGain = 1;
        double convolveBias = 0;
        int kernelOrderX = 1;
        int kernelOrderY = 1;
        int kernelTargetX = 0;
        int kernelTargetY = 0;
        int kernelUnitX = 1;
        int kernelUnitY = 1;
        int kernelCount = 0;
        int edgeMode = 0;
        double kernel[25] = {};
        bool imageReplace = false;
        Color imageColor{0, 0, 0, 255};
        bool turbulence = false;
        bool turbulenceAbs = false;
        int turbulenceSeed = 0;
        int turbulenceOctaves = 1;
        bool turbulenceStitch = false;
        double turbulenceFrequency = 0;
        bool diffuse = false;
        bool distant = false;
        double diffuseAmount = 1;
        double azimuth = 0;
        double elevation = 0;
        double surfaceScale = 1;
        double specularExponent = 1;
        Color lightColor{255, 255, 255, 255};
        bool pointLight = false;
        double pointX = 0;
        double pointY = 0;
        double pointZ = 1;
        bool spot = false;
        double aimX = 0;
        double aimY = 0;
        double aimZ = 0;
        double spotLimit = -1;
        bool mergeSource = false;
        bool mergeFlood = false;
        Color floodColor{0, 0, 0, 0};
        bool region = false;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    void MapFilterRegion(const Node& filter, const Node& target, const Node& primitive, const SvgSpace& space, SvgFilter& effect) {
        const bool objectBox = ToLowerAscii(AttributeValue(filter, "filterunits")) == "objectboundingbox";
        if (!objectBox) {
            effect.x = SvgX(space, SvgNumber(primitive, "x", 0));
            effect.y = SvgY(space, SvgNumber(primitive, "y", 0));
            effect.width = SvgLength(space, SvgNumber(primitive, "width", 0), true);
            effect.height = SvgLength(space, SvgNumber(primitive, "height", 0), false);
            return;
        }
        const double elementX = SvgNumber(target, "x", 0);
        const double elementY = SvgNumber(target, "y", 0);
        const double elementWidth = SvgNumber(target, "width", 0);
        const double elementHeight = SvgNumber(target, "height", 0);
        effect.x = SvgX(space, elementX + SvgOffset(AttributeValue(primitive, "x")) * elementWidth);
        effect.y = SvgY(space, elementY + SvgOffset(AttributeValue(primitive, "y")) * elementHeight);
        effect.width = std::max(0, SvgLength(space, SvgOffset(AttributeValue(primitive, "width")) * elementWidth, true));
        effect.height = std::max(0, SvgLength(space, SvgOffset(AttributeValue(primitive, "height")) * elementHeight, false));
    }

    void ReadSvgLight(const SvgSpace& space, Node& lighting, SvgFilter& effect, double constant) {
        for (const std::unique_ptr<Node>& lamp : lighting.children) {
            const std::string lampTag = ToLowerAscii(lamp->tagName);
            if (lampTag != "fepointlight" && lampTag != "fespotlight") {
                continue;
            }
            effect.pointLight = true;
            effect.spot = lampTag == "fespotlight";
            effect.pointX = SvgX(space, std::atof(AttributeValue(*lamp, "x").c_str()));
            effect.pointY = SvgY(space, std::atof(AttributeValue(*lamp, "y").c_str()));
            effect.pointZ = std::atof(AttributeValue(*lamp, "z").c_str()) * std::max(space.scaleX, space.scaleY);
            effect.diffuseAmount = constant;
            if (effect.spot) {
                effect.aimX = SvgX(space, std::atof(AttributeValue(*lamp, "pointsatx").c_str()));
                effect.aimY = SvgY(space, std::atof(AttributeValue(*lamp, "pointsaty").c_str()));
                effect.aimZ = std::atof(AttributeValue(*lamp, "pointsatz").c_str()) * std::max(space.scaleX, space.scaleY);
                const double degrees = AttributeValue(*lamp, "limitingconeangle").empty() ? 90 : std::atof(AttributeValue(*lamp, "limitingconeangle").c_str());
                effect.spotLimit = std::cos(degrees * std::acos(-1.0) / 180.0);
            }
        }
    }

    bool ResolveFilter(Node& node, const SvgSpace& space, SvgFilter& effect) {
        std::string reference = SvgProperty(node, "filter");
        if (reference.empty()) {
            reference = space.filterUrl;
        }
        const std::string id = SvgPaintId(reference);
        if (id.empty()) {
            return false;
        }
        Node* root = &node;
        while (root->parent != nullptr) {
            root = root->parent;
        }
        Node* filter = FindSvgById(*root, id);
        if (filter == nullptr) {
            return false;
        }
        bool found = false;
        Color lastColor{255, 255, 255, 255};
        std::string lastName;
        for (const std::unique_ptr<Node>& child : filter->children) {
            const std::string tag = ToLowerAscii(child->tagName);
            if (tag == "feflood") {
                effect = SvgFilter();
                effect.flood = true;
                effect.floodColor = Color{0, 0, 0, 255};
                const std::string color = AttributeValue(*child, "flood-color");
                if (!color.empty()) {
                    ParseColor(color, effect.floodColor);
                }
                if (!AttributeValue(*child, "flood-opacity").empty()) {
                    effect.floodColor.alpha = static_cast<std::uint8_t>(
                        std::max(0.0, std::min(255.0, std::atof(AttributeValue(*child, "flood-opacity").c_str()) * 255.0)));
                }
                if (!AttributeValue(*child, "width").empty() && !AttributeValue(*child, "height").empty()) {
                    effect.region = true;
                    MapFilterRegion(*filter, node, *child, space, effect);
                }
                lastColor = effect.floodColor;
                lastName = AttributeValue(*child, "result");
                found = true;
            } else if (tag == "feoffset") {
                effect = SvgFilter();
                if (ToLowerAscii(AttributeValue(*filter, "primitiveunits")) == "objectboundingbox") {
                    effect.dx = static_cast<int>(SvgOffset(AttributeValue(*child, "dx")) * SvgNumber(node, "width", 0) * space.scaleX);
                    effect.dy = static_cast<int>(SvgOffset(AttributeValue(*child, "dy")) * SvgNumber(node, "height", 0) * space.scaleY);
                } else {
                    effect.dx = static_cast<int>(std::atof(AttributeValue(*child, "dx").c_str()) * space.scaleX);
                    effect.dy = static_cast<int>(std::atof(AttributeValue(*child, "dy").c_str()) * space.scaleY);
                }
                found = true;
                const std::string input = ToLowerAscii(AttributeValue(*child, "in"));
                if (!input.empty() && input != "sourcegraphic" && input == ToLowerAscii(lastName)) {
                    effect.imageReplace = true;
                    effect.imageColor = lastColor;
                }
            } else if (tag == "fegaussianblur") {
                effect = SvgFilter();
                const double deviation = std::atof(AttributeValue(*child, "stddeviation").c_str()) * std::max(space.scaleX, space.scaleY);
                effect.blurSigma = deviation > 0 ? deviation : 0;
                effect.blur = deviation > 0 ? std::max(1, static_cast<int>(deviation + 0.5)) : 0;
                found = true;
            } else if (tag == "fedropshadow") {
                effect = SvgFilter();
                effect.dropShadow = true;
                const double shiftX = AttributeValue(*child, "dx").empty() ? 2 : std::atof(AttributeValue(*child, "dx").c_str());
                const double shiftY = AttributeValue(*child, "dy").empty() ? 2 : std::atof(AttributeValue(*child, "dy").c_str());
                effect.shadowDx = shiftX > -100000 && shiftX < 100000 ? static_cast<int>(shiftX * space.scaleX) : 0;
                effect.shadowDy = shiftY > -100000 && shiftY < 100000 ? static_cast<int>(shiftY * space.scaleY) : 0;
                const double deviation = AttributeValue(*child, "stddeviation").empty() ? 2 : std::atof(AttributeValue(*child, "stddeviation").c_str());
                const double scaledDeviation = deviation * std::max(space.scaleX, space.scaleY);
                if (scaledDeviation > 0 && scaledDeviation < 64) {
                    effect.blur = static_cast<int>(scaledDeviation + 0.5);
                    effect.blurSigma = scaledDeviation;
                } else if (scaledDeviation >= 64) {
                    effect.blur = 64;
                    effect.blurSigma = 64;
                }
                effect.floodColor = Color{0, 0, 0, 255};
                const std::string shadowColor = AttributeValue(*child, "flood-color");
                if (!shadowColor.empty()) {
                    ParseColor(shadowColor, effect.floodColor);
                }
                if (!AttributeValue(*child, "flood-opacity").empty()) {
                    const double opacity = std::atof(AttributeValue(*child, "flood-opacity").c_str());
                    int alpha = 255;
                    if (!(opacity > 0)) {
                        alpha = 0;
                    } else if (opacity < 1) {
                        alpha = static_cast<int>(opacity * 255.0);
                    }
                    effect.floodColor.alpha = static_cast<std::uint8_t>(alpha);
                }
                found = true;
            } else if (tag == "femorphology") {
                effect = SvgFilter();
                const int radius = std::max(1, static_cast<int>(std::atof(AttributeValue(*child, "radius").c_str()) * std::max(space.scaleX, space.scaleY)));
                if (ToLowerAscii(AttributeValue(*child, "operator")) == "erode") {
                    effect.erode = radius;
                } else {
                    effect.dilate = radius;
                }
                found = true;
            } else if (tag == "fecolormatrix") {
                effect = SvgFilter();
                const std::string matrixType = ToLowerAscii(AttributeValue(*child, "type"));
                if (matrixType == "saturate") {
                    effect.saturate = true;
                    effect.saturateAmount = std::max(0.0, std::min(1.0, std::atof(AttributeValue(*child, "values").c_str())));
                } else if (matrixType == "huerotate") {
                    effect.colorMatrix = true;
                    effect.colorMatrixKind = 2;
                    effect.hueAngle = std::atof(AttributeValue(*child, "values").c_str());
                } else if (matrixType == "luminancetoalpha") {
                    effect.colorMatrix = true;
                    effect.colorMatrixKind = 3;
                } else if (matrixType == "matrix" || (matrixType.empty() && !AttributeValue(*child, "values").empty())) {
                    effect.colorMatrix = true;
                    effect.colorMatrixKind = 1;
                    std::size_t cursor = 0;
                    int slot = 0;
                    double sample = 0;
                    const std::string samples = AttributeValue(*child, "values");
                    while (slot < 20 && ReadSvgNumber(samples, cursor, sample)) {
                        effect.matrix[slot] = sample;
                        ++slot;
                    }
                }
                found = true;
            } else if (tag == "fecomponenttransfer") {
                effect = SvgFilter();
                effect.transfer = true;
                effect.linearFilters = ToLowerAscii(AttributeValue(*filter, "color-interpolation-filters")) == "linearrgb";
                for (const std::unique_ptr<Node>& channel : child->children) {
                    const std::string channelTag = ToLowerAscii(channel->tagName);
                    int index = channelTag == "fefuncr" ? 0 : channelTag == "fefuncg" ? 1 : channelTag == "fefuncb" ? 2 : channelTag == "fefunca" ? 3 : -1;
                    if (index < 0) {
                        continue;
                    }
                    effect.transferChannel[index] = true;
                    const std::string transferType = ToLowerAscii(AttributeValue(*channel, "type"));
                    if (transferType == "linear") {
                        if (!AttributeValue(*channel, "slope").empty()) {
                            effect.slope[index] = std::atof(AttributeValue(*channel, "slope").c_str());
                        }
                        if (!AttributeValue(*channel, "intercept").empty()) {
                            effect.intercept[index] = std::atof(AttributeValue(*channel, "intercept").c_str());
                        }
                    } else if (transferType == "gamma") {
                        effect.gamma[index] = true;
                        if (!AttributeValue(*channel, "exponent").empty()) {
                            effect.exponent[index] = std::atof(AttributeValue(*channel, "exponent").c_str());
                        }
                        if (!AttributeValue(*channel, "amplitude").empty()) {
                            effect.amplitude[index] = std::atof(AttributeValue(*channel, "amplitude").c_str());
                        }
                        if (!AttributeValue(*channel, "offset").empty()) {
                            effect.gammaOffset[index] = std::atof(AttributeValue(*channel, "offset").c_str());
                        }
                    } else if (transferType == "table" || transferType == "discrete") {
                        std::size_t cursor = 0;
                        double sample = 0;
                        const std::string samples = AttributeValue(*channel, "tablevalues");
                        while (effect.tableCount[index] < 8 && ReadSvgNumber(samples, cursor, sample)) {
                            effect.tableValue[index][effect.tableCount[index]] = sample;
                            ++effect.tableCount[index];
                        }
                    }
                }
                found = true;
            } else if (tag == "feblend") {
                effect = SvgFilter();
                effect.blend = true;
                effect.blendColor = lastColor;
                const std::string secondInput = ToLowerAscii(AttributeValue(*child, "in2"));
                if (!secondInput.empty() && secondInput != "sourcegraphic" && secondInput != ToLowerAscii(lastName)) {
                    effect.blend = false;
                }
                const std::string mode = ToLowerAscii(AttributeValue(*child, "mode"));
                if (mode == "screen") {
                    effect.blendMode = 2;
                } else if (mode == "darken") {
                    effect.blendMode = 3;
                } else if (mode == "lighten") {
                    effect.blendMode = 4;
                } else {
                    effect.blendMode = 1;
                }
                found = true;
            } else if (tag == "fecomposite") {
                effect = SvgFilter();
                effect.arithmetic = ToLowerAscii(AttributeValue(*child, "operator")) == "arithmetic";
                effect.blendColor = lastColor;
                effect.k1 = std::atof(AttributeValue(*child, "k1").c_str());
                effect.k2 = AttributeValue(*child, "k2").empty() ? 0 : std::atof(AttributeValue(*child, "k2").c_str());
                effect.k3 = std::atof(AttributeValue(*child, "k3").c_str());
                effect.k4 = std::atof(AttributeValue(*child, "k4").c_str());
                found = effect.arithmetic;
            } else if (tag == "fetile") {
                effect = SvgFilter();
                effect.tile = true;
                if (!AttributeValue(*child, "width").empty() && !AttributeValue(*child, "height").empty()) {
                    effect.region = true;
                    MapFilterRegion(*filter, node, *child, space, effect);
                }
                found = true;
            } else if (tag == "femerge") {
                effect.mergeSource = false;
                effect.mergeFlood = false;
                for (const std::unique_ptr<Node>& mergeNode : child->children) {
                    if (ToLowerAscii(mergeNode->tagName) != "femergenode") {
                        continue;
                    }
                    const std::string input = ToLowerAscii(AttributeValue(*mergeNode, "in"));
                    if (input.empty() || input == "sourcegraphic") {
                        effect.mergeSource = true;
                    } else if (!lastName.empty() && input == ToLowerAscii(lastName)) {
                        effect.mergeFlood = true;
                    }
                }
                effect.flood = effect.mergeFlood && !effect.mergeSource;
                found = true;
            } else if (tag == "feconvolvematrix") {
                effect = SvgFilter();
                effect.convolve = true;
                double sum = 0;
                std::size_t index = 0;
                const std::string matrix = AttributeValue(*child, "kernelmatrix");
                double value = 0;
                while (ReadSvgNumber(matrix, index, value)) {
                    if (effect.kernelCount < 25) {
                        effect.kernel[effect.kernelCount] = value;
                        ++effect.kernelCount;
                    }
                    sum += value;
                }
                const double divisor = AttributeValue(*child, "divisor").empty() ? (sum == 0 ? 1 : sum) : std::atof(AttributeValue(*child, "divisor").c_str());
                effect.convolveGain = std::abs(divisor) < 0.000001 ? 0 : sum / divisor;
                effect.convolveBias = std::atof(AttributeValue(*child, "bias").c_str());
                effect.convolvePreserveAlpha = ToLowerAscii(AttributeValue(*child, "preservealpha")) != "false";
                const std::string edge = ToLowerAscii(AttributeValue(*child, "edgemode"));
                if (edge == "wrap") {
                    effect.edgeMode = 1;
                } else if (edge == "none") {
                    effect.edgeMode = 2;
                }
                const std::string order = AttributeValue(*child, "order");
                if (!order.empty()) {
                    std::size_t cursor = 0;
                    double across = 1;
                    double down = 1;
                    ReadSvgNumber(order, cursor, across);
                    if (!ReadSvgNumber(order, cursor, down)) {
                        down = across;
                    }
                    effect.kernelOrderX = std::max(1, std::min(5, static_cast<int>(across)));
                    effect.kernelOrderY = std::max(1, std::min(5, static_cast<int>(down)));
                } else if (effect.kernelCount == 1) {
                    effect.kernelOrderX = 1;
                    effect.kernelOrderY = 1;
                } else {
                    const int side = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(effect.kernelCount)) + 0.5));
                    effect.kernelOrderX = std::min(5, side);
                    effect.kernelOrderY = std::min(5, side);
                }
                effect.kernelTargetX = AttributeValue(*child, "targetx").empty() ? effect.kernelOrderX / 2 : std::atoi(AttributeValue(*child, "targetx").c_str());
                effect.kernelTargetY = AttributeValue(*child, "targety").empty() ? effect.kernelOrderY / 2 : std::atoi(AttributeValue(*child, "targety").c_str());
                const std::string unit = AttributeValue(*child, "kernelunitlength");
                if (!unit.empty()) {
                    std::size_t cursor = 0;
                    double across = 1;
                    double down = 1;
                    ReadSvgNumber(unit, cursor, across);
                    if (!ReadSvgNumber(unit, cursor, down)) {
                        down = across;
                    }
                    effect.kernelUnitX = std::max(1, static_cast<int>(across));
                    effect.kernelUnitY = std::max(1, static_cast<int>(down));
                }
                effect.convolveSample = effect.edgeMode != 0 || !order.empty() || !AttributeValue(*child, "targetx").empty() || !AttributeValue(*child, "targety").empty() || !unit.empty();
                found = true;
            } else if (tag == "fedisplacementmap") {
                effect = SvgFilter();
                const double scale = std::atof(AttributeValue(*child, "scale").c_str()) * std::max(space.scaleX, space.scaleY);
                const std::string xChannel = ToLowerAscii(AttributeValue(*child, "xchannelselector"));
                const std::string yChannel = ToLowerAscii(AttributeValue(*child, "ychannelselector"));
                auto channel = [&](const std::string& name) {
                    if (name == "r") {
                        return lastColor.red;
                    }
                    if (name == "g") {
                        return lastColor.green;
                    }
                    if (name == "b") {
                        return lastColor.blue;
                    }
                    return lastColor.alpha;
                };
                effect.dx = static_cast<int>(scale * (channel(xChannel.empty() ? "a" : xChannel) / 255.0 - 0.5));
                effect.dy = static_cast<int>(scale * (channel(yChannel.empty() ? "a" : yChannel) / 255.0 - 0.5));
                found = true;
            } else if (tag == "feimage") {
                effect = SvgFilter();
                std::string href = AttributeValue(*child, "href");
                if (href.empty()) {
                    href = AttributeValue(*child, "xlink:href");
                }
                const std::string imageId = SvgPaintId(href);
                Node* root = filter;
                while (root->parent != nullptr) {
                    root = root->parent;
                }
                Node* source = imageId.empty() ? nullptr : FindSvgById(*root, imageId);
                if (source != nullptr) {
                    const std::string paint = ToLowerAscii(AttributeValue(*source, "fill"));
                    if (!paint.empty() && paint != "none" && paint.find("url(") == std::string::npos) {
                        ParseColor(paint, effect.imageColor);
                        effect.imageReplace = true;
                        found = true;
                    }
                }
            } else if (tag == "feturbulence") {
                effect = SvgFilter();
                effect.turbulence = true;
                effect.turbulenceAbs = ToLowerAscii(AttributeValue(*child, "type")) == "turbulence";
                effect.turbulenceSeed = static_cast<int>(std::atof(AttributeValue(*child, "seed").c_str()));
                effect.turbulenceOctaves = std::max(1, AttributeValue(*child, "numoctaves").empty() ? 1 : std::atoi(AttributeValue(*child, "numoctaves").c_str()));
                effect.turbulenceStitch = ToLowerAscii(AttributeValue(*child, "stitchtiles")) == "stitch";
                effect.turbulenceFrequency = std::max(0.01, std::atof(AttributeValue(*child, "basefrequency").c_str()));
                found = true;
            } else if (tag == "fediffuselighting") {
                effect = SvgFilter();
                effect.diffuse = true;
                const std::string light = AttributeValue(*child, "lighting-color");
                if (!light.empty()) {
                    ParseColor(ToLowerAscii(light), effect.lightColor);
                }
                const double constant = AttributeValue(*child, "diffuseconstant").empty() ? 1 : std::atof(AttributeValue(*child, "diffuseconstant").c_str());
                double elevation = 0;
                double azimuth = 0;
                bool distant = false;
                for (const std::unique_ptr<Node>& lamp : child->children) {
                    if (ToLowerAscii(lamp->tagName) == "fedistantlight") {
                        distant = true;
                        elevation = std::atof(AttributeValue(*lamp, "elevation").c_str());
                        azimuth = std::atof(AttributeValue(*lamp, "azimuth").c_str());
                    }
                }
                effect.surfaceScale = AttributeValue(*child, "surfacescale").empty() ? 1 : std::atof(AttributeValue(*child, "surfacescale").c_str());
                effect.distant = distant;
                effect.azimuth = azimuth;
                effect.elevation = elevation;
                effect.specularExponent = 1;
                effect.diffuseAmount = distant ? constant : std::max(0.0, std::sin(elevation * std::acos(-1.0) / 180.0)) * constant;
                ReadSvgLight(space, *child, effect, constant);
                found = true;
            } else if (tag == "fespecularlighting") {
                effect = SvgFilter();
                effect.diffuse = true;
                const std::string light = AttributeValue(*child, "lighting-color");
                if (!light.empty()) {
                    ParseColor(ToLowerAscii(light), effect.lightColor);
                }
                const double constant = AttributeValue(*child, "specularconstant").empty() ? 1 : std::atof(AttributeValue(*child, "specularconstant").c_str());
                const double exponent = AttributeValue(*child, "specularexponent").empty() ? 1 : std::atof(AttributeValue(*child, "specularexponent").c_str());
                double elevation = 0;
                double azimuth = 0;
                bool distant = false;
                for (const std::unique_ptr<Node>& lamp : child->children) {
                    if (ToLowerAscii(lamp->tagName) == "fedistantlight") {
                        distant = true;
                        elevation = std::atof(AttributeValue(*lamp, "elevation").c_str());
                        azimuth = std::atof(AttributeValue(*lamp, "azimuth").c_str());
                    }
                }
                effect.surfaceScale = AttributeValue(*child, "surfacescale").empty() ? 1 : std::atof(AttributeValue(*child, "surfacescale").c_str());
                effect.distant = distant;
                effect.azimuth = azimuth;
                effect.elevation = elevation;
                effect.specularExponent = std::max(1.0, exponent);
                const double facing = std::max(0.0, std::sin(elevation * std::acos(-1.0) / 180.0));
                effect.diffuseAmount = distant ? constant : constant * std::pow(facing, effect.specularExponent);
                ReadSvgLight(space, *child, effect, constant);
                found = true;
            }
        }
        return found;
    }

    void ApplySvgMotion(Node& node, SvgSpace& space) {
        for (const std::unique_ptr<Node>& child : node.children) {
            if (ToLowerAscii(child->tagName) != "animatemotion") {
                continue;
            }
            const double begin = std::atof(AttributeValue(*child, "begin").c_str());
            std::string durationText = ToLowerAscii(AttributeValue(*child, "dur"));
            double scale = 1;
            if (durationText.size() > 2 && durationText.substr(durationText.size() - 2) == "ms") {
                scale = 0.001;
                durationText.resize(durationText.size() - 2);
            } else if (!durationText.empty() && durationText.back() == 's') {
                durationText.pop_back();
            }
            const double duration = std::atof(durationText.c_str()) * scale;
            if (duration <= 0 || gSvgAnimationTime < begin) {
                continue;
            }
            double local = gSvgAnimationTime - begin;
            const std::string repeat = ToLowerAscii(AttributeValue(*child, "repeatcount"));
            if (repeat == "indefinite") {
                local = std::fmod(local, duration);
            } else if (local >= duration) {
                continue;
            }
            std::string pathData = AttributeValue(*child, "path");
            if (pathData.empty()) {
                for (const std::unique_ptr<Node>& motionChild : child->children) {
                    if (ToLowerAscii(motionChild->tagName) != "mpath") {
                        continue;
                    }
                    std::string href = AttributeValue(*motionChild, "href");
                    if (href.empty()) {
                        href = AttributeValue(*motionChild, "xlink:href");
                    }
                    const std::string id = SvgPaintId(href);
                    Node* root = &node;
                    while (root->parent != nullptr) {
                        root = root->parent;
                    }
                    if (!id.empty()) {
                        if (Node* path = FindSvgById(*root, id)) {
                            pathData = SvgPathData(*path);
                        }
                    }
                }
            }
            const std::vector<SvgPoint> course = MapSvgPoints(StraightPath(pathData, SvgDeviceScale(space)), space);
            if (course.size() < 2) {
                continue;
            }
            double length = 0;
            for (std::size_t index = 1; index < course.size(); ++index) {
                const double dx = course[index].x - course[index - 1].x;
                const double dy = course[index].y - course[index - 1].y;
                length += std::sqrt(dx * dx + dy * dy);
            }
            double startX = 0;
            double startY = 0;
            double atX = 0;
            double atY = 0;
            double traveled = length * (local / duration);
            const std::string keyPoints = AttributeValue(*child, "keypoints");
            if (!keyPoints.empty()) {
                auto readList = [](const std::string& text, std::vector<double>& out) {
                    std::size_t index = 0;
                    while (index < text.size()) {
                        while (index < text.size() && (text[index] == ' ' || text[index] == ';' || text[index] == ',')) {
                            ++index;
                        }
                        if (index >= text.size()) {
                            break;
                        }
                        char* end = nullptr;
                        const double number = std::strtod(text.c_str() + index, &end);
                        if (end == text.c_str() + index) {
                            break;
                        }
                        out.push_back(number);
                        index = static_cast<std::size_t>(end - text.c_str());
                    }
                };
                std::vector<double> marks;
                std::vector<double> times;
                readList(keyPoints, marks);
                readList(AttributeValue(*child, "keytimes"), times);
                if (marks.size() >= 2 && marks.size() == times.size()) {
                    const double paced = local / duration;
                    if (paced >= times.back()) {
                        traveled = length * marks.back();
                    } else {
                        std::size_t index = 0;
                        while (index + 1 < times.size() && paced > times[index + 1]) {
                            ++index;
                        }
                        const double span = times[index + 1] - times[index];
                        const double mix = span <= 0 ? 0 : (paced - times[index]) / span;
                        const double point = marks[index] + (marks[index + 1] - marks[index]) * mix;
                        traveled = length * point;
                    }
                }
            }
            PointAlong(course, 0, startX, startY);
            PointAlong(course, traveled, atX, atY);
            space.originX += atX - startX;
            space.originY += atY - startY;
            const std::string spin = ToLowerAscii(AttributeValue(*child, "rotate"));
            if (spin == "auto" || spin == "auto-reverse") {
                double aheadX = atX;
                double aheadY = atY;
                PointAlong(course, std::min(length, traveled + 1), aheadX, aheadY);
                double angle = std::atan2(aheadY - atY, aheadX - atX) * 180.0 / std::acos(-1.0);
                if (spin == "auto-reverse") {
                    angle += 180;
                }
                const double centerUserX = space.scaleX != 0 ? (atX - space.originX) / space.scaleX + space.minX : 0;
                const double centerUserY = space.scaleY != 0 ? (atY - space.originY) / space.scaleY + space.minY : 0;
                space.matrix = SvgMatrixMultiply(space.matrix, SvgMatrixRotate(angle, centerUserX, centerUserY));
            }
        }
    }

    void PaintSvgTree(Node& node, SvgSpace space, bool fixed, bool referenced = false) {
        const std::string tag = ToLowerAscii(node.tagName);
        if (ToLowerAscii(SvgProperty(node, "display")) == "none") {
            return;
        }
        if (tag == "title" || tag == "desc" || tag == "metadata") {
            return;
        }
        if (tag == "marker" && !referenced) {
            return;
        }
        if (tag == "pattern" || tag == "view" || tag == "animate" || tag == "set" || tag == "animatetransform" || tag == "animatemotion" ||
            tag == "mpath") {
            return;
        }
        AnimationScope animation(node);
        if (!referenced && (tag == "defs" || tag == "symbol" || tag == "clippath" || tag == "mask" || tag == "filter")) {
            return;
        }
        double originX = 0;
        double originY = 0;
        std::size_t originIndex = 0;
        const std::string origin = SvgProperty(node, "transform-origin");
        const bool hasOrigin = !origin.empty() && ReadSvgNumber(origin, originIndex, originX);
        if (hasOrigin) {
            ReadSvgNumber(origin, originIndex, originY);
        }
        // transform and transform-origin are presentation attributes, so the
        // stylesheet version overrides them exactly like any other property.
        ApplySvgTransform(SvgProperty(node, "transform"), space, hasOrigin, originX, originY);
        ApplySvgMotion(node, space);
        if (tag == "svg" && !space.hasViewport) {
            // A nested <svg> establishes its own viewport before its viewBox is
            // applied. Percentages resolve against the parent viewport.
            const double parentUserWidth = space.scaleX != 0 ? space.pixelWidth / space.scaleX : space.pixelWidth;
            const double parentUserHeight = space.scaleY != 0 ? space.pixelHeight / space.scaleY : space.pixelHeight;
            auto viewportValue = [](const std::string& text, double fallback, double reference) {
                if (text.empty()) {
                    return fallback * reference;
                }
                if (text.find('%') != std::string::npos) {
                    return std::atof(text.c_str()) / 100.0 * reference;
                }
                return std::atof(text.c_str());
            };
            const double viewportX = viewportValue(AttributeValue(node, "x"), 0, parentUserWidth);
            const double viewportY = viewportValue(AttributeValue(node, "y"), 0, parentUserHeight);
            const double viewportWidth = viewportValue(AttributeValue(node, "width"), 1, parentUserWidth);
            const double viewportHeight = viewportValue(AttributeValue(node, "height"), 1, parentUserHeight);
            space.originX += (viewportX - space.minX) * space.scaleX;
            space.originY += (viewportY - space.minY) * space.scaleY;
            space.pixelWidth = viewportWidth * space.scaleX;
            space.pixelHeight = viewportHeight * space.scaleY;
            space.minX = 0;
            space.minY = 0;
            space.scaleX = 1;
            space.scaleY = 1;
            space.hasViewport = true;
            // Nested viewports clip their content unless overflow is visible.
            if (ToLowerAscii(AttributeValue(node, "overflow")) != "visible" && space.pixelWidth > 0 && space.pixelHeight > 0) {
                space.clipped = true;
                space.clipX = static_cast<int>(space.originX);
                space.clipY = static_cast<int>(space.originY);
                space.clipWidth = static_cast<int>(space.pixelWidth);
                space.clipHeight = static_cast<int>(space.pixelHeight);
            }
        }
        if (tag == "svg" || node.kind == ElementKind::Svg) {
            ApplyViewBox(node, space);
            if (AttributeValue(node, "viewbox").empty()) {
                for (const std::unique_ptr<Node>& child : node.children) {
                    if (ToLowerAscii(child->tagName) == "view" && !AttributeValue(*child, "viewbox").empty()) {
                        ApplyViewBox(*child, space);
                        break;
                    }
                }
            }
        }
        if ((tag == "svg" || node.kind == ElementKind::Svg) && ToLowerAscii(AttributeValue(node, "overflow")) == "hidden" && space.pixelWidth > 0 && space.pixelHeight > 0) {
            space.clipped = true;
            space.clipX = static_cast<int>(space.originX);
            space.clipY = static_cast<int>(space.originY);
            space.clipWidth = static_cast<int>(space.pixelWidth);
            space.clipHeight = static_cast<int>(space.pixelHeight);
        }
        ReadSvgPaint(node, space);
        SvgMaskContent maskContent;
        bool hasMask = false;
        const std::string maskId = SvgPaintId(SvgProperty(node, "mask"));
        if (!maskId.empty()) {
            hasMask = BuildSvgMask(node, maskId, space, maskContent);
        }
        // Everything this element paints, descendants and referenced content
        // included, passes through the clip-path scopes collected above and the
        // element's own mask.
        SvgClipScope clipScope(*this, space.clipScopes.empty() ? nullptr : &space);
        SvgMaskScope maskScope(*this, hasMask ? &maskContent : nullptr);
        const Color fill = WithOpacity(space.fill, space.opacity * space.fillOpacity);
        const Color stroke = WithOpacity(space.stroke, space.opacity * space.strokeOpacity);
        const bool fixedStroke = ToLowerAscii(SvgProperty(node, "vector-effect")) == "non-scaling-stroke";
        const double matrixScale = SvgMatrixScaleFactor(space.matrix);
        const int thickness = space.strokeWidth <= 0 ? 0 :
            std::max(1, static_cast<int>(space.strokeWidth * (fixedStroke ? 1 : matrixScale * std::max(space.scaleX, space.scaleY)) + 0.5));
        if (!space.hidden && tag == "rect") {
            SvgFilter filterEffect;
            const bool filtered = ResolveFilter(node, space, filterEffect);
            const int x = SvgX(space, SvgNumber(node, "x", 0)) + (filtered ? filterEffect.dx : 0);
            const int y = SvgY(space, SvgNumber(node, "y", 0)) + (filtered ? filterEffect.dy : 0);
            if (filtered && filterEffect.flood && !filterEffect.mergeSource) {
                const int floodX = filterEffect.region ? filterEffect.x : x;
                const int floodY = filterEffect.region ? filterEffect.y : y;
                const int floodWidth = filterEffect.region ? filterEffect.width : SvgLength(space, SvgNumber(node, "width", 0), true);
                const int floodHeight = filterEffect.region ? filterEffect.height : SvgLength(space, SvgNumber(node, "height", 0), false);
                Fill(floodX, floodY, floodWidth, floodHeight, WithOpacity(filterEffect.floodColor, space.opacity), fixed);
                RememberSvgLink(node, floodX, floodY, floodWidth, floodHeight, fixed);
            } else {
            const int width = SvgLength(space, SvgNumber(node, "width", 0), true);
            const int height = SvgLength(space, SvgNumber(node, "height", 0), false);
            const double userWidth = SvgNumber(node, "width", 0);
            const double userHeight = SvgNumber(node, "height", 0);
            double radiusX = 0;
            double radiusY = 0;
            SvgRectRadii(node, userWidth, userHeight, radiusX, radiusY);
            const std::string rectDashArray = AttributeValue(node, "stroke-dasharray");
            std::vector<double> rectDashes;
            const bool rectDashed = ParseSvgDashArray(rectDashArray, rectDashes);
            const bool rectAxisAligned = SvgMatrixIsAxisAligned(space.matrix) && space.matrix.a > 0 && space.matrix.d > 0;
            const bool rectPathMode = !filtered && SvgProperty(node, "mask").empty() &&
                                      (radiusX > 0 || radiusY > 0 || rectDashed || !rectAxisAligned);
            if (rectPathMode) {
                const std::string rectPath = SvgRectPath(SvgNumber(node, "x", 0), SvgNumber(node, "y", 0), userWidth, userHeight, radiusX, radiusY);
                if (!rectPath.empty()) {
                    const std::string rectPaintOrder = ToLowerAscii(SvgProperty(node, "paint-order"));
                    const bool rectStrokeFirst = rectPaintOrder.find("stroke") != std::string::npos &&
                        (rectPaintOrder.find("fill") == std::string::npos || rectPaintOrder.find("stroke") < rectPaintOrder.find("fill"));
                    auto strokePath = [&]() {
                        if (stroke.alpha != 0) {
                            PaintPath(rectPath, static_cast<int>(space.originX), static_cast<int>(space.originY), Color{0, 0, 0, 0}, stroke, fixed,
                                thickness, space.scaleX, space.scaleY, space.minX, space.minY, space.fillEvenOdd, nullptr, &space, rectDashArray,
                                std::atof(AttributeValue(node, "stroke-dashoffset").c_str()), SvgNumber(node, "pathlength", 0));
                        }
                    };
                    if (rectStrokeFirst) {
                        strokePath();
                    }
                    if (fill.alpha != 0 || !space.fillUrl.empty()) {
                        PaintPath(rectPath, static_cast<int>(space.originX), static_cast<int>(space.originY), fill, Color{0, 0, 0, 0}, fixed, thickness,
                            space.scaleX, space.scaleY, space.minX, space.minY, space.fillEvenOdd, &node, &space);
                    }
                    if (!rectStrokeFirst) {
                        strokePath();
                    }
                    RememberSvgLink(node, x, y, width, height, fixed);
                }
            }
            int fillX = x;
            int fillY = y;
            int fillWidth = width;
            int fillHeight = height;
            if (filtered && filterEffect.dilate > 0) {
                fillX -= filterEffect.dilate;
                fillY -= filterEffect.dilate;
                fillWidth += filterEffect.dilate * 2;
                fillHeight += filterEffect.dilate * 2;
            } else if (filtered && filterEffect.erode > 0) {
                fillX += filterEffect.erode;
                fillY += filterEffect.erode;
                fillWidth -= filterEffect.erode * 2;
                fillHeight -= filterEffect.erode * 2;
            }
            Color shown = fill;
            if (filtered && filterEffect.imageReplace) {
                shown = filterEffect.imageColor;
            }
            if (filtered && filterEffect.diffuse && !filterEffect.pointLight && !filterEffect.distant) {
                auto lit = [&](int value) {
                    return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, value * filterEffect.diffuseAmount)));
                };
                shown.red = lit(filterEffect.lightColor.red);
                shown.green = lit(filterEffect.lightColor.green);
                shown.blue = lit(filterEffect.lightColor.blue);
                shown.alpha = 255;
            }
            if (filtered && filterEffect.convolve && !filterEffect.convolveSample) {
                auto mapChannel = [&](int value) {
                    const double mapped = value * filterEffect.convolveGain + filterEffect.convolveBias * 255.0;
                    return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, mapped)));
                };
                shown.red = mapChannel(shown.red);
                shown.green = mapChannel(shown.green);
                shown.blue = mapChannel(shown.blue);
                if (!filterEffect.convolvePreserveAlpha) {
                    shown.alpha = mapChannel(shown.alpha);
                }
            }
            if (filtered && filterEffect.saturate) {
                const int gray = (shown.red * 30 + shown.green * 59 + shown.blue * 11) / 100;
                const double amount = filterEffect.saturateAmount;
                shown.red = static_cast<std::uint8_t>(gray + (shown.red - gray) * amount);
                shown.green = static_cast<std::uint8_t>(gray + (shown.green - gray) * amount);
                shown.blue = static_cast<std::uint8_t>(gray + (shown.blue - gray) * amount);
            }
            if (filtered && filterEffect.colorMatrix) {
                const double red = shown.red / 255.0;
                const double green = shown.green / 255.0;
                const double blue = shown.blue / 255.0;
                const double alpha = shown.alpha / 255.0;
                auto clampChannel = [](double value) {
                    return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, value * 255.0)));
                };
                if (filterEffect.colorMatrixKind == 3) {
                    shown.alpha = clampChannel(0.2125 * red + 0.7154 * green + 0.0721 * blue);
                    shown.red = 0;
                    shown.green = 0;
                    shown.blue = 0;
                } else {
                    double cell[20];
                    for (int index = 0; index < 20; ++index) {
                        cell[index] = filterEffect.matrix[index];
                    }
                    if (filterEffect.colorMatrixKind == 2) {
                        const double radians = filterEffect.hueAngle * std::acos(-1.0) / 180.0;
                        const double cosine = std::cos(radians);
                        const double sine = std::sin(radians);
                        cell[0] = 0.213 + cosine * 0.787 - sine * 0.213;
                        cell[1] = 0.715 - cosine * 0.715 - sine * 0.715;
                        cell[2] = 0.072 - cosine * 0.072 + sine * 0.928;
                        cell[5] = 0.213 - cosine * 0.213 + sine * 0.143;
                        cell[6] = 0.715 + cosine * 0.285 + sine * 0.140;
                        cell[7] = 0.072 - cosine * 0.072 - sine * 0.283;
                        cell[10] = 0.213 - cosine * 0.213 - sine * 0.787;
                        cell[11] = 0.715 - cosine * 0.715 + sine * 0.715;
                        cell[12] = 0.072 + cosine * 0.928 + sine * 0.072;
                    }
                    auto map = [&](int row) {
                        const int base = row * 5;
                        return clampChannel(cell[base] * red + cell[base + 1] * green + cell[base + 2] * blue + cell[base + 3] * alpha + cell[base + 4]);
                    };
                    shown.red = map(0);
                    shown.green = map(1);
                    shown.blue = map(2);
                    shown.alpha = map(3);
                }
            }
            if (filtered && filterEffect.transfer) {
                auto mapChannel = [&](int value, double slope, double intercept) {
                    double amount = static_cast<double>(value) / 255.0;
                    if (filterEffect.linearFilters) {
                        amount = amount <= 0.04045 ? amount / 12.92 : std::pow((amount + 0.055) / 1.055, 2.4);
                    }
                    double mapped = amount * slope + intercept;
                    if (filterEffect.linearFilters) {
                        mapped = mapped <= 0.0031308 ? 12.92 * mapped : 1.055 * std::pow(std::max(0.0, mapped), 1.0 / 2.4) - 0.055;
                    }
                    return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, mapped * 255.0)));
                };
                auto mapTable = [&](int value, int index) {
                    if (filterEffect.gamma[index]) {
                        const double mapped = filterEffect.amplitude[index] * std::pow(static_cast<double>(value) / 255.0, filterEffect.exponent[index]) + filterEffect.gammaOffset[index];
                        return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, mapped * 255.0)));
                    }
                    const int count = filterEffect.tableCount[index];
                    if (count <= 0) {
                        return mapChannel(value, filterEffect.slope[index], filterEffect.intercept[index]);
                    }
                    if (count == 1) {
                        return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, filterEffect.tableValue[index][0] * 255.0)));
                    }
                    const double scaled = static_cast<double>(value) / 255.0 * static_cast<double>(count - 1);
                    const int left = std::min(count - 2, static_cast<int>(scaled));
                    const double mix = scaled - left;
                    const double mapped = filterEffect.tableValue[index][left] + (filterEffect.tableValue[index][left + 1] - filterEffect.tableValue[index][left]) * mix;
                    return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, mapped * 255.0)));
                };
                if (filterEffect.transferChannel[0]) {
                    shown.red = mapTable(shown.red, 0);
                }
                if (filterEffect.transferChannel[1]) {
                    shown.green = mapTable(shown.green, 1);
                }
                if (filterEffect.transferChannel[2]) {
                    shown.blue = mapTable(shown.blue, 2);
                }
                if (filterEffect.transferChannel[3]) {
                    shown.alpha = mapTable(shown.alpha, 3);
                }
            }
            if (filtered && filterEffect.blend) {
                auto channel = [&](int source, int other) {
                    const double first = source / 255.0;
                    const double second = other / 255.0;
                    double mixed = first * second;
                    if (filterEffect.blendMode == 2) {
                        mixed = 1.0 - (1.0 - first) * (1.0 - second);
                    } else if (filterEffect.blendMode == 3) {
                        mixed = std::min(first, second);
                    } else if (filterEffect.blendMode == 4) {
                        mixed = std::max(first, second);
                    }
                    return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, mixed * 255.0)));
                };
                shown.red = channel(shown.red, filterEffect.blendColor.red);
                shown.green = channel(shown.green, filterEffect.blendColor.green);
                shown.blue = channel(shown.blue, filterEffect.blendColor.blue);
            }
            if (filtered && filterEffect.arithmetic) {
                auto channel = [&](int source, int other) {
                    const double first = source / 255.0;
                    const double second = other / 255.0;
                    const double mixed = filterEffect.k1 * first * second + filterEffect.k2 * first + filterEffect.k3 * second + filterEffect.k4;
                    return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, mixed * 255.0)));
                };
                shown.red = channel(shown.red, filterEffect.blendColor.red);
                shown.green = channel(shown.green, filterEffect.blendColor.green);
                shown.blue = channel(shown.blue, filterEffect.blendColor.blue);
            }
            if (space.clipped) {
                const int right = std::min(fillX + fillWidth, space.clipX + space.clipWidth);
                const int bottom = std::min(fillY + fillHeight, space.clipY + space.clipHeight);
                fillX = std::max(fillX, space.clipX);
                fillY = std::max(fillY, space.clipY);
                fillWidth = right - fillX;
                fillHeight = bottom - fillY;
            }
            const std::string paintOrder = ToLowerAscii(SvgProperty(node, "paint-order"));
            const bool strokeFirst = paintOrder.find("stroke") != std::string::npos &&
                                     (paintOrder.find("fill") == std::string::npos || paintOrder.find("stroke") < paintOrder.find("fill"));
            auto strokeRect = [&]() {
                if (stroke.alpha != 0 && width > 0 && height > 0) {
                    std::vector<SvgPoint> rectOutline;
                    rectOutline.push_back(SvgPoint{static_cast<double>(x), static_cast<double>(y)});
                    rectOutline.push_back(SvgPoint{static_cast<double>(x + width), static_cast<double>(y)});
                    rectOutline.push_back(SvgPoint{static_cast<double>(x + width), static_cast<double>(y + height)});
                    rectOutline.push_back(SvgPoint{static_cast<double>(x), static_cast<double>(y + height)});
                    StrokePolyline(rectOutline, 0, 0, stroke, fixed, true, thickness, std::string(), 0, 0, space.lineJoin, space.lineCap,
                        space.miterLimit, space.shapeRendering == 2);
                }
            };
            if (!rectPathMode && strokeFirst) {
                strokeRect();
            }
            const bool painted = rectPathMode ||
                                 (!space.fillUrl.empty() && (FillLinearGradient(node, space, x, y, width, height, fixed) ||
                                                                FillPattern(node, space, x, y, width, height, fixed)));
            if (filtered && filterEffect.mergeFlood && filterEffect.region) {
                Fill(filterEffect.x, filterEffect.y, filterEffect.width, filterEffect.height, filterEffect.floodColor, fixed);
            }
            if (!painted && filtered && filterEffect.tile && width > 0 && height > 0 && shown.alpha != 0) {
                const int regionX = filterEffect.region ? filterEffect.x : x;
                const int regionY = filterEffect.region ? filterEffect.y : y;
                const int regionWidth = filterEffect.region ? filterEffect.width : width;
                const int regionHeight = filterEffect.region ? filterEffect.height : height;
                for (int tileY = regionY; tileY < regionY + regionHeight; tileY += height) {
                    for (int tileX = regionX; tileX < regionX + regionWidth; tileX += width) {
                        Fill(tileX, tileY, std::min(width, regionX + regionWidth - tileX), std::min(height, regionY + regionHeight - tileY), shown, fixed);
                    }
                }
            } else if (!painted && filtered && filterEffect.dropShadow && shown.alpha != 0 && width > 0 && height > 0) {
                const int radius = filterEffect.blur;
                const int shadowX = x + filterEffect.shadowDx;
                const int shadowY = y + filterEffect.shadowDy;
                if (radius <= 0) {
                    Fill(shadowX, shadowY, width, height, filterEffect.floodColor, fixed);
                } else {
                    const double sigma = filterEffect.blurSigma > 0 ? filterEffect.blurSigma : static_cast<double>(radius);
                    const int spread = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
                    for (int row = -spread; row < height + spread; ++row) {
                        for (int column = -spread; column < width + spread; ++column) {
                            const double coverage = SvgBlurredBoxCoverage(shadowX + column, shadowY + row, shadowX, shadowY, width, height, sigma);
                            if (coverage <= 0.002) {
                                continue;
                            }
                            Color blurred = filterEffect.floodColor;
                            const int alpha = static_cast<int>(filterEffect.floodColor.alpha * coverage + 0.5);
                            blurred.alpha = static_cast<std::uint8_t>(alpha > 255 ? 255 : (alpha < 0 ? 0 : alpha));
                            Fill(shadowX + column, shadowY + row, 1, 1, blurred, fixed);
                        }
                    }
                }
                Fill(x, y, width, height, shown, fixed);
            } else if (!painted && filtered && filterEffect.blur > 0 && shown.alpha != 0 && width > 0 && height > 0) {
                const double sigma = filterEffect.blurSigma > 0 ? filterEffect.blurSigma : static_cast<double>(filterEffect.blur);
                const int spread = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
                for (int row = -spread; row < height + spread; ++row) {
                    for (int column = -spread; column < width + spread; ++column) {
                        const double coverage = SvgBlurredBoxCoverage(x + column, y + row, x, y, width, height, sigma);
                        if (coverage <= 0.002) {
                            continue;
                        }
                        Color blurred = shown;
                        const int alpha = static_cast<int>(shown.alpha * coverage + 0.5);
                        blurred.alpha = static_cast<std::uint8_t>(alpha > 255 ? 255 : (alpha < 0 ? 0 : alpha));
                        Fill(x + column, y + row, 1, 1, blurred, fixed);
                    }
                }
            } else if (!painted && filtered && filterEffect.distant && width > 0 && height > 0) {
                const double radians = std::acos(-1.0) / 180.0;
                const double lightX = std::cos(filterEffect.azimuth * radians) * std::cos(filterEffect.elevation * radians);
                const double lightY = std::sin(filterEffect.azimuth * radians) * std::cos(filterEffect.elevation * radians);
                const double lightZ = std::sin(filterEffect.elevation * radians);
                auto heightAt = [&](int column, int row) {
                    if (column < 0 || row < 0 || column >= width || row >= height) {
                        return 0.0;
                    }
                    return filterEffect.surfaceScale;
                };
                for (int row = 0; row < height; ++row) {
                    for (int column = 0; column < width; ++column) {
                        const double slopeX = heightAt(column + 1, row) - heightAt(column - 1, row);
                        const double slopeY = heightAt(column, row + 1) - heightAt(column, row - 1);
                        const double normalX = -slopeX;
                        const double normalY = -slopeY;
                        const double normalZ = 1;
                        const double length = std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
                        const double dot = length <= 0 ? 0 : (normalX * lightX + normalY * lightY + normalZ * lightZ) / length;
                        const double facing = std::pow(std::max(0.0, dot), filterEffect.specularExponent);
                        auto lit = [&](int value) {
                            return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, value * facing * filterEffect.diffuseAmount)));
                        };
                        Color grain;
                        grain.red = lit(filterEffect.lightColor.red);
                        grain.green = lit(filterEffect.lightColor.green);
                        grain.blue = lit(filterEffect.lightColor.blue);
                        grain.alpha = 255;
                        Fill(x + column, y + row, 1, 1, grain, fixed);
                    }
                }
            } else if (!painted && filtered && filterEffect.pointLight && width > 0 && height > 0) {
                const double aimX = filterEffect.aimX - filterEffect.pointX;
                const double aimY = filterEffect.aimY - filterEffect.pointY;
                const double aimZ = filterEffect.aimZ - filterEffect.pointZ;
                const double aimLength = std::sqrt(aimX * aimX + aimY * aimY + aimZ * aimZ);
                for (int row = 0; row < height; ++row) {
                    for (int column = 0; column < width; ++column) {
                        const double towardX = filterEffect.pointX - (x + column);
                        const double towardY = filterEffect.pointY - (y + row);
                        const double towardZ = filterEffect.pointZ;
                        const double towardLength = std::sqrt(towardX * towardX + towardY * towardY + towardZ * towardZ);
                        double facing = towardLength <= 0 ? 0 : std::max(0.0, towardZ / towardLength);
                        if (filterEffect.spot && aimLength > 0 && towardLength > 0) {
                            const double alignment = (-towardX * aimX + -towardY * aimY + -towardZ * aimZ) / (towardLength * aimLength);
                            if (alignment < filterEffect.spotLimit) {
                                facing = 0;
                            }
                        }
                        auto lit = [&](int value) {
                            return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, value * facing * filterEffect.diffuseAmount)));
                        };
                        Color grain;
                        grain.red = lit(filterEffect.lightColor.red);
                        grain.green = lit(filterEffect.lightColor.green);
                        grain.blue = lit(filterEffect.lightColor.blue);
                        grain.alpha = 255;
                        Fill(x + column, y + row, 1, 1, grain, fixed);
                    }
                }
            } else if (!painted && filtered && filterEffect.turbulence && width > 0 && height > 0) {
                auto noise = [&](int px, int py, int salt) {
                    unsigned value = static_cast<unsigned>(filterEffect.turbulenceSeed * 374761393 + salt * 668265263 + px * 1274126177 + py * 2147001325);
                    value = (value ^ (value >> 13)) * 1274126177u;
                    return static_cast<std::uint8_t>(value >> 24);
                };
                const double frequency = filterEffect.turbulenceFrequency;
                for (int row = 0; row < height; ++row) {
                    for (int column = 0; column < width; ++column) {
                        int cellX = static_cast<int>(column * frequency);
                        int cellY = static_cast<int>(row * frequency);
                        if (filterEffect.turbulenceStitch) {
                            cellX %= std::max(1, width / 2);
                            cellY %= std::max(1, height / 2);
                        }
                        Color grain;
                        grain.red = noise(cellX, cellY, 1);
                        grain.green = noise(cellX, cellY, 2);
                        grain.blue = noise(cellX, cellY, 3);
                        if (filterEffect.turbulenceOctaves > 1) {
                            grain.red = static_cast<std::uint8_t>((grain.red + noise(cellX * 2, cellY * 2, 9)) / 2);
                            grain.green = static_cast<std::uint8_t>((grain.green + noise(cellX * 2, cellY * 2, 10)) / 2);
                            grain.blue = static_cast<std::uint8_t>((grain.blue + noise(cellX * 2, cellY * 2, 11)) / 2);
                        }
                        if (filterEffect.turbulenceAbs) {
                            auto fold = [](std::uint8_t value) {
                                return static_cast<std::uint8_t>(std::min(255, std::abs(static_cast<int>(value) - 128) * 2));
                            };
                            grain.red = fold(grain.red);
                            grain.green = fold(grain.green);
                            grain.blue = fold(grain.blue);
                        }
                        grain.alpha = 255;
                        Fill(x + column, y + row, 1, 1, grain, fixed);
                    }
                }
            } else if (!painted && fillWidth > 0 && fillHeight > 0 && shown.alpha != 0) {
                if (filtered && filterEffect.convolveSample) {
                    double kernelSum = 0;
                    for (int index = 0; index < filterEffect.kernelCount; ++index) {
                        kernelSum += filterEffect.kernel[index];
                    }
                    const double usedDivisor = std::abs(kernelSum) < 0.000001 || std::abs(filterEffect.convolveGain) < 0.000001 ? 1 : kernelSum / filterEffect.convolveGain;
                    for (int row = 0; row < fillHeight; ++row) {
                        for (int column = 0; column < fillWidth; ++column) {
                            double weight = 0;
                            int cursor = 0;
                            for (int ky = 0; ky < filterEffect.kernelOrderY; ++ky) {
                                for (int kx = 0; kx < filterEffect.kernelOrderX; ++kx) {
                                    const double cell = cursor < filterEffect.kernelCount ? filterEffect.kernel[cursor] : 0;
                                    ++cursor;
                                    int sampleX = column + (kx - filterEffect.kernelTargetX) * filterEffect.kernelUnitX;
                                    int sampleY = row + (ky - filterEffect.kernelTargetY) * filterEffect.kernelUnitY;
                                    if (filterEffect.edgeMode == 1) {
                                        sampleX = (sampleX % fillWidth + fillWidth) % fillWidth;
                                        sampleY = (sampleY % fillHeight + fillHeight) % fillHeight;
                                        weight += cell;
                                    } else if (filterEffect.edgeMode == 0) {
                                        weight += cell;
                                    } else if (sampleX >= 0 && sampleY >= 0 && sampleX < fillWidth && sampleY < fillHeight) {
                                        weight += cell;
                                    }
                                }
                            }
                            auto map = [&](int value) {
                                const double mapped = value * weight / usedDivisor + filterEffect.convolveBias * 255.0;
                                return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, mapped)));
                            };
                            Color pixel = shown;
                            pixel.red = map(shown.red);
                            pixel.green = map(shown.green);
                            pixel.blue = map(shown.blue);
                            if (!filterEffect.convolvePreserveAlpha) {
                                pixel.alpha = map(shown.alpha);
                            }
                            Fill(fillX + column, fillY + row, 1, 1, pixel, fixed);
                        }
                    }
                } else {
                    const double rawX = space.originX + (SvgReal(node, "x", 0) - space.minX) * space.scaleX;
                    const double rawY = space.originY + (SvgReal(node, "y", 0) - space.minY) * space.scaleY;
                    const double rawWidth = SvgReal(node, "width", 0) * space.scaleX;
                    const double rawHeight = SvgReal(node, "height", 0) * space.scaleY;
                    const bool wholePixelEdges = rawX == std::floor(rawX) && rawY == std::floor(rawY) &&
                                                 rawX + rawWidth == std::floor(rawX + rawWidth) &&
                                                 rawY + rawHeight == std::floor(rawY + rawHeight);
                    if (!wholePixelEdges && space.shapeRendering != 2) {
                        // Fractional rectangle edges cover part of their pixels,
                        // exactly like any other antialiased geometry.
                        FillCoverage(rawX, rawY, rawWidth, rawHeight, shown, fixed);
                    } else if (space.shapeRendering == 2) {
                        const double rawRight = rawX + SvgReal(node, "width", 0) * space.scaleX;
                        const double rawBottom = rawY + SvgReal(node, "height", 0) * space.scaleY;
                        const int snappedX = static_cast<int>(std::lround(rawX));
                        const int snappedY = static_cast<int>(std::lround(rawY));
                        Fill(snappedX, snappedY, std::max(0, static_cast<int>(std::lround(rawRight)) - snappedX),
                            std::max(0, static_cast<int>(std::lround(rawBottom)) - snappedY), shown, fixed);
                    } else {
                        Fill(fillX, fillY, fillWidth, fillHeight, shown, fixed);
                    }
                }
            }
            if (!rectPathMode && !strokeFirst) {
                strokeRect();
            }
            RememberSvgLink(node, x, y, width, height, fixed);
            }
        } else if (!space.hidden && (tag == "circle" || tag == "ellipse") &&
                   (!SvgMatrixIsAxisAligned(space.matrix) || !space.fillUrl.empty() || !space.strokeUrl.empty() ||
                    SvgHasDashArray(AttributeValue(node, "stroke-dasharray")))) {
            const double centerX = SvgReal(node, "cx", 0);
            const double centerY = SvgReal(node, "cy", 0);
            const double radiusX = tag == "circle" ? SvgReal(node, "r", 0) : SvgReal(node, "rx", 0);
            const double radiusY = tag == "circle" ? radiusX : SvgReal(node, "ry", 0);
            std::string outline = "M";
            for (int step = 0; step <= 48; ++step) {
                const double angle = step * 2.0 * std::acos(-1.0) / 48.0;
                if (step == 1) {
                    outline += "L";
                }
                outline += std::to_string(centerX + radiusX * std::cos(angle));
                outline += ' ';
                outline += std::to_string(centerY + radiusY * std::sin(angle));
                outline += ' ';
            }
            outline += "Z";
            SvgFilter roundShadow;
            if (ResolveFilter(node, space, roundShadow) && roundShadow.dropShadow) {
                PaintPath(outline, static_cast<int>(space.originX) + roundShadow.shadowDx, static_cast<int>(space.originY) + roundShadow.shadowDy, roundShadow.floodColor, Color{0, 0, 0, 0}, fixed, thickness, space.scaleX, space.scaleY, space.minX, space.minY, false, nullptr, &space);
            }
            PaintPath(outline, static_cast<int>(space.originX), static_cast<int>(space.originY), fill, stroke, fixed, thickness, space.scaleX, space.scaleY, space.minX, space.minY, false, &node, &space,
                AttributeValue(node, "stroke-dasharray"), std::atof(AttributeValue(node, "stroke-dashoffset").c_str()), SvgNumber(node, "pathlength", 0));
            RememberSvgLink(node, SvgX(space, centerX - radiusX), SvgY(space, centerY - radiusY), SvgLength(space, radiusX * 2, true), SvgLength(space, radiusY * 2, false), fixed);
        } else if (!space.hidden && tag == "circle") {
            const double rawCx = space.originX + (SvgReal(node, "cx", 0) - space.minX) * space.scaleX;
            const double rawCy = space.originY + (SvgReal(node, "cy", 0) - space.minY) * space.scaleY;
            const int cx = space.shapeRendering == 2 ? static_cast<int>(std::lround(rawCx)) : SvgX(space, SvgNumber(node, "cx", 0));
            const int cy = space.shapeRendering == 2 ? static_cast<int>(std::lround(rawCy)) : SvgY(space, SvgNumber(node, "cy", 0));
            const int radiusX = SvgLength(space, SvgNumber(node, "r", 0), true);
            const int radiusY = SvgLength(space, SvgNumber(node, "r", 0), false);
            SvgFilter circleShadow;
            if (ResolveFilter(node, space, circleShadow) && circleShadow.dropShadow) {
                FillEllipseShadow(cx + circleShadow.shadowDx, cy + circleShadow.shadowDy, radiusX, radiusY, circleShadow.blur, circleShadow.floodColor, fixed);
            }
            FillEllipse(cx, cy, radiusX, radiusY, fill, fixed);
            StrokeEllipse(cx, cy, radiusX, radiusY, thickness, stroke, fixed, space.lineJoin, space.lineCap, space.miterLimit);
            RememberSvgLink(node, cx - radiusX, cy - radiusY, radiusX * 2, radiusY * 2, fixed);
        } else if (!space.hidden && tag == "ellipse") {
            const double rawCx = space.originX + (SvgReal(node, "cx", 0) - space.minX) * space.scaleX;
            const double rawCy = space.originY + (SvgReal(node, "cy", 0) - space.minY) * space.scaleY;
            const int cx = space.shapeRendering == 2 ? static_cast<int>(std::lround(rawCx)) : SvgX(space, SvgNumber(node, "cx", 0));
            const int cy = space.shapeRendering == 2 ? static_cast<int>(std::lround(rawCy)) : SvgY(space, SvgNumber(node, "cy", 0));
            const int radiusX = SvgLength(space, SvgNumber(node, "rx", 0), true);
            const int radiusY = SvgLength(space, SvgNumber(node, "ry", 0), false);
            SvgFilter ellipseShadow;
            if (ResolveFilter(node, space, ellipseShadow) && ellipseShadow.dropShadow) {
                FillEllipseShadow(cx + ellipseShadow.shadowDx, cy + ellipseShadow.shadowDy, radiusX, radiusY, ellipseShadow.blur, ellipseShadow.floodColor, fixed);
            }
            FillEllipse(cx, cy, radiusX, radiusY, fill, fixed);
            StrokeEllipse(cx, cy, radiusX, radiusY, thickness, stroke, fixed, space.lineJoin, space.lineCap, space.miterLimit);
            RememberSvgLink(node, cx - radiusX, cy - radiusY, radiusX * 2, radiusY * 2, fixed);
        } else if (!space.hidden && tag == "line") {
            const double rawX1 = space.originX + (SvgReal(node, "x1", 0) - space.minX) * space.scaleX;
            const double rawY1 = space.originY + (SvgReal(node, "y1", 0) - space.minY) * space.scaleY;
            const double rawX2 = space.originX + (SvgReal(node, "x2", 0) - space.minX) * space.scaleX;
            const double rawY2 = space.originY + (SvgReal(node, "y2", 0) - space.minY) * space.scaleY;
            int x1 = space.shapeRendering == 2 ? static_cast<int>(std::lround(rawX1)) : SvgX(space, SvgNumber(node, "x1", 0));
            int y1 = space.shapeRendering == 2 ? static_cast<int>(std::lround(rawY1)) : SvgY(space, SvgNumber(node, "y1", 0));
            int x2 = space.shapeRendering == 2 ? static_cast<int>(std::lround(rawX2)) : SvgX(space, SvgNumber(node, "x2", 0));
            int y2 = space.shapeRendering == 2 ? static_cast<int>(std::lround(rawY2)) : SvgY(space, SvgNumber(node, "y2", 0));
            if (!SvgMatrixIsAxisAligned(space.matrix)) {
                UserToPixel(space, SvgReal(node, "x1", 0), SvgReal(node, "y1", 0), x1, y1);
                UserToPixel(space, SvgReal(node, "x2", 0), SvgReal(node, "y2", 0), x2, y2);
            }
            const std::vector<SvgPoint> linePoints{SvgPoint{static_cast<double>(x1), static_cast<double>(y1)},
                                                    SvgPoint{static_cast<double>(x2), static_cast<double>(y2)}};
            const std::string dasharray = AttributeValue(node, "stroke-dasharray");
            const double dashOffset = std::atof(AttributeValue(node, "stroke-dashoffset").c_str());
            const double pathLength = SvgReal(node, "pathlength", 0);
            SvgFilter lineShadow;
            if (ResolveFilter(node, space, lineShadow) && lineShadow.dropShadow) {
                std::vector<SvgPoint> shadowPoints;
                shadowPoints.push_back(SvgPoint{linePoints[0].x + lineShadow.shadowDx, linePoints[0].y + lineShadow.shadowDy});
                shadowPoints.push_back(SvgPoint{linePoints[1].x + lineShadow.shadowDx, linePoints[1].y + lineShadow.shadowDy});
                StrokePolyline(shadowPoints, 0, 0, lineShadow.floodColor, fixed, false, thickness, dasharray, dashOffset, pathLength, space.lineJoin,
                    space.lineCap, space.miterLimit, space.shapeRendering == 2);
            }
            StrokePolyline(linePoints, 0, 0, stroke, fixed, false, thickness, dasharray, dashOffset, pathLength, space.lineJoin, space.lineCap,
                space.miterLimit, space.shapeRendering == 2);
            PaintLineMarkers(node, space, x1, y1, x2, y2, fixed);
            RememberSvgLink(node, std::min(x1, x2), std::min(y1, y2), std::abs(x2 - x1) + thickness, std::abs(y2 - y1) + thickness, fixed);
        } else if (!space.hidden && (tag == "polygon" || tag == "polyline")) {
            const std::vector<SvgPoint> points = MapSvgPoints(ParsePointList(AttributeValue(node, "points")), space);
            SvgFilter shapeShadow;
            if (ResolveFilter(node, space, shapeShadow) && shapeShadow.dropShadow) {
                std::vector<SvgPoint> shifted;
                shifted.reserve(points.size());
                for (const SvgPoint& point : points) {
                    shifted.push_back(SvgPoint{point.x + shapeShadow.shadowDx, point.y + shapeShadow.shadowDy});
                }
                if (tag == "polygon") {
                    FillPolygon(shifted, 0, 0, shapeShadow.floodColor, fixed, space.fillEvenOdd);
                }
                if (tag == "polyline" || stroke.alpha != 0) {
                    StrokePolyline(shifted, 0, 0, shapeShadow.floodColor, fixed, tag == "polygon", thickness, std::string(), 0, 0, space.lineJoin,
                        space.lineCap, space.miterLimit, space.shapeRendering == 2);
                }
            }
            if (tag == "polygon") {
                FillPolygon(points, 0, 0, fill, fixed, space.fillEvenOdd);
            }
            StrokePolyline(points, 0, 0, stroke, fixed, tag == "polygon", thickness, AttributeValue(node, "stroke-dasharray"),
                std::atof(AttributeValue(node, "stroke-dashoffset").c_str()), SvgReal(node, "pathlength", 0), space.lineJoin, space.lineCap,
                space.miterLimit, space.shapeRendering == 2);
            if (!SvgProperty(node, "marker-mid").empty()) {
                const std::size_t last = points.size();
                for (std::size_t index = 1; last > 1 && index + 1 < last; ++index) {
                    const double midAngle = SvgDirectionDegrees(points[index - 1].x, points[index - 1].y, points[index + 1].x, points[index + 1].y);
                    PaintMarkerAt(node, SvgProperty(node, "marker-mid"), points[index].x, points[index].y, space, fixed, midAngle, false);
                }
            }
            if (!points.empty()) {
                int minX = 1000000;
                int minY = 1000000;
                int maxX = -1000000;
                int maxY = -1000000;
                for (const SvgPoint& point : points) {
                    minX = std::min(minX, static_cast<int>(point.x));
                    minY = std::min(minY, static_cast<int>(point.y));
                    maxX = std::max(maxX, static_cast<int>(point.x));
                    maxY = std::max(maxY, static_cast<int>(point.y));
                }
                RememberSvgLink(node, minX, minY, maxX - minX, maxY - minY, fixed);
            }
        } else         if (!space.hidden && tag == "path") {
            SvgFilter pathShadow;
            if (ResolveFilter(node, space, pathShadow) && pathShadow.dropShadow) {
                PaintPath(SvgPathData(node), static_cast<int>(space.originX) + pathShadow.shadowDx, static_cast<int>(space.originY) + pathShadow.shadowDy, pathShadow.floodColor, Color{0, 0, 0, 0}, fixed, thickness,
                    space.scaleX, space.scaleY, space.minX, space.minY, space.fillEvenOdd, nullptr, nullptr);
            }
            PaintPath(SvgPathData(node), static_cast<int>(space.originX), static_cast<int>(space.originY), fill, stroke, fixed, thickness,
                space.scaleX, space.scaleY, space.minX, space.minY, space.fillEvenOdd, &node, &space,
                AttributeValue(node, "stroke-dasharray"), std::atof(AttributeValue(node, "stroke-dashoffset").c_str()),
                SvgNumber(node, "pathlength", 0));
        } else if (!space.hidden && tag == "image") {
            std::string href = AttributeValue(node, "href");
            if (href.empty()) {
                href = AttributeValue(node, "xlink:href");
            }
            const std::string path = ResolveUrl(*document, href);
            DrawOp op;
            op.kind = DrawOp::Kind::Image;
            op.x = SvgX(space, SvgNumber(node, "x", 0));
            op.y = SvgY(space, SvgNumber(node, "y", 0));
            op.width = std::max(1, SvgLength(space, SvgNumber(node, "width", 0), true));
            op.height = std::max(1, SvgLength(space, SvgNumber(node, "height", 0), false));
            op.imagePath = path.find("://") == std::string::npos ? path : href;
            const std::string rendering = ToLowerAscii(AttributeValue(node, "image-rendering"));
            op.smoothImage = rendering == "optimizequality";
            op.deferImage = ToLowerAscii(AttributeValue(node, "decoding")) == "async";
            std::string fetchMode = "omit";
            if (HasAttribute(node, "crossorigin")) {
                fetchMode = ToLowerAscii(AttributeValue(node, "crossorigin")) == "use-credentials" ? "include" : "anonymous";
            }
            NoteResourceFetch(href, ToLowerAscii(AttributeValue(node, "fetchpriority")), fetchMode);
            SvgFilter imageShadow;
            if (ResolveFilter(node, space, imageShadow) && imageShadow.dropShadow) {
                const int radius = imageShadow.blur;
                const int shadowX = op.x + imageShadow.shadowDx;
                const int shadowY = op.y + imageShadow.shadowDy;
                if (radius <= 0) {
                    Fill(shadowX, shadowY, op.width, op.height, imageShadow.floodColor, fixed);
                } else {
                    const double sigma = imageShadow.blurSigma > 0 ? imageShadow.blurSigma : static_cast<double>(radius);
                    const int spread = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
                    for (int row = -spread; row < op.height + spread; ++row) {
                        for (int column = -spread; column < op.width + spread; ++column) {
                            const double coverage = SvgBlurredBoxCoverage(shadowX + column, shadowY + row, shadowX, shadowY, op.width, op.height, sigma);
                            if (coverage <= 0.002) {
                                continue;
                            }
                            Color blurred = imageShadow.floodColor;
                            const int alpha = static_cast<int>(imageShadow.floodColor.alpha * coverage + 0.5);
                            blurred.alpha = static_cast<std::uint8_t>(alpha > 255 ? 255 : (alpha < 0 ? 0 : alpha));
                            Fill(shadowX + column, shadowY + row, 1, 1, blurred, fixed);
                        }
                    }
                }
            }
            op.fixed = fixed;
            result->ops.push_back(op);
            RememberSvgLink(node, op.x, op.y, op.width, op.height, fixed);
        } else if (!space.hidden && tag == "foreignobject") {
            const int x = SvgX(space, SvgNumber(node, "x", 0));
            const int y = SvgY(space, SvgNumber(node, "y", 0));
            const int width = std::max(1, SvgLength(space, SvgNumber(node, "width", 0), true));
            const int height = std::max(1, SvgLength(space, SvgNumber(node, "height", 0), false));
            ResolvedStyle host;
            AddBox(node, x, y, width, height, host, fixed);
            for (const std::unique_ptr<Node>& child : node.children) {
                if (child->type != Node::Type::Element) {
                    continue;
                }
                const ResolvedStyle childStyle = ResolveStyle(*child, host, *document);
                LayoutBlock(*child, childStyle, x, y, width, fixed, true);
            }
            RememberSvgLink(node, x, y, width, height, fixed);
        } else if (tag == "use") {
            std::string href = AttributeValue(node, "href");
            if (href.empty()) {
                href = AttributeValue(node, "xlink:href");
            }
            if (!href.empty() && href[0] == '#') {
                Node* root = &node;
                while (root->parent != nullptr) {
                    root = root->parent;
                }
                Node* target = FindSvgById(*root, href.substr(1));
                if (target != nullptr && target != &node) {
                    SvgSpace used = space;
                    used.originX += SvgNumber(node, "x", 0) * space.scaleX;
                    used.originY += SvgNumber(node, "y", 0) * space.scaleY;
                    PaintSvgTree(*target, used, fixed, true);
                }
            }
        } else if (!space.hidden && tag == "textpath") {
            std::string pathData = AttributeValue(node, "path");
            if (pathData.empty()) {
                std::string href = AttributeValue(node, "href");
                if (href.empty()) {
                    href = AttributeValue(node, "xlink:href");
                }
                const std::string id = SvgPaintId(href);
                Node* root = &node;
                while (root->parent != nullptr) {
                    root = root->parent;
                }
                if (!id.empty()) {
                    if (Node* path = FindSvgById(*root, id)) {
                        pathData = SvgPathData(*path);
                    }
                }
            }
            const std::vector<SvgPoint> course = MapSvgPoints(StraightPath(pathData, SvgDeviceScale(space)), space);
            ResolvedStyle textStyle;
            textStyle.color = fill;
            const int fontSize = std::max(1, SvgNumber(node, "font-size", DEFAULT_FONT_SIZE_PX));
            textStyle.fontSizePx = std::max(1, SvgLength(space, fontSize, false));
            std::string content = GatherText(node);
            const std::string pathWhiteSpace = ToLowerAscii(AttributeValue(node, "white-space"));
            const std::string pathXmlSpace = ToLowerAscii(AttributeValue(node, "xml:space"));
            if (pathWhiteSpace != "pre" && pathWhiteSpace != "pre-wrap" && pathXmlSpace != "preserve") {
                std::string collapsed;
                bool skipped = false;
                for (char character : content) {
                    if (character == ' ' || character == '\n' || character == '\t') {
                        if (!skipped && !collapsed.empty()) {
                            collapsed += ' ';
                            skipped = true;
                        }
                    } else {
                        collapsed += character;
                        skipped = false;
                    }
                }
                content = collapsed;
            }
            double pathLength = 0;
            for (std::size_t index = 1; index < course.size(); ++index) {
                const double deltaX = course[index].x - course[index - 1].x;
                const double deltaY = course[index].y - course[index - 1].y;
                pathLength += std::sqrt(deltaX * deltaX + deltaY * deltaY);
            }
            int glyphCount = 0;
            for (char character : content) {
                if (character != ' ') {
                    ++glyphCount;
                }
            }
            const bool exactSpacing = (ToLowerAscii(AttributeValue(node, "spacing")) == "exact" || ToLowerAscii(AttributeValue(node, "method")) == "stretch") && glyphCount > 1;
            const double exactStep = exactSpacing ? pathLength / static_cast<double>(glyphCount - 1) : 0;
            double traveled = std::atof(AttributeValue(node, "startoffset").c_str()) * std::max(space.scaleX, space.scaleY);
            int glyphIndex = 0;
            for (std::size_t byte = 0; byte < content.size(); ++byte) {
                const std::string glyph(1, content[byte]);
                if (glyph == " ") {
                    traveled += MeasureTextPx(" ", textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily);
                    continue;
                }
                const int advance = std::max(1, MeasureTextPx(glyph, textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily));
                double glyphX = 0;
                double glyphY = 0;
                if (PointAlong(course, exactSpacing ? glyphIndex * exactStep : traveled, glyphX, glyphY)) {
                    const int sideShift = ToLowerAscii(AttributeValue(node, "side")) == "right" ? 0 : textStyle.fontSizePx;
                    SvgFilter pathShadow;
                    if (ResolveFilter(node, space, pathShadow) && pathShadow.dropShadow) {
                        ResolvedStyle shadowStyle = textStyle;
                        shadowStyle.color = pathShadow.floodColor;
                        shadowStyle.underline = false;
                        shadowStyle.strike = false;
                        Text(static_cast<int>(glyphX) + pathShadow.shadowDx, static_cast<int>(glyphY) - sideShift + pathShadow.shadowDy, glyph, shadowStyle, fixed);
                    }
                    Text(static_cast<int>(glyphX), static_cast<int>(glyphY) - sideShift, glyph, textStyle, fixed);
                }
                ++glyphIndex;
                traveled += advance;
            }
        } else if (!space.hidden && (tag == "text" || tag == "tspan")) {
            bool nestedPath = false;
            for (const std::unique_ptr<Node>& child : node.children) {
                if (ToLowerAscii(child->tagName) == "textpath") {
                    nestedPath = true;
                }
            }
            if (!nestedPath) {
                ResolvedStyle textStyle;
                textStyle.color = fill;
                const int fontSize = std::max(1, static_cast<int>(SvgFontSize(node, DEFAULT_FONT_SIZE_PX)));
                textStyle.fontSizePx = std::max(1, SvgLength(space, fontSize, false));
                const std::string sizeAdjust = SvgProperty(node, "font-size-adjust");
                if (!sizeAdjust.empty() && ToLowerAscii(sizeAdjust) != "none") {
                    const double aspect = std::atof(sizeAdjust.c_str());
                    if (aspect > 0) {
                        textStyle.fontSizePx = std::max(1, static_cast<int>(textStyle.fontSizePx * aspect / 0.5 + 0.5));
                    }
                }
                const std::string weight = ToLowerAscii(SvgProperty(node, "font-weight"));
                textStyle.bold = weight == "bold" || weight == "bolder" || std::atoi(weight.c_str()) >= 700;
                const std::string decoration = ToLowerAscii(SvgProperty(node, "text-decoration"));
                textStyle.underline = decoration.find("underline") != std::string::npos;
                textStyle.strike = decoration.find("line-through") != std::string::npos;
                textStyle.italic = ToLowerAscii(SvgProperty(node, "font-style")) == "italic" || ToLowerAscii(SvgProperty(node, "font-style")) == "oblique";
                textStyle.fontFamily = SvgProperty(node, "font-family");
                textStyle.monospace = ToLowerAscii(textStyle.fontFamily).find("monospace") != std::string::npos;
                std::string content = GatherText(node);
                const std::string whiteSpace = ToLowerAscii(SvgProperty(node, "white-space"));
                const std::string xmlSpace = ToLowerAscii(AttributeValue(node, "xml:space"));
                if (whiteSpace != "pre" && whiteSpace != "pre-wrap" && xmlSpace != "preserve") {
                    std::string collapsed;
                    bool skipped = false;
                    for (char character : content) {
                        if (character == ' ' || character == '\n' || character == '\t') {
                            if (!skipped && !collapsed.empty()) {
                                collapsed += ' ';
                                skipped = true;
                            }
                        } else {
                            collapsed += character;
                            skipped = false;
                        }
                    }
                    content = collapsed;
                }
                if (ToLowerAscii(SvgProperty(node, "direction")) == "rtl" &&
                    ToLowerAscii(SvgProperty(node, "unicode-bidi")).find("override") != std::string::npos) {
                    std::reverse(content.begin(), content.end());
                }
                // The baseline direction and pitch come from the full affine
                // matrix so text inside rotated or skewed groups is placed along
                // the transformed baseline instead of an axis-aligned one.
                const double baseUserX = SvgReal(node, "x", 0);
                const double baseUserY = SvgReal(node, "y", 0);
                double baseRealX = 0;
                double baseRealY = 0;
                UserToPixelReal(space, baseUserX, baseUserY, baseRealX, baseRealY);
                const int basePixelX = space.textRendering == 2 ? static_cast<int>(std::lround(baseRealX)) : static_cast<int>(baseRealX);
                const int basePixelY = space.textRendering == 2 ? static_cast<int>(std::lround(baseRealY)) : static_cast<int>(baseRealY);
                double stepRealX = baseRealX;
                double stepRealY = baseRealY;
                UserToPixelReal(space, baseUserX + 1, baseUserY, stepRealX, stepRealY);
                double dirX = stepRealX - baseRealX;
                double dirY = stepRealY - baseRealY;
                const double dirLength = std::sqrt(dirX * dirX + dirY * dirY);
                if (dirLength > 0) {
                    dirX /= dirLength;
                    dirY /= dirLength;
                } else {
                    dirX = 1;
                    dirY = 0;
                }
                double pitchX = 0;
                double pitchY = 1;
                if (!SvgMatrixIsAxisAligned(space.matrix)) {
                    double downRealX = baseRealX;
                    double downRealY = baseRealY;
                    UserToPixelReal(space, baseUserX, baseUserY + 1, downRealX, downRealY);
                    pitchX = downRealX - baseRealX;
                    pitchY = downRealY - baseRealY;
                    const double pitchLength = std::sqrt(pitchX * pitchX + pitchY * pitchY);
                    if (pitchLength > 0) {
                        pitchX /= pitchLength;
                        pitchY /= pitchLength;
                    } else {
                        pitchX = 0;
                        pitchY = 1;
                    }
                }
                int textX = basePixelX;
                const int textWidth = MeasureTextPx(content, textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily);
                if (space.textAnchor == "middle") {
                    textX -= textWidth / 2;
                } else if (space.textAnchor == "end" || ToLowerAscii(SvgProperty(node, "direction")) == "rtl") {
                    textX -= textWidth;
                }
                textX += SvgLength(space, SvgNumber(node, "dx", 0), true);
                std::string dominant = ToLowerAscii(SvgProperty(node, "dominant-baseline"));
                const std::string alignment = ToLowerAscii(SvgProperty(node, "alignment-baseline"));
                if (alignment == "hanging" || alignment == "before-edge" || alignment == "text-before-edge") {
                    dominant = "hanging";
                } else if (alignment == "middle" || alignment == "central") {
                    dominant = "middle";
                }
                int dominantDrop = textStyle.fontSizePx;
                if (dominant == "hanging" || dominant == "text-before-edge") {
                    dominantDrop = 0;
                } else if (dominant == "middle" || dominant == "central") {
                    dominantDrop = textStyle.fontSizePx / 2;
                }
                const std::string baselineShift = ToLowerAscii(SvgProperty(node, "baseline-shift"));
                int baseline = 0;
                if (baselineShift == "sub") {
                    baseline = textStyle.fontSizePx / 2;
                } else if (baselineShift == "super") {
                    baseline = -textStyle.fontSizePx / 2;
                } else if (!baselineShift.empty() && baselineShift != "baseline") {
                    baseline = -SvgLength(space, std::atof(baselineShift.c_str()), false);
                }
                const int textY = basePixelY + SvgLength(space, SvgNumber(node, "dy", 0), false) + baseline - dominantDrop;
                SvgFilter textShadow;
                const bool shadowText = ResolveFilter(node, space, textShadow) && textShadow.dropShadow;
                // Glyph bitmaps cannot be masked per pixel, so a clip-path is
                // applied through the intersection of the glyph's ink box with
                // the clip region's bounding box: exact for the rectangular
                // clips that dominate, conservative for every other shape.
                const bool textClipped = !space.clipScopes.empty();
                double textClipLeft = 0;
                double textClipTop = 0;
                double textClipRight = 0;
                double textClipBottom = 0;
                const bool textClipBox = textClipped && SvgClipBounds(space, textClipLeft, textClipTop, textClipRight, textClipBottom);
                auto emitGlyph = [&](int glyphX, int glyphY, const std::string& glyph, const ResolvedStyle& style) {
                    if (!textClipped) {
                        Text(glyphX, glyphY, glyph, style, fixed);
                        return;
                    }
                    if (!textClipBox) {
                        return;
                    }
                    const int advance = std::max(0, MeasureTextPx(glyph, style.fontSizePx, style.bold, style.italic, style.fontFamily));
                    const int left = std::max(static_cast<int>(std::floor(glyphX - 1.0)), static_cast<int>(std::floor(textClipLeft)));
                    const int top = std::max(static_cast<int>(std::floor(glyphY - style.fontSizePx - 1.0)), static_cast<int>(std::floor(textClipTop)));
                    const int right = std::min(static_cast<int>(std::ceil(glyphX + advance + 1.0)), static_cast<int>(std::ceil(textClipRight)));
                    const int bottom = std::min(static_cast<int>(std::ceil(glyphY + style.fontSizePx + 1.0)), static_cast<int>(std::ceil(textClipBottom)));
                    if (right <= left || bottom <= top) {
                        return;
                    }
                    Text(glyphX, glyphY, glyph, style, fixed, true, left, top, right - left, bottom - top);
                };
                auto paintGlyph = [&](int glyphX, int glyphY, const std::string& glyph, const ResolvedStyle& style) {
                    const double localX = static_cast<double>(glyphX - basePixelX);
                    const double localY = static_cast<double>(glyphY - basePixelY);
                    const int mappedX = basePixelX + static_cast<int>(std::lround(dirX * localX + pitchX * localY));
                    const int mappedY = basePixelY + static_cast<int>(std::lround(dirY * localX + pitchY * localY));
                    if (shadowText) {
                        ResolvedStyle shadowStyle = style;
                        shadowStyle.color = textShadow.floodColor;
                        shadowStyle.underline = false;
                        shadowStyle.strike = false;
                        emitGlyph(mappedX + textShadow.shadowDx, mappedY + textShadow.shadowDy, glyph, shadowStyle);
                    }
                    emitGlyph(mappedX, mappedY, glyph, style);
                };
                const std::string writingMode = ToLowerAscii(SvgProperty(node, "writing-mode"));
                if ((writingMode == "tb" || writingMode.find("vertical") != std::string::npos) && !content.empty()) {
                    int cursor = textY;
                    const std::string orientation = ToLowerAscii(SvgProperty(node, "glyph-orientation-vertical"));
                    const bool upright = orientation == "0" || orientation == "0deg" || orientation == "180" || orientation == "180deg";
                    for (std::size_t byte = 0; byte < content.size(); ++byte) {
                        const std::string glyph(1, content[byte]);
                        const int advance = upright ? textStyle.fontSizePx : std::max(1, MeasureTextPx(glyph, textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily));
                        paintGlyph(textX, cursor, glyph, textStyle);
                        cursor += advance;
                    }
                    RememberSvgLink(node, textX, textY, textStyle.fontSizePx, std::max(1, cursor - textY), fixed);
                } else {
                const int letterSpacing = SvgLength(space, std::atof(SvgProperty(node, "letter-spacing").c_str()), true);
                const int wordSpacing = SvgLength(space, std::atof(SvgProperty(node, "word-spacing").c_str()), true);
                const int textLength = AttributeValue(node, "textlength").empty() ? 0 : SvgLength(space, std::atof(AttributeValue(node, "textlength").c_str()), true);
                const bool glyphAdjust = ToLowerAscii(AttributeValue(node, "lengthadjust")) == "spacingandglyphs";
                if (glyphAdjust && textLength > 0 && textWidth > 0) {
                    textStyle.fontSizePx = std::max(1, textStyle.fontSizePx * textLength / textWidth);
                }
                int spacing = letterSpacing;
                if (!glyphAdjust && textLength > textWidth && content.size() > 1) {
                    spacing += (textLength - textWidth) / static_cast<int>(content.size() - 1);
                }
                auto stretchOf = [](const std::string& text) {
                    const std::string value = ToLowerAscii(text);
                    if (value == "ultra-condensed") {
                        return 0.5;
                    }
                    if (value == "extra-condensed") {
                        return 0.625;
                    }
                    if (value == "condensed") {
                        return 0.75;
                    }
                    if (value == "semi-condensed") {
                        return 0.875;
                    }
                    if (value == "semi-expanded") {
                        return 1.125;
                    }
                    if (value == "expanded") {
                        return 1.25;
                    }
                    if (value == "extra-expanded") {
                        return 1.5;
                    }
                    if (value == "ultra-expanded") {
                        return 2.0;
                    }
                    if (value.find('%') != std::string::npos) {
                        return std::atof(value.c_str()) / 100.0;
                    }
                    return 1.0;
                };
                std::string stretchText = SvgProperty(node, "font-stretch");
                if (stretchText.empty()) {
                    stretchText = SvgProperty(node, "font-width");
                }
                const double stretch = stretchOf(stretchText);
                const std::string horizontalOrientation = ToLowerAscii(SvgProperty(node, "glyph-orientation-horizontal"));
                const bool turnedGlyph = horizontalOrientation == "90" || horizontalOrientation == "90deg" || horizontalOrientation == "270" || horizontalOrientation == "270deg";
                const std::string textOverflow = ToLowerAscii(SvgProperty(node, "text-overflow"));
                if ((textOverflow == "ellipsis" || textOverflow == "ellipses") && whiteSpace != "pre" && whiteSpace != "pre-wrap" &&
                    !SvgProperty(node, "inline-size").empty() && !content.empty()) {
                    const int limit = SvgLength(space, std::atof(SvgProperty(node, "inline-size").c_str()), true);
                    const int ellipsisWidth = MeasureTextPx("...", textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily);
                    const std::string original = content;
                    while (!content.empty() && MeasureTextPx(content, textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily) + ellipsisWidth > limit) {
                        content.pop_back();
                    }
                    if (content.size() < original.size()) {
                        if (MeasureTextPx(content, textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily) + ellipsisWidth > limit) {
                            content = "...";
                        } else {
                            content += "...";
                        }
                    }
                }
                const bool smallCaps = ToLowerAscii(SvgProperty(node, "font-variant")).find("small-caps") != std::string::npos;
                if (turnedGlyph && !content.empty()) {
                    int cursor = textY;
                    for (std::size_t byte = 0; byte < content.size(); ++byte) {
                        const std::string glyph(1, content[byte]);
                        paintGlyph(textX, cursor, glyph, textStyle);
                        cursor += textStyle.fontSizePx;
                    }
                    RememberSvgLink(node, textX, textY, textStyle.fontSizePx, std::max(1, cursor - textY), fixed);
                } else if (std::abs(stretch - 1.0) > 0.01 && !content.empty()) {
                    int cursor = textX;
                    for (std::size_t byte = 0; byte < content.size(); ++byte) {
                        const std::string glyph(1, content[byte]);
                        const int advance = std::max(1, static_cast<int>(MeasureTextPx(glyph, textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily) * stretch + 0.5));
                        paintGlyph(cursor, textY, glyph, textStyle);
                        cursor += advance;
                    }
                    RememberSvgLink(node, textX, textY, std::max(1, cursor - textX), textStyle.fontSizePx, fixed);
                } else if (smallCaps && !content.empty()) {
                    int cursor = textX;
                    for (std::size_t byte = 0; byte < content.size(); ++byte) {
                        std::string glyph(1, content[byte]);
                        ResolvedStyle glyphStyle = textStyle;
                        if (glyph[0] >= 'a' && glyph[0] <= 'z') {
                            glyph[0] = static_cast<char>(glyph[0] - 'a' + 'A');
                            glyphStyle.fontSizePx = std::max(1, textStyle.fontSizePx * 7 / 10);
                        }
                        const int advance = std::max(1, MeasureTextPx(glyph, glyphStyle.fontSizePx, glyphStyle.bold, glyphStyle.italic, glyphStyle.fontFamily));
                        paintGlyph(cursor, textY, glyph, glyphStyle);
                        cursor += advance;
                    }
                    RememberSvgLink(node, textX, textY, std::max(1, cursor - textX), textStyle.fontSizePx, fixed);
                } else if ((letterSpacing != 0 || wordSpacing != 0 || (!glyphAdjust && textLength > 0)) && !content.empty()) {
                    int cursor = textX;
                    for (std::size_t byte = 0; byte < content.size(); ++byte) {
                        const std::string glyph(1, content[byte]);
                        const int advance = std::max(1, MeasureTextPx(glyph, textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily));
                        paintGlyph(cursor, textY, glyph, textStyle);
                        cursor += advance + spacing;
                        if (glyph == " ") {
                            cursor += wordSpacing;
                        }
                    }
                    RememberSvgLink(node, textX, textY, std::max(1, cursor - textX), textStyle.fontSizePx, fixed);
                } else {
                    paintGlyph(textX, textY, content, textStyle);
                    RememberSvgLink(node, textX, textY, std::max(1, textWidth), textStyle.fontSizePx, fixed);
                }
                }
            }
        }
        if (tag == "switch") {
            for (const std::unique_ptr<Node>& child : node.children) {
                if (child->type == Node::Type::Element && SvgSwitchMatches(*child)) {
                    PaintSvgTree(*child, space, fixed);
                    break;
                }
            }
        } else if (tag == "svg" || tag == "g" || tag == "a" || tag == "symbol" || tag == "defs" || tag == "marker" || tag == "text" ||
                   node.kind == ElementKind::Svg) {
            if (!SvgProperty(node, "filter").empty() && tag != "defs" && tag != "marker") {
                space.filterUrl = SvgProperty(node, "filter");
            }
            for (const std::unique_ptr<Node>& child : node.children) {
                if (child->type == Node::Type::Element) {
                    PaintSvgTree(*child, space, fixed);
                }
            }
        }
    }

    void PaintAtomic(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed) {
        AddBox(node, x, y, width, height, style, fixed);
        if (node.kind == ElementKind::Svg) {
            SvgSpace space;
            space.originX = x;
            space.originY = y;
            space.pixelWidth = width;
            space.pixelHeight = height;
            space.hasViewport = true;
            PaintSvgTree(node, space, fixed);
            if (style.border > 0) {
                Stroke(x, y, width, height, Color{0, 0, 0, 255}, fixed);
            }
            return;
        }
        const Color face{240, 240, 240, 255};
        const Color border{118, 118, 118, 255};
        const Color focus{0, 95, 204, 255};
        if (node.kind == ElementKind::Input) {
            const std::string type = InputType(node);
            if (type == "checkbox" || type == "radio") {
                Stroke(x, y, width, height, border, fixed);
                Fill(x + 1, y + 1, width - 2, height - 2, Color{255, 255, 255, 255}, fixed);
                if (node.checked) {
                    Fill(x + 3, y + 3, width - 6, height - 6, Color{0, 0, 0, 255}, fixed);
                }
                return;
            }
            if (type == "range") {
                Fill(x, y + height / 2, width, 2, border, fixed);
                const double minimum = std::atof(AttributeValue(node, "min").c_str());
                const double maximum = AttributeValue(node, "max").empty() ? 100.0 : std::atof(AttributeValue(node, "max").c_str());
                const double span = maximum - minimum;
                const std::string raw = node.liveValue.empty() ? AttributeValue(node, "value") : node.liveValue;
                const double value = raw.empty() ? (minimum + maximum) / 2.0 : std::atof(raw.c_str());
                const double ratio = span == 0 ? 0 : std::max(0.0, std::min(1.0, (value - minimum) / span));
                const int thumb = x + static_cast<int>(ratio * std::max(0, width - 8));
                Fill(thumb, y, 8, height, face, fixed);
                Stroke(thumb, y, 8, height, border, fixed);
                return;
            }
            if (type == "color") {
                Color swatch{0, 0, 0, 255};
                ParseColor(node.liveValue.empty() ? "#000000" : node.liveValue, swatch);
                Fill(x, y, width, height, swatch, fixed);
                Stroke(x, y, width, height, border, fixed);
                return;
            }
            if (type == "image" || type == "hidden") {
                if (type == "image") {
                    PaintReplaced(node, x, y, width, height, style, fixed);
                }
                return;
            }
            const bool hot = document->hover == &node;
            const Color edge = document->focus == &node ? focus : (hot ? Color{0, 120, 215, 255} : border);
            Fill(x, y, width, height, Color{255, 255, 255, 255}, fixed);
            Stroke(x, y, width, height, edge, fixed);
            PaintFieldValue(node, x, y, width, height, style, fixed);
            return;
        }
        if (node.kind == ElementKind::Textarea) {
            const bool hot = document->hover == &node;
            const Color edge = document->focus == &node ? focus : (hot ? Color{0, 120, 215, 255} : border);
            Fill(x, y, width, height, Color{255, 255, 255, 255}, fixed);
            Stroke(x, y, width, height, edge, fixed);
            PaintFieldValue(node, x, y, width, height, style, fixed);
            return;
        }
        if (node.kind == ElementKind::Select || node.kind == ElementKind::Button || node.kind == ElementKind::Geolocation) {
            const bool hot = document->hover == &node;
            const Color controlFace = hot ? Color{210, 227, 252, 255} : face;
            const Color edge = document->focus == &node ? focus : (hot ? Color{0, 120, 215, 255} : border);
            Fill(x, y, width, height, controlFace, fixed);
            Stroke(x, y, width, height, edge, fixed);
            Text(x + CONTROL_BORDER_PX + CONTROL_PADDING_X_PX, y + CONTROL_BORDER_PX + CONTROL_PADDING_Y_PX, ControlLabel(node), style, fixed);
            bool sizeOk = false;
            const int listSize = node.kind == ElementKind::Select ? ParsePx(AttributeValue(node, "size"), width, sizeOk) : 0;
            if (node.kind == ElementKind::Select && (node.selectExpanded || (sizeOk && listSize > 1))) {
                int optionY = sizeOk && listSize > 1 ? y : y + height;
                std::vector<Node*> stack{&node};
                while (!stack.empty()) {
                    Node* current = stack.back();
                    stack.pop_back();
                    for (const std::unique_ptr<Node>& child : current->children) {
                        if (child->kind == ElementKind::Option) {
                            Fill(x, optionY, width, LineHeightPx(style.fontSizePx), child->selected ? Color{0, 120, 215, 255} : Color{255, 255, 255, 255}, true);
                            ResolvedStyle optionStyle = style;
                            if (child->selected) {
                                optionStyle.color = Color{255, 255, 255, 255};
                            }
                            const std::string label = AttributeValue(*child, "label");
                            Text(x + 4, optionY, label.empty() ? CollapsedText(GatherText(*child), false) : label, optionStyle, true);
                            AddBox(*child, x, optionY, width, LineHeightPx(style.fontSizePx), optionStyle, true);
                            optionY += LineHeightPx(style.fontSizePx);
                        } else if (child->kind == ElementKind::Optgroup) {
                            stack.insert(stack.begin(), child.get());
                        }
                    }
                }
            }
            return;
        }
        if (node.kind == ElementKind::Marquee) {
            const std::string text = CollapsedText(GatherText(node), false);
            const int textWidth = std::max(1, MeasureTextPx(text, style.fontSizePx, style.bold, style.italic, style.fontFamily));
            const std::string direction = ToLowerAscii(AttributeValue(node, "direction"));
            const int travel = textWidth + width;
            int shift = travel == 0 ? 0 : node.marqueeOffset % travel;
            const std::string behavior = ToLowerAscii(AttributeValue(node, "behavior"));
            if (behavior == "alternate") {
                const int cycle = shift % (travel * 2);
                shift = cycle < travel ? cycle : travel * 2 - cycle;
            } else if (behavior == "slide") {
                shift = std::min(shift, width);
            }
            int textX = x + width - shift;
            if (direction == "right") {
                textX = x - textWidth + shift;
            }
            if (style.background.alpha != 0) {
                Fill(x, y, width, height, style.background, fixed);
            }
            Text(textX, y, text, style, fixed, true, x, y, width, height);
            return;
        }
        PaintReplaced(node, x, y, width, height, style, fixed);
    }

    void PaintFraction(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed) {
        std::string numerator;
        std::string denominator;
        for (const std::unique_ptr<Node>& part : node.children) {
            if (part->type != Node::Type::Element) {
                continue;
            }
            if (numerator.empty()) {
                numerator = CollapsedText(GatherText(*part), false);
            } else if (denominator.empty()) {
                denominator = CollapsedText(GatherText(*part), false);
            }
        }
        const int line = LineHeightPx(style.fontSizePx);
        const int thickness = AttributeValue(node, "linethickness").empty() ? 1 : std::max(0, std::atoi(AttributeValue(node, "linethickness").c_str()));
        const int numeratorWidth = MeasureTextPx(numerator, style.fontSizePx, style.bold, style.italic, style.fontFamily);
        const int denominatorWidth = MeasureTextPx(denominator, style.fontSizePx, style.bold, style.italic, style.fontFamily);
        Text(x + std::max(0, (width - numeratorWidth) / 2), y, numerator, style, fixed);
        if (thickness > 0) {
            Fill(x, y + line, width, thickness, style.color, fixed);
        }
        Text(x + std::max(0, (width - denominatorWidth) / 2), y + line + thickness, denominator, style, fixed);
        AddBox(node, x, y, width, height, style, fixed);
    }

    void MathChildren(const Node& node, std::string& first, std::string& second, std::string& third) {
        first.clear();
        second.clear();
        third.clear();
        for (const std::unique_ptr<Node>& part : node.children) {
            if (part->type != Node::Type::Element) {
                continue;
            }
            const std::string text = CollapsedText(GatherText(*part), false);
            if (first.empty()) {
                first = text;
            } else if (second.empty()) {
                second = text;
            } else if (third.empty()) {
                third = text;
            }
        }
    }

    bool MathFlag(const Node& node, const char* name) {
        const std::string value = ToLowerAscii(AttributeValue(node, name));
        return value == "true" || value == "1";
    }

    void PaintRadical(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed, bool indexed) {
        std::string base;
        std::string index;
        std::string unused;
        MathChildren(node, base, index, unused);
        const int line = LineHeightPx(style.fontSizePx);
        const int scriptSize = std::max(1, style.fontSizePx * 7 / 10);
        const int scriptLine = LineHeightPx(scriptSize);
        const int gap = std::max(4, style.fontSizePx / 2);
        const int baseY = indexed ? y + scriptLine / 2 : y;
        if (indexed && !index.empty()) {
            ResolvedStyle indexStyle = style;
            indexStyle.fontSizePx = scriptSize;
            Text(x, y, index, indexStyle, fixed);
        }
        Fill(x, baseY, 1, std::max(1, line), style.color, fixed);
        Fill(x, baseY, width, 1, style.color, fixed);
        Text(x + gap, baseY, base, style, fixed);
        AddBox(node, x, y, width, height, style, fixed);
    }

    void PaintScript(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed, bool superscript) {
        std::string base;
        std::string script;
        std::string unused;
        MathChildren(node, base, script, unused);
        const int line = LineHeightPx(style.fontSizePx);
        const int scriptSize = std::max(1, style.fontSizePx * 7 / 10);
        ResolvedStyle scriptStyle = style;
        scriptStyle.fontSizePx = scriptSize;
        const int baseWidth = MeasureTextPx(base, style.fontSizePx, style.bold, style.italic, style.fontFamily);
        const int shift = std::max(0, std::atoi(AttributeValue(node, superscript ? "superscriptshift" : "subscriptshift").c_str()));
        if (superscript) {
            const int baseDrop = LineHeightPx(scriptSize) / 2 + shift;
            Text(x, y + baseDrop, base, style, fixed);
            Text(x + baseWidth, y, script, scriptStyle, fixed);
        } else {
            Text(x, y, base, style, fixed);
            Text(x + baseWidth, y + line / 2 + shift, script, scriptStyle, fixed);
        }
        AddBox(node, x, y, width, height, style, fixed);
    }

    void PaintSubSup(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed) {
        std::string base;
        std::string subscript;
        std::string superscript;
        MathChildren(node, base, subscript, superscript);
        const int line = LineHeightPx(style.fontSizePx);
        const int scriptSize = std::max(1, style.fontSizePx * 7 / 10);
        ResolvedStyle scriptStyle = style;
        scriptStyle.fontSizePx = scriptSize;
        const int baseWidth = MeasureTextPx(base, style.fontSizePx, style.bold, style.italic, style.fontFamily);
        const int superShift = std::max(0, std::atoi(AttributeValue(node, "superscriptshift").c_str()));
        const int subShift = std::max(0, std::atoi(AttributeValue(node, "subscriptshift").c_str()));
        const int baseDrop = LineHeightPx(scriptSize) / 2 + superShift;
        Text(x, y + baseDrop, base, style, fixed);
        Text(x + baseWidth, y, superscript, scriptStyle, fixed);
        Text(x + baseWidth, y + baseDrop + line / 2 + subShift, subscript, scriptStyle, fixed);
        AddBox(node, x, y, width, height, style, fixed);
    }

    void PaintLimits(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed, bool over, bool under) {
        std::string base;
        std::string second;
        std::string third;
        MathChildren(node, base, second, third);
        const std::string underscript = over && under ? second : under ? second : std::string();
        const std::string overscript = over && under ? third : over ? second : std::string();
        const int line = LineHeightPx(style.fontSizePx);
        const bool accent = over && MathFlag(node, "accent");
        const bool accentUnder = under && MathFlag(node, "accentunder");
        const int overSize = accent ? style.fontSizePx : std::max(1, style.fontSizePx * 7 / 10);
        const int underSize = accentUnder ? style.fontSizePx : std::max(1, style.fontSizePx * 7 / 10);
        const int overLine = over ? LineHeightPx(overSize) : 0;
        const int underLine = under ? LineHeightPx(underSize) : 0;
        const int overGap = accent ? 0 : over ? overLine / 2 : 0;
        const int underGap = accentUnder ? 0 : under ? underLine / 2 : 0;
        ResolvedStyle overStyle = style;
        overStyle.fontSizePx = overSize;
        ResolvedStyle underStyle = style;
        underStyle.fontSizePx = underSize;
        bool operatorBase = false;
        for (const std::unique_ptr<Node>& part : node.children) {
            if (part->type != Node::Type::Element) {
                continue;
            }
            operatorBase = ToLowerAscii(part->tagName) == "mo";
            break;
        }
        int displayMode = -1;
        for (const Node* cursor = &node; cursor != nullptr; cursor = cursor->parent) {
            const std::string value = ToLowerAscii(AttributeValue(*cursor, "displaystyle"));
            if (value == "true" || value == "1") {
                displayMode = 1;
                break;
            }
            if (value == "false" || value == "0") {
                displayMode = 0;
                break;
            }
            if (ToLowerAscii(cursor->tagName) == "math" && ToLowerAscii(AttributeValue(*cursor, "display")) == "block") {
                displayMode = 1;
                break;
            }
        }
        if (operatorBase && displayMode == 0) {
            ResolvedStyle scriptStyle = style;
            scriptStyle.fontSizePx = std::max(1, style.fontSizePx * 7 / 10);
            const int baseWidth = MeasureTextPx(base, style.fontSizePx, style.bold, style.italic, style.fontFamily);
            const int scriptLine = LineHeightPx(scriptStyle.fontSizePx);
            if (over && under) {
                const int baseDrop = scriptLine / 2;
                Text(x, y + baseDrop, base, style, fixed);
                Text(x + baseWidth, y, overscript, scriptStyle, fixed);
                Text(x + baseWidth, y + baseDrop + line / 2, underscript, scriptStyle, fixed);
            } else if (over) {
                Text(x, y + scriptLine / 2, base, style, fixed);
                Text(x + baseWidth, y, overscript, scriptStyle, fixed);
            } else {
                Text(x, y, base, style, fixed);
                Text(x + baseWidth, y + line / 2, underscript, scriptStyle, fixed);
            }
            AddBox(node, x, y, width, height, style, fixed);
            return;
        }
        ResolvedStyle baseStyle = style;
        if (operatorBase && displayMode == 1) {
            baseStyle.fontSizePx = std::max(1, baseStyle.fontSizePx * 3 / 2);
        }
        const int baseWidth = MeasureTextPx(base, baseStyle.fontSizePx, baseStyle.bold, baseStyle.italic, baseStyle.fontFamily);
        const int overWidth = MeasureTextPx(overscript, overSize, style.bold, style.italic, style.fontFamily);
        const int underWidth = MeasureTextPx(underscript, underSize, style.bold, style.italic, style.fontFamily);
        if (over) {
            Text(x + std::max(0, (width - overWidth) / 2), y, overscript, overStyle, fixed);
        }
        Text(x + std::max(0, (width - baseWidth) / 2), y + overLine + overGap, base, baseStyle, fixed);
        if (under) {
            Text(x + std::max(0, (width - underWidth) / 2), y + overLine + overGap + line + underGap, underscript, underStyle, fixed);
        }
        AddBox(node, x, y, width, height, style, fixed);
    }

    int MathLength(const std::string& raw, int base) const {
        if (raw.empty()) {
            return 0;
        }
        const std::string lower = ToLowerAscii(raw);
        std::size_t index = 0;
        const bool plus = lower[0] == '+';
        const bool minus = lower[0] == '-';
        if (plus || minus) {
            ++index;
        }
        char* end = nullptr;
        const double number = std::strtod(lower.c_str() + index, &end);
        const std::string unit = end == nullptr ? std::string() : std::string(end);
        const bool percent = unit.find('%') != std::string::npos;
        const bool pseudo = unit.find("width") != std::string::npos || unit.find("height") != std::string::npos || unit.find("depth") != std::string::npos;
        int magnitude = percent ? static_cast<int>(number / 100.0 * static_cast<double>(base) + (number < 0 ? -0.5 : 0.5))
                                : pseudo ? static_cast<int>(number * static_cast<double>(base) + (number < 0 ? -0.5 : 0.5))
                                         : static_cast<int>(number + (number < 0 ? -0.5 : 0.5));
        if (minus) {
            magnitude = -std::abs(magnitude);
        }
        if (plus || minus) {
            return base + magnitude;
        }
        return magnitude;
    }

    void MathParts(const Node& node, std::vector<std::string>& parts) {
        parts.clear();
        for (const std::unique_ptr<Node>& part : node.children) {
            if (part->type != Node::Type::Element) {
                continue;
            }
            parts.push_back(CollapsedText(GatherText(*part), false));
        }
    }

    int MathRunWidth(const std::vector<std::string>& parts, const ResolvedStyle& style) {
        int width = 0;
        for (const std::string& part : parts) {
            width += MeasureTextPx(part, style.fontSizePx, style.bold, style.italic, style.fontFamily);
        }
        return width;
    }

    void PaintMathRun(int x, int y, const std::vector<std::string>& parts, const ResolvedStyle& style, bool fixed) {
        int cursor = x;
        for (const std::string& part : parts) {
            if (!part.empty()) {
                Text(cursor, y, part, style, fixed);
            }
            cursor += MeasureTextPx(part, style.fontSizePx, style.bold, style.italic, style.fontFamily);
        }
    }

    bool HasNotation(const std::string& notation, const char* token) {
        std::string word;
        const std::string lower = ToLowerAscii(notation);
        for (std::size_t index = 0; index <= lower.size(); ++index) {
            const char letter = index < lower.size() ? lower[index] : ' ';
            if (letter == ' ' || letter == '\t' || letter == '\n') {
                if (word == token) {
                    return true;
                }
                word.clear();
            } else {
                word.push_back(letter);
            }
        }
        return false;
    }

    void Hairline(int x0, int y0, int x1, int y1, Color color, bool fixed) {
        const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        if (steps == 0) {
            Fill(x0, y0, 1, 1, color, fixed);
            return;
        }
        for (int step = 0; step <= steps; ++step) {
            Fill(x0 + (x1 - x0) * step / steps, y0 + (y1 - y0) * step / steps, 1, 1, color, fixed);
        }
    }

    void PaintEnclose(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed) {
        std::vector<std::string> parts;
        MathParts(node, parts);
        const int pad = 3;
        PaintMathRun(x + pad, y, parts, style, fixed);
        const std::string notation = AttributeValue(node, "notation");
        const std::string marks = notation.empty() ? std::string("longdiv") : notation;
        const bool rounded = HasNotation(marks, "roundedbox");
        const bool box = HasNotation(marks, "box") || rounded;
        const int inset = rounded ? 2 : 0;
        const bool left = box || HasNotation(marks, "left") || HasNotation(marks, "longdiv") || HasNotation(marks, "phasorangle");
        const bool right = box || HasNotation(marks, "right") || HasNotation(marks, "actuarial") || HasNotation(marks, "madruwb");
        const bool top = box || HasNotation(marks, "top") || HasNotation(marks, "longdiv") || HasNotation(marks, "actuarial");
        const bool bottom = box || HasNotation(marks, "bottom") || HasNotation(marks, "madruwb") || HasNotation(marks, "phasorangle");
        if (top) {
            Fill(x + inset, y, std::max(1, width - inset * 2), 1, style.color, fixed);
        }
        if (bottom) {
            Fill(x + inset, y + height - 1, std::max(1, width - inset * 2), 1, style.color, fixed);
        }
        if (left) {
            Fill(x, y + inset, 1, std::max(1, height - inset * 2), style.color, fixed);
        }
        if (right) {
            Fill(x + width - 1, y + inset, 1, std::max(1, height - inset * 2), style.color, fixed);
        }
        if (HasNotation(marks, "horizontalstrike")) {
            Fill(x, y + height / 2, width, 1, style.color, fixed);
        }
        if (HasNotation(marks, "verticalstrike")) {
            Fill(x + width / 2, y, 1, height, style.color, fixed);
        }
        if (HasNotation(marks, "updiagonalstrike") || HasNotation(marks, "updiagonalarrow")) {
            Hairline(x, y + height - 1, x + width - 1, y, style.color, fixed);
        }
        if (HasNotation(marks, "downdiagonalstrike")) {
            Hairline(x, y, x + width - 1, y + height - 1, style.color, fixed);
        }
        if (HasNotation(marks, "phasorangle")) {
            Hairline(x, y + height - 1, x + width - 1, y, style.color, fixed);
        }
        if (HasNotation(marks, "circle")) {
            const int steps = std::max(16, width + height);
            const double cx = static_cast<double>(x) + static_cast<double>(width) / 2.0;
            const double cy = static_cast<double>(y) + static_cast<double>(height) / 2.0;
            const double rx = std::max(1.0, static_cast<double>(width) / 2.0 - 0.5);
            const double ry = std::max(1.0, static_cast<double>(height) / 2.0 - 0.5);
            Fill(static_cast<int>(cx), static_cast<int>(cy - ry), 1, 1, style.color, fixed);
            Fill(static_cast<int>(cx), static_cast<int>(cy + ry), 1, 1, style.color, fixed);
            Fill(static_cast<int>(cx - rx), static_cast<int>(cy), 1, 1, style.color, fixed);
            Fill(static_cast<int>(cx + rx), static_cast<int>(cy), 1, 1, style.color, fixed);
            for (int step = 0; step < steps; ++step) {
                const double angle = 6.28318530718 * static_cast<double>(step) / static_cast<double>(steps);
                Fill(static_cast<int>(cx + rx * std::cos(angle)), static_cast<int>(cy + ry * std::sin(angle)), 1, 1, style.color, fixed);
            }
        }
        AddBox(node, x, y, width, height, style, fixed);
    }

    void PaintPadded(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed) {
        std::vector<std::string> parts;
        MathParts(node, parts);
        const int lspace = MathLength(AttributeValue(node, "lspace"), 0);
        const int voffset = MathLength(AttributeValue(node, "voffset"), 0);
        PaintMathRun(x + lspace, y - voffset, parts, style, fixed);
        AddBox(node, x, y, width, height, style, fixed);
    }

    void PaintFenced(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed) {
        std::vector<std::string> parts;
        MathParts(node, parts);
        const std::string open = HasAttribute(node, "open") ? AttributeValue(node, "open") : "(";
        const std::string close = HasAttribute(node, "close") ? AttributeValue(node, "close") : ")";
        std::string separators = HasAttribute(node, "separators") ? AttributeValue(node, "separators") : ",";
        std::string marks;
        for (char letter : separators) {
            if (letter != ' ' && letter != '\t' && letter != '\n') {
                marks.push_back(letter);
            }
        }
        int cursor = x;
        if (!open.empty()) {
            Text(cursor, y, open, style, fixed);
            cursor += MeasureTextPx(open, style.fontSizePx, style.bold, style.italic, style.fontFamily);
        }
        for (std::size_t index = 0; index < parts.size(); ++index) {
            if (!parts[index].empty()) {
                Text(cursor, y, parts[index], style, fixed);
            }
            cursor += MeasureTextPx(parts[index], style.fontSizePx, style.bold, style.italic, style.fontFamily);
            if (index + 1 < parts.size() && !marks.empty()) {
                const std::string separator(1, marks[std::min(index, marks.size() - 1)]);
                Text(cursor, y, separator, style, fixed);
                cursor += MeasureTextPx(separator, style.fontSizePx, style.bold, style.italic, style.fontFamily);
            }
        }
        if (!close.empty()) {
            Text(cursor, y, close, style, fixed);
        }
        AddBox(node, x, y, width, height, style, fixed);
    }

    void MathTokens(const std::string& raw, std::vector<std::string>& tokens) const {
        tokens.clear();
        std::string word;
        const std::string lower = ToLowerAscii(raw);
        for (std::size_t index = 0; index <= lower.size(); ++index) {
            const char letter = index < lower.size() ? lower[index] : ' ';
            if (letter == ' ' || letter == '\t' || letter == '\n') {
                if (!word.empty()) {
                    tokens.push_back(word);
                }
                word.clear();
            } else {
                word.push_back(letter);
            }
        }
    }

    const std::string& TokenAt(const std::vector<std::string>& tokens, std::size_t index) const {
        static const std::string empty;
        if (tokens.empty()) {
            return empty;
        }
        return index < tokens.size() ? tokens[index] : tokens.back();
    }

    void PaintMathRule(int x, int y, int width, int height, bool dashed, Color color, bool fixed) {
        if (width <= 0 || height <= 0) {
            return;
        }
        if (!dashed) {
            Fill(x, y, width, height, color, fixed);
            return;
        }
        if (width >= height) {
            for (int step = 0; step < width; step += 4) {
                Fill(x + step, y, std::min(2, width - step), height, color, fixed);
            }
        } else {
            for (int step = 0; step < height; step += 4) {
                Fill(x, y + step, width, std::min(2, height - step), color, fixed);
            }
        }
    }

    void ReadMathTable(const Node& node, std::vector<std::vector<std::string>>& cells, std::vector<std::vector<int>>& spans,         std::vector<std::vector<std::string>>& cellAlign, std::vector<std::string>& rowAlign, std::vector<std::string>& rowColumnAlign) const {
        cells.clear();
        spans.clear();
        cellAlign.clear();
        rowAlign.clear();
        rowColumnAlign.clear();
        for (const std::unique_ptr<Node>& child : node.children) {
            if (child->type != Node::Type::Element) {
                continue;
            }
            const std::string tag = ToLowerAscii(child->tagName);
            if (tag != "mtr" && tag != "mtd") {
                continue;
            }
            std::vector<std::string> row;
            std::vector<int> rowSpans;
            std::vector<std::string> aligns;
            const Node* rowNode = child.get();
            if (tag == "mtd") {
                row.push_back(CollapsedText(GatherText(*child), false));
                rowSpans.push_back(std::max(1, std::atoi(AttributeValue(*child, "columnspan").c_str())));
                aligns.push_back(ToLowerAscii(AttributeValue(*child, "columnalign")));
                rowAlign.push_back(ToLowerAscii(AttributeValue(*child, "rowalign")));
                rowColumnAlign.push_back(ToLowerAscii(AttributeValue(*child, "columnalign")));
            } else {
                for (const std::unique_ptr<Node>& cellNode : rowNode->children) {
                    if (cellNode->type != Node::Type::Element) {
                        continue;
                    }
                    row.push_back(CollapsedText(GatherText(*cellNode), false));
                    const int span = std::max(1, std::atoi(AttributeValue(*cellNode, "columnspan").c_str()));
                    rowSpans.push_back(span);
                    const std::string own = ToLowerAscii(AttributeValue(*cellNode, "columnalign"));
                    aligns.push_back(own);
                }
                rowAlign.push_back(ToLowerAscii(AttributeValue(*rowNode, "rowalign")));
                rowColumnAlign.push_back(ToLowerAscii(AttributeValue(*rowNode, "columnalign")));
            }
            if (!row.empty()) {
                cells.push_back(row);
                spans.push_back(rowSpans);
                cellAlign.push_back(aligns);
            }
        }
    }

    void MeasureMathTable(const Node& node, const ResolvedStyle& style, int& width, int& height, std::vector<int>& columnWidths, std::vector<int>& columnX, int& rowHeight,
        int& rowGap, int& origin, std::vector<std::vector<std::string>>& cells, std::vector<std::vector<int>>& spans) const {
        std::vector<std::vector<std::string>> cellAlign;
        std::vector<std::string> rowAlign;
        std::vector<std::string> rowColumnAlign;
        ReadMathTable(node, cells, spans, cellAlign, rowAlign, rowColumnAlign);
        int columns = 0;
        for (const std::vector<int>& rowSpans : spans) {
            int count = 0;
            for (int span : rowSpans) {
                count += span;
            }
            columns = std::max(columns, count);
        }
        columnWidths.assign(static_cast<std::size_t>(std::max(1, columns)), 0);
        for (std::size_t row = 0; row < cells.size(); ++row) {
            int column = 0;
            for (std::size_t cell = 0; cell < cells[row].size(); ++cell) {
                const int span = spans[row][cell];
                if (span == 1 && column < columns) {
                    const int textWidth = MeasureTextPx(cells[row][cell], style.fontSizePx, style.bold, style.italic, style.fontFamily);
                    columnWidths[static_cast<std::size_t>(column)] = std::max(columnWidths[static_cast<std::size_t>(column)], textWidth);
                }
                column += span;
            }
        }
        const int spacing = std::max(0, HasAttribute(node, "columnspacing") ? MathLength(AttributeValue(node, "columnspacing"), style.fontSizePx) : style.fontSizePx / 2);
        for (std::size_t row = 0; row < cells.size(); ++row) {
            int column = 0;
            for (std::size_t cell = 0; cell < cells[row].size(); ++cell) {
                const int span = spans[row][cell];
                if (span > 1 && column < columns) {
                    const int used = std::min(span, columns - column);
                    int sum = spacing * std::max(0, used - 1);
                    for (int index = 0; index < used; ++index) {
                        sum += columnWidths[static_cast<std::size_t>(column + index)];
                    }
                    const int textWidth = MeasureTextPx(cells[row][cell], style.fontSizePx, style.bold, style.italic, style.fontFamily);
                    if (textWidth > sum) {
                        columnWidths[static_cast<std::size_t>(column + used - 1)] += textWidth - sum;
                    }
                }
                column += span;
            }
        }
        rowGap = std::max(0, HasAttribute(node, "rowspacing") ? MathLength(AttributeValue(node, "rowspacing"), style.fontSizePx) : 2);
        rowHeight = LineHeightPx(style.fontSizePx);
        const std::string frame = ToLowerAscii(AttributeValue(node, "frame"));
        origin = frame == "solid" || frame == "dashed" ? 2 : 0;
        if (HasAttribute(node, "framespacing")) {
            std::vector<std::string> frameSpace;
            MathTokens(AttributeValue(node, "framespacing"), frameSpace);
            if (!frameSpace.empty()) {
                origin += std::max(0, MathLength(frameSpace[0], style.fontSizePx));
            }
        }
        int contentWidth = origin * 2;
        for (int column = 0; column < columns; ++column) {
            contentWidth += columnWidths[static_cast<std::size_t>(column)];
            if (column + 1 < columns) {
                contentWidth += spacing;
            }
        }
        if (HasAttribute(node, "width")) {
            const int requested = MathLength(AttributeValue(node, "width"), contentWidth);
            if (requested > contentWidth && columns > 0) {
                columnWidths[static_cast<std::size_t>(columns - 1)] += requested - contentWidth;
                contentWidth = requested;
            }
        }
        columnX.resize(static_cast<std::size_t>(std::max(1, columns)));
        int cursor = origin;
        for (int column = 0; column < columns; ++column) {
            columnX[static_cast<std::size_t>(column)] = cursor;
            cursor += columnWidths[static_cast<std::size_t>(column)] + (column + 1 < columns ? spacing : 0);
        }
        const int rows = std::max(1, static_cast<int>(cells.size()));
        width = std::max(1, contentWidth);
        height = std::max(rowHeight, origin * 2 + rows * rowHeight + std::max(0, rows - 1) * rowGap);
    }

    void PaintTable(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed) {
        std::vector<int> columnWidths;
        std::vector<int> columnX;
        std::vector<std::vector<std::string>> cells;
        std::vector<std::vector<int>> spans;
        int rowHeight = 0;
        int rowGap = 0;
        int origin = 0;
        int measuredWidth = width;
        int measuredHeight = height;
        MeasureMathTable(node, style, measuredWidth, measuredHeight, columnWidths, columnX, rowHeight, rowGap, origin, cells, spans);
        std::vector<std::string> columnAlign;
        std::vector<std::string> rowLines;
        std::vector<std::string> columnLines;
        MathTokens(AttributeValue(node, "columnalign"), columnAlign);
        MathTokens(AttributeValue(node, "rowlines"), rowLines);
        MathTokens(AttributeValue(node, "columnlines"), columnLines);
        std::vector<std::vector<std::string>> paintCells;
        std::vector<std::vector<int>> paintSpans;
        std::vector<std::vector<std::string>> cellAlign;
        std::vector<std::string> rowAlign;
        std::vector<std::string> rowColumnAlign;
        ReadMathTable(node, paintCells, paintSpans, cellAlign, rowAlign, rowColumnAlign);
        const std::string frame = ToLowerAscii(AttributeValue(node, "frame"));
        if (frame == "solid" || frame == "dashed") {
            const bool dashed = frame == "dashed";
            PaintMathRule(x, y, width, 1, dashed, style.color, fixed);
            PaintMathRule(x, y + height - 1, width, 1, dashed, style.color, fixed);
            PaintMathRule(x, y, 1, height, dashed, style.color, fixed);
            PaintMathRule(x + width - 1, y, 1, height, dashed, style.color, fixed);
        }
        for (std::size_t row = 0; row < cells.size(); ++row) {
            const int rowY = y + origin + static_cast<int>(row) * (rowHeight + rowGap);
            if (row + 1 < cells.size()) {
                const std::string line = TokenAt(rowLines, row);
                if (line == "solid" || line == "dashed") {
                    PaintMathRule(x, rowY + rowHeight, width, 1, line == "dashed", style.color, fixed);
                }
            }
            int column = 0;
            for (std::size_t cell = 0; cell < cells[row].size(); ++cell) {
                const int span = spans[row][cell];
                const int used = std::min(span, std::max(0, static_cast<int>(columnWidths.size()) - column));
                if (used <= 0) {
                    break;
                }
                int spanWidth = 0;
                for (int index = 0; index < used; ++index) {
                    spanWidth += columnWidths[static_cast<std::size_t>(column + index)];
                }
                if (used > 1 && column + used - 1 < static_cast<int>(columnX.size())) {
                    spanWidth = columnX[static_cast<std::size_t>(column + used - 1)] + columnWidths[static_cast<std::size_t>(column + used - 1)] - columnX[static_cast<std::size_t>(column)];
                }
                if (column + 1 < static_cast<int>(columnWidths.size())) {
                    const std::string line = TokenAt(columnLines, static_cast<std::size_t>(column));
                    if ((line == "solid" || line == "dashed") && span == 1) {
                        PaintMathRule(x + columnX[static_cast<std::size_t>(column + 1)] - 1, y, 1, height, line == "dashed", style.color, fixed);
                    }
                }
                std::string align = cell < cellAlign[row].size() ? cellAlign[row][cell] : std::string();
                if (align.empty() && row < rowColumnAlign.size()) {
                    std::vector<std::string> rowTokens;
                    MathTokens(rowColumnAlign[row], rowTokens);
                    align = TokenAt(rowTokens, static_cast<std::size_t>(column));
                }
                if (align.empty()) {
                    align = TokenAt(columnAlign, static_cast<std::size_t>(column));
                }
                const std::string text = cells[row][cell];
                const int textWidth = MeasureTextPx(text, style.fontSizePx, style.bold, style.italic, style.fontFamily);
                int textX = x + columnX[static_cast<std::size_t>(column)];
                if (align == "right") {
                    textX += std::max(0, spanWidth - textWidth);
                } else if (align != "left") {
                    textX += std::max(0, (spanWidth - textWidth) / 2);
                }
                Text(textX, rowY, text, style, fixed);
                column += span;
            }
        }
        AddBox(node, x, y, width, height, style, fixed);
    }

    void PaintMultiscripts(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed) {
        std::string base;
        std::vector<std::string> postSub;
        std::vector<std::string> postSup;
        std::vector<std::string> preSub;
        std::vector<std::string> preSup;
        bool baseSet = false;
        bool prescripts = false;
        bool subscript = true;
        for (const std::unique_ptr<Node>& part : node.children) {
            if (part->type != Node::Type::Element) {
                continue;
            }
            if (ToLowerAscii(part->tagName) == "mprescripts") {
                prescripts = true;
                subscript = true;
                continue;
            }
            const std::string text = CollapsedText(GatherText(*part), false);
            if (!baseSet) {
                base = text;
                baseSet = true;
                continue;
            }
            std::vector<std::string>& target = prescripts ? (subscript ? preSub : preSup) : (subscript ? postSub : postSup);
            target.push_back(text);
            subscript = !subscript;
        }
        const int line = LineHeightPx(style.fontSizePx);
        const int scriptSize = std::max(1, style.fontSizePx * 7 / 10);
        ResolvedStyle scriptStyle = style;
        scriptStyle.fontSizePx = scriptSize;
        const int superShift = std::max(0, std::atoi(AttributeValue(node, "superscriptshift").c_str()));
        const int subShift = std::max(0, std::atoi(AttributeValue(node, "subscriptshift").c_str()));
        const int baseDrop = LineHeightPx(scriptSize) / 2 + superShift;
        const auto pairWidth = [&](const std::vector<std::string>& subs, const std::vector<std::string>& sups) {
            int total = 0;
            const std::size_t count = std::max(subs.size(), sups.size());
            for (std::size_t index = 0; index < count; ++index) {
                const int subWidth = index < subs.size() ? MeasureTextPx(subs[index], scriptSize, style.bold, style.italic, style.fontFamily) : 0;
                const int supWidth = index < sups.size() ? MeasureTextPx(sups[index], scriptSize, style.bold, style.italic, style.fontFamily) : 0;
                total += std::max(subWidth, supWidth);
            }
            return total;
        };
        const int preWidth = pairWidth(preSub, preSup);
        const int baseWidth = MeasureTextPx(base, style.fontSizePx, style.bold, style.italic, style.fontFamily);
        const auto paintPairs = [&](int cursor, const std::vector<std::string>& subs, const std::vector<std::string>& sups, bool forward) {
            const std::size_t count = std::max(subs.size(), sups.size());
            for (std::size_t step = 0; step < count; ++step) {
                const std::size_t index = forward ? step : count - 1 - step;
                const std::string sub = index < subs.size() ? subs[index] : std::string();
                const std::string sup = index < sups.size() ? sups[index] : std::string();
                const int subWidth = MeasureTextPx(sub, scriptSize, style.bold, style.italic, style.fontFamily);
                const int supWidth = MeasureTextPx(sup, scriptSize, style.bold, style.italic, style.fontFamily);
                const int pair = std::max(subWidth, supWidth);
                if (!forward) {
                    cursor -= pair;
                }
                if (!sup.empty()) {
                    Text(cursor, y, sup, scriptStyle, fixed);
                }
                if (!sub.empty()) {
                    Text(cursor, y + baseDrop + line / 2 + subShift, sub, scriptStyle, fixed);
                }
                if (forward) {
                    cursor += pair;
                }
            }
        };
        paintPairs(x + preWidth, preSub, preSup, false);
        Text(x + preWidth, y + baseDrop, base, style, fixed);
        paintPairs(x + preWidth + baseWidth, postSub, postSup, true);
        AddBox(node, x, y, width, height, style, fixed);
    }

    void LayoutLines(Node& block, int x, int y, int width, const ResolvedStyle& style, bool fixed, int& usedHeight, bool paint) {
        std::vector<Piece> pieces;
        CollectInlines(block, style, width, pieces, true);
        int cursorY = y;
        std::size_t index = 0;
        bool firstLine = true;
        bool afterForced = false;
        while (index < pieces.size()) {
            int lineWidth = 0;
            int lineHeight = style.lineHeightPx > 0 ? style.lineHeightPx : LineHeightPx(style.fontSizePx);
            std::size_t end = index;
            while (end < pieces.size() && !pieces[end].lineBreak) {
                if (!style.nowrap && !style.pre && !pieces[end].style.nowrap && lineWidth > 0 && lineWidth + pieces[end].width > width) {
                    break;
                }
                lineWidth += pieces[end].width;
                lineHeight = std::max(lineHeight, pieces[end].height);
                ++end;
                if (end < pieces.size() && pieces[end - 1].lineBreak) {
                    break;
                }
            }
            int offset = 0;
            if (style.textAlign == 1) {
                offset = std::max(0, (width - lineWidth) / 2);
            } else if (style.textAlign == 2 || style.rtl) {
                offset = std::max(0, width - lineWidth);
            }
            int indent = style.textIndentMilli >= 0 ? width * style.textIndentMilli / 1000 : style.textIndentPx;
            bool indented = firstLine || (style.textIndentEachLine && afterForced);
            if (style.textIndentHanging) {
                indented = !indented;
            }
            if (!indented) {
                indent = 0;
            }
            int cursorX = x + offset + indent;
            for (std::size_t pieceIndex = index; pieceIndex < end; ++pieceIndex) {
                Piece& piece = pieces[pieceIndex];
                const std::string mathTag = piece.node == nullptr ? std::string() : ToLowerAscii(piece.node->tagName);
                if (paint && mathTag == "mfrac") {
                    PaintFraction(*piece.node, cursorX, cursorY, piece.width, piece.height, piece.style, fixed);
                } else if (paint && (mathTag == "msqrt" || mathTag == "mroot")) {
                    PaintRadical(*piece.node, cursorX, cursorY, piece.width, piece.height, piece.style, fixed, mathTag == "mroot");
                } else if (paint && (mathTag == "msup" || mathTag == "msub")) {
                    PaintScript(*piece.node, cursorX, cursorY, piece.width, piece.height, piece.style, fixed, mathTag == "msup");
                } else if (paint && mathTag == "msubsup") {
                    PaintSubSup(*piece.node, cursorX, cursorY, piece.width, piece.height, piece.style, fixed);
                } else if (paint && (mathTag == "mover" || mathTag == "munder" || mathTag == "munderover")) {
                    PaintLimits(*piece.node, cursorX, cursorY, piece.width, piece.height, piece.style, fixed, mathTag != "munder", mathTag != "mover");
                } else if (paint && mathTag == "menclose") {
                    PaintEnclose(*piece.node, cursorX, cursorY, piece.width, piece.height, piece.style, fixed);
                } else if (paint && mathTag == "mpadded") {
                    PaintPadded(*piece.node, cursorX, cursorY, piece.width, piece.height, piece.style, fixed);
                } else if (paint && mathTag == "mfenced") {
                    PaintFenced(*piece.node, cursorX, cursorY, piece.width, piece.height, piece.style, fixed);
                } else if (paint && mathTag == "mo") {
                    const std::string form = ToLowerAscii(AttributeValue(*piece.node, "form"));
                    int lead = HasAttribute(*piece.node, "lspace") ? std::max(0, MathLength(AttributeValue(*piece.node, "lspace"), piece.style.fontSizePx)) : 0;
                    if (!HasAttribute(*piece.node, "lspace") && (form == "infix" || form == "postfix")) {
                        lead = std::max(1, piece.style.fontSizePx / 5);
                    }
                    ResolvedStyle operatorStyle = piece.style;
                    if (MathFlag(*piece.node, "largeop")) {
                        operatorStyle.fontSizePx = std::max(1, operatorStyle.fontSizePx * 3 / 2);
                    }
                    if (MathFlag(*piece.node, "stretchy")) {
                        Fill(cursorX + lead, cursorY, 2, piece.height, operatorStyle.color, fixed);
                    } else if (!piece.text.empty()) {
                        Text(cursorX + lead, cursorY, piece.text, operatorStyle, fixed);
                    }
                    AddBox(*piece.node, cursorX, cursorY, piece.width, piece.height, operatorStyle, fixed);
                } else if (paint && (mathTag == "mspace" || mathTag == "mphantom")) {
                    AddBox(*piece.node, cursorX, cursorY, piece.width, piece.height, piece.style, fixed);
                } else if (paint && mathTag == "mtable") {
                    PaintTable(*piece.node, cursorX, cursorY, piece.width, piece.height, piece.style, fixed);
                } else if (paint && mathTag == "mmultiscripts") {
                    PaintMultiscripts(*piece.node, cursorX, cursorY, piece.width, piece.height, piece.style, fixed);
                } else if (paint && piece.atomic && piece.node != nullptr) {
                    PaintAtomic(*piece.node, cursorX, cursorY, piece.width, piece.height, piece.style, fixed);
                } else if (paint && !piece.text.empty()) {
                    Text(cursorX, cursorY, piece.text, piece.style, fixed);
                    if (piece.node != nullptr && piece.node->type == Node::Type::Element) {
                        AddBox(*piece.node, cursorX, cursorY, piece.width, lineHeight, piece.style, fixed);
                    }
                }
                cursorX += piece.width;
            }
            cursorY += lineHeight;
            const bool hardBreak = end < pieces.size() && pieces[end].lineBreak;
            if (hardBreak) {
                ++end;
            }
            firstLine = false;
            afterForced = hardBreak;
            if (end == index) {
                if (index < pieces.size() && pieces[index].lineBreak) {
                    afterForced = true;
                }
                ++index;
                continue;
            }
            index = end;
        }
        usedHeight = cursorY - y;
        if (usedHeight == 0 && pieces.empty()) {
            usedHeight = 0;
        }
    }

    void CollectInlines(Node& node, const ResolvedStyle& style, int containing, std::vector<Piece>& pieces, bool rootBlock) {
        const std::size_t rtlStart = pieces.size();
        if (node.scriptText || HasAttribute(node, "contenteditable")) {
            const std::string text = CollapsedText(node.liveValue.empty() && !node.scriptText ? GatherText(node) : node.liveValue, style.pre);
            if (!text.empty()) {
                Piece piece;
                piece.text = text;
                piece.style = style;
                piece.node = &node;
                piece.width = MeasureTextPx(text, style.fontSizePx, style.bold, style.italic, style.fontFamily, style.letterSpacingPx, style.wordSpacingPx);
                piece.height = LineHeightPx(style.fontSizePx);
                pieces.push_back(piece);
            }
            return;
        }
        for (Node* childPtr : FlowChildren(node)) {
            Node& child = *childPtr;
            if (child.type == Node::Type::Text) {
                const std::string text = CollapsedText(child.text, style.pre);
                if (text.empty() || (!style.pre && text == " ")) {
                    if (!pieces.empty() && !style.pre) {
                        pieces.back().text.push_back(' ');
                        pieces.back().width += MeasureTextPx(" ", style.fontSizePx, style.bold, style.italic, style.fontFamily);
                    }
                    continue;
                }
                if (style.pre) {
                    std::string line;
                    for (char character : text) {
                        if (character == '\n') {
                            Piece piece;
                            piece.text = line;
                            piece.style = style;
                            piece.width = MeasureTextPx(line, style.fontSizePx, style.bold, style.italic, style.fontFamily, style.letterSpacingPx, style.wordSpacingPx);
                            piece.height = LineHeightPx(style.fontSizePx);
                            pieces.push_back(piece);
                            Piece breaker;
                            breaker.lineBreak = true;
                            pieces.push_back(breaker);
                            line.clear();
                        } else {
                            line.push_back(character);
                        }
                    }
                    if (!line.empty()) {
                        Piece piece;
                        piece.text = line;
                        piece.style = style;
                        piece.width = MeasureTextPx(line, style.fontSizePx, style.bold, style.italic, style.fontFamily, style.letterSpacingPx, style.wordSpacingPx);
                        piece.height = LineHeightPx(style.fontSizePx);
                        pieces.push_back(piece);
                    }
                } else {
                    Piece piece;
                    piece.text = text;
                    piece.style = style;
                    piece.node = &node;
                    piece.width = MeasureTextPx(text, style.fontSizePx, style.bold, style.italic, style.fontFamily, style.letterSpacingPx, style.wordSpacingPx);
                    piece.height = LineHeightPx(style.fontSizePx);
                    if (rootBlock) {
                        piece.node = nullptr;
                    }
                    pieces.push_back(piece);
                }
                continue;
            }
            if (child.kind == ElementKind::Br) {
                Piece piece;
                piece.lineBreak = true;
                piece.node = &child;
                pieces.push_back(piece);
                continue;
            }
            if (IsHidden(child)) {
                continue;
            }
            if (ResolveStyle(child, style, *document).display == DisplayKind::None) {
                continue;
            }
            if (child.kind == ElementKind::Option || child.kind == ElementKind::Optgroup) {
                continue;
            }
            const ResolvedStyle childStyle = ResolveStyle(child, style, *document);
            const std::string mathTag = ToLowerAscii(child.tagName);
            if (mathTag == "math" && ToLowerAscii(AttributeValue(child, "display")) == "block") {
                Piece beforeBreak;
                beforeBreak.lineBreak = true;
                pieces.push_back(beforeBreak);
                CollectInlines(child, childStyle, containing, pieces, false);
                Piece afterBreak;
                afterBreak.lineBreak = true;
                pieces.push_back(afterBreak);
                continue;
            }
            if (mathTag == "semantics") {
                for (const std::unique_ptr<Node>& part : child.children) {
                    if (part->type != Node::Type::Element) {
                        continue;
                    }
                    CollectInlines(*part, ResolveStyle(*part, childStyle, *document), containing, pieces, false);
                    break;
                }
                continue;
            }
            if (mathTag == "maction") {
                const bool toggle = ToLowerAscii(AttributeValue(child, "actiontype")) == "toggle";
                const int selection = toggle ? std::max(1, std::atoi(AttributeValue(child, "selection").c_str())) : 1;
                int shown = 1;
                for (const std::unique_ptr<Node>& part : child.children) {
                    if (part->type != Node::Type::Element) {
                        continue;
                    }
                    if (shown == selection) {
                        CollectInlines(*part, ResolveStyle(*part, childStyle, *document), containing, pieces, false);
                        break;
                    }
                    ++shown;
                }
                continue;
            }
            if (mathTag == "ms" || mathTag == "mo") {
                const std::string body = CollapsedText(GatherText(child), false);
                std::string shown = body;
                if (mathTag == "ms") {
                    const std::string openQuote = HasAttribute(child, "lquote") ? AttributeValue(child, "lquote") : "\"";
                    const std::string closeQuote = HasAttribute(child, "rquote") ? AttributeValue(child, "rquote") : "\"";
                    shown = openQuote + body + closeQuote;
                }
                const std::string form = ToLowerAscii(AttributeValue(child, "form"));
                int lead = HasAttribute(child, "lspace") ? std::max(0, MathLength(AttributeValue(child, "lspace"), childStyle.fontSizePx)) : 0;
                int trail = HasAttribute(child, "rspace") ? std::max(0, MathLength(AttributeValue(child, "rspace"), childStyle.fontSizePx)) : 0;
                if (mathTag == "mo" && !HasAttribute(child, "lspace") && (form == "infix" || form == "postfix")) {
                    lead = std::max(1, childStyle.fontSizePx / 5);
                }
                if (mathTag == "mo" && !HasAttribute(child, "rspace") && (form == "infix" || form == "prefix")) {
                    trail = std::max(1, childStyle.fontSizePx / 5);
                }
                ResolvedStyle operatorStyle = childStyle;
                if (mathTag == "mo" && (ToLowerAscii(AttributeValue(child, "largeop")) == "true" || AttributeValue(child, "largeop") == "1")) {
                    operatorStyle.fontSizePx = std::max(1, operatorStyle.fontSizePx * 3 / 2);
                }
                Piece piece;
                piece.node = &child;
                piece.style = operatorStyle;
                piece.text = shown;
                const bool stretchy = mathTag == "mo" && (ToLowerAscii(AttributeValue(child, "stretchy")) == "true" || AttributeValue(child, "stretchy") == "1");
                int bar = LineHeightPx(operatorStyle.fontSizePx);
                if (stretchy) {
                    const int minSize = HasAttribute(child, "minsize") ? std::max(1, MathLength(AttributeValue(child, "minsize"), bar)) : bar * 2;
                    const int maxSize = HasAttribute(child, "maxsize") ? std::max(minSize, MathLength(AttributeValue(child, "maxsize"), bar)) : minSize;
                    bar = std::min(maxSize, std::max(minSize, bar));
                    piece.text.clear();
                    piece.width = lead + 2 + trail;
                    piece.height = bar;
                } else {
                    piece.width = lead + MeasureTextPx(shown, operatorStyle.fontSizePx, operatorStyle.bold, operatorStyle.italic, operatorStyle.fontFamily) + trail;
                    piece.height = LineHeightPx(operatorStyle.fontSizePx);
                }
                pieces.push_back(piece);
                continue;
            }
            if (mathTag == "mtable" || mathTag == "mmultiscripts") {
                Piece piece;
                piece.node = &child;
                piece.style = childStyle;
                if (mathTag == "mtable") {
                    std::vector<int> columnWidths;
                    std::vector<int> columnX;
                    std::vector<std::vector<std::string>> cells;
                    std::vector<std::vector<int>> spans;
                    int rowHeight = 0;
                    int rowGap = 0;
                    int origin = 0;
                    MeasureMathTable(child, childStyle, piece.width, piece.height, columnWidths, columnX, rowHeight, rowGap, origin, cells, spans);
                } else {
                    std::string base;
                    std::vector<std::string> postSub;
                    std::vector<std::string> postSup;
                    std::vector<std::string> preSub;
                    std::vector<std::string> preSup;
                    bool baseSet = false;
                    bool prescripts = false;
                    bool subscript = true;
                    for (const std::unique_ptr<Node>& part : child.children) {
                        if (part->type != Node::Type::Element) {
                            continue;
                        }
                        if (ToLowerAscii(part->tagName) == "mprescripts") {
                            prescripts = true;
                            subscript = true;
                            continue;
                        }
                        const std::string text = CollapsedText(GatherText(*part), false);
                        if (!baseSet) {
                            base = text;
                            baseSet = true;
                            continue;
                        }
                        std::vector<std::string>& target = prescripts ? (subscript ? preSub : preSup) : (subscript ? postSub : postSup);
                        target.push_back(text);
                        subscript = !subscript;
                    }
                    const int scriptSize = std::max(1, childStyle.fontSizePx * 7 / 10);
                    const int scriptLine = LineHeightPx(scriptSize);
                    const int superShift = std::max(0, std::atoi(AttributeValue(child, "superscriptshift").c_str()));
                    const int subShift = std::max(0, std::atoi(AttributeValue(child, "subscriptshift").c_str()));
                    const auto pairWidth = [&](const std::vector<std::string>& subs, const std::vector<std::string>& sups) {
                        int total = 0;
                        const std::size_t count = std::max(subs.size(), sups.size());
                        for (std::size_t index = 0; index < count; ++index) {
                            const int subWidth = index < subs.size() ? MeasureTextPx(subs[index], scriptSize, childStyle.bold, childStyle.italic, childStyle.fontFamily) : 0;
                            const int supWidth = index < sups.size() ? MeasureTextPx(sups[index], scriptSize, childStyle.bold, childStyle.italic, childStyle.fontFamily) : 0;
                            total += std::max(subWidth, supWidth);
                        }
                        return total;
                    };
                    piece.width = pairWidth(preSub, preSup) + MeasureTextPx(base, childStyle.fontSizePx, childStyle.bold, childStyle.italic, childStyle.fontFamily) + pairWidth(postSub, postSup);
                    piece.height = LineHeightPx(childStyle.fontSizePx) + scriptLine + superShift + subShift;
                }
                pieces.push_back(piece);
                continue;
            }
            if (mathTag == "mspace" || mathTag == "mphantom" || mathTag == "menclose" || mathTag == "mpadded" || mathTag == "mfenced") {
                std::vector<std::string> parts;
                MathParts(child, parts);
                const int line = LineHeightPx(childStyle.fontSizePx);
                const int contentWidth = MathRunWidth(parts, childStyle);
                Piece piece;
                piece.node = &child;
                piece.style = childStyle;
                piece.height = line;
                if (mathTag == "mspace") {
                    const int depth = std::max(0, MathLength(AttributeValue(child, "depth"), line));
                    const int rise = std::max(0, MathLength(AttributeValue(child, "height"), line));
                    piece.width = std::max(0, MathLength(AttributeValue(child, "width"), contentWidth));
                    piece.height = std::max(line, rise + depth);
                } else if (mathTag == "mphantom") {
                    piece.width = contentWidth;
                } else if (mathTag == "menclose") {
                    piece.width = contentWidth + 6;
                } else if (mathTag == "mpadded") {
                    const int lspace = MathLength(AttributeValue(child, "lspace"), 0);
                    const int voffset = MathLength(AttributeValue(child, "voffset"), 0);
                    const bool widthSet = HasAttribute(child, "width");
                    piece.width = widthSet ? std::max(0, MathLength(AttributeValue(child, "width"), contentWidth)) : lspace + contentWidth;
                    piece.height = line + std::abs(voffset);
                } else {
                    const std::string open = HasAttribute(child, "open") ? AttributeValue(child, "open") : "(";
                    const std::string close = HasAttribute(child, "close") ? AttributeValue(child, "close") : ")";
                    std::string separators = HasAttribute(child, "separators") ? AttributeValue(child, "separators") : ",";
                    std::string marks;
                    for (char letter : separators) {
                        if (letter != ' ' && letter != '\t' && letter != '\n') {
                            marks.push_back(letter);
                        }
                    }
                    int fenceWidth = MeasureTextPx(open, childStyle.fontSizePx, childStyle.bold, childStyle.italic, childStyle.fontFamily) +
                                     MeasureTextPx(close, childStyle.fontSizePx, childStyle.bold, childStyle.italic, childStyle.fontFamily) + contentWidth;
                    if (!marks.empty() && parts.size() > 1) {
                        for (std::size_t index = 0; index + 1 < parts.size(); ++index) {
                            const std::string separator(1, marks[std::min(index, marks.size() - 1)]);
                            fenceWidth += MeasureTextPx(separator, childStyle.fontSizePx, childStyle.bold, childStyle.italic, childStyle.fontFamily);
                        }
                    }
                    piece.width = fenceWidth;
                }
                pieces.push_back(piece);
                continue;
            }
            if (mathTag == "mfrac" || mathTag == "msqrt" || mathTag == "mroot" || mathTag == "msup" || mathTag == "msub" || mathTag == "msubsup" ||
                mathTag == "mover" || mathTag == "munder" || mathTag == "munderover") {
                std::string first;
                std::string second;
                std::string third;
                for (const std::unique_ptr<Node>& part : child.children) {
                    if (part->type != Node::Type::Element) {
                        continue;
                    }
                    const std::string text = CollapsedText(GatherText(*part), false);
                    if (first.empty()) {
                        first = text;
                    } else if (second.empty()) {
                        second = text;
                    } else if (third.empty()) {
                        third = text;
                    }
                }
                const int line = LineHeightPx(childStyle.fontSizePx);
                const int scriptSize = std::max(1, childStyle.fontSizePx * 7 / 10);
                const int scriptLine = LineHeightPx(scriptSize);
                const int firstWidth = MeasureTextPx(first, childStyle.fontSizePx, childStyle.bold, childStyle.italic, childStyle.fontFamily);
                const int secondWidth = MeasureTextPx(second, scriptSize, childStyle.bold, childStyle.italic, childStyle.fontFamily);
                Piece piece;
                piece.node = &child;
                piece.style = childStyle;
                if (mathTag == "mfrac") {
                    const int thickness = AttributeValue(child, "linethickness").empty() ? 1 : std::max(0, std::atoi(AttributeValue(child, "linethickness").c_str()));
                    const int denominatorWidth = MeasureTextPx(second, childStyle.fontSizePx, childStyle.bold, childStyle.italic, childStyle.fontFamily);
                    piece.width = std::max(1, std::max(firstWidth, denominatorWidth));
                    piece.height = line + thickness + line;
                } else if (mathTag == "msqrt" || mathTag == "mroot") {
                    const int gap = std::max(4, childStyle.fontSizePx / 2);
                    piece.width = gap + std::max(1, firstWidth);
                    piece.height = line + (mathTag == "mroot" ? scriptLine / 2 : 0);
                } else if (mathTag == "msup") {
                    const int shift = std::max(0, std::atoi(AttributeValue(child, "superscriptshift").c_str()));
                    piece.width = std::max(1, firstWidth) + secondWidth;
                    piece.height = line + scriptLine / 2 + shift;
                } else if (mathTag == "msub") {
                    const int shift = std::max(0, std::atoi(AttributeValue(child, "subscriptshift").c_str()));
                    piece.width = std::max(1, firstWidth) + secondWidth;
                    piece.height = line + scriptLine / 2 + shift;
                } else if (mathTag == "msubsup") {
                    const int superShift = std::max(0, std::atoi(AttributeValue(child, "superscriptshift").c_str()));
                    const int subShift = std::max(0, std::atoi(AttributeValue(child, "subscriptshift").c_str()));
                    const int thirdWidth = MeasureTextPx(third, scriptSize, childStyle.bold, childStyle.italic, childStyle.fontFamily);
                    piece.width = std::max(1, firstWidth) + std::max(secondWidth, thirdWidth);
                    piece.height = line + scriptLine + superShift + subShift;
                } else {
                    const bool over = mathTag != "munder";
                    const bool under = mathTag != "mover";
                    const bool accent = over && (ToLowerAscii(AttributeValue(child, "accent")) == "true" || AttributeValue(child, "accent") == "1");
                    const bool accentUnder = under && (ToLowerAscii(AttributeValue(child, "accentunder")) == "true" || AttributeValue(child, "accentunder") == "1");
                    const int overSize = accent ? childStyle.fontSizePx : scriptSize;
                    const int underSize = accentUnder ? childStyle.fontSizePx : scriptSize;
                    const int overLine = over ? LineHeightPx(overSize) : 0;
                    const int underLine = under ? LineHeightPx(underSize) : 0;
                    const int overGap = accent ? 0 : over ? overLine / 2 : 0;
                    const int underGap = accentUnder ? 0 : under ? underLine / 2 : 0;
                    const std::string overText = mathTag == "munderover" ? third : over ? second : std::string();
                    const std::string underText = mathTag == "munder" ? second : mathTag == "munderover" ? second : std::string();
                    const int overWidth = MeasureTextPx(overText, overSize, childStyle.bold, childStyle.italic, childStyle.fontFamily);
                    const int underWidth = MeasureTextPx(underText, underSize, childStyle.bold, childStyle.italic, childStyle.fontFamily);
                    piece.width = std::max(1, std::max(firstWidth, std::max(overWidth, underWidth)));
                    piece.height = overLine + overGap + line + underGap + underLine;
                }
                pieces.push_back(piece);
                continue;
            }
            const DisplayKind display = childStyle.display;
            if (display == DisplayKind::Inline || display == DisplayKind::ListItem || child.kind == ElementKind::Ruby) {
                if ((SpecFor(child.kind).presentation & PRESENTATION_QUOTE) != 0) {
                    Piece openQuote;
                    openQuote.text = "\"";
                    openQuote.style = childStyle;
                    openQuote.width = MeasureTextPx("\"", childStyle.fontSizePx, childStyle.bold, childStyle.italic, childStyle.fontFamily);
                    openQuote.height = LineHeightPx(childStyle.fontSizePx);
                    pieces.push_back(openQuote);
                }
                const std::size_t before = pieces.size();
                CollectInlines(child, childStyle, containing, pieces, false);
                for (std::size_t pieceIndex = before; pieceIndex < pieces.size(); ++pieceIndex) {
                    if (pieces[pieceIndex].node == nullptr || pieces[pieceIndex].node == &child) {
                        pieces[pieceIndex].node = &child;
                        pieces[pieceIndex].style.underline = childStyle.underline;
                        pieces[pieceIndex].style.strike = childStyle.strike;
                        pieces[pieceIndex].style.mark = childStyle.mark;
                        pieces[pieceIndex].style.bold = childStyle.bold;
                        pieces[pieceIndex].style.italic = childStyle.italic;
                        pieces[pieceIndex].style.color = childStyle.color;
                    }
                }
                if ((SpecFor(child.kind).presentation & PRESENTATION_QUOTE) != 0) {
                    Piece closeQuote;
                    closeQuote.text = "\"";
                    closeQuote.style = childStyle;
                    closeQuote.width = MeasureTextPx("\"", childStyle.fontSizePx, childStyle.bold, childStyle.italic, childStyle.fontFamily);
                    closeQuote.height = LineHeightPx(childStyle.fontSizePx);
                    pieces.push_back(closeQuote);
                }
                continue;
            }
            Piece atomic;
            atomic.node = &child;
            atomic.atomic = true;
            atomic.style = childStyle;
            MeasureAtomic(child, childStyle, containing, atomic.width, atomic.height);
            pieces.push_back(atomic);
        }
        if (ToLowerAscii(AttributeValue(node, "dir")) == "rtl") {
            std::reverse(pieces.begin() + static_cast<std::ptrdiff_t>(rtlStart), pieces.end());
        }
        (void)rootBlock;
    }

    int LayoutBlock(Node& node, const ResolvedStyle& style, int x, int y, int width, bool fixed, bool suppressTop);

    int PreferredWidth(Node& node, const ResolvedStyle& style) {
        std::vector<Piece> pieces;
        CollectInlines(node, style, 100000, pieces, true);
        int line = 0;
        int widest = 0;
        for (const Piece& piece : pieces) {
            if (piece.lineBreak) {
                widest = std::max(widest, line);
                line = 0;
                continue;
            }
            line += piece.width;
        }
        return std::max(widest, line);
    }

    int LayoutTable(Node& table, const ResolvedStyle& style, int x, int y, int width, bool fixed) {
        struct Cell {
            Node* node = nullptr;
            int column = 0;
            int columnSpan = 1;
        };
        struct Row {
            Node* node = nullptr;
            std::vector<Cell> cells;
        };
        std::vector<Row> rows;
        Node* caption = nullptr;
        std::vector<int> spanLeft(32, 0);
        auto addRow = [&](Node& rowNode) {
            Row row;
            row.node = &rowNode;
            int column = 0;
            for (const std::unique_ptr<Node>& child : rowNode.children) {
                if (child->kind != ElementKind::Td && child->kind != ElementKind::Th) {
                    continue;
                }
                while (column < static_cast<int>(spanLeft.size()) && spanLeft[static_cast<std::size_t>(column)] > 0) {
                    ++column;
                }
                Cell cell;
                cell.node = child.get();
                cell.column = column;
                bool spanOk = false;
                cell.columnSpan = ParsePx(AttributeValue(*child, "colspan"), width, spanOk);
                if (!spanOk || cell.columnSpan < 1) {
                    cell.columnSpan = 1;
                }
                bool rowOk = false;
                int rowSpan = ParsePx(AttributeValue(*child, "rowspan"), width, rowOk);
                if (!rowOk || rowSpan < 1) {
                    rowSpan = 1;
                }
                if (column + cell.columnSpan > static_cast<int>(spanLeft.size())) {
                    spanLeft.resize(static_cast<std::size_t>(column + cell.columnSpan), 0);
                }
                for (int span = 0; span < cell.columnSpan; ++span) {
                    spanLeft[static_cast<std::size_t>(column + span)] = rowSpan;
                }
                column += cell.columnSpan;
                row.cells.push_back(cell);
            }
            for (int& remaining : spanLeft) {
                if (remaining > 0) {
                    --remaining;
                }
            }
            rows.push_back(row);
        };
        for (const std::unique_ptr<Node>& child : table.children) {
            if (child->kind == ElementKind::Caption && caption == nullptr) {
                caption = child.get();
            } else if (child->kind == ElementKind::Tr) {
                addRow(*child);
            } else if (child->kind == ElementKind::Thead || child->kind == ElementKind::Tbody || child->kind == ElementKind::Tfoot) {
                for (const std::unique_ptr<Node>& row : child->children) {
                    if (row->kind == ElementKind::Tr) {
                        addRow(*row);
                    } else if (row->kind == ElementKind::Caption && caption == nullptr) {
                        caption = row.get();
                    }
                }
            }
        }
        bool paddingOk = false;
        bool spacingOk = false;
        int cellPadding = ParsePx(AttributeValue(table, "cellpadding"), width, paddingOk);
        int spacing = ParsePx(AttributeValue(table, "cellspacing"), width, spacingOk);
        if (!paddingOk || cellPadding < 0) {
            cellPadding = CELL_PADDING_PX;
        }
        if (!spacingOk || spacing < 0) {
            spacing = TABLE_BORDER_SPACING_PX;
        }
        int columns = 1;
        for (const Row& row : rows) {
            int count = 0;
            for (const Cell& cell : row.cells) {
                count += cell.columnSpan;
            }
            columns = std::max(columns, count);
        }
        std::vector<int> columnWidths(static_cast<std::size_t>(columns), 0);
        for (const Row& row : rows) {
            for (const Cell& cell : row.cells) {
                if (cell.columnSpan != 1 || cell.column < 0 || cell.column >= columns || cell.node == nullptr) {
                    continue;
                }
                const ResolvedStyle cellStyle = ResolveStyle(*cell.node, style, *document);
                const int needed = PreferredWidth(*cell.node, cellStyle) + cellPadding * 2;
                int& columnWidth = columnWidths[static_cast<std::size_t>(cell.column)];
                columnWidth = std::max(columnWidth, needed);
            }
        }
        bool borderOk = false;
        const int borderAttr = ParsePx(AttributeValue(table, "border"), width, borderOk);
        const int border = borderOk && borderAttr > 0 ? borderAttr : 0;
        for (int& columnWidth : columnWidths) {
            if (columnWidth < 1) {
                columnWidth = std::max(1, CharAdvancePx(style.fontSizePx, style.fontFamily));
            }
        }
        int columnIndex = 0;
        auto applyColumn = [&](Node& columnNode) {
            if (columnIndex >= columns) {
                return;
            }
            bool widthOk = false;
            const int specified = ParsePx(AttributeValue(columnNode, "width"), width, widthOk);
            if (widthOk && specified > 0) {
                columnWidths[static_cast<std::size_t>(columnIndex)] = specified;
            }
            bool spanOk = false;
            int span = ParsePx(AttributeValue(columnNode, "span"), width, spanOk);
            if (!spanOk || span < 1) {
                span = 1;
            }
            columnIndex += span;
        };
        for (const std::unique_ptr<Node>& child : table.children) {
            if (child->kind == ElementKind::Col) {
                applyColumn(*child);
            } else if (child->kind == ElementKind::Colgroup) {
                if (child->children.empty()) {
                    applyColumn(*child);
                }
                for (const std::unique_ptr<Node>& column : child->children) {
                    if (column->kind == ElementKind::Col) {
                        applyColumn(*column);
                    }
                }
            }
        }
        int contentSum = 0;
        for (int columnWidth : columnWidths) {
            contentSum += columnWidth;
        }
        const int chrome = border * 2 + spacing * (columns + 1);
        int tableWidth = contentSum + chrome;
        if (tableWidth > width && contentSum > 0) {
            const int budget = std::max(columns, width - chrome);
            int assigned = 0;
            for (int index = 0; index < columns - 1; ++index) {
                columnWidths[static_cast<std::size_t>(index)] =
                    std::max(1, columnWidths[static_cast<std::size_t>(index)] * budget / contentSum);
                assigned += columnWidths[static_cast<std::size_t>(index)];
            }
            columnWidths[static_cast<std::size_t>(columns - 1)] = std::max(1, budget - assigned);
            tableWidth = width;
        }
        const int originY = y;
        int captionHeight = 0;
        if (caption != nullptr) {
            const ResolvedStyle captionStyle = ResolveStyle(*caption, style, *document);
            LayoutLines(*caption, x, y, tableWidth, captionStyle, fixed, captionHeight, true);
            AddBox(*caption, x, y, tableWidth, captionHeight, captionStyle, fixed);
            y += captionHeight;
        }
        int cursorY = y + border;
        AddBox(table, x, originY, tableWidth, 0, style, fixed);
        const std::size_t tableBox = result->boxes.size() - 1;
        if (border > 0) {
            Stroke(x, y, tableWidth, border, Color{0, 0, 0, 255}, fixed);
        }
        for (Row& row : rows) {
            int rowHeight = LineHeightPx(style.fontSizePx) + cellPadding * 2;
            std::vector<int> contentHeights(row.cells.size(), 0);
            for (std::size_t cellIndex = 0; cellIndex < row.cells.size(); ++cellIndex) {
                Cell& cell = row.cells[cellIndex];
                if (cell.node == nullptr) {
                    continue;
                }
                int spanWidth = 0;
                for (int span = 0; span < cell.columnSpan && cell.column + span < columns; ++span) {
                    spanWidth += columnWidths[static_cast<std::size_t>(cell.column + span)];
                }
                spanWidth += spacing * std::max(0, cell.columnSpan - 1);
                const ResolvedStyle cellStyle = ResolveStyle(*cell.node, style, *document);
                LayoutLines(*cell.node, 0, 0, std::max(1, spanWidth - cellPadding * 2), cellStyle, fixed, contentHeights[cellIndex], false);
                rowHeight = std::max(rowHeight, contentHeights[cellIndex] + cellPadding * 2);
            }
            int cursorX = x + border + spacing;
            AddBox(*row.node, x, cursorY, tableWidth, rowHeight, style, fixed);
            for (std::size_t cellIndex = 0; cellIndex < row.cells.size(); ++cellIndex) {
                Cell& cell = row.cells[cellIndex];
                int spanWidth = 0;
                for (int span = 0; span < cell.columnSpan && cell.column + span < columns; ++span) {
                    spanWidth += columnWidths[static_cast<std::size_t>(cell.column + span)];
                }
                spanWidth += spacing * std::max(0, cell.columnSpan - 1);
                if (cell.node == nullptr) {
                    cursorX += spanWidth + spacing;
                    continue;
                }
                const ResolvedStyle cellStyle = ResolveStyle(*cell.node, style, *document);
                const Color cellBackground = cellStyle.background.alpha != 0 ? cellStyle.background : Color{255, 255, 255, 255};
                Fill(cursorX, cursorY, spanWidth, rowHeight, cellBackground, fixed);
                if (border > 0) {
                    Stroke(cursorX, cursorY, spanWidth, rowHeight, Color{0, 0, 0, 255}, fixed);
                }
                AddBox(*cell.node, cursorX, cursorY, spanWidth, rowHeight, cellStyle, fixed);
                const int contentHeight = contentHeights[cellIndex];
                const std::string valign = ToLowerAscii(AttributeValue(*cell.node, "valign"));
                int contentTop = cursorY + cellPadding;
                if (valign == "bottom") {
                    contentTop = cursorY + rowHeight - contentHeight - cellPadding;
                } else if (valign != "top") {
                    contentTop = cursorY + std::max(cellPadding, (rowHeight - contentHeight) / 2);
                }
                int used = 0;
                LayoutLines(
                    *cell.node, cursorX + cellPadding, contentTop, std::max(1, spanWidth - cellPadding * 2), cellStyle, fixed, used, true);
                cursorX += spanWidth + spacing;
            }
            cursorY += rowHeight + spacing;
        }
        if (border > 0) {
            cursorY += border;
            Stroke(x, y, tableWidth, cursorY - y, Color{0, 0, 0, 255}, fixed);
        }
        const int tableHeight = std::max(1, cursorY - originY);
        result->boxes[tableBox].height = tableHeight;
        return tableHeight;
    }

    int LayoutFlow(Node& node, const ResolvedStyle& style, int x, int y, int width, bool fixed, bool suppressFirstTop) {
        std::vector<int> gridTracks = style.gridColumns;
        if (!style.gridTracks.empty()) {
            gridTracks.clear();
            int frTotal = 0;
            std::vector<int> minimums;
            std::vector<int> maximums;
            std::vector<int> flex;
            int fixedSum = 0;
            CssLengthContext trackContext;
            trackContext.fontSizePx = std::max(1, style.fontSizePx);
            trackContext.rootFontSizePx = std::max(1, style.rootFontSizePx);
            trackContext.viewportWidth = style.viewportWidthPx;
            trackContext.viewportHeight = style.viewportHeightPx;
            trackContext.percentBase = width;
            trackContext.zeroAdvancePx = std::max(1, CharAdvancePx(trackContext.fontSizePx, style.fontFamily));
            trackContext.rootZeroAdvancePx = trackContext.zeroAdvancePx;
            for (const std::string& token : style.gridTracks) {
                const std::string lower = ToLowerAscii(token);
                if (lower.size() > 2 && lower.compare(lower.size() - 2, 2, "fr") == 0) {
                    const int weight = std::max(1, std::atoi(lower.c_str()));
                    flex.push_back(weight);
                    minimums.push_back(0);
                    maximums.push_back(0);
                    frTotal += weight;
                } else if (lower == "auto" || lower == "min-content" || lower == "max-content") {
                    flex.push_back(1);
                    minimums.push_back(0);
                    maximums.push_back(0);
                    frTotal += 1;
                } else if (lower.compare(0, 12, "fit-content(") == 0) {
                    const std::size_t close = token.find(')');
                    int cap = 0;
                    if (close != std::string::npos) {
                        EvaluateCssPx(token.substr(12, close - 12), style.customProperties, trackContext, cap);
                    }
                    flex.push_back(0);
                    minimums.push_back(0);
                    maximums.push_back(std::max(0, cap));
                } else if (lower.compare(0, 7, "minmax(") == 0) {
                    const std::size_t comma = token.find(',');
                    const std::size_t close = token.find(')');
                    int low = 0;
                    int high = 0;
                    if (comma != std::string::npos && close != std::string::npos) {
                        EvaluateCssPx(token.substr(7, comma - 7), style.customProperties, trackContext, low);
                        EvaluateCssPx(token.substr(comma + 1, close - comma - 1), style.customProperties, trackContext, high);
                    }
                    if (high < low) {
                        high = low;
                    }
                    flex.push_back(0);
                    minimums.push_back(std::max(0, low));
                    maximums.push_back(std::max(0, high));
                    fixedSum += std::max(0, low);
                } else {
                    int track = 0;
                    EvaluateCssPx(token, style.customProperties, trackContext, track);
                    flex.push_back(0);
                    minimums.push_back(std::max(0, track));
                    maximums.push_back(std::max(0, track));
                    fixedSum += std::max(0, track);
                }
            }
            int leftover = width - fixedSum;
            if (leftover < 0) {
                leftover = 0;
            }
            for (std::size_t index = 0; index < minimums.size(); ++index) {
                int size = minimums[index];
                if (flex[index] == 0 && maximums[index] > size && leftover > 0) {
                    const int grow = std::min(leftover, maximums[index] - size);
                    size += grow;
                    leftover -= grow;
                }
                gridTracks.push_back(size);
            }
            if (frTotal > 0 && leftover > 0) {
                int used = 0;
                std::size_t flexIndex = 0;
                for (std::size_t index = 0; index < flex.size(); ++index) {
                    if (flex[index] == 0) {
                        continue;
                    }
                    ++flexIndex;
                    const int share = flexIndex == static_cast<std::size_t>(std::count_if(flex.begin(), flex.end(), [](int weight) { return weight > 0; }))
                        ? leftover - used
                        : leftover * flex[index] / frTotal;
                    gridTracks[index] += share;
                    used += share;
                }
            }
        }
        if (!gridTracks.empty()) {
            const int columns = static_cast<int>(gridTracks.size());
            std::vector<int> rowTracks;
            for (const std::string& token : style.gridRows) {
                int track = 0;
                CssLengthContext rowContext;
                rowContext.fontSizePx = std::max(1, style.fontSizePx);
                rowContext.rootFontSizePx = std::max(1, style.rootFontSizePx);
                rowContext.viewportWidth = style.viewportWidthPx;
                rowContext.viewportHeight = style.viewportHeightPx;
                rowContext.percentBase = style.heightPx > 0 ? style.heightPx : width;
                if (EvaluateCssPx(token, style.customProperties, rowContext, track) && track > 0) {
                    rowTracks.push_back(track);
                } else {
                    rowTracks.push_back(0);
                }
            }
            int column = 0;
            int rowIndex = 0;
            int rowY = y;
            int rowHeight = 0;
            int columnX = x;
            for (Node* childPtr : FlowChildren(node)) {
                Node& child = *childPtr;
                if (child.type != Node::Type::Element || IsHidden(child)) {
                    continue;
                }
                if (column == 0) {
                    columnX = x;
                }
                const int track = gridTracks[static_cast<std::size_t>(column)];
                const ResolvedStyle childStyle = ResolveStyle(child, style, *document, track);
                const int childHeight = LayoutBlock(child, childStyle, columnX, rowY, track, fixed, false);
                if (childHeight > rowHeight) {
                    rowHeight = childHeight;
                }
                columnX += track;
                ++column;
                if (column == columns) {
                    if (rowIndex < static_cast<int>(rowTracks.size()) && rowTracks[static_cast<std::size_t>(rowIndex)] > rowHeight) {
                        rowHeight = rowTracks[static_cast<std::size_t>(rowIndex)];
                    }
                    rowY += rowHeight;
                    rowHeight = 0;
                    column = 0;
                    ++rowIndex;
                }
            }
            if (column != 0) {
                if (rowIndex < static_cast<int>(rowTracks.size()) && rowTracks[static_cast<std::size_t>(rowIndex)] > rowHeight) {
                    rowHeight = rowTracks[static_cast<std::size_t>(rowIndex)];
                }
                rowY += rowHeight;
            }
            return rowY - y;
        }
        int cursor = y;
        int previousBottom = 0;
        bool any = false;
        bool pendingInlines = false;
        auto flushInlines = [&](std::size_t, std::size_t) {
        };
        (void)flushInlines;
        (void)pendingInlines;
        std::vector<Node*> anchored;
        for (Node* childPtr : FlowChildren(node)) {
            Node& child = *childPtr;
            if (child.type == Node::Type::Text) {
                const std::string text = CollapsedText(child.text, style.pre);
                if (text.empty() || text == " ") {
                    continue;
                }
                int used = 0;
                const int lineHeight = LineHeightPx(style.fontSizePx);
                Text(x + style.paddingLeft, cursor, text, style, fixed);
                used = lineHeight;
                cursor += used;
                any = true;
                previousBottom = 0;
                continue;
            }
            if (IsHidden(child)) {
                continue;
            }
            if (ResolveStyle(child, style, *document, width, style.heightPx > 0 ? style.heightPx : -1).display == DisplayKind::None) {
                continue;
            }
            if (node.kind == ElementKind::Details && !node.open && child.kind != ElementKind::Summary) {
                continue;
            }
            if (node.kind == ElementKind::Select && child.kind != ElementKind::Button) {
                continue;
            }
            if (child.kind == ElementKind::Dialog || HasAttribute(child, "popover")) {
                if (child.kind == ElementKind::Dialog ? child.open : child.popoverOpen) {
                    overlays.push_back(&child);
                }
                continue;
            }
            ResolvedStyle childStyle = ResolveStyle(child, style, *document, width, style.heightPx > 0 ? style.heightPx : -1);
            if (childStyle.positionKind == 1) {
                anchored.push_back(&child);
                continue;
            }
            if (childStyle.display == DisplayKind::Inline || childStyle.display == DisplayKind::InlineBlock ||
                childStyle.display == DisplayKind::Replaced || childStyle.display == DisplayKind::LineBreak ||
                childStyle.display == DisplayKind::WordBreak) {
                int used = 0;
                LayoutLines(node, x + style.paddingLeft, cursor, width - style.paddingLeft - style.paddingRight, style, fixed, used, true);
                cursor += used;
                break;
            }
            const int top = (!any && suppressFirstTop) ? 0 : childStyle.marginTop;
            if (any) {
                cursor += Collapse(previousBottom, childStyle.marginTop);
            } else {
                cursor += top;
            }
            const int childWidth = std::max(0, width - childStyle.marginLeft - childStyle.marginRight);
            const int childHeight = LayoutBlock(child, childStyle, x + childStyle.marginLeft, cursor, childWidth, fixed, false);
            cursor += childHeight;
            previousBottom = childStyle.marginBottom;
            any = true;
        }
        for (Node* absolute : anchored) {
            const int heightBase = style.heightPx > 0 ? style.heightPx : -1;
            const ResolvedStyle absoluteStyle = ResolveStyle(*absolute, style, *document, width, heightBase);
            int placeX = x;
            int placeY = y;
            if (absoluteStyle.anchorLeft && !absoluteStyle.anchorQuery.empty()) {
                for (const ElementBox& box : result->boxes) {
                    if (box.node == nullptr) {
                        continue;
                    }
                    const ResolvedStyle anchorStyle = ResolveStyle(*box.node, style, *document, width, heightBase);
                    if (anchorStyle.anchorName == absoluteStyle.anchorQuery) {
                        placeX = box.x + box.width;
                        placeY = box.y;
                        break;
                    }
                }
            }
            const int absoluteWidth = absoluteStyle.widthPx > 0 ? absoluteStyle.widthPx : 8;
            LayoutBlock(*absolute, absoluteStyle, placeX, placeY, absoluteWidth, fixed, false);
        }
        return cursor - y;
    }
};

std::string Roman(int number, bool upper) {
    if (number <= 0 || number > 3999) {
        return std::to_string(number);
    }
    const char* ones[] = {"", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix"};
    const char* tens[] = {"", "x", "xx", "xxx", "xl", "l", "lx", "lxx", "lxxx", "xc"};
    const char* hundreds[] = {"", "c", "cc", "ccc", "cd", "d", "dc", "dcc", "dccc", "cm"};
    const char* thousands[] = {"", "m", "mm", "mmm"};
    std::string text = std::string(thousands[number / 1000]) + hundreds[(number / 100) % 10] + tens[(number / 10) % 10] + ones[number % 10];
    if (upper) {
        for (char& character : text) {
            if (character >= 'a' && character <= 'z') {
                character = static_cast<char>(character - 'a' + 'A');
            }
        }
    }
    return text;
}

std::string ListMarker(int index, const std::string& type) {
    if (type == "A" || type == "a") {
        std::string letters;
        int value = std::max(1, index);
        while (value > 0) {
            --value;
            letters.insert(letters.begin(), static_cast<char>((type == "A" ? 'A' : 'a') + value % 26));
            value /= 26;
        }
        return letters + ".";
    }
    if (type == "I" || type == "i") {
        return Roman(index, type == "I") + ".";
    }
    return std::to_string(index) + ".";
}

int Engine::LayoutBlock(Node& node, const ResolvedStyle& style, int x, int y, int width, bool fixed, bool suppressTop) {
    (void)suppressTop;
    if (style.display == DisplayKind::None) {
        return 0;
    }
    PaintScope paintScope(*this, style);
    x += style.translateX;
    y += style.translateY;
    if (style.widthPx > 0) {
        width = std::min(width, style.widthPx);
    }
    if (style.maxWidthPx >= 0) {
        width = std::min(width, style.maxWidthPx);
    }
    if (style.minWidthPx >= 0 && width < style.minWidthPx) {
        width = style.minWidthPx;
    }
    if (node.kind == ElementKind::Frameset) {
        std::vector<Node*> frames;
        for (const std::unique_ptr<Node>& child : node.children) {
            if (child->kind == ElementKind::Frame || child->kind == ElementKind::Frameset) {
                frames.push_back(child.get());
            }
        }
        const int count = std::max(1, static_cast<int>(frames.size()));
        const int frameWidth = std::max(1, width / count);
        const int frameHeight = style.heightPx > 0 ? style.heightPx : 180;
        for (int index = 0; index < static_cast<int>(frames.size()); ++index) {
            Node& frame = *frames[static_cast<std::size_t>(index)];
            const ResolvedStyle frameStyle = ResolveStyle(frame, style, *document);
            const int childHeight = LayoutBlock(frame, frameStyle, x + index * frameWidth, y, frameWidth, fixed, false);
            (void)childHeight;
        }
        AddBox(node, x, y, width, frameHeight, style, fixed);
        return frameHeight;
    }
    if (node.kind == ElementKind::Table) {
        const int height = LayoutTable(node, style, x, y, width, fixed);
        return height;
    }
    if (style.display == DisplayKind::Replaced || style.display == DisplayKind::InlineBlock || node.kind == ElementKind::Hr) {
        int atomWidth = 0;
        int atomHeight = 0;
        MeasureAtomic(node, style, width, atomWidth, atomHeight);
        if (node.kind == ElementKind::Hr || style.display == DisplayKind::Block) {
            atomWidth = width;
        }
        PaintAtomic(node, x, y, atomWidth, atomHeight, style, fixed);
        return atomHeight;
    }
    const int contentX = x + style.border + style.paddingLeft;
    const int contentY = y + style.border + style.paddingTop;
    const int contentWidth = std::max(0, width - style.border * 2 - style.paddingLeft - style.paddingRight);
    const std::size_t backgroundIndex = result->ops.size();
    int contentHeight = 0;
    bool inlineFlow = true;
    for (const std::unique_ptr<Node>& child : node.children) {
        if (child->type == Node::Type::Element && !IsHidden(*child)) {
            const DisplayKind display = ResolveStyle(*child, style, *document, contentWidth).display;
            if (display == DisplayKind::Block || display == DisplayKind::ListItem || display == DisplayKind::Table) {
                inlineFlow = false;
            }
        }
    }
    if (node.kind == ElementKind::Ul || node.kind == ElementKind::Ol || node.kind == ElementKind::Menu ||
        node.kind == ElementKind::Dir || node.kind == ElementKind::Details) {
        inlineFlow = false;
    }
    if (inlineFlow) {
        LayoutLines(node, contentX, contentY, contentWidth, style, fixed, contentHeight, true);
    } else {
        contentHeight = LayoutFlow(node, style, contentX, contentY, contentWidth, fixed, false);
    }
    bool paintedBackground = false;
    if (!style.linearGradient) {
        Color backgroundColor = style.background;
        ApplyCssFilters(backgroundColor);
        if (backgroundColor.alpha != 0) {
            DrawOp background;
            background.kind = DrawOp::Kind::Fill;
            background.x = x;
            background.y = y;
            background.width = width;
            background.height = contentHeight + style.paddingTop + style.paddingBottom + style.border * 2;
            background.color = backgroundColor;
            background.fixed = fixed;
            result->ops.insert(result->ops.begin() + static_cast<std::ptrdiff_t>(backgroundIndex), background);
            paintedBackground = true;
        }
    }
    int borderHeight = contentHeight + style.paddingTop + style.paddingBottom + style.border * 2;
    if (style.heightPx > borderHeight) {
        borderHeight = style.heightPx;
    }
    if (style.minHeightPx > borderHeight) {
        borderHeight = style.minHeightPx;
    }
    if (style.maxHeightPx >= 0 && borderHeight > style.maxHeightPx) {
        borderHeight = style.maxHeightPx;
    }
    if (paintedBackground && backgroundIndex < result->ops.size()) {
        result->ops[backgroundIndex].height = borderHeight;
    }
    if (style.shadowColor.alpha != 0 && width > 0 && borderHeight > 0) {
        Fill(x + style.shadowDx, y + style.shadowDy, width, borderHeight, style.shadowColor, fixed);
    }
    if (style.blurPx > 0 && style.background.alpha != 0 && width > 0 && borderHeight > 0) {
        // A CSS blur(<length>) is a Gaussian whose standard deviation is that
        // length, so the halo follows the normal curve rather than a linear ramp.
        const double sigma = static_cast<double>(style.blurPx);
        const int spread = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
        for (int row = -spread; row < borderHeight + spread; ++row) {
            for (int column = -spread; column < width + spread; ++column) {
                const double coverage = SvgBlurredBoxCoverage(x + column, y + row, x, y, width, borderHeight, sigma);
                if (coverage <= 0.002) {
                    continue;
                }
                Color halo = style.background;
                const int alpha = static_cast<int>(style.background.alpha * coverage + 0.5);
                halo.alpha = static_cast<std::uint8_t>(alpha > 255 ? 255 : (alpha < 0 ? 0 : alpha));
                Fill(x + column, y + row, 1, 1, halo, fixed);
            }
        }
    }
    if (style.scaleMilli > 1000 && style.background.alpha != 0 && !style.linearGradient && width > 0 && borderHeight > 0) {
        const int extraWidth = width * (style.scaleMilli - 1000) / 2000;
        const int extraHeight = borderHeight * (style.scaleMilli - 1000) / 2000;
        Fill(x - extraWidth, y - extraHeight, width + extraWidth * 2, borderHeight + extraHeight * 2, style.background, fixed);
    }
    if ((style.rotateDeg == 90 || style.rotateDeg == 270) && style.background.alpha != 0 && width > 0 && borderHeight > 0) {
        const int centerX = x + width / 2;
        const int centerY = y + borderHeight / 2;
        Fill(centerX - borderHeight / 2, centerY - width / 2, borderHeight, width, style.background, fixed);
    }
    if (style.hasCssMatrix && style.background.alpha != 0 && width > 0 && borderHeight > 0) {
        const double determinant = style.matrixA * style.matrixD - style.matrixC * style.matrixB;
        if (std::fabs(determinant) > 0.0001) {
            const double halfWidth = width / 2.0;
            const double halfHeight = borderHeight / 2.0;
            const double centerX = x + halfWidth;
            const double centerY = y + borderHeight / 2.0;
            auto mapPoint = [&](double localX, double localY, double& outX, double& outY) {
                outX = style.matrixA * localX + style.matrixC * localY + style.matrixE;
                outY = style.matrixB * localX + style.matrixD * localY + style.matrixF;
            };
            double minX = 0;
            double minY = 0;
            double maxX = 0;
            double maxY = 0;
            const double corners[4][2] = {{-halfWidth, -halfHeight}, {halfWidth, -halfHeight}, {halfWidth, halfHeight}, {-halfWidth, halfHeight}};
            for (int corner = 0; corner < 4; ++corner) {
                double mappedX = 0;
                double mappedY = 0;
                mapPoint(corners[corner][0], corners[corner][1], mappedX, mappedY);
                if (corner == 0 || mappedX < minX) {
                    minX = mappedX;
                }
                if (corner == 0 || mappedY < minY) {
                    minY = mappedY;
                }
                if (corner == 0 || mappedX > maxX) {
                    maxX = mappedX;
                }
                if (corner == 0 || mappedY > maxY) {
                    maxY = mappedY;
                }
            }
            const int left = static_cast<int>(std::floor(centerX + minX));
            const int top = static_cast<int>(std::floor(centerY + minY));
            const int right = static_cast<int>(std::ceil(centerX + maxX));
            const int bottom = static_cast<int>(std::ceil(centerY + maxY));
            for (int row = top; row < bottom; ++row) {
                for (int column = left; column < right; ++column) {
                    const double offsetX = column + 0.5 - centerX - style.matrixE;
                    const double offsetY = row + 0.5 - centerY - style.matrixF;
                    const double localX = (style.matrixD * offsetX - style.matrixC * offsetY) / determinant;
                    const double localY = (-style.matrixB * offsetX + style.matrixA * offsetY) / determinant;
                    if (localX >= -halfWidth && localX < halfWidth && localY >= -halfHeight && localY < halfHeight) {
                        Fill(column, row, 1, 1, style.background, fixed);
                    }
                }
            }
        }
    }
    if (style.linearGradient && borderHeight > 0 && width > 0 && style.gradientColors.size() >= 2) {
        auto sampleGradient = [&](int distance, int span) {
            std::vector<int> positions(style.gradientColors.size(), -1);
            const int limit = span > 1 ? span : 1;
            for (std::size_t index = 0; index < style.gradientColors.size(); ++index) {
                if (index < style.gradientStopPx.size() && style.gradientStopPx[index] >= 0) {
                    positions[index] = style.gradientStopPx[index] * 1000 / limit;
                } else if (index < style.gradientStopMilli.size() && style.gradientStopMilli[index] >= 0) {
                    positions[index] = style.gradientStopMilli[index];
                }
            }
            if (positions.front() < 0) {
                positions.front() = 0;
            }
            if (positions.back() < 0) {
                positions.back() = 1000;
            }
            for (std::size_t index = 1; index + 1 < positions.size(); ++index) {
                if (positions[index] >= 0) {
                    continue;
                }
                std::size_t next = index + 1;
                while (next + 1 < positions.size() && positions[next] < 0) {
                    ++next;
                }
                const int left = positions[index - 1];
                const int right = positions[next] < 0 ? 1000 : positions[next];
                const int gap = static_cast<int>(next - (index - 1));
                positions[index] = left + (right - left) / gap;
            }
            int along = limit <= 1 ? 0 : distance * 1000 / limit;
            if (along < 0) {
                along = 0;
            }
            if (along > 1000) {
                along = 1000;
            }
            std::size_t upper = 1;
            while (upper + 1 < positions.size() && positions[upper] < along) {
                ++upper;
            }
            const std::size_t lower = upper - 1;
            const int start = positions[lower];
            const int end = positions[upper] <= start ? start + 1 : positions[upper];
            const int weight = std::min(256, (along - start) * 256 / (end - start));
            const Color& from = style.gradientColors[lower];
            const Color& to = style.gradientColors[upper];
            Color color = from;
            color.red = static_cast<std::uint8_t>((from.red * (256 - weight) + to.red * weight) / 256);
            color.green = static_cast<std::uint8_t>((from.green * (256 - weight) + to.green * weight) / 256);
            color.blue = static_cast<std::uint8_t>((from.blue * (256 - weight) + to.blue * weight) / 256);
            return color;
        };
        if (style.radialGradient) {
            const int centerX = width / 2;
            const int centerY = borderHeight / 2;
            const int radius = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(centerX * centerX + centerY * centerY))));
            const int period = style.gradientPeriod > 1 ? style.gradientPeriod : radius;
            for (int row = 0; row < borderHeight; ++row) {
                for (int column = 0; column < width; ++column) {
                    const int dx = column - centerX;
                    const int dy = row - centerY;
                    int distance = static_cast<int>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
                    if (style.repeatingGradient && style.gradientPeriod > 1) {
                        distance = distance % period;
                    }
                    Fill(x + column, y + row, 1, 1, sampleGradient(distance, period), fixed);
                }
            }
        } else {
            const int span = style.gradientAcross ? width : borderHeight;
            const int period = style.gradientPeriod > 1 ? style.gradientPeriod : span;
            for (int step = 0; step < span; ++step) {
                const int local = style.repeatingGradient && style.gradientPeriod > 1 ? step % period : step;
                const Color color = sampleGradient(local, style.repeatingGradient && style.gradientPeriod > 1 ? period : span);
                if (style.gradientAcross) {
                    Fill(x + step, y, 1, borderHeight, color, fixed);
                } else {
                    Fill(x, y + step, width, 1, color, fixed);
                }
            }
        }
    }
    if (style.border > 0) {
        Stroke(x, y, width, borderHeight, Color{118, 118, 118, 255}, fixed);
    }
    AddBox(node, x, y, width, borderHeight, style, fixed);
    if (node.kind == ElementKind::Li || node.kind == ElementKind::Summary) {
        std::string             marker = "\xE2\x80\xA2";
        if (node.kind == ElementKind::Li && node.parent != nullptr && node.parent->kind != ElementKind::Ol) {
            std::string listType = AttributeValue(node, "type");
            if (listType.empty()) {
                listType = AttributeValue(*node.parent, "type");
            }
            if (listType == "circle") {
                marker = "o";
            } else if (listType == "square") {
                marker = "#";
            } else if (listType == "none") {
                marker = "";
            }
        }
        if (node.kind == ElementKind::Summary) {
            marker = node.parent != nullptr && node.parent->open ? "v" : ">";
        } else if (node.parent != nullptr && (node.parent->kind == ElementKind::Ol)) {
            bool startOk = false;
            const int start = ParsePx(AttributeValue(*node.parent, "start"), width, startOk);
            const bool reversed = HasAttribute(*node.parent, "reversed");
            int before = 0;
            int total = 0;
            bool reached = false;
            for (const std::unique_ptr<Node>& sibling : node.parent->children) {
                if (sibling->kind != ElementKind::Li) {
                    continue;
                }
                if (sibling.get() == &node) {
                    reached = true;
                }
                if (!reached) {
                    ++before;
                }
                ++total;
            }
            int index = startOk ? start : (reversed ? total : 1);
            index += reversed ? -before : before;
            bool valueOk = false;
            const int explicitValue = ParsePx(AttributeValue(node, "value"), width, valueOk);
            if (valueOk) {
                index = explicitValue;
            }
            marker = ListMarker(index, AttributeValue(*node.parent, "type"));
        }
        Text(contentX - MeasureTextPx(marker, style.fontSizePx, style.bold, style.italic, style.fontFamily) - 6, contentY, marker, style, fixed);
    }
    return borderHeight;
}

}  // namespace

std::string EditedControlText(const Node& node) {
    return FieldEditedText(node);
}

int TextScrollShift(
    const std::string& text, int caretByte, int innerWidth, int fontSizePx, bool bold, bool italic, const std::string& family) {
    if (innerWidth < 1) {
        innerWidth = 1;
    }
    const std::vector<FieldLine> lines = SplitFieldLines(text);
    const int caret = NormalizeCaretByte(text, caretByte);
    const int lineIndex = CaretLineIndex(lines, caret);
    const FieldLine& line = lines[static_cast<std::size_t>(lineIndex)];
    int column = caret - line.start;
    if (column < 0) {
        column = 0;
    }
    if (column > static_cast<int>(line.text.size())) {
        column = static_cast<int>(line.text.size());
    }
    const int width = MeasureTextPx(line.text.substr(0, static_cast<std::size_t>(column)), fontSizePx, bold, italic, family);
    if (width >= innerWidth) {
        return width - innerWidth + 1;
    }
    return 0;
}

int HitTestTextByte(
    const std::string& text,
    int localX,
    int localY,
    int innerWidth,
    int fontSizePx,
    bool bold,
    bool italic,
    int caretByte,
    const std::string& family) {
    const std::vector<FieldLine> lines = SplitFieldLines(text);
    const int shift = TextScrollShift(text, caretByte, innerWidth, fontSizePx, bold, italic, family);
    const int lineHeight = std::max(1, LineHeightPx(fontSizePx));
    int lineIndex = localY < 0 ? 0 : localY / lineHeight;
    if (lineIndex < 0) {
        lineIndex = 0;
    }
    if (lineIndex >= static_cast<int>(lines.size())) {
        lineIndex = static_cast<int>(lines.size()) - 1;
    }
    const FieldLine& line = lines[static_cast<std::size_t>(lineIndex)];
    const int x = localX + shift;
    int previous = 0;
    std::size_t index = 0;
    while (index < line.text.size()) {
        std::size_t next = index;
        char32_t codePoint = 0;
        DecodeUtf8(line.text, next, codePoint);
        const int width = MeasureTextPx(line.text.substr(0, next), fontSizePx, bold, italic, family);
        if (x < (previous + width) / 2) {
            return line.start + static_cast<int>(index);
        }
        previous = width;
        index = next;
    }
    return line.start + static_cast<int>(line.text.size());
}

ResolvedStyle DefaultStyle() {
    ResolvedStyle style;
    style.color = Color{0, 0, 0, 255};
    return style;
}

ResolvedStyle ResolveStyle(const Node& node, const ResolvedStyle& parent, const Document& document, int percentBase, int percentBaseHeight) {
    (void)document;
    ResolvedStyle style = parent;
    style.isContainer = false;
    style.translateX = 0;
    style.translateY = 0;
    style.scaleMilli = 1000;
    style.rotateDeg = 0;
    style.hasCssMatrix = false;
    style.matrixA = 1;
    style.matrixB = 0;
    style.matrixC = 0;
    style.matrixD = 1;
    style.matrixE = 0;
    style.matrixF = 0;
    style.animationName.clear();
    style.animationDurationSec = 0;
    style.easeKind = 0;
    style.easeSteps = 1;
    style.easeX1 = 0;
    style.easeY1 = 0;
    style.easeX2 = 1;
    style.easeY2 = 1;
    style.blurPx = 0;
    style.shadowDx = 0;
    style.shadowDy = 0;
    style.shadowColor = Color{0, 0, 0, 0};
    style.linearGradient = false;
    style.radialGradient = false;
    style.repeatingGradient = false;
    style.gradientPeriod = 0;
    style.gridColumns.clear();
    style.gridTracks.clear();
    style.positionKind = 0;
    style.anchorName.clear();
    style.anchorQuery.clear();
    style.anchorLeft = false;
    style.hasColorFilter = false;
    style.brightnessMilli = 1000;
    style.contrastMilli = 1000;
    style.grayscaleMilli = 0;
    style.invertMilli = 0;
    style.saturateMilli = 1000;
    style.sepiaMilli = 0;
    style.hueRotateDeg = 0;
    style.filterOpacityMilli = 1000;
    style.opacityMilli = 1000;
    style.minWidthPx = -1;
    style.maxWidthPx = -1;
    style.minHeightPx = -1;
    style.maxHeightPx = -1;
    style.gradientColors.clear();
    style.gradientStopMilli.clear();
    style.gradientStopPx.clear();
    style.gridRows.clear();
    if (parent.isContainer && percentBase >= 0) {
        style.containerWidthPx = percentBase;
    }
    style.mark = false;
    style.background = Color{0, 0, 0, 0};
    style.border = 0;
    style.widthPx = -1;
    style.heightPx = -1;
    style.paddingLeft = 0;
    style.paddingRight = 0;
    style.paddingTop = 0;
    style.paddingBottom = 0;
    style.baselineShift = 0;
    const ElementSpec& spec = SpecFor(node.kind);
    style.display = spec.display;
    style.fontSizePx = std::max(1, EmToPx(parent.fontSizePx, spec.fontSizeMilli));
    style.bold = parent.bold || (spec.presentation & PRESENTATION_BOLD) != 0;
    style.italic = parent.italic || (spec.presentation & PRESENTATION_ITALIC) != 0;
    style.monospace = parent.monospace || (spec.presentation & PRESENTATION_MONOSPACE) != 0;
    style.underline = parent.underline || (spec.presentation & PRESENTATION_UNDERLINE) != 0;
    style.strike = parent.strike || (spec.presentation & PRESENTATION_STRIKE) != 0;
    style.mark = (spec.presentation & PRESENTATION_MARK) != 0;
    style.nowrap = parent.nowrap || (spec.presentation & PRESENTATION_NOWRAP) != 0;
    style.pre = parent.pre || (spec.presentation & PRESENTATION_PRE) != 0;
    if ((spec.presentation & PRESENTATION_CENTER) != 0) {
        style.textAlign = 1;
    }
    if ((spec.presentation & PRESENTATION_SUB) != 0) {
        style.baselineShift = EmToPx(style.fontSizePx, SUB_SUP_SHIFT_MILLI);
    }
    if ((spec.presentation & PRESENTATION_SUP) != 0) {
        style.baselineShift = -EmToPx(style.fontSizePx, SUB_SUP_SHIFT_MILLI);
    }
    style.marginTop = EmToPx(style.fontSizePx, spec.marginTopMilli);
    style.marginBottom = EmToPx(style.fontSizePx, spec.marginBottomMilli);
    style.marginLeft = spec.marginLeftPx;
    style.marginRight = spec.marginRightPx;
    style.paddingLeft = spec.paddingLeftPx;
    if (node.kind == ElementKind::Summary && style.paddingLeft < 24) {
        style.paddingLeft = 24;
    }
    if (node.kind == ElementKind::Body) {
        style.marginTop = BODY_MARGIN_PX;
        style.marginBottom = BODY_MARGIN_PX;
        style.marginLeft = BODY_MARGIN_PX;
        style.marginRight = BODY_MARGIN_PX;
    }
    if (node.kind == ElementKind::A && HasAttribute(node, "href")) {
        style.color = Color{0, 0, 238, 255};
        style.underline = true;
        for (Node* ancestor = node.parent; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->kind != ElementKind::Body) {
                continue;
            }
            Color link{0, 0, 0, 255};
            const char* which = "link";
            if (node.visited) {
                which = "vlink";
            } else if (document.hover == &node) {
                which = "alink";
            }
            if (ParseColor(AttributeValue(*ancestor, which), link) || ParseColor(AttributeValue(*ancestor, "link"), link)) {
                style.color = link;
            }
            break;
        }
    }
    if (node.kind == ElementKind::Fieldset) {
        style.border = 2;
        style.paddingTop = 8;
        style.paddingBottom = 8;
        style.paddingLeft = 8;
        style.paddingRight = 8;
    }
    Color parsed{0, 0, 0, 255};
    if (ParseColor(AttributeValue(node, "color"), parsed) || ParseColor(AttributeValue(node, "text"), parsed)) {
        style.color = parsed;
    }
    if (ParseColor(AttributeValue(node, "bgcolor"), parsed) || ParseColor(AttributeValue(node, "background"), parsed)) {
        style.background = parsed;
    }
    const std::string align = ToLowerAscii(AttributeValue(node, "align"));
    if (align == "center") {
        style.textAlign = 1;
    } else if (align == "right") {
        style.textAlign = 2;
    } else if (align == "left") {
        style.textAlign = 0;
    }
    const std::string direction = ToLowerAscii(AttributeValue(node, "dir"));
    if (direction == "rtl") {
        style.rtl = true;
        style.textAlign = 2;
    } else if (direction == "ltr") {
        style.rtl = false;
    }
    if (node.kind == ElementKind::Font && HasAttribute(node, "size")) {
        const std::string size = AttributeValue(node, "size");
        int step = 3;
        if (!size.empty() && (size[0] == '+' || size[0] == '-')) {
            step = 3 + std::atoi(size.c_str());
        } else {
            step = std::atoi(size.c_str());
        }
        step = std::max(1, std::min(7, step));
        const int milli[] = {0, 630, 820, 1000, 1130, 1500, 2000, 3000};
        style.fontSizePx = std::max(1, EmToPx(parent.fontSizePx, milli[step]));
    }
    if (HasAttribute(node, "nowrap")) {
        style.nowrap = true;
    }
    bool widthOk = false;
    bool heightOk = false;
    const int widthAttribute = ParsePx(AttributeValue(node, "width"), 0, widthOk);
    const int heightAttribute = ParsePx(AttributeValue(node, "height"), 0, heightOk);
    if (widthOk && widthAttribute > 0 && AttributeValue(node, "width").find('%') == std::string::npos) {
        style.widthPx = widthAttribute;
    }
    if (heightOk && heightAttribute > 0 && AttributeValue(node, "height").find('%') == std::string::npos) {
        style.heightPx = heightAttribute;
    }
    if (node.kind == ElementKind::Hr && HasAttribute(node, "size")) {
        style.heightPx = std::max(1, std::atoi(AttributeValue(node, "size").c_str()));
    }
    const int horizontalSpace = std::atoi(AttributeValue(node, "hspace").c_str());
    const int verticalSpace = std::atoi(AttributeValue(node, "vspace").c_str());
    if (horizontalSpace > 0) {
        style.marginLeft = horizontalSpace;
        style.marginRight = horizontalSpace;
    }
    if (verticalSpace > 0) {
        style.marginTop = verticalSpace;
        style.marginBottom = verticalSpace;
    }
    if (HasAttribute(node, "border") && node.kind != ElementKind::Table) {
        const int border = std::atoi(AttributeValue(node, "border").c_str());
        if (border > 0) {
            style.border = border;
        }
    }
    if (node.kind == ElementKind::Svg) {
        style.display = DisplayKind::Replaced;
    }
    if (node.kind == ElementKind::Rt) {
        style.baselineShift = -style.fontSizePx;
    }
    const std::string frameBorder = ToLowerAscii(AttributeValue(node, "frameborder"));
    if (frameBorder == "0" || frameBorder == "no") {
        style.border = 0;
    } else if (!frameBorder.empty()) {
        style.border = std::max(1, style.border);
    }
    const int marginWidth = std::atoi(AttributeValue(node, "marginwidth").c_str());
    const int marginHeight = std::atoi(AttributeValue(node, "marginheight").c_str());
    if (marginWidth > 0) {
        style.paddingLeft = marginWidth;
        style.paddingRight = marginWidth;
    }
    if (marginHeight > 0) {
        style.paddingTop = marginHeight;
        style.paddingBottom = marginHeight;
    }
    if (ToLowerAscii(node.tagName) == "mi") {
        const std::string variant = ToLowerAscii(AttributeValue(node, "mathvariant"));
        const std::string identifier = CollapsedText(GatherText(node), false);
        if (variant == "normal" || variant == "monospace") {
            style.italic = false;
            style.monospace = variant == "monospace";
        } else if (variant == "bold") {
            style.bold = true;
            style.italic = false;
        } else if (variant == "italic" || variant == "bold-italic") {
            style.italic = true;
            style.bold = variant == "bold-italic";
        } else if (identifier.size() <= 1) {
            style.italic = true;
        }
    }
    if (ToLowerAscii(node.tagName) == "merror") {
        style.color = Color{255, 0, 0, 255};
    }
    if (ToLowerAscii(node.tagName) == "mn") {
        style.italic = false;
    }
    if (ToLowerAscii(node.tagName) == "mtext") {
        style.italic = false;
        style.pre = true;
    }
    if (ToLowerAscii(node.tagName) == "mrow") {
        style.nowrap = true;
    }
    const std::string face = ToLowerAscii(AttributeValue(node, "face"));
    if (face.find("mono") != std::string::npos || face.find("courier") != std::string::npos) {
        style.monospace = true;
    } else if (!face.empty()) {
        style.fontFamily = face;
    }
    auto applyRule = [](ResolvedStyle& target, const ElementStyle& rule) {
        if (!rule.fontFamily.empty()) {
            target.fontFamily = rule.fontFamily;
        }
        if (rule.fontWeight >= 600) {
            target.bold = true;
        } else if (rule.fontWeight >= 0) {
            target.bold = false;
        }
        if (rule.fontStyle == 1) {
            target.italic = true;
        } else if (rule.fontStyle == 0) {
            target.italic = false;
        }
        if (rule.fontSizePx > 0) {
            target.fontSizePx = rule.fontSizePx;
        }
        if (rule.hasLetterSpacing) {
            target.letterSpacingPx = rule.letterSpacingPx;
        }
        if (rule.hasWordSpacing) {
            if (rule.wordSpacingMilli >= 0) {
                target.wordSpacingPx = EmToPx(target.fontSizePx, rule.wordSpacingMilli);
            } else {
                target.wordSpacingPx = rule.wordSpacingPx;
            }
        }
        if (rule.hasTextTransform) {
            target.textTransform = rule.textTransform;
        }
        if (rule.hasUnderlineOffset) {
            if (rule.underlineOffsetRelative) {
                target.underlineOffsetPx = EmToPx(target.fontSizePx, rule.underlineOffsetMilli);
            } else {
                target.underlineOffsetPx = rule.underlineOffsetPx;
            }
        }
        if (rule.hasDecorationColor) {
            if (rule.decorationUsesText) {
                target.hasDecorationColor = false;
            } else {
                target.hasDecorationColor = true;
                target.decorationColor = Color{rule.decorationRed, rule.decorationGreen, rule.decorationBlue, rule.decorationAlpha};
            }
        }
        if (rule.hasTextIndent) {
            target.textIndentHanging = rule.textIndentHanging;
            target.textIndentEachLine = rule.textIndentEachLine;
            if (rule.textIndentFromEm) {
                target.textIndentPx = EmToPx(target.fontSizePx, rule.textIndentEmMilli);
                target.textIndentMilli = -1;
            } else {
                target.textIndentPx = rule.textIndentPx;
                target.textIndentMilli = rule.textIndentMilli;
            }
        }
        if (rule.hasLineHeight) {
            if (rule.lineHeightMilli >= 0) {
                target.lineHeightPx = std::max(1, EmToPx(target.fontSizePx, rule.lineHeightMilli));
            } else {
                target.lineHeightPx = std::max(0, rule.lineHeightPx);
            }
        }
        if (rule.hasColor) {
            target.color = Color{rule.colorRed, rule.colorGreen, rule.colorBlue, 255};
        }
        if (rule.hasBackground) {
            target.background = Color{rule.backgroundRed, rule.backgroundGreen, rule.backgroundBlue, 255};
        }
        if (rule.displayKind == 0) {
            target.display = DisplayKind::None;
        } else if (rule.displayKind == 1) {
            target.display = DisplayKind::Block;
        } else if (rule.displayKind == 2) {
            target.display = DisplayKind::Inline;
        } else if (rule.displayKind == 3) {
            target.display = DisplayKind::InlineBlock;
        } else if (rule.displayKind == 4) {
            target.display = DisplayKind::ListItem;
        }
        if (rule.widthPx >= 0) {
            target.widthPx = rule.widthPx;
        }
        if (rule.heightPx >= 0) {
            target.heightPx = rule.heightPx;
        }
        if (rule.marginTopPx >= 0) {
            target.marginTop = rule.marginTopPx;
        }
        if (rule.marginRightPx >= 0) {
            target.marginRight = rule.marginRightPx;
        }
        if (rule.marginBottomPx >= 0) {
            target.marginBottom = rule.marginBottomPx;
        }
        if (rule.marginLeftPx >= 0) {
            target.marginLeft = rule.marginLeftPx;
        }
        if (rule.paddingTopPx >= 0) {
            target.paddingTop = rule.paddingTopPx;
        }
        if (rule.paddingRightPx >= 0) {
            target.paddingRight = rule.paddingRightPx;
        }
        if (rule.paddingBottomPx >= 0) {
            target.paddingBottom = rule.paddingBottomPx;
        }
        if (rule.paddingLeftPx >= 0) {
            target.paddingLeft = rule.paddingLeftPx;
        }
        if (rule.textAlign >= 0) {
            target.textAlign = rule.textAlign;
        }
        if (rule.decoration == 0) {
            target.underline = false;
            target.strike = false;
        } else if (rule.decoration == 1) {
            target.underline = true;
        } else if (rule.decoration == 2) {
            target.strike = true;
        }
        if (rule.whiteSpace == 0) {
            target.nowrap = false;
            target.pre = false;
        } else if (rule.whiteSpace == 1) {
            target.nowrap = true;
        } else if (rule.whiteSpace == 2) {
            target.pre = true;
            target.nowrap = true;
        }
        if (rule.borderPx >= 0) {
            target.border = rule.borderPx;
        }
    };
    auto absorbVariables = [](ResolvedStyle& target, const ElementStyle& rule) {
        for (const StyleDeclaration& declaration : rule.declarations) {
            if (declaration.name.size() < 2 || declaration.name[0] != '-' || declaration.name[1] != '-') {
                continue;
            }
            bool replaced = false;
            for (StyleDeclaration& existing : target.customProperties) {
                if (existing.name == declaration.name) {
                    existing.value = declaration.value;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                target.customProperties.push_back(declaration);
            }
        }
    };
    auto resolveLengths = [percentBase, percentBaseHeight](ResolvedStyle& target, const ElementStyle& rule) {
        int fontSize = target.fontSizePx;
        for (const StyleDeclaration& declaration : rule.declarations) {
            CssLengthContext context;
            context.fontSizePx = std::max(1, fontSize);
            context.rootFontSizePx = std::max(1, target.rootFontSizePx);
            context.lineHeightPx = target.lineHeightPx > 0 ? target.lineHeightPx : LineHeightPx(context.fontSizePx);
            context.rootLineHeightPx = LineHeightPx(context.rootFontSizePx);
            context.zeroAdvancePx = std::max(1, CharAdvancePx(context.fontSizePx, target.fontFamily));
            context.rootZeroAdvancePx = std::max(1, CharAdvancePx(context.rootFontSizePx, target.fontFamily));
            context.viewportWidth = target.viewportWidthPx;
            context.viewportHeight = target.viewportHeightPx;
            context.containerWidth = target.containerWidthPx;
            context.containerHeight = target.containerHeightPx;
            if (declaration.name == "font-size") {
                context.percentBase = context.fontSizePx;
            } else if (declaration.name == "height" || declaration.name == "min-height" || declaration.name == "max-height") {
                context.percentBase = percentBaseHeight;
            } else {
                context.percentBase = percentBase;
            }
            int pixels = 0;
            if (!EvaluateCssPx(declaration.value, target.customProperties, context, pixels)) {
                continue;
            }
            if (declaration.name == "font-size" && pixels > 0) {
                target.fontSizePx = pixels;
                fontSize = pixels;
            } else if (declaration.name == "width") {
                target.widthPx = std::max(0, pixels);
            } else if (declaration.name == "height") {
                target.heightPx = std::max(0, pixels);
            } else if (declaration.name == "margin-top") {
                target.marginTop = std::max(0, pixels);
            } else if (declaration.name == "margin-right") {
                target.marginRight = std::max(0, pixels);
            } else if (declaration.name == "margin-bottom") {
                target.marginBottom = std::max(0, pixels);
            } else if (declaration.name == "margin-left") {
                target.marginLeft = std::max(0, pixels);
            } else if (declaration.name == "padding-top") {
                target.paddingTop = std::max(0, pixels);
            } else if (declaration.name == "padding-right") {
                target.paddingRight = std::max(0, pixels);
            } else if (declaration.name == "padding-bottom") {
                target.paddingBottom = std::max(0, pixels);
            } else if (declaration.name == "padding-left") {
                target.paddingLeft = std::max(0, pixels);
            } else if (declaration.name == "border" || declaration.name == "border-width") {
                target.border = std::max(0, pixels);
            } else if (declaration.name == "min-width") {
                target.minWidthPx = std::max(0, pixels);
            } else if (declaration.name == "max-width") {
                target.maxWidthPx = std::max(0, pixels);
            } else if (declaration.name == "min-height") {
                target.minHeightPx = std::max(0, pixels);
            } else if (declaration.name == "max-height") {
                target.maxHeightPx = std::max(0, pixels);
            }
        }
        for (const StyleDeclaration& declaration : rule.declarations) {
            if (declaration.name != "color" && declaration.name != "background" && declaration.name != "background-color") {
                continue;
            }
            std::uint8_t red = 0;
            std::uint8_t green = 0;
            std::uint8_t blue = 0;
            if (!EvaluateCssColor(declaration.value, target.customProperties, red, green, blue)) {
                continue;
            }
            if (declaration.name == "color") {
                target.color = Color{red, green, blue, 255};
            } else {
                target.background = Color{red, green, blue, 255};
            }
        }
        for (const StyleDeclaration& declaration : rule.declarations) {
            CssLengthContext context;
            context.fontSizePx = std::max(1, target.fontSizePx);
            context.rootFontSizePx = std::max(1, target.rootFontSizePx);
            context.lineHeightPx = target.lineHeightPx > 0 ? target.lineHeightPx : LineHeightPx(context.fontSizePx);
            context.rootLineHeightPx = LineHeightPx(context.rootFontSizePx);
            context.zeroAdvancePx = std::max(1, CharAdvancePx(context.fontSizePx, target.fontFamily));
            context.rootZeroAdvancePx = std::max(1, CharAdvancePx(context.rootFontSizePx, target.fontFamily));
            context.viewportWidth = target.viewportWidthPx;
            context.viewportHeight = target.viewportHeightPx;
            context.containerWidth = target.containerWidthPx;
            context.containerHeight = target.containerHeightPx;
            context.percentBase = percentBase;
            if (declaration.name == "grid-template-columns" || declaration.name == "grid-template-rows") {
                std::vector<std::string>& tracksOut = declaration.name == "grid-template-rows" ? target.gridRows : target.gridTracks;
                tracksOut.clear();
                if (declaration.name == "grid-template-columns") {
                    target.gridColumns.clear();
                }
                const std::string tracks = declaration.value;
                std::size_t cursor = 0;
                while (cursor < tracks.size()) {
                    while (cursor < tracks.size() && tracks[cursor] == ' ') {
                        ++cursor;
                    }
                    if (cursor >= tracks.size()) {
                        break;
                    }
                    const std::string lower = ToLowerAscii(tracks.substr(cursor));
                    if (lower.compare(0, 7, "repeat(") == 0) {
                        cursor += 7;
                        int count = std::atoi(tracks.c_str() + cursor);
                        const std::size_t comma = tracks.find(',', cursor);
                        const std::size_t close = tracks.find(')', comma == std::string::npos ? cursor : comma);
                        std::string inner = comma == std::string::npos || close == std::string::npos ? std::string() : TrimSpaces(tracks.substr(comma + 1, close - comma - 1));
                        if (count < 1) {
                            count = 1;
                        }
                        for (int copy = 0; copy < count; ++copy) {
                            tracksOut.push_back(inner);
                        }
                        cursor = close == std::string::npos ? tracks.size() : close + 1;
                        continue;
                    }
                    int depth = 0;
                    const std::size_t start = cursor;
                    while (cursor < tracks.size() && !(depth == 0 && tracks[cursor] == ' ')) {
                        if (tracks[cursor] == '(') {
                            ++depth;
                        } else if (tracks[cursor] == ')' && depth > 0) {
                            --depth;
                        }
                        ++cursor;
                    }
                    tracksOut.push_back(TrimSpaces(tracks.substr(start, cursor - start)));
                }
            }
            if (declaration.name == "transform") {
                const std::string transform = ToLowerAscii(declaration.value);
                struct Plane {
                    double a = 1;
                    double b = 0;
                    double c = 0;
                    double d = 1;
                    double e = 0;
                    double f = 0;
                };
                auto multiply = [](Plane left, Plane right) {
                    Plane out;
                    out.a = left.a * right.a + left.c * right.b;
                    out.b = left.b * right.a + left.d * right.b;
                    out.c = left.a * right.c + left.c * right.d;
                    out.d = left.b * right.c + left.d * right.d;
                    out.e = left.a * right.e + left.c * right.f + left.e;
                    out.f = left.b * right.e + left.d * right.f + left.f;
                    return out;
                };
                auto readLength = [&](const std::string& text, double& pixels) {
                    int resolved = 0;
                    CssLengthContext shift = context;
                    shift.percentBase = percentBase;
                    std::string token = TrimSpaces(text);
                    if (!token.empty() && token.back() == ',') {
                        token.pop_back();
                    }
                    if (!EvaluateCssPx(token, target.customProperties, shift, resolved)) {
                        return false;
                    }
                    pixels = resolved;
                    return true;
                };
                auto readAngle = [](const std::string& text) {
                    const std::string lower = ToLowerAscii(TrimSpaces(text));
                    if (lower.size() > 3 && lower.compare(lower.size() - 3, 3, "rad") == 0) {
                        return std::atof(lower.c_str()) * 180.0 / 3.141592653589793;
                    }
                    if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, "turn") == 0) {
                        return std::atof(lower.c_str()) * 360.0;
                    }
                    if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, "grad") == 0) {
                        return std::atof(lower.c_str()) * 0.9;
                    }
                    return std::atof(lower.c_str());
                };
                auto splitArgs = [](const std::string& inner) {
                    std::vector<std::string> args;
                    std::string current;
                    for (char character : inner) {
                        if (character == ',') {
                            args.push_back(TrimSpaces(current));
                            current.clear();
                        } else {
                            current.push_back(character);
                        }
                    }
                    if (!TrimSpaces(current).empty()) {
                        args.push_back(TrimSpaces(current));
                    }
                    return args;
                };
                Plane plane;
                bool saw = false;
                std::size_t cursor = 0;
                while (cursor < transform.size()) {
                    while (cursor < transform.size() && transform[cursor] == ' ') {
                        ++cursor;
                    }
                    const std::size_t nameAt = cursor;
                    while (cursor < transform.size() && transform[cursor] != '(' && transform[cursor] != ' ') {
                        ++cursor;
                    }
                    if (cursor >= transform.size() || transform[cursor] != '(') {
                        break;
                    }
                    const std::string function = transform.substr(nameAt, cursor - nameAt);
                    int depth = 1;
                    const std::size_t innerAt = ++cursor;
                    while (cursor < transform.size() && depth > 0) {
                        if (transform[cursor] == '(') {
                            ++depth;
                        } else if (transform[cursor] == ')') {
                            --depth;
                        }
                        if (depth > 0) {
                            ++cursor;
                        }
                    }
                    const std::string inner = transform.substr(innerAt, cursor - innerAt);
                    if (cursor < transform.size() && transform[cursor] == ')') {
                        ++cursor;
                    }
                    const std::vector<std::string> args = splitArgs(inner);
                    Plane next;
                    if (function == "translate" || function == "translate3d") {
                        double dx = 0;
                        double dy = 0;
                        if (!args.empty()) {
                            readLength(args[0], dx);
                        }
                        if (args.size() >= 2) {
                            readLength(args[1], dy);
                        }
                        next.e = dx;
                        next.f = dy;
                    } else if (function == "translatex") {
                        double dx = 0;
                        if (!args.empty()) {
                            readLength(args[0], dx);
                        }
                        next.e = dx;
                    } else if (function == "translatey") {
                        double dy = 0;
                        if (!args.empty()) {
                            readLength(args[0], dy);
                        }
                        next.f = dy;
                    } else if (function == "scale" || function == "scale3d") {
                        const double sx = args.empty() ? 1 : std::atof(args[0].c_str());
                        const double sy = args.size() >= 2 ? std::atof(args[1].c_str()) : sx;
                        next.a = sx == 0 ? 0.0001 : sx;
                        next.d = sy == 0 ? 0.0001 : sy;
                    } else if (function == "scalex") {
                        const double sx = args.empty() ? 1 : std::atof(args[0].c_str());
                        next.a = sx == 0 ? 0.0001 : sx;
                    } else if (function == "scaley") {
                        const double sy = args.empty() ? 1 : std::atof(args[0].c_str());
                        next.d = sy == 0 ? 0.0001 : sy;
                    } else if (function == "rotate" || function == "rotatez") {
                        const double radians = (args.empty() ? 0 : readAngle(args[0])) * 3.141592653589793 / 180.0;
                        next.a = std::cos(radians);
                        next.b = std::sin(radians);
                        next.c = -std::sin(radians);
                        next.d = std::cos(radians);
                    } else if (function == "rotatex") {
                        const double radians = (args.empty() ? 0 : readAngle(args[0])) * 3.141592653589793 / 180.0;
                        next.d = std::cos(radians);
                    } else if (function == "rotatey") {
                        const double radians = (args.empty() ? 0 : readAngle(args[0])) * 3.141592653589793 / 180.0;
                        next.a = std::cos(radians);
                    } else if (function == "skewx") {
                        const double radians = (args.empty() ? 0 : readAngle(args[0])) * 3.141592653589793 / 180.0;
                        next.c = std::tan(radians);
                    } else if (function == "skewy") {
                        const double radians = (args.empty() ? 0 : readAngle(args[0])) * 3.141592653589793 / 180.0;
                        next.b = std::tan(radians);
                    } else if (function == "skew") {
                        const double xRadians = (args.empty() ? 0 : readAngle(args[0])) * 3.141592653589793 / 180.0;
                        const double yRadians = (args.size() >= 2 ? readAngle(args[1]) : 0) * 3.141592653589793 / 180.0;
                        next.c = std::tan(xRadians);
                        next.b = std::tan(yRadians);
                    } else if (function == "matrix" && args.size() >= 6) {
                        next.a = std::atof(args[0].c_str());
                        next.b = std::atof(args[1].c_str());
                        next.c = std::atof(args[2].c_str());
                        next.d = std::atof(args[3].c_str());
                        readLength(args[4], next.e);
                        readLength(args[5], next.f);
                    } else if (function == "matrix3d" && args.size() >= 16) {
                        next.a = std::atof(args[0].c_str());
                        next.b = std::atof(args[1].c_str());
                        next.c = std::atof(args[4].c_str());
                        next.d = std::atof(args[5].c_str());
                        readLength(args[12], next.e);
                        readLength(args[13], next.f);
                    } else if (function == "perspective" || function == "translatez" || function == "scalez" || function == "rotate3d") {
                        next = Plane();
                    } else {
                        continue;
                    }
                    plane = multiply(plane, next);
                    saw = true;
                }
                if (saw) {
                    const bool pureMove = std::fabs(plane.a - 1) < 0.001 && std::fabs(plane.b) < 0.001 && std::fabs(plane.c) < 0.001 &&
                        std::fabs(plane.d - 1) < 0.001;
                    if (pureMove) {
                        target.translateX += static_cast<int>(std::lround(plane.e));
                        target.translateY += static_cast<int>(std::lround(plane.f));
                    } else {
                        target.hasCssMatrix = true;
                        target.matrixA = plane.a;
                        target.matrixB = plane.b;
                        target.matrixC = plane.c;
                        target.matrixD = plane.d;
                        target.matrixE = plane.e;
                        target.matrixF = plane.f;
                        if (std::fabs(plane.a - plane.d) < 0.001 && std::fabs(plane.b) < 0.001 && std::fabs(plane.c) < 0.001) {
                            target.scaleMilli = static_cast<int>(plane.a * 1000.0 + 0.5);
                        }
                    }
                }
            }
            if (declaration.name == "animation-name") {
                target.animationName = ToLowerAscii(TrimSpaces(declaration.value));
            }
            if (declaration.name == "animation-duration") {
                const std::string timing = ToLowerAscii(TrimSpaces(declaration.value));
                if (timing.size() > 2 && timing.compare(timing.size() - 2, 2, "ms") == 0) {
                    target.animationDurationSec = std::atof(timing.c_str()) / 1000.0;
                } else {
                    target.animationDurationSec = std::atof(timing.c_str());
                }
            }
            if (declaration.name == "animation-timing-function") {
                const std::string timing = ToLowerAscii(TrimSpaces(declaration.value));
                target.easeKind = 0;
                target.easeX1 = 0;
                target.easeY1 = 0;
                target.easeX2 = 1;
                target.easeY2 = 1;
                if (timing == "ease") {
                    target.easeKind = 1;
                    target.easeX1 = 0.25;
                    target.easeY1 = 0.1;
                    target.easeX2 = 0.25;
                    target.easeY2 = 1;
                } else if (timing == "ease-in") {
                    target.easeKind = 2;
                    target.easeX1 = 0.42;
                    target.easeY1 = 0;
                    target.easeX2 = 1;
                    target.easeY2 = 1;
                } else if (timing == "ease-out") {
                    target.easeKind = 3;
                    target.easeX1 = 0;
                    target.easeY1 = 0;
                    target.easeX2 = 0.58;
                    target.easeY2 = 1;
                } else if (timing == "ease-in-out") {
                    target.easeKind = 4;
                    target.easeX1 = 0.42;
                    target.easeY1 = 0;
                    target.easeX2 = 0.58;
                    target.easeY2 = 1;
                } else if (timing == "step-start") {
                    target.easeKind = 5;
                } else if (timing == "step-end") {
                    target.easeKind = 6;
                } else if (timing.compare(0, 6, "steps(") == 0) {
                    target.easeKind = 7;
                    target.easeSteps = std::max(1, std::atoi(timing.c_str() + 6));
                } else if (timing.compare(0, 13, "cubic-bezier(") == 0) {
                    target.easeKind = 8;
                    double values[4] = {0, 0, 1, 1};
                    int slot = 0;
                    std::string number;
                    for (char character : timing.substr(13)) {
                        if ((character >= '0' && character <= '9') || character == '.' || character == '-') {
                            number.push_back(character);
                        } else if (!number.empty() && slot < 4) {
                            values[slot++] = std::atof(number.c_str());
                            number.clear();
                        }
                    }
                    target.easeX1 = values[0];
                    target.easeY1 = values[1];
                    target.easeX2 = values[2];
                    target.easeY2 = values[3];
                }
            }
            if (declaration.name == "position" && ToLowerAscii(declaration.value) == "absolute") {
                target.positionKind = 1;
            }
            if (declaration.name == "anchor-name") {
                target.anchorName = TrimSpaces(declaration.value);
            }
            if (declaration.name == "left" && ToLowerAscii(declaration.value).compare(0, 7, "anchor(") == 0) {
                target.anchorLeft = true;
                target.positionKind = 1;
                const std::size_t open = declaration.value.find('(');
                const std::size_t close = declaration.value.find(')', open);
                if (open != std::string::npos && close != std::string::npos) {
                    std::string inner = TrimSpaces(declaration.value.substr(open + 1, close - open - 1));
                    if (inner.size() >= 2 && inner[0] == '-' && inner[1] == '-') {
                        const std::size_t space = inner.find(' ');
                        target.anchorQuery = space == std::string::npos ? inner : inner.substr(0, space);
                    }
                }
            }
            if (declaration.name == "filter") {
                const std::string filter = ToLowerAscii(declaration.value);
                if (filter.compare(0, 5, "blur(") == 0) {
                    std::string radiusText = declaration.value.substr(5);
                    if (!radiusText.empty() && radiusText.back() == ')') {
                        radiusText.pop_back();
                    }
                    int radius = 0;
                    if (EvaluateCssPx(radiusText, target.customProperties, context, radius) && radius > 0) {
                        target.blurPx = radius;
                    }
                }
                if (filter.compare(0, 12, "drop-shadow(") == 0) {
                    std::string inner = declaration.value.substr(12);
                    const std::size_t close = inner.find(')');
                    if (close != std::string::npos) {
                        inner = inner.substr(0, close);
                    }
                    std::size_t piece = 0;
                    std::vector<std::string> parts;
                    while (piece < inner.size() && parts.size() < 4) {
                        while (piece < inner.size() && inner[piece] == ' ') {
                            ++piece;
                        }
                        const std::size_t start = piece;
                        while (piece < inner.size() && inner[piece] != ' ') {
                            ++piece;
                        }
                        if (start < piece) {
                            parts.push_back(inner.substr(start, piece - start));
                        }
                    }
                    int dx = 0;
                    int dy = 0;
                    if (parts.size() >= 2 && EvaluateCssPx(parts[0], target.customProperties, context, dx) &&
                        EvaluateCssPx(parts[1], target.customProperties, context, dy)) {
                        target.shadowDx = dx;
                        target.shadowDy = dy;
                        std::uint8_t red = 0;
                        std::uint8_t green = 0;
                        std::uint8_t blue = 0;
                        const std::string colorText = parts.size() >= 4 ? parts[3] : "#000000";
                        if (EvaluateCssColor(colorText, target.customProperties, red, green, blue)) {
                            target.shadowColor = Color{red, green, blue, 255};
                        }
                    }
                }
                auto readFilterAmount = [](const std::string& text) {
                    double amount = std::atof(text.c_str());
                    if (ToLowerAscii(text).find('%') != std::string::npos) {
                        amount /= 100.0;
                    }
                    return amount;
                };
                std::size_t filterCursor = 0;
                while (filterCursor < filter.size()) {
                    while (filterCursor < filter.size() && filter[filterCursor] == ' ') {
                        ++filterCursor;
                    }
                    const std::size_t nameAt = filterCursor;
                    while (filterCursor < filter.size() && filter[filterCursor] != '(' && filter[filterCursor] != ' ') {
                        ++filterCursor;
                    }
                    if (filterCursor >= filter.size() || filter[filterCursor] != '(') {
                        break;
                    }
                    const std::string function = filter.substr(nameAt, filterCursor - nameAt);
                    int depth = 1;
                    const std::size_t innerAt = ++filterCursor;
                    while (filterCursor < filter.size() && depth > 0) {
                        if (filter[filterCursor] == '(') {
                            ++depth;
                        } else if (filter[filterCursor] == ')') {
                            --depth;
                        }
                        if (depth > 0) {
                            ++filterCursor;
                        }
                    }
                    const std::string inner = filter.substr(innerAt, filterCursor - innerAt);
                    if (filterCursor < filter.size() && filter[filterCursor] == ')') {
                        ++filterCursor;
                    }
                    if (function == "grayscale") {
                        double amount = readFilterAmount(inner);
                        if (amount < 0) {
                            amount = 0;
                        }
                        if (amount > 1) {
                            amount = 1;
                        }
                        target.grayscaleMilli = static_cast<int>(amount * 1000);
                        target.hasColorFilter = true;
                    } else if (function == "brightness") {
                        double amount = readFilterAmount(inner);
                        if (amount < 0) {
                            amount = 0;
                        }
                        target.brightnessMilli = static_cast<int>(amount * 1000);
                        target.hasColorFilter = true;
                    } else if (function == "contrast") {
                        double amount = readFilterAmount(inner);
                        if (amount < 0) {
                            amount = 0;
                        }
                        target.contrastMilli = static_cast<int>(amount * 1000);
                        target.hasColorFilter = true;
                    } else if (function == "invert") {
                        double amount = readFilterAmount(inner);
                        if (amount < 0) {
                            amount = 0;
                        }
                        if (amount > 1) {
                            amount = 1;
                        }
                        target.invertMilli = static_cast<int>(amount * 1000);
                        target.hasColorFilter = true;
                    } else if (function == "saturate") {
                        double amount = readFilterAmount(inner);
                        if (amount < 0) {
                            amount = 0;
                        }
                        target.saturateMilli = static_cast<int>(amount * 1000);
                        target.hasColorFilter = true;
                    } else if (function == "sepia") {
                        double amount = readFilterAmount(inner);
                        if (amount < 0) {
                            amount = 0;
                        }
                        if (amount > 1) {
                            amount = 1;
                        }
                        target.sepiaMilli = static_cast<int>(amount * 1000);
                        target.hasColorFilter = true;
                    } else if (function == "hue-rotate") {
                        const std::string angle = ToLowerAscii(inner);
                        double degrees = std::atof(angle.c_str());
                        if (angle.find("rad") != std::string::npos) {
                            degrees = degrees * 180.0 / 3.141592653589793;
                        } else if (angle.find("turn") != std::string::npos) {
                            degrees *= 360.0;
                        } else if (angle.find("grad") != std::string::npos) {
                            degrees *= 0.9;
                        }
                        target.hueRotateDeg = static_cast<int>(std::lround(degrees));
                        target.hasColorFilter = true;
                    } else if (function == "opacity") {
                        double amount = readFilterAmount(inner);
                        if (amount < 0) {
                            amount = 0;
                        }
                        if (amount > 1) {
                            amount = 1;
                        }
                        target.filterOpacityMilli = static_cast<int>(amount * 1000);
                        target.hasColorFilter = true;
                    }
                }
            }
            if (declaration.name == "opacity") {
                double amount = std::atof(declaration.value.c_str());
                if (ToLowerAscii(declaration.value).find('%') != std::string::npos) {
                    amount /= 100.0;
                }
                if (amount < 0) {
                    amount = 0;
                }
                if (amount > 1) {
                    amount = 1;
                }
                target.opacityMilli = static_cast<int>(amount * 1000);
            }
            if (declaration.name == "visibility") {
                const std::string visibility = ToLowerAscii(TrimSpaces(declaration.value));
                if (visibility == "hidden" || visibility == "collapse") {
                    target.visibilityHidden = true;
                    target.visibilityVisible = false;
                } else if (visibility == "visible") {
                    target.visibilityHidden = false;
                    target.visibilityVisible = true;
                }
            }
            if (declaration.name == "background" || declaration.name == "background-image") {
                const std::string lower = ToLowerAscii(declaration.value);
                const bool repeating = lower.compare(0, 26, "repeating-linear-gradient(") == 0;
                const bool radial = lower.compare(0, 16, "radial-gradient(") == 0 || lower.compare(0, 26, "repeating-radial-gradient(") == 0;
                const bool linear = lower.compare(0, 16, "linear-gradient(") == 0 || repeating;
                if ((linear || radial) && !declaration.value.empty() && declaration.value.back() == ')') {
                    const std::size_t open = declaration.value.find('(');
                    std::string inner = TrimSpaces(declaration.value.substr(open + 1, declaration.value.size() - open - 2));
                    bool across = false;
                    const std::string direction = ToLowerAscii(inner);
                    if (direction.compare(0, 8, "to right") == 0) {
                        across = true;
                        inner = TrimSpaces(inner.substr(8));
                        if (!inner.empty() && inner.front() == ',') {
                            inner = TrimSpaces(inner.substr(1));
                        }
                    } else if (direction.compare(0, 9, "to bottom") == 0) {
                        inner = TrimSpaces(inner.substr(9));
                        if (!inner.empty() && inner.front() == ',') {
                            inner = TrimSpaces(inner.substr(1));
                        }
                    }
                    std::vector<std::string> stops;
                    std::string currentStop;
                    int depth = 0;
                    for (char character : inner) {
                        if (character == '(') {
                            ++depth;
                        } else if (character == ')' && depth > 0) {
                            --depth;
                        }
                        if (character == ',' && depth == 0) {
                            stops.push_back(TrimSpaces(currentStop));
                            currentStop.clear();
                        } else {
                            currentStop.push_back(character);
                        }
                    }
                    if (!TrimSpaces(currentStop).empty()) {
                        stops.push_back(TrimSpaces(currentStop));
                    }
                    target.gradientColors.clear();
                    target.gradientStopMilli.clear();
                    target.gradientStopPx.clear();
                    int period = 0;
                    for (const std::string& stop : stops) {
                        std::string colorText = stop;
                        int milli = -1;
                        int pixels = -1;
                        const std::size_t space = stop.find_last_of(' ');
                        if (space != std::string::npos) {
                            const std::string position = TrimSpaces(stop.substr(space + 1));
                            if (!position.empty() && position.back() == '%') {
                                milli = static_cast<int>(std::atof(position.c_str()) * 10.0);
                                colorText = TrimSpaces(stop.substr(0, space));
                            } else {
                                int resolved = 0;
                                if (EvaluateCssPx(position, target.customProperties, context, resolved) && resolved >= 0) {
                                    pixels = resolved;
                                    period = resolved;
                                    colorText = TrimSpaces(stop.substr(0, space));
                                }
                            }
                        }
                        std::uint8_t red = 0;
                        std::uint8_t green = 0;
                        std::uint8_t blue = 0;
                        if (!EvaluateCssColor(colorText, target.customProperties, red, green, blue)) {
                            continue;
                        }
                        target.gradientColors.push_back(Color{red, green, blue, 255});
                        target.gradientStopMilli.push_back(milli);
                        target.gradientStopPx.push_back(pixels);
                    }
                    if (target.gradientColors.size() >= 2) {
                        target.linearGradient = true;
                        target.radialGradient = radial;
                        target.repeatingGradient = repeating || lower.compare(0, 26, "repeating-radial-gradient(") == 0;
                        target.gradientPeriod = period;
                        target.gradientAcross = across || radial;
                        target.gradientStart = target.gradientColors.front();
                        target.gradientEnd = target.gradientColors.back();
                        target.background = target.gradientStart;
                    }
                }
            }
            if (declaration.name == "container-type") {
                const std::string kind = ToLowerAscii(declaration.value);
                if (kind.find("size") != std::string::npos || kind.find("inline-size") != std::string::npos) {
                    target.isContainer = true;
                }
            }
        }
    };
    auto matches = [](const Node& current, const std::string& selector) {
        if (selector == ":root") {
            return current.parent != nullptr && current.parent->type == Node::Type::Document;
        }
        if (selector == "*") {
            return true;
        }
        if (!selector.empty() && selector[0] == '#') {
            return AttributeValue(current, "id") == selector.substr(1);
        }
        if (!selector.empty() && selector[0] == '.') {
            const std::string classes = " " + AttributeValue(current, "class") + " ";
            return classes.find(" " + selector.substr(1) + " ") != std::string::npos;
        }
        std::string head = selector;
        std::string wantedLanguage;
        const std::size_t langAt = selector.find(":lang(");
        if (langAt != std::string::npos) {
            head = selector.substr(0, langAt);
            const std::size_t close = selector.find(')', langAt);
            wantedLanguage = ToLowerAscii(selector.substr(langAt + 6, close == std::string::npos ? std::string::npos : close - (langAt + 6)));
        }
        if (!wantedLanguage.empty()) {
            bool languageOk = false;
            for (const Node* cursor = &current; cursor != nullptr; cursor = cursor->parent) {
                std::string spoken = ToLowerAscii(AttributeValue(*cursor, "xml:lang"));
                if (spoken.empty()) {
                    spoken = ToLowerAscii(AttributeValue(*cursor, "lang"));
                }
                if (spoken == wantedLanguage || (spoken.size() > wantedLanguage.size() && spoken.compare(0, wantedLanguage.size(), wantedLanguage) == 0 &&
                                                    spoken[wantedLanguage.size()] == '-')) {
                    languageOk = true;
                    break;
                }
            }
            if (!languageOk) {
                return false;
            }
        }
        if (head.empty()) {
            return true;
        }
        const std::size_t dot = head.find('.');
        const std::string tag = dot == std::string::npos ? head : head.substr(0, dot);
        if (tag != ToLowerAscii(current.tagName)) {
            return false;
        }
        if (dot == std::string::npos) {
            return true;
        }
        const std::string classes = " " + AttributeValue(current, "class") + " ";
        return classes.find(" " + head.substr(dot + 1) + " ") != std::string::npos;
    };
    std::vector<const Node*> mathChain;
    for (const Node* cursor = &node; cursor != nullptr; cursor = cursor->parent) {
        mathChain.push_back(cursor);
    }
    for (std::size_t chainIndex = mathChain.size(); chainIndex > 0; --chainIndex) {
        const Node& source = *mathChain[chainIndex - 1];
        Color mathColor{0, 0, 0, 255};
        if (ParseColor(AttributeValue(source, "mathcolor"), mathColor)) {
            style.color = mathColor;
        }
        if (ParseColor(AttributeValue(source, "mathbackground"), mathColor)) {
            style.background = mathColor;
        }
        const std::string mathSize = ToLowerAscii(AttributeValue(source, "mathsize"));
        if (mathSize == "small") {
            style.fontSizePx = std::max(1, style.fontSizePx * 7 / 10);
        } else if (mathSize == "big") {
            style.fontSizePx = std::max(1, style.fontSizePx * 14 / 10);
        } else if (!mathSize.empty()) {
            const int size = std::atoi(mathSize.c_str());
            if (size > 0) {
                style.fontSizePx = size;
            }
        }
        if (ToLowerAscii(source.tagName) == "mstyle") {
            if (ParseColor(AttributeValue(source, "color"), mathColor)) {
                style.color = mathColor;
            }
            if (ParseColor(AttributeValue(source, "background"), mathColor)) {
                style.background = mathColor;
            }
            const std::string weight = ToLowerAscii(AttributeValue(source, "fontweight"));
            if (weight == "bold" || weight == "bolder") {
                style.bold = true;
            } else if (weight == "normal") {
                style.bold = false;
            }
            const std::string fontStyle = ToLowerAscii(AttributeValue(source, "fontstyle"));
            if (fontStyle == "italic") {
                style.italic = true;
            } else if (fontStyle == "normal") {
                style.italic = false;
            }
            if (HasAttribute(source, "fontsize")) {
                const int size = std::atoi(AttributeValue(source, "fontsize").c_str());
                if (size > 0) {
                    style.fontSizePx = size;
                }
            }
        }
    }
    int ancestorDepth = 0;
    int scriptDepth = 0;
    std::vector<const Node*> scriptChain;
    for (const Node* cursor = node.parent; cursor != nullptr; cursor = cursor->parent) {
        scriptChain.push_back(cursor);
    }
    for (std::size_t chainIndex = scriptChain.size(); chainIndex > 0; --chainIndex) {
        const Node& source = *scriptChain[chainIndex - 1];
        if (!HasAttribute(source, "scriptlevel")) {
            continue;
        }
        const std::string level = AttributeValue(source, "scriptlevel");
        if (level.empty()) {
            continue;
        }
        if (level[0] == '+' || level[0] == '-') {
            ancestorDepth += std::atoi(level.c_str());
        } else {
            ancestorDepth = std::atoi(level.c_str());
        }
    }
    scriptDepth = ancestorDepth;
    if (HasAttribute(node, "scriptlevel")) {
        const std::string level = AttributeValue(node, "scriptlevel");
        if (!level.empty() && (level[0] == '+' || level[0] == '-')) {
            scriptDepth += std::atoi(level.c_str());
        } else if (!level.empty()) {
            scriptDepth = std::atoi(level.c_str());
        }
    }
    if (scriptDepth != ancestorDepth) {
        int nextSize = style.fontSizePx;
        const int steps = scriptDepth > ancestorDepth ? scriptDepth - ancestorDepth : ancestorDepth - scriptDepth;
        for (int step = 0; step < steps; ++step) {
            nextSize = scriptDepth > ancestorDepth ? std::max(1, nextSize * 7 / 10) : std::max(1, nextSize * 10 / 7);
        }
        style.fontSizePx = nextSize;
    }
    std::vector<const Node*> ancestors;
    for (const Node* cursor = node.parent; cursor != nullptr; cursor = cursor->parent) {
        if (cursor->type == Node::Type::Element) {
            ancestors.push_back(cursor);
        }
    }
    for (std::size_t ancestorIndex = ancestors.size(); ancestorIndex > 0; --ancestorIndex) {
        const Node& ancestor = *ancestors[ancestorIndex - 1];
        for (const ElementStyle& rule : document.styleRules) {
            if (matches(ancestor, rule.selector)) {
                absorbVariables(style, rule);
            }
        }
        if (HasAttribute(ancestor, "style")) {
            ElementStyle ancestorStyle;
            ApplyStyleText(ancestorStyle, AttributeValue(ancestor, "style"));
            absorbVariables(style, ancestorStyle);
        }
    }
    for (const ElementStyle& rule : document.styleRules) {
        if (matches(node, rule.selector)) {
            absorbVariables(style, rule);
            applyRule(style, rule);
            resolveLengths(style, rule);
        }
    }
    if (HasAttribute(node, "style")) {
        ElementStyle inlineRule;
        ApplyStyleText(inlineRule, AttributeValue(node, "style"));
        absorbVariables(style, inlineRule);
        applyRule(style, inlineRule);
        resolveLengths(style, inlineRule);
    }
    if (!style.animationName.empty() && style.animationDurationSec > 0) {
        double progress = gSvgAnimationTime / style.animationDurationSec;
        if (progress < 0) {
            progress = 0;
        }
        if (progress > 1) {
            progress = 1;
        }
        if (style.easeKind == 5) {
            progress = progress <= 0 ? 0 : 1;
        } else if (style.easeKind == 6) {
            progress = progress >= 1 ? 1 : 0;
        } else if (style.easeKind == 7) {
            progress = progress >= 1 ? 1 : std::floor(progress * style.easeSteps) / style.easeSteps;
        } else if (style.easeKind == 1 || style.easeKind == 2 || style.easeKind == 3 || style.easeKind == 4 || style.easeKind == 8) {
            auto curve = [](double amount, double first, double second) {
                const double remain = 1 - amount;
                return 3 * remain * remain * amount * first + 3 * remain * amount * amount * second + amount * amount * amount;
            };
            double low = 0;
            double high = 1;
            for (int step = 0; step < 24; ++step) {
                const double mid = (low + high) * 0.5;
                if (curve(mid, style.easeX1, style.easeX2) < progress) {
                    low = mid;
                } else {
                    high = mid;
                }
            }
            progress = curve((low + high) * 0.5, style.easeY1, style.easeY2);
            if (progress < 0) {
                progress = 0;
            }
            if (progress > 1) {
                progress = 1;
            }
        }
        const ElementStyle* fromRule = nullptr;
        const ElementStyle* toRule = nullptr;
        for (const ElementStyle& rule : document.styleRules) {
            if (rule.selector == "@" + style.animationName + "@0") {
                fromRule = &rule;
            }
            if (rule.selector == "@" + style.animationName + "@100") {
                toRule = &rule;
            }
        }
        auto keyframeLength = [&](const ElementStyle* rule, const char* property, int& pixels) {
            if (rule == nullptr) {
                return false;
            }
            if (std::string(property) == "width" && rule->widthPx >= 0) {
                pixels = rule->widthPx;
                return true;
            }
            if (std::string(property) == "height" && rule->heightPx >= 0) {
                pixels = rule->heightPx;
                return true;
            }
            for (const StyleDeclaration& declaration : rule->declarations) {
                if (ToLowerAscii(declaration.name) != property) {
                    continue;
                }
                CssLengthContext lengthContext;
                lengthContext.fontSizePx = std::max(1, style.fontSizePx);
                lengthContext.rootFontSizePx = std::max(1, style.rootFontSizePx);
                lengthContext.viewportWidth = style.viewportWidthPx;
                lengthContext.viewportHeight = style.viewportHeightPx;
                lengthContext.percentBase = percentBase > 0 ? percentBase : style.viewportWidthPx;
                return EvaluateCssPx(declaration.value, style.customProperties, lengthContext, pixels);
            }
            return false;
        };
        int fromWidth = 0;
        int toWidth = 0;
        if (keyframeLength(fromRule, "width", fromWidth) && keyframeLength(toRule, "width", toWidth)) {
            style.widthPx = fromWidth + static_cast<int>(std::lround((toWidth - fromWidth) * progress));
        }
        int fromHeight = 0;
        int toHeight = 0;
        if (keyframeLength(fromRule, "height", fromHeight) && keyframeLength(toRule, "height", toHeight)) {
            style.heightPx = fromHeight + static_cast<int>(std::lround((toHeight - fromHeight) * progress));
        }
        if (fromRule != nullptr && toRule != nullptr && fromRule->hasBackground && toRule->hasBackground) {
            auto mixChannel = [&](int from, int to) {
                return static_cast<std::uint8_t>(std::lround(from + (to - from) * progress));
            };
            style.background = Color{
                mixChannel(fromRule->backgroundRed, toRule->backgroundRed),
                mixChannel(fromRule->backgroundGreen, toRule->backgroundGreen),
                mixChannel(fromRule->backgroundBlue, toRule->backgroundBlue),
                255};
        }
    }
    return style;
}

void SetSvgAnimationTime(double seconds) {
    gSvgAnimationTime = seconds < 0 ? 0 : seconds;
}

double SvgAnimationTime() {
    return gSvgAnimationTime;
}

LayoutResult LayoutDocument(Document& document, int viewportWidth, int viewportHeight) {
    ClearResourceFetchPlan();
    LoadDocumentStyles(document);
    RunDocumentScripts(document);
    LayoutResult result;
    result.viewportWidth = std::max(MIN_VIEWPORT_PX, std::min(MAX_VIEWPORT_PX, viewportWidth));
    result.viewportHeight = std::max(MIN_VIEWPORT_PX, std::min(MAX_VIEWPORT_PX, viewportHeight));
    result.contentWidth = result.viewportWidth;
    if (document.root == nullptr) {
        return result;
    }
    Engine engine;
    engine.document = &document;
    engine.result = &result;
    engine.viewportWidth = result.viewportWidth;
    engine.viewportHeight = result.viewportHeight;
    Node* html = nullptr;
    Node* body = nullptr;
    for (const std::unique_ptr<Node>& child : document.root->children) {
        if (child->kind == ElementKind::Html) {
            html = child.get();
        }
    }
    if (html != nullptr) {
        for (const std::unique_ptr<Node>& child : html->children) {
            if (child->kind == ElementKind::Body) {
                body = child.get();
            }
        }
    }
    engine.Fill(0, 0, result.viewportWidth, result.viewportHeight, Color{255, 255, 255, 255}, true);
    if (body == nullptr) {
        result.contentHeight = 0;
        return result;
    }
    ResolvedStyle rootStyle = DefaultStyle();
    rootStyle.viewportWidthPx = result.viewportWidth;
    rootStyle.viewportHeightPx = result.viewportHeight;
    if (html != nullptr) {
        const ResolvedStyle htmlStyle = ResolveStyle(*html, rootStyle, document, result.viewportWidth);
        rootStyle.rootFontSizePx = htmlStyle.fontSizePx;
    }
    ResolvedStyle bodyStyle = ResolveStyle(*body, rootStyle, document, result.viewportWidth);
    if (bodyStyle.background.alpha != 0) {
        engine.Fill(0, 0, result.viewportWidth, result.viewportHeight, bodyStyle.background, true);
    }
    int firstMargin = 0;
    bool foundFirst = false;
    for (const std::unique_ptr<Node>& child : body->children) {
        if (child->type == Node::Type::Element && !IsHidden(*child) && child->kind != ElementKind::Dialog) {
            firstMargin = ResolveStyle(*child, bodyStyle, document).marginTop;
            foundFirst = true;
            break;
        }
        if (child->type == Node::Type::Text && CollapsedText(child->text, false).find_first_not_of(' ') != std::string::npos) {
            foundFirst = true;
            break;
        }
    }
    const int collapsedTop = foundFirst ? Collapse(bodyStyle.marginTop, firstMargin) : bodyStyle.marginTop;
    const int bodyX = bodyStyle.marginLeft;
    const int bodyY = collapsedTop;
    const int bodyWidth = std::max(0, result.viewportWidth - bodyStyle.marginLeft - bodyStyle.marginRight);
    const int contentHeight = engine.LayoutFlow(*body, bodyStyle, bodyX, bodyY, bodyWidth, false, true);
    int lastMargin = 0;
    for (int index = static_cast<int>(body->children.size()) - 1; index >= 0; --index) {
        Node& child = *body->children[static_cast<std::size_t>(index)];
        if (child.type == Node::Type::Element && !IsHidden(child) && child.kind != ElementKind::Dialog) {
            lastMargin = ResolveStyle(child, bodyStyle, document).marginBottom;
            break;
        }
    }
    const int collapsedBottom = Collapse(bodyStyle.marginBottom, lastMargin);
    engine.AddBox(*body, bodyX, bodyY, bodyWidth, contentHeight, bodyStyle, false);
    if (html != nullptr) {
        engine.AddBox(*html, 0, 0, result.viewportWidth, bodyY + contentHeight + collapsedBottom, rootStyle, false);
    }
    result.contentHeight = bodyY + contentHeight + collapsedBottom;
    for (Node* overlay : engine.overlays) {
        ResolvedStyle overlayStyle = ResolveStyle(*overlay, rootStyle, document);
        const int overlayWidth = std::min(480, result.viewportWidth - 40);
        const int overlayX = (result.viewportWidth - overlayWidth) / 2;
        const int overlayY = 40;
        engine.LayoutBlock(*overlay, overlayStyle, overlayX, overlayY, overlayWidth, true, false);
    }
    return result;
}

const ElementBox* FindBox(const LayoutResult& layout, const std::string& tag) {
    for (const ElementBox& box : layout.boxes) {
        if (box.tag == tag) {
            return &box;
        }
    }
    return nullptr;
}

}
