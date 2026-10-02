#include "Compiler/CodeGenerator.h"

#include <cstdio>
#include <sstream>
#include <vector>

namespace RoboWeb {

namespace {

std::string Escape(const std::string& text) {
    std::string escaped;
    for (unsigned char character : text) {
        if (character == '\\' || character == '"') {
            escaped.push_back('\\');
            escaped.push_back(static_cast<char>(character));
        } else if (character == '\n') {
            escaped += "\\n";
        } else if (character == '\r') {
            escaped += "\\r";
        } else if (character == '\t') {
            escaped += "\\t";
        } else if (character >= 32 && character < 127) {
            escaped.push_back(static_cast<char>(character));
        } else {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "\\%03o", character);
            escaped += buffer;
        }
    }
    return escaped;
}

struct OwnedAttribute {
    std::string name;
    std::string value;
};

struct OwnedNode {
    int type = 0;
    int elementKind = 0;
    std::string tagName;
    std::string text;
    int attributeBegin = 0;
    int attributeCount = 0;
    int childBegin = 0;
    int childCount = 0;
    int line = 1;
    int column = 1;
};

struct FrozenDocument {
    std::vector<OwnedNode> nodes;
    std::vector<int> children;
    std::vector<OwnedAttribute> attributes;
    std::string sourcePath;
};

void EmitNode(const Node& node, FrozenDocument& frozen) {
    const int nodeIndex = static_cast<int>(frozen.nodes.size());
    frozen.nodes.push_back(OwnedNode{});
    const int attributeBegin = static_cast<int>(frozen.attributes.size());
    for (const Attribute& attribute : node.attributes) {
        frozen.attributes.push_back(OwnedAttribute{attribute.name, attribute.value});
    }
    std::vector<int> childIndexes;
    childIndexes.reserve(node.children.size());
    for (const std::unique_ptr<Node>& child : node.children) {
        childIndexes.push_back(static_cast<int>(frozen.nodes.size()));
        EmitNode(*child, frozen);
    }
    const int childBegin = static_cast<int>(frozen.children.size());
    frozen.children.insert(frozen.children.end(), childIndexes.begin(), childIndexes.end());
    int type = 0;
    if (node.type == Node::Type::Text) {
        type = 1;
    } else if (node.type == Node::Type::Document) {
        type = 2;
    }
    OwnedNode& stored = frozen.nodes[static_cast<std::size_t>(nodeIndex)];
    stored.type = type;
    stored.elementKind = static_cast<int>(node.kind);
    stored.tagName = node.type == Node::Type::Text ? "" : node.tagName;
    stored.text = node.text;
    stored.attributeBegin = attributeBegin;
    stored.attributeCount = static_cast<int>(node.attributes.size());
    stored.childBegin = childBegin;
    stored.childCount = static_cast<int>(node.children.size());
    stored.line = node.line;
    stored.column = node.column;
}

std::unique_ptr<Node> Materialize(const FrozenDocument& frozen, int index) {
    auto node = std::make_unique<Node>();
    if (index < 0 || index >= static_cast<int>(frozen.nodes.size())) {
        return node;
    }
    const OwnedNode& spec = frozen.nodes[static_cast<std::size_t>(index)];
    if (spec.type == 1) {
        node->type = Node::Type::Text;
    } else if (spec.type == 2) {
        node->type = Node::Type::Document;
    } else {
        node->type = Node::Type::Element;
    }
    node->kind = static_cast<ElementKind>(spec.elementKind);
    node->tagName = spec.tagName;
    node->text = spec.text;
    node->line = spec.line;
    node->column = spec.column;
    for (int attributeIndex = 0; attributeIndex < spec.attributeCount; ++attributeIndex) {
        const int slot = spec.attributeBegin + attributeIndex;
        if (slot < 0 || slot >= static_cast<int>(frozen.attributes.size())) {
            continue;
        }
        Attribute attribute;
        attribute.name = frozen.attributes[static_cast<std::size_t>(slot)].name;
        attribute.value = frozen.attributes[static_cast<std::size_t>(slot)].value;
        attribute.line = spec.line;
        attribute.column = spec.column;
        node->attributes.push_back(attribute);
    }
    for (int childSlot = 0; childSlot < spec.childCount; ++childSlot) {
        const int childIndexSlot = spec.childBegin + childSlot;
        if (childIndexSlot < 0 || childIndexSlot >= static_cast<int>(frozen.children.size())) {
            continue;
        }
        std::unique_ptr<Node> child = Materialize(frozen, frozen.children[static_cast<std::size_t>(childIndexSlot)]);
        child->parent = node.get();
        node->children.push_back(std::move(child));
    }
    return node;
}

FrozenDocument Freeze(const Document& document) {
    FrozenDocument frozen;
    frozen.sourcePath = document.sourcePath;
    if (document.root != nullptr) {
        EmitNode(*document.root, frozen);
    }
    if (frozen.children.empty()) {
        frozen.children.push_back(0);
    }
    if (frozen.attributes.empty()) {
        frozen.attributes.push_back(OwnedAttribute{});
    }
    if (frozen.nodes.empty()) {
        frozen.nodes.push_back(OwnedNode{});
    }
    return frozen;
}

}  // namespace

std::unique_ptr<Node> RebuildStatic(const Document& document) {
    const FrozenDocument frozen = Freeze(document);
    if (frozen.nodes.empty()) {
        return std::make_unique<Node>();
    }
    std::unique_ptr<Node> root = Materialize(frozen, 0);
    InitializeControlState(*root);
    return root;
}

std::string GenerateCpp(const Document& document) {
    const FrozenDocument frozen = Freeze(document);
    std::ostringstream output;
    output << "#include \"Runtime/HostLoop.h\"\n\n";
    output << "static const RoboWeb::StaticAttribute kAttributes[] = {\n";
    for (const OwnedAttribute& attribute : frozen.attributes) {
        output << "    {\"" << Escape(attribute.name) << "\", \"" << Escape(attribute.value) << "\"},\n";
    }
    output << "};\n\nstatic const int kChildren[] = {\n";
    for (int child : frozen.children) {
        output << "    " << child << ",\n";
    }
    output << "};\n\nstatic const RoboWeb::StaticNode kNodes[] = {\n";
    for (const OwnedNode& node : frozen.nodes) {
        output << "    {" << node.type << ", " << node.elementKind << ", \"" << Escape(node.tagName) << "\", \""
               << Escape(node.text) << "\", " << node.attributeBegin << ", " << node.attributeCount << ", "
               << node.childBegin << ", " << node.childCount << ", " << node.line << ", " << node.column << "},\n";
    }
    output << "};\n\nstatic const RoboWeb::StaticDocument kDocument = {\n";
    output << "    kNodes,\n    " << frozen.nodes.size() << ",\n";
    output << "    kChildren,\n    " << frozen.children.size() << ",\n";
    output << "    kAttributes,\n    " << frozen.attributes.size() << ",\n";
    output << "    \"" << Escape(document.sourcePath) << "\",\n};\n\n";
    output << "int main(int argc, char** argv) {\n";
    output << "    return RoboWeb::RunCompiledDocument(kDocument, argc, argv);\n}\n";
    return output.str();
}

}
