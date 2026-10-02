#pragma once

#include "Catalog/ElementCatalog.h"

#include <cstdint>
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
    bool scriptText = false;
    bool hiddenByScript = false;
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

struct ElementStyle {
    std::string selector;
    std::string fontFamily;
    int fontWeight = -1;
    int fontStyle = -1;
    int fontSizePx = -1;
    std::uint8_t colorRed = 0;
    std::uint8_t colorGreen = 0;
    std::uint8_t colorBlue = 0;
    bool hasColor = false;
    std::uint8_t backgroundRed = 0;
    std::uint8_t backgroundGreen = 0;
    std::uint8_t backgroundBlue = 0;
    bool hasBackground = false;
    int displayKind = -1;
    int widthPx = -1;
    int heightPx = -1;
    int marginTopPx = -1;
    int marginRightPx = -1;
    int marginBottomPx = -1;
    int marginLeftPx = -1;
    int paddingTopPx = -1;
    int paddingRightPx = -1;
    int paddingBottomPx = -1;
    int paddingLeftPx = -1;
    int textAlign = -1;
    int decoration = -1;
    int whiteSpace = -1;
    int borderPx = -1;
};

struct Document {
    std::unique_ptr<Node> root;
    std::string sourcePath;
    std::string title;
    std::string baseHref;
    std::string status;
    bool quirks = false;
    Node* focus = nullptr;
    Node* hover = nullptr;
    int caretOffset = -1;
    bool caretOn = true;
    int scrollY = 0;
    bool stylesLoaded = false;
    bool scriptsRan = false;
    std::vector<ElementStyle> styleRules;
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
