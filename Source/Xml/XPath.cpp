#include "Xml/XPath.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>

namespace RoboWeb {

namespace {

bool IsSpace(char character) {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

std::string LocalNameOf(const std::string& qualified) {
    const std::size_t colon = qualified.find(':');
    return colon == std::string::npos ? qualified : qualified.substr(colon + 1);
}

std::string PrefixOf(const std::string& qualified) {
    const std::size_t colon = qualified.find(':');
    return colon == std::string::npos ? std::string() : qualified.substr(0, colon);
}

const Node* DocumentRoot(const Node* node) {
    const Node* cursor = node;
    while (cursor != nullptr && cursor->parent != nullptr) {
        cursor = cursor->parent;
    }
    return cursor;
}

void CollectOrder(const Node* node, std::map<const Node*, int>& order, int& next) {
    if (node == nullptr) {
        return;
    }
    order[node] = next++;
    for (const std::unique_ptr<Node>& child : node->children) {
        CollectOrder(child.get(), order, next);
    }
}

int NodeRank(const XPathNode& node, const std::map<const Node*, int>& order) {
    const auto found = order.find(node.node);
    return found == order.end() ? 0 : found->second;
}

int AttributeRank(const XPathNode& node) {
    if (!node.attribute || node.node == nullptr) {
        return 0;
    }
    for (std::size_t index = 0; index < node.node->attributes.size(); ++index) {
        if (node.node->attributes[index].name == node.attributeName) {
            return static_cast<int>(index) + 1;
        }
    }
    return 1;
}

bool DocumentBefore(const XPathNode& left, const XPathNode& right, const std::map<const Node*, int>& order) {
    const int leftRank = NodeRank(left, order);
    const int rightRank = NodeRank(right, order);
    if (leftRank != rightRank) {
        return leftRank < rightRank;
    }
    return AttributeRank(left) < AttributeRank(right);
}

class Parser {
public:
    const std::string& text;
    std::size_t index = 0;
    const XPathEnvironment& environment;
    bool failed = false;

    explicit Parser(const std::string& text, const XPathEnvironment& environment) : text(text), environment(environment) {}

    void Skip() {
        while (index < text.size() && IsSpace(text[index])) {
            ++index;
        }
    }

    bool Consume(const char* token) {
        Skip();
        const std::size_t length = std::char_traits<char>::length(token);
        if (text.compare(index, length, token) != 0) {
            return false;
        }
        const bool word = std::isalpha(static_cast<unsigned char>(token[0]));
        if (word && index + length < text.size()) {
            const unsigned char next = static_cast<unsigned char>(text[index + length]);
            if (std::isalnum(next) || next == '_' || next == '-' || next == ':') {
                return false;
            }
        }
        index += length;
        return true;
    }

    XPathValue Parse(const XPathNode& context, std::size_t position, std::size_t size) {
        XPathValue value = ParseOr(context, position, size);
        Skip();
        if (index < text.size()) {
            failed = true;
        }
        return value;
    }

    XPathValue ParseOr(const XPathNode& context, std::size_t position, std::size_t size) {
        XPathValue value = ParseAnd(context, position, size);
        while (Consume("or")) {
            const XPathValue right = ParseAnd(context, position, size);
            value = XPathBoolean(XPathToBoolean(value) || XPathToBoolean(right));
        }
        return value;
    }

    XPathValue ParseAnd(const XPathNode& context, std::size_t position, std::size_t size) {
        XPathValue value = ParseEquality(context, position, size);
        while (Consume("and")) {
            const XPathValue right = ParseEquality(context, position, size);
            value = XPathBoolean(XPathToBoolean(value) && XPathToBoolean(right));
        }
        return value;
    }

    XPathValue ParseEquality(const XPathNode& context, std::size_t position, std::size_t size) {
        XPathValue value = ParseRelational(context, position, size);
        while (true) {
            if (Consume("=")) {
                value = Compare(value, ParseRelational(context, position, size), true);
            } else if (Consume("!=")) {
                value = Compare(value, ParseRelational(context, position, size), false);
            } else {
                break;
            }
        }
        return value;
    }

    XPathValue ParseRelational(const XPathNode& context, std::size_t position, std::size_t size) {
        XPathValue value = ParseAdditive(context, position, size);
        while (true) {
            int operation = 0;
            if (Consume("<=")) {
                operation = 1;
            } else if (Consume(">=")) {
                operation = 2;
            } else if (Consume("<")) {
                operation = 3;
            } else if (Consume(">")) {
                operation = 4;
            } else {
                break;
            }
            value = Relate(value, ParseAdditive(context, position, size), operation);
        }
        return value;
    }

    XPathValue ParseAdditive(const XPathNode& context, std::size_t position, std::size_t size) {
        XPathValue value = ParseMultiplicative(context, position, size);
        while (true) {
            int sign = 0;
            if (Consume("+")) {
                sign = 1;
            } else if (Consume("-")) {
                sign = -1;
            } else {
                break;
            }
            const double right = XPathToNumber(ParseMultiplicative(context, position, size));
            value = XPathNumber(XPathToNumber(value) + sign * right);
        }
        return value;
    }

    XPathValue ParseMultiplicative(const XPathNode& context, std::size_t position, std::size_t size) {
        XPathValue value = ParseUnary(context, position, size);
        while (true) {
            int operation = 0;
            if (Consume("*")) {
                operation = 1;
            } else if (Consume("div")) {
                operation = 2;
            } else if (Consume("mod")) {
                operation = 3;
            } else {
                break;
            }
            const double right = XPathToNumber(ParseUnary(context, position, size));
            const double left = XPathToNumber(value);
            if (operation == 1) {
                value = XPathNumber(left * right);
            } else if (operation == 2) {
                value = XPathNumber(right == 0 ? std::nan("") : left / right);
            } else {
                value = XPathNumber(right == 0 ? std::nan("") : std::fmod(left, right));
            }
        }
        return value;
    }

    XPathValue ParseUnary(const XPathNode& context, std::size_t position, std::size_t size) {
        int sign = 1;
        while (Consume("-")) {
            sign = -sign;
        }
        XPathValue value = ParseUnion(context, position, size);
        if (sign < 0) {
            value = XPathNumber(-XPathToNumber(value));
        }
        return value;
    }

    XPathValue ParseUnion(const XPathNode& context, std::size_t position, std::size_t size) {
        XPathValue value = ParsePath(context, position, size);
        while (Consume("|")) {
            XPathValue right = ParsePath(context, position, size);
            if (value.kind != XPathKind::NodeSet) {
                value = XPathNodes({});
            }
            if (right.kind == XPathKind::NodeSet) {
                value.nodes.insert(value.nodes.end(), right.nodes.begin(), right.nodes.end());
            }
            SortUnique(value.nodes, context.node);
        }
        return value;
    }

    XPathValue ParsePath(const XPathNode& context, std::size_t position, std::size_t size) {
        Skip();
        if (Consume("$")) {
            const std::string name = ReadName();
            const auto found = environment.variables.find(name);
            XPathValue value = found == environment.variables.end() ? XPathString("") : found->second;
            return ContinuePath(ApplyPredicates(value), context, position, size);
        }
        if (index < text.size() && (text[index] == '"' || text[index] == '\'')) {
            return XPathString(ReadLiteral());
        }
        if (index < text.size() && (std::isdigit(static_cast<unsigned char>(text[index])) || text[index] == '.')) {
            const std::size_t saved = index;
            const std::string number = ReadNumber();
            if (!number.empty() && number != ".") {
                return XPathNumber(std::atof(number.c_str()));
            }
            index = saved;
        }
        if (Consume("(")) {
            XPathValue value = ParseOr(context, position, size);
            Consume(")");
            return ContinuePath(ApplyPredicates(value), context, position, size);
        }
        bool absolute = false;
        bool descendant = false;
        if (Consume("//")) {
            absolute = true;
            descendant = true;
        } else if (Consume("/")) {
            absolute = true;
        }
        std::vector<XPathNode> nodes;
        if (absolute) {
            const Node* root = DocumentRoot(context.node);
            if (root != nullptr) {
                nodes.push_back(XPathNode{const_cast<Node*>(root), false, "", ""});
            }
            if (descendant) {
                nodes = Axis(nodes, "descendant-or-self", "node", "", context.node);
            }
        } else {
            nodes.push_back(context);
        }
        bool needSlash = false;
        while (true) {
            Skip();
            if (needSlash) {
                if (Consume("//")) {
                    nodes = Axis(nodes, "descendant-or-self", "node", "", context.node);
                } else if (!Consume("/")) {
                    break;
                }
            }
            if (LookaheadStop()) {
                break;
            }
            std::string axis = "child";
            std::string nodeTest;
            std::string argument;
            if (Consume("@")) {
                axis = "attribute";
                nodeTest = ReadName();
                if (nodeTest.empty()) {
                    nodeTest = "*";
                }
            } else if (Consume(".")) {
                if (Consume(".")) {
                    axis = "parent";
                    nodeTest = "node";
                } else {
                    axis = "self";
                    nodeTest = "node";
                }
            } else {
                const std::size_t saved = index;
                const std::string name = ReadName();
                if (Consume("::")) {
                    axis = name;
                    if (Consume("processing-instruction")) {
                        nodeTest = "processing-instruction";
                        Consume("(");
                        if (index < text.size() && (text[index] == '"' || text[index] == '\'')) {
                            argument = ReadLiteral();
                        }
                        Consume(")");
                    } else if (Consume("text") || name == "text") {
                        nodeTest = "text";
                        Consume("(");
                        Consume(")");
                    } else if (Consume("comment")) {
                        nodeTest = "comment";
                        Consume("(");
                        Consume(")");
                    } else if (Consume("node")) {
                        nodeTest = "node";
                        Consume("(");
                        Consume(")");
                    } else {
                        nodeTest = ReadName();
                    }
                } else if (name == "text" || name == "comment" || name == "node" || name == "processing-instruction") {
                    index = saved;
                    ReadName();
                    nodeTest = name;
                    Consume("(");
                    if (name == "processing-instruction" && index < text.size() && (text[index] == '"' || text[index] == '\'')) {
                        argument = ReadLiteral();
                    }
                    Consume(")");
                } else if (Consume("(")) {
                    index = saved;
                    return ContinuePath(ApplyPredicates(CallFunction(context, position, size)), context, position, size);
                } else {
                    nodeTest = name.empty() ? "*" : name;
                }
            }
            if (nodeTest.empty() && axis != "parent" && axis != "self") {
                failed = true;
                break;
            }
            if (nodeTest.empty()) {
                nodeTest = "node";
            }
            nodes = Axis(nodes, axis, nodeTest, argument, context.node);
            nodes = FilterPredicates(nodes);
            needSlash = true;
            Skip();
            if (index >= text.size() || text[index] != '/') {
                break;
            }
        }
        SortUnique(nodes, context.node);
        return XPathNodes(nodes);
    }

    XPathValue ContinuePath(XPathValue value, const XPathNode& context, std::size_t position, std::size_t size) {
        (void)position;
        (void)size;
        if (value.kind != XPathKind::NodeSet) {
            return value;
        }
        while (true) {
            Skip();
            if (index >= text.size() || (text[index] != '/')) {
                break;
            }
            const bool descendant = Consume("//");
            if (!descendant) {
                Consume("/");
            }
            if (descendant) {
                value.nodes = Axis(value.nodes, "descendant-or-self", "node", "", context.node);
            }
            if (LookaheadStop()) {
                break;
            }
            std::string axis = "child";
            std::string nodeTest;
            std::string argument;
            if (Consume("@")) {
                axis = "attribute";
                nodeTest = ReadName();
                if (nodeTest.empty()) {
                    nodeTest = "*";
                }
            } else if (Consume(".")) {
                if (Consume(".")) {
                    axis = "parent";
                    nodeTest = "node";
                } else {
                    axis = "self";
                    nodeTest = "node";
                }
            } else {
                const std::string name = ReadName();
                if (Consume("::")) {
                    axis = name;
                    nodeTest = ReadName();
                } else {
                    nodeTest = name.empty() ? "*" : name;
                }
            }
            value.nodes = Axis(value.nodes, axis, nodeTest, argument, context.node);
            value.nodes = FilterPredicates(value.nodes);
        }
        return value;
    }

    bool LookaheadStop() {
        Skip();
        if (index >= text.size()) {
            return true;
        }
        const char character = text[index];
        return character == ')' || character == ']' || character == '|' || character == '+' || character == '=' || character == '!' ||
            character == '<' || character == '>' || character == ',';
    }

    std::vector<XPathNode> FilterPredicates(std::vector<XPathNode> nodes) {
        while (Consume("[")) {
            const std::string predicate = ReadPredicate();
            std::vector<XPathNode> kept;
            Parser nested(predicate, environment);
            for (std::size_t cursor = 0; cursor < nodes.size(); ++cursor) {
                XPathValue value = nested.Parse(nodes[cursor], cursor + 1, nodes.size());
                nested.index = 0;
                nested.failed = false;
                bool match = false;
                if (value.kind == XPathKind::Number) {
                    match = static_cast<std::size_t>(std::lround(value.numberValue)) == cursor + 1 || value.numberValue == static_cast<double>(cursor + 1);
                } else {
                    match = XPathToBoolean(value);
                }
                if (match) {
                    kept.push_back(nodes[cursor]);
                }
            }
            nodes = std::move(kept);
        }
        return nodes;
    }

    XPathValue ApplyPredicates(XPathValue value) {
        if (!Consume("[")) {
            return value;
        }
        index -= 1;
        if (value.kind != XPathKind::NodeSet) {
            return value;
        }
        value.nodes = FilterPredicates(value.nodes);
        return value;
    }

    std::string ReadPredicate() {
        std::string body;
        int depth = 1;
        while (index < text.size() && depth > 0) {
            if (text[index] == '[') {
                ++depth;
            } else if (text[index] == ']') {
                --depth;
                if (depth == 0) {
                    ++index;
                    break;
                }
            }
            body.push_back(text[index]);
            ++index;
        }
        return body;
    }

    void ReadNameToken(std::string& name) {
        while (index < text.size()) {
            const unsigned char character = static_cast<unsigned char>(text[index]);
            if (std::isalnum(character) || text[index] == '_' || text[index] == '-' || text[index] == '.') {
                name.push_back(text[index]);
                ++index;
            } else {
                break;
            }
        }
    }

    std::string ReadName() {
        Skip();
        std::string name;
        if (index >= text.size()) {
            return name;
        }
        if (text[index] == '*') {
            ++index;
            return "*";
        }
        if (!(std::isalpha(static_cast<unsigned char>(text[index])) || text[index] == '_')) {
            return name;
        }
        ReadNameToken(name);
        if (index + 1 < text.size() && text[index] == ':' && text[index + 1] != ':') {
            name.push_back(':');
            ++index;
            if (index < text.size() && text[index] == '*') {
                name.push_back('*');
                ++index;
            } else {
                ReadNameToken(name);
            }
        }
        return name;
    }

    std::string ReadLiteral() {
        const char quote = text[index++];
        std::string value;
        while (index < text.size() && text[index] != quote) {
            value.push_back(text[index]);
            ++index;
        }
        if (index < text.size()) {
            ++index;
        }
        return value;
    }

    std::string ReadNumber() {
        Skip();
        std::string number;
        while (index < text.size() && (std::isdigit(static_cast<unsigned char>(text[index])) || text[index] == '.')) {
            number.push_back(text[index]);
            ++index;
        }
        return number;
    }

    std::vector<XPathNode> Axis(
        const std::vector<XPathNode>& contexts, const std::string& axis, const std::string& nodeTest, const std::string& argument, Node* orderRoot) {
        std::vector<XPathNode> result;
        for (const XPathNode& context : contexts) {
            std::vector<XPathNode> gathered;
            Gather(context, axis, gathered);
            for (const XPathNode& candidate : gathered) {
                if (Matches(candidate, axis, nodeTest, argument)) {
                    result.push_back(candidate);
                }
            }
        }
        (void)orderRoot;
        return result;
    }

    void Gather(const XPathNode& context, const std::string& axis, std::vector<XPathNode>& output) {
        Node* node = context.node;
        if (node == nullptr) {
            return;
        }
        if (axis == "self") {
            output.push_back(context.attribute ? context : XPathNode{node, false, "", ""});
            return;
        }
        if (axis == "child") {
            for (const std::unique_ptr<Node>& child : node->children) {
                output.push_back(XPathNode{child.get(), false, "", ""});
            }
            return;
        }
        if (axis == "parent") {
            if (context.attribute) {
                output.push_back(XPathNode{node, false, "", ""});
            } else if (node->parent != nullptr) {
                output.push_back(XPathNode{node->parent, false, "", ""});
            }
            return;
        }
        if (axis == "attribute") {
            if (node->type == Node::Type::Element) {
                for (const Attribute& attribute : node->attributes) {
                    output.push_back(XPathNode{node, true, attribute.name, attribute.value});
                }
            }
            return;
        }
        if (axis == "namespace") {
            if (node->type != Node::Type::Element) {
                return;
            }
            for (const Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
                if (cursor->type != Node::Type::Element) {
                    continue;
                }
                for (const Attribute& attribute : cursor->attributes) {
                    if (attribute.name == "xmlns" || attribute.name.compare(0, 6, "xmlns:") == 0) {
                        bool seen = false;
                        for (const XPathNode& existing : output) {
                            if (existing.attributeName == attribute.name) {
                                seen = true;
                            }
                        }
                        if (!seen) {
                            output.push_back(XPathNode{node, true, attribute.name, attribute.value});
                        }
                    }
                }
            }
            return;
        }
        if (axis == "ancestor" || axis == "ancestor-or-self") {
            if (axis == "ancestor-or-self" && !context.attribute) {
                output.push_back(XPathNode{node, false, "", ""});
            }
            for (Node* cursor = context.attribute ? node : node->parent; cursor != nullptr; cursor = cursor->parent) {
                output.push_back(XPathNode{cursor, false, "", ""});
            }
            return;
        }
        if (axis == "descendant" || axis == "descendant-or-self") {
            if (axis == "descendant-or-self" && !context.attribute) {
                output.push_back(XPathNode{node, false, "", ""});
            }
            std::vector<Node*> pending;
            for (const std::unique_ptr<Node>& child : node->children) {
                pending.push_back(child.get());
            }
            for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
                output.push_back(XPathNode{pending[cursor], false, "", ""});
                for (const std::unique_ptr<Node>& child : pending[cursor]->children) {
                    pending.push_back(child.get());
                }
            }
            return;
        }
        if (axis == "following-sibling" || axis == "preceding-sibling") {
            if (context.attribute || node->parent == nullptr) {
                return;
            }
            bool seen = false;
            std::vector<XPathNode> siblings;
            for (const std::unique_ptr<Node>& child : node->parent->children) {
                if (child.get() == node) {
                    seen = true;
                    if (axis == "preceding-sibling") {
                        break;
                    }
                    continue;
                }
                if (axis == "following-sibling" && seen) {
                    siblings.push_back(XPathNode{child.get(), false, "", ""});
                }
                if (axis == "preceding-sibling" && !seen) {
                    siblings.push_back(XPathNode{child.get(), false, "", ""});
                }
            }
            if (axis == "preceding-sibling") {
                std::reverse(siblings.begin(), siblings.end());
            }
            output.insert(output.end(), siblings.begin(), siblings.end());
            return;
        }
        if (axis == "following" || axis == "preceding") {
            const Node* root = DocumentRoot(node);
            std::vector<XPathNode> all;
            std::vector<const Node*> pending;
            if (root != nullptr) {
                pending.push_back(root);
            }
            for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
                if (pending[cursor] != root) {
                    all.push_back(XPathNode{const_cast<Node*>(pending[cursor]), false, "", ""});
                }
                for (const std::unique_ptr<Node>& child : pending[cursor]->children) {
                    pending.push_back(child.get());
                }
            }
            bool after = false;
            std::vector<XPathNode> selected;
            for (const XPathNode& candidate : all) {
                if (candidate.node == node) {
                    after = true;
                    continue;
                }
                if (axis == "following" && after && !IsAncestor(node, candidate.node) && !IsAncestor(candidate.node, node)) {
                    selected.push_back(candidate);
                }
            }
            if (axis == "preceding") {
                for (const XPathNode& candidate : all) {
                    if (candidate.node == node) {
                        break;
                    }
                    if (!IsAncestor(candidate.node, node)) {
                        selected.push_back(candidate);
                    }
                }
                std::reverse(selected.begin(), selected.end());
            }
            output.insert(output.end(), selected.begin(), selected.end());
        }
    }

