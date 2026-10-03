#pragma once

#include "Layout/LayoutEngine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace RoboWeb {

struct Framebuffer {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

Framebuffer RenderDocument(const LayoutResult& layout, int scrollY, bool settled = true);
void RenderDirty(const LayoutResult& layout, int scrollY, Framebuffer& framebuffer, int x, int y, int width, int height, bool settled = true);
bool WritePpm(const Framebuffer& framebuffer, const std::string& path);
std::uint8_t PixelRed(const Framebuffer& framebuffer, int x, int y);
std::uint8_t PixelGreen(const Framebuffer& framebuffer, int x, int y);
std::uint8_t PixelBlue(const Framebuffer& framebuffer, int x, int y);

}
