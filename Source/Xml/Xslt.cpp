#include "Xml/Xslt.h"

#include "Xml/XmlParser.h"
#include "Xml/XPath.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>

namespace RoboWeb {

namespace {

const char* XSLT_URI = "http://www.w3.org/1999/XSL/Transform";
const char* EXSL_URI = "http://exslt.org/common";
const char* MATH_URI = "http://exslt.org/math";
const char* SET_URI = "http://exslt.org/sets";
const char* STR_URI = "http://exslt.org/strings";
const char* REGEXP_URI = "http://exslt.org/regular-expressions";

bool IsXslt(const Node& node) {
    return node.type == Node::Type::Element && NamespaceUriOf(node, node.tagName.find(':') == std::string::npos ? "" : node.tagName.substr(0, node.tagName.find(':'))) == XSLT_URI;
}

std::string LocalOf(const Node& node) {
    const std::size_t colon = node.tagName.find(':');
    return colon == std::string::npos ? node.tagName : node.tagName.substr(colon + 1);
}

std::string AttributeOf(const Node& node, const char* name) {
    for (const Attribute& attribute : node.attributes) {
        if (attribute.name == name) {
            return attribute.value;
        }
    }
    return "";
}

void CollectNamespaces(const Node& node, std::map<std::string, std::string>& namespaces) {
    for (const Node* cursor = &node; cursor != nullptr; cursor = cursor->parent) {
        if (cursor->type != Node::Type::Element) {
            continue;
        }
        for (const Attribute& attribute : cursor->attributes) {
            if (attribute.name == "xmlns" && namespaces.find("") == namespaces.end()) {
                namespaces[""] = attribute.value;
            } else if (attribute.name.compare(0, 6, "xmlns:") == 0 && namespaces.find(attribute.name.substr(6)) == namespaces.end()) {
                namespaces[attribute.name.substr(6)] = attribute.value;
            }
        }
    }
}

struct TemplateRule {
    const Node* node = nullptr;
    std::string match;
    std::string name;
    std::string mode;
    double priority = 0;
    int precedence = 0;
    int order = 0;
    bool literal = false;
};

struct AttributeSet {
    std::string name;
    const Node* node = nullptr;
};

struct DecimalFormat {
    std::string name;
    char decimal = '.';
    char grouping = ',';
};

class Transformer {
public:
    XmlParseResult source;
    XmlParseResult sheet;
    std::string sheetPath;
    std::vector<TemplateRule> templates;
    std::vector<AttributeSet> attributeSets;
    std::vector<DecimalFormat> formats;
    std::map<std::string, std::string> aliases;
    std::vector<std::string> strip;
    std::vector<std::string> preserve;
    std::string method = "xml";
    XsltResult result;
    XPathEnvironment environment;
    int templateOrder = 0;
    int depth = 0;
    const TemplateRule* activeTemplate = nullptr;

    XPathNode RootNode() const {
        return XPathNode{source.document.root.get(), false, "", ""};
    }

    XPathValue Evaluate(const std::string& expression, const XPathNode& context, std::size_t position, std::size_t size) {
        environment.current = context;
        environment.sourceRoot = source.document.root.get();
        environment.idAttributes = source.idAttributes;
        environment.unparsedEntities = source.unparsedEntities;
        environment.extension = [this](const std::string& nameSpace, const std::string& name, const std::vector<XPathValue>& arguments) {
            return Extension(nameSpace, name, arguments);
        };
        return EvaluateXPath(expression, context, position, size, environment);
    }

    bool Run() {
        if (!source.wellFormed) {
            result.error = "source xml is not well-formed";
            return false;
        }
        if (!sheet.wellFormed) {
            result.error = "stylesheet xml is not well-formed";
            return false;
        }
        Node* stylesheet = StylesheetElement();
        if (stylesheet == nullptr) {
            result.error = "stylesheet root is missing";
            return false;
        }
        CollectNamespaces(*stylesheet, environment.namespaces);
        if (LocalOf(*stylesheet) == "stylesheet" || LocalOf(*stylesheet) == "transform") {
            CollectTop(*stylesheet, 1);
        } else {
            TemplateRule rule;
            rule.node = stylesheet;
            rule.match = "/";
            rule.priority = 0.5;
            rule.precedence = 1;
            rule.order = ++templateOrder;
            rule.literal = true;
            templates.push_back(rule);
        }
        StripSpace(*source.document.root);
        for (const std::unique_ptr<Node>& child : sheet.document.root->children) {
            if (child->type == Node::Type::Element && IsXslt(*child) && (LocalOf(*child) == "variable" || LocalOf(*child) == "param")) {
                environment.variables[AttributeOf(*child, "name")] = VariableValue(*child, RootNode(), 1, 1);
            }
        }
        auto output = std::make_unique<Node>();
        output->type = Node::Type::Document;
        ApplyTemplates(RootNode(), "", 1, 1, nullptr, output.get());
        if (method == "text") {
            result.output = SerializeText(*output);
        } else {
            result.output = SerializeXml(*output);
        }
        result.method = method;
        result.ok = result.error.empty();
        return result.ok;
    }

    Node* StylesheetElement() {
        if (sheet.document.root == nullptr) {
            return nullptr;
        }
        for (const std::unique_ptr<Node>& child : sheet.document.root->children) {
            if (child->type == Node::Type::Element) {
                return child.get();
            }
        }
        return nullptr;
    }

