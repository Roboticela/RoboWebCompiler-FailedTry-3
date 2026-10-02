#include "Render/ImageDecoder.h"

#include <fstream>

namespace RoboWeb {

namespace {

std::uint16_t Read16(const unsigned char* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

std::uint32_t Read32(const unsigned char* data) {
    return static_cast<std::uint32_t>(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
}

DecodedImage DecodeBmp(const std::vector<unsigned char>& bytes) {
    DecodedImage image;
    if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M') {
        return image;
    }
    const std::uint32_t pixelOffset = Read32(bytes.data() + 10);
    const std::uint32_t dibSize = Read32(bytes.data() + 14);
    if (dibSize < 40 || bytes.size() < 26) {
        return image;
    }
    const int width = static_cast<int>(Read32(bytes.data() + 18));
    const int heightRaw = static_cast<int>(Read32(bytes.data() + 22));
    const int height = heightRaw < 0 ? -heightRaw : heightRaw;
    const std::uint16_t planes = Read16(bytes.data() + 26);
    const std::uint16_t bitCount = Read16(bytes.data() + 28);
    const std::uint32_t compression = Read32(bytes.data() + 30);
    if (width <= 0 || height <= 0 || planes != 1 || compression != 0 || (bitCount != 24 && bitCount != 32)) {
        return image;
    }
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<std::size_t>(width * height * 4), 255);
    const int stride = ((width * (bitCount / 8) + 3) / 4) * 4;
    const bool bottomUp = heightRaw > 0;
    for (int y = 0; y < height; ++y) {
        const int sourceY = bottomUp ? height - 1 - y : y;
        const std::size_t row = static_cast<std::size_t>(pixelOffset) + static_cast<std::size_t>(sourceY * stride);
        if (row + static_cast<std::size_t>(width * (bitCount / 8)) > bytes.size()) {
            return DecodedImage();
        }
        for (int x = 0; x < width; ++x) {
            const std::size_t pixel = row + static_cast<std::size_t>(x * (bitCount / 8));
            const std::size_t dest = static_cast<std::size_t>((y * width + x) * 4);
            image.rgba[dest] = bytes[pixel + 2];
            image.rgba[dest + 1] = bytes[pixel + 1];
            image.rgba[dest + 2] = bytes[pixel];
            image.rgba[dest + 3] = bitCount == 32 ? bytes[pixel + 3] : 255;
        }
    }
    image.ok = true;
    return image;
}

DecodedImage DecodePpm(const std::vector<unsigned char>& bytes) {
    DecodedImage image;
    if (bytes.size() < 3 || bytes[0] != 'P' || bytes[1] != '6') {
        return image;
    }
    std::size_t index = 2;
    auto skip = [&]() {
        while (index < bytes.size()) {
            if (bytes[index] == '#') {
                while (index < bytes.size() && bytes[index] != '\n') {
                    ++index;
                }
            } else if (bytes[index] == ' ' || bytes[index] == '\n' || bytes[index] == '\r' || bytes[index] == '\t') {
                ++index;
            } else {
                break;
            }
        }
    };
    auto number = [&]() -> int {
        skip();
        int value = 0;
        bool any = false;
        while (index < bytes.size() && bytes[index] >= '0' && bytes[index] <= '9') {
            any = true;
            value = value * 10 + (bytes[index] - '0');
            ++index;
        }
        return any ? value : -1;
    };
    const int width = number();
    const int height = number();
    const int maxValue = number();
    if (width <= 0 || height <= 0 || maxValue != 255) {
        return image;
    }
    if (index < bytes.size() && (bytes[index] == '\n' || bytes[index] == ' ' || bytes[index] == '\r')) {
        ++index;
    }
    const std::size_t need = static_cast<std::size_t>(width * height * 3);
    if (index + need > bytes.size()) {
        return image;
    }
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<std::size_t>(width * height * 4), 255);
    for (int pixel = 0; pixel < width * height; ++pixel) {
        image.rgba[static_cast<std::size_t>(pixel * 4)] = bytes[index + static_cast<std::size_t>(pixel * 3)];
        image.rgba[static_cast<std::size_t>(pixel * 4 + 1)] = bytes[index + static_cast<std::size_t>(pixel * 3 + 1)];
        image.rgba[static_cast<std::size_t>(pixel * 4 + 2)] = bytes[index + static_cast<std::size_t>(pixel * 3 + 2)];
    }
    image.ok = true;
    return image;
}

}  // namespace

DecodedImage DecodeImageFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return DecodedImage();
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') {
        return DecodeBmp(bytes);
    }
    if (bytes.size() >= 2 && bytes[0] == 'P' && bytes[1] == '6') {
        return DecodePpm(bytes);
    }
    return DecodedImage();
}

}
