#pragma once

#include "Dom/Document.h"

#include <string>
#include <vector>

namespace RoboWeb {

struct CompiledWebFont {
    std::string family;
    int weight = 400;
    bool italic = false;
    std::vector<unsigned char> bytes;
};

void LoadDocumentStyles(Document& document);
bool CompileLinkedStyles(Document& document, std::vector<CompiledWebFont>& fonts, std::string& error);

}
