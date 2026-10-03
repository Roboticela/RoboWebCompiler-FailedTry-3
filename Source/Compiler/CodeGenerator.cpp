#include "Compiler/CodeGenerator.h"

#include "Core/Utf8.h"

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

void WriteFrozenTables(std::ostringstream& output, const FrozenDocument& frozen) {
    output << "static const RoboWeb::StaticAttribute kAttributes[] = {\n";
    for (const OwnedAttribute& attribute : frozen.attributes) {
        output << "    {\"" << Escape(attribute.name) << "\", \"" << Escape(attribute.value) << "\"},\n";
    }
    output << "};\n\n";
    output << "static const int kChildren[] = {";
    for (std::size_t index = 0; index < frozen.children.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << frozen.children[index];
    }
    output << "};\n\n";
    output << "static const RoboWeb::StaticNode kNodes[] = {\n";
    for (const OwnedNode& node : frozen.nodes) {
        output << "    {" << node.type << ", " << node.elementKind << ", \"" << Escape(node.tagName) << "\", \"" << Escape(node.text)
               << "\", " << node.attributeBegin << ", " << node.attributeCount << ", " << node.childBegin << ", " << node.childCount
               << ", " << node.line << ", " << node.column << "},\n";
    }
    output << "};\n\n";
    output << "static const RoboWeb::StaticDocument kCompiledDocument = {kNodes, " << frozen.nodes.size() << ", kChildren, "
           << frozen.children.size() << ", kAttributes, " << frozen.attributes.size() << ", \"" << Escape(frozen.sourcePath) << "\"};\n\n";
}

