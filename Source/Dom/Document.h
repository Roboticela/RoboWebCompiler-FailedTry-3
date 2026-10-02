#pragma once

#include "Catalog/ElementCatalog.h"

#include <memory>
#include <string>
#include <vector>

namespace RoboWeb {

struct Attribute {
    std::string name;
    std::string value;
    int line = 1;
    int column = 1;
};

struct Node {
    enum class Type { Document, Element, Text };

    Type type = Type::Element;
    ElementKind kind = ElementKind::Unknown;
    std::string tagName;
    std::vector<Attribute> attributes;
    std::vector<std::unique_ptr<Node>> children;
    std::string text;
    int line = 1;
    int column = 1;
    std::string initialValue;
    std::string liveValue;
    bool initialChecked = false;
    bool checked = false;
    bool initialSelected = false;
    bool selected = false;
    bool initialOpen = false;
    bool open = false;
    bool popoverOpen = false;
    bool selectExpanded = false;
    int marqueeOffset = 0;
    Node* parent = nullptr;
};

struct StaticAttribute {
    const char* name;
    const char* value;
};

struct StaticNode {
    int type;
    int elementKind;
    const char* tagName;
    const char* text;
    int attributeBegin;
    int attributeCount;
    int childBegin;
    int childCount;
    int line;
    int column;
};

struct StaticDocument {
    const StaticNode* nodes;
    int nodeCount;
    const int* children;
    int childCount;
    const StaticAttribute* attributes;
    int attributeCount;
    const char* sourcePath;
};

struct Document {
    std::unique_ptr<Node> root;
    std::string sourcePath;
    std::string title;
    std::string baseHref;
    std::string status;
    bool quirks = false;
    Node* focus = nullptr;
    int scrollY = 0;
};

const Attribute* FindAttribute(const Node& node, const char* name);
std::string AttributeValue(const Node& node, const char* name);
bool HasAttribute(const Node& node, const char* name);
std::string GatherText(const Node& node);
Node* FindElementById(Node& node, const std::string& id);
Node* FindAncestor(Node& node, ElementKind kind);
void InitializeControlState(Node& node);
void ResetControlState(Node& node);
std::unique_ptr<Node> BuildFromStatic(const StaticDocument& source);
void CollectDocumentInfo(Document& document);
std::string DirectoryOf(const std::string& path);
std::string JoinPath(const std::string& directory, const std::string& relative);
bool IsAbsolutePath(const std::string& path);
std::string ResolveUrl(const Document& document, const std::string& url);

}
