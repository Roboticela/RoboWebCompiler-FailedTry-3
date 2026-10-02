#pragma once

#include "Dom/Document.h"

#include <cstdint>
#include <string>
#include <vector>

namespace RoboWeb {

struct Color {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;
};

inline bool operator==(const Color& left, const Color& right) {
    return left.red == right.red && left.green == right.green && left.blue == right.blue && left.alpha == right.alpha;
}

struct DrawOp {
    enum class Kind { Fill, Stroke, Text, Image };

    Kind kind = Kind::Fill;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    Color color;
    std::string text;
    std::string imagePath;
    int fontSizePx = DEFAULT_FONT_SIZE_PX;
    bool bold = false;
    bool italic = false;
    bool monospace = false;
    bool underline = false;
    bool strike = false;
    bool fixed = false;
};

struct ElementBox {
    Node* node = nullptr;
    std::string tag;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool fixed = false;
    int fontSizePx = DEFAULT_FONT_SIZE_PX;
    bool bold = false;
    bool italic = false;
    bool monospace = false;
    bool underline = false;
    bool strike = false;
    DisplayKind display = DisplayKind::Block;
};

struct LayoutResult {
    int viewportWidth = 0;
    int viewportHeight = 0;
    int contentWidth = 0;
    int contentHeight = 0;
    std::vector<ElementBox> boxes;
    std::vector<DrawOp> ops;
};

struct ResolvedStyle {
    int fontSizePx = DEFAULT_FONT_SIZE_PX;
    bool bold = false;
    bool italic = false;
    bool monospace = false;
    bool nowrap = false;
    bool pre = false;
    bool underline = false;
    bool strike = false;
    bool mark = false;
    int baselineShift = 0;
    Color color{0, 0, 0, 255};
    Color background{0, 0, 0, 0};
    int textAlign = 0;
    bool rtl = false;
    int marginTop = 0;
    int marginBottom = 0;
    int marginLeft = 0;
    int marginRight = 0;
    int paddingLeft = 0;
    int paddingRight = 0;
    int paddingTop = 0;
    int paddingBottom = 0;
    int border = 0;
    DisplayKind display = DisplayKind::Block;
};

int LineHeightPx(int fontSizePx);
int CharAdvancePx(int fontSizePx);
int EmToPx(int fontSizePx, int milli);
ResolvedStyle DefaultStyle();
ResolvedStyle ResolveStyle(const Node& node, const ResolvedStyle& parent, const Document& document);
LayoutResult LayoutDocument(Document& document, int viewportWidth, int viewportHeight);
const ElementBox* FindBox(const LayoutResult& layout, const std::string& tag);

}