    bool IsAncestor(const Node* ancestor, const Node* node) const {
        for (const Node* cursor = node == nullptr ? nullptr : node->parent; cursor != nullptr; cursor = cursor->parent) {
            if (cursor == ancestor) {
                return true;
            }
        }
        return false;
    }

    bool Matches(const XPathNode& candidate, const std::string& axis, const std::string& nodeTest, const std::string& argument) const {
        if (nodeTest == "node") {
            return true;
        }
        if (axis == "attribute" || axis == "namespace" || candidate.attribute) {
            if (nodeTest == "text" || nodeTest == "comment" || nodeTest == "processing-instruction") {
                return false;
            }
            return nodeTest == "*" || candidate.attributeName == nodeTest;
        }
        if (nodeTest == "text") {
            return candidate.node != nullptr && candidate.node->type == Node::Type::Text;
        }
        if (nodeTest == "comment") {
            return candidate.node != nullptr && candidate.node->type == Node::Type::Comment;
        }
        if (nodeTest == "processing-instruction") {
            if (candidate.node == nullptr || candidate.node->type != Node::Type::ProcessingInstruction) {
                return false;
            }
            return argument.empty() || candidate.node->tagName == argument;
        }
        if (candidate.node == nullptr || candidate.node->type != Node::Type::Element) {
            return false;
        }
        if (nodeTest == "*") {
            return true;
        }
        if (nodeTest.size() > 2 && nodeTest.compare(nodeTest.size() - 2, 2, ":*") == 0) {
            return PrefixOf(candidate.node->tagName) == nodeTest.substr(0, nodeTest.size() - 2);
        }
        return candidate.node->tagName == nodeTest;
    }

