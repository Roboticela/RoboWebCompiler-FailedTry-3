#pragma once

#include "Diagnostics/Diagnostics.h"
#include "Dom/Document.h"

#include <string>
#include <vector>

namespace RoboWeb {

struct CompiledImage {
    std::string url;
    std::vector<unsigned char> bytes;
};

struct CompiledWebFont {
    std::string family;
    int weight = 400;
    bool italic = false;
    std::vector<unsigned char> bytes;
};

void LoadDocumentStyles(Document& document);
void ApplyStyleText(ElementStyle& rule, const std::string& declarations);

struct CssLengthContext {
    int fontSizePx = 16;
    int rootFontSizePx = 16;
    int lineHeightPx = 0;
    int rootLineHeightPx = 0;
    int zeroAdvancePx = 8;
    int rootZeroAdvancePx = 8;
    int viewportWidth = 0;
    int viewportHeight = 0;
    int percentBase = -1;
    int containerWidth = -1;
    int containerHeight = -1;
};

bool EvaluateCssPx(const std::string& text, const std::vector<StyleDeclaration>& variables, const CssLengthContext& context, int& pixels);
bool EvaluateCssColor(const std::string& text, const std::vector<StyleDeclaration>& variables, std::uint8_t& red, std::uint8_t& green, std::uint8_t& blue);
void ValidateCssText(const std::string& css, const std::string& path, int line, Diagnostics& diagnostics);
bool CompileLinkedStyles(Document& document, std::vector<CompiledWebFont>& fonts, std::string& error);
bool CompileStylesheetText(Document& document, const std::string& css, std::vector<CompiledWebFont>& fonts, std::string& error);
bool LoadTextResource(const Document& document, const std::string& url, std::string& text, std::string& error);
bool CompileRemoteImages(Document& document, std::vector<CompiledImage>& images, std::string& error);

}
