#include "Render/Renderer.h"

#include "Render/BitmapFont.h"
#include "Render/ImageDecoder.h"

#include <fstream>

namespace RoboWeb {

namespace {

bool InsideClip(const DrawOp& op, int originY, int x, int y, int frameWidth, int frameHeight) {
    if (x < 0 || y < 0 || x >= frameWidth || y >= frameHeight) {
        return false;
    }
    if (!op.clipped) {
        return true;
    }
    const int top = op.clipY - originY;
    return x >= op.clipX && y >= top && x < op.clipX + op.clipWidth && y < top + op.clipHeight;
}

void Blend(Framebuffer& framebuffer, int x, int y, Color color) {
    if (x < 0 || y < 0 || x >= framebuffer.width || y >= framebuffer.height || color.alpha == 0) {
        return;
    }
    const std::size_t offset = static_cast<std::size_t>((y * framebuffer.width + x) * 4);
    framebuffer.pixels[offset] = color.red;
    framebuffer.pixels[offset + 1] = color.green;
    framebuffer.pixels[offset + 2] = color.blue;
    framebuffer.pixels[offset + 3] = 255;
}

}  // namespace

Framebuffer RenderDocument(const LayoutResult& layout, int scrollY) {
    Framebuffer framebuffer;
    framebuffer.width = layout.viewportWidth;
    framebuffer.height = layout.viewportHeight;
    framebuffer.pixels.assign(static_cast<std::size_t>(framebuffer.width * framebuffer.height * 4), 255);
    for (const DrawOp& op : layout.ops) {
        const int originY = op.fixed ? 0 : scrollY;
        const int x = op.x;
        const int y = op.y - originY;
        if (op.kind == DrawOp::Kind::Fill) {
            for (int row = 0; row < op.height; ++row) {
                for (int column = 0; column < op.width; ++column) {
                    if (InsideClip(op, originY, x + column, y + row, framebuffer.width, framebuffer.height)) {
                        Blend(framebuffer, x + column, y + row, op.color);
                    }
                }
            }
        } else if (op.kind == DrawOp::Kind::Stroke) {
            for (int column = 0; column < op.width; ++column) {
                if (InsideClip(op, originY, x + column, y, framebuffer.width, framebuffer.height)) {
                    Blend(framebuffer, x + column, y, op.color);
                }
                if (InsideClip(op, originY, x + column, y + op.height - 1, framebuffer.width, framebuffer.height)) {
                    Blend(framebuffer, x + column, y + op.height - 1, op.color);
                }
            }
            for (int row = 0; row < op.height; ++row) {
                if (InsideClip(op, originY, x, y + row, framebuffer.width, framebuffer.height)) {
                    Blend(framebuffer, x, y + row, op.color);
                }
                if (InsideClip(op, originY, x + op.width - 1, y + row, framebuffer.width, framebuffer.height)) {
                    Blend(framebuffer, x + op.width - 1, y + row, op.color);
                }
            }
        } else if (op.kind == DrawOp::Kind::Text) {
            int clipLeft = 0;
            int clipTop = 0;
            int clipRight = framebuffer.width;
            int clipBottom = framebuffer.height;
            if (op.clipped) {
                clipLeft = op.clipX;
                clipTop = op.clipY - originY;
                clipRight = op.clipX + op.clipWidth;
                clipBottom = clipTop + op.clipHeight;
            }
            DrawTextPx(framebuffer.pixels.data(), framebuffer.width, framebuffer.height, x, y, op.text, op.fontSizePx, op.color,
                op.bold, op.italic, clipLeft, clipTop, clipRight, clipBottom, op.fontFamily);
            const int textWidth = MeasureTextPx(op.text, op.fontSizePx, op.bold, op.italic, op.fontFamily);
            const int line = LineHeightPx(op.fontSizePx);
            if (op.underline) {
                for (int column = 0; column < textWidth; ++column) {
                    if (InsideClip(op, originY, x + column, y + line - 2, framebuffer.width, framebuffer.height)) {
                        Blend(framebuffer, x + column, y + line - 2, op.color);
                    }
                }
            }
            if (op.strike) {
                for (int column = 0; column < textWidth; ++column) {
                    if (InsideClip(op, originY, x + column, y + line / 2, framebuffer.width, framebuffer.height)) {
                        Blend(framebuffer, x + column, y + line / 2, op.color);
                    }
                }
            }
        } else if (op.kind == DrawOp::Kind::Image) {
            DecodedImage image = DecodeImageFile(op.imagePath);
            if (!image.ok || image.width <= 0 || image.height <= 0) {
                continue;
            }
            for (int row = 0; row < op.height; ++row) {
                for (int column = 0; column < op.width; ++column) {
                    const int sourceX = column * image.width / op.width;
                    const int sourceY = row * image.height / op.height;
                    const std::size_t source = static_cast<std::size_t>((sourceY * image.width + sourceX) * 4);
                    Color color{image.rgba[source], image.rgba[source + 1], image.rgba[source + 2], 255};
                    if (InsideClip(op, originY, x + column, y + row, framebuffer.width, framebuffer.height)) {
                        Blend(framebuffer, x + column, y + row, color);
                    }
                }
            }
        }
    }
    return framebuffer;
}

bool WritePpm(const Framebuffer& framebuffer, const std::string& path) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output << "P6\n" << framebuffer.width << " " << framebuffer.height << "\n255\n";
    for (int index = 0; index < framebuffer.width * framebuffer.height; ++index) {
        const std::size_t offset = static_cast<std::size_t>(index * 4);
        output.put(static_cast<char>(framebuffer.pixels[offset]));
        output.put(static_cast<char>(framebuffer.pixels[offset + 1]));
        output.put(static_cast<char>(framebuffer.pixels[offset + 2]));
    }
    return static_cast<bool>(output);
}

std::uint8_t PixelRed(const Framebuffer& framebuffer, int x, int y) {
    if (x < 0 || y < 0 || x >= framebuffer.width || y >= framebuffer.height) {
        return 0;
    }
    return framebuffer.pixels[static_cast<std::size_t>((y * framebuffer.width + x) * 4)];
}

std::uint8_t PixelGreen(const Framebuffer& framebuffer, int x, int y) {
    if (x < 0 || y < 0 || x >= framebuffer.width || y >= framebuffer.height) {
        return 0;
    }
    return framebuffer.pixels[static_cast<std::size_t>((y * framebuffer.width + x) * 4 + 1)];
}

std::uint8_t PixelBlue(const Framebuffer& framebuffer, int x, int y) {
    if (x < 0 || y < 0 || x >= framebuffer.width || y >= framebuffer.height) {
        return 0;
    }
    return framebuffer.pixels[static_cast<std::size_t>((y * framebuffer.width + x) * 4 + 2)];
}

}
