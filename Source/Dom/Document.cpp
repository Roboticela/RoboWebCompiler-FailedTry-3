#include "Dom/Document.h"

#include "Core/Utf8.h"

namespace RoboWeb {

const Attribute* FindAttribute(const Node& node, const char* name) {
    for (const Attribute& attribute : node.attributes) {
        if (EqualsAsciiIgnoreCase(attribute.name, name)) {
            return &attribute;
        }
    }
    return nullptr;
}

std::string AttributeValue(const Node& node, const char* name) {
    const Attribute* attribute = FindAttribute(node, name);
    if (attribute == nullptr) {
        return "";
    }
    return attribute->value;
}

bool HasAttribute(const Node& node, const char* name) {
    return FindAttribute(node, name) != nullptr;
}

std::string GatherText(const Node& node) {
    if (node.type == Node::Type::Text) {
        return node.text;
    }
    std::string text;
    for (const std::unique_ptr<Node>& child : node.children) {
        text += GatherText(*child);
    }
    return text;
}

Node* FindElementById(Node& node, const std::string& id) {
    if (node.type == Node::Type::Element && AttributeValue(node, "id") == id) {
        return &node;
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        Node* found = FindElementById(*child, id);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

Node* FindAncestor(Node& node, ElementKind kind) {
    Node* cursor = node.parent;
    while (cursor != nullptr) {
        if (cursor->type == Node::Type::Element && cursor->kind == kind) {
            return cursor;
        }
        cursor = cursor->parent;
    }
    return nullptr;
}

void InitializeControlState(Node& node) {
    if (node.type == Node::Type::Element) {
        if (node.kind == ElementKind::Input || node.kind == ElementKind::Textarea ||
            node.kind == ElementKind::Button || node.kind == ElementKind::Option ||
            node.kind == ElementKind::Param || node.kind == ElementKind::Data ||
            node.kind == ElementKind::Meter || node.kind == ElementKind::Progress ||
            node.kind == ElementKind::Li) {
            node.initialValue = AttributeValue(node, "value");
            if (node.kind == ElementKind::Textarea && node.initialValue.empty()) {
                node.initialValue = GatherText(node);
            }
            node.liveValue = node.initialValue;
        }
        node.initialChecked = HasAttribute(node, "checked");
        node.checked = node.initialChecked;
        node.initialSelected = HasAttribute(node, "selected");
        node.selected = node.initialSelected;
        node.initialOpen = HasAttribute(node, "open");
        node.open = node.initialOpen;
        node.popoverOpen = false;
        node.selectExpanded = false;
        node.scriptText = false;
        node.hiddenByScript = false;
        node.marqueeOffset = 0;
        if (HasAttribute(node, "contenteditable")) {
            const std::string editable = ToLowerAscii(AttributeValue(node, "contenteditable"));
            if (editable != "false" && node.liveValue.empty()) {
                node.initialValue = GatherText(node);
                node.liveValue = node.initialValue;
            }
        }
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        child->parent = &node;
        InitializeControlState(*child);
    }
}

void ResetControlState(Node& node) {
    node.liveValue = node.initialValue;
    node.checked = node.initialChecked;
    node.selected = node.initialSelected;
    node.open = node.initialOpen;
    node.popoverOpen = false;
    node.selectExpanded = false;
    for (const std::unique_ptr<Node>& child : node.children) {
        ResetControlState(*child);
    }
}

namespace {

std::unique_ptr<Node> BuildNode(const StaticDocument& source, int index) {
    auto node = std::make_unique<Node>();
    if (index < 0 || index >= source.nodeCount) {
        return node;
    }
    const StaticNode& spec = source.nodes[index];
    if (spec.type == 1) {
        node->type = Node::Type::Text;
    } else if (spec.type == 2) {
        node->type = Node::Type::Document;
    } else {
        node->type = Node::Type::Element;
    }
    node->kind = static_cast<ElementKind>(spec.elementKind);
    node->tagName = spec.tagName != nullptr ? spec.tagName : "";
    node->text = spec.text != nullptr ? spec.text : "";
    node->line = spec.line;
    node->column = spec.column;
    for (int attributeIndex = 0; attributeIndex < spec.attributeCount; ++attributeIndex) {
        const int slot = spec.attributeBegin + attributeIndex;
        if (slot < 0 || slot >= source.attributeCount) {
            continue;
        }
        Attribute attribute;
        attribute.name = source.attributes[slot].name != nullptr ? source.attributes[slot].name : "";
        attribute.value = source.attributes[slot].value != nullptr ? source.attributes[slot].value : "";
        attribute.line = spec.line;
        attribute.column = spec.column;
        node->attributes.push_back(attribute);
    }
    for (int childSlot = 0; childSlot < spec.childCount; ++childSlot) {
        const int childIndexSlot = spec.childBegin + childSlot;
        if (childIndexSlot < 0 || childIndexSlot >= source.childCount) {
            continue;
        }
        std::unique_ptr<Node> child = BuildNode(source, source.children[childIndexSlot]);
        child->parent = node.get();
        node->children.push_back(std::move(child));
    }
    return node;
}

void FindTitle(const Node& node, std::string& title, bool& found) {
    if (found) {
        return;
    }
    if (node.type == Node::Type::Element && node.kind == ElementKind::Title) {
        title = GatherText(node);
        found = true;
        return;
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        FindTitle(*child, title, found);
    }
}

void FindBase(const Node& node, std::string& baseHref, bool& found) {
    if (found) {
        return;
    }
    if (node.type == Node::Type::Element && node.kind == ElementKind::Base) {
        baseHref = AttributeValue(node, "href");
        found = !baseHref.empty();
        return;
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        FindBase(*child, baseHref, found);
    }
}

}  // namespace

std::unique_ptr<Node> BuildFromStatic(const StaticDocument& source) {
    if (source.nodeCount <= 0) {
        return std::make_unique<Node>();
    }
    std::unique_ptr<Node> root = BuildNode(source, 0);
    InitializeControlState(*root);
    return root;
}

void CollectDocumentInfo(Document& document) {
    document.title.clear();
    document.baseHref.clear();
    if (document.root == nullptr) {
        return;
    }
    bool foundTitle = false;
    FindTitle(*document.root, document.title, foundTitle);
    bool foundBase = false;
    FindBase(*document.root, document.baseHref, foundBase);
}

std::string DirectoryOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return "";
    }
    return path.substr(0, slash);
}

bool IsAbsolutePath(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    return path.size() > 2 && path[1] == ':';
}

std::string JoinPath(const std::string& directory, const std::string& relative) {
    if (directory.empty()) {
        return relative;
    }
    const char separator = directory.find('\\') != std::string::npos ? '\\' : '/';
    if (directory.back() == '/' || directory.back() == '\\') {
        return directory + relative;
    }
    return directory + separator + relative;
}

std::string ResolveUrl(const Document& document, const std::string& url) {
    if (url.empty()) {
        return "";
    }
    if (url.find("://") != std::string::npos || url.rfind("data:", 0) == 0 || url[0] == '#') {
        return url;
    }
    if (IsAbsolutePath(url)) {
        return url;
    }
    std::string base = document.baseHref;
    if (!base.empty() && base.find("://") == std::string::npos && !IsAbsolutePath(base)) {
        base = JoinPath(DirectoryOf(document.sourcePath), base);
    }
    if (base.empty()) {
        return JoinPath(DirectoryOf(document.sourcePath), url);
    }
    if (base.find("://") != std::string::npos) {
        return base + "/" + url;
    }
    return JoinPath(DirectoryOf(base), url);
}

}
