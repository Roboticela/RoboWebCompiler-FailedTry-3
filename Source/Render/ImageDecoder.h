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
DecodedImage DecodeImageBytes(const std::vector<unsigned char>& bytes);
void RegisterCompiledImage(const std::string& url, const std::vector<unsigned char>& bytes);

struct PlannedFetch {
    std::string url;
    std::string mode;
    int rank = 1;
};

void ClearResourceFetchPlan();
void NoteResourceFetch(const std::string& url, const std::string& priority, const std::string& mode);
const std::vector<PlannedFetch>& ResourceFetchPlan();

}