    void CollectTop(Node& stylesheet, int precedence) {
        for (const std::unique_ptr<Node>& child : stylesheet.children) {
            if (child->type != Node::Type::Element || !IsXslt(*child)) {
                continue;
            }
            const std::string local = LocalOf(*child);
            if (local == "import" || local == "include") {
                LoadHref(*child, local == "import" ? precedence - 1 : precedence);
            } else if (local == "template") {
                TemplateRule rule;
                rule.node = child.get();
                rule.match = AttributeOf(*child, "match");
                rule.name = AttributeOf(*child, "name");
                rule.mode = AttributeOf(*child, "mode");
                rule.precedence = precedence;
                rule.order = ++templateOrder;
                if (!AttributeOf(*child, "priority").empty()) {
                    rule.priority = std::atof(AttributeOf(*child, "priority").c_str());
                } else {
                    rule.priority = DefaultPriority(rule.match);
                }
                templates.push_back(rule);
            } else if (local == "output") {
                if (!AttributeOf(*child, "method").empty()) {
                    method = AttributeOf(*child, "method");
                }
            } else if (local == "attribute-set") {
                attributeSets.push_back(AttributeSet{AttributeOf(*child, "name"), child.get()});
            } else if (local == "decimal-format") {
                DecimalFormat format;
                format.name = AttributeOf(*child, "name");
                if (!AttributeOf(*child, "decimal-separator").empty()) {
                    format.decimal = AttributeOf(*child, "decimal-separator")[0];
                }
                if (!AttributeOf(*child, "grouping-separator").empty()) {
                    format.grouping = AttributeOf(*child, "grouping-separator")[0];
                }
                formats.push_back(format);
            } else if (local == "namespace-alias") {
                aliases[AttributeOf(*child, "stylesheet-prefix")] = AttributeOf(*child, "result-prefix");
            } else if (local == "strip-space") {
                AddNames(strip, AttributeOf(*child, "elements"));
            } else if (local == "preserve-space") {
                AddNames(preserve, AttributeOf(*child, "elements"));
            } else if (local == "key") {
                keys.push_back(child.get());
            }
        }
    }

    std::vector<const Node*> keys;

    void AddNames(std::vector<std::string>& target, const std::string& text) {
        std::string token;
        for (std::size_t index = 0; index <= text.size(); ++index) {
            if (index == text.size() || text[index] == ' ' || text[index] == '\t') {
                if (!token.empty()) {
                    target.push_back(token);
                }
                token.clear();
            } else {
                token.push_back(text[index]);
            }
        }
    }

    void LoadHref(const Node& instruction, int precedence) {
        const std::string href = AttributeOf(instruction, "href");
        std::ifstream input(href);
        if (!input && !sheetPath.empty()) {
            const std::size_t slash = sheetPath.find_last_of("/\\");
            const std::string base = slash == std::string::npos ? std::string() : sheetPath.substr(0, slash + 1);
            input.open(base + href);
        }
        if (!input) {
            result.error = "stylesheet was not loaded: " + href;
            return;
        }
        std::stringstream buffer;
        buffer << input.rdbuf();
        Diagnostics diagnostics;
        documents.push_back(std::make_unique<XmlParseResult>(ParseXml(buffer.str(), href, diagnostics)));
        XmlParseResult& included = *documents.back();
        if (!included.wellFormed || included.document.root == nullptr) {
            result.error = "included stylesheet is not well-formed: " + href;
            return;
        }
        for (const std::unique_ptr<Node>& child : included.document.root->children) {
            if (child->type == Node::Type::Element) {
                CollectTop(*child, precedence);
            }
        }
    }

    double DefaultPriority(const std::string& match) const {
        if (match.empty() || match == "/" ) {
            return 0.5;
        }
        if (match.find('/') != std::string::npos || match.find('[') != std::string::npos) {
            return 0.5;
        }
        if (match == "*" || match == "@*" || match == "node()" || match == "text()" || match == "comment()") {
            return match == "*" || match == "@*" ? -0.25 : -0.5;
        }
        if (match.size() > 2 && match.compare(match.size() - 2, 2, ":*") == 0) {
            return -0.25;
        }
        return 0;
    }

    bool Listed(const std::vector<std::string>& names, const std::string& tag) const {
        for (const std::string& name : names) {
            if (name == "*" || name == tag) {
                return true;
            }
        }
        return false;
    }

    void StripSpace(Node& node) {
        if (node.type != Node::Type::Element && node.type != Node::Type::Document) {
            return;
        }
        std::vector<std::unique_ptr<Node>> kept;
        for (std::unique_ptr<Node>& child : node.children) {
            if (child->type == Node::Type::Text && node.type == Node::Type::Element) {
                bool blank = true;
                for (char character : child->text) {
                    if (character != ' ' && character != '\t' && character != '\n' && character != '\r') {
                        blank = false;
                    }
                }
                if (blank && Listed(strip, node.tagName) && !Listed(preserve, node.tagName)) {
                    continue;
                }
            }
            StripSpace(*child);
            kept.push_back(std::move(child));
        }
        node.children = std::move(kept);
    }

    const TemplateRule* BestTemplate(const XPathNode& node, const std::string& mode, int maxPrecedence) const {
        const TemplateRule* best = nullptr;
        for (const TemplateRule& rule : templates) {
            if (rule.match.empty() || rule.mode != mode || rule.precedence > maxPrecedence) {
                continue;
            }
            if (!PatternMatches(node, rule.match)) {
                continue;
            }
            if (best == nullptr || rule.precedence > best->precedence ||
                (rule.precedence == best->precedence && (rule.priority > best->priority || (rule.priority == best->priority && rule.order > best->order)))) {
                best = &rule;
            }
        }
        return best;
    }

    bool PatternMatches(const XPathNode& node, const std::string& pattern) const {
        if (pattern == "/") {
            return node.node != nullptr && node.node->type == Node::Type::Document;
        }
        std::string path = pattern;
        if (path.empty()) {
            return false;
        }
        if (path[0] != '/') {
            path = "//" + path;
        }
        XPathEnvironment local = environment;
        local.extension = nullptr;
        const XPathValue value = EvaluateXPath(path, RootNode(), 1, 1, local);
        if (value.kind != XPathKind::NodeSet) {
            return false;
        }
        for (const XPathNode& candidate : value.nodes) {
            if (SameXPathNode(candidate, node)) {
                return true;
            }
        }
        return false;
    }

