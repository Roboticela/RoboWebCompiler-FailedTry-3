#include "Runtime/Interaction.h"

#include "Core/Utf8.h"

#include <cstdlib>

namespace RoboWeb {

namespace {

Node* HitTest(const LayoutResult& layout, int x, int y, int scrollY) {
    Node* best = nullptr;
    int bestArea = 0;
    for (const ElementBox& box : layout.boxes) {
        const int boxY = box.fixed ? box.y : box.y - scrollY;
        if (x >= box.x && y >= boxY && x < box.x + box.width && y < boxY + box.height) {
            const int area = box.width * box.height;
            if (best == nullptr || area < bestArea) {
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

bool IsSuccessful(const Node& node) {
    if (HasAttribute(node, "disabled")) {
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
                    std::vector<Node*> options{current};
                    while (!options.empty()) {
                        Node* optionParent = options.back();
                        options.pop_back();
                        for (const std::unique_ptr<Node>& child : optionParent->children) {
                            if (child->kind == ElementKind::Option && (child->selected || HasAttribute(*child, "selected"))) {
                                value = AttributeValue(*child, "value");
                                if (value.empty()) {
                                    value = GatherText(*child);
                                }
                            } else if (child->kind == ElementKind::Optgroup) {
                                options.push_back(child.get());
                            }
                        }
                    }
                }
                body += Encode(name) + "=" + Encode(value);
            }
        }
        for (int index = static_cast<int>(current->children.size()) - 1; index >= 0; --index) {
            stack.push_back(current->children[static_cast<std::size_t>(index)].get());
        }
    }
    document.status = "form data: " + body;
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
    if (HasAttribute(node, "disabled") || HasAttribute(node, "inert")) {
        return;
    }
    if (node.kind == ElementKind::Summary && node.parent != nullptr && node.parent->kind == ElementKind::Details) {
        node.parent->open = !node.parent->open;
        document.status = node.parent->open ? "details open" : "details closed";
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
            std::vector<Node*> stack{selectNode};
            while (!stack.empty()) {
                Node* current = stack.back();
                stack.pop_back();
                if (current->kind == ElementKind::Option) {
                    current->selected = current == &node;
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
    if (node.kind == ElementKind::A) {
        const std::string href = AttributeValue(node, "href");
        if (!href.empty() && href[0] == '#') {
            document.status = "fragment " + href.substr(1);
        } else if (!href.empty()) {
            document.status = "navigation is not performed";
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

}  // namespace

bool DispatchClick(Document& document, const LayoutResult& layout, int x, int y) {
    const std::string before = document.status;
    Node* hit = HitTest(layout, x, y, document.scrollY);
    if (hit == nullptr) {
        document.focus = nullptr;
        return false;
    }
    Node* target = hit;
    while (target != nullptr && target->kind != ElementKind::A && target->kind != ElementKind::Button &&
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
    Activate(document, *target);
    return document.status != before || document.focus == target;
}

bool DispatchKey(Document& document, const std::string& key) {
    Node* focus = document.focus;
    if (focus == nullptr) {
        return false;
    }
    const bool editable = focus->kind == ElementKind::Textarea ||
                          (focus->kind == ElementKind::Input && ToLowerAscii(AttributeValue(*focus, "type")) != "checkbox" &&
                              ToLowerAscii(AttributeValue(*focus, "type")) != "radio") ||
                          HasAttribute(*focus, "contenteditable");
    if (!editable || HasAttribute(*focus, "readonly") || HasAttribute(*focus, "disabled")) {
        return false;
    }
    if (key == "backspace") {
        if (!focus->liveValue.empty()) {
            while (!focus->liveValue.empty() &&
                   (static_cast<unsigned char>(focus->liveValue.back()) & 0xC0) == 0x80) {
                focus->liveValue.pop_back();
            }
            if (!focus->liveValue.empty()) {
                focus->liveValue.pop_back();
            }
        }
        return true;
    }
    if (key.size() == 1 && key[0] >= 32) {
        const std::string maximum = AttributeValue(*focus, "maxlength");
        if (!maximum.empty() && Utf8Length(focus->liveValue) >= std::atoi(maximum.c_str())) {
            return false;
        }
        focus->liveValue += key;
        return true;
    }
    return false;
}

void DispatchScroll(Document& document, int delta, int viewportHeight, int contentHeight) {
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