    XPathValue CallFunction(const XPathNode& context, std::size_t position, std::size_t size) {
        const std::string name = ReadName();
        Consume("(");
        std::vector<XPathValue> arguments;
        Skip();
        if (!Consume(")")) {
            while (true) {
                arguments.push_back(ParseOr(context, position, size));
                if (Consume(",")) {
                    continue;
                }
                Consume(")");
                break;
            }
        }
        const std::string prefix = PrefixOf(name);
        const std::string local = LocalNameOf(name);
        if (!prefix.empty()) {
            const auto found = environment.namespaces.find(prefix);
            const std::string nameSpace = found == environment.namespaces.end() ? std::string() : found->second;
            if (environment.extension) {
                return environment.extension(nameSpace, local, arguments);
            }
            return XPathString("");
        }
        return CoreFunction(local, arguments, context, position, size);
    }

    XPathValue CoreFunction(
        const std::string& name, const std::vector<XPathValue>& arguments, const XPathNode& context, std::size_t position, std::size_t size) {
        auto argument = [&](std::size_t slot) {
            return slot < arguments.size() ? arguments[slot] : XPathString("");
        };
        if (name == "true") {
            return XPathBoolean(true);
        }
        if (name == "false") {
            return XPathBoolean(false);
        }
        if (name == "boolean") {
            return XPathBoolean(XPathToBoolean(argument(0)));
        }
        if (name == "not") {
            return XPathBoolean(!XPathToBoolean(argument(0)));
        }
        if (name == "number") {
            return XPathNumber(arguments.empty() ? XPathToNumber(XPathNodes({context})) : XPathToNumber(argument(0)));
        }
        if (name == "string") {
            return XPathString(arguments.empty() ? XPathStringValue(context) : XPathToString(argument(0)));
        }
        if (name == "floor") {
            return XPathNumber(std::floor(XPathToNumber(argument(0))));
        }
        if (name == "ceiling") {
            return XPathNumber(std::ceil(XPathToNumber(argument(0))));
        }
        if (name == "round") {
            const double value = XPathToNumber(argument(0));
            if (std::isnan(value) || std::isinf(value)) {
                return XPathNumber(value);
            }
            return XPathNumber(std::floor(value + 0.5));
        }
        if (name == "concat") {
            std::string text;
            for (const XPathValue& value : arguments) {
                text += XPathToString(value);
            }
            return XPathString(text);
        }
        if (name == "contains") {
            return XPathBoolean(XPathToString(argument(0)).find(XPathToString(argument(1))) != std::string::npos);
        }
        if (name == "starts-with") {
            const std::string text = XPathToString(argument(0));
            const std::string prefix = XPathToString(argument(1));
            return XPathBoolean(text.compare(0, prefix.size(), prefix) == 0);
        }
        if (name == "string-length") {
            const std::string text = arguments.empty() ? XPathStringValue(context) : XPathToString(argument(0));
            return XPathNumber(static_cast<double>(text.size()));
        }
        if (name == "substring") {
            return XPathString(Substring(XPathToString(argument(0)), XPathToNumber(argument(1)), arguments.size() >= 3 ? XPathToNumber(argument(2)) : std::nan("")));
        }
        if (name == "substring-before") {
            const std::string text = XPathToString(argument(0));
            const std::size_t at = text.find(XPathToString(argument(1)));
            return XPathString(at == std::string::npos ? std::string() : text.substr(0, at));
        }
        if (name == "substring-after") {
            const std::string text = XPathToString(argument(0));
            const std::string mark = XPathToString(argument(1));
            const std::size_t at = text.find(mark);
            return XPathString(at == std::string::npos ? std::string() : text.substr(at + mark.size()));
        }
        if (name == "normalize-space") {
            std::string text = arguments.empty() ? XPathStringValue(context) : XPathToString(argument(0));
            std::string output;
            bool pending = false;
            for (char character : text) {
                if (IsSpace(character)) {
                    pending = !output.empty();
                } else {
                    if (pending) {
                        output.push_back(' ');
                    }
                    pending = false;
                    output.push_back(character);
                }
            }
            return XPathString(output);
        }
        if (name == "translate") {
            const std::string text = XPathToString(argument(0));
            const std::string from = XPathToString(argument(1));
            const std::string to = XPathToString(argument(2));
            std::string output;
            for (char character : text) {
                const std::size_t at = from.find(character);
                if (at == std::string::npos) {
                    output.push_back(character);
                } else if (at < to.size()) {
                    output.push_back(to[at]);
                }
            }
            return XPathString(output);
        }
        if (name == "position") {
            return XPathNumber(static_cast<double>(position));
        }
        if (name == "last") {
            return XPathNumber(static_cast<double>(size));
        }
        if (name == "count") {
            return XPathNumber(argument(0).kind == XPathKind::NodeSet ? static_cast<double>(argument(0).nodes.size()) : 0);
        }
        if (name == "sum") {
            double total = 0;
            if (argument(0).kind == XPathKind::NodeSet) {
                for (const XPathNode& node : argument(0).nodes) {
                    total += XPathToNumber(XPathString(XPathStringValue(node)));
                }
            }
            return XPathNumber(total);
        }
        if (name == "local-name") {
            const XPathNode node = arguments.empty() ? context : (argument(0).nodes.empty() ? XPathNode{} : argument(0).nodes.front());
            if (node.attribute) {
                return XPathString(LocalNameOf(node.attributeName));
            }
            return XPathString(node.node == nullptr ? std::string() : LocalNameOf(node.node->tagName));
        }
        if (name == "name") {
            const XPathNode node = arguments.empty() ? context : (argument(0).nodes.empty() ? XPathNode{} : argument(0).nodes.front());
            if (node.attribute) {
                return XPathString(node.attributeName);
            }
            return XPathString(node.node != nullptr && node.node->type == Node::Type::Element ? node.node->tagName : std::string());
        }
        if (name == "namespace-uri") {
            const XPathNode node = arguments.empty() ? context : (argument(0).nodes.empty() ? XPathNode{} : argument(0).nodes.front());
            if (node.node == nullptr) {
                return XPathString("");
            }
            const std::string qualified = node.attribute ? node.attributeName : node.node->tagName;
            return XPathString(NamespaceUriOf(*node.node, PrefixOf(qualified)));
        }
        if (name == "lang") {
            const std::string wanted = XPathToString(argument(0));
            std::string spoken;
            for (const Node* cursor = context.node; cursor != nullptr; cursor = cursor->parent) {
                if (cursor->type != Node::Type::Element) {
                    continue;
                }
                for (const Attribute& attribute : cursor->attributes) {
                    if (attribute.name == "xml:lang" || attribute.name == "lang") {
                        spoken = attribute.value;
                        break;
                    }
                }
                if (!spoken.empty()) {
                    break;
                }
            }
            const auto lower = [](std::string text) {
                for (char& character : text) {
                    if (character >= 'A' && character <= 'Z') {
                        character = static_cast<char>(character - 'A' + 'a');
                    }
                }
                return text;
            };
            const std::string left = lower(spoken);
            const std::string right = lower(wanted);
            const bool match = left == right || (left.size() > right.size() && left.compare(0, right.size(), right) == 0 && left[right.size()] == '-');
            return XPathBoolean(match);
        }
        if (name == "id") {
            std::vector<XPathNode> found;
            std::string tokens = XPathToString(argument(0));
            std::string token;
            auto consider = [&](const std::string& id) {
                if (id.empty() || environment.sourceRoot == nullptr) {
                    return;
                }
                std::vector<Node*> pending{environment.sourceRoot};
                for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
                    Node* current = pending[cursor];
                    if (current->type == Node::Type::Element) {
                        for (const Attribute& attribute : current->attributes) {
                            bool isId = attribute.name == "xml:id";
                            for (const auto& declared : environment.idAttributes) {
                                if (attribute.name == declared.second && (declared.first.empty() || declared.first == current->tagName)) {
                                    isId = true;
                                }
                            }
                            if (isId && attribute.value == id) {
                                found.push_back(XPathNode{current, false, "", ""});
                            }
                        }
                    }
                    for (const std::unique_ptr<Node>& child : current->children) {
                        pending.push_back(child.get());
                    }
                }
            };
            for (std::size_t cursor = 0; cursor <= tokens.size(); ++cursor) {
                if (cursor == tokens.size() || IsSpace(tokens[cursor])) {
                    consider(token);
                    token.clear();
                } else {
                    token.push_back(tokens[cursor]);
                }
            }
            SortUnique(found, context.node);
            return XPathNodes(found);
        }
        if (name == "generate-id") {
            const XPathNode node = arguments.empty() ? context : (argument(0).nodes.empty() ? XPathNode{} : argument(0).nodes.front());
            const auto pointer = reinterpret_cast<std::uintptr_t>(node.node);
            return XPathString("id" + std::to_string(pointer) + (node.attribute ? node.attributeName : std::string()));
        }
        if (name == "choose") {
            return XPathToBoolean(argument(0)) ? argument(1) : (arguments.size() > 2 ? arguments[2] : XPathString(""));
        }
        if (environment.extension) {
            return environment.extension("", name, arguments);
        }
        return XPathString("");
    }