    void ApplyTemplates(const XPathNode& node, const std::string& mode, std::size_t position, std::size_t size, const Node* params, Node* output) {
        if (++depth > 200) {
            result.error = "template recursion is too deep";
            --depth;
            return;
        }
        const int limit = activeTemplate != nullptr && params == reinterpret_cast<const Node*>(1) ? activeTemplate->precedence - 1 : 1000;
        const TemplateRule* rule = BestTemplate(node, mode, limit);
        const TemplateRule* previous = activeTemplate;
        std::map<std::string, XPathValue> saved = environment.variables;
        if (params != nullptr && params != reinterpret_cast<const Node*>(1)) {
            for (const std::unique_ptr<Node>& child : params->children) {
                if (child->type == Node::Type::Element && IsXslt(*child) && LocalOf(*child) == "with-param") {
                    environment.variables[AttributeOf(*child, "name")] = VariableValue(*child, node, position, size);
                }
            }
        }
        if (rule != nullptr) {
            activeTemplate = rule;
            if (rule->node != nullptr) {
                for (const std::unique_ptr<Node>& child : rule->node->children) {
                    if (child->type == Node::Type::Element && IsXslt(*child) && LocalOf(*child) == "param" &&
                        environment.variables.find(AttributeOf(*child, "name")) == environment.variables.end()) {
                        environment.variables[AttributeOf(*child, "name")] = VariableValue(*child, node, position, size);
                    }
                }
            }
            if (rule->literal) {
                Instantiate(*rule->node, node, position, size, output);
            } else {
                InstantiateChildren(*rule->node, node, position, size, output);
            }
        } else {
            BuiltIn(node, mode, output);
        }
        environment.variables = saved;
        activeTemplate = previous;
        --depth;
    }

    void BuiltIn(const XPathNode& node, const std::string& mode, Node* output) {
        if (node.attribute || (node.node != nullptr && (node.node->type == Node::Type::Text || node.node->type == Node::Type::ProcessingInstruction))) {
            AppendText(output, XPathStringValue(node));
            return;
        }
        if (node.node == nullptr) {
            return;
        }
        std::size_t count = 0;
        for (const std::unique_ptr<Node>& child : node.node->children) {
            if (child->type != Node::Type::Element && child->type != Node::Type::Text) {
                continue;
            }
            ++count;
        }
        std::size_t position = 1;
        for (const std::unique_ptr<Node>& child : node.node->children) {
            if (child->type != Node::Type::Element && child->type != Node::Type::Text) {
                continue;
            }
            ApplyTemplates(XPathNode{child.get(), false, "", ""}, mode, position, count, nullptr, output);
            ++position;
        }
    }

    void InstantiateChildren(const Node& instruction, const XPathNode& context, std::size_t position, std::size_t size, Node* output) {
        for (const std::unique_ptr<Node>& child : instruction.children) {
            Instantiate(*child, context, position, size, output);
        }
    }

