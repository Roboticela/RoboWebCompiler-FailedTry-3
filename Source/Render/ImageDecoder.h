#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace RoboWeb {

struct DecodedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    bool ok = false;
};

DecodedImage DecodeImageFile(const std::string& path);

}