    std::string Substring(const std::string& text, double start, double length) const {
        if (std::isnan(start)) {
            return "";
        }
        const double roundedStart = std::floor(start + 0.5);
        double roundedLength = std::isnan(length) ? static_cast<double>(text.size()) : std::floor(length + 0.5);
        double begin = roundedStart - 1;
        if (begin < 0) {
            roundedLength += begin;
            begin = 0;
        }
        if (roundedLength <= 0 || begin >= static_cast<double>(text.size())) {
            return "";
        }
        const std::size_t from = static_cast<std::size_t>(begin);
        const std::size_t count = std::min(text.size() - from, static_cast<std::size_t>(roundedLength));
        return text.substr(from, count);
    }

    void SortUnique(std::vector<XPathNode>& nodes, Node* root) {
        const Node* document = DocumentRoot(root);
        std::map<const Node*, int> order;
        int next = 0;
        CollectOrder(document, order, next);
        std::stable_sort(nodes.begin(), nodes.end(), [&](const XPathNode& left, const XPathNode& right) { return DocumentBefore(left, right, order); });
        std::vector<XPathNode> unique;
        for (const XPathNode& node : nodes) {
            if (!unique.empty() && SameXPathNode(unique.back(), node)) {
                continue;
            }
            unique.push_back(node);
        }
        nodes = std::move(unique);
    }

