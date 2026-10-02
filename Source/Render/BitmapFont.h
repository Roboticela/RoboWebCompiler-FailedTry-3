#pragma once

#include "Layout/LayoutEngine.h"

#include <cstdint>
#include <string>

namespace RoboWeb {

int MeasureTextPx(const std::string& text, int fontSizePx);
void DrawTextPx(
    std::uint8_t* pixels,
    int frameWidth,
    int frameHeight,
    int x,
    int y,
    const std::string& text,
    int fontSizePx,
    Color color,
    bool bold,
    bool italic);

}
