#pragma once

#include "Diagnostics/Diagnostics.h"
#include "Dom/Document.h"

namespace RoboWeb {

void ValidateTree(const Document& document, Diagnostics& diagnostics);

}
