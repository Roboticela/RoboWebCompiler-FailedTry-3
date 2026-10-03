#include "Render/ImageDecoder.h"

#include <cstring>
#include <fstream>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>

namespace RoboWeb {

namespace {

std::vector<std::pair<std::string, std::vector<unsigned char>>> gCompiledImages;

struct ImageView {
    std::string url;
    const unsigned char* bytes = nullptr;
    std::size_t size = 0;
};

std::vector<ImageView> gImageViews;

DecodedImage DecodeGdiplus(const std::vector<unsigned char>& bytes) {
    DecodedImage image;
    if (bytes.size() < 8) {
        return image;
    }
    static ULONG_PTR token = 0;
    if (token == 0) {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
            return image;
        }
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (memory == nullptr) {
        return image;
    }
    void* destination = GlobalLock(memory);
    if (destination == nullptr) {
        GlobalFree(memory);
        return image;
    }
    std::memcpy(destination, bytes.data(), bytes.size());
    GlobalUnlock(memory);
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK || stream == nullptr) {
        GlobalFree(memory);
        return image;
    }
    Gdiplus::Bitmap bitmap(stream, FALSE);
    stream->Release();
    if (bitmap.GetLastStatus() != Gdiplus::Ok || bitmap.GetWidth() == 0 || bitmap.GetHeight() == 0) {
        return image;
    }
    const int width = static_cast<int>(bitmap.GetWidth());
    const int height = static_cast<int>(bitmap.GetHeight());
    Gdiplus::BitmapData data;
    Gdiplus::Rect rectangle(0, 0, width, height);
    if (bitmap.LockBits(&rectangle, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return image;
    }
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<std::size_t>(width * height * 4), 255);
    for (int row = 0; row < height; ++row) {
        const auto* source = static_cast<const unsigned char*>(data.Scan0) + static_cast<std::size_t>(row * data.Stride);
        for (int column = 0; column < width; ++column) {
            const std::size_t target = static_cast<std::size_t>((row * width + column) * 4);
            image.rgba[target] = source[column * 4 + 2];
            image.rgba[target + 1] = source[column * 4 + 1];
            image.rgba[target + 2] = source[column * 4];
            image.rgba[target + 3] = source[column * 4 + 3];
        }
    }
    bitmap.UnlockBits(&data);
    image.ok = true;
    return image;
}

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

DecodedImage DecodeImageBytes(const std::vector<unsigned char>& bytes) {
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') {
        return DecodeBmp(bytes);
    }
    if (bytes.size() >= 2 && bytes[0] == 'P' && bytes[1] == '6') {
        return DecodePpm(bytes);
    }
    return DecodeGdiplus(bytes);
}

std::vector<PlannedFetch> gFetchPlan;

void ClearResourceFetchPlan() {
    gFetchPlan.clear();
}

void NoteResourceFetch(const std::string& url, const std::string& priority, const std::string& mode) {
    if (url.empty()) {
        return;
    }
    PlannedFetch fetch;
    fetch.url = url;
    fetch.mode = mode;
    fetch.rank = priority == "high" ? 0 : priority == "low" ? 2 : 1;
    std::size_t insertAt = gFetchPlan.size();
    for (std::size_t index = 0; index < gFetchPlan.size(); ++index) {
        if (fetch.rank < gFetchPlan[index].rank) {
            insertAt = index;
            break;
        }
    }
    gFetchPlan.insert(gFetchPlan.begin() + static_cast<std::ptrdiff_t>(insertAt), fetch);
}

const std::vector<PlannedFetch>& ResourceFetchPlan() {
    return gFetchPlan;
}

void RegisterCompiledImageView(const std::string& url, const unsigned char* bytes, std::size_t size) {
    if (bytes == nullptr || size == 0) {
        return;
    }
    for (ImageView& entry : gImageViews) {
        if (entry.url == url) {
            entry.bytes = bytes;
            entry.size = size;
            return;
        }
    }
    ImageView view;
    view.url = url;
    view.bytes = bytes;
    view.size = size;
    gImageViews.push_back(view);
}

void RegisterCompiledImage(const std::string& url, const std::vector<unsigned char>& bytes) {
    for (std::pair<std::string, std::vector<unsigned char>>& entry : gCompiledImages) {
        if (entry.first == url) {
            entry.second = bytes;
            return;
        }
    }
    gCompiledImages.push_back({url, bytes});
}

DecodedImage DecodeImageFile(const std::string& path) {
    for (const ImageView& view : gImageViews) {
        if (view.url == path && view.bytes != nullptr) {
            return DecodeImageBytes(std::vector<unsigned char>(view.bytes, view.bytes + view.size));
        }
    }
    for (const std::pair<std::string, std::vector<unsigned char>>& entry : gCompiledImages) {
        if (entry.first == path) {
            return DecodeImageBytes(entry.second);
        }
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return DecodedImage();
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return DecodeImageBytes(bytes);
}

}
