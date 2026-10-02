#pragma once

#include "Dom/Document.h"

#include <memory>
#include <string>

namespace RoboWeb {

std::string GenerateCpp(const Document& document);
std::unique_ptr<Node> RebuildStatic(const Document& document);

}
