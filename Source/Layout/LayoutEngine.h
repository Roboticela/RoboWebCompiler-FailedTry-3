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
    bool smoothImage = false;
    bool deferImage = false;
    int fontSizePx = DEFAULT_FONT_SIZE_PX;
    int letterSpacingPx = 0;
    int wordSpacingPx = 0;
    bool bold = false;
    bool italic = false;
    bool monospace = false;
    bool underline = false;
    bool strike = false;
    Color decorationColor;
    bool hasDecorationColor = false;
    int underlineOffsetPx = 0;
    bool fixed = false;
    std::string fontFamily;
    bool clipped = false;
    int clipX = 0;
    int clipY = 0;
    int clipWidth = 0;
    int clipHeight = 0;
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
    std::string fontFamily;
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
    int letterSpacingPx = 0;
    int wordSpacingPx = 0;
    int lineHeightPx = 0;
    int textIndentPx = 0;
    int textIndentMilli = -1;
    bool textIndentHanging = false;
    bool textIndentEachLine = false;
    int textTransform = 0;
    Color decorationColor;
    bool hasDecorationColor = false;
    int underlineOffsetPx = 0;
    int border = 0;
    int widthPx = -1;
    int heightPx = -1;
    DisplayKind display = DisplayKind::Block;
    std::string fontFamily;
    std::vector<StyleDeclaration> customProperties;
    int rootFontSizePx = 16;
    int viewportWidthPx = 0;
    int viewportHeightPx = 0;
    bool isContainer = false;
    int containerWidthPx = -1;
    int containerHeightPx = -1;
    int translateX = 0;
    int translateY = 0;
    int scaleMilli = 1000;
    int rotateDeg = 0;
    bool hasCssMatrix = false;
    double matrixA = 1;
    double matrixB = 0;
    double matrixC = 0;
    double matrixD = 1;
    double matrixE = 0;
    double matrixF = 0;
    std::string animationName;
    double animationDurationSec = 0;
    int easeKind = 0;
    int easeSteps = 1;
    double easeX1 = 0;
    double easeY1 = 0;
    double easeX2 = 1;
    double easeY2 = 1;
    int blurPx = 0;
    int shadowDx = 0;
    int shadowDy = 0;
    Color shadowColor{0, 0, 0, 0};
    bool linearGradient = false;
    bool radialGradient = false;
    bool repeatingGradient = false;
    int gradientPeriod = 0;
    bool gradientAcross = false;
    Color gradientStart{0, 0, 0, 255};
    Color gradientEnd{0, 0, 0, 255};
    std::vector<int> gridColumns;
    std::vector<std::string> gridTracks;
    int positionKind = 0;
    std::string anchorName;
    std::string anchorQuery;
    bool anchorLeft = false;
};

int LineHeightPx(int fontSizePx);
int CharAdvancePx(int fontSizePx, const std::string& family = "");
std::string EditedControlText(const Node& node);
int TextScrollShift(
    const std::string& text, int caretByte, int innerWidth, int fontSizePx, bool bold, bool italic, const std::string& family = "");
int HitTestTextByte(
    const std::string& text,
    int localX,
    int localY,
    int innerWidth,
    int fontSizePx,
    bool bold,
    bool italic,
    int caretByte,
    const std::string& family = "");
int EmToPx(int fontSizePx, int milli);
ResolvedStyle DefaultStyle();
ResolvedStyle ResolveStyle(const Node& node, const ResolvedStyle& parent, const Document& document, int percentBase = -1, int percentBaseHeight = -1);
LayoutResult LayoutDocument(Document& document, int viewportWidth, int viewportHeight);
void SetSvgAnimationTime(double seconds);
double SvgAnimationTime();
const ElementBox* FindBox(const LayoutResult& layout, const std::string& tag);

}
