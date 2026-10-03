#include "Runtime/MiniScript.h"

#include "Core/Utf8.h"
#include "Render/StyleSheet.h"

#include <memory>
#include <vector>

namespace RoboWeb {

namespace {

std::string Trim(const std::string& text) {
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

bool ReadString(const std::string& text, std::size_t& index, std::string& value) {
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

void ReplaceText(Node& node, const std::string& text) {
    node.children.clear();
    node.liveValue = text;
    node.scriptText = true;
    auto child = std::make_unique<Node>();
    child->type = Node::Type::Text;
    child->text = text;
    child->parent = &node;
    node.children.push_back(std::move(child));
}

void SetHidden(Node& node, bool hidden) {
    node.hiddenByScript = hidden;
    if (!hidden) {
        std::vector<Attribute> kept;
        for (const Attribute& attribute : node.attributes) {
            if (ToLowerAscii(attribute.name) != "hidden") {
                kept.push_back(attribute);
            }
        }
        node.attributes.swap(kept);
    }
}

bool ReadDataset(const Node& node, const std::string& expression, std::string& value) {
    std::string text = Trim(expression);
    if (text.rfind("this.", 0) == 0) {
        text = text.substr(5);
    }
    if (text.rfind("dataset.", 0) != 0) {
        return false;
    }
    const std::string key = text.substr(std::string("dataset.").size());
    if (key.empty()) {
        return false;
    }
    for (const Attribute& attribute : node.attributes) {
        const std::string attributeName = ToLowerAscii(attribute.name);
        if (attributeName.rfind("data-", 0) != 0 || attributeName.size() < 6) {
            continue;
        }
        const std::string suffix = attributeName.substr(5);
        if (suffix.rfind("xml", 0) == 0) {
            continue;
        }
        std::string camel;
        bool capitalize = false;
        for (char character : suffix) {
            if (character == '-') {
                capitalize = true;
                continue;
            }
            if (capitalize && character >= 'a' && character <= 'z') {
                camel.push_back(static_cast<char>(character - 'a' + 'A'));
                capitalize = false;
            } else {
                camel.push_back(character);
                capitalize = false;
            }
        }
        if (camel == key) {
            value = attribute.value;
            return true;
        }
    }
    return false;
}

bool AssignProperty(Document& document, Node& node, const std::string& property, const std::string& rawValue) {
    (void)document;
    const std::string name = ToLowerAscii(Trim(property));
    std::size_t index = 0;
    std::string valueText = Trim(rawValue);
    std::string datasetValue;
    if (ReadDataset(node, valueText, datasetValue)) {
        valueText = "\"" + datasetValue + "\"";
    }
    if (name == "textcontent" || name == "innertext") {
        std::string value;
        std::size_t cursor = 0;
        if (!ReadString(valueText, cursor, value)) {
            return false;
        }
        ReplaceText(node, value);
        return true;
    }
    if (name == "hidden") {
        const std::string flag = ToLowerAscii(valueText);
        SetHidden(node, flag != "false" && flag != "0");
        return true;
    }
    if (name == "style.color" || name == "style.backgroundcolor" || name == "style.background") {
        std::string value;
        std::size_t cursor = 0;
        if (!ReadString(valueText, cursor, value)) {
            value = valueText;
        }
        std::string style = AttributeValue(node, "style");
        if (!style.empty() && style.back() != ';') {
            style.push_back(';');
        }
        if (name == "style.color") {
            style += "color:" + value;
        } else {
            style += "background-color:" + value;
        }
        bool found = false;
        for (Attribute& attribute : node.attributes) {
            if (ToLowerAscii(attribute.name) == "style") {
                attribute.value = style;
                found = true;
            }
        }
        if (!found) {
            Attribute attribute;
            attribute.name = "style";
            attribute.value = style;
            node.attributes.push_back(attribute);
        }
        return true;
    }
    (void)index;
    return false;
}

Node* DocumentRoot(Document& document) {
    return document.root.get();
}

bool RunStatement(Document& document, Node* self, const std::string& statement) {
    const std::string text = Trim(statement);
    if (text.empty() || text[0] == '/' || text.rfind("function", 0) == 0 || text.rfind("var ", 0) == 0 ||
        text.rfind("let ", 0) == 0 || text.rfind("const ", 0) == 0) {
        return true;
    }
    if (text.rfind("alert(", 0) == 0) {
        std::size_t index = 6;
        std::string message;
        if (!ReadString(text, index, message)) {
            return false;
        }
        document.status = message;
        return true;
    }
    if (text.rfind("document.title", 0) == 0) {
        const std::size_t equal = text.find('=');
        if (equal == std::string::npos) {
            return false;
        }
        std::size_t index = equal + 1;
        std::string title;
        if (!ReadString(text, index, title)) {
            return false;
        }
        document.title = title;
        return true;
    }
    Node* target = nullptr;
    std::string rest;
    if (text.rfind("this.", 0) == 0) {
        target = self;
        rest = text.substr(5);
    } else if (text.rfind("document.getElementById(", 0) == 0) {
        std::size_t index = std::string("document.getElementById(").size();
        std::string id;
        if (!ReadString(text, index, id)) {
            return false;
        }
        while (index < text.size() && (text[index] == ' ' || text[index] == ')')) {
            ++index;
        }
        if (index >= text.size() || text[index] != '.') {
            return false;
        }
        if (document.root == nullptr) {
            return false;
        }
        target = FindElementById(*DocumentRoot(document), id);
        rest = text.substr(index + 1);
    } else {
        return false;
    }
    if (target == nullptr) {
        return false;
    }
    const std::size_t equal = rest.find('=');
    if (equal == std::string::npos) {
        return false;
    }
    return AssignProperty(document, *target, rest.substr(0, equal), rest.substr(equal + 1));
}

bool RunSource(Document& document, Node* self, const std::string& source) {
    bool ran = false;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= source.size(); ++index) {
        if (index != source.size() && source[index] != ';') {
            continue;
        }
        if (RunStatement(document, self, source.substr(start, index - start))) {
            ran = true;
        }
        start = index + 1;
    }
    return ran;
}

void WalkScripts(Document& document, Node& node) {
    if (node.kind == ElementKind::Script) {
        const std::string type = ToLowerAscii(AttributeValue(node, "type"));
        if (type.empty() || type == "text/javascript" || type == "application/javascript") {
            RunSource(document, nullptr, GatherText(node));
        }
    }
    if (HasAttribute(node, "onload")) {
        RunSource(document, &node, AttributeValue(node, "onload"));
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        WalkScripts(document, *child);
    }
}

}  // namespace

bool LoadExternalScripts(Document& document, std::string& error) {
    if (document.root == nullptr) {
        return true;
    }
    std::vector<Node*> stack{document.root.get()};
    while (!stack.empty()) {
        Node* node = stack.back();
        stack.pop_back();
        if (node->kind == ElementKind::Script) {
            const std::string type = ToLowerAscii(AttributeValue(*node, "type"));
            const bool javascript = type.empty() || type == "text/javascript" || type == "application/javascript" || type == "module";
            const std::string src = AttributeValue(*node, "src");
            if (javascript && !src.empty()) {
                std::string source;
                if (!LoadTextResource(document, src, source, error)) {
                    const std::string prefix = "resource was not loaded: ";
                    if (error.rfind(prefix, 0) == 0) {
                        error = "script was not loaded: " + error.substr(prefix.size());
                    } else if (error.empty()) {
                        error = "script was not loaded: " + src;
                    }
                    return false;
                }
                node->children.clear();
                auto child = std::make_unique<Node>();
                child->type = Node::Type::Text;
                child->text = source;
                child->parent = node;
                node->children.push_back(std::move(child));
            }
        }
        for (const std::unique_ptr<Node>& child : node->children) {
            stack.push_back(child.get());
        }
    }
    return true;
}

void RunDocumentScripts(Document& document) {
    if (document.scriptsRan || document.root == nullptr) {
        document.scriptsRan = true;
        return;
    }
    document.scriptsRan = true;
    if (document.compiledScript != nullptr) {
        document.compiledScript(document);
        return;
    }
    WalkScripts(document, *document.root);
}

bool RunEvent(Document& document, Node& node, const char* eventName) {
    if (!HasAttribute(node, eventName)) {
        return false;
    }
    return RunSource(document, &node, AttributeValue(node, eventName));
}

}
