#pragma once

#include "Dom/Document.h"
#include "Layout/LayoutEngine.h"

namespace RoboWeb {

bool DispatchClick(Document& document, const LayoutResult& layout, int x, int y);
bool DispatchKey(Document& document, const std::string& key);
void DispatchScroll(Document& document, int delta, int viewportHeight, int contentHeight);
void TickMarquee(Document& document);

}
