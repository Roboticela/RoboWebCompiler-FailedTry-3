#pragma once

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
bool EvaluateCssPx(const std::string& text, const std::vector<StyleDeclaration>& variables, int fontSizePx, int& pixels);
bool EvaluateCssColor(const std::string& text, const std::vector<StyleDeclaration>& variables, std::uint8_t& red, std::uint8_t& green, std::uint8_t& blue);
bool CompileLinkedStyles(Document& document, std::vector<CompiledWebFont>& fonts, std::string& error);
bool CompileRemoteImages(Document& document, std::vector<CompiledImage>& images, std::string& error);

}
