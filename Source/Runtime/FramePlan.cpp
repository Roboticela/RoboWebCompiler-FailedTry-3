#include "Runtime/FramePlan.h"

#include "Core/Constants.h"
#include "Core/Utf8.h"
#include "Render/BitmapFont.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace RoboWeb {

namespace {

FrameStats gStats;

const Color kIdleBorder{118, 118, 118, 255};
const Color kHoverBorder{0, 120, 215, 255};
const Color kFocusBorder{0, 95, 204, 255};
const Color kFace{240, 240, 240, 255};
const Color kHoverFace{210, 227, 252, 255};

int Mix(int hash, int value) {
    return static_cast<int>(static_cast<unsigned>(hash) * 16777619u) ^ value;
}

int MixText(int hash, const std::string& text) {
    for (unsigned char character : text) {
        hash = Mix(hash, character);
    }
    return hash;
}

void StampNode(const Node& node, int& hash, bool geometry) {
    hash = Mix(hash, static_cast<int>(node.kind));
    hash = MixText(hash, node.text);
    if (!geometry || HasAttribute(node, "contenteditable")) {
        hash = MixText(hash, node.liveValue);
    }
    if (!geometry) {
        hash = Mix(hash, node.checked ? 1 : 0);
    }
    hash = Mix(hash, node.selected ? 1 : 0);
    hash = Mix(hash, node.open ? 1 : 0);
    hash = Mix(hash, node.popoverOpen ? 1 : 0);
    hash = Mix(hash, node.selectExpanded ? 1 : 0);
    hash = Mix(hash, node.hiddenByScript ? 1 : 0);
    hash = Mix(hash, node.scriptText ? 1 : 0);
    hash = Mix(hash, node.visited ? 1 : 0);
    hash = Mix(hash, node.marqueeOffset);
    for (const Attribute& attribute : node.attributes) {
        hash = MixText(hash, attribute.name);
        hash = MixText(hash, attribute.value);
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        StampNode(*child, hash, geometry);
    }
}

std::string InputTypeOf(const Node& node) {
    std::string type = ToLowerAscii(AttributeValue(node, "type"));
    if (type.empty()) {
        type = "text";
    }
    return type;
}

bool TextChrome(const Node& node) {
    if (node.kind == ElementKind::Textarea) {
        return true;
    }
    if (node.kind != ElementKind::Input) {
        return false;
    }
    const std::string type = InputTypeOf(node);
    return type != "checkbox" && type != "radio" && type != "button" && type != "submit" && type != "reset" && type != "file" &&
           type != "image" && type != "hidden" && type != "color" && type != "range";
}

bool ChromeNode(const Node& node) {
    return TextChrome(node) || node.kind == ElementKind::Button || node.kind == ElementKind::Select || node.kind == ElementKind::Geolocation;
}

bool ChromeColor(const Color& color) {
    return color == kIdleBorder || color == kHoverBorder || color == kFocusBorder || color == kFace || color == kHoverFace;
}

bool AnchorNeedsLayout(const Node* node) {
    if (node == nullptr || node->kind != ElementKind::A || !HasAttribute(*node, "href")) {
        return false;
    }
    for (Node* ancestor = node->parent; ancestor != nullptr; ancestor = ancestor->parent) {
        if (ancestor->kind == ElementKind::Body && HasAttribute(*ancestor, "alink")) {
            return true;
        }
    }
    return false;
}

void ApplyChrome(LayoutResult& layout, const Document& document) {
    for (DrawOp& op : layout.ops) {
        if (op.owner == nullptr || op.caret || !ChromeNode(*op.owner) || !ChromeColor(op.color)) {
            continue;
        }
        const bool hot = document.hover == op.owner;
        const bool focused = document.focus == op.owner;
        const Color edge = focused ? kFocusBorder : (hot ? kHoverBorder : kIdleBorder);
        const bool faceControl = op.owner->kind != ElementKind::Input && op.owner->kind != ElementKind::Textarea;
        const Color face = hot ? kHoverFace : kFace;
        if (op.kind == DrawOp::Kind::Stroke && (op.color == kIdleBorder || op.color == kHoverBorder || op.color == kFocusBorder)) {
            op.color = edge;
        } else if (op.kind == DrawOp::Kind::Fill && faceControl && (op.color == kFace || op.color == kHoverFace)) {
            op.color = face;
        }
    }
}

void ApplyCaret(LayoutResult& layout, const Document& document) {
    std::vector<DrawOp> kept;
    kept.reserve(layout.ops.size());
    for (DrawOp& op : layout.ops) {
        if (!op.caret) {
            kept.push_back(std::move(op));
        }
    }
    layout.ops.swap(kept);
    if (document.caretOn && layout.caretPresent) {
        layout.ops.push_back(layout.caret);
    }
}

struct Dirty {
    bool any = false;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

void Include(Dirty& dirty, int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    const int right = x + width;
    const int bottom = y + height;
    if (!dirty.any) {
        dirty.any = true;
        dirty.left = x;
        dirty.top = y;
        dirty.right = right;
        dirty.bottom = bottom;
        return;
    }
    dirty.left = std::min(dirty.left, x);
    dirty.top = std::min(dirty.top, y);
    dirty.right = std::max(dirty.right, right);
    dirty.bottom = std::max(dirty.bottom, bottom);
}

void IncludeNode(Dirty& dirty, const LayoutResult& layout, const Node* node, int scrollY) {
    if (node == nullptr) {
        return;
    }
    for (const ElementBox& box : layout.boxes) {
        if (box.node != node) {
            continue;
        }
        const int y = box.fixed ? box.y : box.y - scrollY;
        Include(dirty, box.x - 2, y - 2, box.width + 4, box.height + 4);
    }
}

void IncludeCaret(Dirty& dirty, const LayoutResult& layout, int scrollY) {
    if (!layout.caretPresent) {
        return;
    }
    const int y = layout.caret.fixed ? layout.caret.y : layout.caret.y - scrollY;
    Include(dirty, layout.caret.x - 1, y - 1, layout.caret.width + 2, layout.caret.height + 2);
}

FrameUpdate FromDirty(const Dirty& dirty, int width, int height) {
    FrameUpdate update;
    if (!dirty.any) {
        return update;
    }
    const int left = std::max(0, dirty.left);
    const int top = std::max(0, dirty.top);
    const int right = std::min(width, dirty.right);
    const int bottom = std::min(height, dirty.bottom);
    if (right <= left || bottom <= top) {
        return update;
    }
    update.paint = true;
    update.x = left;
    update.y = top;
    update.width = right - left;
    update.height = bottom - top;
    return update;
}

FrameUpdate FullUpdate(const ViewCache& cache) {
    FrameUpdate update;
    update.paint = true;
    update.full = true;
    update.width = cache.frame.width;
    update.height = cache.frame.height;
    return update;
}

bool HasFixedPaint(const LayoutResult& layout) {
    for (const DrawOp& op : layout.ops) {
        if (!op.fixed) {
            continue;
        }
        const bool pageBackground = op.kind == DrawOp::Kind::Fill && op.x == 0 && op.y == 0 && op.width == layout.viewportWidth &&
                                    op.height == layout.viewportHeight;
        if (!pageBackground) {
            return true;
        }
    }
    return false;
}

const ElementBox* BoxFor(const LayoutResult& layout, const Node* node) {
    const ElementBox* found = nullptr;
    for (const ElementBox& box : layout.boxes) {
        if (box.node == node) {
            found = &box;
        }
    }
    return found;
}

bool CheckControl(const Node& node) {
    if (node.kind != ElementKind::Input) {
        return false;
    }
    const std::string type = InputTypeOf(node);
    return type == "checkbox" || type == "radio";
}

void IncludeBox(Dirty& dirty, const ElementBox& box, int scrollY) {
    const int y = box.fixed ? box.y : box.y - scrollY;
    Include(dirty, box.x - 2, y - 2, box.width + 4, box.height + 4);
}

void SyncMark(LayoutResult& layout, const ElementBox& box, bool checked, Dirty& dirty, int scrollY) {
    const int markX = box.x + 3;
    const int markY = box.y + 3;
    const int markWidth = box.width - 6;
    const int markHeight = box.height - 6;
    int found = -1;
    for (int index = 0; index < static_cast<int>(layout.ops.size()); ++index) {
        const DrawOp& op = layout.ops[static_cast<std::size_t>(index)];
        if (op.owner == box.node && op.kind == DrawOp::Kind::Fill && op.x == markX && op.y == markY && op.width == markWidth &&
            op.height == markHeight && op.color.red == 0 && op.color.green == 0 && op.color.blue == 0) {
            found = index;
            break;
        }
    }
    if (checked == (found >= 0) || markWidth <= 0 || markHeight <= 0) {
        return;
    }
    if (!checked) {
        layout.ops.erase(layout.ops.begin() + found);
    } else {
        DrawOp mark;
        mark.kind = DrawOp::Kind::Fill;
        mark.x = markX;
        mark.y = markY;
        mark.width = markWidth;
        mark.height = markHeight;
        mark.color = Color{0, 0, 0, 255};
        mark.owner = box.node;
        int insertAt = static_cast<int>(layout.ops.size());
        for (int index = 0; index < static_cast<int>(layout.ops.size()); ++index) {
            const DrawOp& op = layout.ops[static_cast<std::size_t>(index)];
            if (op.owner == box.node && op.kind == DrawOp::Kind::Fill) {
                insertAt = index + 1;
            }
        }
        layout.ops.insert(layout.ops.begin() + insertAt, mark);
    }
    IncludeBox(dirty, box, scrollY);
}

std::vector<std::string> FieldLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= text.size(); ++index) {
        if (index == text.size() || text[index] == '\n') {
            lines.push_back(text.substr(start, index - start));
            start = index + 1;
        }
    }
    if (lines.empty()) {
        lines.push_back("");
    }
    return lines;
}

