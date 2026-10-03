#include "Xml/XmlParser.h"

#include "Core/Utf8.h"

#include <cctype>

namespace RoboWeb {

namespace {

void RecordStylesheet(const std::string& target, const std::string& data, std::vector<std::pair<std::string, std::string>>& stylesheets) {
    if (target != "xml-stylesheet") {
        return;
    }
    auto take = [&](const char* key) {
        const std::string token = std::string(key) + "=\"";
        const std::size_t at = data.find(token);
        if (at == std::string::npos) {
            return std::string();
        }
        const std::size_t start = at + token.size();
        const std::size_t end = data.find('"', start);
        return end == std::string::npos ? std::string() : data.substr(start, end - start);
    };
    stylesheets.push_back({take("type"), take("href")});
}

struct Reader {
    const std::string& source;
    std::string path;
    Diagnostics& diagnostics;
    std::size_t index = 0;
    int line = 1;
    int column = 1;
    bool failed = false;
    std::map<std::string, std::string> entities = {{"lt", "<"}, {"gt", ">"}, {"amp", "&"}, {"apos", "'"}, {"quot", "\""}};
    std::map<std::string, std::string> unparsed;
    std::vector<std::pair<std::string, std::string>> idAttributes;
    std::vector<std::pair<std::string, std::string>> stylesheets;
    std::string version = "1.0";
    std::string encoding = "UTF-8";

    Reader(const std::string& sourceText, const std::string& sourcePath, Diagnostics& diagnosticsOut)
        : source(sourceText), path(sourcePath), diagnostics(diagnosticsOut) {}

    char Current() const {
        return index < source.size() ? source[index] : '\0';
    }

