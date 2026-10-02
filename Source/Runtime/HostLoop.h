#pragma once

#include "Dom/Document.h"

namespace RoboWeb {

int RunCompiledDocument(const StaticDocument& source, int argc, char** argv);
int RunDocument(Document& document, int argc, char** argv);
int RunWindow(Document& document, int width, int height);
void AttachParentConsole();

}