void SyncField(LayoutResult& layout, const Document& document, const ElementBox& box, Dirty& dirty) {
    const int padX = CONTROL_BORDER_PX + CONTROL_PADDING_X_PX;
    const int padY = CONTROL_BORDER_PX + CONTROL_PADDING_Y_PX;
    const int innerX = box.x + padX;
    const int innerY = box.y + padY;
    const int innerW = std::max(1, box.width - padX * 2);
    const int innerH = std::max(1, box.height - padY * 2);
    const std::string edited = EditedControlText(*box.node);
    const bool placeholder = edited.empty() && HasAttribute(*box.node, "placeholder");
    const std::string shown = placeholder ? AttributeValue(*box.node, "placeholder") : edited;
    const int caretByte = document.focus == box.node && !placeholder ? std::max(0, document.caretOffset) : 0;
    const int shift = TextScrollShift(placeholder ? std::string() : edited, caretByte, innerW, box.fontSizePx, box.bold, box.italic, box.fontFamily);
    const std::vector<std::string> lines = FieldLines(shown);
    const int lineHeight = LineHeightPx(box.fontSizePx);
    std::vector<int> textOps;
    for (int index = 0; index < static_cast<int>(layout.ops.size()); ++index) {
        const DrawOp& op = layout.ops[static_cast<std::size_t>(index)];
        if (op.owner == box.node && op.kind == DrawOp::Kind::Text) {
            textOps.push_back(index);
        }
    }
    DrawOp prototype;
    prototype.kind = DrawOp::Kind::Text;
    prototype.owner = box.node;
    prototype.fontSizePx = box.fontSizePx;
    prototype.bold = box.bold;
    prototype.italic = box.italic;
    prototype.fontFamily = box.fontFamily;
    prototype.color = Color{0, 0, 0, 255};
    prototype.clipped = true;
    prototype.clipX = innerX;
    prototype.clipY = innerY;
    prototype.clipWidth = innerW;
    prototype.clipHeight = innerH;
    prototype.fixed = box.fixed;
    if (!textOps.empty()) {
        prototype = layout.ops[static_cast<std::size_t>(textOps[0])];
    }
    const Color valueColor = prototype.color.red == 117 && prototype.color.green == 117 && prototype.color.blue == 117 ? Color{0, 0, 0, 255}
                                                                                                                      : prototype.color;
    bool changed = textOps.size() != lines.size();
    for (int line = 0; line < static_cast<int>(lines.size()); ++line) {
        DrawOp op = prototype;
        op.kind = DrawOp::Kind::Text;
        op.owner = box.node;
        op.text = lines[static_cast<std::size_t>(line)];
        op.x = innerX - shift;
        op.y = innerY + line * lineHeight;
        op.color = placeholder ? Color{117, 117, 117, 255} : valueColor;
        op.clipped = true;
        op.clipX = innerX;
        op.clipY = innerY;
        op.clipWidth = innerW;
        op.clipHeight = innerH;
        if (op.text.empty()) {
            if (line < static_cast<int>(textOps.size())) {
                layout.ops[static_cast<std::size_t>(textOps[static_cast<std::size_t>(line)])].text.clear();
                changed = true;
            }
            continue;
        }
        if (line < static_cast<int>(textOps.size())) {
            DrawOp& slot = layout.ops[static_cast<std::size_t>(textOps[static_cast<std::size_t>(line)])];
            if (slot.text != op.text || slot.x != op.x || slot.y != op.y || !(slot.color == op.color)) {
                changed = true;
            }
            slot = op;
        } else {
            changed = true;
            const int insertAt = textOps.empty() ? static_cast<int>(layout.ops.size()) : textOps.back() + 1;
            layout.ops.insert(layout.ops.begin() + insertAt, op);
            textOps.push_back(insertAt);
        }
    }
    for (int extra = static_cast<int>(textOps.size()) - 1; extra >= static_cast<int>(lines.size()); --extra) {
        changed = true;
        layout.ops.erase(layout.ops.begin() + textOps[static_cast<std::size_t>(extra)]);
    }
    for (int index = static_cast<int>(layout.ops.size()) - 1; index >= 0; --index) {
        DrawOp& op = layout.ops[static_cast<std::size_t>(index)];
        if (op.owner == box.node && op.kind == DrawOp::Kind::Text && op.text.empty()) {
            layout.ops.erase(layout.ops.begin() + index);
            changed = true;
        }
    }
    if (document.focus == box.node) {
        int column = std::max(0, std::min(caretByte, static_cast<int>(edited.size())));
        int lineIndex = 0;
        int lineStart = 0;
        for (int index = 0; index < static_cast<int>(edited.size()) && index < column; ++index) {
            if (edited[static_cast<std::size_t>(index)] == '\n') {
                ++lineIndex;
                lineStart = index + 1;
            }
        }
        const std::string lineText = lineIndex < static_cast<int>(lines.size()) ? lines[static_cast<std::size_t>(lineIndex)] : std::string();
        const int prefix = MeasureTextPx(lineText.substr(0, static_cast<std::size_t>(std::max(0, column - lineStart))), box.fontSizePx, box.bold,
            box.italic, box.fontFamily);
        layout.caretPresent = true;
        layout.caret.kind = DrawOp::Kind::Fill;
        layout.caret.x = innerX - shift + prefix;
        layout.caret.y = innerY + lineIndex * lineHeight;
        layout.caret.width = 1;
        layout.caret.height = lineHeight;
        layout.caret.color = Color{0, 0, 0, 255};
        layout.caret.fixed = box.fixed;
        layout.caret.clipped = true;
        layout.caret.clipX = innerX;
        layout.caret.clipY = innerY;
        layout.caret.clipWidth = innerW;
        layout.caret.clipHeight = innerH;
        layout.caret.owner = box.node;
        layout.caret.caret = true;
        changed = true;
    }
    if (changed) {
        IncludeBox(dirty, box, document.scrollY);
    }
}

