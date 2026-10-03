#pragma once

#include "Dom/Document.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace RoboWeb {

struct XPathNode {
    Node* node = nullptr;
    bool attribute = false;
    std::string attributeName;
    std::string attributeValue;
};

enum class XPathKind { NodeSet, Boolean, Number, String };

struct XPathValue {
    XPathKind kind = XPathKind::NodeSet;
    std::vector<XPathNode> nodes;
    bool booleanValue = false;
    double numberValue = 0;
    std::string stringValue;
};

struct XPathEnvironment {
    std::map<std::string, std::string> namespaces;
    std::map<std::string, XPathValue> variables;
    XPathNode current;
    Node* sourceRoot = nullptr;
    std::vector<std::pair<std::string, std::string>> idAttributes;
    std::map<std::string, std::string> unparsedEntities;
    std::function<XPathValue(const std::string& nameSpace, const std::string& name, const std::vector<XPathValue>& arguments)> extension;
};

XPathValue XPathBoolean(bool value);
XPathValue XPathNumber(double value);
XPathValue XPathString(const std::string& value);
XPathValue XPathNodes(std::vector<XPathNode> nodes);

std::string XPathStringValue(const XPathNode& node);
double XPathToNumber(const XPathValue& value);
bool XPathToBoolean(const XPathValue& value);
std::string XPathToString(const XPathValue& value);
bool SameXPathNode(const XPathNode& left, const XPathNode& right);
std::string NamespaceUriOf(const Node& element, const std::string& prefix);

XPathValue EvaluateXPath(
    const std::string& expression, const XPathNode& context, std::size_t position, std::size_t size, const XPathEnvironment& environment);

}
