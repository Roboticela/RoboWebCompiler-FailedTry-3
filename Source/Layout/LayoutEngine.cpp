#include "Layout/LayoutEngine.h"

#include "Core/Utf8.h"
#include "Render/BitmapFont.h"
#include "Render/ImageDecoder.h"

#include <algorithm>
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
    if (HasAttribute(node, "hidden")) {
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
            Fill(x, y, MeasureTextPx(text, style.fontSizePx, style.bold, style.italic), LineHeightPx(style.fontSizePx), background, fixed,
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
        const int shift = editing ? TextScrollShift(placeholder ? std::string() : edited, caretByte, innerW, style.fontSizePx, style.bold, style.italic)
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
        const int prefix = MeasureTextPx(line.text.substr(0, static_cast<std::size_t>(column)), style.fontSizePx, style.bold, style.italic);
        Fill(innerX - shift + prefix, innerY + lineIndex * lineHeight, 1, lineHeight, Color{0, 0, 0, 255}, fixed, true, innerX, innerY, innerW,
            innerH);
    }

    int ReplacedSize(Node& node, const ResolvedStyle& style, int containing, int& width, int& height) {
        bool widthOk = false;
        bool heightOk = false;
        width = ParsePx(AttributeValue(node, "width"), containing, widthOk);
        height = ParsePx(AttributeValue(node, "height"), viewportHeight, heightOk);
        if (node.kind == ElementKind::Img || node.kind == ElementKind::Input) {
            const std::string source = node.kind == ElementKind::Input ? AttributeValue(node, "src") : AttributeValue(node, "src");
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
                    width = MeasureTextPx(alt, style.fontSizePx);
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
            const std::string path = ResolveUrl(*document, AttributeValue(node, "src"));
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
            Fill(x, y, width, HR_THICKNESS_PX, Color{128, 128, 128, 255}, fixed);
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
                if (clamped < low || clamped > high) {
                    fillColor = Color{255, 165, 0, 255};
                }
            }
            Fill(x + 1, y + 1, std::max(0, fillWidth - 2), std::max(0, height - 2), fillColor, fixed);
            (void)value;
            return;
        }
        Fill(x, y, width, height, face, fixed);
        Stroke(x, y, width, height, border, fixed);
        std::string label = AttributeValue(node, "src");
        if (label.empty()) {
            label = AttributeValue(node, "data");
        }
        if (label.empty()) {
            label = node.tagName;
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
            width = CharAdvancePx(style.fontSizePx) * columns + CONTROL_PADDING_X_PX * 2 + CONTROL_BORDER_PX * 2;
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
            width = CharAdvancePx(style.fontSizePx) * cols + CONTROL_PADDING_X_PX * 2 + CONTROL_BORDER_PX * 2;
            height = LineHeightPx(style.fontSizePx) * rows + CONTROL_PADDING_Y_PX * 2 + CONTROL_BORDER_PX * 2;
            return;
        }
        if (node.kind == ElementKind::Button || node.kind == ElementKind::Geolocation) {
            const std::string label = ControlLabel(node);
            width = MeasureTextPx(label, style.fontSizePx) + CONTROL_PADDING_X_PX * 2 + CONTROL_BORDER_PX * 2 + 8;
            height = LineHeightPx(style.fontSizePx) + CONTROL_PADDING_Y_PX * 2 + CONTROL_BORDER_PX * 2;
            return;
        }
        if (node.kind == ElementKind::Select) {
            const std::string label = ControlLabel(node);
            width = MeasureTextPx(label, style.fontSizePx) + 28;
            height = LineHeightPx(style.fontSizePx) + CONTROL_PADDING_Y_PX * 2 + CONTROL_BORDER_PX * 2;
            return;
        }
        if (node.kind == ElementKind::Hr) {
            width = containing;
            height = HR_THICKNESS_PX;
            return;
        }
        if (node.kind == ElementKind::Br) {
            width = 0;
            height = LineHeightPx(style.fontSizePx);
            return;
        }
        ReplacedSize(node, style, containing, width, height);
    }

    void PaintAtomic(Node& node, int x, int y, int width, int height, const ResolvedStyle& style, bool fixed) {
        AddBox(node, x, y, width, height, style, fixed);
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
                Fill(x + width / 2, y, 8, height, face, fixed);
                Stroke(x + width / 2, y, 8, height, border, fixed);
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
            if (node.kind == ElementKind::Select && node.selectExpanded) {
                int optionY = y + height;
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
        for (const std::unique_ptr<Node>& childPtr : node.children) {
            Node& child = *childPtr;
            if (child.type == Node::Type::Text) {
                const std::string text = CollapsedText(child.text, style.pre);
                if (text.empty() || (!style.pre && text == " ")) {
                    if (!pieces.empty() && !style.pre) {
                        pieces.back().text.push_back(' ');
                        pieces.back().width += MeasureTextPx(" ", style.fontSizePx, style.bold, style.italic);
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
                            piece.width = MeasureTextPx(line, style.fontSizePx, style.bold, style.italic);
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
                        piece.width = MeasureTextPx(line, style.fontSizePx, style.bold, style.italic);
                        piece.height = LineHeightPx(style.fontSizePx);
                        pieces.push_back(piece);
                    }
                } else {
                    Piece piece;
                    piece.text = text;
                    piece.style = style;
                    piece.node = &node;
                    piece.width = MeasureTextPx(text, style.fontSizePx, style.bold, style.italic);
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
                    openQuote.width = MeasureTextPx("\"", childStyle.fontSizePx, childStyle.bold, childStyle.italic);
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
                    closeQuote.width = MeasureTextPx("\"", childStyle.fontSizePx, childStyle.bold, childStyle.italic);
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
        auto addRow = [&](Node& rowNode) {
            Row row;
            row.node = &rowNode;
            int column = 0;
            for (const std::unique_ptr<Node>& child : rowNode.children) {
                if (child->kind != ElementKind::Td && child->kind != ElementKind::Th) {
                    continue;
                }
                Cell cell;
                cell.node = child.get();
                cell.column = column;
                bool spanOk = false;
                cell.columnSpan = ParsePx(AttributeValue(*child, "colspan"), width, spanOk);
                if (!spanOk || cell.columnSpan < 1) {
                    cell.columnSpan = 1;
                }
                column += cell.columnSpan;
                row.cells.push_back(cell);
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
                const int needed = PreferredWidth(*cell.node, cellStyle) + CELL_PADDING_PX * 2;
                int& columnWidth = columnWidths[static_cast<std::size_t>(cell.column)];
                columnWidth = std::max(columnWidth, needed);
            }
        }
        const int spacing = TABLE_BORDER_SPACING_PX;
        bool borderOk = false;
        const int borderAttr = ParsePx(AttributeValue(table, "border"), width, borderOk);
        const int border = borderOk && borderAttr > 0 ? borderAttr : 0;
        for (int& columnWidth : columnWidths) {
            if (columnWidth < 1) {
                columnWidth = std::max(1, CharAdvancePx(style.fontSizePx));
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
            int rowHeight = LineHeightPx(style.fontSizePx) + CELL_PADDING_PX * 2;
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
                LayoutLines(*cell.node, 0, 0, std::max(1, spanWidth - CELL_PADDING_PX * 2), cellStyle, fixed, contentHeights[cellIndex], false);
                rowHeight = std::max(rowHeight, contentHeights[cellIndex] + CELL_PADDING_PX * 2);
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
                const int contentTop = cursorY + std::max(CELL_PADDING_PX, (rowHeight - contentHeight) / 2);
                int used = 0;
                LayoutLines(
                    *cell.node, cursorX + CELL_PADDING_PX, contentTop, std::max(1, spanWidth - CELL_PADDING_PX * 2), cellStyle, fixed, used, true);
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
        for (const std::unique_ptr<Node>& childPtr : node.children) {
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

int Engine::LayoutBlock(Node& node, const ResolvedStyle& style, int x, int y, int width, bool fixed, bool suppressTop) {
    (void)suppressTop;
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
    const int borderHeight = contentHeight + style.paddingTop + style.paddingBottom + style.border * 2;
    if (style.border > 0) {
        Stroke(x, y, width, borderHeight, Color{118, 118, 118, 255}, fixed);
    }
    AddBox(node, x, y, width, borderHeight, style, fixed);
    if (node.kind == ElementKind::Li || node.kind == ElementKind::Summary) {
        std::string marker = "\xE2\x80\xA2";
        if (node.kind == ElementKind::Summary) {
            marker = node.parent != nullptr && node.parent->open ? "v" : ">";
        } else if (node.parent != nullptr && (node.parent->kind == ElementKind::Ol)) {
            int index = 1;
            bool startOk = false;
            const int start = ParsePx(AttributeValue(*node.parent, "start"), width, startOk);
            if (startOk) {
                index = start;
            }
            int seen = 0;
            for (const std::unique_ptr<Node>& sibling : node.parent->children) {
                if (sibling->kind == ElementKind::Li) {
                    if (sibling.get() == &node) {
                        break;
                    }
                    ++seen;
                }
            }
            index += seen;
            marker = std::to_string(index) + ".";
        }
        Text(contentX - MeasureTextPx(marker, style.fontSizePx, style.bold, style.italic) - 6, contentY, marker, style, fixed);
    }
    return borderHeight;
}

}  // namespace

std::string EditedControlText(const Node& node) {
    return FieldEditedText(node);
}

int TextScrollShift(const std::string& text, int caretByte, int innerWidth, int fontSizePx, bool bold, bool italic) {
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
    const int width = MeasureTextPx(line.text.substr(0, static_cast<std::size_t>(column)), fontSizePx, bold, italic);
    if (width >= innerWidth) {
        return width - innerWidth + 1;
    }
    return 0;
}

int HitTestTextByte(
    const std::string& text, int localX, int localY, int innerWidth, int fontSizePx, bool bold, bool italic, int caretByte) {
    const std::vector<FieldLine> lines = SplitFieldLines(text);
    const int shift = TextScrollShift(text, caretByte, innerWidth, fontSizePx, bold, italic);
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
        const int width = MeasureTextPx(line.text.substr(0, next), fontSizePx, bold, italic);
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
    if (ParseColor(AttributeValue(node, "bgcolor"), parsed)) {
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
    const std::string face = ToLowerAscii(AttributeValue(node, "face"));
    if (face.find("mono") != std::string::npos || face.find("courier") != std::string::npos) {
        style.monospace = true;
    }
    return style;
}

LayoutResult LayoutDocument(Document& document, int viewportWidth, int viewportHeight) {
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