void SyncControls(LayoutResult& layout, Document& document, Dirty& dirty) {
    for (const ElementBox& box : layout.boxes) {
        if (box.node == nullptr) {
            continue;
        }
        if (CheckControl(*box.node)) {
            SyncMark(layout, box, box.node->checked, dirty, document.scrollY);
        } else if (TextChrome(*box.node)) {
            SyncField(layout, document, box, dirty);
        }
    }
    if (document.focus != nullptr) {
        if (const ElementBox* box = BoxFor(layout, document.focus)) {
            IncludeBox(dirty, *box, document.scrollY);
        }
    }
}

void ShiftFrame(Framebuffer& frame, int delta) {
    if (delta == 0 || frame.width <= 0 || frame.height <= 0) {
        return;
    }
    const int distance = delta < 0 ? -delta : delta;
    if (distance >= frame.height) {
        return;
    }
    const std::size_t stride = static_cast<std::size_t>(frame.width * 4);
    unsigned char* pixels = frame.pixels.data();
    if (delta > 0) {
        std::memmove(pixels, pixels + static_cast<std::size_t>(delta) * stride, static_cast<std::size_t>(frame.height - delta) * stride);
    } else {
        std::memmove(pixels + static_cast<std::size_t>(distance) * stride, pixels, static_cast<std::size_t>(frame.height - distance) * stride);
    }
}

}  // namespace

