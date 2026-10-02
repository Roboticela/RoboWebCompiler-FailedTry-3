#pragma once

#include "Diagnostics/Diagnostics.h"

#include <string>

namespace RoboWeb {

bool DecodeCharacterReference(
    const std::string& source,
    std::size_t& index,
    std::string& output,
    Diagnostics* diagnostics,
    const std::string& path,
    int line,
    int column);

}
