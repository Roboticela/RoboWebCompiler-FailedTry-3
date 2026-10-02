#include "Layout/LayoutEngine.h"

#include "Core/Utf8.h"
#include "Render/BitmapFont.h"
#include "Render/ImageDecoder.h"
#include "Render/StyleSheet.h"
#include "Runtime/MiniScript.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace RoboWeb {

namespace {

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
        if (style.mark || style.background.alpha != 0) {
            Color background = style.mark ? Color{255, 255, 0, 255} : style.background;
            Fill(x, y, MeasureTextPx(text, style.fontSizePx, style.bold, style.italic, style.fontFamily), LineHeightPx(style.fontSizePx), background, fixed,
                clipped, clipX, clipY, clipWidth, clipHeight);
        }
        DrawOp op;
        op.kind = DrawOp::Kind::Text;
        op.x = x;
        op.y = y + style.baselineShift;
        op.text = text;
        op.fontSizePx = style.fontSizePx;
        op.color = style.color;
        op.bold = style.bold;
        op.italic = style.italic;
        op.monospace = style.monospace;
        op.underline = style.underline;
        op.strike = style.strike;
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
                op.fixed = fixed;
                result->ops.push_back(op);
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

    void StrokeLine(int x0, int y0, int x1, int y1, Color color, bool fixed, int thickness = 1) {
        const int deltaX = std::abs(x1 - x0);
        const int stepX = x0 < x1 ? 1 : -1;
        const int deltaY = -std::abs(y1 - y0);
        const int stepY = y0 < y1 ? 1 : -1;
        int error = deltaX + deltaY;
        const int mark = std::max(1, thickness);
        for (int guard = 0; guard < 4096; ++guard) {
            Fill(x0 - mark / 2, y0 - mark / 2, mark, mark, color, fixed);
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
        bool hidden = false;
        bool fillEvenOdd = false;
        std::string textAnchor = "start";
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
        const std::string fillText = ToLowerAscii(AttributeValue(node, "fill"));
        if (fillText == "none") {
            space.fill.alpha = 0;
        } else if (!fillText.empty() && fillText.find("url(") == std::string::npos) {
            ParseColor(fillText, space.fill);
        }
        const std::string strokeText = ToLowerAscii(AttributeValue(node, "stroke"));
        if (strokeText == "none") {
            space.stroke.alpha = 0;
        } else if (!strokeText.empty() && strokeText.find("url(") == std::string::npos) {
            ParseColor(strokeText, space.stroke);
        }
        if (!AttributeValue(node, "stroke-width").empty()) {
            space.strokeWidth = std::atof(AttributeValue(node, "stroke-width").c_str());
        }
        const std::string rule = ToLowerAscii(AttributeValue(node, "fill-rule"));
        if (rule == "evenodd") {
            space.fillEvenOdd = true;
        } else if (rule == "nonzero") {
            space.fillEvenOdd = false;
        }
        const std::string anchor = ToLowerAscii(AttributeValue(node, "text-anchor"));
        if (anchor == "start" || anchor == "middle" || anchor == "end") {
            space.textAnchor = anchor;
        }
    }

    bool SvgSwitchMatches(const Node& child) {
        if (HasAttribute(child, "requiredextensions")) {
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

    void ApplySvgTransform(const std::string& text, SvgSpace& space) {
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

    void PaintSvgTree(Node& node, SvgSpace space, bool fixed, bool referenced = false) {
        const std::string tag = ToLowerAscii(node.tagName);
        if (ToLowerAscii(AttributeValue(node, "display")) == "none") {
            return;
        }
        if (tag == "title" || tag == "desc" || tag == "metadata") {
            return;
        }
        if (!referenced && (tag == "defs" || tag == "symbol" || tag == "clippath" || tag == "mask" || tag == "filter")) {
            return;
        }
        ApplySvgTransform(AttributeValue(node, "transform"), space);
        if (tag == "svg" || node.kind == ElementKind::Svg) {
            ApplyViewBox(node, space);
        }
        ReadSvgPaint(node, space);
        const Color fill = WithOpacity(space.fill, space.opacity);
        const Color stroke = WithOpacity(space.stroke, space.opacity);
        const int thickness = std::max(1, static_cast<int>(space.strokeWidth * std::max(space.scaleX, space.scaleY) + 0.5));
        if (!space.hidden && tag == "rect") {
            const int x = SvgX(space, SvgNumber(node, "x", 0));
            const int y = SvgY(space, SvgNumber(node, "y", 0));
            const int width = SvgLength(space, SvgNumber(node, "width", 0), true);
            const int height = SvgLength(space, SvgNumber(node, "height", 0), false);
            Fill(x, y, width, height, fill, fixed);
            if (stroke.alpha != 0 && width > 0 && height > 0) {
                StrokeLine(x, y, x + width, y, stroke, fixed, thickness);
                StrokeLine(x + width, y, x + width, y + height, stroke, fixed, thickness);
                StrokeLine(x + width, y + height, x, y + height, stroke, fixed, thickness);
                StrokeLine(x, y + height, x, y, stroke, fixed, thickness);
            }
            RememberSvgLink(node, x, y, width, height, fixed);
        } else if (!space.hidden && tag == "circle") {
            const int cx = SvgX(space, SvgNumber(node, "cx", 0));
            const int cy = SvgY(space, SvgNumber(node, "cy", 0));
            const int radiusX = SvgLength(space, SvgNumber(node, "r", 0), true);
            const int radiusY = SvgLength(space, SvgNumber(node, "r", 0), false);
            FillEllipse(cx, cy, radiusX, radiusY, fill, fixed);
            StrokeEllipse(cx, cy, radiusX, radiusY, thickness, stroke, fixed);
            RememberSvgLink(node, cx - radiusX, cy - radiusY, radiusX * 2, radiusY * 2, fixed);
        } else if (!space.hidden && tag == "ellipse") {
            const int cx = SvgX(space, SvgNumber(node, "cx", 0));
            const int cy = SvgY(space, SvgNumber(node, "cy", 0));
            const int radiusX = SvgLength(space, SvgNumber(node, "rx", 0), true);
            const int radiusY = SvgLength(space, SvgNumber(node, "ry", 0), false);
            FillEllipse(cx, cy, radiusX, radiusY, fill, fixed);
            StrokeEllipse(cx, cy, radiusX, radiusY, thickness, stroke, fixed);
            RememberSvgLink(node, cx - radiusX, cy - radiusY, radiusX * 2, radiusY * 2, fixed);
        } else if (!space.hidden && tag == "line") {
            const int x1 = SvgX(space, SvgNumber(node, "x1", 0));
            const int y1 = SvgY(space, SvgNumber(node, "y1", 0));
            const int x2 = SvgX(space, SvgNumber(node, "x2", 0));
            const int y2 = SvgY(space, SvgNumber(node, "y2", 0));
            StrokeLine(x1, y1, x2, y2, stroke, fixed, thickness);
            RememberSvgLink(node, std::min(x1, x2), std::min(y1, y2), std::abs(x2 - x1) + thickness, std::abs(y2 - y1) + thickness, fixed);
        } else if (!space.hidden && (tag == "polygon" || tag == "polyline")) {
            const std::vector<SvgPoint> points = MapSvgPoints(ParsePointList(AttributeValue(node, "points")), space);
            if (tag == "polygon") {
                FillPolygon(points, 0, 0, fill, fixed, space.fillEvenOdd);
            }
            StrokePolyline(points, 0, 0, stroke, fixed, tag == "polygon", thickness);
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
        } else if (!space.hidden && (tag == "text" || tag == "tspan")) {
            ResolvedStyle textStyle;
            textStyle.color = fill;
            const int fontSize = std::max(1, SvgNumber(node, "font-size", DEFAULT_FONT_SIZE_PX));
            textStyle.fontSizePx = std::max(1, SvgLength(space, fontSize, false));
            const std::string content = GatherText(node);
            int textX = SvgX(space, SvgNumber(node, "x", 0));
            const int textWidth = MeasureTextPx(content, textStyle.fontSizePx, textStyle.bold, textStyle.italic, textStyle.fontFamily);
            if (space.textAnchor == "middle") {
                textX -= textWidth / 2;
            } else if (space.textAnchor == "end") {
                textX -= textWidth;
            }
            const int textY = SvgY(space, SvgNumber(node, "y", 0)) - textStyle.fontSizePx;
            Text(textX, textY, content, textStyle, fixed);
            RememberSvgLink(node, textX, textY, std::max(1, textWidth), textStyle.fontSizePx, fixed);
        }
        if (tag == "switch") {
            for (const std::unique_ptr<Node>& child : node.children) {
                if (child->type == Node::Type::Element && SvgSwitchMatches(*child)) {
                    PaintSvgTree(*child, space, fixed);
                    break;
                }
            }
        } else if (tag == "svg" || tag == "g" || tag == "a" || tag == "symbol" || tag == "defs" || node.kind == ElementKind::Svg) {
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

    void LayoutLines(Node& block, int x, int y, int width, const ResolvedStyle& style, bool fixed, int& usedHeight, bool paint) {
        std::vector<Piece> pieces;
        CollectInlines(block, style, width, pieces, true);
        int cursorY = y;
        std::size_t index = 0;
        while (index < pieces.size()) {
            int lineWidth = 0;
            int lineHeight = LineHeightPx(style.fontSizePx);
            std::size_t end = index;
            while (end < pieces.size() && !pieces[end].lineBreak) {
                if (!style.nowrap && !style.pre && lineWidth > 0 && lineWidth + pieces[end].width > width) {
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
            int cursorX = x + offset;
            for (std::size_t pieceIndex = index; pieceIndex < end; ++pieceIndex) {
                Piece& piece = pieces[pieceIndex];
                if (paint && piece.atomic && piece.node != nullptr) {
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
            if (end < pieces.size() && pieces[end].lineBreak) {
                ++end;
            }
            if (end == index) {
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
        if (node.scriptText || HasAttribute(node, "contenteditable")) {
            const std::string text = CollapsedText(node.liveValue.empty() && !node.scriptText ? GatherText(node) : node.liveValue, style.pre);
            if (!text.empty()) {
                Piece piece;
                piece.text = text;
                piece.style = style;
                piece.node = &node;
                piece.width = MeasureTextPx(text, style.fontSizePx, style.bold, style.italic, style.fontFamily);
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
                            piece.width = MeasureTextPx(line, style.fontSizePx, style.bold, style.italic, style.fontFamily);
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
                        piece.width = MeasureTextPx(line, style.fontSizePx, style.bold, style.italic, style.fontFamily);
                        piece.height = LineHeightPx(style.fontSizePx);
                        pieces.push_back(piece);
                    }
                } else {
                    Piece piece;
                    piece.text = text;
                    piece.style = style;
                    piece.node = &node;
                    piece.width = MeasureTextPx(text, style.fontSizePx, style.bold, style.italic, style.fontFamily);
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
    if (node.tagName == "mi") {
        style.italic = true;
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
        const std::size_t dot = selector.find('.');
        const std::string tag = dot == std::string::npos ? selector : selector.substr(0, dot);
        if (tag != ToLowerAscii(current.tagName)) {
            return false;
        }
        if (dot == std::string::npos) {
            return true;
        }
        const std::string classes = " " + AttributeValue(current, "class") + " ";
        return classes.find(" " + selector.substr(dot + 1) + " ") != std::string::npos;
    };
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

LayoutResult LayoutDocument(Document& document, int viewportWidth, int viewportHeight) {
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
