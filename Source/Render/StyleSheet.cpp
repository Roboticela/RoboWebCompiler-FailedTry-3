#include "Render/StyleSheet.h"

#include "Core/Utf8.h"
#include "Render/ImageDecoder.h"

#include <algorithm>
#include <cmath>
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

struct CssQty {
    double value = 0;
    double percent = 0;
    int unit = 0;
    bool ok = false;
};

const int CSS_NUMBER = 0;
const int CSS_PX = 1;
const int CSS_DEG = 2;

const CssLengthContext* gCssLength = nullptr;

bool FoldCssLength(CssQty& quantity) {
    if (quantity.unit != CSS_PX || quantity.percent == 0) {
        return true;
    }
    if (gCssLength == nullptr || gCssLength->percentBase < 0) {
        return false;
    }
    quantity.value += quantity.percent * gCssLength->percentBase / 100.0;
    quantity.percent = 0;
    return true;
}

double CssViewportPart(double number, bool widthAxis, bool smallSide) {
    if (gCssLength == nullptr) {
        return 0;
    }
    const double width = gCssLength->viewportWidth;
    const double height = gCssLength->viewportHeight;
    double basis = widthAxis ? width : height;
    if (smallSide) {
        basis = width < height ? width : height;
    }
    return number * basis / 100.0;
}

void SkipCssSpace(const std::string& text, std::size_t& index) {
    while (index < text.size() && (text[index] == ' ' || text[index] == '\t' || text[index] == '\n' || text[index] == '\r')) {
        ++index;
    }
}

