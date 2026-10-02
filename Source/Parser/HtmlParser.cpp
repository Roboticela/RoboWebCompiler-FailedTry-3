#include "Parser/HtmlParser.h"

#include "Core/Utf8.h"
#include "Parser/CharacterReference.h"

#include <cctype>

namespace RoboWeb {

namespace {

enum class TokenKind { Doctype, StartTag, EndTag, Comment, Character, End };

struct Token {
    TokenKind kind = TokenKind::End;
    std::string name;
    std::string data;
    std::vector<Attribute> attributes;
    bool forceQuirks = false;
    int line = 1;
    int column = 1;
};

enum class ScanMode { Data, RawText, RcData, PlainText };

class Tokenizer {
public:
    Tokenizer(const std::string& source, const std::string& path, Diagnostics& diagnostics)
        : source(source), path(path), diagnostics(diagnostics) {
    }

    void SetMode(ScanMode nextMode, const std::string& endTag) {
        mode = nextMode;
        rawEndTag = endTag;
    }

    Token Next() {
        if (mode != ScanMode::Data) {
            return NextRaw();
        }
        SkipToContent();
        Token token;
        token.line = line;
        token.column = column;
        if (index >= source.size()) {
            token.kind = TokenKind::End;
            return token;
        }
        if (source[index] != '<') {
            token.kind = TokenKind::Character;
            while (index < source.size() && source[index] != '<') {
                if (source[index] == '&') {
                    DecodeCharacterReference(source, index, token.data, &diagnostics, path, line, column);
                    continue;
                }
                TakeChar(&token.data);
            }
            return token;
        }
        const int tagLine = line;
        const int tagColumn = column;
        TakeChar(nullptr);
        if (index >= source.size()) {
            diagnostics.Error(path, tagLine, tagColumn, "unclosed '<' at end of file");
            Token text;
            text.kind = TokenKind::Character;
            text.data = "<";
            text.line = tagLine;
            text.column = tagColumn;
            return text;
        }
        if (source[index] == '!') {
            return NextMarkup(tagLine, tagColumn);
        }
        if (source[index] == '/') {
            return NextEndTag(tagLine, tagColumn);
        }
        if (source[index] == '?') {
            diagnostics.Warning(path, tagLine, tagColumn, "processing instruction is ignored");
            SkipUntil('>');
            Token empty;
            empty.kind = TokenKind::Comment;
            empty.line = tagLine;
            empty.column = tagColumn;
            return empty;
        }
        if (!IsNameStart(source[index])) {
            diagnostics.Warning(path, tagLine, tagColumn, "stray '<' treated as text");
            Token text;
            text.kind = TokenKind::Character;
            text.data = "<";
            text.line = tagLine;
            text.column = tagColumn;
            return text;
        }
        return NextStartTag(tagLine, tagColumn);
    }

private:
    const std::string& source;
    const std::string& path;
    Diagnostics& diagnostics;
    std::size_t index = 0;
    int line = 1;
    int column = 1;
    ScanMode mode = ScanMode::Data;
    std::string rawEndTag;

    static bool IsNameStart(char character) {
        return std::isalpha(static_cast<unsigned char>(character)) != 0;
    }

    static bool IsNameChar(char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '-' || character == ':' ||
               character == '_';
    }

    static bool IsSpace(char character) {
        return character == ' ' || character == '\t' || character == '\n' || character == '\r' || character == '\f';
    }