    XPathValue Compare(const XPathValue& left, const XPathValue& right, bool equal) {
        const bool same = ComparePair(left, right);
        return XPathBoolean(equal ? same : !same);
    }

    bool ComparePair(const XPathValue& left, const XPathValue& right) {
        if (left.kind == XPathKind::NodeSet && right.kind == XPathKind::NodeSet) {
            for (const XPathNode& leftNode : left.nodes) {
                for (const XPathNode& rightNode : right.nodes) {
                    if (XPathStringValue(leftNode) == XPathStringValue(rightNode)) {
                        return true;
                    }
                }
            }
            return false;
        }
        if (left.kind == XPathKind::NodeSet) {
            for (const XPathNode& node : left.nodes) {
                if (ComparePair(XPathString(XPathStringValue(node)), right)) {
                    return true;
                }
            }
            return false;
        }
        if (right.kind == XPathKind::NodeSet) {
            return ComparePair(right, left);
        }
        if (left.kind == XPathKind::Boolean || right.kind == XPathKind::Boolean) {
            return XPathToBoolean(left) == XPathToBoolean(right);
        }
        if (left.kind == XPathKind::Number || right.kind == XPathKind::Number) {
            return XPathToNumber(left) == XPathToNumber(right);
        }
        return XPathToString(left) == XPathToString(right);
    }

