#include "Render/BitmapFont.h"

#include "Core/Constants.h"
#include "Core/Utf8.h"
#include "Render/InterFontData.h"

#include <unordered_map>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "Render/StbTrueType.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace RoboWeb {

namespace {

struct Face {
    stbtt_fontinfo info;
    bool ready = false;
};

struct Glyph {
    int advance = 0;
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> coverage;
};

struct GlyphKey {
    int size = 0;
    int face = 0;
    char32_t codePoint = 0;

    bool operator==(const GlyphKey& other) const {
        return size == other.size && face == other.face && codePoint == other.codePoint;
    }
};

struct GlyphKeyHash {
    std::size_t operator()(const GlyphKey& key) const {
        return (static_cast<std::size_t>(key.size) << 21) ^ (static_cast<std::size_t>(key.face) << 18) ^ key.codePoint;
    }
};

Face gFaces[4];
std::unordered_map<GlyphKey, Glyph, GlyphKeyHash> gGlyphs;

bool EnsureFace(int faceIndex) {
    Face& face = gFaces[faceIndex];
    if (face.ready) {
        return true;
    }
    const unsigned char* bytes = kInterRegular;
    if (faceIndex == 1) {
        bytes = kInterBold;
    } else if (faceIndex == 2) {
        bytes = kInterItalic;
    } else if (faceIndex == 3) {
        bytes = kInterBoldItalic;
    }
    const int offset = stbtt_GetFontOffsetForIndex(bytes, 0);
    face.ready = stbtt_InitFont(&face.info, bytes, offset) != 0;
    return face.ready;
}

int FaceIndex(bool bold, bool italic) {
    if (bold && italic) {
        return 3;
    }
    if (bold) {
        return 1;
    }
    if (italic) {
        return 2;
    }
    return 0;
}

const Glyph* GlyphFor(char32_t codePoint, int fontSizePx, bool bold, bool italic) {
    if (fontSizePx < 1) {
        fontSizePx = 1;
    }
    if (codePoint == 0xA0) {
        codePoint = ' ';
    }
    const int faceIndex = FaceIndex(bold, italic);
    if (!EnsureFace(faceIndex)) {
        return nullptr;
    }
    const GlyphKey key{fontSizePx, faceIndex, codePoint};
    const auto found = gGlyphs.find(key);
    if (found != gGlyphs.end()) {
        return &found->second;
    }
    stbtt_fontinfo& info = gFaces[faceIndex].info;
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, static_cast<float>(fontSizePx));
    int advance = 0;
    int leftSide = 0;
    stbtt_GetCodepointHMetrics(&info, static_cast<int>(codePoint), &advance, &leftSide);
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(&info, static_cast<int>(codePoint), scale, scale, &x0, &y0, &x1, &y1);
    Glyph glyph;
    glyph.advance = static_cast<int>(advance * scale + 0.5f);
    if (glyph.advance < 1 && codePoint != ' ') {
        glyph.advance = 1;
    }
    glyph.left = x0;
    glyph.top = y0;
    glyph.width = x1 - x0;
    glyph.height = y1 - y0;
    if (glyph.width > 0 && glyph.height > 0) {
        glyph.coverage.assign(static_cast<std::size_t>(glyph.width * glyph.height), 0);
        stbtt_MakeCodepointBitmap(
            &info, glyph.coverage.data(), glyph.width, glyph.height, glyph.width, scale, scale, static_cast<int>(codePoint));
    }
    const auto stored = gGlyphs.emplace(key, std::move(glyph));
    return &stored.first->second;
}

int BaselineOffset(int fontSizePx, bool bold, bool italic) {
    const int faceIndex = FaceIndex(bold, italic);
    if (!EnsureFace(faceIndex)) {
        return fontSizePx;
    }
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&gFaces[faceIndex].info, &ascent, &descent, &lineGap);
    const float scale = stbtt_ScaleForMappingEmToPixels(&gFaces[faceIndex].info, static_cast<float>(fontSizePx));
    const int content = static_cast<int>((ascent - descent) * scale + 0.5f);
    const int line = LineHeightPx(fontSizePx);
    return (line - content) / 2 + static_cast<int>(ascent * scale + 0.5f);
}