    void Instantiate(const Node& instruction, const XPathNode& context, std::size_t position, std::size_t size, Node* output) {
        if (instruction.type == Node::Type::Text) {
            if (IsXslt(*instruction.parent)) {
                bool blank = true;
                for (char character : instruction.text) {
                    if (character != ' ' && character != '\t' && character != '\n' && character != '\r') {
                        blank = false;
                    }
                }
                if (blank) {
                    return;
                }
            }
            AppendText(output, instruction.text);
            return;
        }
        if (instruction.type != Node::Type::Element) {
            return;
        }
        const std::size_t colon = instruction.tagName.find(':');
        const std::string prefix = colon == std::string::npos ? std::string() : instruction.tagName.substr(0, colon);
        if (aliases.find(prefix) != aliases.end()) {
            Literal(instruction, context, position, size, output);
            return;
        }
        if (!IsXslt(instruction)) {
            Literal(instruction, context, position, size, output);
            return;
        }
        const std::string local = LocalOf(instruction);
        if (local == "value-of") {
            AppendText(output, XPathToString(Evaluate(AttributeOf(instruction, "select"), context, position, size)));
        } else if (local == "text") {
            std::string text;
            for (const std::unique_ptr<Node>& child : instruction.children) {
                if (child->type == Node::Type::Text) {
                    text += child->text;
                }
            }
            AppendText(output, text);
        } else if (local == "if") {
            if (XPathToBoolean(Evaluate(AttributeOf(instruction, "test"), context, position, size))) {
                InstantiateChildren(instruction, context, position, size, output);
            }
        } else if (local == "choose") {
            for (const std::unique_ptr<Node>& child : instruction.children) {
                if (child->type != Node::Type::Element || !IsXslt(*child)) {
                    continue;
                }
                if (LocalOf(*child) == "when" && XPathToBoolean(Evaluate(AttributeOf(*child, "test"), context, position, size))) {
                    InstantiateChildren(*child, context, position, size, output);
                    break;
                }
                if (LocalOf(*child) == "otherwise") {
                    InstantiateChildren(*child, context, position, size, output);
                    break;
                }
            }
        } else if (local == "for-each") {
            XPathValue selected = Evaluate(AttributeOf(instruction, "select"), context, position, size);
            SortNodes(instruction, selected.nodes);
            for (std::size_t index = 0; index < selected.nodes.size(); ++index) {
                InstantiateChildren(instruction, selected.nodes[index], index + 1, selected.nodes.size(), output);
            }
        } else if (local == "apply-templates") {
            XPathValue selected = AttributeOf(instruction, "select").empty()
                ? Evaluate("node()", context, position, size)
                : Evaluate(AttributeOf(instruction, "select"), context, position, size);
            SortNodes(instruction, selected.nodes);
            for (std::size_t index = 0; index < selected.nodes.size(); ++index) {
                ApplyTemplates(selected.nodes[index], AttributeOf(instruction, "mode"), index + 1, selected.nodes.size(), &instruction, output);
            }
        } else if (local == "call-template") {
            for (const TemplateRule& rule : templates) {
                if (rule.name == AttributeOf(instruction, "name")) {
                    const TemplateRule* previous = activeTemplate;
                    activeTemplate = &rule;
                    std::map<std::string, XPathValue> saved = environment.variables;
                    for (const std::unique_ptr<Node>& child : instruction.children) {
                        if (child->type == Node::Type::Element && IsXslt(*child) && LocalOf(*child) == "with-param") {
                            environment.variables[AttributeOf(*child, "name")] = VariableValue(*child, context, position, size);
                        }
                    }
                    InstantiateChildren(*rule.node, context, position, size, output);
                    environment.variables = saved;
                    activeTemplate = previous;
                    break;
                }
            }
        } else if (local == "apply-imports") {
            ApplyTemplates(context, activeTemplate == nullptr ? "" : activeTemplate->mode, position, size, reinterpret_cast<const Node*>(1), output);
        } else if (local == "copy") {
            if (context.attribute) {
                if (output->type == Node::Type::Element) {
                    Attribute attribute;
                    attribute.name = context.attributeName;
                    attribute.value = context.attributeValue;
                    output->attributes.push_back(attribute);
                }
            } else if (context.node != nullptr && context.node->type == Node::Type::Element) {
                auto element = std::make_unique<Node>();
                element->type = Node::Type::Element;
                element->tagName = context.node->tagName;
                element->parent = output;
                Node* raw = element.get();
                output->children.push_back(std::move(element));
                InstantiateChildren(instruction, context, position, size, raw);
            } else {
                AppendText(output, XPathStringValue(context));
            }
        } else if (local == "copy-of") {
            const XPathValue selected = Evaluate(AttributeOf(instruction, "select"), context, position, size);
            if (selected.kind == XPathKind::NodeSet) {
                for (const XPathNode& node : selected.nodes) {
                    CopyDeep(node, output);
                }
            } else {
                AppendText(output, XPathToString(selected));
            }
        } else if (local == "element") {
            auto element = std::make_unique<Node>();
            element->type = Node::Type::Element;
            element->tagName = Alias(Expand(AttributeOf(instruction, "name"), context, position, size));
            element->parent = output;
            Node* raw = element.get();
            ApplyAttributeSets(AttributeOf(instruction, "use-attribute-sets"), context, position, size, raw);
            output->children.push_back(std::move(element));
            InstantiateChildren(instruction, context, position, size, raw);
        } else if (local == "attribute") {
            if (output->type == Node::Type::Element) {
                Attribute attribute;
                attribute.name = Expand(AttributeOf(instruction, "name"), context, position, size);
                std::string value = AttributeOf(instruction, "select").empty() ? std::string() : XPathToString(Evaluate(AttributeOf(instruction, "select"), context, position, size));
                if (value.empty()) {
                    auto buffer = std::make_unique<Node>();
                    buffer->type = Node::Type::Document;
                    InstantiateChildren(instruction, context, position, size, buffer.get());
                    value = SerializeText(*buffer);
                }
                attribute.value = value;
                output->attributes.push_back(attribute);
            }
        } else if (local == "comment") {
            auto comment = std::make_unique<Node>();
            comment->type = Node::Type::Comment;
            auto buffer = std::make_unique<Node>();
            buffer->type = Node::Type::Document;
            InstantiateChildren(instruction, context, position, size, buffer.get());
            comment->text = SerializeText(*buffer);
            comment->parent = output;
            output->children.push_back(std::move(comment));
        } else if (local == "processing-instruction") {
            auto instructionNode = std::make_unique<Node>();
            instructionNode->type = Node::Type::ProcessingInstruction;
            instructionNode->tagName = Expand(AttributeOf(instruction, "name"), context, position, size);
            auto buffer = std::make_unique<Node>();
            buffer->type = Node::Type::Document;
            InstantiateChildren(instruction, context, position, size, buffer.get());
            instructionNode->text = SerializeText(*buffer);
            instructionNode->parent = output;
            output->children.push_back(std::move(instructionNode));
        } else if (local == "number") {
            AppendText(output, FormatNumber(instruction, context, position, size));
        } else if (local == "message") {
            auto buffer = std::make_unique<Node>();
            buffer->type = Node::Type::Document;
            if (!AttributeOf(instruction, "select").empty()) {
                AppendText(buffer.get(), XPathToString(Evaluate(AttributeOf(instruction, "select"), context, position, size)));
            }
            InstantiateChildren(instruction, context, position, size, buffer.get());
            result.messages.push_back(SerializeText(*buffer));
        } else if (local == "param") {
            if (environment.variables.find(AttributeOf(instruction, "name")) == environment.variables.end()) {
                environment.variables[AttributeOf(instruction, "name")] = VariableValue(instruction, context, position, size);
            }
        } else if (local == "variable") {
            environment.variables[AttributeOf(instruction, "name")] = VariableValue(instruction, context, position, size);
        } else if (local == "fallback") {
            InstantiateChildren(instruction, context, position, size, output);
        } else {
            bool fell = false;
            for (const std::unique_ptr<Node>& child : instruction.children) {
                if (child->type == Node::Type::Element && IsXslt(*child) && LocalOf(*child) == "fallback") {
                    InstantiateChildren(*child, context, position, size, output);
                    fell = true;
                }
            }
            if (!fell && local != "sort" && local != "with-param" && local != "param") {
                result.messages.push_back("unsupported xslt element " + local);
            }
        }
    }

    void Literal(const Node& instruction, const XPathNode& context, std::size_t position, std::size_t size, Node* output) {
        auto element = std::make_unique<Node>();
        element->type = Node::Type::Element;
        element->tagName = Alias(instruction.tagName);
        element->parent = output;
        Node* raw = element.get();
        for (const Attribute& attribute : instruction.attributes) {
            if (attribute.name.compare(0, 6, "xmlns:") == 0 || attribute.name == "xmlns") {
                raw->attributes.push_back(attribute);
                continue;
            }
            if (attribute.name == "xsl:use-attribute-sets" || LocalOfAttribute(attribute.name) == "use-attribute-sets") {
                ApplyAttributeSets(attribute.value, context, position, size, raw);
                continue;
            }
            Attribute copy = attribute;
            copy.value = Expand(attribute.value, context, position, size);
            raw->attributes.push_back(copy);
        }
        output->children.push_back(std::move(element));
        InstantiateChildren(instruction, context, position, size, raw);
    }