    XPathValue Relate(const XPathValue& left, const XPathValue& right, int operation) {
        auto compare = [&](double first, double second) {
            if (operation == 1) {
                return first <= second;
            }
            if (operation == 2) {
                return first >= second;
            }
            if (operation == 3) {
                return first < second;
            }
            return first > second;
        };
        if (left.kind == XPathKind::NodeSet || right.kind == XPathKind::NodeSet) {
            const XPathValue& nodes = left.kind == XPathKind::NodeSet ? left : right;
            const XPathValue& other = left.kind == XPathKind::NodeSet ? right : left;
            for (const XPathNode& node : nodes.nodes) {
                const double first = left.kind == XPathKind::NodeSet ? XPathToNumber(XPathString(XPathStringValue(node))) : XPathToNumber(other);
                const double second = left.kind == XPathKind::NodeSet ? XPathToNumber(other.kind == XPathKind::NodeSet ? XPathString(XPathStringValue(node)) : other)
                                                                      : XPathToNumber(XPathString(XPathStringValue(node)));
                (void)first;
                (void)second;
                if (other.kind == XPathKind::NodeSet) {
                    for (const XPathNode& otherNode : other.nodes) {
                        if (compare(XPathToNumber(XPathString(XPathStringValue(node))), XPathToNumber(XPathString(XPathStringValue(otherNode))))) {
                            return XPathBoolean(true);
                        }
                    }
                } else if (left.kind == XPathKind::NodeSet && compare(XPathToNumber(XPathString(XPathStringValue(node))), XPathToNumber(other))) {
                    return XPathBoolean(true);
                } else if (right.kind == XPathKind::NodeSet && compare(XPathToNumber(left), XPathToNumber(XPathString(XPathStringValue(node))))) {
                    return XPathBoolean(true);
                }
            }
            return XPathBoolean(false);
        }
        return XPathBoolean(compare(XPathToNumber(left), XPathToNumber(right)));
    }
};

