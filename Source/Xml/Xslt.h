#pragma once

#include <string>
#include <vector>

namespace RoboWeb {

struct XsltResult {
    std::string output;
    std::string method = "xml";
    std::vector<std::string> messages;
    bool ok = false;
    std::string error;
};

XsltResult TransformXml(const std::string& source, const std::string& stylesheet, const std::string& stylesheetPath = "");

}
