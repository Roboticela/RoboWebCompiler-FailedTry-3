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

void WriteFontBytes(std::ostringstream& output, const std::vector<unsigned char>& bytes) {
    output << "{\n";
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index % 16 == 0) {
            output << "    ";
        }
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "0x%02x", bytes[index]);
        output << buffer;
        if (index + 1 != bytes.size()) {
            output << ",";
        }
        if (index % 16 == 15 || index + 1 == bytes.size()) {
            output << "\n";
        } else {
            output << " ";
        }
    }
    output << "}";
}

void WriteCompiledStyles(std::ostringstream& output, const Document& document, const std::vector<CompiledWebFont>& fonts) {
    for (std::size_t index = 0; index < fonts.size(); ++index) {
        output << "static const unsigned char kCompiledFont" << index << "[] = ";
        WriteFontBytes(output, fonts[index].bytes);
        output << ";\n\n";
    }
    output << "void InstallCompiledStyles(RoboWeb::Document& document) {\n";
    output << "    document.stylesLoaded = true;\n";
    for (std::size_t index = 0; index < fonts.size(); ++index) {
        const CompiledWebFont& font = fonts[index];
        output << "    RoboWeb::RegisterWebFont(\"" << Escape(font.family) << "\", " << font.weight << ", "
               << (font.italic ? "true" : "false") << ", std::vector<unsigned char>(kCompiledFont" << index
               << ", kCompiledFont" << index << " + sizeof(kCompiledFont" << index << ")));\n";
    }
    for (const ElementStyle& rule : document.styleRules) {
        output << "    {\n";
        output << "        RoboWeb::ElementStyle rule;\n";
        output << "        rule.selector = \"" << Escape(rule.selector) << "\";\n";
        if (!rule.fontFamily.empty()) {
            output << "        rule.fontFamily = \"" << Escape(rule.fontFamily) << "\";\n";
        }
        if (rule.fontWeight >= 0) {
            output << "        rule.fontWeight = " << rule.fontWeight << ";\n";
        }
        if (rule.fontStyle >= 0) {
            output << "        rule.fontStyle = " << rule.fontStyle << ";\n";
        }
        if (rule.fontSizePx > 0) {
            output << "        rule.fontSizePx = " << rule.fontSizePx << ";\n";
        }
        if (rule.hasColor) {
            output << "        rule.hasColor = true;\n";
            output << "        rule.colorRed = " << static_cast<int>(rule.colorRed) << ";\n";
            output << "        rule.colorGreen = " << static_cast<int>(rule.colorGreen) << ";\n";
            output << "        rule.colorBlue = " << static_cast<int>(rule.colorBlue) << ";\n";
        }
        if (rule.hasBackground) {
            output << "        rule.hasBackground = true;\n";
            output << "        rule.backgroundRed = " << static_cast<int>(rule.backgroundRed) << ";\n";
            output << "        rule.backgroundGreen = " << static_cast<int>(rule.backgroundGreen) << ";\n";
            output << "        rule.backgroundBlue = " << static_cast<int>(rule.backgroundBlue) << ";\n";
        }
        if (rule.displayKind >= 0) {
            output << "        rule.displayKind = " << rule.displayKind << ";\n";
        }
        if (rule.widthPx >= 0) {
            output << "        rule.widthPx = " << rule.widthPx << ";\n";
        }
        if (rule.heightPx >= 0) {
            output << "        rule.heightPx = " << rule.heightPx << ";\n";
        }
        if (rule.marginTopPx >= 0) {
            output << "        rule.marginTopPx = " << rule.marginTopPx << ";\n";
        }
        if (rule.marginRightPx >= 0) {
            output << "        rule.marginRightPx = " << rule.marginRightPx << ";\n";
        }
        if (rule.marginBottomPx >= 0) {
            output << "        rule.marginBottomPx = " << rule.marginBottomPx << ";\n";
        }
        if (rule.marginLeftPx >= 0) {
            output << "        rule.marginLeftPx = " << rule.marginLeftPx << ";\n";
        }
        if (rule.paddingTopPx >= 0) {
            output << "        rule.paddingTopPx = " << rule.paddingTopPx << ";\n";
        }
        if (rule.paddingRightPx >= 0) {
            output << "        rule.paddingRightPx = " << rule.paddingRightPx << ";\n";
        }
        if (rule.paddingBottomPx >= 0) {
            output << "        rule.paddingBottomPx = " << rule.paddingBottomPx << ";\n";
        }
        if (rule.paddingLeftPx >= 0) {
            output << "        rule.paddingLeftPx = " << rule.paddingLeftPx << ";\n";
        }
        if (rule.textAlign >= 0) {
            output << "        rule.textAlign = " << rule.textAlign << ";\n";
        }
        if (rule.decoration >= 0) {
            output << "        rule.decoration = " << rule.decoration << ";\n";
        }
        if (rule.whiteSpace >= 0) {
            output << "        rule.whiteSpace = " << rule.whiteSpace << ";\n";
        }
        if (rule.borderPx >= 0) {
            output << "        rule.borderPx = " << rule.borderPx << ";\n";
        }
        output << "        document.styleRules.push_back(rule);\n";
        output << "    }\n";
    }
    if (!document.status.empty()) {
        output << "    document.status = \"" << Escape(document.status) << "\";\n";
    }
    output << "}\n\n";
}

std::string GenerateCpp(const Document& document, const std::vector<CompiledWebFont>& fonts) {
    std::ostringstream output;
    output << "#include \"Runtime/HostLoop.h\"\n";
    output << "#include \"Render/BitmapFont.h\"\n\n";
    output << "#include <vector>\n\n";
    WriteCompiledStyles(output, document, fonts);
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
    output << "    InstallCompiledStyles(document);\n";
    output << "    return RoboWeb::RunDocument(document, argc, argv);\n}\n";
    return output.str();
}

}