FrameStats& FrameStatistics() {
    return gStats;
}

void ResetFrameStatistics() {
    gStats = FrameStats{};
}

int StampDocument(const Document& document, bool geometry) {
    int hash = 216613626;
    if (document.root != nullptr) {
        StampNode(*document.root, hash, geometry);
    }
    hash = MixText(hash, document.title);
    if (!geometry) {
        hash = MixText(hash, document.status);
    }
    return hash;
}

int ContentStamp(const Document& document) {
    return StampDocument(document, false);
}

int GeometryStamp(const Document& document) {
    return StampDocument(document, true);
}

FrameUpdate AdvanceFrame(
    ViewCache& cache, Document& document, int width, int height, FrameCause cause, Node* previousHover, bool settled) {
    const bool viewportChanged = !cache.ready || cache.width != width || cache.height != height;
    const bool cheapCause = cache.ready && !viewportChanged &&
                            (cause == FrameCause::Hover || cause == FrameCause::Scroll || cause == FrameCause::Caret);
    const int stamp = cheapCause ? cache.contentStamp : ContentStamp(document);
    const int geometry = cheapCause ? cache.geometryStamp : GeometryStamp(document);
    const bool geometryChanged = !cache.ready || cache.geometryStamp != geometry;
    const bool hoverLayout = cause == FrameCause::Hover && (AnchorNeedsLayout(previousHover) || AnchorNeedsLayout(document.hover));
    const bool layout = cause == FrameCause::Viewport || viewportChanged || geometryChanged || hoverLayout;
    if (layout) {
        cache.layout = LayoutDocument(document, width, height);
        cache.frame = RenderDocument(cache.layout, document.scrollY, settled);
        cache.ready = true;
        cache.width = cache.layout.viewportWidth;
        cache.height = cache.layout.viewportHeight;
        cache.contentStamp = ContentStamp(document);
        cache.geometryStamp = GeometryStamp(document);
        cache.scrollY = document.scrollY;
        ++gStats.layoutCount;
        ++gStats.fullRasterCount;
        return FullUpdate(cache);
    }
    if (cause == FrameCause::Scroll) {
        const int delta = document.scrollY - cache.scrollY;
        const bool shift = delta != 0 && !HasFixedPaint(cache.layout) && cache.frame.height > 0 && delta > -cache.frame.height &&
                           delta < cache.frame.height;
        if (!shift) {
            if (delta != 0) {
                cache.frame = RenderDocument(cache.layout, document.scrollY, settled);
                ++gStats.fullRasterCount;
            }
            cache.scrollY = document.scrollY;
            return delta == 0 ? FrameUpdate{} : FullUpdate(cache);
        }
        ShiftFrame(cache.frame, delta);
        const int bandHeight = delta < 0 ? -delta : delta;
        const int bandY = delta > 0 ? cache.frame.height - delta : 0;
        RenderDirty(cache.layout, document.scrollY, cache.frame, 0, bandY, cache.frame.width, bandHeight, settled);
        cache.scrollY = document.scrollY;
        ++gStats.dirtyRasterCount;
        FrameUpdate update;
        update.paint = true;
        update.y = bandY;
        update.width = cache.frame.width;
        update.height = bandHeight;
        update.scrollDelta = delta;
        return update;
    }
    if (cause == FrameCause::Content) {
        Dirty dirty;
        ApplyChrome(cache.layout, document);
        SyncControls(cache.layout, document, dirty);
        IncludeCaret(dirty, cache.layout, document.scrollY);
        ApplyCaret(cache.layout, document);
        IncludeCaret(dirty, cache.layout, document.scrollY);
        cache.contentStamp = stamp;
        cache.geometryStamp = geometry;
        FrameUpdate update = FromDirty(dirty, cache.frame.width, cache.frame.height);
        if (!update.paint) {
            return update;
        }
        RenderDirty(cache.layout, document.scrollY, cache.frame, update.x, update.y, update.width, update.height, settled);
        ++gStats.dirtyRasterCount;
        return update;
    }
    if (cause == FrameCause::Hover) {
        const bool previousChrome = previousHover != nullptr && ChromeNode(*previousHover);
        const bool nextChrome = document.hover != nullptr && ChromeNode(*document.hover);
        if (!previousChrome && !nextChrome) {
            FrameUpdate update;
            return update;
        }
        ApplyChrome(cache.layout, document);
        Dirty dirty;
        IncludeNode(dirty, cache.layout, previousHover, document.scrollY);
        IncludeNode(dirty, cache.layout, document.hover, document.scrollY);
        FrameUpdate update = FromDirty(dirty, cache.frame.width, cache.frame.height);
        if (!update.paint) {
            return update;
        }
        RenderDirty(cache.layout, document.scrollY, cache.frame, update.x, update.y, update.width, update.height, settled);
        ++gStats.dirtyRasterCount;
        return update;
    }
    Dirty dirty;
    IncludeCaret(dirty, cache.layout, document.scrollY);
    ApplyCaret(cache.layout, document);
    IncludeCaret(dirty, cache.layout, document.scrollY);
    FrameUpdate update = FromDirty(dirty, cache.frame.width, cache.frame.height);
    if (!update.paint) {
        return update;
    }
    RenderDirty(cache.layout, document.scrollY, cache.frame, update.x, update.y, update.width, update.height, settled);
    ++gStats.dirtyRasterCount;
    return update;
}

}
