#pragma once

#include "Layout/LayoutEngine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace RoboWeb {

int MeasureTextPx(
    const std::string& text, int fontSizePx, bool bold = false, bool italic = false, const std::string& family = "");
bool RegisterWebFont(const std::string& family, int weight, bool italic, const std::vector<unsigned char>& bytes);
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
    bool italic,
    int clipLeft,
    int clipTop,
    int clipRight,
    int clipBottom,
    const std::string& family = "");

}