    void TakeChar(std::string* into) {
        if (index >= source.size()) {
            return;
        }
        const char character = source[index++];
        if (into != nullptr) {
            into->push_back(character);
        }
        if (character == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }

    void SkipUntil(char endCharacter) {
        while (index < source.size() && source[index] != endCharacter) {
            TakeChar(nullptr);
        }
        if (index < source.size()) {
            TakeChar(nullptr);
        }
    }

    void SkipToContent() {
    }

    Token NextMarkup(int tagLine, int tagColumn) {
        TakeChar(nullptr);
        if (index + 1 < source.size() && source[index] == '-' && source[index + 1] == '-') {
            TakeChar(nullptr);
            TakeChar(nullptr);
            Token comment;
            comment.kind = TokenKind::Comment;
            comment.line = tagLine;
            comment.column = tagColumn;
            bool closed = false;
            while (index < source.size()) {
                if (source[index] == '-' && index + 2 < source.size() && source[index + 1] == '-' &&
                    source[index + 2] == '>') {
                    TakeChar(nullptr);
                    TakeChar(nullptr);
                    TakeChar(nullptr);
                    closed = true;
                    break;
                }
                TakeChar(&comment.data);
            }
            if (!closed) {
                diagnostics.Error(path, tagLine, tagColumn, "unterminated comment");
            }
            return comment;
        }
        std::string word;
        while (index < source.size() && std::isalpha(static_cast<unsigned char>(source[index])) != 0) {
            TakeChar(&word);
        }
        if (EqualsAsciiIgnoreCase(word, "doctype")) {
            Token doctype;
            doctype.kind = TokenKind::Doctype;
            doctype.line = tagLine;
            doctype.column = tagColumn;
            while (index < source.size() && IsSpace(source[index])) {
                TakeChar(nullptr);
            }
            while (index < source.size() && !IsSpace(source[index]) && source[index] != '>') {
                TakeChar(&doctype.name);
            }
            std::string rest;
            while (index < source.size() && source[index] != '>') {
                TakeChar(&rest);
            }
            if (index >= source.size()) {
                diagnostics.Error(path, tagLine, tagColumn, "unterminated doctype");
            } else {
                TakeChar(nullptr);
            }
            doctype.name = ToLowerAscii(doctype.name);
            if (doctype.name != "html" || rest.find_first_not_of(" \t\r\n\f") != std::string::npos) {
                doctype.forceQuirks = true;
            }
            return doctype;
        }
        diagnostics.Warning(path, tagLine, tagColumn, "invalid markup declaration ignored");
        while (index < source.size() && source[index] != '>') {
            TakeChar(nullptr);
        }
        if (index < source.size()) {
            TakeChar(nullptr);
        } else {
            diagnostics.Error(path, tagLine, tagColumn, "unterminated markup declaration");
        }
        Token comment;
        comment.kind = TokenKind::Comment;
        comment.line = tagLine;
        comment.column = tagColumn;
        return comment;
    }

    Token NextEndTag(int tagLine, int tagColumn) {
        TakeChar(nullptr);
        Token token;
        token.kind = TokenKind::EndTag;
        token.line = tagLine;
        token.column = tagColumn;
        if (index >= source.size() || !IsNameStart(source[index])) {
            diagnostics.Error(path, tagLine, tagColumn, "invalid end tag");
            token.kind = TokenKind::Character;
            token.data = "</";
            return token;
        }
        while (index < source.size() && IsNameChar(source[index])) {
            TakeChar(&token.name);
        }
        token.name = ToLowerAscii(token.name);
        while (index < source.size() && IsSpace(source[index])) {
            TakeChar(nullptr);
        }
        if (index >= source.size() || source[index] != '>') {
            diagnostics.Error(path, tagLine, tagColumn, "end tag '" + token.name + "' is missing '>'");
            return token;
        }
        TakeChar(nullptr);
        return token;
    }

    Token NextStartTag(int tagLine, int tagColumn) {
        Token token;
        token.kind = TokenKind::StartTag;
        token.line = tagLine;
        token.column = tagColumn;
        while (index < source.size() && IsNameChar(source[index])) {
            TakeChar(&token.name);
        }
        token.name = ToLowerAscii(token.name);
        bool closed = false;
        while (index < source.size()) {
            if (IsSpace(source[index])) {
                TakeChar(nullptr);
                continue;
            }
            if (source[index] == '>') {
                TakeChar(nullptr);
                closed = true;
                break;
            }
            if (source[index] == '/' && index + 1 < source.size() && source[index + 1] == '>') {
                TakeChar(nullptr);
                TakeChar(nullptr);
                token.data = "/";
                closed = true;
                break;
            }
            if (!IsNameStart(source[index]) && source[index] != '_' && source[index] != ':') {
                diagnostics.Error(path, tagLine, tagColumn, "invalid character inside tag '" + token.name + "'");
                break;
            }
            Attribute attribute;
            attribute.line = line;
            attribute.column = column;
            while (index < source.size() && IsNameChar(source[index])) {
                TakeChar(&attribute.name);
            }
            attribute.name = ToLowerAscii(attribute.name);
            while (index < source.size() && IsSpace(source[index])) {
                TakeChar(nullptr);
            }
            if (index < source.size() && source[index] == '=') {
                TakeChar(nullptr);
                while (index < source.size() && IsSpace(source[index])) {
                    TakeChar(nullptr);
                }
                if (index < source.size() && (source[index] == '"' || source[index] == '\'')) {
                    const char quote = source[index];
                    TakeChar(nullptr);
                    std::string raw;
                    bool ended = false;
                    while (index < source.size()) {
                        if (source[index] == quote) {
                            TakeChar(nullptr);
                            ended = true;
                            break;
                        }
                        if (source[index] == '&') {
                            DecodeCharacterReference(source, index, raw, &diagnostics, path, line, column);
                        } else {
                            TakeChar(&raw);
                        }
                    }
                    if (!ended) {
                        diagnostics.Error(
                            path, attribute.line, attribute.column,
                            "unterminated attribute value for '" + attribute.name + "'");
                    }
                    attribute.value = raw;
                } else {
                    while (index < source.size() && !IsSpace(source[index]) && source[index] != '>') {
                        if (source[index] == '&') {
                            DecodeCharacterReference(source, index, attribute.value, &diagnostics, path, line, column);
                        } else {
                            TakeChar(&attribute.value);
                        }
                    }
                }
            }
            bool duplicate = false;
            for (const Attribute& existing : token.attributes) {
                if (existing.name == attribute.name) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                diagnostics.Error(
                    path, attribute.line, attribute.column, "duplicate attribute '" + attribute.name + "'");
            } else if (attribute.name.empty()) {
                diagnostics.Error(path, attribute.line, attribute.column, "attribute is missing a name");
            } else {
                token.attributes.push_back(attribute);
            }
        }
        if (!closed) {
            diagnostics.Error(path, tagLine, tagColumn, "tag '" + token.name + "' is missing '>'");
        }
        return token;
    }

    Token NextRaw() {
        Token token;
        token.kind = TokenKind::Character;
        token.line = line;
        token.column = column;
        if (mode == ScanMode::PlainText) {
            while (index < source.size()) {
                TakeChar(&token.data);
            }
            mode = ScanMode::Data;
            if (token.data.empty()) {
                token.kind = TokenKind::End;
            }
            return token;
        }
        const std::string needle = "</" + rawEndTag;
        std::string buffer;
        int bufferLine = line;
        int bufferColumn = column;
        while (index < source.size()) {
            if (source[index] == '<') {
                std::string candidate;
                std::size_t probe = index;
                int probeLine = line;
                int probeColumn = column;
                auto step = [&](char character) {
                    candidate.push_back(character);
                    if (character == '\n') {
                        ++probeLine;
                        probeColumn = 1;
                    } else {
                        ++probeColumn;
                    }
                    ++probe;
                };
                if (probe < source.size()) {
                    step(source[probe]);
                }
                if (probe < source.size() && source[probe] == '/') {
                    step(source[probe]);
                    std::string name;
                    while (probe < source.size() && IsNameChar(source[probe])) {
                        name.push_back(source[probe]);
                        step(source[probe]);
                    }
                    if (ToLowerAscii(name) == rawEndTag) {
                        while (probe < source.size() && IsSpace(source[probe])) {
                            step(source[probe]);
                        }
                        if (probe < source.size() && source[probe] == '>') {
                            step(source[probe]);
                            if (mode == ScanMode::RcData && !buffer.empty()) {
                                std::size_t cursor = 0;
                                std::string decoded;
                                while (cursor < buffer.size()) {
                                    if (buffer[cursor] == '&') {
                                        DecodeCharacterReference(
                                            buffer, cursor, decoded, &diagnostics, path, bufferLine, bufferColumn);
                                    } else {
                                        decoded.push_back(buffer[cursor]);
                                        ++cursor;
                                    }
                                }
                                token.data = decoded;
                            } else {
                                token.data = buffer;
                            }
                            index = probe;
                            line = probeLine;
                            column = probeColumn;
                            mode = ScanMode::Data;
                            Token endTag;
                            endTag.kind = TokenKind::EndTag;
                            endTag.name = rawEndTag;
                            endTag.line = probeLine;
                            endTag.column = probeColumn;
                            if (!token.data.empty()) {
                                pendingEnd = endTag;
                                hasPending = true;
                                return token;
                            }
                            return endTag;
                        }
                    }
                }
            }
            buffer.push_back(source[index]);
            TakeChar(nullptr);
        }
        diagnostics.Error(path, token.line, token.column, "missing end tag '</" + rawEndTag + ">'");
        token.data = buffer;
        mode = ScanMode::Data;
        return token;
    }

    bool hasPending = false;
    Token pendingEnd;

public:
    bool HasPending() const {
        return hasPending;
    }

    Token TakePending() {
        hasPending = false;
        return pendingEnd;
    }
};

bool IsCustomElementName(const std::string& name) {
    return name.find('-') != std::string::npos && !name.empty() &&
           std::isalpha(static_cast<unsigned char>(name[0])) != 0;
}

}  // namespace

Document HtmlParser::Parse(const std::string& sourceText, const std::string& path, Diagnostics& diagnostics) {
    Document document;
    document.sourcePath = path;
    std::string source = sourceText;
    if (source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB && static_cast<unsigned char>(source[2]) == 0xBF) {
        source.erase(0, 3);
    }
    {
        std::size_t cursor = 0;
        bool valid = true;
        while (cursor < source.size()) {
            const std::size_t before = cursor;
            char32_t codePoint = 0;
            if (!DecodeUtf8(source, cursor, codePoint) && codePoint == 0xFFFD && cursor == before + 1 &&
                static_cast<unsigned char>(source[before]) >= 0x80) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            diagnostics.Error(path, 1, 1, "source is not valid UTF-8");
        }
    }

    auto root = std::make_unique<Node>();
    root->type = Node::Type::Document;
    root->tagName = "#document";
    Node* html = nullptr;
    Node* head = nullptr;
    Node* body = nullptr;
    std::vector<Node*> stack;
    bool sawDoctype = false;
    Tokenizer tokenizer(source, path, diagnostics);

    auto current = [&]() -> Node* {
        if (stack.empty()) {
            return root.get();
        }
        return stack.back();
    };

    auto append = [&](std::unique_ptr<Node> node) -> Node* {
        Node* parent = current();
        node->parent = parent;
        Node* raw = node.get();
        parent->children.push_back(std::move(node));
        return raw;
    };

    auto pushElement = [&](const Token& token, ElementKind kind) -> Node* {
        auto node = std::make_unique<Node>();
        node->type = Node::Type::Element;
        node->kind = kind;
        node->tagName = token.name;
        node->attributes = token.attributes;
        node->line = token.line;
        node->column = token.column;
        Node* raw = append(std::move(node));
        stack.push_back(raw);
        return raw;
    };

    auto ensureHtml = [&](int line, int column) {
        if (html != nullptr) {
            return;
        }
        Token token;
        token.name = "html";
        token.line = line;
        token.column = column;
        html = pushElement(token, ElementKind::Html);
    };

    auto ensureHead = [&](int line, int column) {
        ensureHtml(line, column);
        if (head != nullptr) {
            return;
        }
        Token token;
        token.name = "head";
        token.line = line;
        token.column = column;
        head = pushElement(token, ElementKind::Head);
    };

    auto closeHead = [&]() {
        if (head == nullptr) {
            return;
        }
        while (!stack.empty() && stack.back() != html) {
            if (stack.back()->kind == ElementKind::Head || stack.back()->parent == head || stack.back() == head) {
                stack.pop_back();
                continue;
            }
            break;
        }
        if (!stack.empty() && stack.back() == head) {
            stack.pop_back();
        }
    };

    auto ensureBody = [&](int line, int column) {
        ensureHead(line, column);
        closeHead();
        if (body != nullptr) {
            return;
        }
        Token token;
        token.name = "body";
        token.line = line;
        token.column = column;
        body = pushElement(token, ElementKind::Body);
    };

    auto impliedCloseForStart = [&](ElementKind kind, int line, int column) {
        const std::string name = SpecFor(kind).name;
        if (ClosesParagraph(kind)) {
            while (!stack.empty() && stack.back()->kind == ElementKind::P) {
                diagnostics.Warning(path, line, column, "end tag for 'p' was implied by '<" + name + ">'");
                stack.pop_back();
            }
        }
        if (kind == ElementKind::Li) {
            while (!stack.empty() && stack.back()->kind == ElementKind::Li) {
                diagnostics.Warning(path, line, column, "end tag for 'li' was implied by '<li>'");
                stack.pop_back();
            }
        }
        if (kind == ElementKind::Dt || kind == ElementKind::Dd) {
            while (!stack.empty() &&
                   (stack.back()->kind == ElementKind::Dt || stack.back()->kind == ElementKind::Dd)) {
                diagnostics.Warning(
                    path, line, column, "end tag for '" + stack.back()->tagName + "' was implied by '<" + name + ">'");
                stack.pop_back();
            }
        }
        if (kind == ElementKind::Option) {
            while (!stack.empty() && stack.back()->kind == ElementKind::Option) {
                stack.pop_back();
            }
        }
        if (kind == ElementKind::Optgroup) {
            while (!stack.empty() &&
                   (stack.back()->kind == ElementKind::Option || stack.back()->kind == ElementKind::Optgroup)) {
                stack.pop_back();
            }
        }
        if (kind == ElementKind::Tr || kind == ElementKind::Thead || kind == ElementKind::Tbody ||
            kind == ElementKind::Tfoot) {
            while (!stack.empty() &&
                   (stack.back()->kind == ElementKind::Td || stack.back()->kind == ElementKind::Th ||
                    stack.back()->kind == ElementKind::Tr)) {
                stack.pop_back();
            }
        }
        if (kind == ElementKind::Td || kind == ElementKind::Th) {
            while (!stack.empty() &&
                   (stack.back()->kind == ElementKind::Td || stack.back()->kind == ElementKind::Th)) {
                stack.pop_back();
            }
        }
        if (kind == ElementKind::Rt || kind == ElementKind::Rp) {
            while (!stack.empty() &&
                   (stack.back()->kind == ElementKind::Rt || stack.back()->kind == ElementKind::Rp)) {
                stack.pop_back();
            }
        }
    };

    auto handleStart = [&](Token token) {
        if (token.name == "image") {
            diagnostics.Warning(path, token.line, token.column, "element 'image' is an obsolete alias for 'img'");
            token.name = "img";
        }
        ElementKind kind = FindElement(token.name);
        if (kind == ElementKind::Svg) {
            diagnostics.Error(path, token.line, token.column, "SVG is not implemented");
        } else if (kind == ElementKind::Math) {
            diagnostics.Error(path, token.line, token.column, "MathML is not implemented");
        } else if (kind == ElementKind::Unknown) {
            if (IsCustomElementName(token.name)) {
                diagnostics.Warning(
                    path, token.line, token.column,
                    "custom element '" + token.name + "' has no registered behavior and renders as an inline element");
            } else {
                diagnostics.Error(path, token.line, token.column, "unknown element '" + token.name + "'");
            }
        } else if ((SpecFor(kind).traits & TRAIT_OBSOLETE) != 0) {
            diagnostics.Warning(
                path, token.line, token.column,
                "element '" + token.name + "' is obsolete and may not match current browser behavior");
        } else if ((SpecFor(kind).traits & TRAIT_EXPERIMENTAL) != 0) {
            diagnostics.Warning(
                path, token.line, token.column, "element '" + token.name + "' is experimental");
        }
        const bool selfClosing = token.data == "/";
        const bool isVoid = kind != ElementKind::Unknown && (SpecFor(kind).traits & TRAIT_VOID) != 0;
        if (selfClosing && !isVoid && kind != ElementKind::Unknown) {
            diagnostics.Warning(
                path, token.line, token.column,
                "non-void element '" + token.name + "' uses a self-closing tag; the end tag is still required by HTML");
        }
        if (kind == ElementKind::Html) {
            ensureHtml(token.line, token.column);
            if (!token.attributes.empty() && html->attributes.empty()) {
                html->attributes = token.attributes;
            }
            return;
        }
        if (kind == ElementKind::Head) {
            ensureHead(token.line, token.column);
            return;
        }
        if (kind == ElementKind::Body) {
            ensureBody(token.line, token.column);
            if (!token.attributes.empty() && body->attributes.empty()) {
                body->attributes = token.attributes;
            }
            return;
        }
        if (body == nullptr && IsMetadataElement(kind)) {
            ensureHead(token.line, token.column);
        } else {
            ensureBody(token.line, token.column);
            bool metaCharset = false;
            if (kind == ElementKind::Meta) {
                for (const Attribute& attribute : token.attributes) {
                    if (attribute.name == "charset") {
                        metaCharset = true;
                    }
                }
            }
            if (kind == ElementKind::Title || kind == ElementKind::Base || metaCharset) {
                diagnostics.Warning(
                    path, token.line, token.column,
                    "element '" + token.name + "' belongs in head and was moved there");
                while (!stack.empty() && stack.back() != html) {
                    stack.pop_back();
                }
                stack.push_back(head);
            }
        }
        impliedCloseForStart(kind, token.line, token.column);
        if (kind == ElementKind::Plaintext) {
            Node* node = pushElement(token, kind);
            (void)node;
            tokenizer.SetMode(ScanMode::PlainText, "");
            return;
        }
        if (!isVoid) {
            pushElement(token, kind);
            if ((SpecFor(kind).traits & TRAIT_RAW_TEXT) != 0) {
                tokenizer.SetMode(ScanMode::RawText, token.name);
            } else if ((SpecFor(kind).traits & TRAIT_RC_DATA) != 0) {
                tokenizer.SetMode(ScanMode::RcData, token.name);
            }
        } else {
            auto node = std::make_unique<Node>();
            node->type = Node::Type::Element;
            node->kind = kind;
            node->tagName = token.name;
            node->attributes = token.attributes;
            node->line = token.line;
            node->column = token.column;
            append(std::move(node));
        }
    };

    auto handleEnd = [&](const Token& token) {
        if (token.name == "br") {
            diagnostics.Warning(path, token.line, token.column, "end tag '</br>' is treated as a line break");
            Token start = token;
            start.kind = TokenKind::StartTag;
            handleStart(start);
            return;
        }
        if (token.name == "head") {
            closeHead();
            return;
        }
        if (token.name == "body") {
            ensureBody(token.line, token.column);
            while (!stack.empty() && stack.back() != body) {
                diagnostics.Warning(
                    path, token.line, token.column,
                    "end tag for '" + stack.back()->tagName + "' was implied by '</body>'");
                stack.pop_back();
            }
            return;
        }
        if (token.name == "html") {
            while (stack.size() > 1) {
                stack.pop_back();
            }
            return;
        }
        bool found = false;
        for (Node* open : stack) {
            if (open->tagName == token.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            diagnostics.Error(path, token.line, token.column, "stray end tag '</" + token.name + ">'");
            return;
        }
        while (!stack.empty() && stack.back()->tagName != token.name) {
            diagnostics.Warning(
                path, token.line, token.column,
                "end tag for '" + stack.back()->tagName + "' was implied by '</" + token.name + ">'");
            stack.pop_back();
        }
        if (!stack.empty() && stack.back()->tagName == token.name) {
            stack.pop_back();
        }
    };

    while (true) {
        Token token = tokenizer.HasPending() ? tokenizer.TakePending() : tokenizer.Next();
        if (token.kind == TokenKind::End) {
            break;
        }
        if (token.kind == TokenKind::Doctype) {
            if (sawDoctype || html != nullptr) {
                diagnostics.Warning(path, token.line, token.column, "doctype is ignored because it is not the first token");
            } else {
                sawDoctype = true;
                document.quirks = token.forceQuirks;
                if (token.forceQuirks) {
                    diagnostics.Warning(
                        path, token.line, token.column,
                        "doctype is not the HTML doctype; the document is rendered in quirks mode");
                }
            }
            continue;
        }
        if (token.kind == TokenKind::Comment) {
            continue;
        }
        if (token.kind == TokenKind::Character) {
            bool onlySpace = true;
            for (char character : token.data) {
                if (character != ' ' && character != '\t' && character != '\n' && character != '\r' && character != '\f') {
                    onlySpace = false;
                    break;
                }
            }
            const bool insideOpenElement = !stack.empty() && stack.back() != html && stack.back() != head;
            if (body == nullptr && onlySpace && !insideOpenElement) {
                continue;
            }
            if (body == nullptr && !insideOpenElement) {
                ensureBody(token.line, token.column);
            }
            if (!stack.empty() && stack.back()->type == Node::Type::Element &&
                !stack.back()->children.empty() && stack.back()->children.back()->type == Node::Type::Text) {
                stack.back()->children.back()->text += token.data;
            } else {
                auto text = std::make_unique<Node>();
                text->type = Node::Type::Text;
                text->text = token.data;
                text->line = token.line;
                text->column = token.column;
                append(std::move(text));
            }
            continue;
        }
        if (token.kind == TokenKind::StartTag) {
            handleStart(token);
            continue;
        }
        if (token.kind == TokenKind::EndTag) {
            handleEnd(token);
        }
    }
    if (!sawDoctype) {
        diagnostics.Warning(path, 1, 1, "doctype is missing; the document is rendered in quirks mode");
        document.quirks = true;
    }
    if (html == nullptr) {
        ensureBody(1, 1);
    } else if (body == nullptr) {
        ensureBody(1, 1);
    }
    while (stack.size() > 1) {
        Node* open = stack.back();
        const bool optionalEnd =
            open->kind == ElementKind::Html || open->kind == ElementKind::Head || open->kind == ElementKind::Body ||
            open->kind == ElementKind::P || open->kind == ElementKind::Li || open->kind == ElementKind::Dt ||
            open->kind == ElementKind::Dd || open->kind == ElementKind::Option || open->kind == ElementKind::Optgroup ||
            open->kind == ElementKind::Tr || open->kind == ElementKind::Td || open->kind == ElementKind::Th ||
            open->kind == ElementKind::Thead || open->kind == ElementKind::Tbody || open->kind == ElementKind::Tfoot ||
            open->kind == ElementKind::Colgroup || open->kind == ElementKind::Caption || open->kind == ElementKind::Rt ||
            open->kind == ElementKind::Rp;
        if (!optionalEnd && (SpecFor(open->kind).traits & TRAIT_VOID) == 0) {
            diagnostics.Warning(
                path, open->line, open->column, "end tag for '" + open->tagName + "' was implied at end of file");
        }
        stack.pop_back();
    }
    InitializeControlState(*root);
    document.root = std::move(root);
    CollectDocumentInfo(document);
    return document;
}

}