    std::string LocalOfAttribute(const std::string& name) const {
        const std::size_t colon = name.find(':');
        return colon == std::string::npos ? name : name.substr(colon + 1);
    }

    std::string Alias(const std::string& name) const {
        const std::size_t colon = name.find(':');
        if (colon == std::string::npos) {
            return name;
        }
        const std::string prefix = name.substr(0, colon);
        const auto found = aliases.find(prefix);
        if (found == aliases.end() || found->second.empty()) {
            return name;
        }
        return found->second + name.substr(colon);
    }

    void ApplyAttributeSets(const std::string& names, const XPathNode& context, std::size_t position, std::size_t size, Node* output) {
        std::string token;
        for (std::size_t index = 0; index <= names.size(); ++index) {
            if (index == names.size() || names[index] == ' ') {
                if (!token.empty()) {
                    for (const AttributeSet& set : attributeSets) {
                        if (set.name == token && set.node != nullptr) {
                            InstantiateChildren(*set.node, context, position, size, output);
                        }
                    }
                }
                token.clear();
            } else {
                token.push_back(names[index]);
            }
        }
    }

    XPathValue VariableValue(const Node& instruction, const XPathNode& context, std::size_t position, std::size_t size) {
        if (!AttributeOf(instruction, "select").empty()) {
            return Evaluate(AttributeOf(instruction, "select"), context, position, size);
        }
        auto fragment = std::make_unique<Node>();
        fragment->type = Node::Type::Document;
        Node* raw = fragment.get();
        InstantiateChildren(instruction, context, position, size, raw);
        std::vector<XPathNode> nodes;
        for (const std::unique_ptr<Node>& child : fragment->children) {
            nodes.push_back(XPathNode{child.get(), false, "", ""});
            child->parent = nullptr;
        }
        for (std::unique_ptr<Node>& child : fragment->children) {
            owned.push_back(std::move(child));
        }
        fragment->children.clear();
        return XPathNodes(nodes);
    }

    std::vector<std::unique_ptr<Node>> owned;

    void SortNodes(const Node& instruction, std::vector<XPathNode>& nodes) {
        std::vector<const Node*> sorts;
        for (const std::unique_ptr<Node>& child : instruction.children) {
            if (child->type == Node::Type::Element && IsXslt(*child) && LocalOf(*child) == "sort") {
                sorts.push_back(child.get());
            }
        }
        if (sorts.empty()) {
            return;
        }
        std::stable_sort(nodes.begin(), nodes.end(), [&](const XPathNode& left, const XPathNode& right) {
            for (const Node* sort : sorts) {
                const std::string select = AttributeOf(*sort, "select").empty() ? std::string(".") : AttributeOf(*sort, "select");
                const XPathValue leftValue = Evaluate(select, left, 1, 1);
                const XPathValue rightValue = Evaluate(select, right, 1, 1);
                const bool number = AttributeOf(*sort, "data-type") == "number";
                const bool descending = AttributeOf(*sort, "order") == "descending";
                int compare = 0;
                if (number) {
                    const double delta = XPathToNumber(leftValue) - XPathToNumber(rightValue);
                    compare = delta < 0 ? -1 : (delta > 0 ? 1 : 0);
                } else {
                    const std::string leftText = XPathToString(leftValue);
                    const std::string rightText = XPathToString(rightValue);
                    compare = leftText < rightText ? -1 : (leftText > rightText ? 1 : 0);
                }
                if (descending) {
                    compare = -compare;
                }
                if (compare != 0) {
                    return compare < 0;
                }
            }
            return false;
        });
    }

    std::string Expand(const std::string& text, const XPathNode& context, std::size_t position, std::size_t size) {
        std::string output;
        for (std::size_t index = 0; index < text.size(); ++index) {
            if (text[index] == '{' && index + 1 < text.size() && text[index + 1] == '{') {
                output.push_back('{');
                ++index;
                continue;
            }
            if (text[index] == '}' && index + 1 < text.size() && text[index + 1] == '}') {
                output.push_back('}');
                ++index;
                continue;
            }
            if (text[index] == '{') {
                const std::size_t end = text.find('}', index);
                if (end == std::string::npos) {
                    output.push_back(text[index]);
                    continue;
                }
                output += XPathToString(Evaluate(text.substr(index + 1, end - index - 1), context, position, size));
                index = end;
                continue;
            }
            output.push_back(text[index]);
        }
        return output;
    }

    void AppendText(Node* output, const std::string& text) {
        if (text.empty() || output == nullptr) {
            return;
        }
        if (!output->children.empty() && output->children.back()->type == Node::Type::Text) {
            output->children.back()->text += text;
            return;
        }
        auto node = std::make_unique<Node>();
        node->type = Node::Type::Text;
        node->text = text;
        node->parent = output;
        output->children.push_back(std::move(node));
    }

    void CopyDeep(const XPathNode& node, Node* output) {
        if (node.attribute) {
            if (output->type == Node::Type::Element) {
                Attribute attribute;
                attribute.name = node.attributeName;
                attribute.value = node.attributeValue;
                output->attributes.push_back(attribute);
            }
            return;
        }
        if (node.node == nullptr) {
            return;
        }
        if (node.node->type == Node::Type::Text) {
            AppendText(output, node.node->text);
            return;
        }
        if (node.node->type == Node::Type::Comment || node.node->type == Node::Type::ProcessingInstruction) {
            auto copy = std::make_unique<Node>();
            copy->type = node.node->type;
            copy->tagName = node.node->tagName;
            copy->text = node.node->text;
            copy->parent = output;
            output->children.push_back(std::move(copy));
            return;
        }
        if (node.node->type == Node::Type::Document) {
            for (const std::unique_ptr<Node>& child : node.node->children) {
                CopyDeep(XPathNode{child.get(), false, "", ""}, output);
            }
            return;
        }
        auto element = std::make_unique<Node>();
        element->type = Node::Type::Element;
        element->tagName = node.node->tagName;
        element->attributes = node.node->attributes;
        element->parent = output;
        Node* raw = element.get();
        output->children.push_back(std::move(element));
        for (const std::unique_ptr<Node>& child : node.node->children) {
            CopyDeep(XPathNode{child.get(), false, "", ""}, raw);
        }
    }