void BlendCoverage(std::uint8_t* pixels, int frameWidth, int frameHeight, int x, int y, Color color, int coverage) {
    if (coverage <= 0 || x < 0 || y < 0 || x >= frameWidth || y >= frameHeight) {
        return;
    }
    const std::size_t offset = static_cast<std::size_t>((y * frameWidth + x) * 4);
    if (coverage >= 220) {
        pixels[offset] = color.red;
        pixels[offset + 1] = color.green;
        pixels[offset + 2] = color.blue;
        pixels[offset + 3] = 255;
        return;
    }
    const int inverse = 255 - coverage;
    pixels[offset] = static_cast<std::uint8_t>((color.red * coverage + pixels[offset] * inverse) / 255);
    pixels[offset + 1] = static_cast<std::uint8_t>((color.green * coverage + pixels[offset + 1] * inverse) / 255);
    pixels[offset + 2] = static_cast<std::uint8_t>((color.blue * coverage + pixels[offset + 2] * inverse) / 255);
    pixels[offset + 3] = 255;
}

}  // namespace

int LineHeightPx(int fontSizePx) {
    if (fontSizePx < 1) {
        fontSizePx = 1;
    }
    return fontSizePx * LINE_HEIGHT_NUMERATOR / LINE_HEIGHT_DENOMINATOR;
}

int CharAdvancePx(int fontSizePx) {
    return MeasureTextPx("0", fontSizePx, false, false);
}

int EmToPx(int fontSizePx, int milli) {
    return (fontSizePx * milli + 500) / 1000;
}

int MeasureTextPx(const std::string& text, int fontSizePx, bool bold, bool italic) {
    int width = 0;
    char32_t previous = 0;
    bool hasPrevious = false;
    const int faceIndex = FaceIndex(bold, italic);
    std::size_t index = 0;
    while (index < text.size()) {
        char32_t codePoint = 0;
        DecodeUtf8(text, index, codePoint);
        if (codePoint == '\n') {
            continue;
        }
        const Glyph* glyph = GlyphFor(codePoint, fontSizePx, bold, italic);
        if (glyph == nullptr) {
            continue;
        }
        if (hasPrevious && EnsureFace(faceIndex)) {
            const float scale = stbtt_ScaleForMappingEmToPixels(&gFaces[faceIndex].info, static_cast<float>(fontSizePx < 1 ? 1 : fontSizePx));
            const int kern = stbtt_GetCodepointKernAdvance(&gFaces[faceIndex].info, static_cast<int>(previous), static_cast<int>(codePoint));
            width += static_cast<int>(kern * scale + (kern < 0 ? -0.5f : 0.5f));
        }
        width += glyph->advance;
        previous = codePoint;
        hasPrevious = true;
    }
    return width;
}

void DrawTextPx(
    std::uint8_t* pixels,
    int frameWidth,
    int frameHeight,
    int x,
    int y,
    const std::string& text,
    int fontSizePx,
    Color color,
    bool bold,
    bool italic) {
    if (pixels == nullptr) {
        return;
    }
    const int baseline = y + BaselineOffset(fontSizePx, bold, italic);
    int cursor = x;
    char32_t previous = 0;
    bool hasPrevious = false;
    const int faceIndex = FaceIndex(bold, italic);
    std::size_t index = 0;
    while (index < text.size()) {
        char32_t codePoint = 0;
        DecodeUtf8(text, index, codePoint);
        if (codePoint == '\n') {
            continue;
        }
        const Glyph* glyph = GlyphFor(codePoint, fontSizePx, bold, italic);
        if (glyph == nullptr) {
            continue;
        }
        if (hasPrevious && EnsureFace(faceIndex)) {
            const float scale = stbtt_ScaleForMappingEmToPixels(&gFaces[faceIndex].info, static_cast<float>(fontSizePx < 1 ? 1 : fontSizePx));
            const int kern = stbtt_GetCodepointKernAdvance(&gFaces[faceIndex].info, static_cast<int>(previous), static_cast<int>(codePoint));
            cursor += static_cast<int>(kern * scale + (kern < 0 ? -0.5f : 0.5f));
        }
        for (int row = 0; row < glyph->height; ++row) {
            for (int column = 0; column < glyph->width; ++column) {
                const int coverage = glyph->coverage[static_cast<std::size_t>(row * glyph->width + column)];
                BlendCoverage(pixels, frameWidth, frameHeight, cursor + glyph->left + column, baseline + glyph->top + row, color, coverage);
            }
        }
        cursor += glyph->advance;
        previous = codePoint;
        hasPrevious = true;
    }
}

}
