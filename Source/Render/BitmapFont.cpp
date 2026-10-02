#include "Render/BitmapFont.h"

#include "Core/Constants.h"
#include "Core/Utf8.h"
#include "Render/InterFontData.h"

#include <algorithm>
#include <memory>
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
    std::vector<unsigned char> owned;
    const unsigned char* bytes = nullptr;
    std::string family;
    int weight = 400;
    bool italic = false;
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

std::vector<std::unique_ptr<Face>> gFaces;
std::unordered_map<GlyphKey, Glyph, GlyphKeyHash> gGlyphs;

void EnsureInterFaces() {
    if (!gFaces.empty()) {
        return;
    }
    const unsigned char* bytes[] = {kInterRegular, kInterBold, kInterItalic, kInterBoldItalic};
    const int weights[] = {400, 700, 400, 700};
    const bool italics[] = {false, false, true, true};
    for (int index = 0; index < 4; ++index) {
        auto face = std::make_unique<Face>();
        face->bytes = bytes[index];
        face->family = "inter";
        face->weight = weights[index];
        face->italic = italics[index];
        gFaces.push_back(std::move(face));
    }
}

bool EnsureFace(int faceIndex) {
    EnsureInterFaces();
    if (faceIndex < 0 || faceIndex >= static_cast<int>(gFaces.size())) {
        return false;
    }
    Face& face = *gFaces[static_cast<std::size_t>(faceIndex)];
    if (face.ready) {
        return true;
    }
    if (face.bytes == nullptr) {
        return false;
    }
    const int offset = stbtt_GetFontOffsetForIndex(face.bytes, 0);
    if (offset < 0) {
        return false;
    }
    face.ready = stbtt_InitFont(&face.info, face.bytes, offset) != 0;
    return face.ready;
}

int FaceIndex(bool bold, bool italic);

