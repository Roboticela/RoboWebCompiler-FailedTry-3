#pragma once

#include "Diagnostics/Diagnostics.h"
#include "Dom/Document.h"

namespace RoboWeb {

void WarnSlowPaths(const Document& document, Diagnostics& diagnostics);

}
