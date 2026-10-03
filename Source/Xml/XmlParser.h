#pragma once

#include "Diagnostics/Diagnostics.h"
#include "Dom/Document.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace RoboWeb {

struct XmlParseResult {
    Document document;
    std::string version = "1.0";
    std::string encoding = "UTF-8";
    bool wellFormed = false;
    // element QName (empty means every element) and the attribute that has type ID.
    std::vector<std::pair<std::string, std::string>> idAttributes;
    std::map<std::string, std::string> generalEntities;
    std::map<std::string, std::string> unparsedEntities;
    std::vector<std::pair<std::string, std::string>> stylesheetLinks;
};

XmlParseResult ParseXml(const std::string& source, const std::string& path, Diagnostics& diagnostics);
std::string SerializeXml(const Node& node);
std::string SerializeText(const Node& node);

}
