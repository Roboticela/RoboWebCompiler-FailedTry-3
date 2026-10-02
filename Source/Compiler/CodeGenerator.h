#pragma once

#include "Dom/Document.h"
#include "Render/StyleSheet.h"

#include <memory>
#include <string>
#include <vector>

namespace RoboWeb {

std::string GenerateCpp(
    const Document& document,
    const std::vector<CompiledWebFont>& fonts = {},
    const std::vector<CompiledImage>& images = {});
std::unique_ptr<Node> RebuildStatic(const Document& document);

}
