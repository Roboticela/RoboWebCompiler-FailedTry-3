#pragma once

#include "Dom/Document.h"

namespace RoboWeb {

void RunDocumentScripts(Document& document);
bool RunEvent(Document& document, Node& node, const char* eventName);

}
