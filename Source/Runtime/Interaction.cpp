#include "Runtime/Interaction.h"

#include "Core/Utf8.h"
#include "Runtime/MiniScript.h"

#include <algorithm>
#include <cstdlib>

namespace RoboWeb {

namespace {

// pointer-events: none takes an element and its subtree out of hit testing, the
// way a browser does. The ancestor walk stops at the first element that sets it.
bool IgnoresPointer(const Node& node) {
    for (const Node* current = &node; current != nullptr; current = current->parent) {
        if (!HasAttribute(*current, "style")) {
            continue;
        }
        const std::string style = ToLowerAscii(AttributeValue(*current, "style"));
        const std::size_t at = style.find("pointer-events");
        if (at == std::string::npos) {
            continue;
        }
        const std::size_t colon = style.find(':', at);
        if (colon == std::string::npos) {
            continue;
        }
        const std::size_t end = style.find(';', colon);
        const std::string value = style.substr(colon + 1, end == std::string::npos ? std::string::npos : end - colon - 1);
        std::string trimmed;
        for (char character : value) {
            if (character != ' ' && character != '\t') {
                trimmed.push_back(character);
            }
        }
        if (trimmed == "none") {
            return true;
        }
    }
    return false;
}

Node* HitTest(const LayoutResult& layout, int x, int y, int scrollY) {
    Node* best = nullptr;
    int bestArea = 0;
    for (const ElementBox& box : layout.boxes) {
        const int boxY = box.fixed ? box.y : box.y - scrollY;
        if (x >= box.x && y >= boxY && x < box.x + box.width && y < boxY + box.height) {
            const int area = box.width * box.height;
            if (best == nullptr || area < bestArea) {
                if (IgnoresPointer(*box.node)) {
                    continue;
                }
                best = box.node;
                bestArea = area;
            }
        }
    }
    return best;
}

Node* FormOwner(Node& node) {
    if (HasAttribute(node, "form")) {
        Node* root = &node;
        while (root->parent != nullptr) {
            root = root->parent;
        }
        return FindElementById(*root, AttributeValue(node, "form"));
    }
    return FindAncestor(node, ElementKind::Form);
}

bool ControlDisabled(const Node& node) {
    if (HasAttribute(node, "disabled") || HasAttribute(node, "inert") || node.hiddenByScript) {
        return true;
    }
    for (const Node* ancestor = node.parent; ancestor != nullptr; ancestor = ancestor->parent) {
        if (HasAttribute(*ancestor, "inert") || ancestor->hiddenByScript) {
            return true;
        }
        if (ancestor->kind == ElementKind::Fieldset && HasAttribute(*ancestor, "disabled")) {
            return true;
        }
        if (ancestor->kind == ElementKind::Optgroup && HasAttribute(*ancestor, "disabled")) {
            return true;
        }
    }
    return false;
}

bool MatchPattern(const std::string& text, const std::string& pattern) {
    std::size_t textIndex = 0;
    std::size_t patternIndex = 0;
    std::size_t starText = std::string::npos;
    std::size_t starPattern = std::string::npos;
    auto matchesClass = [&](char patternChar, char value) {
        if (patternChar == '.') {
            return true;
        }
        if (patternChar == 'd') {
            return value >= '0' && value <= '9';
        }
        if (patternChar == 'w') {
            return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '_';
        }
        return patternChar == value;
    };
    while (textIndex < text.size()) {
        if (patternIndex < pattern.size() && pattern[patternIndex] == '\\' && patternIndex + 1 < pattern.size()) {
            const char kind = pattern[patternIndex + 1];
            const char value = text[textIndex];
            if (!matchesClass(kind == 'd' || kind == 'w' ? kind : kind, value) && kind != value) {
                if (starPattern == std::string::npos) {
                    return false;
                }
                patternIndex = starPattern;
                textIndex = ++starText;
                continue;
            }
            patternIndex += 2;
            ++textIndex;
            continue;
        }
        if (patternIndex < pattern.size() && (pattern[patternIndex] == text[textIndex] || pattern[patternIndex] == '.')) {
            ++patternIndex;
            ++textIndex;
            continue;
        }
        if (patternIndex < pattern.size() && (pattern[patternIndex] == '*' || pattern[patternIndex] == '+')) {
            starPattern = patternIndex + 1;
            starText = textIndex;
            ++patternIndex;
            continue;
        }
        if (starPattern != std::string::npos) {
            patternIndex = starPattern;
            textIndex = ++starText;
            continue;
        }
        return false;
    }
    while (patternIndex < pattern.size() && (pattern[patternIndex] == '*' || pattern[patternIndex] == '?')) {
        ++patternIndex;
    }
    return patternIndex == pattern.size();
}

bool IsSuccessful(const Node& node) {
    if (ControlDisabled(node)) {
        return false;
    }
    if (node.kind == ElementKind::Input) {
        const std::string type = ToLowerAscii(AttributeValue(node, "type"));
        if (type == "checkbox" || type == "radio") {
            return node.checked;
        }
        if (type == "submit" || type == "button" || type == "reset" || type == "image") {
            return false;
        }
        return true;
    }
    if (node.kind == ElementKind::Select || node.kind == ElementKind::Textarea) {
        return true;
    }
    return false;
}

std::string Encode(const std::string& text) {
    std::string encoded;
    for (unsigned char character : text) {
        if ((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.' ||
            character == '*') {
            encoded.push_back(static_cast<char>(character));
        } else if (character == ' ') {
            encoded.push_back('+');
        } else {
            const char hex[] = "0123456789ABCDEF";
            encoded.push_back('%');
            encoded.push_back(hex[character >> 4]);
            encoded.push_back(hex[character & 0x0F]);
        }
    }
    return encoded;
}

void SubmitForm(Document& document, Node& form) {
    if (!HasAttribute(form, "novalidate")) {
        std::vector<Node*> check{&form};
        while (!check.empty()) {
            Node* current = check.back();
            check.pop_back();
            if (current != &form && current->kind == ElementKind::Form) {
                continue;
            }
            if (current->kind == ElementKind::Input || current->kind == ElementKind::Textarea || current->kind == ElementKind::Select) {
                const std::string value = current->liveValue;
                const std::string name = AttributeValue(*current, "name");
                if (HasAttribute(*current, "required") && value.empty() && current->kind != ElementKind::Select) {
                    document.status = "invalid required " + name;
                    return;
                }
                const int minimumLength = std::atoi(AttributeValue(*current, "minlength").c_str());
                if (minimumLength > 0 && static_cast<int>(value.size()) < minimumLength) {
                    document.status = "invalid minlength " + name;
                    return;
                }
                const std::string pattern = AttributeValue(*current, "pattern");
                if (!pattern.empty() && !value.empty() && !MatchPattern(value, pattern)) {
                    document.status = "invalid pattern " + name;
                    return;
                }
                const std::string type = ToLowerAscii(AttributeValue(*current, "type"));
                if (type == "email" && !value.empty() && value.find('@') == std::string::npos) {
                    document.status = "invalid email " + name;
                    return;
                }
                if (type == "number" && !value.empty()) {
                    const double number = std::atof(value.c_str());
                    if (!AttributeValue(*current, "min").empty() && number < std::atof(AttributeValue(*current, "min").c_str())) {
                        document.status = "invalid min " + name;
                        return;
                    }
                    if (!AttributeValue(*current, "max").empty() && number > std::atof(AttributeValue(*current, "max").c_str())) {
                        document.status = "invalid max " + name;
                        return;
                    }
                }
            }
            for (const std::unique_ptr<Node>& child : current->children) {
                check.push_back(child.get());
            }
        }
    }
    std::string body;
    std::vector<Node*> stack{&form};
    while (!stack.empty()) {
        Node* current = stack.back();
        stack.pop_back();
        if (current != &form && current->kind == ElementKind::Form) {
            continue;
        }
        if (IsSuccessful(*current)) {
            const std::string name = AttributeValue(*current, "name");
            if (!name.empty()) {
                if (!body.empty()) {
                    body.push_back('&');
                }
                std::string value = current->liveValue;
                if (current->kind == ElementKind::Select) {
                    std::vector<std::string> values;
                    std::vector<Node*> options{current};
                    while (!options.empty()) {
                        Node* optionParent = options.back();
                        options.pop_back();
                        for (const std::unique_ptr<Node>& child : optionParent->children) {
                            if (child->kind == ElementKind::Option && (child->selected || HasAttribute(*child, "selected"))) {
                                std::string optionValue = AttributeValue(*child, "value");
                                if (optionValue.empty()) {
                                    optionValue = GatherText(*child);
                                }
                                values.push_back(optionValue);
                            } else if (child->kind == ElementKind::Optgroup) {
                                options.push_back(child.get());
                            }
                        }
                    }
                    if (!values.empty()) {
                        value = values[0];
                        for (std::size_t index = 1; index < values.size(); ++index) {
                            body += Encode(name) + "=" + Encode(value);
                            body.push_back('&');
                            value = values[index];
                        }
                    }
                }
                body += Encode(name) + "=" + Encode(value);
                if (HasAttribute(*current, "dirname")) {
                    body.push_back('&');
                    body += Encode(AttributeValue(*current, "dirname")) + "=ltr";
                }
            }
        }
        for (int index = static_cast<int>(current->children.size()) - 1; index >= 0; --index) {
            stack.push_back(current->children[static_cast<std::size_t>(index)].get());
        }
    }
    const std::string action = AttributeValue(form, "action");
    const std::string method = AttributeValue(form, "method").empty() ? "get" : AttributeValue(form, "method");
    document.status = method + " " + action + " form data: " + body;
    RunEvent(document, form, "onsubmit");
}

Node* LabeledControl(Node& label) {
    const std::string id = AttributeValue(label, "for");
    if (!id.empty()) {
        Node* root = &label;
        while (root->parent != nullptr) {
            root = root->parent;
        }
        return FindElementById(*root, id);
    }
    std::vector<Node*> stack{&label};
    while (!stack.empty()) {
        Node* current = stack.back();
        stack.pop_back();
        if (current != &label &&
            (current->kind == ElementKind::Input || current->kind == ElementKind::Textarea ||
                current->kind == ElementKind::Select || current->kind == ElementKind::Button)) {
            return current;
        }
        for (const std::unique_ptr<Node>& child : current->children) {
            stack.push_back(child.get());
        }
    }
    return nullptr;
}

void Activate(Document& document, Node& node) {
    if (ControlDisabled(node)) {
        return;
    }
    for (Node* cursor = &node; cursor != nullptr; cursor = cursor->parent) {
        if (ToLowerAscii(cursor->tagName) != "maction" || ToLowerAscii(AttributeValue(*cursor, "actiontype")) != "toggle") {
            continue;
        }
        int count = 0;
        for (const std::unique_ptr<Node>& child : cursor->children) {
            if (child->type == Node::Type::Element) {
                ++count;
            }
        }
        int selection = std::max(1, std::atoi(AttributeValue(*cursor, "selection").c_str()));
        selection = count <= 1 ? 1 : selection % count + 1;
        bool wrote = false;
        for (Attribute& attribute : cursor->attributes) {
            if (EqualsAsciiIgnoreCase(attribute.name, "selection")) {
                attribute.value = std::to_string(selection);
                wrote = true;
            }
        }
        if (!wrote) {
            Attribute attribute;
            attribute.name = "selection";
            attribute.value = std::to_string(selection);
            cursor->attributes.push_back(attribute);
        }
        document.status = "selection " + std::to_string(selection);
        return;
    }
    if (HasAttribute(node, "draggable")) {
        document.status = "dragging " + (AttributeValue(node, "id").empty() ? node.tagName : AttributeValue(node, "id"));
    }
    if (node.kind == ElementKind::Summary && node.parent != nullptr && node.parent->kind == ElementKind::Details) {
        node.parent->open = !node.parent->open;
        document.status = node.parent->open ? "details open" : "details closed";
        RunEvent(document, *node.parent, "ontoggle");
        return;
    }
    if (node.kind == ElementKind::Input) {
        const std::string type = ToLowerAscii(AttributeValue(node, "type"));
        if (type == "checkbox") {
            node.checked = !node.checked;
            document.status = node.checked ? "checked" : "unchecked";
            return;
        }
        if (type == "radio") {
            Node* form = FormOwner(node);
            Node* scope = form != nullptr ? form : &node;
            Node* root = scope;
            if (form == nullptr) {
                root = &node;
                while (root->parent != nullptr) {
                    root = root->parent;
                }
            }
            const std::string name = AttributeValue(node, "name");
            std::vector<Node*> stack{root};
            while (!stack.empty()) {
                Node* current = stack.back();
                stack.pop_back();
                if (current->kind == ElementKind::Input && ToLowerAscii(AttributeValue(*current, "type")) == "radio" &&
                    AttributeValue(*current, "name") == name) {
                    current->checked = current == &node;
                }
                for (const std::unique_ptr<Node>& child : current->children) {
                    stack.push_back(child.get());
                }
            }
            document.status = "radio selected";
            return;
        }
        if (type == "reset") {
            Node* form = FormOwner(node);
            if (form != nullptr) {
                ResetControlState(*form);
            }
            document.status = "form reset";
            return;
        }
        if (type == "submit") {
            Node* form = FormOwner(node);
            if (form != nullptr) {
                SubmitForm(document, *form);
            }
            return;
        }
        document.focus = &node;
        return;
    }
    if (node.kind == ElementKind::Option) {
        Node* selectNode = FindAncestor(node, ElementKind::Select);
        if (selectNode != nullptr) {
            const bool multiple = HasAttribute(*selectNode, "multiple");
            std::vector<Node*> stack{selectNode};
            while (!stack.empty()) {
                Node* current = stack.back();
                stack.pop_back();
                if (current->kind == ElementKind::Option) {
                    if (multiple && current == &node) {
                        current->selected = !current->selected;
                    } else if (!multiple) {
                        current->selected = current == &node;
                    }
                }
                for (const std::unique_ptr<Node>& child : current->children) {
                    stack.push_back(child.get());
                }
            }
            selectNode->selectExpanded = false;
            document.status = "option selected";
        }
        return;
    }
    if (node.kind == ElementKind::Select) {
        node.selectExpanded = !node.selectExpanded;
        document.focus = &node;
        return;
    }
    if (node.kind == ElementKind::Button) {
        const std::string type = ToLowerAscii(AttributeValue(node, "type"));
        const std::string effective = type.empty() ? "submit" : type;
        if (HasAttribute(node, "commandfor")) {
            Node* root = &node;
            while (root->parent != nullptr) {
                root = root->parent;
            }
            Node* target = FindElementById(*root, AttributeValue(node, "commandfor"));
            const std::string command = ToLowerAscii(AttributeValue(node, "command"));
            if (target != nullptr) {
                if (command == "close" || command == "request-close") {
                    target->open = false;
                    target->popoverOpen = false;
                } else if (command == "hide-popover") {
                    target->popoverOpen = false;
                } else if (command == "toggle-popover") {
                    target->popoverOpen = !target->popoverOpen;
                } else if (command == "show-popover") {
                    target->popoverOpen = true;
                } else {
                    target->open = true;
                }
                document.status = "command " + command;
            }
            return;
        }
        if (HasAttribute(node, "popovertarget")) {
            Node* root = &node;
            while (root->parent != nullptr) {
                root = root->parent;
            }
            Node* target = FindElementById(*root, AttributeValue(node, "popovertarget"));
            if (target != nullptr) {
                const std::string action = ToLowerAscii(AttributeValue(node, "popovertargetaction"));
                if (action == "show") {
                    target->popoverOpen = true;
                } else if (action == "hide") {
                    target->popoverOpen = false;
                } else {
                    target->popoverOpen = !target->popoverOpen;
                }
                document.status = target->popoverOpen ? "popover open" : "popover closed";
            }
            return;
        }
        if (effective == "reset") {
            Node* form = FormOwner(node);
            if (form != nullptr) {
                ResetControlState(*form);
                document.status = "form reset";
            }
            return;
        }
        if (effective == "submit") {
            Node* form = FormOwner(node);
            if (form != nullptr) {
                SubmitForm(document, *form);
            }
            return;
        }
        document.focus = &node;
        return;
    }
    if (node.kind == ElementKind::Label) {
        Node* control = LabeledControl(node);
        if (control != nullptr) {
            Activate(document, *control);
        }
        return;
    }
    const std::string mathTag = ToLowerAscii(node.tagName);
    const bool mathHref = HasAttribute(node, "href") && (node.kind == ElementKind::Math || (!mathTag.empty() && mathTag[0] == 'm'));
    if (node.kind == ElementKind::A || mathTag == "a" || mathHref) {
        const std::string linkType = ToLowerAscii(AttributeValue(node, "xlink:type"));
        if (!linkType.empty() && linkType != "simple") {
            return;
        }
        std::string href = AttributeValue(node, "href");
        if (href.empty()) {
            href = AttributeValue(node, "xlink:href");
        }
        if (!href.empty()) {
            node.visited = true;
        }
        if (HasAttribute(node, "download")) {
            document.status = "download " + AttributeValue(node, "download") + " " + href;
        } else if (!href.empty() && href[0] == '#') {
            document.status = "fragment " + href.substr(1);
        } else if (!href.empty()) {
            std::string target = AttributeValue(node, "target");
            if (target.empty() && ToLowerAscii(AttributeValue(node, "xlink:show")) == "new") {
                target = "new";
            }
            document.status = target.empty() ? "open " + href : "open " + href + " in " + target;
        }
        const std::string arcrole = AttributeValue(node, "xlink:arcrole");
        if (!arcrole.empty() && !document.status.empty()) {
            document.status += " role " + arcrole;
        }
        return;
    }
    if (node.kind == ElementKind::Geolocation) {
        document.status = "location is not available";
        return;
    }
    if (node.kind == ElementKind::Textarea || HasAttribute(node, "contenteditable")) {
        document.focus = &node;
    }
}

bool IsTextField(const Node& node) {
    if (node.kind == ElementKind::Textarea) {
        return true;
    }
    if (node.kind != ElementKind::Input) {
        return false;
    }
    const std::string type = ToLowerAscii(AttributeValue(node, "type"));
    const std::string effective = type.empty() ? std::string("text") : type;
    return effective != "checkbox" && effective != "radio" && effective != "button" && effective != "submit" && effective != "reset" &&
           effective != "file" && effective != "image" && effective != "hidden" && effective != "color" && effective != "range";
}

int PreviousBoundary(const std::string& text, int offset) {
    if (offset <= 0) {
        return 0;
    }
    int index = offset;
    do {
        --index;
    } while (index > 0 && (static_cast<unsigned char>(text[static_cast<std::size_t>(index)]) & 0xC0) == 0x80);
    return index;
}

int NextBoundary(const std::string& text, int offset) {
    if (offset < 0) {
        offset = 0;
    }
    if (offset >= static_cast<int>(text.size())) {
        return static_cast<int>(text.size());
    }
    std::size_t index = static_cast<std::size_t>(offset);
    char32_t codePoint = 0;
    DecodeUtf8(text, index, codePoint);
    return static_cast<int>(index);
}

int CaretIndex(const Document& document, const Node& node) {
    if (document.caretOffset < 0 || document.caretOffset > static_cast<int>(node.liveValue.size())) {
        return static_cast<int>(node.liveValue.size());
    }
    return document.caretOffset;
}

bool CanEdit(const Node& node) {
    return !HasAttribute(node, "readonly") && !HasAttribute(node, "disabled");
}

std::string FitText(const Node& node, const std::string& extra) {
    const std::string maximum = AttributeValue(node, "maxlength");
    if (maximum.empty()) {
        return extra;
    }
    const int room = std::atoi(maximum.c_str()) - Utf8Length(node.liveValue);
    if (room <= 0) {
        return "";
    }
    std::string fitted;
    std::size_t index = 0;
    int taken = 0;
    while (index < extra.size() && taken < room) {
        const std::size_t before = index;
        char32_t codePoint = 0;
        DecodeUtf8(extra, index, codePoint);
        fitted.append(extra, before, index - before);
        ++taken;
    }
    return fitted;
}

void InsertAtCaret(Document& document, Node& node, const std::string& extra) {
    const std::string fitted = FitText(node, extra);
    if (fitted.empty()) {
        return;
    }
    const int offset = CaretIndex(document, node);
    node.liveValue.insert(static_cast<std::size_t>(offset), fitted);
    document.caretOffset = offset + static_cast<int>(fitted.size());
    document.caretOn = true;
}

void PlaceCaret(Document& document, const LayoutResult& layout, Node& node, int x, int y) {
    const ElementBox* best = nullptr;
    int bestArea = 0;
    for (const ElementBox& box : layout.boxes) {
        if (box.node != &node) {
            continue;
        }
        const int boxY = box.fixed ? box.y : box.y - document.scrollY;
        if (x >= box.x && y >= boxY && x < box.x + box.width && y < boxY + box.height) {
            const int area = box.width * box.height;
            if (best == nullptr || area < bestArea) {
                best = &box;
                bestArea = area;
            }
        }
    }
    if (best == nullptr) {
        document.caretOffset = static_cast<int>(node.liveValue.size());
        document.caretOn = true;
        return;
    }
    const int boxY = best->fixed ? best->y : best->y - document.scrollY;
    const int padX = CONTROL_BORDER_PX + CONTROL_PADDING_X_PX;
    const int padY = CONTROL_BORDER_PX + CONTROL_PADDING_Y_PX;
    const int localX = x - (best->x + padX);
    const int localY = y - (boxY + padY);
    const int innerWidth = std::max(1, best->width - padX * 2);
    const std::string shown = EditedControlText(node);
    document.caretOffset = HitTestTextByte(shown, localX, localY, innerWidth, best->fontSizePx, best->bold, best->italic,
        document.caretOffset, best->fontFamily);
    document.caretOn = true;
}

void SubmitControl(Document& document, Node& control) {
    Node* form = FormOwner(control);
    if (form != nullptr) {
        SubmitForm(document, *form);
    }
}

bool Focusable(const Node& node) {
    if (node.type != Node::Type::Element || ControlDisabled(node) || HasAttribute(node, "hidden")) {
        return false;
    }
    if (HasAttribute(node, "tabindex") && std::atoi(AttributeValue(node, "tabindex").c_str()) < 0) {
        return false;
    }
    if (node.kind == ElementKind::Input) {
        return ToLowerAscii(AttributeValue(node, "type")) != "hidden";
    }
    if (node.kind == ElementKind::Textarea || node.kind == ElementKind::Select || node.kind == ElementKind::Button ||
        node.kind == ElementKind::Summary) {
        return true;
    }
    if (HasAttribute(node, "tabindex") || HasAttribute(node, "contenteditable")) {
        return true;
    }
    return node.kind == ElementKind::A && !AttributeValue(node, "href").empty();
}

void CollectFocusable(Node& node, std::vector<Node*>& order) {
    std::vector<Node*> normal;
    std::vector<std::pair<int, Node*>> positive;
    std::vector<Node*> stack{&node};
    while (!stack.empty()) {
        Node* current = stack.back();
        stack.pop_back();
        if (current->type == Node::Type::Element && Focusable(*current)) {
            const int tabIndex = HasAttribute(*current, "tabindex") ? std::atoi(AttributeValue(*current, "tabindex").c_str()) : 0;
            if (tabIndex > 0) {
                positive.push_back({tabIndex, current});
            } else {
                normal.push_back(current);
            }
        }
        for (int index = static_cast<int>(current->children.size()) - 1; index >= 0; --index) {
            stack.push_back(current->children[static_cast<std::size_t>(index)].get());
        }
    }
    std::stable_sort(positive.begin(), positive.end(), [](const std::pair<int, Node*>& left, const std::pair<int, Node*>& right) {
        return left.first < right.first;
    });
    for (const std::pair<int, Node*>& entry : positive) {
        order.push_back(entry.second);
    }
    order.insert(order.end(), normal.begin(), normal.end());
}

bool MoveFocus(Document& document, bool reverse) {
    if (document.root == nullptr) {
        return false;
    }
    std::vector<Node*> order;
    CollectFocusable(*document.root, order);
    if (order.empty()) {
        return false;
    }
    int index = reverse ? static_cast<int>(order.size()) - 1 : 0;
    for (int cursor = 0; cursor < static_cast<int>(order.size()); ++cursor) {
        if (order[static_cast<std::size_t>(cursor)] == document.focus) {
            index = reverse ? cursor - 1 : cursor + 1;
            break;
        }
    }
    if (index < 0) {
        index = static_cast<int>(order.size()) - 1;
    }
    if (index >= static_cast<int>(order.size())) {
        index = 0;
    }
    document.focus = order[static_cast<std::size_t>(index)];
    document.caretOffset = -1;
    document.caretOn = true;
    return true;
}

}  // namespace

bool DispatchClick(Document& document, const LayoutResult& layout, int x, int y) {
    const std::string before = document.status;
    Node* hit = HitTest(layout, x, y, document.scrollY);
    if (hit == nullptr) {
        document.focus = nullptr;
        return false;
    }
    for (Node* cursor = hit; cursor != nullptr; cursor = cursor->parent) {
        if (ToLowerAscii(cursor->tagName) == "maction") {
            Activate(document, *hit);
            return document.status != before;
        }
    }
    Node* target = hit;
    while (target != nullptr && target->kind != ElementKind::A && ToLowerAscii(target->tagName) != "a" &&
           !(HasAttribute(*target, "href") && !AttributeValue(*target, "href").empty() &&
               (target->kind == ElementKind::Math || (!ToLowerAscii(target->tagName).empty() && ToLowerAscii(target->tagName)[0] == 'm'))) &&
           target->kind != ElementKind::Button &&
           target->kind != ElementKind::Summary && target->kind != ElementKind::Label &&
           target->kind != ElementKind::Option && target->kind != ElementKind::Select &&
           target->kind != ElementKind::Input && target->kind != ElementKind::Textarea &&
           target->kind != ElementKind::Geolocation && !HasAttribute(*target, "contenteditable")) {
        target = target->parent;
    }
    if (target == nullptr) {
        document.focus = hit;
        return false;
    }
    if (hit->kind == ElementKind::Img && HasAttribute(*hit, "ismap") && target->kind == ElementKind::A) {
        const ElementBox* box = nullptr;
        for (const ElementBox& candidate : layout.boxes) {
            if (candidate.node == hit) {
                box = &candidate;
                break;
            }
        }
        if (box != nullptr) {
            const int localX = x - box->x;
            const int localY = y - (box->fixed ? box->y : box->y - document.scrollY);
            document.status = AttributeValue(*target, "href") + "?" + std::to_string(localX) + "," + std::to_string(localY);
            target->visited = true;
            return true;
        }
    }
    Activate(document, *target);
    RunEvent(document, *target, "onclick");
    if (IsTextField(*target)) {
        PlaceCaret(document, layout, *target, x, y);
    }
    return document.status != before || document.focus == target;
}

bool DispatchHover(Document& document, const LayoutResult& layout, int x, int y) {
    Node* hit = HitTest(layout, x, y, document.scrollY);
    if (document.hover == hit) {
        return false;
    }
    document.hover = hit;
    std::string titled;
    int titledArea = 0;
    bool foundTitle = false;
    for (const ElementBox& box : layout.boxes) {
        const int boxY = box.fixed ? box.y : box.y - document.scrollY;
        if (box.node == nullptr || x < box.x || y < boxY || x >= box.x + box.width || y >= boxY + box.height) {
            continue;
        }
        const std::string title = AttributeValue(*box.node, "xlink:title");
        if (title.empty()) {
            continue;
        }
        const int area = box.width * box.height;
        if (!foundTitle || area < titledArea) {
            titled = title;
            titledArea = area;
            foundTitle = true;
        }
    }
    if (!foundTitle) {
        for (Node* node = hit; node != nullptr; node = node->parent) {
            const std::string title = AttributeValue(*node, "xlink:title");
            if (!title.empty()) {
                titled = title;
                foundTitle = true;
                break;
            }
        }
    }
    if (!foundTitle) {
        for (Node* node = hit; node != nullptr; node = node->parent) {
            if (ToLowerAscii(node->tagName) != "maction" || ToLowerAscii(AttributeValue(*node, "actiontype")) != "statusline") {
                continue;
            }
            int index = 0;
            for (const std::unique_ptr<Node>& child : node->children) {
                if (child->type != Node::Type::Element) {
                    continue;
                }
                ++index;
                if (index == 2) {
                    titled = GatherText(*child);
                    foundTitle = !titled.empty();
                    break;
                }
            }
            break;
        }
    }
    if (foundTitle) {
        document.status = titled;
    }
    return true;
}

// The CSS cursor property on the nearest ancestor that sets it. An empty result
// means no element declared one and the legacy attribute still decides.
std::string CssCursorFor(const Node& node) {
    for (const Node* current = &node; current != nullptr; current = current->parent) {
        if (!HasAttribute(*current, "style")) {
            continue;
        }
        const std::string style = ToLowerAscii(AttributeValue(*current, "style"));
        const std::size_t at = style.find("cursor");
        if (at == std::string::npos) {
            continue;
        }
        const std::size_t colon = style.find(':', at);
        if (colon == std::string::npos) {
            continue;
        }
        const std::size_t end = style.find(';', colon);
        const std::string value = style.substr(colon + 1, end == std::string::npos ? std::string::npos : end - colon - 1);
        std::string trimmed;
        for (char character : value) {
            if (character != ' ' && character != '\t') {
                trimmed.push_back(character);
            }
        }
        if (trimmed == "text" || trimmed == "vertical-text") {
            return "text";
        }
        if (trimmed == "pointer" || trimmed == "hand" || trimmed == "grab" || trimmed == "grabbing") {
            return "pointer";
        }
        if (!trimmed.empty()) {
            return "default";
        }
    }
    return std::string();
}

PointerShape PointerAt(const LayoutResult& layout, int x, int y, int scrollY) {
    Node* hit = HitTest(layout, x, y, scrollY);
    for (Node* node = hit; node != nullptr; node = node->parent) {
        // The CSS cursor property wins over the legacy HTML cursor attribute.
        const std::string cssCursor = CssCursorFor(*node);
        if (cssCursor == "text") {
            return PointerShape::Text;
        }
        if (cssCursor == "pointer") {
            return PointerShape::Hand;
        }
        if (!cssCursor.empty()) {
            return PointerShape::Arrow;
        }
        const std::string cursor = ToLowerAscii(AttributeValue(*node, "cursor"));
        if (cursor == "text") {
            return PointerShape::Text;
        }
        if (cursor == "pointer" || cursor == "hand") {
            return PointerShape::Hand;
        }
        if (IsTextField(*node)) {
            return PointerShape::Text;
        }
        if ((node->kind == ElementKind::A || ToLowerAscii(node->tagName) == "a") &&
            (!AttributeValue(*node, "href").empty() || !AttributeValue(*node, "xlink:href").empty())) {
            return PointerShape::Hand;
        }
    }
    return PointerShape::Arrow;
}

bool DispatchKey(Document& document, const std::string& key) {
    if (key == "tab" || key == "shift-tab") {
        return MoveFocus(document, key == "shift-tab");
    }
    Node* focus = document.focus;
    const bool editable = focus != nullptr && (IsTextField(*focus) || HasAttribute(*focus, "contenteditable")) && CanEdit(*focus);
    if (!editable && document.root != nullptr && key.size() == 1) {
        std::vector<Node*> stack{document.root.get()};
        while (!stack.empty()) {
            Node* current = stack.back();
            stack.pop_back();
            const std::string access = AttributeValue(*current, "accesskey");
            if (!access.empty() && ToLowerAscii(access.substr(0, 1)) == ToLowerAscii(key) && !ControlDisabled(*current)) {
                Activate(document, *current);
                document.focus = current;
                return true;
            }
            for (const std::unique_ptr<Node>& child : current->children) {
                stack.push_back(child.get());
            }
        }
    }
    if (focus == nullptr || !editable) {
        return false;
    }
    int offset = CaretIndex(document, *focus);
    if (key == "backspace") {
        if (offset <= 0) {
            document.caretOffset = 0;
            return false;
        }
        const int previous = PreviousBoundary(focus->liveValue, offset);
        focus->liveValue.erase(static_cast<std::size_t>(previous), static_cast<std::size_t>(offset - previous));
        document.caretOffset = previous;
        document.caretOn = true;
        return true;
    }
    if (key == "delete") {
        if (offset >= static_cast<int>(focus->liveValue.size())) {
            return false;
        }
        const int next = NextBoundary(focus->liveValue, offset);
        focus->liveValue.erase(static_cast<std::size_t>(offset), static_cast<std::size_t>(next - offset));
        document.caretOffset = offset;
        document.caretOn = true;
        return true;
    }
    if (key == "left") {
        document.caretOffset = PreviousBoundary(focus->liveValue, offset);
        document.caretOn = true;
        return true;
    }
    if (key == "right") {
        document.caretOffset = NextBoundary(focus->liveValue, offset);
        document.caretOn = true;
        return true;
    }
    if (key == "home") {
        document.caretOffset = 0;
        document.caretOn = true;
        return true;
    }
    if (key == "end") {
        document.caretOffset = static_cast<int>(focus->liveValue.size());
        document.caretOn = true;
        return true;
    }
    if (key == "enter") {
        if (focus->kind == ElementKind::Textarea) {
            InsertAtCaret(document, *focus, "\n");
            return true;
        }
        SubmitControl(document, *focus);
        return true;
    }
    if (key.size() == 1 && key[0] >= 32) {
        const int before = static_cast<int>(focus->liveValue.size());
        InsertAtCaret(document, *focus, key);
        return static_cast<int>(focus->liveValue.size()) != before;
    }
    return false;
}

bool DispatchText(Document& document, const std::string& text) {
    Node* focus = document.focus;
    if (focus == nullptr || text.empty()) {
        return false;
    }
    const bool editable = focus->kind == ElementKind::Textarea ||
                          (focus->kind == ElementKind::Input && IsTextField(*focus)) || HasAttribute(*focus, "contenteditable");
    if (!editable || !CanEdit(*focus)) {
        return false;
    }
    std::string filtered;
    std::size_t index = 0;
    while (index < text.size()) {
        const std::size_t before = index;
        char32_t codePoint = 0;
        DecodeUtf8(text, index, codePoint);
        if (codePoint == '\r') {
            continue;
        }
        if (codePoint == '\n' && focus->kind != ElementKind::Textarea) {
            continue;
        }
        if (codePoint < 32 && codePoint != '\n' && codePoint != '\t') {
            continue;
        }
        filtered.append(text, before, index - before);
    }
    if (filtered.empty()) {
        return false;
    }
    const std::string capital = ToLowerAscii(AttributeValue(*focus, "autocapitalize"));
    if (capital == "characters") {
        for (char& character : filtered) {
            if (character >= 'a' && character <= 'z') {
                character = static_cast<char>(character - 'a' + 'A');
            }
        }
    }
    const std::string mode = ToLowerAscii(AttributeValue(*focus, "inputmode"));
    if (mode == "numeric" || mode == "decimal") {
        std::string digits;
        for (char character : filtered) {
            if ((character >= '0' && character <= '9') || (mode == "decimal" && (character == '.' || character == '-'))) {
                digits.push_back(character);
            }
        }
        filtered.swap(digits);
    }
    const int before = static_cast<int>(focus->liveValue.size());
    InsertAtCaret(document, *focus, filtered);
    const bool changed = static_cast<int>(focus->liveValue.size()) != before;
    if (changed) {
        RunEvent(document, *focus, "oninput");
    }
    return changed;
}

void RevealFocus(Document& document, const LayoutResult& layout, int viewportHeight) {
    if (document.focus == nullptr) {
        DispatchScroll(document, 0, viewportHeight, layout.contentHeight);
        return;
    }
    const ElementBox* best = nullptr;
    int bestArea = 0;
    for (const ElementBox& box : layout.boxes) {
        if (box.node != document.focus || box.fixed) {
            continue;
        }
        const int area = box.width * box.height;
        if (best == nullptr || area < bestArea) {
            best = &box;
            bestArea = area;
        }
    }
    if (best != nullptr) {
        if (best->y < document.scrollY) {
            document.scrollY = best->y;
        } else if (best->y + best->height > document.scrollY + viewportHeight) {
            document.scrollY = best->y + best->height - viewportHeight;
        }
    }
    DispatchScroll(document, 0, viewportHeight, layout.contentHeight);
}

void ApplyAutofocus(Document& document) {
    if (document.root == nullptr || document.focus != nullptr) {
        return;
    }
    std::vector<Node*> stack{document.root.get()};
    while (!stack.empty()) {
        Node* current = stack.back();
        stack.pop_back();
        if (current->type == Node::Type::Element && HasAttribute(*current, "autofocus") && !HasAttribute(*current, "disabled")) {
            document.focus = current;
            document.caretOffset = -1;
            document.caretOn = true;
            return;
        }
        for (int index = static_cast<int>(current->children.size()) - 1; index >= 0; --index) {
            stack.push_back(current->children[static_cast<std::size_t>(index)].get());
        }
    }
}

void DispatchScroll(Document& document, int delta, int viewportHeight, int contentHeight) {
    if (document.root != nullptr) {
        std::vector<const Node*> stack{document.root.get()};
        while (!stack.empty()) {
            const Node* node = stack.back();
            stack.pop_back();
            const bool svg = node->kind == ElementKind::Svg || ToLowerAscii(node->tagName) == "svg";
            const Node* parent = node->parent;
            const bool outermost = parent == nullptr || parent->kind == ElementKind::Body || parent->kind == ElementKind::Html || parent->parent == nullptr;
            if (svg && outermost && ToLowerAscii(AttributeValue(*node, "zoomandpan")) == "disable") {
                return;
            }
            for (const std::unique_ptr<Node>& child : node->children) {
                stack.push_back(child.get());
            }
        }
    }
    const int maximum = contentHeight > viewportHeight ? contentHeight - viewportHeight : 0;
    document.scrollY += delta;
    if (document.scrollY < 0) {
        document.scrollY = 0;
    }
    if (document.scrollY > maximum) {
        document.scrollY = maximum;
    }
}

void TickMarquee(Document& document) {
    if (document.root == nullptr) {
        return;
    }
    std::vector<Node*> stack{document.root.get()};
    while (!stack.empty()) {
        Node* current = stack.back();
        stack.pop_back();
        if (current->kind == ElementKind::Marquee) {
            current->marqueeOffset += 2;
        }
        for (const std::unique_ptr<Node>& child : current->children) {
            stack.push_back(child.get());
        }
    }
}

}
