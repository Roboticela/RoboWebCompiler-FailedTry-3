#include "Runtime/FramePlan.h"

#include "Core/Utf8.h"

#include <algorithm>
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

void StampNode(const Node& node, int& hash) {
    hash = Mix(hash, static_cast<int>(node.kind));
    hash = MixText(hash, node.text);
    hash = MixText(hash, node.liveValue);
    hash = Mix(hash, node.checked ? 1 : 0);
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
        StampNode(*child, hash);
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

}  // namespace

FrameStats& FrameStatistics() {
    return gStats;
}

void ResetFrameStatistics() {
    gStats = FrameStats{};
}

int ContentStamp(const Document& document) {
    int hash = 216613626;
    if (document.root != nullptr) {
        StampNode(*document.root, hash);
    }
    hash = MixText(hash, document.title);
    return hash;
}

FrameUpdate AdvanceFrame(
    ViewCache& cache, Document& document, int width, int height, FrameCause cause, Node* previousHover, bool settled) {
    const int stamp = ContentStamp(document);
    const bool viewportChanged = !cache.ready || cache.width != width || cache.height != height;
    const bool contentChanged = !cache.ready || cache.contentStamp != stamp;
    const bool hoverLayout = cause == FrameCause::Hover && (AnchorNeedsLayout(previousHover) || AnchorNeedsLayout(document.hover));
    const bool layout = cause == FrameCause::Viewport || cause == FrameCause::Content || viewportChanged || contentChanged || hoverLayout;
    if (layout) {
        cache.layout = LayoutDocument(document, width, height);
        cache.frame = RenderDocument(cache.layout, document.scrollY, settled);
        cache.ready = true;
        cache.width = cache.layout.viewportWidth;
        cache.height = cache.layout.viewportHeight;
        cache.contentStamp = ContentStamp(document);
        ++gStats.layoutCount;
        ++gStats.fullRasterCount;
        return FullUpdate(cache);
    }
    if (cause == FrameCause::Scroll) {
        cache.frame = RenderDocument(cache.layout, document.scrollY, settled);
        ++gStats.fullRasterCount;
        return FullUpdate(cache);
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