std::string ConcatenateText(const Node& node) {
    if (node.type == Node::Type::Text) {
        return node.text;
    }
    std::string text;
    for (const std::unique_ptr<Node>& child : node.children) {
        text += ConcatenateText(*child);
    }
    return text;
}

}  // namespace

std::string NamespaceUriOf(const Node& element, const std::string& prefix) {
    for (const Node* cursor = &element; cursor != nullptr; cursor = cursor->parent) {
        if (cursor->type != Node::Type::Element) {
            continue;
        }
        for (const Attribute& attribute : cursor->attributes) {
            if (prefix.empty() && attribute.name == "xmlns") {
                return attribute.value;
            }
            if (!prefix.empty() && attribute.name == "xmlns:" + prefix) {
                return attribute.value;
            }
        }
    }
    return "";
}

std::string XPathStringValue(const XPathNode& node) {
    if (node.attribute) {
        return node.attributeValue;
    }
    if (node.node == nullptr) {
        return "";
    }
    if (node.node->type == Node::Type::Text || node.node->type == Node::Type::Comment || node.node->type == Node::Type::ProcessingInstruction) {
        return node.node->text;
    }
    return ConcatenateText(*node.node);
}

XPathValue XPathBoolean(bool value) {
    XPathValue result;
    result.kind = XPathKind::Boolean;
    result.booleanValue = value;
    return result;
}