int ResolveFace(const std::string& family, bool bold, bool italic) {
    EnsureInterFaces();
    const std::string name = ToLowerAscii(family);
    if (name.empty() || name == "inter" || name == "sans-serif" || name == "serif" || name == "system-ui" || name == "ui-sans-serif") {
        return FaceIndex(bold, italic);
    }
    int best = -1;
    int bestScore = 100000;
    const int wanted = bold ? 700 : 400;
    for (int index = 0; index < static_cast<int>(gFaces.size()); ++index) {
        const Face& face = *gFaces[static_cast<std::size_t>(index)];
        if (face.family != name) {
            continue;
        }
        int score = std::abs(face.weight - wanted);
        if (face.italic != italic) {
            score += 1000;
        }
        if (score < bestScore) {
            bestScore = score;
            best = index;
        }
    }
    if (best >= 0) {
        return best;
    }
    return FaceIndex(bold, italic);
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

const Glyph* GlyphFor(char32_t codePoint, int fontSizePx, bool bold, bool italic, const std::string& family) {
    if (fontSizePx < 1) {
        fontSizePx = 1;
    }
    if (codePoint == 0xA0) {
        codePoint = ' ';
    }
    const int faceIndex = ResolveFace(family, bold, italic);
    if (!EnsureFace(faceIndex)) {
        return nullptr;
    }
    const GlyphKey key{fontSizePx, faceIndex, codePoint};
    const auto found = gGlyphs.find(key);
    if (found != gGlyphs.end()) {
        return &found->second;
    }
    stbtt_fontinfo& info = gFaces[static_cast<std::size_t>(faceIndex)]->info;
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

int BaselineOffset(int fontSizePx, bool bold, bool italic, const std::string& family) {
    const int faceIndex = ResolveFace(family, bold, italic);
    if (!EnsureFace(faceIndex)) {
        return fontSizePx;
    }
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_fontinfo& info = gFaces[static_cast<std::size_t>(faceIndex)]->info;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, static_cast<float>(fontSizePx));
    const int content = static_cast<int>((ascent - descent) * scale + 0.5f);
    const int line = LineHeightPx(fontSizePx);
    return (line - content) / 2 + static_cast<int>(ascent * scale + 0.5f);
}

void BlendCoverage(
    std::uint8_t* pixels,
    int frameWidth,
    int frameHeight,
    int x,
    int y,
    Color color,
    int coverage,
    int clipLeft,
    int clipTop,
    int clipRight,
    int clipBottom) {
    if (coverage <= 0 || x < 0 || y < 0 || x >= frameWidth || y >= frameHeight || x < clipLeft || y < clipTop || x >= clipRight ||
        y >= clipBottom) {
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

int CharAdvancePx(int fontSizePx, const std::string& family) {
    return MeasureTextPx("0", fontSizePx, false, false, family);
}

int EmToPx(int fontSizePx, int milli) {
    return (fontSizePx * milli + 500) / 1000;
}

int MeasureTextPx(const std::string& text, int fontSizePx, bool bold, bool italic, const std::string& family, int letterSpacingPx, int wordSpacingPx) {
    int width = 0;
    char32_t previous = 0;
    bool hasPrevious = false;
    const int faceIndex = ResolveFace(family, bold, italic);
    std::size_t index = 0;
    while (index < text.size()) {
        char32_t codePoint = 0;
        DecodeUtf8(text, index, codePoint);
        if (codePoint == '\n') {
            continue;
        }
        const Glyph* glyph = GlyphFor(codePoint, fontSizePx, bold, italic, family);
        if (glyph == nullptr) {
            continue;
        }
        if (hasPrevious) {
            width += letterSpacingPx;
            if (EnsureFace(faceIndex)) {
                stbtt_fontinfo& info = gFaces[static_cast<std::size_t>(faceIndex)]->info;
                const float scale = stbtt_ScaleForMappingEmToPixels(&info, static_cast<float>(fontSizePx < 1 ? 1 : fontSizePx));
                const int kern = stbtt_GetCodepointKernAdvance(&info, static_cast<int>(previous), static_cast<int>(codePoint));
                width += static_cast<int>(kern * scale + (kern < 0 ? -0.5f : 0.5f));
            }
        }
        width += glyph->advance;
        if (codePoint == ' ') {
            width += wordSpacingPx;
        }
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
    bool italic,
    int clipLeft,
    int clipTop,
    int clipRight,
    int clipBottom,
    const std::string& family,
    int letterSpacingPx,
    int wordSpacingPx) {
    if (pixels == nullptr) {
        return;
    }
    const int baseline = y + BaselineOffset(fontSizePx, bold, italic, family);
    int cursor = x;
    char32_t previous = 0;
    bool hasPrevious = false;
    const int faceIndex = ResolveFace(family, bold, italic);
    std::size_t index = 0;
    while (index < text.size()) {
        char32_t codePoint = 0;
        DecodeUtf8(text, index, codePoint);
        if (codePoint == '\n') {
            continue;
        }
        const Glyph* glyph = GlyphFor(codePoint, fontSizePx, bold, italic, family);
        if (glyph == nullptr) {
            continue;
        }
        if (hasPrevious) {
            cursor += letterSpacingPx;
            if (EnsureFace(faceIndex)) {
                stbtt_fontinfo& info = gFaces[static_cast<std::size_t>(faceIndex)]->info;
                const float scale = stbtt_ScaleForMappingEmToPixels(&info, static_cast<float>(fontSizePx < 1 ? 1 : fontSizePx));
                const int kern = stbtt_GetCodepointKernAdvance(&info, static_cast<int>(previous), static_cast<int>(codePoint));
                cursor += static_cast<int>(kern * scale + (kern < 0 ? -0.5f : 0.5f));
            }
        }
        for (int row = 0; row < glyph->height; ++row) {
            for (int column = 0; column < glyph->width; ++column) {
                const int coverage = glyph->coverage[static_cast<std::size_t>(row * glyph->width + column)];
                BlendCoverage(pixels, frameWidth, frameHeight, cursor + glyph->left + column, baseline + glyph->top + row, color, coverage,
                    clipLeft, clipTop, clipRight, clipBottom);
            }
        }
        cursor += glyph->advance;
        if (codePoint == ' ') {
            cursor += wordSpacingPx;
        }
        previous = codePoint;
        hasPrevious = true;
    }
}

bool RegisterWebFont(const std::string& family, int weight, bool italic, const std::vector<unsigned char>& bytes) {
    if (bytes.size() < 12 || family.empty()) {
        return false;
    }
    EnsureInterFaces();
    const std::string name = ToLowerAscii(family);
    for (const std::unique_ptr<Face>& existing : gFaces) {
        if (existing->family == name && existing->weight == weight && existing->italic == italic) {
            return true;
        }
    }
    auto face = std::make_unique<Face>();
    face->owned = bytes;
    face->bytes = face->owned.data();
    face->family = name;
    face->weight = weight < 1 ? 400 : weight;
    face->italic = italic;
    const int offset = stbtt_GetFontOffsetForIndex(face->bytes, 0);
    if (offset < 0 || stbtt_InitFont(&face->info, face->bytes, offset) == 0) {
        return false;
    }
    face->ready = true;
    gFaces.push_back(std::move(face));
    return true;
}

}
