#include "Layout/LayoutEngine.h"

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

bool ParseColor(const std::string& text, Color& color) {
    std::string value = ToLowerAscii(text);
    if (!value.empty() && value[0] == '#') {
        std::string hex = value.substr(1);
        if (hex.size() == 3) {
            hex = std::string{hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
        }
        if (hex.size() != 6) {
            return false;
        }
        auto channel = [&](int offset) -> int {
            int result = 0;
            for (int index = 0; index < 2; ++index) {
                const char character = hex[static_cast<std::size_t>(offset + index)];
                result *= 16;
                if (character >= '0' && character <= '9') {
                    result += character - '0';
                } else if (character >= 'a' && character <= 'f') {
                    result += character - 'a' + 10;
                } else {
                    return -1;
                }
            }
            return result;
        };
        const int red = channel(0);
        const int green = channel(2);
        const int blue = channel(4);
        if (red < 0 || green < 0 || blue < 0) {
            return false;
        }
        color = Color{static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green), static_cast<std::uint8_t>(blue), 255};
        return true;
    }
    struct Named {
        const char* name;
        Color color;
    };
    const Named named[] = {
        {"black", {0, 0, 0, 255}}, {"white", {255, 255, 255, 255}}, {"red", {255, 0, 0, 255}},
        {"green", {0, 128, 0, 255}}, {"blue", {0, 0, 255, 255}}, {"yellow", {255, 255, 0, 255}},
        {"gray", {128, 128, 128, 255}}, {"grey", {128, 128, 128, 255}}, {"silver", {192, 192, 192, 255}},
        {"maroon", {128, 0, 0, 255}}, {"purple", {128, 0, 128, 255}}, {"fuchsia", {255, 0, 255, 255}},
        {"lime", {0, 255, 0, 255}}, {"olive", {128, 128, 0, 255}}, {"navy", {0, 0, 128, 255}},
        {"teal", {0, 128, 128, 255}}, {"aqua", {0, 255, 255, 255}}, {"orange", {255, 165, 0, 255}},
    };
    for (const Named& entry : named) {
        if (value == entry.name) {
            color = entry.color;
            return true;
        }
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
    Document* document = nullptr;
    LayoutResult* result = nullptr;
    int viewportWidth = 0;
    int viewportHeight = 0;
    std::vector<Node*> overlays;

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

    void Fill(int x, int y, int width, int height, Color color, bool fixed, bool clipped = false, int clipX = 0, int clipY = 0,
        int clipWidth = 0, int clipHeight = 0) {
        if (color.alpha == 0 || width <= 0 || height <= 0) {
            return;
        }
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

    int SvgNumber(const Node& node, const char* name, int fallback) {
        const std::string text = AttributeValue(node, name);
        if (text.empty()) {
            return fallback;
        }
        return std::atoi(text.c_str());
    }

    double SvgReal(const Node& node, const char* name, double fallback) {
        const std::string text = AttributeValue(node, name);
        if (text.empty()) {
            return fallback;
        }
        return std::strtod(text.c_str(), nullptr);
    }

    void FillEllipse(int centerX, int centerY, int radiusX, int radiusY, Color color, bool fixed) {
        if (radiusX < 1 || radiusY < 1 || color.alpha == 0) {
            return;
        }
        for (int row = -radiusY; row <= radiusY; ++row) {
            const double span = 1.0 - (static_cast<double>(row) * row) / (static_cast<double>(radiusY) * radiusY);
            if (span <= 0) {
                continue;
            }
            const int half = std::max(1, static_cast<int>(radiusX * std::sqrt(span)));
            Fill(centerX - half, centerY + row, half * 2, 1, color, fixed);
        }
    }

    void StrokeLine(int x0, int y0, int x1, int y1, Color color, bool fixed, int thickness = 1, const std::string& dasharray = std::string(), double dashOffset = 0) {
        std::vector<double> dashes;
        std::size_t dashIndex = 0;
        double dashValue = 0;
        while (ReadSvgNumber(dasharray, dashIndex, dashValue)) {
            dashes.push_back(std::max(0.0, dashValue));
        }
        const bool dashed = !dashes.empty();
        std::size_t pattern = 0;
        double remaining = dashed ? dashes[0] : 0;
        bool drawing = true;
        if (dashed) {
            double cycle = 0;
            for (double dash : dashes) {
                cycle += dash;
            }
            if (cycle > 0) {
                dashOffset = std::fmod(dashOffset, cycle);
                if (dashOffset < 0) {
                    dashOffset += cycle;
                }
            }
            while (!dashes.empty() && dashOffset >= dashes[pattern]) {
                dashOffset -= dashes[pattern];
                drawing = !drawing;
                pattern = (pattern + 1) % dashes.size();
            }
            remaining = dashes[pattern] - dashOffset;
        }
        const int deltaX = std::abs(x1 - x0);
        const int stepX = x0 < x1 ? 1 : -1;
        const int deltaY = -std::abs(y1 - y0);
        const int stepY = y0 < y1 ? 1 : -1;
        int error = deltaX + deltaY;
        const int mark = std::max(1, thickness);
        for (int guard = 0; guard < 4096; ++guard) {
            if (!dashed || drawing) {
                Fill(x0 - mark / 2, y0 - mark / 2, mark, mark, color, fixed);
            }
            if (dashed) {
                remaining -= 1;
                while (remaining <= 0 && !dashes.empty()) {
                    drawing = !drawing;
                    pattern = (pattern + 1) % dashes.size();
                    remaining += dashes[pattern] == 0 ? 1 : dashes[pattern];
                }
            }
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int doubled = 2 * error;
            if (doubled >= deltaY) {
                error += deltaY;
                x0 += stepX;
            }
            if (doubled <= deltaX) {
                error += deltaX;
                y0 += stepY;
            }
        }
    }

    struct SvgPoint {
        double x = 0;
        double y = 0;
    };

    void FillPolygon(const std::vector<SvgPoint>& points, int originX, int originY, Color color, bool fixed, bool evenOdd = true) {
        if (points.size() < 3 || color.alpha == 0) {
            return;
        }
        int minY = 1000000;
        int maxY = -1000000;
        for (const SvgPoint& point : points) {
            minY = std::min(minY, static_cast<int>(std::floor(point.y)));
            maxY = std::max(maxY, static_cast<int>(std::ceil(point.y)));
        }
        for (int row = minY; row <= maxY; ++row) {
            std::vector<int> crossings;
            for (std::size_t index = 0; index < points.size(); ++index) {
                const SvgPoint& start = points[index];
                const SvgPoint& end = points[(index + 1) % points.size()];
                const bool crosses = (start.y <= row && end.y > row) || (end.y <= row && start.y > row);
                if (!crosses || start.y == end.y) {
                    continue;
                }
                const double distance = (static_cast<double>(row) + 0.5 - start.y) / (end.y - start.y);
                crossings.push_back(static_cast<int>(start.x + distance * (end.x - start.x)));
            }
            std::sort(crossings.begin(), crossings.end());
            if (evenOdd) {
                for (std::size_t index = 0; index + 1 < crossings.size(); index += 2) {
                    const int left = crossings[index];
                    const int right = crossings[index + 1];
                    if (right > left) {
                        Fill(originX + left, originY + row, right - left, 1, color, fixed);
                    }
                }
                continue;
            }
            struct Hit {
                int x = 0;
                int wind = 0;
            };
            std::vector<Hit> hits;
            for (std::size_t index = 0; index < points.size(); ++index) {
                const SvgPoint& start = points[index];
                const SvgPoint& end = points[(index + 1) % points.size()];
                const bool crosses = (start.y <= row && end.y > row) || (end.y <= row && start.y > row);
                if (!crosses || start.y == end.y) {
                    continue;
                }
                const double distance = (static_cast<double>(row) + 0.5 - start.y) / (end.y - start.y);
                hits.push_back(Hit{static_cast<int>(start.x + distance * (end.x - start.x)), end.y > start.y ? 1 : -1});
            }
            std::sort(hits.begin(), hits.end(), [](const Hit& left, const Hit& right) { return left.x < right.x; });
            int wind = 0;
            int cursor = hits.empty() ? 0 : hits.front().x;
            for (const Hit& hit : hits) {
                if (wind != 0 && hit.x > cursor) {
                    Fill(originX + cursor, originY + row, hit.x - cursor, 1, color, fixed);
                }
                wind += hit.wind;
                cursor = hit.x;
            }
        }
    }

    void FillPolygonInRect(const std::vector<SvgPoint>& points, int clipX, int clipY, int clipWidth, int clipHeight, Color color, bool fixed, bool evenOdd) {
        if (points.size() < 3 || color.alpha == 0 || clipWidth <= 0 || clipHeight <= 0) {
            return;
        }
        const int clipRight = clipX + clipWidth;
        const int clipBottom = clipY + clipHeight;
        auto span = [&](int left, int right, int row) {
            const int start = std::max(left, clipX);
            const int end = std::min(right, clipRight);
            if (end > start && row >= clipY && row < clipBottom) {
                Fill(start, row, end - start, 1, color, fixed);
            }
        };
        for (int row = clipY; row < clipBottom; ++row) {
            if (evenOdd) {
                std::vector<int> crossings;
                for (std::size_t index = 0; index < points.size(); ++index) {
                    const SvgPoint& start = points[index];
                    const SvgPoint& end = points[(index + 1) % points.size()];
                    const bool crosses = (start.y <= row && end.y > row) || (end.y <= row && start.y > row);
                    if (!crosses || start.y == end.y) {
                        continue;
                    }
                    const double distance = (static_cast<double>(row) + 0.5 - start.y) / (end.y - start.y);
                    crossings.push_back(static_cast<int>(start.x + distance * (end.x - start.x)));
                }
                std::sort(crossings.begin(), crossings.end());
                for (std::size_t index = 0; index + 1 < crossings.size(); index += 2) {
                    span(crossings[index], crossings[index + 1], row);
                }
                continue;
            }
            struct Hit {
                int x = 0;
                int wind = 0;
            };
            std::vector<Hit> hits;
            for (std::size_t index = 0; index < points.size(); ++index) {
                const SvgPoint& start = points[index];
                const SvgPoint& end = points[(index + 1) % points.size()];
                const bool crosses = (start.y <= row && end.y > row) || (end.y <= row && start.y > row);
                if (!crosses || start.y == end.y) {
                    continue;
                }
                const double distance = (static_cast<double>(row) + 0.5 - start.y) / (end.y - start.y);
                hits.push_back(Hit{static_cast<int>(start.x + distance * (end.x - start.x)), end.y > start.y ? 1 : -1});
            }
            std::sort(hits.begin(), hits.end(), [](const Hit& left, const Hit& right) { return left.x < right.x; });
            int wind = 0;
            int cursor = hits.empty() ? 0 : hits.front().x;
            for (const Hit& hit : hits) {
                if (wind != 0 && hit.x > cursor) {
                    span(cursor, hit.x, row);
                }
                wind += hit.wind;
                cursor = hit.x;
            }
        }
    }

    void StrokePolyline(const std::vector<SvgPoint>& points, int originX, int originY, Color color, bool fixed, bool close, int thickness = 1) {
        if (points.size() < 2 || color.alpha == 0) {
            return;
        }
        for (std::size_t index = 1; index < points.size(); ++index) {
            StrokeLine(originX + static_cast<int>(points[index - 1].x), originY + static_cast<int>(points[index - 1].y),
                originX + static_cast<int>(points[index].x), originY + static_cast<int>(points[index].y), color, fixed, thickness);
        }
        if (close) {
            StrokeLine(originX + static_cast<int>(points.back().x), originY + static_cast<int>(points.back().y),
                originX + static_cast<int>(points.front().x), originY + static_cast<int>(points.front().y), color, fixed, thickness);
        }
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
        bool digits = false;
        while (index < text.size() && ((text[index] >= '0' && text[index] <= '9') || text[index] == '.')) {
            digits = true;
            ++index;
        }
        if (!digits) {
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

    void AddCurve(std::vector<SvgPoint>& points, double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3) {
        for (int step = 1; step <= 8; ++step) {
            const double t = static_cast<double>(step) / 8.0;
            const double u = 1.0 - t;
            SvgPoint point;
            point.x = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
            point.y = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
            points.push_back(point);
        }
    }

    void PaintPath(const std::string& data, int originX, int originY, Color fill, Color stroke, bool fixed, int thickness = 1,
        double scaleX = 1, double scaleY = 1, double minX = 0, double minY = 0, bool evenOdd = false) {
        std::vector<SvgPoint> figure;
        double currentX = 0;
        double currentY = 0;
        double startX = 0;
        double startY = 0;
        std::size_t index = 0;
        char command = 0;
        auto finish = [&](bool close) {
            std::vector<SvgPoint> mapped;
            for (const SvgPoint& point : figure) {
                mapped.push_back(SvgPoint{(point.x - minX) * scaleX, (point.y - minY) * scaleY});
            }
            if (fill.alpha != 0) {
                FillPolygon(mapped, originX, originY, fill, fixed, evenOdd);
            }
            if (stroke.alpha != 0) {
                StrokePolyline(mapped, originX, originY, stroke, fixed, close, thickness);
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
                continue;
            }
            if (operation == 'A') {
                for (int skip = 0; skip < 5; ++skip) {
                    if (!ReadSvgNumber(data, index, first)) {
                        return;
                    }
                }
                if (!ReadSvgNumber(data, index, first) || !ReadSvgNumber(data, index, second)) {
                    return;
                }
                currentX = relative ? currentX + first : first;
                currentY = relative ? currentY + second : second;
                figure.push_back(SvgPoint{currentX, currentY});
                continue;
            }
            if (!ReadSvgNumber(data, index, first) || !ReadSvgNumber(data, index, second)) {
                break;
            }
            if (operation == 'M' || operation == 'L' || operation == 'T') {
                const double x = relative ? currentX + first : first;
                const double y = relative ? currentY + second : second;
                if (operation == 'M') {
                    if (figure.size() > 2) {
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
                continue;
            }
            double third = 0;
            double fourth = 0;
            if (!ReadSvgNumber(data, index, third) || !ReadSvgNumber(data, index, fourth)) {
                break;
            }
            if (operation == 'Q' || operation == 'S') {
                const double x = relative ? currentX + third : third;
                const double y = relative ? currentY + fourth : fourth;
                currentX = x;
                currentY = y;
                figure.push_back(SvgPoint{currentX, currentY});
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
            AddCurve(figure, currentX, currentY, x1, y1, x2, y2, x, y);
            currentX = x;
            currentY = y;
        }
        if (figure.size() > 2) {
            finish(false);
        }
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
        double strokeWidth = 1;
        double opacity = 1;
        Color fill{0, 0, 0, 255};
        Color stroke{0, 0, 0, 0};
        std::string fillUrl;
        bool hidden = false;
        bool clipped = false;
        int clipX = 0;
        int clipY = 0;
        int clipWidth = 0;
        int clipHeight = 0;
        std::vector<SvgPoint> clipPoints;
        bool clipEvenOdd = false;
        bool fillEvenOdd = false;
        double rotateDeg = 0;
        double rotateX = 0;
        double rotateY = 0;
        std::string textAnchor = "start";
        double fillOpacity = 1;
        double strokeOpacity = 1;
        Color currentColor{0, 0, 0, 255};
        int shapeRendering = 0;
        int textRendering = 0;
    };

    Color WithOpacity(Color color, double opacity) {
        color.alpha = static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, static_cast<double>(color.alpha) * opacity)));
        return color;
    }

    void ReadSvgPaint(const Node& node, SvgSpace& space) {
        const std::string visibility = ToLowerAscii(AttributeValue(node, "visibility"));
        if (visibility == "hidden" || visibility == "collapse") {
            space.hidden = true;
        } else if (visibility == "visible") {
            space.hidden = false;
        }
        if (!AttributeValue(node, "opacity").empty()) {
            space.opacity *= std::atof(AttributeValue(node, "opacity").c_str());
        }
        if (!AttributeValue(node, "color").empty()) {
            ParseColor(AttributeValue(node, "color"), space.currentColor);
        }
        std::string fillText = ToLowerAscii(AttributeValue(node, "fill"));
        if (fillText.empty()) {
            const std::string style = ToLowerAscii(AttributeValue(node, "style"));
            const std::size_t fillAt = style.find("fill:");
            if (fillAt != std::string::npos) {
                const std::size_t end = style.find(';', fillAt);
                fillText = style.substr(fillAt + 5, end == std::string::npos ? std::string::npos : end - (fillAt + 5));
                while (!fillText.empty() && fillText.front() == ' ') {
                    fillText.erase(fillText.begin());
                }
            }
        }
        if (fillText.empty() && document != nullptr) {
            const std::string classes = " " + ToLowerAscii(AttributeValue(node, "class")) + " ";
            const std::string id = ToLowerAscii(AttributeValue(node, "id"));
            const std::string tag = ToLowerAscii(node.tagName);
            for (const ElementStyle& rule : document->styleRules) {
                const std::string selector = rule.selector;
                std::string head = selector;
                std::string wantedLanguage;
                const std::size_t langAt = selector.find(":lang(");
                if (langAt != std::string::npos) {
                    head = selector.substr(0, langAt);
                    const std::size_t close = selector.find(')', langAt);
                    wantedLanguage = selector.substr(langAt + 6, close == std::string::npos ? std::string::npos : close - (langAt + 6));
                }
                bool languageOk = wantedLanguage.empty();
                if (!languageOk) {
                    for (const Node* cursor = &node; cursor != nullptr; cursor = cursor->parent) {
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
                }
                bool matched = false;
                if (languageOk && !head.empty() && head[0] == '.') {
                    matched = classes.find(" " + head.substr(1) + " ") != std::string::npos;
                } else if (languageOk && !head.empty() && head[0] == '#') {
                    matched = head.substr(1) == id;
                } else if (languageOk && head.empty()) {
                    matched = true;
                } else if (languageOk) {
                    const std::size_t dot = head.find('.');
                    if (dot != std::string::npos && head.substr(0, dot) == tag) {
                        matched = classes.find(" " + head.substr(dot + 1) + " ") != std::string::npos;
                    } else if (dot == std::string::npos && head == tag) {
                        matched = true;
                    }
                }
                if (matched && rule.hasColor) {
                    space.fill = Color{rule.colorRed, rule.colorGreen, rule.colorBlue, 255};
                    space.fillUrl.clear();
                    fillText = "styled";
                }
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
        const std::string strokeText = ToLowerAscii(AttributeValue(node, "stroke"));
        if (strokeText == "currentcolor") {
            space.stroke = space.currentColor;
        } else if (strokeText == "none") {
            space.stroke.alpha = 0;
        } else if (!strokeText.empty() && strokeText.find("url(") == std::string::npos) {
            ParseColor(strokeText, space.stroke);
        }
        if (!AttributeValue(node, "stroke-width").empty()) {
            space.strokeWidth = std::atof(AttributeValue(node, "stroke-width").c_str());
        }
        if (!AttributeValue(node, "fill-opacity").empty()) {
            space.fillOpacity = std::max(0.0, std::min(1.0, std::atof(AttributeValue(node, "fill-opacity").c_str())));
        }
        if (!AttributeValue(node, "stroke-opacity").empty()) {
            space.strokeOpacity = std::max(0.0, std::min(1.0, std::atof(AttributeValue(node, "stroke-opacity").c_str())));
        }
        const std::string rule = ToLowerAscii(AttributeValue(node, "fill-rule"));
        if (rule == "evenodd") {
            space.fillEvenOdd = true;
        } else if (rule == "nonzero") {
            space.fillEvenOdd = false;
        }
        const std::string shape = ToLowerAscii(AttributeValue(node, "shape-rendering"));
        if (shape == "geometricprecision") {
            space.shapeRendering = 1;
        } else if (shape == "crispedges") {
            space.shapeRendering = 2;
        } else if (shape == "auto" || shape == "optimizespeed") {
            space.shapeRendering = 0;
        }
        const std::string textHint = ToLowerAscii(AttributeValue(node, "text-rendering"));
        if (textHint == "optimizelegibility") {
            space.textRendering = 2;
        } else if (textHint == "geometricprecision" || textHint == "auto" || textHint == "optimizespeed") {
            space.textRendering = 0;
        }
        const std::string anchor = ToLowerAscii(AttributeValue(node, "text-anchor"));
        if (anchor == "start" || anchor == "middle" || anchor == "end") {
            space.textAnchor = anchor;
        }
        const std::string clip = AttributeValue(node, "clip-path");
        if (!clip.empty()) {
            const std::string id = SvgPaintId(clip);
            Node* root = const_cast<Node*>(&node);
            while (root->parent != nullptr) {
                root = root->parent;
            }
            Node* path = id.empty() ? nullptr : FindSvgById(*root, id);
            if (path != nullptr) {
                for (const std::unique_ptr<Node>& child : path->children) {
                    if (ToLowerAscii(child->tagName) == "polygon") {
                        space.clipPoints = MapSvgPoints(ParsePointList(AttributeValue(*child, "points")), space);
                        space.clipEvenOdd = ToLowerAscii(AttributeValue(*child, "clip-rule")) == "evenodd";
                        break;
                    }
                    if (ToLowerAscii(child->tagName) != "rect") {
                        continue;
                    }
                    space.clipped = true;
                    const bool objectBox = ToLowerAscii(AttributeValue(*path, "clippathunits")) == "objectboundingbox";
                    if (objectBox) {
                        const double elementX = SvgNumber(node, "x", 0);
                        const double elementY = SvgNumber(node, "y", 0);
                        const double elementWidth = SvgNumber(node, "width", 0);
                        const double elementHeight = SvgNumber(node, "height", 0);
                        space.clipX = SvgX(space, elementX + SvgOffset(AttributeValue(*child, "x")) * elementWidth);
                        space.clipY = SvgY(space, elementY + SvgOffset(AttributeValue(*child, "y")) * elementHeight);
                        space.clipWidth = SvgLength(space, SvgOffset(AttributeValue(*child, "width")) * elementWidth, true);
                        space.clipHeight = SvgLength(space, SvgOffset(AttributeValue(*child, "height")) * elementHeight, false);
                    } else {
                        space.clipX = SvgX(space, SvgNumber(*child, "x", 0));
                        space.clipY = SvgY(space, SvgNumber(*child, "y", 0));
                        space.clipWidth = SvgLength(space, SvgNumber(*child, "width", 0), true);
                        space.clipHeight = SvgLength(space, SvgNumber(*child, "height", 0), false);
                    }
                    break;
                }
            }
        }
        const std::string clipRect = ToLowerAscii(AttributeValue(node, "clip"));
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

    void StrokeEllipse(int centerX, int centerY, int radiusX, int radiusY, int thickness, Color color, bool fixed) {
        if (color.alpha == 0 || radiusX < 1 || radiusY < 1) {
            return;
        }
        const int steps = std::max(16, radiusX + radiusY);
        const double pi = std::acos(-1.0);
        int previousX = centerX + radiusX;
        int previousY = centerY;
        for (int step = 1; step <= steps; ++step) {
            const double angle = 2.0 * pi * static_cast<double>(step) / static_cast<double>(steps);
            const int x = centerX + static_cast<int>(std::cos(angle) * radiusX);
            const int y = centerY + static_cast<int>(std::sin(angle) * radiusY);
            StrokeLine(previousX, previousY, x, y, color, fixed, thickness);
            previousX = x;
            previousY = y;
        }
    }

    void ApplySvgTransform(const std::string& text, SvgSpace& space, bool hasOrigin = false, double originX = 0, double originY = 0) {
        const std::size_t translate = text.find("translate");
        if (translate != std::string::npos) {
            std::size_t index = text.find('(', translate);
            double dx = 0;
            double dy = 0;
            if (index != std::string::npos) {
                ++index;
                ReadSvgNumber(text, index, dx);
                ReadSvgNumber(text, index, dy);
                space.originX += dx * space.scaleX;
                space.originY += dy * space.scaleY;
            }
        }
        const std::size_t scale = text.find("scale");
        if (scale != std::string::npos) {
            std::size_t index = text.find('(', scale);
            double sx = 1;
            double sy = 1;
            if (index != std::string::npos) {
                ++index;
                if (ReadSvgNumber(text, index, sx)) {
                    sy = sx;
                    ReadSvgNumber(text, index, sy);
                    space.scaleX *= sx;
                    space.scaleY *= sy;
                }
            }
        }
        const std::size_t rotation = text.find("rotate");
        if (rotation != std::string::npos) {
            std::size_t index = text.find('(', rotation);
            double angle = 0;
            double centerX = 0;
            double centerY = 0;
            if (index != std::string::npos) {
                ++index;
                ReadSvgNumber(text, index, angle);
                const bool gotCenter = ReadSvgNumber(text, index, centerX);
                ReadSvgNumber(text, index, centerY);
                space.rotateDeg += angle;
                if (gotCenter) {
                    space.rotateX = centerX;
                    space.rotateY = centerY;
                } else if (hasOrigin) {
                    space.rotateX = originX;
                    space.rotateY = originY;
                }
            }
        }
    }

    void UserToPixel(const SvgSpace& space, double x, double y, int& outX, int& outY) {
        double px = (x - space.minX) * space.scaleX;
        double py = (y - space.minY) * space.scaleY;
        if (space.rotateDeg != 0) {
            const double radians = space.rotateDeg * std::acos(-1.0) / 180.0;
            const double centerX = (space.rotateX - space.minX) * space.scaleX;
            const double centerY = (space.rotateY - space.minY) * space.scaleY;
            const double dx = px - centerX;
            const double dy = py - centerY;
            px = centerX + dx * std::cos(radians) - dy * std::sin(radians);
            py = centerY + dx * std::sin(radians) + dy * std::cos(radians);
        }
        outX = static_cast<int>(space.originX + px);
        outY = static_cast<int>(space.originY + py);
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
        const std::string aspect = ToLowerAscii(AttributeValue(node, "preserveaspectratio"));
        if (!aspect.empty() && aspect.find("none") == std::string::npos) {
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

    int SvgX(const SvgSpace& space, double x) {
        return static_cast<int>(space.originX + (x - space.minX) * space.scaleX);
    }

    int SvgY(const SvgSpace& space, double y) {
        return static_cast<int>(space.originY + (y - space.minY) * space.scaleY);
    }

    int SvgLength(const SvgSpace& space, double length, bool horizontal) {
        return std::max(0, static_cast<int>(length * (horizontal ? space.scaleX : space.scaleY)));
    }

    std::vector<SvgPoint> MapSvgPoints(const std::vector<SvgPoint>& points, const SvgSpace& space) {
        std::vector<SvgPoint> mapped;
        for (const SvgPoint& point : points) {
            mapped.push_back(SvgPoint{static_cast<double>(SvgX(space, point.x)), static_cast<double>(SvgY(space, point.y))});
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
            const std::string colorText = AttributeValue(*child, "stop-color");
            if (!colorText.empty()) {
                ParseColor(colorText, stop.color);
            }
            if (!AttributeValue(*child, "stop-opacity").empty()) {
                stop.color.alpha = static_cast<std::uint8_t>(
                    std::max(0.0, std::min(255.0, std::atof(AttributeValue(*child, "stop-opacity").c_str()) * 255.0)));
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

    Color SampleStops(const std::vector<SvgStop>& stops, double t, bool linear) {
        if (stops.empty()) {
            return Color{0, 0, 0, 255};
        }
        if (t < 0) {
            t = 0;
        }
        if (t > 1) {
            t = 1;
        }
        if (t <= stops.front().offset) {
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
            const double mix = span <= 0 ? 0 : (t - left.offset) / span;
            auto mixChannel = [&](int start, int end) {
                if (!linear) {
                    return static_cast<int>(start + (end - start) * mix);
                }
                auto decode = [](int value) {
                    const double encoded = value / 255.0;
                    return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
                };
                const double decoded = decode(start) + (decode(end) - decode(start)) * mix;
                const double encoded = decoded <= 0.0031308 ? 12.92 * decoded : 1.055 * std::pow(decoded, 1.0 / 2.4) - 0.055;
                return static_cast<int>(std::max(0.0, std::min(255.0, encoded * 255.0 + 0.5)));
            };
            Color color;
            color.red = static_cast<std::uint8_t>(mixChannel(left.color.red, right.color.red));
            color.green = static_cast<std::uint8_t>(mixChannel(left.color.green, right.color.green));
            color.blue = static_cast<std::uint8_t>(mixChannel(left.color.blue, right.color.blue));
            color.alpha = static_cast<std::uint8_t>(left.color.alpha + (right.color.alpha - left.color.alpha) * mix);
            return color;
        }
        return stops.back().color;
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
        const bool objectBox = ToLowerAscii(AttributeValue(*gradient, "gradientunits")) != "userspaceonuse";
        if (gradientTag == "radialgradient") {
            const double cx = AttributeValue(*gradient, "cx").empty() ? 0.5 : SvgOffset(AttributeValue(*gradient, "cx"));
            const double cy = AttributeValue(*gradient, "cy").empty() ? 0.5 : SvgOffset(AttributeValue(*gradient, "cy"));
            const double radius = AttributeValue(*gradient, "r").empty() ? 0.5 : SvgOffset(AttributeValue(*gradient, "r"));
            const double centerX = objectBox ? x + cx * width : SvgX(space, cx);
            const double centerY = objectBox ? y + cy * height : SvgY(space, cy);
            const double radiusX = std::max(0.001, objectBox ? radius * width : radius * space.scaleX);
            const double radiusY = std::max(0.001, objectBox ? radius * height : radius * space.scaleY);
            const double focalX = AttributeValue(*gradient, "fx").empty() ? centerX : (objectBox ? x + SvgOffset(AttributeValue(*gradient, "fx")) * width : SvgX(space, SvgOffset(AttributeValue(*gradient, "fx"))));
            const double focalY = AttributeValue(*gradient, "fy").empty() ? centerY : (objectBox ? y + SvgOffset(AttributeValue(*gradient, "fy")) * height : SvgY(space, SvgOffset(AttributeValue(*gradient, "fy"))));
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
                    if (!AttributeValue(*gradient, "fr").empty() && radius > 0) {
                        const double focalRadius = SvgOffset(AttributeValue(*gradient, "fr")) / radius;
                        if (focalRadius >= 1) {
                            offset = offset <= 1 ? 0 : offset;
                        } else if (focalRadius > 0) {
                            offset = (offset - focalRadius) / (1 - focalRadius);
                        }
                    }
                    Fill(x + column, y + row, 1, 1, WithOpacity(SampleStops(stops, offset, ToLowerAscii(AttributeValue(*gradient, "color-interpolation")) == "linearrgb"), space.opacity * space.fillOpacity), fixed);
                }
            }
            return true;
        }
        const double x1 = AttributeValue(*gradient, "x1").empty() ? (objectBox ? 0 : 0) : SvgOffset(AttributeValue(*gradient, "x1"));
        const double y1 = AttributeValue(*gradient, "y1").empty() ? 0 : SvgOffset(AttributeValue(*gradient, "y1"));
        const double x2 = AttributeValue(*gradient, "x2").empty() ? (objectBox ? 1 : static_cast<double>(width)) : SvgOffset(AttributeValue(*gradient, "x2"));
        const double y2 = AttributeValue(*gradient, "y2").empty() ? 0 : SvgOffset(AttributeValue(*gradient, "y2"));
        double startX = objectBox ? x + x1 * width : SvgX(space, x1);
        double startY = objectBox ? y + y1 * height : SvgY(space, y1);
        double endX = objectBox ? x + x2 * width : SvgX(space, x2);
        double endY = objectBox ? y + y2 * height : SvgY(space, y2);
        const std::string gradientTransform = AttributeValue(*gradient, "gradienttransform");
        const std::size_t gradientScale = gradientTransform.find("scale");
        if (gradientScale != std::string::npos) {
            std::size_t index = gradientTransform.find('(', gradientScale);
            double scaleX = 1;
            double scaleY = 1;
            if (index != std::string::npos) {
                ++index;
                if (ReadSvgNumber(gradientTransform, index, scaleX)) {
                    scaleY = scaleX;
                    ReadSvgNumber(gradientTransform, index, scaleY);
                    endX = startX + (endX - startX) * scaleX;
                    endY = startY + (endY - startY) * scaleY;
                }
            }
        }
        const double deltaX = endX - startX;
        const double deltaY = endY - startY;
        const double length = deltaX * deltaX + deltaY * deltaY;
        const std::string spread = ToLowerAscii(AttributeValue(*gradient, "spreadmethod"));
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
                if (spread == "repeat") {
                    t -= std::floor(t);
                } else if (spread == "reflect") {
                    if (t < 0) {
                        t = -t;
                    }
                    const double period = std::floor(t);
                    const double fraction = t - period;
                    t = static_cast<int>(period) % 2 == 0 ? fraction : 1 - fraction;
                }
                Fill(x + column, y + row, 1, 1, WithOpacity(SampleStops(stops, t, ToLowerAscii(AttributeValue(*gradient, "color-interpolation")) == "linearrgb"), space.opacity * space.fillOpacity), fixed);
            }
        }
        return true;
    }

    std::vector<SvgPoint> StraightPath(const std::string& data) {
        std::vector<SvgPoint> points;
        std::size_t index = 0;
        char command = 0;
        double x = 0;
        double y = 0;
        while (index < data.size()) {
            while (index < data.size() && (data[index] == ' ' || data[index] == '\t' || data[index] == ',')) {
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
                continue;
            }
            if (!ReadSvgNumber(data, index, first) || !ReadSvgNumber(data, index, second)) {
                break;
            }
            if (operation == 'M' || operation == 'L') {
                x = relative ? x + first : first;
                y = relative ? y + second : second;
                points.push_back(SvgPoint{x, y});
                if (operation == 'M') {
                    command = relative ? 'l' : 'L';
                }
                continue;
            }
            break;
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

    void PaintMarkerAt(Node& node, const std::string& reference, double pixelX, double pixelY, SvgSpace space, bool fixed) {
        const std::string id = SvgPaintId(reference);
        if (id.empty()) {
            return;
        }
        Node* root = &node;
        while (root->parent != nullptr) {
            root = root->parent;
        }
        Node* marker = FindSvgById(*root, id);
        if (marker == nullptr) {
            return;
        }
        const double refX = std::atof(AttributeValue(*marker, "refx").c_str());
        const double refY = std::atof(AttributeValue(*marker, "refy").c_str());
        SvgSpace local = space;
        if (!AttributeValue(*marker, "markerwidth").empty()) {
            local.scaleX *= std::atof(AttributeValue(*marker, "markerwidth").c_str()) / 3.0;
        }
        if (!AttributeValue(*marker, "markerheight").empty()) {
            local.scaleY *= std::atof(AttributeValue(*marker, "markerheight").c_str()) / 3.0;
        }
        if (ToLowerAscii(AttributeValue(*marker, "markerunits")) != "userspaceonuse") {
            const double strokeScale = std::max(0.0, space.strokeWidth);
            local.scaleX *= strokeScale;
            local.scaleY *= strokeScale;
        }
        const std::string orient = ToLowerAscii(AttributeValue(*marker, "orient"));
        if (!orient.empty() && orient != "auto" && orient != "auto-start-reverse") {
            local.rotateDeg += std::atof(orient.c_str());
            local.rotateX = refX;
            local.rotateY = refY;
        }
        local.originX = pixelX - refX * local.scaleX;
        local.originY = pixelY - refY * local.scaleY;
        local.minX = 0;
        local.minY = 0;
        PaintSvgTree(*marker, local, fixed, true);
    }

    void PaintLineMarkers(Node& node, const SvgSpace& space, int x1, int y1, int x2, int y2, bool fixed) {
        PaintMarkerAt(node, AttributeValue(node, "marker-start"), x1, y1, space, fixed);
        PaintMarkerAt(node, AttributeValue(node, "marker-end"), x2, y2, space, fixed);
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
                const std::size_t patternScale = patternTransform.find("scale");
                if (patternScale != std::string::npos) {
                    std::size_t index = patternTransform.find('(', patternScale);
                    double scaleX = 1;
                    double scaleY = 1;
                    if (index != std::string::npos) {
                        ++index;
                        if (ReadSvgNumber(patternTransform, index, scaleX)) {
                            scaleY = scaleX;
                            ReadSvgNumber(patternTransform, index, scaleY);
                            tile.scaleX *= scaleX;
                            tile.scaleY *= scaleY;
                        }
                    }
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

    bool MaskShows(const Node& node, const SvgSpace& space, int pixelX, int pixelY) {
        const std::string id = SvgPaintId(AttributeValue(node, "mask"));
        if (id.empty()) {
            return true;
        }
        Node* root = const_cast<Node*>(&node);
        while (root->parent != nullptr) {
            root = root->parent;
        }
        Node* mask = FindSvgById(*root, id);
        if (mask == nullptr) {
            return true;
        }
        if (ToLowerAscii(AttributeValue(*mask, "maskunits")) == "objectboundingbox" && !AttributeValue(*mask, "width").empty() &&
            !AttributeValue(*mask, "height").empty()) {
            const double elementX = SvgNumber(node, "x", 0);
            const double elementY = SvgNumber(node, "y", 0);
            const double elementWidth = SvgNumber(node, "width", 0);
            const double elementHeight = SvgNumber(node, "height", 0);
            const int regionLeft = SvgX(space, elementX + SvgOffset(AttributeValue(*mask, "x")) * elementWidth);
            const int regionTop = SvgY(space, elementY + SvgOffset(AttributeValue(*mask, "y")) * elementHeight);
            const int regionRight = regionLeft + SvgLength(space, SvgOffset(AttributeValue(*mask, "width")) * elementWidth, true);
            const int regionBottom = regionTop + SvgLength(space, SvgOffset(AttributeValue(*mask, "height")) * elementHeight, false);
            if (pixelX < regionLeft || pixelY < regionTop || pixelX >= regionRight || pixelY >= regionBottom) {
                return false;
            }
        }
        const bool alpha = ToLowerAscii(AttributeValue(*mask, "mask-type")) == "alpha";
        Color covered{0, 0, 0, 0};
        bool hit = false;
        for (const std::unique_ptr<Node>& child : mask->children) {
            if (ToLowerAscii(child->tagName) != "rect") {
                continue;
            }
            const bool objectBox = ToLowerAscii(AttributeValue(*mask, "maskcontentunits")) == "objectboundingbox";
            int left = 0;
            int top = 0;
            int right = 0;
            int bottom = 0;
            if (objectBox) {
                const double elementX = SvgNumber(node, "x", 0);
                const double elementY = SvgNumber(node, "y", 0);
                const double elementWidth = SvgNumber(node, "width", 0);
                const double elementHeight = SvgNumber(node, "height", 0);
                left = SvgX(space, elementX + SvgOffset(AttributeValue(*child, "x")) * elementWidth);
                top = SvgY(space, elementY + SvgOffset(AttributeValue(*child, "y")) * elementHeight);
                right = left + SvgLength(space, SvgOffset(AttributeValue(*child, "width")) * elementWidth, true);
                bottom = top + SvgLength(space, SvgOffset(AttributeValue(*child, "height")) * elementHeight, false);
            } else {
                left = SvgX(space, SvgNumber(*child, "x", 0));
                top = SvgY(space, SvgNumber(*child, "y", 0));
                right = left + SvgLength(space, SvgNumber(*child, "width", 0), true);
                bottom = top + SvgLength(space, SvgNumber(*child, "height", 0), false);
            }
            if (pixelX < left || pixelY < top || pixelX >= right || pixelY >= bottom) {
                continue;
            }
            covered = Color{255, 255, 255, 255};
            const std::string paint = AttributeValue(*child, "fill");
            if (!paint.empty()) {
                ParseColor(paint, covered);
            }
            hit = true;
        }
        if (!hit) {
            return false;
        }
        if (alpha) {
            return covered.alpha > 127;
        }
        const int luminance = (covered.red * 30 + covered.green * 59 + covered.blue * 11) / 100;
        return luminance > 127 && covered.alpha > 127;
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
        const std::string id = SvgPaintId(AttributeValue(node, "filter"));
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
                effect.blur = std::max(1, static_cast<int>(std::atof(AttributeValue(*child, "stddeviation").c_str()) * std::max(space.scaleX, space.scaleY)));
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
                            pathData = AttributeValue(*path, "d");
                        }
                    }
                }
            }
            const std::vector<SvgPoint> course = MapSvgPoints(StraightPath(pathData), space);
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
                space.rotateDeg += angle;
            }
        }
    }

    void PaintSvgTree(Node& node, SvgSpace space, bool fixed, bool referenced = false) {
        const std::string tag = ToLowerAscii(node.tagName);
        if (ToLowerAscii(AttributeValue(node, "display")) == "none") {
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
        const std::string origin = AttributeValue(node, "transform-origin");
        const bool hasOrigin = !origin.empty() && ReadSvgNumber(origin, originIndex, originX);
        if (hasOrigin) {
            ReadSvgNumber(origin, originIndex, originY);
        }
        ApplySvgTransform(AttributeValue(node, "transform"), space, hasOrigin, originX, originY);
        ApplySvgMotion(node, space);
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
        const Color fill = WithOpacity(space.fill, space.opacity * space.fillOpacity);
        const Color stroke = WithOpacity(space.stroke, space.opacity * space.strokeOpacity);
        const bool fixedStroke = ToLowerAscii(AttributeValue(node, "vector-effect")) == "non-scaling-stroke";
        const int thickness = std::max(1, static_cast<int>(space.strokeWidth * (fixedStroke ? 1 : std::max(space.scaleX, space.scaleY)) + 0.5));
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
            if (space.rotateDeg != 0 && fill.alpha != 0 && space.fillUrl.empty()) {
                const double userX = SvgNumber(node, "x", 0);
                const double userY = SvgNumber(node, "y", 0);
                const double userWidth = SvgNumber(node, "width", 0);
                const double userHeight = SvgNumber(node, "height", 0);
                int cornerX[4];
                int cornerY[4];
                UserToPixel(space, userX, userY, cornerX[0], cornerY[0]);
                UserToPixel(space, userX + userWidth, userY, cornerX[1], cornerY[1]);
                UserToPixel(space, userX + userWidth, userY + userHeight, cornerX[2], cornerY[2]);
                UserToPixel(space, userX, userY + userHeight, cornerX[3], cornerY[3]);
                std::vector<SvgPoint> corners;
                for (int index = 0; index < 4; ++index) {
                    corners.push_back(SvgPoint{static_cast<double>(cornerX[index]), static_cast<double>(cornerY[index])});
                }
                FillPolygon(corners, 0, 0, fill, fixed, false);
            }
            const std::string paintOrder = ToLowerAscii(AttributeValue(node, "paint-order"));
            const bool strokeFirst = paintOrder.find("stroke") != std::string::npos &&
                                     (paintOrder.find("fill") == std::string::npos || paintOrder.find("stroke") < paintOrder.find("fill"));
            auto strokeRect = [&]() {
                if (stroke.alpha != 0 && width > 0 && height > 0) {
                    StrokeLine(x, y, x + width, y, stroke, fixed, thickness);
                    StrokeLine(x + width, y, x + width, y + height, stroke, fixed, thickness);
                    StrokeLine(x + width, y + height, x, y + height, stroke, fixed, thickness);
                    StrokeLine(x, y + height, x, y, stroke, fixed, thickness);
                }
            };
            if (strokeFirst) {
                strokeRect();
            }
            const bool painted = space.rotateDeg != 0 ||
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
            } else if (!painted && filtered && filterEffect.blur > 0 && shown.alpha != 0 && width > 0 && height > 0) {
                const int radius = filterEffect.blur;
                for (int row = -radius; row < height + radius; ++row) {
                    for (int column = -radius; column < width + radius; ++column) {
                        const int outsideX = column < 0 ? -column : (column >= width ? column - width + 1 : 0);
                        const int outsideY = row < 0 ? -row : (row >= height ? row - height + 1 : 0);
                        const int distance = std::max(outsideX, outsideY);
                        if (distance > radius) {
                            continue;
                        }
                        Color blurred = shown;
                        blurred.alpha = static_cast<std::uint8_t>(shown.alpha * (radius - distance + 1) / (radius + 1));
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
                } else if (AttributeValue(node, "mask").empty()) {
                    if (space.clipPoints.size() >= 3) {
                        FillPolygonInRect(space.clipPoints, fillX, fillY, fillWidth, fillHeight, shown, fixed, space.clipEvenOdd);
                    } else if (space.shapeRendering == 1) {
                        const double rawX = space.originX + (SvgReal(node, "x", 0) - space.minX) * space.scaleX;
                        const double rawY = space.originY + (SvgReal(node, "y", 0) - space.minY) * space.scaleY;
                        const double rawWidth = SvgReal(node, "width", 0) * space.scaleX;
                        const double rawHeight = SvgReal(node, "height", 0) * space.scaleY;
                        FillCoverage(rawX, rawY, rawWidth, rawHeight, shown, fixed);
                    } else if (space.shapeRendering == 2) {
                        const double rawX = space.originX + (SvgReal(node, "x", 0) - space.minX) * space.scaleX;
                        const double rawY = space.originY + (SvgReal(node, "y", 0) - space.minY) * space.scaleY;
                        const double rawRight = rawX + SvgReal(node, "width", 0) * space.scaleX;
                        const double rawBottom = rawY + SvgReal(node, "height", 0) * space.scaleY;
                        const int snappedX = static_cast<int>(std::lround(rawX));
                        const int snappedY = static_cast<int>(std::lround(rawY));
                        Fill(snappedX, snappedY, std::max(0, static_cast<int>(std::lround(rawRight)) - snappedX),
                            std::max(0, static_cast<int>(std::lround(rawBottom)) - snappedY), shown, fixed);
                    } else {
                        Fill(fillX, fillY, fillWidth, fillHeight, shown, fixed);
                    }
                } else {
                    for (int row = 0; row < fillHeight; ++row) {
                        for (int column = 0; column < fillWidth; ++column) {
                            if (MaskShows(node, space, fillX + column, fillY + row)) {
                                Fill(fillX + column, fillY + row, 1, 1, shown, fixed);
                            }
                        }
                    }
                }
            }
            if (!strokeFirst) {
                strokeRect();
            }
            RememberSvgLink(node, x, y, width, height, fixed);
            }
        } else if (!space.hidden && tag == "circle") {
            const double rawCx = space.originX + (SvgReal(node, "cx", 0) - space.minX) * space.scaleX;
            const double rawCy = space.originY + (SvgReal(node, "cy", 0) - space.minY) * space.scaleY;
            const int cx = space.shapeRendering == 2 ? static_cast<int>(std::lround(rawCx)) : SvgX(space, SvgNumber(node, "cx", 0));
            const int cy = space.shapeRendering == 2 ? static_cast<int>(std::lround(rawCy)) : SvgY(space, SvgNumber(node, "cy", 0));
            const int radiusX = SvgLength(space, SvgNumber(node, "r", 0), true);
            const int radiusY = SvgLength(space, SvgNumber(node, "r", 0), false);
            FillEllipse(cx, cy, radiusX, radiusY, fill, fixed);
            StrokeEllipse(cx, cy, radiusX, radiusY, thickness, stroke, fixed);
            RememberSvgLink(node, cx - radiusX, cy - radiusY, radiusX * 2, radiusY * 2, fixed);
        } else if (!space.hidden && tag == "ellipse") {
            const double rawCx = space.originX + (SvgReal(node, "cx", 0) - space.minX) * space.scaleX;
            const double rawCy = space.originY + (SvgReal(node, "cy", 0) - space.minY) * space.scaleY;
            const int cx = space.shapeRendering == 2 ? static_cast<int>(std::lround(rawCx)) : SvgX(space, SvgNumber(node, "cx", 0));
            const int cy = space.shapeRendering == 2 ? static_cast<int>(std::lround(rawCy)) : SvgY(space, SvgNumber(node, "cy", 0));
            const int radiusX = SvgLength(space, SvgNumber(node, "rx", 0), true);
            const int radiusY = SvgLength(space, SvgNumber(node, "ry", 0), false);
            FillEllipse(cx, cy, radiusX, radiusY, fill, fixed);
            StrokeEllipse(cx, cy, radiusX, radiusY, thickness, stroke, fixed);
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
            const std::string cap = ToLowerAscii(AttributeValue(node, "stroke-linecap"));
            if ((cap == "square" || cap == "round") && thickness > 1) {
                const double deltaX = x2 - x1;
                const double deltaY = y2 - y1;
                const double length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
                if (length > 0) {
                    const double extend = thickness / 2.0;
                    x1 -= static_cast<int>(deltaX / length * extend);
                    y1 -= static_cast<int>(deltaY / length * extend);
                    x2 += static_cast<int>(deltaX / length * extend);
                    y2 += static_cast<int>(deltaY / length * extend);
                }
            }
            std::string dasharray = AttributeValue(node, "stroke-dasharray");
            const double pathLength = std::atof(AttributeValue(node, "pathlength").c_str());
            if (!AttributeValue(node, "pathlength").empty() && pathLength > 0) {
                const double pixelLength = std::sqrt(static_cast<double>(x2 - x1) * (x2 - x1) + static_cast<double>(y2 - y1) * (y2 - y1));
                if (pixelLength > 0) {
                    const double dashScale = pixelLength / pathLength;
                    std::string scaled;
                    std::size_t index = 0;
                    double number = 0;
                    bool any = false;
                    while (ReadSvgNumber(dasharray, index, number)) {
                        if (any) {
                            scaled += ' ';
                        }
                        scaled += std::to_string(static_cast<int>(number * dashScale + 0.5));
                        any = true;
                    }
                    if (any) {
                        dasharray = scaled;
                    }
                }
            }
            StrokeLine(x1, y1, x2, y2, stroke, fixed, thickness, dasharray, std::atof(AttributeValue(node, "stroke-dashoffset").c_str()));
            PaintLineMarkers(node, space, x1, y1, x2, y2, fixed);
            RememberSvgLink(node, std::min(x1, x2), std::min(y1, y2), std::abs(x2 - x1) + thickness, std::abs(y2 - y1) + thickness, fixed);
        } else if (!space.hidden && (tag == "polygon" || tag == "polyline")) {
            const std::vector<SvgPoint> points = MapSvgPoints(ParsePointList(AttributeValue(node, "points")), space);
            if (tag == "polygon") {
                FillPolygon(points, 0, 0, fill, fixed, space.fillEvenOdd);
            }
            StrokePolyline(points, 0, 0, stroke, fixed, tag == "polygon", thickness);
            const std::string join = ToLowerAscii(AttributeValue(node, "stroke-linejoin"));
            const double miterLimit = AttributeValue(node, "stroke-miterlimit").empty() ? 4 : std::atof(AttributeValue(node, "stroke-miterlimit").c_str());
            if (stroke.alpha != 0 && join != "round" && join != "bevel" && thickness > 1 && points.size() > 2) {
                for (std::size_t index = 1; index + 1 < points.size(); ++index) {
                    const double ax = points[index].x - points[index - 1].x;
                    const double ay = points[index].y - points[index - 1].y;
                    const double bx = points[index + 1].x - points[index].x;
                    const double by = points[index + 1].y - points[index].y;
                    const double aLength = std::sqrt(ax * ax + ay * ay);
                    const double bLength = std::sqrt(bx * bx + by * by);
                    if (aLength < 1 || bLength < 1) {
                        continue;
                    }
                    const double ux = ax / aLength;
                    const double uy = ay / aLength;
                    const double vx = bx / bLength;
                    const double vy = by / bLength;
                    const double dot = std::max(-1.0, std::min(1.0, ux * vx + uy * vy));
                    const double sine = std::sin(std::acos(dot) / 2);
                    if (sine < 0.05 || (1.0 / sine) > miterLimit) {
                        continue;
                    }
                    const double cross = ux * vy - uy * vx;
                    double normalX = cross >= 0 ? uy + vy : -uy - vy;
                    double normalY = cross >= 0 ? -ux - vx : ux + vx;
                    const double normalLength = std::sqrt(normalX * normalX + normalY * normalY);
                    if (normalLength < 0.001) {
                        continue;
                    }
                    normalX /= normalLength;
                    normalY /= normalLength;
                    const double extension = thickness / 2.0 / sine;
                    const int brush = std::max(1, thickness / 2);
                    for (double distance = thickness / 2.0 + 1; distance <= extension; distance += 1) {
                        const int tipX = static_cast<int>(points[index].x + normalX * distance) - brush / 2;
                        const int tipY = static_cast<int>(points[index].y + normalY * distance) - brush / 2;
                        Fill(tipX, tipY, brush, brush, stroke, fixed);
                    }
                }
            }
            if (join == "round" && thickness > 1 && points.size() > 2) {
                const int radius = std::max(1, thickness / 2);
                for (std::size_t index = 1; index + 1 < points.size(); ++index) {
                    const int centerX = static_cast<int>(points[index].x);
                    const int centerY = static_cast<int>(points[index].y);
                    for (int row = -radius; row <= radius; ++row) {
                        for (int column = -radius; column <= radius; ++column) {
                            if (column * column + row * row <= radius * radius) {
                                Fill(centerX + column, centerY + row, 1, 1, stroke, fixed);
                            }
                        }
                    }
                }
            }
            if (!AttributeValue(node, "marker-mid").empty()) {
                const std::size_t last = points.size();
                for (std::size_t index = 1; last > 1 && index + 1 < last; ++index) {
                    PaintMarkerAt(node, AttributeValue(node, "marker-mid"), points[index].x, points[index].y, space, fixed);
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
        } else if (!space.hidden && tag == "path") {
            PaintPath(AttributeValue(node, "d"), static_cast<int>(space.originX), static_cast<int>(space.originY), fill, stroke, fixed, thickness,
                space.scaleX, space.scaleY, space.minX, space.minY, space.fillEvenOdd);
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
                        pathData = AttributeValue(*path, "d");
                    }
                }
            }
            const std::vector<SvgPoint> course = MapSvgPoints(StraightPath(pathData), space);
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
                const int fontSize = std::max(1, SvgNumber(node, "font-size", DEFAULT_FONT_SIZE_PX));
                textStyle.fontSizePx = std::max(1, SvgLength(space, fontSize, false));
                const std::string sizeAdjust = AttributeValue(node, "font-size-adjust");
                if (!sizeAdjust.empty() && ToLowerAscii(sizeAdjust) != "none") {
                    const double aspect = std::atof(sizeAdjust.c_str());
                    if (aspect > 0) {
                        textStyle.fontSizePx = std::max(1, static_cast<int>(textStyle.fontSizePx * aspect / 0.5 + 0.5));
                    }
                }
                const std::string weight = ToLowerAscii(AttributeValue(node, "font-weight"));
                textStyle.bold = weight == "bold" || weight == "bolder" || std::atoi(weight.c_str()) >= 700;
                const std::string decoration = ToLowerAscii(AttributeValue(node, "text-decoration"));
                textStyle.underline = decoration.find("underline") != std::string::npos;
                textStyle.strike = decoration.find("line-through") != std::string::npos;
                textStyle.italic = ToLowerAscii(AttributeValue(node, "font-style")) == "italic" || ToLowerAscii(AttributeValue(node, "font-style")) == "oblique";
                textStyle.fontFamily = AttributeValue(node, "font-family");
                textStyle.monospace = ToLowerAscii(textStyle.fontFamily).find("monospace") != std::string::npos;
                std::string content = GatherText(node);
                const std::string whiteSpace = ToLowerAscii(AttributeValue(node, "white-space"));
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
                if (ToLowerAscii(AttributeValue(node, "direction")) == "rtl" &&
                    ToLowerAscii(AttributeValue(node, "unicode-bidi")).find("override") != std::string::npos) {
                    std::reverse(content.begin(), content.end());
                }
                const double rawTextX = space.originX + (SvgReal(node, "x", 0) - space.minX) * space.scaleX;
                int textX = space.textRendering == 2 ? static_cast<int>(std::lround(rawTextX)) : SvgX(space, SvgNumber(node, "x", 0));
                const int textWidth = MeasureTextPx(content, textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily);
                if (space.textAnchor == "middle") {
                    textX -= textWidth / 2;
                } else if (space.textAnchor == "end" || ToLowerAscii(AttributeValue(node, "direction")) == "rtl") {
                    textX -= textWidth;
                }
                textX += SvgLength(space, SvgNumber(node, "dx", 0), true);
                std::string dominant = ToLowerAscii(AttributeValue(node, "dominant-baseline"));
                const std::string alignment = ToLowerAscii(AttributeValue(node, "alignment-baseline"));
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
                const std::string baselineShift = ToLowerAscii(AttributeValue(node, "baseline-shift"));
                int baseline = 0;
                if (baselineShift == "sub") {
                    baseline = textStyle.fontSizePx / 2;
                } else if (baselineShift == "super") {
                    baseline = -textStyle.fontSizePx / 2;
                } else if (!baselineShift.empty() && baselineShift != "baseline") {
                    baseline = -SvgLength(space, std::atof(baselineShift.c_str()), false);
                }
                const int textY = SvgY(space, SvgNumber(node, "y", 0)) + SvgLength(space, SvgNumber(node, "dy", 0), false) + baseline - dominantDrop;
                const std::string writingMode = ToLowerAscii(AttributeValue(node, "writing-mode"));
                if ((writingMode == "tb" || writingMode.find("vertical") != std::string::npos) && !content.empty()) {
                    int cursor = textY;
                    const std::string orientation = ToLowerAscii(AttributeValue(node, "glyph-orientation-vertical"));
                    const bool upright = orientation == "0" || orientation == "0deg" || orientation == "180" || orientation == "180deg";
                    for (std::size_t byte = 0; byte < content.size(); ++byte) {
                        const std::string glyph(1, content[byte]);
                        const int advance = upright ? textStyle.fontSizePx : std::max(1, MeasureTextPx(glyph, textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily));
                        Text(textX, cursor, glyph, textStyle, fixed);
                        cursor += advance;
                    }
                    RememberSvgLink(node, textX, textY, textStyle.fontSizePx, std::max(1, cursor - textY), fixed);
                } else {
                const int letterSpacing = SvgLength(space, std::atof(AttributeValue(node, "letter-spacing").c_str()), true);
                const int wordSpacing = SvgLength(space, std::atof(AttributeValue(node, "word-spacing").c_str()), true);
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
                std::string stretchText = AttributeValue(node, "font-stretch");
                if (stretchText.empty()) {
                    stretchText = AttributeValue(node, "font-width");
                }
                const double stretch = stretchOf(stretchText);
                const std::string horizontalOrientation = ToLowerAscii(AttributeValue(node, "glyph-orientation-horizontal"));
                const bool turnedGlyph = horizontalOrientation == "90" || horizontalOrientation == "90deg" || horizontalOrientation == "270" || horizontalOrientation == "270deg";
                const std::string textOverflow = ToLowerAscii(AttributeValue(node, "text-overflow"));
                if ((textOverflow == "ellipsis" || textOverflow == "ellipses") && whiteSpace != "pre" && whiteSpace != "pre-wrap" &&
                    !AttributeValue(node, "inline-size").empty() && !content.empty()) {
                    const int limit = SvgLength(space, std::atof(AttributeValue(node, "inline-size").c_str()), true);
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
                const bool smallCaps = ToLowerAscii(AttributeValue(node, "font-variant")).find("small-caps") != std::string::npos;
                if (turnedGlyph && !content.empty()) {
                    int cursor = textY;
                    for (std::size_t byte = 0; byte < content.size(); ++byte) {
                        const std::string glyph(1, content[byte]);
                        Text(textX, cursor, glyph, textStyle, fixed);
                        cursor += textStyle.fontSizePx;
                    }
                    RememberSvgLink(node, textX, textY, textStyle.fontSizePx, std::max(1, cursor - textY), fixed);
                } else if (std::abs(stretch - 1.0) > 0.01 && !content.empty()) {
                    int cursor = textX;
                    for (std::size_t byte = 0; byte < content.size(); ++byte) {
                        const std::string glyph(1, content[byte]);
                        const int advance = std::max(1, static_cast<int>(MeasureTextPx(glyph, textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily) * stretch + 0.5));
                        Text(cursor, textY, glyph, textStyle, fixed);
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
                        Text(cursor, textY, glyph, glyphStyle, fixed);
                        cursor += advance;
                    }
                    RememberSvgLink(node, textX, textY, std::max(1, cursor - textX), textStyle.fontSizePx, fixed);
                } else if ((letterSpacing != 0 || wordSpacing != 0 || (!glyphAdjust && textLength > 0)) && !content.empty()) {
                    int cursor = textX;
                    for (std::size_t byte = 0; byte < content.size(); ++byte) {
                        const std::string glyph(1, content[byte]);
                        const int advance = std::max(1, MeasureTextPx(glyph, textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily));
                        Text(cursor, textY, glyph, textStyle, fixed);
                        cursor += advance + spacing;
                        if (glyph == " ") {
                            cursor += wordSpacing;
                        }
                    }
                    RememberSvgLink(node, textX, textY, std::max(1, cursor - textX), textStyle.fontSizePx, fixed);
                } else {
                    Text(textX, textY, content, textStyle, fixed);
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
        int cursor = y;
        int previousBottom = 0;
        bool any = false;
        bool pendingInlines = false;
        auto flushInlines = [&](std::size_t, std::size_t) {
        };
        (void)flushInlines;
        (void)pendingInlines;
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
            if (ResolveStyle(child, style, *document).display == DisplayKind::None) {
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
            ResolvedStyle childStyle = ResolveStyle(child, style, *document);
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
    if (style.widthPx > 0) {
        width = std::min(width, style.widthPx);
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
            const DisplayKind display = ResolveStyle(*child, style, *document).display;
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
    if (style.background.alpha != 0) {
        DrawOp background;
        background.kind = DrawOp::Kind::Fill;
        background.x = x;
        background.y = y;
        background.width = width;
        background.height = contentHeight + style.paddingTop + style.paddingBottom + style.border * 2;
        background.color = style.background;
        background.fixed = fixed;
        result->ops.insert(result->ops.begin() + static_cast<std::ptrdiff_t>(backgroundIndex), background);
    }
    int borderHeight = contentHeight + style.paddingTop + style.paddingBottom + style.border * 2;
    if (style.heightPx > borderHeight) {
        borderHeight = style.heightPx;
        if (style.background.alpha != 0 && backgroundIndex < result->ops.size()) {
            result->ops[backgroundIndex].height = borderHeight;
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

ResolvedStyle ResolveStyle(const Node& node, const ResolvedStyle& parent, const Document& document) {
    (void)document;
    ResolvedStyle style = parent;
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
    auto matches = [](const Node& current, const std::string& selector) {
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
    for (const ElementStyle& rule : document.styleRules) {
        if (matches(node, rule.selector)) {
            applyRule(style, rule);
        }
    }
    if (HasAttribute(node, "style")) {
        ElementStyle inlineRule;
        ApplyStyleText(inlineRule, AttributeValue(node, "style"));
        applyRule(style, inlineRule);
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
    ResolvedStyle bodyStyle = ResolveStyle(*body, rootStyle, document);
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