XPathValue XPathNumber(double value) {
    XPathValue result;
    result.kind = XPathKind::Number;
    result.numberValue = value;
    return result;
}

XPathValue XPathString(const std::string& value) {
    XPathValue result;
    result.kind = XPathKind::String;
    result.stringValue = value;
    return result;
}

XPathValue XPathNodes(std::vector<XPathNode> nodes) {
    XPathValue result;
    result.kind = XPathKind::NodeSet;
    result.nodes = std::move(nodes);
    return result;
}

double XPathToNumber(const XPathValue& value) {
    if (value.kind == XPathKind::Number) {
        return value.numberValue;
    }
    if (value.kind == XPathKind::Boolean) {
        return value.booleanValue ? 1 : 0;
    }
    const std::string text = XPathToString(value);
    if (text.empty()) {
        return std::nan("");
    }
    char* end = nullptr;
    const double number = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) {
        return std::nan("");
    }
    while (*end != '\0' && IsSpace(*end)) {
        ++end;
    }
    return *end == '\0' ? number : std::nan("");
}

bool XPathToBoolean(const XPathValue& value) {
    if (value.kind == XPathKind::Boolean) {
        return value.booleanValue;
    }
    if (value.kind == XPathKind::Number) {
        return value.numberValue != 0 && !std::isnan(value.numberValue);
    }
    if (value.kind == XPathKind::String) {
        return !value.stringValue.empty();
    }
    return !value.nodes.empty();
}

std::string XPathToString(const XPathValue& value) {
    if (value.kind == XPathKind::String) {
        return value.stringValue;
    }
    if (value.kind == XPathKind::Boolean) {
        return value.booleanValue ? "true" : "false";
    }
    if (value.kind == XPathKind::Number) {
        if (std::isnan(value.numberValue)) {
            return "NaN";
        }
        if (std::floor(value.numberValue) == value.numberValue && std::fabs(value.numberValue) < 1e15) {
            return std::to_string(static_cast<long long>(value.numberValue));
        }
        return std::to_string(value.numberValue);
    }
    return value.nodes.empty() ? std::string() : XPathStringValue(value.nodes.front());
}

bool SameXPathNode(const XPathNode& left, const XPathNode& right) {
    return left.node == right.node && left.attribute == right.attribute && left.attributeName == right.attributeName;
}

XPathValue EvaluateXPath(
    const std::string& expression, const XPathNode& context, std::size_t position, std::size_t size, const XPathEnvironment& environment) {
    Parser parser(expression, environment);
    return parser.Parse(context, position, size);
}

}
