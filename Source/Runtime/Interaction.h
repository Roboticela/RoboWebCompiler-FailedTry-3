#pragma once

#include "Dom/Document.h"
#include "Layout/LayoutEngine.h"

namespace RoboWeb {

enum class PointerShape { Arrow, Hand, Text };

bool DispatchClick(Document& document, const LayoutResult& layout, int x, int y);
bool DispatchHover(Document& document, const LayoutResult& layout, int x, int y);
PointerShape PointerAt(const LayoutResult& layout, int x, int y, int scrollY);
bool DispatchKey(Document& document, const std::string& key);
bool DispatchText(Document& document, const std::string& text);
void DispatchScroll(Document& document, int delta, int viewportHeight, int contentHeight);
void RevealFocus(Document& document, const LayoutResult& layout, int viewportHeight);
void ApplyAutofocus(Document& document);
void TickMarquee(Document& document);

}