    std::string FormatNumber(const Node& instruction, const XPathNode& context, std::size_t position, std::size_t size) {
        const double value = AttributeOf(instruction, "value").empty() ? static_cast<double>(position) : XPathToNumber(Evaluate(AttributeOf(instruction, "value"), context, position, size));
        const std::string format = AttributeOf(instruction, "format").empty() ? std::string("1") : AttributeOf(instruction, "format");
        long long number = static_cast<long long>(std::llround(value));
        if (number < 0) {
            number = 0;
        }
        if (format == "a" || format == "A") {
            std::string text;
            long long current = number;
            while (current > 0) {
                --current;
                text.insert(text.begin(), static_cast<char>('a' + current % 26));
                current /= 26;
            }
            if (format == "A") {
                for (char& character : text) {
                    character = static_cast<char>(character - 'a' + 'A');
                }
            }
            return text.empty() ? "a" : text;
        }
        if (format == "i" || format == "I") {
            const int values[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
            const char* numerals[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
            std::string text;
            long long remaining = number <= 0 ? 1 : number;
            for (int index = 0; index < 13; ++index) {
                while (remaining >= values[index]) {
                    text += numerals[index];
                    remaining -= values[index];
                }
            }
            if (format == "I") {
                for (char& character : text) {
                    if (character >= 'a' && character <= 'z') {
                        character = static_cast<char>(character - 'a' + 'A');
                    }
                }
            }
            return text;
        }
        std::string digits = std::to_string(number);
        if (!format.empty() && format.find('0') != std::string::npos && digits.size() < format.size()) {
            digits = std::string(format.size() - digits.size(), '0') + digits;
        }
        return digits;
    }

    XPathValue Extension(const std::string& nameSpace, const std::string& name, const std::vector<XPathValue>& arguments) {
        auto argument = [&](std::size_t slot) {
            return slot < arguments.size() ? arguments[slot] : XPathString("");
        };
        if ((nameSpace.empty() || nameSpace == XSLT_URI) && name == "current") {
            return XPathNodes({environment.current});
        }
        if ((nameSpace.empty() || nameSpace == XSLT_URI) && name == "key") {
            return Key(XPathToString(argument(0)), XPathToString(argument(1)));
        }
        if ((nameSpace.empty() || nameSpace == XSLT_URI) && name == "system-property") {
            const std::string property = XPathToString(argument(0));
            if (property == "xsl:version") {
                return XPathString("1.0");
            }
            if (property == "xsl:vendor") {
                return XPathString("RoboWebCompiler");
            }
            if (property == "xsl:vendor-url") {
                return XPathString("");
            }
            return XPathString("");
        }
        if ((nameSpace.empty() || nameSpace == XSLT_URI) && name == "element-available") {
            const std::string local = LocalOfName(XPathToString(argument(0)));
            const char* known[] = {"apply-imports", "apply-templates", "attribute", "attribute-set", "call-template", "choose", "comment", "copy",
                "copy-of", "decimal-format", "element", "fallback", "for-each", "if", "import", "include", "key", "message", "namespace-alias",
                "number", "otherwise", "output", "param", "preserve-space", "processing-instruction", "sort", "strip-space", "stylesheet", "template",
                "text", "transform", "value-of", "variable", "when", "with-param"};
            for (const char* item : known) {
                if (local == item) {
                    return XPathBoolean(true);
                }
            }
            return XPathBoolean(false);
        }
        if ((nameSpace.empty() || nameSpace == XSLT_URI) && name == "function-available") {
            const std::string local = LocalOfName(XPathToString(argument(0)));
            const char* known[] = {"boolean", "ceiling", "choose", "concat", "contains", "count", "current", "document", "element-available",
                "false", "floor", "format-number", "function-available", "generate-id", "id", "key", "lang", "last", "local-name", "name",
                "namespace-uri", "normalize-space", "not", "number", "position", "round", "starts-with", "string", "string-length", "substring",
                "substring-after", "substring-before", "sum", "system-property", "translate", "true", "unparsed-entity-url", "node-set",
                "object-type", "min", "max", "highest", "lowest", "difference", "distinct", "has-same-node", "intersection", "leading",
                "trailing", "split", "tokenize", "test", "match", "replace"};
            for (const char* item : known) {
                if (local == item) {
                    return XPathBoolean(true);
                }
            }
            return XPathBoolean(false);
        }
        if ((nameSpace.empty() || nameSpace == XSLT_URI) && name == "unparsed-entity-url") {
            const auto found = source.unparsedEntities.find(XPathToString(argument(0)));
            return XPathString(found == source.unparsedEntities.end() ? std::string() : found->second);
        }
        if ((nameSpace.empty() || nameSpace == XSLT_URI) && name == "document") {
            return DocumentFunction(XPathToString(argument(0)));
        }
        if ((nameSpace.empty() || nameSpace == XSLT_URI) && name == "format-number") {
            return XPathString(FormatPicture(XPathToNumber(argument(0)), XPathToString(argument(1)), arguments.size() > 2 ? XPathToString(argument(2)) : std::string()));
        }
        if (nameSpace == EXSL_URI && name == "node-set") {
            if (argument(0).kind == XPathKind::NodeSet) {
                return argument(0);
            }
            return XPathNodes({});
        }
        if (nameSpace == EXSL_URI && name == "object-type") {
            if (argument(0).kind == XPathKind::Boolean) {
                return XPathString("boolean");
            }
            if (argument(0).kind == XPathKind::Number) {
                return XPathString("number");
            }
            if (argument(0).kind == XPathKind::String) {
                return XPathString("string");
            }
            return XPathString("node-set");
        }
        if (nameSpace == MATH_URI && (name == "min" || name == "max" || name == "lowest" || name == "highest")) {
            return Math(name, argument(0));
        }
        if (nameSpace == SET_URI) {
            return Set(name, argument(0), arguments.size() > 1 ? arguments[1] : XPathNodes({}));
        }
        if (nameSpace == STR_URI) {
            return StringFunction(name, arguments);
        }
        if (nameSpace == REGEXP_URI) {
            return Regexp(name, arguments);
        }
        return XPathString("");
    }

    std::string LocalOfName(const std::string& name) const {
        const std::size_t colon = name.find(':');
        return colon == std::string::npos ? name : name.substr(colon + 1);
    }

    XPathValue Key(const std::string& name, const std::string& value) {
        std::vector<XPathNode> found;
        for (const Node* key : keys) {
            if (AttributeOf(*key, "name") != name) {
                continue;
            }
            const std::string match = AttributeOf(*key, "match");
            const std::string use = AttributeOf(*key, "use");
            std::vector<Node*> pending{source.document.root.get()};
            for (std::size_t index = 0; index < pending.size(); ++index) {
                Node* current = pending[index];
                if (current != nullptr && current->type == Node::Type::Element && PatternMatches(XPathNode{current, false, "", ""}, match)) {
                    const XPathValue used = Evaluate(use, XPathNode{current, false, "", ""}, 1, 1);
                    if (XPathToString(used) == value) {
                        found.push_back(XPathNode{current, false, "", ""});
                    }
                }
                if (current != nullptr) {
                    for (const std::unique_ptr<Node>& child : current->children) {
                        pending.push_back(child.get());
                    }
                }
            }
        }
        return XPathNodes(found);
    }

    XPathValue DocumentFunction(const std::string& href) {
        if (href.empty()) {
            return XPathNodes({XPathNode{sheet.document.root.get(), false, "", ""}});
        }
        std::ifstream input(href);
        if (!input) {
            return XPathNodes({});
        }
        std::stringstream buffer;
        buffer << input.rdbuf();
        Diagnostics diagnostics;
        auto parsed = std::make_unique<XmlParseResult>(ParseXml(buffer.str(), href, diagnostics));
        if (!parsed->wellFormed) {
            return XPathNodes({});
        }
        Node* root = parsed->document.root.get();
        documents.push_back(std::move(parsed));
        return XPathNodes({XPathNode{root, false, "", ""}});
    }

    std::vector<std::unique_ptr<XmlParseResult>> documents;

    std::string FormatPicture(double number, const std::string& picture, const std::string& formatName) {
        DecimalFormat format;
        for (const DecimalFormat& candidate : formats) {
            if (candidate.name == formatName) {
                format = candidate;
            }
        }
        const bool negative = number < 0;
        double absolute = std::fabs(number);
        int fraction = 0;
        const std::size_t dot = picture.find(format.decimal);
        if (dot != std::string::npos) {
            for (std::size_t index = dot + 1; index < picture.size() && (picture[index] == '0' || picture[index] == '#'); ++index) {
                ++fraction;
            }
        }
        double scale = 1;
        for (int index = 0; index < fraction; ++index) {
            scale *= 10;
        }
        absolute = std::floor(absolute * scale + 0.5) / scale;
        const long long whole = static_cast<long long>(std::floor(absolute));
        std::string digits = std::to_string(whole);
        if (picture.find(format.grouping) != std::string::npos && digits.size() > 3) {
            std::string grouped;
            int count = 0;
            for (int index = static_cast<int>(digits.size()) - 1; index >= 0; --index) {
                if (count == 3) {
                    grouped.insert(grouped.begin(), format.grouping);
                    count = 0;
                }
                grouped.insert(grouped.begin(), digits[static_cast<std::size_t>(index)]);
                ++count;
            }
            digits = grouped;
        }
        if (fraction > 0) {
            double fractional = absolute - static_cast<double>(whole);
            digits.push_back(format.decimal);
            for (int index = 0; index < fraction; ++index) {
                fractional *= 10;
                const int digit = static_cast<int>(std::floor(fractional + 1e-8));
                digits.push_back(static_cast<char>('0' + digit));
                fractional -= digit;
            }
        }
        if (negative) {
            digits.insert(digits.begin(), '-');
        }
        return digits;
    }

    XPathValue Math(const std::string& name, const XPathValue& value) {
        if (value.kind != XPathKind::NodeSet || value.nodes.empty()) {
            return name == "min" || name == "max" ? XPathNumber(std::nan("")) : XPathNodes({});
        }
        double extreme = XPathToNumber(XPathString(XPathStringValue(value.nodes.front())));
        for (const XPathNode& node : value.nodes) {
            const double number = XPathToNumber(XPathString(XPathStringValue(node)));
            if ((name == "min" || name == "lowest") && number < extreme) {
                extreme = number;
            }
            if ((name == "max" || name == "highest") && number > extreme) {
                extreme = number;
            }
        }
        if (name == "min" || name == "max") {
            return XPathNumber(extreme);
        }
        std::vector<XPathNode> selected;
        for (const XPathNode& node : value.nodes) {
            if (XPathToNumber(XPathString(XPathStringValue(node))) == extreme) {
                selected.push_back(node);
            }
        }
        return XPathNodes(selected);
    }

    XPathValue Set(const std::string& name, const XPathValue& first, const XPathValue& second) {
        const std::vector<XPathNode> left = first.kind == XPathKind::NodeSet ? first.nodes : std::vector<XPathNode>{};
        const std::vector<XPathNode> right = second.kind == XPathKind::NodeSet ? second.nodes : std::vector<XPathNode>{};
        if (name == "has-same-node") {
            for (const XPathNode& node : left) {
                for (const XPathNode& other : right) {
                    if (SameXPathNode(node, other)) {
                        return XPathBoolean(true);
                    }
                }
            }
            return XPathBoolean(false);
        }
        if (name == "intersection") {
            std::vector<XPathNode> selected;
            for (const XPathNode& node : left) {
                for (const XPathNode& other : right) {
                    if (SameXPathNode(node, other)) {
                        selected.push_back(node);
                    }
                }
            }
            return XPathNodes(selected);
        }
        if (name == "difference") {
            std::vector<XPathNode> selected;
            for (const XPathNode& node : left) {
                bool found = false;
                for (const XPathNode& other : right) {
                    if (SameXPathNode(node, other)) {
                        found = true;
                    }
                }
                if (!found) {
                    selected.push_back(node);
                }
            }
            return XPathNodes(selected);
        }
        if (name == "distinct") {
            std::vector<XPathNode> selected;
            for (const XPathNode& node : left) {
                bool found = false;
                for (const XPathNode& other : selected) {
                    if (XPathStringValue(node) == XPathStringValue(other)) {
                        found = true;
                    }
                }
                if (!found) {
                    selected.push_back(node);
                }
            }
            return XPathNodes(selected);
        }
        if (name == "leading" || name == "trailing") {
            std::vector<XPathNode> selected;
            bool seen = false;
            for (const XPathNode& node : left) {
                bool marker = false;
                for (const XPathNode& other : right) {
                    if (SameXPathNode(node, other)) {
                        marker = true;
                    }
                }
                if (name == "leading") {
                    if (marker) {
                        break;
                    }
                    selected.push_back(node);
                } else {
                    if (seen) {
                        selected.push_back(node);
                    }
                    if (marker) {
                        seen = true;
                    }
                }
            }
            return XPathNodes(selected);
        }
        return XPathNodes({});
    }

    XPathValue StringFunction(const std::string& name, const std::vector<XPathValue>& arguments) {
        if (name == "concat") {
            std::string text;
            if (!arguments.empty() && arguments[0].kind == XPathKind::NodeSet) {
                for (const XPathNode& node : arguments[0].nodes) {
                    text += XPathStringValue(node);
                }
            }
            return XPathString(text);
        }
        const std::string text = arguments.empty() ? std::string() : XPathToString(arguments[0]);
        std::string pattern = arguments.size() > 1 ? XPathToString(arguments[1]) : std::string(" ");
        if (pattern.empty()) {
            pattern = " ";
        }
        std::vector<std::string> parts;
        if (name == "tokenize") {
            std::string current;
            for (char character : text) {
                if (pattern.find(character) != std::string::npos) {
                    if (!current.empty()) {
                        parts.push_back(current);
                    }
                    current.clear();
                } else {
                    current.push_back(character);
                }
            }
            if (!current.empty()) {
                parts.push_back(current);
            }
        } else if (name == "split") {
            std::size_t start = 0;
            while (start <= text.size()) {
                const std::size_t at = text.find(pattern, start);
                if (at == std::string::npos) {
                    parts.push_back(text.substr(start));
                    break;
                }
                parts.push_back(text.substr(start, at - start));
                start = at + pattern.size();
            }
        } else {
            return XPathString("");
        }
        std::vector<XPathNode> nodes;
        for (const std::string& part : parts) {
            auto element = std::make_unique<Node>();
            element->type = Node::Type::Element;
            element->tagName = "token";
            auto textNode = std::make_unique<Node>();
            textNode->type = Node::Type::Text;
            textNode->text = part;
            textNode->parent = element.get();
            element->children.push_back(std::move(textNode));
            nodes.push_back(XPathNode{element.get(), false, "", ""});
            owned.push_back(std::move(element));
        }
        return XPathNodes(nodes);
    }

    XPathValue Regexp(const std::string& name, const std::vector<XPathValue>& arguments) {
        const std::string text = arguments.empty() ? std::string() : XPathToString(arguments[0]);
        const std::string pattern = arguments.size() > 1 ? XPathToString(arguments[1]) : std::string();
        try {
            const std::regex expression(pattern);
            if (name == "test") {
                return XPathBoolean(std::regex_search(text, expression));
            }
            if (name == "replace") {
                const std::string replacement = arguments.size() > 3 ? XPathToString(arguments[3]) : (arguments.size() > 2 ? XPathToString(arguments[2]) : std::string());
                return XPathString(std::regex_replace(text, expression, replacement));
            }
            if (name == "match") {
                std::vector<XPathNode> nodes;
                auto begin = std::sregex_iterator(text.begin(), text.end(), expression);
                for (auto match = begin; match != std::sregex_iterator(); ++match) {
                    auto element = std::make_unique<Node>();
                    element->type = Node::Type::Element;
                    element->tagName = "match";
                    auto textNode = std::make_unique<Node>();
                    textNode->type = Node::Type::Text;
                    textNode->text = match->str();
                    textNode->parent = element.get();
                    element->children.push_back(std::move(textNode));
                    nodes.push_back(XPathNode{element.get(), false, "", ""});
                    owned.push_back(std::move(element));
                }
                return XPathNodes(nodes);
            }
        } catch (const std::regex_error&) {
            return name == "test" ? XPathBoolean(false) : XPathString("");
        }
        return XPathString("");
    }
};

}  // namespace

XsltResult TransformXml(const std::string& sourceText, const std::string& stylesheetText, const std::string& stylesheetPath) {
    Diagnostics sourceDiagnostics;
    Diagnostics sheetDiagnostics;
    Transformer transformer;
    transformer.source = ParseXml(sourceText, "source.xml", sourceDiagnostics);
    transformer.sheet = ParseXml(stylesheetText, stylesheetPath.empty() ? "sheet.xsl" : stylesheetPath, sheetDiagnostics);
    transformer.sheetPath = stylesheetPath;
    transformer.Run();
    return transformer.result;
}

}
