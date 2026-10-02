#pragma once

#include "Diagnostics/Diagnostics.h"
#include "Dom/Document.h"

#include <string>

namespace RoboWeb {

class HtmlParser {
public:
    Document Parse(const std::string& source, const std::string& path, Diagnostics& diagnostics);
};

}