bool ReadCssIdent(const std::string& text, std::size_t& index, std::string& name) {
    SkipCssSpace(text, index);
    if (index >= text.size() || !((text[index] >= 'A' && text[index] <= 'Z') || (text[index] >= 'a' && text[index] <= 'z') || text[index] == '_')) {
        return false;
    }
    name.clear();
    while (index < text.size()) {
        const char character = text[index];
        if ((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-' ||
            character == '_') {
            name.push_back(character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character);
            ++index;
            continue;
        }
        break;
    }
    return !name.empty();
}

bool SubstituteCssVars(const std::string& text, const std::vector<StyleDeclaration>& variables, int depth, std::string& output) {
    if (depth > 16) {
        return false;
    }
    output.clear();
    for (std::size_t index = 0; index < text.size();) {
        const bool call = index + 4 <= text.size() && ToLowerAscii(text.substr(index, 4)) == "var(";
        if (!call) {
            output.push_back(text[index]);
            ++index;
            continue;
        }
        std::size_t cursor = index + 4;
        int depthCount = 1;
        bool quoted = false;
        char quote = 0;
        for (; cursor < text.size(); ++cursor) {
            const char character = text[cursor];
            if (quoted) {
                if (character == quote) {
                    quoted = false;
                }
                continue;
            }
            if (character == '"' || character == '\'') {
                quoted = true;
                quote = character;
                continue;
            }
            if (character == '(') {
                ++depthCount;
            } else if (character == ')') {
                --depthCount;
                if (depthCount == 0) {
                    break;
                }
            }
        }
        if (cursor >= text.size()) {
            return false;
        }
        const std::string inner = text.substr(index + 4, cursor - (index + 4));
        std::string name;
        std::string fallback;
        bool sawComma = false;
        int argumentDepth = 0;
        bool argumentQuoted = false;
        char argumentQuote = 0;
        for (std::size_t piece = 0; piece < inner.size(); ++piece) {
            const char character = inner[piece];
            if (argumentQuoted) {
                if (character == argumentQuote) {
                    argumentQuoted = false;
                }
                if (!sawComma) {
                    name.push_back(character);
                } else {
                    fallback.push_back(character);
                }
                continue;
            }
            if (character == '"' || character == '\'') {
                argumentQuoted = true;
                argumentQuote = character;
            }
            if (character == '(') {
                ++argumentDepth;
            } else if (character == ')' && argumentDepth > 0) {
                --argumentDepth;
            }
            if (character == ',' && argumentDepth == 0 && !sawComma) {
                sawComma = true;
                continue;
            }
            if (!sawComma) {
                name.push_back(character);
            } else {
                fallback.push_back(character);
            }
        }
        name = Trim(name);
        fallback = Trim(fallback);
        std::string replacement;
        bool found = false;
        for (const StyleDeclaration& declaration : variables) {
            if (declaration.name == name) {
                replacement = declaration.value;
                found = true;
            }
        }
        if (!found) {
            if (!sawComma) {
                return false;
            }
            replacement = fallback;
        }
        std::string resolved;
        if (!SubstituteCssVars(replacement, variables, depth + 1, resolved)) {
            return false;
        }
        output += resolved;
        index = cursor + 1;
    }
    return true;
}

bool ParseCssSum(const std::string& text, std::size_t& index, int fontSizePx, CssQty& quantity);

bool ParseCssFunction(const std::string& name, const std::string& text, std::size_t& index, int fontSizePx, CssQty& quantity) {
    if (name == "calc") {
        if (!ParseCssSum(text, index, fontSizePx, quantity)) {
            return false;
        }
    } else         if (name == "min" || name == "max" || name == "hypot") {
        CssQty current;
        if (!ParseCssSum(text, index, fontSizePx, current) || !FoldCssLength(current)) {
            return false;
        }
        double squares = current.value * current.value;
        SkipCssSpace(text, index);
        while (index < text.size() && text[index] == ',') {
            ++index;
            CssQty next;
            if (!ParseCssSum(text, index, fontSizePx, next) || !FoldCssLength(next) || next.unit != current.unit) {
                return false;
            }
            if (name == "min" && next.value < current.value) {
                current = next;
            }
            if (name == "max" && next.value > current.value) {
                current = next;
            }
            if (name == "hypot") {
                squares += next.value * next.value;
            }
            SkipCssSpace(text, index);
        }
        quantity = current;
        if (name == "hypot") {
            quantity.value = std::sqrt(squares);
        }
    } else if (name == "clamp") {
        CssQty low;
        CssQty preferred;
        CssQty high;
        if (!ParseCssSum(text, index, fontSizePx, low) || !FoldCssLength(low)) {
            return false;
        }
        SkipCssSpace(text, index);
        if (index >= text.size() || text[index] != ',') {
            return false;
        }
        ++index;
        if (!ParseCssSum(text, index, fontSizePx, preferred) || !FoldCssLength(preferred) || preferred.unit != low.unit) {
            return false;
        }
        SkipCssSpace(text, index);
        if (index >= text.size() || text[index] != ',') {
            return false;
        }
        ++index;
        if (!ParseCssSum(text, index, fontSizePx, high) || !FoldCssLength(high) || high.unit != low.unit) {
            return false;
        }
        quantity = preferred;
        if (quantity.value < low.value) {
            quantity = low;
        }
        if (quantity.value > high.value) {
            quantity = high;
        }
    } else if (name == "round") {
        std::string strategy = "nearest";
        const std::size_t mark = index;
        std::string keyword;
        if (ReadCssIdent(text, index, keyword) && (keyword == "nearest" || keyword == "up" || keyword == "down" || keyword == "to-zero")) {
            strategy = keyword;
            SkipCssSpace(text, index);
            if (index < text.size() && text[index] == ',') {
                ++index;
            }
        } else {
            index = mark;
        }
        if (!ParseCssSum(text, index, fontSizePx, quantity) || !FoldCssLength(quantity)) {
            return false;
        }
        CssQty step;
        SkipCssSpace(text, index);
        if (index < text.size() && text[index] == ',') {
            ++index;
            if (!ParseCssSum(text, index, fontSizePx, step) || !FoldCssLength(step) || step.unit != quantity.unit || step.value == 0) {
                return false;
            }
        } else if (quantity.unit == CSS_NUMBER) {
            step.value = 1;
            step.unit = CSS_NUMBER;
            step.ok = true;
        } else {
            return false;
        }
        const double quotient = quantity.value / step.value;
        double rounded = std::floor(quotient + 0.5);
        if (strategy == "up") {
            rounded = std::ceil(quotient - 1e-9);
        } else if (strategy == "down") {
            rounded = std::floor(quotient + 1e-9);
        } else if (strategy == "to-zero") {
            rounded = std::trunc(quotient);
        }
        quantity.value = rounded * step.value;
    } else if (name == "mod" || name == "rem") {
        CssQty dividend;
        CssQty divisor;
        if (!ParseCssSum(text, index, fontSizePx, dividend) || !FoldCssLength(dividend)) {
            return false;
        }
        SkipCssSpace(text, index);
        if (index >= text.size() || text[index] != ',') {
            return false;
        }
        ++index;
        if (!ParseCssSum(text, index, fontSizePx, divisor) || !FoldCssLength(divisor) || divisor.unit != dividend.unit || divisor.value == 0) {
            return false;
        }
        double remainder = std::fmod(dividend.value, divisor.value);
        if (name == "mod" && ((remainder < 0 && divisor.value > 0) || (remainder > 0 && divisor.value < 0))) {
            remainder += divisor.value;
        }
        quantity = dividend;
        quantity.value = remainder;
    } else if (name == "progress") {
        CssQty value;
        CssQty start;
        CssQty end;
        if (!ParseCssSum(text, index, fontSizePx, value) || !FoldCssLength(value)) {
            return false;
        }
        SkipCssSpace(text, index);
        if (index >= text.size() || text[index] != ',') {
            return false;
        }
        ++index;
        if (!ParseCssSum(text, index, fontSizePx, start) || !FoldCssLength(start) || start.unit != value.unit) {
            return false;
        }
        SkipCssSpace(text, index);
        if (index >= text.size() || text[index] != ',') {
            return false;
        }
        ++index;
        if (!ParseCssSum(text, index, fontSizePx, end) || !FoldCssLength(end) || end.unit != value.unit || end.value == start.value) {
            return false;
        }
        quantity.value = (value.value - start.value) / (end.value - start.value);
        quantity.unit = CSS_NUMBER;
        quantity.ok = true;
    } else if (name == "abs" || name == "sign") {
        if (!ParseCssSum(text, index, fontSizePx, quantity)) {
            return false;
        }
        if (name == "abs") {
            quantity.value = std::fabs(quantity.value);
        } else {
            quantity.unit = CSS_NUMBER;
            quantity.value = quantity.value < 0 ? -1 : (quantity.value > 0 ? 1 : 0);
        }
    } else if (name == "sin" || name == "cos" || name == "tan" || name == "asin" || name == "acos" || name == "atan") {
        if (!ParseCssSum(text, index, fontSizePx, quantity)) {
            return false;
        }
        double radians = quantity.value;
        if (quantity.unit == CSS_DEG) {
            radians = quantity.value * std::acos(-1.0) / 180.0;
        } else if (quantity.unit != CSS_NUMBER) {
            return false;
        }
        if (name == "sin") {
            quantity.value = std::sin(radians);
        } else if (name == "cos") {
            quantity.value = std::cos(radians);
        } else if (name == "tan") {
            quantity.value = std::tan(radians);
        } else if (name == "asin") {
            quantity.value = std::asin(radians) * 180.0 / std::acos(-1.0);
        } else if (name == "acos") {
            quantity.value = std::acos(radians) * 180.0 / std::acos(-1.0);
        } else {
            quantity.value = std::atan(radians) * 180.0 / std::acos(-1.0);
        }
        quantity.unit = name == "sin" || name == "cos" || name == "tan" ? CSS_NUMBER : CSS_DEG;
        quantity.ok = true;
    } else if (name == "atan2") {
        CssQty yValue;
        CssQty xValue;
        if (!ParseCssSum(text, index, fontSizePx, yValue)) {
            return false;
        }
        SkipCssSpace(text, index);
        if (index >= text.size() || text[index] != ',') {
            return false;
        }
        ++index;
        if (!ParseCssSum(text, index, fontSizePx, xValue) || xValue.unit != yValue.unit) {
            return false;
        }
        quantity.value = std::atan2(yValue.value, xValue.value) * 180.0 / std::acos(-1.0);
        quantity.unit = CSS_DEG;
        quantity.ok = true;
    } else if (name == "pow" || name == "sqrt" || name == "log" || name == "exp") {
        if (!ParseCssSum(text, index, fontSizePx, quantity) || quantity.unit != CSS_NUMBER) {
            return false;
        }
        if (name == "pow") {
            SkipCssSpace(text, index);
            if (index >= text.size() || text[index] != ',') {
                return false;
            }
            ++index;
            CssQty exponent;
            if (!ParseCssSum(text, index, fontSizePx, exponent) || exponent.unit != CSS_NUMBER) {
                return false;
            }
            quantity.value = std::pow(quantity.value, exponent.value);
        } else if (name == "sqrt") {
            if (quantity.value < 0) {
                return false;
            }
            quantity.value = std::sqrt(quantity.value);
        } else if (name == "exp") {
            quantity.value = std::exp(quantity.value);
        } else {
            SkipCssSpace(text, index);
            if (index < text.size() && text[index] == ',') {
                ++index;
                CssQty base;
                if (!ParseCssSum(text, index, fontSizePx, base) || base.unit != CSS_NUMBER || !(base.value > 0) || base.value == 1) {
                    return false;
                }
                quantity.value = std::log(quantity.value) / std::log(base.value);
            } else if (quantity.value > 0) {
                quantity.value = std::log(quantity.value);
            } else {
                return false;
            }
        }
        quantity.unit = CSS_NUMBER;
        quantity.ok = true;
    } else {
        return false;
    }
    SkipCssSpace(text, index);
    if (index >= text.size() || text[index] != ')') {
        return false;
    }
    ++index;
    quantity.ok = true;
    return true;
}

bool ParseCssFactor(const std::string& text, std::size_t& index, int fontSizePx, CssQty& quantity) {
    SkipCssSpace(text, index);
    if (index >= text.size()) {
        return false;
    }
    if (text[index] == '+' || text[index] == '-') {
        const char sign = text[index];
        ++index;
        if (!ParseCssFactor(text, index, fontSizePx, quantity)) {
            return false;
        }
        if (sign == '-') {
            quantity.value = -quantity.value;
        }
        return true;
    }
    if (text[index] == '(') {
        ++index;
        if (!ParseCssSum(text, index, fontSizePx, quantity)) {
            return false;
        }
        SkipCssSpace(text, index);
        if (index >= text.size() || text[index] != ')') {
            return false;
        }
        ++index;
        return true;
    }
    const std::size_t mark = index;
    std::string name;
    if (ReadCssIdent(text, index, name)) {
        SkipCssSpace(text, index);
        if (index < text.size() && text[index] == '(') {
            ++index;
            return ParseCssFunction(name, text, index, fontSizePx, quantity);
        }
        index = mark;
    }
    SkipCssSpace(text, index);
    std::size_t start = index;
    if (index < text.size() && (text[index] == '+' || text[index] == '-')) {
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
    if (!sawDigit) {
        return false;
    }
    quantity.value = std::strtod(text.c_str() + start, nullptr);
    quantity.percent = 0;
    quantity.ok = true;
    quantity.unit = CSS_NUMBER;
    if (index < text.size() && text[index] == '%') {
        ++index;
        quantity.percent = quantity.value;
        quantity.value = 0;
        quantity.unit = CSS_PX;
        return true;
    }
    std::string unit;
    while (index < text.size() && ((text[index] >= 'A' && text[index] <= 'Z') || (text[index] >= 'a' && text[index] <= 'z'))) {
        unit.push_back(text[index]);
        ++index;
    }
    if (unit.empty()) {
        return true;
    }
    const CssLengthContext local;
    const CssLengthContext& basis = gCssLength != nullptr ? *gCssLength : local;
    const int font = std::max(1, basis.fontSizePx);
    const int root = std::max(1, basis.rootFontSizePx);
    auto lengthOf = [&](double pixels) {
        quantity.value = pixels;
        quantity.percent = 0;
        quantity.unit = CSS_PX;
        return true;
    };
    if (unit == "Q") {
        return lengthOf(quantity.value * 96.0 / 2.54 / 40.0);
    }
    for (char& character : unit) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    if (unit == "px") {
        return lengthOf(quantity.value);
    }
    if (unit == "em") {
        return lengthOf(quantity.value * font);
    }
    if (unit == "rem") {
        return lengthOf(quantity.value * root);
    }
    if (unit == "ex") {
        return lengthOf(quantity.value * font * 0.5);
    }
    if (unit == "rex") {
        return lengthOf(quantity.value * root * 0.5);
    }
    if (unit == "ch") {
        return lengthOf(quantity.value * std::max(1, basis.zeroAdvancePx));
    }
    if (unit == "rch") {
        return lengthOf(quantity.value * std::max(1, basis.rootZeroAdvancePx));
    }
    if (unit == "cap" || unit == "ic") {
        return lengthOf(quantity.value * font);
    }
    if (unit == "rcap" || unit == "ric") {
        return lengthOf(quantity.value * root);
    }
    if (unit == "lh") {
        return lengthOf(quantity.value * (basis.lineHeightPx > 0 ? basis.lineHeightPx : font));
    }
    if (unit == "rlh") {
        return lengthOf(quantity.value * (basis.rootLineHeightPx > 0 ? basis.rootLineHeightPx : root));
    }
    if (unit == "vw" || unit == "svw" || unit == "lvw" || unit == "dvw" || unit == "vi" || unit == "svi" || unit == "lvi" || unit == "dvi") {
        return lengthOf(CssViewportPart(quantity.value, true, false));
    }
    if (unit == "vh" || unit == "svh" || unit == "lvh" || unit == "dvh" || unit == "vb" || unit == "svb" || unit == "lvb" || unit == "dvb") {
        return lengthOf(CssViewportPart(quantity.value, false, false));
    }
    if (unit == "vmin" || unit == "svmin" || unit == "lvmin" || unit == "dvmin") {
        return lengthOf(CssViewportPart(quantity.value, true, true));
    }
    if (unit == "vmax" || unit == "svmax" || unit == "lvmax" || unit == "dvmax") {
        const double width = basis.viewportWidth;
        const double height = basis.viewportHeight;
        const double larger = width > height ? width : height;
        return lengthOf(quantity.value * larger / 100.0);
    }
    if (unit == "cqw" || unit == "cqi") {
        const double container = basis.containerWidth >= 0 ? basis.containerWidth : basis.viewportWidth;
        return lengthOf(quantity.value * container / 100.0);
    }
    if (unit == "cqh" || unit == "cqb") {
        const double container = basis.containerHeight >= 0 ? basis.containerHeight : basis.viewportHeight;
        return lengthOf(quantity.value * container / 100.0);
    }
    if (unit == "cqmin") {
        const double width = basis.containerWidth >= 0 ? basis.containerWidth : basis.viewportWidth;
        const double height = basis.containerHeight >= 0 ? basis.containerHeight : basis.viewportHeight;
        const double smaller = width < height ? width : height;
        return lengthOf(quantity.value * smaller / 100.0);
    }
    if (unit == "cqmax") {
        const double width = basis.containerWidth >= 0 ? basis.containerWidth : basis.viewportWidth;
        const double height = basis.containerHeight >= 0 ? basis.containerHeight : basis.viewportHeight;
        const double larger = width > height ? width : height;
        return lengthOf(quantity.value * larger / 100.0);
    }
    if (unit == "in") {
        return lengthOf(quantity.value * 96.0);
    }
    if (unit == "cm") {
        return lengthOf(quantity.value * 96.0 / 2.54);
    }
    if (unit == "mm") {
        return lengthOf(quantity.value * 96.0 / 25.4);
    }
    if (unit == "pt") {
        return lengthOf(quantity.value * 96.0 / 72.0);
    }
    if (unit == "pc") {
        return lengthOf(quantity.value * 96.0 / 6.0);
    }
    if (unit == "deg") {
        quantity.unit = CSS_DEG;
        return true;
    }
    if (unit == "rad") {
        quantity.value = quantity.value * 180.0 / std::acos(-1.0);
        quantity.unit = CSS_DEG;
        return true;
    }
    if (unit == "grad") {
        quantity.value *= 0.9;
        quantity.unit = CSS_DEG;
        return true;
    }
    if (unit == "turn") {
        quantity.value *= 360.0;
        quantity.unit = CSS_DEG;
        return true;
    }
    return false;
}

bool ParseCssProduct(const std::string& text, std::size_t& index, int fontSizePx, CssQty& quantity) {
    if (!ParseCssFactor(text, index, fontSizePx, quantity)) {
        return false;
    }
    while (true) {
        SkipCssSpace(text, index);
        if (index >= text.size() || (text[index] != '*' && text[index] != '/')) {
            break;
        }
        const char operation = text[index];
        ++index;
        CssQty next;
        if (!ParseCssFactor(text, index, fontSizePx, next)) {
            return false;
        }
        if (operation == '*') {
            if (quantity.unit != CSS_NUMBER && next.unit != CSS_NUMBER) {
                return false;
            }
            if (quantity.unit == CSS_NUMBER) {
                const double scale = quantity.value;
                quantity = next;
                quantity.value *= scale;
                quantity.percent *= scale;
            } else {
                quantity.value *= next.value;
                quantity.percent *= next.value;
            }
        } else {
            if ((next.unit != CSS_NUMBER && next.value == 0 && next.percent == 0) || (quantity.unit == CSS_NUMBER && next.unit != CSS_NUMBER)) {
                return false;
            }
            if (next.unit == CSS_NUMBER) {
                if (next.value == 0) {
                    return false;
                }
                quantity.value /= next.value;
                quantity.percent /= next.value;
            } else if (!FoldCssLength(quantity) || !FoldCssLength(next) || next.value == 0) {
                return false;
            } else {
                quantity.value /= next.value;
                quantity.percent = 0;
                quantity.unit = CSS_NUMBER;
            }
        }
    }
    return true;
}

bool ParseCssSum(const std::string& text, std::size_t& index, int fontSizePx, CssQty& quantity) {
    if (!ParseCssProduct(text, index, fontSizePx, quantity)) {
        return false;
    }
    while (true) {
        SkipCssSpace(text, index);
        if (index >= text.size() || (text[index] != '+' && text[index] != '-')) {
            break;
        }
        const char operation = text[index];
        ++index;
        CssQty next;
        if (!ParseCssProduct(text, index, fontSizePx, next) || next.unit != quantity.unit) {
            return false;
        }
        const double sign = operation == '+' ? 1 : -1;
        quantity.value += sign * next.value;
        quantity.percent += sign * next.percent;
    }
    return quantity.ok;
}

bool EvaluateCssPxInner(const std::string& text, const std::vector<StyleDeclaration>& variables, const CssLengthContext& context, int& pixels) {
    std::string resolved;
    if (!SubstituteCssVars(text, variables, 0, resolved)) {
        return false;
    }
    const CssLengthContext* previous = gCssLength;
    gCssLength = &context;
    std::size_t index = 0;
    CssQty quantity;
    const bool parsed = ParseCssSum(resolved, index, context.fontSizePx < 1 ? 16 : context.fontSizePx, quantity);
    SkipCssSpace(resolved, index);
    const bool complete = parsed && index == resolved.size() && quantity.unit == CSS_PX && FoldCssLength(quantity);
    gCssLength = previous;
    if (!complete) {
        return false;
    }
    pixels = static_cast<int>(std::lround(quantity.value));
    return true;
}

int ParseSizePx(const std::string& value) {
    const std::string text = ToLowerAscii(Trim(value));
    int calculated = 0;
    CssLengthContext plain;
    if (EvaluateCssPxInner(value, {}, plain, calculated)) {
        return std::max(0, calculated);
    }
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
    } else if (name == "line-height") {
        const std::string spacing = ToLowerAscii(value);
        if (spacing == "normal") {
            rule.hasLineHeight = true;
            rule.lineHeightPx = 0;
            rule.lineHeightMilli = -1;
        } else if (spacing.find('%') != std::string::npos) {
            rule.hasLineHeight = true;
            rule.lineHeightMilli = static_cast<int>(std::strtod(spacing.c_str(), nullptr) * 10.0 + 0.5);
            rule.lineHeightPx = -1;
        } else if (spacing.size() > 2 && spacing.compare(spacing.size() - 2, 2, "em") == 0) {
            rule.hasLineHeight = true;
            rule.lineHeightMilli = static_cast<int>(std::strtod(spacing.c_str(), nullptr) * 1000.0 + 0.5);
            rule.lineHeightPx = -1;
        } else if (spacing.find("px") != std::string::npos) {
            rule.hasLineHeight = true;
            rule.lineHeightPx = std::atoi(spacing.c_str());
            rule.lineHeightMilli = -1;
        } else if (!spacing.empty()) {
            const double multiplier = std::strtod(spacing.c_str(), nullptr);
            if (multiplier > 0) {
                rule.hasLineHeight = true;
                rule.lineHeightMilli = static_cast<int>(multiplier * 1000.0 + 0.5);
                rule.lineHeightPx = -1;
            }
        }
    } else if (name == "word-spacing") {
        const std::string spacing = ToLowerAscii(value);
        if (spacing == "normal") {
            rule.wordSpacingPx = 0;
            rule.wordSpacingMilli = -1;
            rule.hasWordSpacing = true;
        } else if (spacing.find('%') != std::string::npos) {
            rule.wordSpacingMilli = static_cast<int>(std::strtod(spacing.c_str(), nullptr) * 10.0 + (std::strtod(spacing.c_str(), nullptr) < 0 ? -0.5 : 0.5));
            rule.wordSpacingPx = 0;
            rule.hasWordSpacing = true;
        } else if (spacing.size() > 2 && spacing.compare(spacing.size() - 2, 2, "em") == 0) {
            rule.wordSpacingMilli = static_cast<int>(std::strtod(spacing.c_str(), nullptr) * 1000.0 + (std::strtod(spacing.c_str(), nullptr) < 0 ? -0.5 : 0.5));
            rule.wordSpacingPx = 0;
            rule.hasWordSpacing = true;
        } else if (!spacing.empty()) {
            rule.wordSpacingPx = std::atoi(spacing.c_str());
            rule.wordSpacingMilli = -1;
            rule.hasWordSpacing = true;
        }
    } else if (name == "text-transform") {
        const std::string transform = ToLowerAscii(value);
        if (transform == "uppercase") {
            rule.textTransform = 1;
            rule.hasTextTransform = true;
        } else if (transform == "lowercase") {
            rule.textTransform = 2;
            rule.hasTextTransform = true;
        } else if (transform == "capitalize") {
            rule.textTransform = 3;
            rule.hasTextTransform = true;
        } else if (transform == "none") {
            rule.textTransform = 0;
            rule.hasTextTransform = true;
        }
    } else if (name == "text-indent") {
        std::string token;
        bool lengthSet = false;
        const std::string lower = ToLowerAscii(value);
        for (std::size_t index = 0; index <= lower.size(); ++index) {
            const char letter = index < lower.size() ? lower[index] : ' ';
            if (letter == ' ' || letter == '\t') {
                if (!token.empty()) {
                    if (token == "hanging") {
                        rule.textIndentHanging = true;
                    } else if (token == "each-line") {
                        rule.textIndentEachLine = true;
                    } else if (!lengthSet) {
                        if (token.find('%') != std::string::npos) {
                            rule.textIndentMilli = static_cast<int>(std::strtod(token.c_str(), nullptr) * 10.0 + (std::strtod(token.c_str(), nullptr) < 0 ? -0.5 : 0.5));
                            rule.textIndentPx = 0;
                        } else if (token.size() > 2 && token.compare(token.size() - 2, 2, "em") == 0) {
                            const double ems = std::strtod(token.c_str(), nullptr);
                            rule.textIndentFromEm = true;
                            rule.textIndentEmMilli = static_cast<int>(ems * 1000.0 + (ems < 0 ? -0.5 : 0.5));
                            rule.textIndentMilli = -1;
                            rule.textIndentPx = 0;
                        } else {
                            rule.textIndentPx = std::atoi(token.c_str());
                            rule.textIndentMilli = -1;
                        }
                        lengthSet = true;
                    }
                }
                token.clear();
            } else {
                token.push_back(letter);
            }
        }
        if (lengthSet) {
            rule.hasTextIndent = true;
        }
    } else if (name == "letter-spacing") {
        const std::string spacing = ToLowerAscii(value);
        if (spacing == "normal") {
            rule.letterSpacingPx = 0;
            rule.hasLetterSpacing = true;
        } else if (!spacing.empty()) {
            rule.letterSpacingPx = std::atoi(spacing.c_str());
            rule.hasLetterSpacing = true;
        }
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
        } else if (display == "grid" || display == "inline-grid") {
            rule.displayKind = 1;
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
    } else if (name == "text-underline-offset") {
        const std::string offset = ToLowerAscii(Trim(value));
        if (offset == "auto") {
            rule.underlineOffsetPx = 0;
            rule.underlineOffsetRelative = false;
            rule.hasUnderlineOffset = true;
        } else if (offset.find('%') != std::string::npos) {
            const double percent = std::strtod(offset.c_str(), nullptr);
            rule.underlineOffsetMilli = static_cast<int>(percent * 10.0 + (percent < 0 ? -0.5 : 0.5));
            rule.underlineOffsetRelative = true;
            rule.hasUnderlineOffset = true;
        } else if (offset.size() > 2 && offset.compare(offset.size() - 2, 2, "em") == 0) {
            const double ems = std::strtod(offset.c_str(), nullptr);
            rule.underlineOffsetMilli = static_cast<int>(ems * 1000.0 + (ems < 0 ? -0.5 : 0.5));
            rule.underlineOffsetRelative = true;
            rule.hasUnderlineOffset = true;
        } else if (!offset.empty()) {
            rule.underlineOffsetPx = std::atoi(offset.c_str());
            rule.underlineOffsetRelative = false;
            rule.hasUnderlineOffset = true;
        }
    } else if (name == "text-decoration-color") {
        const std::string color = ToLowerAscii(Trim(value));
        if (color == "currentcolor") {
            rule.hasDecorationColor = true;
            rule.decorationUsesText = true;
        } else if (color == "transparent") {
            rule.decorationRed = 0;
            rule.decorationGreen = 0;
            rule.decorationBlue = 0;
            rule.decorationAlpha = 0;
            rule.decorationUsesText = false;
            rule.hasDecorationColor = true;
        } else if (ParseHexColor(value, rule.decorationRed, rule.decorationGreen, rule.decorationBlue)) {
            rule.decorationAlpha = 255;
            rule.decorationUsesText = false;
            rule.hasDecorationColor = true;
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
        StyleDeclaration declaration;
        const std::string rawName = Trim(piece.substr(0, colon));
        declaration.name = rawName.size() >= 2 && rawName[0] == '-' && rawName[1] == '-' ? rawName : ToLowerAscii(rawName);
        declaration.value = Trim(piece.substr(colon + 1));
        if (!declaration.name.empty() && !declaration.value.empty()) {
            rule.declarations.push_back(declaration);
        }
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
        rule.decoration >= 0 || rule.whiteSpace >= 0 || rule.borderPx >= 0 || rule.hasLetterSpacing || rule.hasWordSpacing || rule.hasLineHeight ||
            rule.hasTextIndent || rule.hasTextTransform || rule.hasDecorationColor || rule.hasUnderlineOffset || !rule.declarations.empty()) {
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
        if (ToLowerAscii(text.substr(index, std::min<std::size_t>(10, text.size() - index))) == "@keyframes") {
            std::size_t nameAt = index + 10;
            while (nameAt < text.size() && (text[nameAt] == ' ' || text[nameAt] == '\t' || text[nameAt] == '\n' || text[nameAt] == '\r')) {
                ++nameAt;
            }
            std::size_t nameEnd = nameAt;
            while (nameEnd < text.size() && text[nameEnd] != '{' && text[nameEnd] != ' ' && text[nameEnd] != '\t') {
                ++nameEnd;
            }
            const std::string animation = ToLowerAscii(text.substr(nameAt, nameEnd - nameAt));
            const std::size_t open = text.find('{', nameEnd);
            if (open == std::string::npos || animation.empty()) {
                break;
            }
            int depth = 0;
            std::size_t close = open;
            for (; close < text.size(); ++close) {
                if (text[close] == '{') {
                    ++depth;
                } else if (text[close] == '}') {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
            }
            if (close >= text.size()) {
                break;
            }
            const std::string body = text.substr(open + 1, close - open - 1);
            std::size_t frame = 0;
            while (frame < body.size()) {
                const std::size_t frameOpen = body.find('{', frame);
                if (frameOpen == std::string::npos) {
                    break;
                }
                const std::size_t frameClose = body.find('}', frameOpen);
                if (frameClose == std::string::npos) {
                    break;
                }
                const std::string labels = ToLowerAscii(Trim(body.substr(frame, frameOpen - frame)));
                std::size_t labelStart = 0;
                for (std::size_t labelIndex = 0; labelIndex <= labels.size(); ++labelIndex) {
                    if (labelIndex != labels.size() && labels[labelIndex] != ',') {
                        continue;
                    }
                    std::string label = Trim(labels.substr(labelStart, labelIndex - labelStart));
                    labelStart = labelIndex + 1;
                    std::string offset;
                    if (label == "from" || label == "0%") {
                        offset = "0";
                    } else if (label == "to" || label == "100%") {
                        offset = "100";
                    } else if (!label.empty() && label.back() == '%') {
                        offset = label.substr(0, label.size() - 1);
                    }
                    if (!offset.empty()) {
                        ElementStyle frameRule;
                        frameRule.selector = "@" + animation + "@" + offset;
                        ParseDeclarations(body.substr(frameOpen + 1, frameClose - frameOpen - 1), frameRule);
                        document.styleRules.push_back(frameRule);
                    }
                }
                frame = frameClose + 1;
            }
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
        const std::string media = ToLowerAscii(AttributeValue(node, "media"));
        const std::string styleType = ToLowerAscii(AttributeValue(node, "type"));
        const bool cssType = styleType.empty() || styleType.find("css") != std::string::npos;
        if (cssType && media.find("print") == std::string::npos && media.find("speech") == std::string::npos) {
            ParseStylesheet(document, GatherText(node), job);
        }
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

bool EvaluateCssPx(const std::string& text, const std::vector<StyleDeclaration>& variables, const CssLengthContext& context, int& pixels) {
    return EvaluateCssPxInner(text, variables, context, pixels);
}

bool EvaluateCssColor(const std::string& text, const std::vector<StyleDeclaration>& variables, std::uint8_t& red, std::uint8_t& green, std::uint8_t& blue) {
    std::string resolved;
    if (!SubstituteCssVars(text, variables, 0, resolved)) {
        return false;
    }
    return ParseHexColor(resolved, red, green, blue);
}

namespace {

std::string StripImportant(std::string value) {
    const std::string lower = ToLowerAscii(value);
    const std::size_t mark = lower.find("!important");
    if (mark != std::string::npos) {
        value = value.substr(0, mark);
    }
    return Trim(value);
}

bool KeywordValue(const std::string& value, const char* allowed) {
    std::string text = ToLowerAscii(StripImportant(value));
    const std::string allowedText = ToLowerAscii(allowed);
    std::string token;
    for (char character : allowedText) {
        if (character == ' ') {
            if (token == text) {
                return true;
            }
            token.clear();
        } else {
            token.push_back(character);
        }
    }
    return token == text;
}

bool IsCssLength(const std::string& value, bool allowAuto) {
    const std::string text = ToLowerAscii(StripImportant(value));
    if (text.find("var(") != std::string::npos || text == "0") {
        return true;
    }
    if (allowAuto && KeywordValue(text, "auto min-content max-content fit-content")) {
        return true;
    }
    CssLengthContext context;
    context.percentBase = 100;
    context.viewportWidth = 100;
    context.viewportHeight = 100;
    context.fontSizePx = 16;
    context.rootFontSizePx = 16;
    context.zeroAdvancePx = 8;
    context.rootZeroAdvancePx = 8;
    context.lineHeightPx = 20;
    context.rootLineHeightPx = 20;
    int pixels = 0;
    return EvaluateCssPx(text, {}, context, pixels);
}

bool IsCssColor(const std::string& value) {
    const std::string text = StripImportant(value);
    const std::string lower = ToLowerAscii(text);
    if (lower.find("var(") != std::string::npos || lower == "transparent" || lower == "currentcolor") {
        return true;
    }
    if (lower.compare(0, 16, "linear-gradient(") == 0 || lower.compare(0, 26, "repeating-linear-gradient(") == 0 ||
        lower.compare(0, 16, "radial-gradient(") == 0 || lower.compare(0, 26, "repeating-radial-gradient(") == 0) {
        return lower.back() == ')';
    }
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    return EvaluateCssColor(text, {}, red, green, blue);
}

bool FunctionList(const std::string& value, const char* names) {
    const std::string text = ToLowerAscii(StripImportant(value));
    if (text == "none" || text.find("var(") != std::string::npos) {
        return true;
    }
    std::size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() && text[index] == ' ') {
            ++index;
        }
        if (index >= text.size()) {
            break;
        }
        const std::size_t start = index;
        while (index < text.size() && text[index] != '(' && text[index] != ' ') {
            ++index;
        }
        const std::string name = text.substr(start, index - start);
        if (!KeywordValue(name, names) || index >= text.size() || text[index] != '(') {
            return false;
        }
        int depth = 1;
        ++index;
        while (index < text.size() && depth > 0) {
            if (text[index] == '(') {
                ++depth;
            } else if (text[index] == ')') {
                --depth;
            }
            ++index;
        }
        if (depth != 0) {
            return false;
        }
    }
    return true;
}

bool IsGridTrack(const std::string& value) {
    const std::string text = ToLowerAscii(StripImportant(value));
    if (text.find("var(") != std::string::npos) {
        return true;
    }
    std::size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() && text[index] == ' ') {
            ++index;
        }
        if (index >= text.size()) {
            break;
        }
        if (text.compare(index, 7, "repeat(") == 0 || text.compare(index, 7, "minmax(") == 0) {
            int depth = 0;
            while (index < text.size()) {
                if (text[index] == '(') {
                    ++depth;
                } else if (text[index] == ')') {
                    --depth;
                    ++index;
                    if (depth == 0) {
                        break;
                    }
                    continue;
                }
                ++index;
            }
            if (depth != 0) {
                return false;
            }
            continue;
        }
        const std::size_t start = index;
        while (index < text.size() && text[index] != ' ') {
            ++index;
        }
        const std::string token = text.substr(start, index - start);
        if (token.size() > 2 && token.compare(token.size() - 2, 2, "fr") == 0) {
            continue;
        }
        if (token == "auto" || token == "min-content" || token == "max-content") {
            continue;
        }
        if (token.compare(0, 12, "fit-content(") == 0 && !token.empty() && token.back() == ')') {
            continue;
        }
        if (!IsCssLength(token, false)) {
            return false;
        }
    }
    return true;
}

bool IsCssTime(const std::string& value) {
    const std::string text = ToLowerAscii(StripImportant(value));
    if (text.find("var(") != std::string::npos || text == "0") {
        return true;
    }
    std::string number = text;
    if (text.size() > 2 && text.compare(text.size() - 2, 2, "ms") == 0) {
        number = text.substr(0, text.size() - 2);
    } else if (text.size() > 1 && text.back() == 's') {
        number = text.substr(0, text.size() - 1);
    } else {
        return false;
    }
    if (number.empty() || number.front() == '.') {
        return false;
    }
    int dots = 0;
    for (char character : number) {
        if (character == '.') {
            ++dots;
        } else if (character < '0' || character > '9') {
            return false;
        }
    }
    return dots <= 1;
}

bool IsTiming(const std::string& value) {
    const std::string text = ToLowerAscii(StripImportant(value));
    if (KeywordValue(text, "linear ease ease-in ease-out ease-in-out step-start step-end")) {
        return true;
    }
    return FunctionList(text, "cubic-bezier steps linear");
}

bool CssValueOk(const std::string& name, const std::string& value) {
    if (name.size() >= 2 && name[0] == '-' && name[1] == '-') {
        return true;
    }
    const std::string text = StripImportant(value);
    if (text.empty()) {
        return false;
    }
    if (name == "width" || name == "height" || name == "margin-top" || name == "margin-right" || name == "margin-bottom" ||
        name == "margin-left" || name == "padding-top" || name == "padding-right" || name == "padding-bottom" || name == "padding-left" ||
        name == "font-size" || name == "border" || name == "border-width" || name == "letter-spacing" || name == "text-underline-offset" ||
        name == "left" || name == "top" || name == "right" || name == "bottom") {
        if (ToLowerAscii(text).compare(0, 7, "anchor(") == 0) {
            return text.back() == ')';
        }
        return IsCssLength(text, name != "font-size" && name != "border" && name != "border-width");
    }
    if (name == "margin" || name == "padding") {
        std::size_t index = 0;
        int count = 0;
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
            if (!IsCssLength(text.substr(start, index - start), true)) {
                return false;
            }
            ++count;
        }
        return count >= 1 && count <= 4;
    }
    if (name == "display") {
        return KeywordValue(text, "none block inline inline-block list-item grid inline-grid");
    }
    if (name == "position") {
        return KeywordValue(text, "static relative absolute fixed sticky");
    }
    if (name == "font-style") {
        return KeywordValue(text, "normal italic oblique");
    }
    if (name == "font-weight") {
        return KeywordValue(text, "normal bold bolder lighter") || (text.find_first_not_of("0123456789") == std::string::npos && std::atoi(text.c_str()) > 0);
    }
    if (name == "text-align") {
        return KeywordValue(text, "left right center start end justify");
    }
    if (name == "text-transform") {
        return KeywordValue(text, "none uppercase lowercase capitalize");
    }
    if (name == "white-space") {
        return KeywordValue(text, "normal nowrap pre pre-wrap pre-line");
    }
    if (name == "text-decoration" || name == "text-decoration-line") {
        return KeywordValue(text, "none underline line-through overline");
    }
    if (name == "container-type") {
        return KeywordValue(text, "normal size inline-size");
    }
    if (name == "anchor-name") {
        const std::string trimmed = Trim(text);
        return trimmed.size() >= 2 && trimmed[0] == '-' && trimmed[1] == '-';
    }
    if (name == "color" || name == "background" || name == "background-color" || name == "background-image" || name == "text-decoration-color") {
        return IsCssColor(text);
    }
    if (name == "filter") {
        return FunctionList(text, "blur brightness contrast drop-shadow grayscale hue-rotate invert opacity saturate sepia");
    }
    if (name == "transform") {
        return FunctionList(text, "matrix matrix3d perspective rotate rotate3d rotateX rotateY rotateZ scale scale3d scaleX scaleY scaleZ skew skewX skewY translate translate3d translateX translateY translateZ");
    }
    if (name == "grid-template-columns" || name == "grid-template-rows") {
        return IsGridTrack(text);
    }
    if (name == "animation-timing-function" || name == "transition-timing-function") {
        return IsTiming(text);
    }
    if (name == "line-height") {
        return KeywordValue(text, "normal") || IsCssLength(text, false) || text.find_first_not_of("0123456789.") == std::string::npos;
    }
    if (name == "animation-duration") {
        return IsCssTime(text);
    }
    if (name == "font-family" || name == "animation" || name == "transition" || name == "animation-name") {
        return !Trim(text).empty();
    }
    if (name == "text-indent" || name == "word-spacing") {
        return !Trim(text).empty();
    }
    const char* svgProperties =
        "fill stroke stroke-width stroke-linecap stroke-linejoin stroke-miterlimit fill-opacity stroke-opacity opacity stop-color "
        "flood-color flood-opacity clip-path mask marker marker-start marker-end marker-mid text-anchor dominant-baseline";
    return KeywordValue(name, svgProperties) && !Trim(text).empty();
}

bool KnownCssProperty(const std::string& name) {
    if (name.size() >= 2 && name[0] == '-' && name[1] == '-') {
        return true;
    }
    const char* known =
        "font-family font-weight font-style font-size line-height word-spacing text-transform text-indent letter-spacing color "
        "background background-color background-image display width height margin margin-top margin-right margin-bottom margin-left "
        "padding padding-top padding-right padding-bottom padding-left text-align text-underline-offset text-decoration-color "
        "text-decoration text-decoration-line white-space border border-width transform filter grid-template-columns grid-template-rows "
        "container-type position left top right bottom anchor-name animation-timing-function transition-timing-function animation "
        "transition animation-duration animation-name fill stroke stroke-width stroke-linecap stroke-linejoin stroke-miterlimit "
        "fill-opacity stroke-opacity opacity stop-color flood-color flood-opacity clip-path mask marker marker-start marker-end "
        "marker-mid text-anchor dominant-baseline";
    return KeywordValue(name, known);
}

void ValidateDeclaration(const std::string& property, const std::string& rawValue, const std::string& path, int line, Diagnostics& diagnostics) {
    const std::string name = ToLowerAscii(Trim(property));
    const std::string value = Trim(rawValue);
    if (name.empty() || value.empty()) {
        return;
    }
    if (name.size() >= 2 && name[0] == '-' && name[1] == '-') {
        return;
    }
    if (!KnownCssProperty(name)) {
        diagnostics.Error(path, line, 1, "unknown property '" + name + "'");
        return;
    }
    if (!CssValueOk(name, value)) {
        diagnostics.Error(path, line, 1, "invalid value '" + value + "' for property '" + name + "'");
    }
}

void ValidateDeclarationList(const std::string& body, const std::string& path, int line, Diagnostics& diagnostics) {
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
        ValidateDeclaration(piece.substr(0, colon), piece.substr(colon + 1), path, line, diagnostics);
    }
}

}  // namespace

