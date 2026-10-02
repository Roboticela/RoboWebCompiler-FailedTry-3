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

void WriteNode(std::ostringstream& output, const Node& node, const std::string& variable, int indent) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    if (node.type == Node::Type::Document) {
        output << pad << "auto " << variable << " = std::make_unique<RoboWeb::Node>();\n";
        output << pad << variable << "->type = RoboWeb::Node::Type::Document;\n";
        output << pad << variable << "->line = " << node.line << ";\n";
        output << pad << variable << "->column = " << node.column << ";\n";
    } else if (node.type == Node::Type::Text) {
        output << pad << "auto " << variable << " = MakeText(\"" << Escape(node.text) << "\", " << node.line << ", " << node.column
               << ");\n";
    } else {
        output << pad << "auto " << variable << " = MakeElement(" << static_cast<int>(node.kind) << ", \"" << Escape(node.tagName)
               << "\", " << node.line << ", " << node.column << ");\n";
        for (const Attribute& attribute : node.attributes) {
            output << pad << "AddAttribute(*" << variable << ", \"" << Escape(attribute.name) << "\", \"" << Escape(attribute.value)
                   << "\");\n";
        }
    }
    int childNumber = 0;
    for (const std::unique_ptr<Node>& child : node.children) {
        const std::string childVariable = variable + "c" + std::to_string(childNumber);
        ++childNumber;
        WriteNode(output, *child, childVariable, indent);
        output << pad << "AppendChild(*" << variable << ", std::move(" << childVariable << "));\n";
    }
}

std::string GenerateCpp(const Document& document) {
    std::ostringstream output;
    output << "#include \"Runtime/HostLoop.h\"\n\n";
    output << "namespace {\n\n";
    output << "std::unique_ptr<RoboWeb::Node> MakeElement(int kind, const char* tag, int line, int column) {\n";
    output << "    auto node = std::make_unique<RoboWeb::Node>();\n";
    output << "    node->type = RoboWeb::Node::Type::Element;\n";
    output << "    node->kind = static_cast<RoboWeb::ElementKind>(kind);\n";
    output << "    node->tagName = tag;\n";
    output << "    node->line = line;\n";
    output << "    node->column = column;\n";
    output << "    return node;\n}\n\n";
    output << "std::unique_ptr<RoboWeb::Node> MakeText(const char* text, int line, int column) {\n";
    output << "    auto node = std::make_unique<RoboWeb::Node>();\n";
    output << "    node->type = RoboWeb::Node::Type::Text;\n";
    output << "    node->text = text;\n";
    output << "    node->line = line;\n";
    output << "    node->column = column;\n";
    output << "    return node;\n}\n\n";
    output << "void AddAttribute(RoboWeb::Node& node, const char* name, const char* value) {\n";
    output << "    RoboWeb::Attribute attribute;\n";
    output << "    attribute.name = name;\n";
    output << "    attribute.value = value;\n";
    output << "    attribute.line = node.line;\n";
    output << "    attribute.column = node.column;\n";
    output << "    node.attributes.push_back(attribute);\n}\n\n";
    output << "void AppendChild(RoboWeb::Node& parent, std::unique_ptr<RoboWeb::Node> child) {\n";
    output << "    child->parent = &parent;\n";
    output << "    parent.children.push_back(std::move(child));\n}\n\n";
    output << "std::unique_ptr<RoboWeb::Node> BuildCompiledDocument() {\n";
    if (document.root != nullptr) {
        WriteNode(output, *document.root, "root", 4);
        output << "    return root;\n";
    } else {
        output << "    return std::make_unique<RoboWeb::Node>();\n";
    }
    output << "}\n\n}  // namespace\n\n";
    output << "int main(int argc, char** argv) {\n";
    output << "    RoboWeb::Document document;\n";
    output << "    document.sourcePath = \"" << Escape(document.sourcePath) << "\";\n";
    output << "    document.quirks = " << (document.quirks ? "true" : "false") << ";\n";
    output << "    document.root = BuildCompiledDocument();\n";
    output << "    if (document.root != nullptr) {\n";
    output << "        RoboWeb::InitializeControlState(*document.root);\n";
    output << "    }\n";
    output << "    RoboWeb::CollectDocumentInfo(document);\n";
    output << "    return RoboWeb::RunDocument(document, argc, argv);\n}\n";
    return output.str();
}

}
