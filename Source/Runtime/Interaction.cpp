#include "Runtime/Interaction.h"

#include "Core/Utf8.h"
#include "Runtime/MiniScript.h"

#include <algorithm>
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
    if (HasAttribute(node, "disabled") || HasAttribute(node, "inert") || node.hiddenByScript) {
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
    if (node.type != Node::Type::Element || HasAttribute(node, "disabled") || HasAttribute(node, "hidden") || HasAttribute(node, "inert")) {
        return false;
    }
    if (node.kind == ElementKind::Input) {
        return ToLowerAscii(AttributeValue(node, "type")) != "hidden";
    }
    if (node.kind == ElementKind::Textarea || node.kind == ElementKind::Select || node.kind == ElementKind::Button ||
        node.kind == ElementKind::Summary) {
        return true;
    }
    return node.kind == ElementKind::A && !AttributeValue(node, "href").empty();
}

void CollectFocusable(Node& node, std::vector<Node*>& order) {
    if (node.type == Node::Type::Element && Focusable(node)) {
        order.push_back(&node);
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        CollectFocusable(*child, order);
    }
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
    return true;
}

PointerShape PointerAt(const LayoutResult& layout, int x, int y, int scrollY) {
    Node* hit = HitTest(layout, x, y, scrollY);
    for (Node* node = hit; node != nullptr; node = node->parent) {
        if (IsTextField(*node)) {
            return PointerShape::Text;
        }
        if (node->kind == ElementKind::A && !AttributeValue(*node, "href").empty()) {
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
    if (focus == nullptr) {
        return false;
    }
    const bool editable = IsTextField(*focus) || HasAttribute(*focus, "contenteditable");
    if (!editable || !CanEdit(*focus)) {
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