void ValidateCssText(const std::string& css, const std::string& path, int line, Diagnostics& diagnostics) {
    const std::string text = StripComments(css);
    if (text.find('{') == std::string::npos) {
        ValidateDeclarationList(text, path, line, diagnostics);
        return;
    }
    std::size_t index = 0;
    while (index < text.size()) {
        const std::size_t open = text.find('{', index);
        if (open == std::string::npos) {
            break;
        }
        const std::size_t close = text.find('}', open);
        if (close == std::string::npos) {
            break;
        }
        ValidateDeclarationList(text.substr(open + 1, close - open - 1), path, line, diagnostics);
        index = close + 1;
    }
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

void CollectImageUrls(const Node& node, std::vector<std::string>& urls) {
    auto add = [&](const std::string& url) {
        if (url.find("://") == std::string::npos) {
            return;
        }
        for (const std::string& existing : urls) {
            if (existing == url) {
                return;
            }
        }
        urls.push_back(url);
    };
    if (node.kind == ElementKind::Img || node.kind == ElementKind::Image ||
        (node.kind == ElementKind::Input && ToLowerAscii(AttributeValue(node, "type")) == "image")) {
        add(AttributeValue(node, "src"));
        std::string set = AttributeValue(node, "srcset");
        const std::size_t space = set.find_first_of(" \t,");
        if (space != std::string::npos) {
            set = set.substr(0, space);
        }
        add(set);
    }
    if (node.kind == ElementKind::Video) {
        add(AttributeValue(node, "poster"));
    }
    if (node.kind == ElementKind::Source && node.parent != nullptr && node.parent->kind == ElementKind::Picture) {
        add(AttributeValue(node, "src"));
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        CollectImageUrls(*child, urls);
    }
}

bool CompileRemoteImages(Document& document, std::vector<CompiledImage>& images, std::string& error) {
    images.clear();
    if (document.root == nullptr) {
        return true;
    }
    std::vector<std::string> urls;
    CollectImageUrls(*document.root, urls);
    for (const std::string& url : urls) {
        const std::vector<unsigned char> bytes = Download(url);
        if (DecodeImageBytes(bytes).ok) {
            CompiledImage image;
            image.url = url;
            image.bytes = bytes;
            images.push_back(std::move(image));
            continue;
        }
        error = "image was not loaded: " + url;
        return false;
    }
    return true;
}

}