    void Advance() {
        if (index >= source.size()) {
            return;
        }
        if (source[index] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
        ++index;
    }

    void Fail(const std::string& message) {
        if (!failed) {
            diagnostics.Error(path, line, column, message);
        }
        failed = true;
    }

    void SkipSpace() {
        while (Current() == ' ' || Current() == '\t' || Current() == '\n' || Current() == '\r') {
            Advance();
        }
    }

    bool StartsWith(const char* text) const {
        for (std::size_t cursor = 0; text[cursor] != '\0'; ++cursor) {
            if (index + cursor >= source.size() || source[index + cursor] != text[cursor]) {
                return false;
            }
        }
        return true;
    }

    bool NameStart(char character) const {
        return std::isalpha(static_cast<unsigned char>(character)) || character == '_' || character == ':';
    }

    bool NameChar(char character) const {
        return NameStart(character) || std::isdigit(static_cast<unsigned char>(character)) || character == '-' || character == '.';
    }

    std::string ReadName() {
        if (!NameStart(Current())) {
            Fail("expected a name");
            return "";
        }
        std::string name;
        while (NameChar(Current())) {
            name.push_back(Current());
            Advance();
        }
        return name;
    }

    std::string ReadUntil(const char* marker) {
        std::string text;
        const std::size_t length = std::char_traits<char>::length(marker);
        while (index < source.size()) {
            if (source.compare(index, length, marker) == 0) {
                return text;
            }
            text.push_back(Current());
            Advance();
        }
        Fail(std::string("missing ") + marker);
        return text;
    }

    void AppendReference(std::string& output) {
        Advance();
        if (Current() == '#') {
            Advance();
            int code = 0;
            if (Current() == 'x' || Current() == 'X') {
                Advance();
                if (!std::isxdigit(static_cast<unsigned char>(Current()))) {
                    Fail("bad character reference");
                    return;
                }
                while (std::isxdigit(static_cast<unsigned char>(Current()))) {
                    const char digit = Current();
                    code *= 16;
                    if (digit >= '0' && digit <= '9') {
                        code += digit - '0';
                    } else if (digit >= 'a' && digit <= 'f') {
                        code += digit - 'a' + 10;
                    } else {
                        code += digit - 'A' + 10;
                    }
                    Advance();
                }
            } else {
                if (!std::isdigit(static_cast<unsigned char>(Current()))) {
                    Fail("bad character reference");
                    return;
                }
                while (std::isdigit(static_cast<unsigned char>(Current()))) {
                    code = code * 10 + (Current() - '0');
                    Advance();
                }
            }
            if (Current() != ';') {
                Fail("character reference is missing ';'");
                return;
            }
            Advance();
            if (code <= 0) {
                Fail("character reference is empty");
                return;
            }
            AppendUtf8(output, static_cast<char32_t>(code));
            return;
        }
        const std::string name = ReadName();
        if (Current() != ';') {
            Fail("entity reference is missing ';'");
            return;
        }
        Advance();
        const auto found = entities.find(name);
        if (found == entities.end()) {
            if (unparsed.find(name) != unparsed.end()) {
                Fail("unparsed entity '" + name + "' cannot be expanded");
                return;
            }
            Fail("unknown entity '" + name + "'");
            return;
        }
        output += found->second;
    }

    std::string ReadQuoted() {
        const char quote = Current();
        if (quote != '"' && quote != '\'') {
            Fail("attribute value must be quoted");
            return "";
        }
        Advance();
        std::string value;
        while (Current() != '\0' && Current() != quote) {
            if (Current() == '<') {
                Fail("attribute value contains '<'");
                return value;
            }
            if (Current() == '&') {
                AppendReference(value);
            } else {
                value.push_back(Current());
                Advance();
            }
        }
        if (Current() != quote) {
            Fail("unterminated attribute value");
            return value;
        }
        Advance();
        return value;
    }

    void ReadAttributes(Node& element) {
        while (!failed) {
            SkipSpace();
            if (Current() == '>' || Current() == '/' || Current() == '\0') {
                break;
            }
            const std::string name = ReadName();
            SkipSpace();
            if (Current() != '=') {
                Fail("attribute '" + name + "' is missing a value");
                return;
            }
            Advance();
            SkipSpace();
            const std::string value = ReadQuoted();
            for (const Attribute& attribute : element.attributes) {
                if (attribute.name == name) {
                    Fail("duplicate attribute '" + name + "'");
                    return;
                }
            }
            Attribute attribute;
            attribute.name = name;
            attribute.value = value;
            attribute.line = line;
            attribute.column = column;
            element.attributes.push_back(attribute);
        }
    }

    void ParseDoctype() {
        ReadUntil("[");
        if (failed) {
            return;
        }
        Advance();
        while (!failed && Current() != '\0' && Current() != ']') {
            SkipSpace();
            if (Current() == ']') {
                break;
            }
            if (StartsWith("<!ENTITY")) {
                for (int count = 0; count < 8; ++count) {
                    Advance();
                }
                SkipSpace();
                const bool parameter = Current() == '%';
                if (parameter) {
                    Advance();
                    SkipSpace();
                }
                const std::string name = ReadName();
                SkipSpace();
                if (Current() == '"' || Current() == '\'') {
                    const std::string value = ReadQuoted();
                    if (!parameter) {
                        entities[name] = value;
                    }
                } else if (StartsWith("SYSTEM") || StartsWith("PUBLIC")) {
                    const bool publicId = StartsWith("PUBLIC");
                    while (std::isalpha(static_cast<unsigned char>(Current()))) {
                        Advance();
                    }
                    SkipSpace();
                    if (publicId) {
                        ReadQuoted();
                        SkipSpace();
                    }
                    const std::string systemId = ReadQuoted();
                    SkipSpace();
                    if (StartsWith("NDATA")) {
                        for (int count = 0; count < 5; ++count) {
                            Advance();
                        }
                        SkipSpace();
                        ReadName();
                        unparsed[name] = systemId;
                    } else {
                        Fail("external entity '" + name + "' is not expanded");
                    }
                } else {
                    Fail("unsupported entity declaration");
                }
                SkipSpace();
                if (Current() == '>') {
                    Advance();
                }
                continue;
            }
            if (StartsWith("<!ATTLIST")) {
                for (int count = 0; count < 9; ++count) {
                    Advance();
                }
                SkipSpace();
                const std::string element = ReadName();
                SkipSpace();
                const std::string attribute = ReadName();
                SkipSpace();
                const std::string type = ReadName();
                if (type == "ID") {
                    idAttributes.push_back({element, attribute});
                }
                ReadUntil(">");
                if (Current() == '>') {
                    Advance();
                }
                continue;
            }
            if (Current() == '<') {
                ReadUntil(">");
                if (Current() == '>') {
                    Advance();
                }
                continue;
            }
            Fail("bad document type declaration");
            return;
        }
        ReadUntil(">");
        if (Current() == '>') {
            Advance();
        }
    }

    std::unique_ptr<Node> ParseElement() {
        Advance();
        auto element = std::make_unique<Node>();
        element->type = Node::Type::Element;
        element->kind = ElementKind::Unknown;
        element->line = line;
        element->column = column;
        element->tagName = ReadName();
        ReadAttributes(*element);
        SkipSpace();
        if (Current() == '/') {
            Advance();
            if (Current() != '>') {
                Fail("empty element is missing '>'");
            } else {
                Advance();
            }
            return element;
        }
        if (Current() != '>') {
            Fail("element '" + element->tagName + "' is missing '>'");
            return element;
        }
        Advance();
        while (!failed && index < source.size()) {
            if (StartsWith("</")) {
                break;
            }
            if (StartsWith("<!--")) {
                for (int count = 0; count < 4; ++count) {
                    Advance();
                }
                const std::string body = ReadUntil("-->");
                if (body.find("--") != std::string::npos) {
                    Fail("comment contains '--'");
                }
                if (StartsWith("-->")) {
                    Advance();
                    Advance();
                    Advance();
                }
                auto comment = std::make_unique<Node>();
                comment->type = Node::Type::Comment;
                comment->text = body;
                comment->parent = element.get();
                element->children.push_back(std::move(comment));
                continue;
            }
            if (StartsWith("<![CDATA[")) {
                for (int count = 0; count < 9; ++count) {
                    Advance();
                }
                const std::string body = ReadUntil("]]>");
                if (StartsWith("]]>")) {
                    Advance();
                    Advance();
                    Advance();
                }
                auto text = std::make_unique<Node>();
                text->type = Node::Type::Text;
                text->text = body;
                text->parent = element.get();
                element->children.push_back(std::move(text));
                continue;
            }
            if (StartsWith("<?")) {
                Advance();
                Advance();
                const std::string target = ReadName();
                std::string data;
                if (Current() == ' ' || Current() == '\t' || Current() == '\n' || Current() == '\r') {
                    Advance();
                    data = ReadUntil("?>");
                }
                if (StartsWith("?>")) {
                    Advance();
                    Advance();
                } else {
                    Fail("processing instruction is missing '?>'");
                }
                RecordStylesheet(target, data, stylesheets);
                auto instruction = std::make_unique<Node>();
                instruction->type = Node::Type::ProcessingInstruction;
                instruction->tagName = target;
                instruction->text = data;
                instruction->parent = element.get();
                element->children.push_back(std::move(instruction));
                continue;
            }
            if (Current() == '<') {
                auto child = ParseElement();
                child->parent = element.get();
                element->children.push_back(std::move(child));
                continue;
            }
            std::string text;
            while (Current() != '\0' && Current() != '<') {
                if (Current() == '&') {
                    AppendReference(text);
                } else {
                    text.push_back(Current());
                    Advance();
                }
            }
            if (!text.empty()) {
                auto textNode = std::make_unique<Node>();
                textNode->type = Node::Type::Text;
                textNode->text = text;
                textNode->parent = element.get();
                element->children.push_back(std::move(textNode));
            }
        }
        if (!StartsWith("</")) {
            Fail("element '" + element->tagName + "' is not closed");
            return element;
        }
        Advance();
        Advance();
        const std::string endName = ReadName();
        if (endName != element->tagName) {
            Fail("end tag '" + endName + "' does not match '" + element->tagName + "'");
        }
        SkipSpace();
        if (Current() != '>') {
            Fail("end tag is missing '>'");
        } else {
            Advance();
        }
        return element;
    }
};

void EscapeText(std::string& output, const std::string& text, bool attribute) {
    for (char character : text) {
        if (character == '&') {
            output += "&amp;";
        } else if (character == '<') {
            output += "&lt;";
        } else if (character == '>' && !attribute) {
            output += "&gt;";
        } else if (attribute && character == '"') {
            output += "&quot;";
        } else {
            output.push_back(character);
        }
    }
}

void WriteNode(std::string& output, const Node& node) {
    if (node.type == Node::Type::Document) {
        for (const std::unique_ptr<Node>& child : node.children) {
            WriteNode(output, *child);
        }
        return;
    }
    if (node.type == Node::Type::Text) {
        EscapeText(output, node.text, false);
        return;
    }
    if (node.type == Node::Type::Comment) {
        output += "<!--";
        output += node.text;
        output += "-->";
        return;
    }
    if (node.type == Node::Type::ProcessingInstruction) {
        output += "<?";
        output += node.tagName;
        if (!node.text.empty()) {
            output += " ";
            output += node.text;
        }
        output += "?>";
        return;
    }
    output += "<";
    output += node.tagName;
    for (const Attribute& attribute : node.attributes) {
        output += " ";
        output += attribute.name;
        output += "=\"";
        EscapeText(output, attribute.value, true);
        output += "\"";
    }
    if (node.children.empty()) {
        output += "/>";
        return;
    }
    output += ">";
    for (const std::unique_ptr<Node>& child : node.children) {
        WriteNode(output, *child);
    }
    output += "</";
    output += node.tagName;
    output += ">";
}

void WriteText(std::string& output, const Node& node) {
    if (node.type == Node::Type::Text) {
        output += node.text;
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        WriteText(output, *child);
    }
}

}  // namespace

XmlParseResult ParseXml(const std::string& source, const std::string& path, Diagnostics& diagnostics) {
    XmlParseResult result;
    Reader reader(source, path, diagnostics);
    if (reader.StartsWith("\xEF\xBB\xBF")) {
        reader.Advance();
        reader.Advance();
        reader.Advance();
    }
    reader.SkipSpace();
    if (reader.StartsWith("<?xml") && reader.source.size() > reader.index + 5 &&
        (reader.source[reader.index + 5] == ' ' || reader.source[reader.index + 5] == '\t' || reader.source[reader.index + 5] == '\n' ||
            reader.source[reader.index + 5] == '\r' || reader.source[reader.index + 5] == '?')) {
        reader.Advance();
        reader.Advance();
        reader.Advance();
        reader.Advance();
        reader.Advance();
        reader.SkipSpace();
        while (reader.Current() != '?' && reader.Current() != '\0') {
            const std::string name = reader.ReadName();
            reader.SkipSpace();
            if (reader.Current() == '=') {
                reader.Advance();
                reader.SkipSpace();
                const std::string value = reader.ReadQuoted();
                if (name == "version") {
                    reader.version = value;
                } else if (name == "encoding") {
                    reader.encoding = value;
                }
            }
            reader.SkipSpace();
        }
        if (!reader.StartsWith("?>")) {
            reader.Fail("xml declaration is missing '?>'");
        } else {
            reader.Advance();
            reader.Advance();
        }
        if (reader.encoding != "UTF-8" && reader.encoding != "utf-8") {
            reader.Fail("encoding '" + reader.encoding + "' is not UTF-8");
        }
    }
    int roots = 0;
    auto document = std::make_unique<Node>();
    document->type = Node::Type::Document;
    while (!reader.failed && reader.index < source.size()) {
        reader.SkipSpace();
        if (reader.index >= source.size()) {
            break;
        }
        if (reader.StartsWith("<!--")) {
            for (int count = 0; count < 4; ++count) {
                reader.Advance();
            }
            const std::string body = reader.ReadUntil("-->");
            if (reader.StartsWith("-->")) {
                reader.Advance();
                reader.Advance();
                reader.Advance();
            }
            auto comment = std::make_unique<Node>();
            comment->type = Node::Type::Comment;
            comment->text = body;
            comment->parent = document.get();
            document->children.push_back(std::move(comment));
            continue;
        }
        if (reader.StartsWith("<?")) {
            reader.Advance();
            reader.Advance();
            const std::string target = reader.ReadName();
            std::string data;
            if (reader.Current() != '?') {
                reader.SkipSpace();
                data = reader.ReadUntil("?>");
            }
            if (reader.StartsWith("?>")) {
                reader.Advance();
                reader.Advance();
            }
            RecordStylesheet(target, data, reader.stylesheets);
            auto instruction = std::make_unique<Node>();
            instruction->type = Node::Type::ProcessingInstruction;
            instruction->tagName = target;
            instruction->text = data;
            instruction->parent = document.get();
            document->children.push_back(std::move(instruction));
            continue;
        }
        if (reader.StartsWith("<!DOCTYPE")) {
            reader.ParseDoctype();
            continue;
        }
        if (reader.Current() == '<') {
            ++roots;
            if (roots > 1) {
                reader.Fail("document has more than one root element");
                break;
            }
            auto root = reader.ParseElement();
            root->parent = document.get();
            document->children.push_back(std::move(root));
            continue;
        }
        reader.Fail("unexpected character in document");
        break;
    }
    if (roots == 0 && !reader.failed) {
        reader.Fail("document has no root element");
    }
    result.version = reader.version;
    result.encoding = reader.encoding;
    result.idAttributes = reader.idAttributes;
    result.generalEntities = reader.entities;
    result.unparsedEntities = reader.unparsed;
    result.stylesheetLinks = reader.stylesheets;
    result.wellFormed = !reader.failed;
    if (!result.wellFormed) {
        auto error = std::make_unique<Node>();
        error->type = Node::Type::Element;
        error->tagName = "parsererror";
        error->text = "xml is not well-formed";
        auto replacement = std::make_unique<Node>();
        replacement->type = Node::Type::Document;
        error->parent = replacement.get();
        replacement->children.push_back(std::move(error));
        result.document.root = std::move(replacement);
        return result;
    }
    result.document.root = std::move(document);
    return result;
}

std::string SerializeXml(const Node& node) {
    std::string output;
    WriteNode(output, node);
    return output;
}

std::string SerializeText(const Node& node) {
    std::string output;
    WriteText(output, node);
    return output;
}

}
