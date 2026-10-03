#include "Compiler/FastPath.h"

#include "Core/Utf8.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace RoboWeb {

namespace {

std::vector<std::string> SplitCssTokens(const std::string& value) {
    std::vector<std::string> tokens;
    std::size_t index = 0;
    while (index < value.size()) {
        while (index < value.size() && (value[index] == ' ' || value[index] == '\t')) {
            ++index;
        }
        if (index >= value.size()) {
            break;
        }
        const std::size_t start = index;
        while (index < value.size() && value[index] != ' ' && value[index] != '\t') {
            ++index;
        }
        tokens.push_back(value.substr(start, index - start));
    }
    return tokens;
}

bool SlowScript(const std::string& source) {
    return source.find("eval(") != std::string::npos || source.find("innerHTML") != std::string::npos ||
           source.find("function") != std::string::npos || source.find('{') != std::string::npos || source.find("=>") != std::string::npos;
}

// True when the text carries a text-shadow whose blur length is nonzero. The
// first two lengths are the offset; a third is the blur.
bool BlurredTextShadow(const std::string& css) {
    std::size_t index = css.find("text-shadow");
    while (index != std::string::npos) {
        const std::size_t colon = css.find(':', index);
        if (colon == std::string::npos) {
            return false;
        }
        const std::size_t end = css.find_first_of(";}", colon);
        const std::string value = css.substr(colon + 1, end == std::string::npos ? std::string::npos : end - colon - 1);
        const std::vector<std::string> tokens = SplitCssTokens(value);
        int lengths = 0;
        for (const std::string& token : tokens) {
            const std::size_t unit = token.find_first_not_of("-+.0123456789");
            if (unit == std::string::npos || token.compare(unit, 2, "px") != 0) {
                continue;
            }
            ++lengths;
            if (lengths == 3) {
                const double blur = std::strtod(token.c_str(), nullptr);
                if (blur > 0) {
                    return true;
                }
            }
        }
        index = css.find("text-shadow", end == std::string::npos ? css.size() : end);
    }
    return false;
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
        // A blurred text-shadow softens glyph outlines, which the cached draw
        // records cannot express, so that region keeps the general engine.
        if (BlurredTextShadow(css)) {
            diagnostics.Warning(document.sourcePath, node.line, node.column,
                "blurred text-shadow stays on the general engine and will not have C++ performance");
        }
    }
    if (HasAttribute(node, "style") && BlurredTextShadow(AttributeValue(node, "style"))) {
        diagnostics.Warning(document.sourcePath, node.line, node.column,
            "blurred text-shadow stays on the general engine and will not have C++ performance");
    }
    const std::string inlineStyle = ToLowerAscii(AttributeValue(node, "style"));
    if (inlineStyle.find("box-shadow") != std::string::npos && inlineStyle.find("inset") != std::string::npos) {
        diagnostics.Warning(document.sourcePath, node.line, node.column,
            "inset box-shadow stays on the general engine and will not have C++ performance");
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
    if (document.root != nullptr) {
        Walk(*document.root, document, diagnostics);
    }
    for (const ElementStyle& rule : document.styleRules) {
        for (const StyleDeclaration& declaration : rule.declarations) {
            if (declaration.name == "box-shadow" && ToLowerAscii(declaration.value).find("inset") != std::string::npos) {
                diagnostics.Warning(document.sourcePath, 1, 1,
                    "inset box-shadow stays on the general engine and will not have C++ performance");
            }
        }
    }
}

}
