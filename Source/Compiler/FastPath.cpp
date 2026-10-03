#include "Compiler/FastPath.h"

#include "Core/Utf8.h"

namespace RoboWeb {

namespace {

bool SlowScript(const std::string& source) {
    return source.find("eval(") != std::string::npos || source.find("innerHTML") != std::string::npos ||
           source.find("function") != std::string::npos || source.find('{') != std::string::npos || source.find("=>") != std::string::npos;
}

void Walk(const Node& node, const Document& document, Diagnostics& diagnostics) {
    const std::string tag = ToLowerAscii(node.tagName);
    if (node.kind == ElementKind::Marquee) {
        diagnostics.Warning(document.sourcePath, node.line, node.column,
            "marquee stays on the general engine and will not have C++ performance");
    }
    if (tag == "animate" || tag == "set" || tag == "animatetransform" || tag == "animatemotion") {
        diagnostics.Warning(document.sourcePath, node.line, node.column,
            "animation stays on the general engine and will not have C++ performance");
    }
    if (node.kind == ElementKind::Script && SlowScript(GatherText(node))) {
        diagnostics.Warning(document.sourcePath, node.line, node.column,
            "script stays on the interpreter and will not have C++ performance");
    }
    if (node.kind == ElementKind::Style) {
        const std::string css = GatherText(node);
        if (css.find("@keyframes") != std::string::npos || css.find("animation") != std::string::npos) {
            diagnostics.Warning(document.sourcePath, node.line, node.column,
                "animation stays on the general engine and will not have C++ performance");
        }
    }
    const char* events[] = {"onclick", "onload", "oninput", "onchange"};
    for (const char* event : events) {
        if (SlowScript(AttributeValue(node, event))) {
            diagnostics.Warning(document.sourcePath, node.line, node.column,
                "script stays on the interpreter and will not have C++ performance");
            break;
        }
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        Walk(*child, document, diagnostics);
    }
}

}  // namespace

void WarnSlowPaths(const Document& document, Diagnostics& diagnostics) {
    if (document.root == nullptr) {
        return;
    }
    Walk(*document.root, document, diagnostics);
}

}