std::string TrimCopy(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\n' || text[begin] == '\r')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' || text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool ReadCppString(const std::string& text, std::size_t& index, std::string& value) {
    while (index < text.size() && (text[index] == ' ' || text[index] == '\t')) {
        ++index;
    }
    if (index >= text.size() || (text[index] != '"' && text[index] != '\'')) {
        return false;
    }
    const char quote = text[index];
    ++index;
    value.clear();
    while (index < text.size() && text[index] != quote) {
        if (text[index] == '\\' && index + 1 < text.size()) {
            ++index;
        }
        value.push_back(text[index]);
        ++index;
    }
    if (index >= text.size() || text[index] != quote) {
        return false;
    }
    ++index;
    return true;
}

bool LowerStatement(const std::string& statement, std::ostringstream& body) {
    const std::string text = TrimCopy(statement);
    if (text.empty() || text[0] == '/' || text.rfind("function", 0) == 0 || text.rfind("var ", 0) == 0 || text.rfind("let ", 0) == 0 ||
        text.rfind("const ", 0) == 0) {
        return true;
    }
    if (text.rfind("alert(", 0) == 0) {
        std::size_t index = 6;
        std::string message;
        if (!ReadCppString(text, index, message)) {
            return false;
        }
        body << "    document.status = \"" << Escape(message) << "\";\n";
        return true;
    }
    if (text.rfind("document.title", 0) == 0) {
        const std::size_t equal = text.find('=');
        if (equal == std::string::npos) {
            return false;
        }
        std::size_t index = equal + 1;
        std::string title;
        if (!ReadCppString(text, index, title)) {
            return false;
        }
        body << "    document.title = \"" << Escape(title) << "\";\n";
        return true;
    }
    if (text.rfind("document.getElementById(", 0) != 0) {
        return false;
    }
    std::size_t index = std::string("document.getElementById(").size();
    std::string id;
    if (!ReadCppString(text, index, id)) {
        return false;
    }
    while (index < text.size() && (text[index] == ' ' || text[index] == ')')) {
        ++index;
    }
    if (index >= text.size() || text[index] != '.') {
        return false;
    }
    const std::string rest = text.substr(index + 1);
    const std::size_t equal = rest.find('=');
    if (equal == std::string::npos) {
        return false;
    }
    const std::string property = TrimCopy(rest.substr(0, equal));
    std::string rawValue = TrimCopy(rest.substr(equal + 1));
    body << "    if (document.root != nullptr) {\n";
    body << "        if (RoboWeb::Node* target = RoboWeb::FindElementById(*document.root, \"" << Escape(id) << "\")) {\n";
    if (property == "textContent" || property == "innerText") {
        std::size_t cursor = 0;
        std::string value;
        if (!ReadCppString(rawValue, cursor, value)) {
            return false;
        }
        body << "            target->children.clear();\n";
        body << "            target->liveValue = \"" << Escape(value) << "\";\n";
        body << "            target->scriptText = true;\n";
        body << "            auto child = std::make_unique<RoboWeb::Node>();\n";
        body << "            child->type = RoboWeb::Node::Type::Text;\n";
        body << "            child->text = \"" << Escape(value) << "\";\n";
        body << "            child->parent = target;\n";
        body << "            target->children.push_back(std::move(child));\n";
    } else if (property == "hidden") {
        const bool hidden = rawValue != "false" && rawValue != "0";
        body << "            target->hiddenByScript = " << (hidden ? "true" : "false") << ";\n";
    } else {
        return false;
    }
    body << "        }\n";
    body << "    }\n";
    return true;
}

bool LowerSource(const std::string& source, std::ostringstream& body) {
    std::size_t start = 0;
    for (std::size_t index = 0; index <= source.size(); ++index) {
        if (index != source.size() && source[index] != ';') {
            continue;
        }
        if (!LowerStatement(source.substr(start, index - start), body)) {
            return false;
        }
        start = index + 1;
    }
    return true;
}

bool WriteCompiledScript(std::ostringstream& output, const Document& document) {
    if (document.root == nullptr) {
        return false;
    }
    std::ostringstream body;
    bool any = false;
    std::vector<const Node*> stack{document.root.get()};
    while (!stack.empty()) {
        const Node* node = stack.back();
        stack.pop_back();
        if (node->kind == ElementKind::Script) {
            const std::string type = ToLowerAscii(AttributeValue(*node, "type"));
            if (type.empty() || type == "text/javascript" || type == "application/javascript") {
                const std::string source = GatherText(*node);
                if (!TrimCopy(source).empty()) {
                    any = true;
                    if (!LowerSource(source, body)) {
                        return false;
                    }
                }
            }
        }
        for (const std::unique_ptr<Node>& child : node->children) {
            stack.push_back(child.get());
        }
    }
    if (!any) {
        return false;
    }
    output << "void RunCompiledPage(RoboWeb::Document& document) {\n";
    output << body.str();
    output << "}\n\n";
    return true;
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

void WriteCompiledStyles(
    std::ostringstream& output, const Document& document, const std::vector<CompiledWebFont>& fonts, const std::vector<CompiledImage>& images) {
    for (std::size_t index = 0; index < fonts.size(); ++index) {
        output << "static const unsigned char kCompiledFont" << index << "[] = ";
        WriteFontBytes(output, fonts[index].bytes);
        output << ";\n\n";
    }
    for (std::size_t index = 0; index < images.size(); ++index) {
        output << "static const unsigned char kCompiledImage" << index << "[] = ";
        WriteFontBytes(output, images[index].bytes);
        output << ";\n\n";
    }
    output << "void InstallCompiledStyles(RoboWeb::Document& document) {\n";
    output << "    document.stylesLoaded = true;\n";
    for (std::size_t index = 0; index < fonts.size(); ++index) {
        const CompiledWebFont& font = fonts[index];
        output << "    RoboWeb::RegisterWebFontView(\"" << Escape(font.family) << "\", " << font.weight << ", "
               << (font.italic ? "true" : "false") << ", kCompiledFont" << index << ", sizeof(kCompiledFont" << index << "));\n";
    }
    for (std::size_t index = 0; index < images.size(); ++index) {
        output << "    RoboWeb::RegisterCompiledImageView(\"" << Escape(images[index].url) << "\", kCompiledImage" << index
               << ", sizeof(kCompiledImage" << index << "));\n";
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
        for (const StyleDeclaration& declaration : rule.declarations) {
            output << "        rule.declarations.push_back(RoboWeb::StyleDeclaration{\"" << Escape(declaration.name) << "\", \""
                   << Escape(declaration.value) << "\"});\n";
        }
        output << "        document.styleRules.push_back(rule);\n";
        output << "    }\n";
    }
    if (!document.status.empty()) {
        output << "    document.status = \"" << Escape(document.status) << "\";\n";
    }
    output << "}\n\n";
}

std::string GenerateCpp(const Document& document, const std::vector<CompiledWebFont>& fonts, const std::vector<CompiledImage>& images) {
    std::ostringstream output;
    output << "// Performance contract: hover, caret blink, and scroll repaint cached draw records.\n";
    output << "// LayoutDocument runs for the first frame and for a viewport or geometry change.\n";
    output << "#include \"Runtime/HostLoop.h\"\n";
    output << "#include \"Render/BitmapFont.h\"\n";
    output << "#include \"Render/ImageDecoder.h\"\n\n";
    output << "#include <memory>\n";
    output << "#include <vector>\n\n";
    WriteCompiledStyles(output, document, fonts, images);
    output << "namespace {\n\n";
    WriteFrozenTables(output, Freeze(document));
    const bool compiledScript = WriteCompiledScript(output, document);
    output << "}  // namespace\n\n";
    output << "int main(int argc, char** argv) {\n";
    output << "    RoboWeb::Document document;\n";
    output << "    document.sourcePath = \"" << Escape(document.sourcePath) << "\";\n";
    output << "    document.quirks = " << (document.quirks ? "true" : "false") << ";\n";
    output << "    document.root = RoboWeb::BuildFromStatic(kCompiledDocument);\n";
    if (compiledScript) {
        output << "    document.compiledScript = RunCompiledPage;\n";
    }
    output << "    RoboWeb::CollectDocumentInfo(document);\n";
    output << "    InstallCompiledStyles(document);\n";
    output << "    return RoboWeb::RunDocument(document, argc, argv);\n}\n";
    return output.str();
}

}
