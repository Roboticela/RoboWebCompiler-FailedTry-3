#include "Validate/TreeValidator.h"

#include "Catalog/AttributeCatalog.h"
#include "Core/Utf8.h"

#include <initializer_list>
#include <set>
#include <sstream>

namespace RoboWeb {

namespace {

bool ContainsToken(const char* list, const std::string& value) {
    std::stringstream stream(list);
    std::string token;
    while (stream >> token) {
        if (token == value) {
            return true;
        }
    }
    return false;
}

void WarnEnum(
    const Document& document,
    Diagnostics& diagnostics,
    const Attribute& attribute,
    const char* allowed,
    const char* invalidDefault) {
    const std::string value = ToLowerAscii(attribute.value);
    if (value.empty() && std::string(allowed).find(" \"\" ") == std::string::npos && ContainsToken(allowed, "\"\"")) {
        return;
    }
    if (!ContainsToken(allowed, value) && !(attribute.value.empty() && ContainsToken(allowed, "empty"))) {
        diagnostics.Warning(
            document.sourcePath, attribute.line, attribute.column,
            "attribute '" + attribute.name + "' has invalid value '" + attribute.value + "'; using '" +
                invalidDefault + "'");
    }
}

bool ParentIs(const Node& node, ElementKind kind) {
    return node.parent != nullptr && node.parent->kind == kind;
}

bool ParentIsOneOf(const Node& node, std::initializer_list<ElementKind> kinds) {
    if (node.parent == nullptr) {
        return false;
    }
    for (ElementKind kind : kinds) {
        if (node.parent->kind == kind) {
            return true;
        }
    }
    return false;
}

void ValidateNode(const Document& document, Diagnostics& diagnostics, const Node& node, std::set<std::string>& ids, int& geolocationCount) {
    if (node.type == Node::Type::Element) {
        if (node.kind == ElementKind::Geolocation) {
            ++geolocationCount;
            if (geolocationCount > MAX_GEOLOCATION_CONTROLS) {
                diagnostics.Warning(
                    document.sourcePath, node.line, node.column,
                    "more than three geolocation elements disables their controls");
            }
            diagnostics.Warning(
                document.sourcePath, node.line, node.column,
                "geolocation cannot read a device location in this runtime");
        }
        if (node.kind == ElementKind::Script) {
            diagnostics.Warning(
                document.sourcePath, node.line, node.column, "script is not executed; scripting is not implemented");
        }
        if (node.kind == ElementKind::Style) {
            diagnostics.Warning(
                document.sourcePath, node.line, node.column, "CSS is not implemented; the style element is ignored");
        }
        if (node.kind == ElementKind::Noscript) {
            diagnostics.Warning(
                document.sourcePath, node.line, node.column,
                "scripting is disabled, so noscript content is rendered");
        }
        if (node.kind == ElementKind::Template) {
            diagnostics.Warning(
                document.sourcePath, node.line, node.column,
                "template content is not cloned because scripting is not implemented");
        }
        if (node.kind == ElementKind::Slot) {
            diagnostics.Warning(
                document.sourcePath, node.line, node.column,
                "shadow DOM is not implemented; slot fallback content is rendered");
        }
        if (node.kind == ElementKind::Bdo && !HasAttribute(node, "dir")) {
            diagnostics.Warning(document.sourcePath, node.line, node.column, "bdo element is missing required attribute 'dir'");
        }
        if ((node.kind == ElementKind::Img || node.kind == ElementKind::Area) && !HasAttribute(node, "alt")) {
            diagnostics.Warning(
                document.sourcePath, node.line, node.column,
                "element '" + node.tagName + "' is missing attribute 'alt'");
        }
        if (node.kind == ElementKind::A && HasAttribute(node, "href")) {
            const std::string href = AttributeValue(node, "href");
            if (href.find("://") != std::string::npos) {
                diagnostics.Warning(
                    document.sourcePath, node.line, node.column,
                    "hyperlink navigation is not performed; the href is preserved");
            }
        }
        if (node.kind == ElementKind::Form && HasAttribute(node, "action")) {
            diagnostics.Warning(
                document.sourcePath, node.line, node.column,
                "form submission does not navigate; the form data set is computed when the form is submitted");
        }
        if ((node.kind == ElementKind::Img || node.kind == ElementKind::Video || node.kind == ElementKind::Audio ||
             node.kind == ElementKind::Iframe || node.kind == ElementKind::Embed || node.kind == ElementKind::Object ||
             node.kind == ElementKind::FencedFrame || node.kind == ElementKind::Frame ||
             node.kind == ElementKind::Script || node.kind == ElementKind::Source) &&
            HasAttribute(node, "src")) {
            const std::string src = AttributeValue(node, "src");
            if (src.find("://") != std::string::npos) {
                diagnostics.Warning(
                    document.sourcePath, node.line, node.column,
                    "remote resource '" + src + "' is not loaded");
            }
        }
        if (node.kind == ElementKind::Li && !ParentIsOneOf(node, {ElementKind::Ul, ElementKind::Ol, ElementKind::Menu, ElementKind::Dir})) {
            diagnostics.Warning(document.sourcePath, node.line, node.column, "li element is not inside ul, ol, or menu");
        }
        if ((node.kind == ElementKind::Dt || node.kind == ElementKind::Dd) && !ParentIs(node, ElementKind::Dl)) {
            diagnostics.Warning(document.sourcePath, node.line, node.column, node.tagName + " element is not inside dl");
        }
        if ((node.kind == ElementKind::Td || node.kind == ElementKind::Th) && !ParentIs(node, ElementKind::Tr)) {
            diagnostics.Warning(document.sourcePath, node.line, node.column, node.tagName + " element is not inside tr");
        }
        if (node.kind == ElementKind::Tr &&
            !ParentIsOneOf(node, {ElementKind::Table, ElementKind::Thead, ElementKind::Tbody, ElementKind::Tfoot})) {
            diagnostics.Warning(document.sourcePath, node.line, node.column, "tr element is not inside a table");
        }
        if (node.kind == ElementKind::Figcaption && !ParentIs(node, ElementKind::Figure)) {
            diagnostics.Warning(document.sourcePath, node.line, node.column, "figcaption element is not inside figure");
        }
        if (node.kind == ElementKind::Legend && !ParentIs(node, ElementKind::Fieldset)) {
            diagnostics.Warning(document.sourcePath, node.line, node.column, "legend element is not inside fieldset");
        }
        if (node.kind == ElementKind::Summary && !ParentIs(node, ElementKind::Details)) {
            diagnostics.Warning(document.sourcePath, node.line, node.column, "summary element is not inside details");
        }
        if (node.kind == ElementKind::Option &&
            !ParentIsOneOf(node, {ElementKind::Select, ElementKind::Optgroup, ElementKind::Datalist})) {
            diagnostics.Warning(
                document.sourcePath, node.line, node.column, "option element is not inside select, optgroup, or datalist");
        }
        if (node.kind == ElementKind::Optgroup && !ParentIs(node, ElementKind::Select)) {
            diagnostics.Warning(document.sourcePath, node.line, node.column, "optgroup element is not inside select");
        }
        for (const Attribute& attribute : node.attributes) {
            const std::string name = ToLowerAscii(attribute.name);
            if (name == "style") {
                diagnostics.Warning(
                    document.sourcePath, attribute.line, attribute.column,
                    "CSS is not implemented; the style attribute is ignored");
            }
            if (IsEventHandlerName(name)) {
                diagnostics.Warning(
                    document.sourcePath, attribute.line, attribute.column,
                    "event handler '" + name + "' is not executed because scripting is not implemented");
            } else if (name.size() > 2 && name[0] == 'o' && name[1] == 'n' && !IsKnownAttribute(name)) {
                diagnostics.Warning(
                    document.sourcePath, attribute.line, attribute.column, "unknown event handler '" + name + "'");
            }
            if (!IsKnownAttribute(name)) {
                diagnostics.Warning(
                    document.sourcePath, attribute.line, attribute.column, "unknown attribute '" + name + "'");
            } else if (!AttributeAppliesTo(name, node.kind == ElementKind::Unknown ? ElementKind::Span : node.kind)) {
                diagnostics.Warning(
                    document.sourcePath, attribute.line, attribute.column,
                    "attribute '" + name + "' is not allowed on element '" + node.tagName + "'");
            }
            const AttributeRule* rule = FindAttributeRule(name);
            if (rule != nullptr && (rule->flags & ATTRIBUTE_OBSOLETE) != 0) {
                diagnostics.Warning(
                    document.sourcePath, attribute.line, attribute.column,
                    "attribute '" + name + "' is obsolete; it is still applied as a presentational hint");
            }
            if (name == "id") {
                if (attribute.value.empty() || attribute.value.find_first_of(" \t\r\n") != std::string::npos) {
                    diagnostics.Warning(
                        document.sourcePath, attribute.line, attribute.column, "id '" + attribute.value + "' is not a valid identifier");
                } else if (!ids.insert(attribute.value).second) {
                    diagnostics.Warning(
                        document.sourcePath, attribute.line, attribute.column, "duplicate id '" + attribute.value + "'");
                }
            }
            if (name == "dir") {
                WarnEnum(document, diagnostics, attribute, "ltr rtl auto", "ltr");
            }
            if (name == "hidden") {
                const std::string value = ToLowerAscii(attribute.value);
                if (!value.empty() && value != "hidden" && value != "until-found") {
                    diagnostics.Warning(
                        document.sourcePath, attribute.line, attribute.column,
                        "attribute 'hidden' has invalid value '" + attribute.value + "'; using 'hidden'");
                }
            }
            if (name == "contenteditable") {
                const std::string value = ToLowerAscii(attribute.value);
                if (!value.empty() && value != "true" && value != "false" && value != "plaintext-only") {
                    diagnostics.Warning(
                        document.sourcePath, attribute.line, attribute.column,
                        "attribute 'contenteditable' has invalid value '" + attribute.value + "'");
                }
            }
            if (name == "draggable") {
                const std::string value = ToLowerAscii(attribute.value);
                if (value != "true" && value != "false") {
                    diagnostics.Warning(
                        document.sourcePath, attribute.line, attribute.column,
                        "attribute 'draggable' has invalid value '" + attribute.value + "'; using 'true'");
                }
                if (value == "true") {
                    diagnostics.Warning(
                        document.sourcePath, attribute.line, attribute.column,
                        "drag and drop is not implemented; draggable has no effect");
                }
            }
            if (name == "popover") {
                const std::string value = ToLowerAscii(attribute.value);
                if (!value.empty() && value != "auto" && value != "manual") {
                    diagnostics.Warning(
                        document.sourcePath, attribute.line, attribute.column,
                        "attribute 'popover' has invalid value '" + attribute.value + "'; using 'auto'");
                }
            }
            if (name == "type" && node.kind == ElementKind::Input) {
                const char* types =
                    "button checkbox color date datetime-local email file hidden image month number password radio "
                    "range reset search submit tel text time url week";
                if (!ContainsToken(types, ToLowerAscii(attribute.value))) {
                    diagnostics.Warning(
                        document.sourcePath, attribute.line, attribute.column,
                        "input type '" + attribute.value + "' is invalid; using 'text'");
                }
            }
            if (name == "type" && node.kind == ElementKind::Button) {
                const std::string value = ToLowerAscii(attribute.value);
                if (value != "submit" && value != "reset" && value != "button") {
                    diagnostics.Warning(
                        document.sourcePath, attribute.line, attribute.column,
                        "button type '" + attribute.value + "' is invalid; using 'submit'");
                }
            }
            if (name == "method" && node.kind == ElementKind::Form) {
                const std::string value = ToLowerAscii(attribute.value);
                if (value != "get" && value != "post" && value != "dialog") {
                    diagnostics.Warning(
                        document.sourcePath, attribute.line, attribute.column,
                        "form method '" + attribute.value + "' is invalid; using 'get'");
                }
            }
            if ((name == "tabindex" || name == "colspan" || name == "rowspan" || name == "span" || name == "size" ||
                 name == "maxlength" || name == "minlength" || name == "cols" || name == "rows" || name == "start") &&
                !attribute.value.empty()) {
                std::size_t consumed = 0;
                try {
                    std::stoi(attribute.value, &consumed);
                } catch (...) {
                    consumed = 0;
                }
                if (consumed != attribute.value.size()) {
                    diagnostics.Warning(
                        document.sourcePath, attribute.line, attribute.column,
                        "attribute '" + name + "' expects an integer, not '" + attribute.value + "'");
                }
            }
        }
    }
    int mainCount = 0;
    for (const std::unique_ptr<Node>& child : node.children) {
        if (child->kind == ElementKind::Main) {
            ++mainCount;
        }
        ValidateNode(document, diagnostics, *child, ids, geolocationCount);
    }
    if (mainCount > 1) {
        diagnostics.Warning(document.sourcePath, node.line, node.column, "a document should contain only one main element");
    }
}

}  // namespace

void ValidateTree(const Document& document, Diagnostics& diagnostics) {
    if (document.root == nullptr) {
        diagnostics.Error(document.sourcePath, 1, 1, "document is empty");
        return;
    }
    std::set<std::string> ids;
    int geolocationCount = 0;
    ValidateNode(document, diagnostics, *document.root, ids, geolocationCount);
}

}
