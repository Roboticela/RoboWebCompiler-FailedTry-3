#include "Catalog/AttributeCatalog.h"
#include "Catalog/ElementCatalog.h"
#include "Catalog/SvgCatalog.h"
#include "Compiler/CodeGenerator.h"
#include "Compiler/FastPath.h"
#include "Diagnostics/Diagnostics.h"
#include "Dom/Document.h"
#include "Layout/LayoutEngine.h"
#include "Parser/HtmlParser.h"
#include "Render/BitmapFont.h"
#include "Render/ImageDecoder.h"
#include "Render/InterFontData.h"
#include "Render/Renderer.h"
#include "Render/StyleSheet.h"
#include "Runtime/FramePlan.h"
#include "Runtime/Interaction.h"
#include "Runtime/MiniScript.h"
#include "Validate/TreeValidator.h"
#include "Xml/XmlParser.h"
#include "Xml/XPath.h"
#include "Xml/Xslt.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

int gFailed = 0;

void Expect(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::cerr << file << ":" << line << ": expected " << expression << "\n";
        ++gFailed;
    }
}

#define EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __FILE__, __LINE__)

RoboWeb::Document Parse(const std::string& html, RoboWeb::Diagnostics& diagnostics) {
    RoboWeb::HtmlParser parser;
    return parser.Parse(html, "fixture.html", diagnostics);
}

int CountMessages(const RoboWeb::Diagnostics& diagnostics, RoboWeb::Severity severity, const std::string& fragment) {
    int count = 0;
    for (const RoboWeb::Diagnostic& diagnostic : diagnostics.Items()) {
        if (diagnostic.severity == severity && diagnostic.message.find(fragment) != std::string::npos) {
            ++count;
        }
    }
    return count;
}

void TestCatalog() {
    std::string error;
    EXPECT(RoboWeb::CatalogSelfCheck(error));
    EXPECT(RoboWeb::FindElement("div") == RoboWeb::ElementKind::Div);
    EXPECT(RoboWeb::FindElement("h1") == RoboWeb::ElementKind::H1);
    EXPECT(RoboWeb::FindElement("selectedcontent") == RoboWeb::ElementKind::SelectedContent);
    EXPECT(RoboWeb::FindElement("no-such") == RoboWeb::ElementKind::Unknown);
    EXPECT(RoboWeb::SpecFor(RoboWeb::ElementKind::Br).traits & RoboWeb::TRAIT_VOID);
    EXPECT(RoboWeb::SpecFor(RoboWeb::ElementKind::P).display == RoboWeb::DisplayKind::Block);
    EXPECT(RoboWeb::SpecFor(RoboWeb::ElementKind::Span).display == RoboWeb::DisplayKind::Inline);
    EXPECT(RoboWeb::SpecFor(RoboWeb::ElementKind::Li).display == RoboWeb::DisplayKind::ListItem);
    EXPECT(RoboWeb::SpecFor(RoboWeb::ElementKind::Table).display == RoboWeb::DisplayKind::Table);
    EXPECT(RoboWeb::SpecFor(RoboWeb::ElementKind::H1).presentation & RoboWeb::PRESENTATION_BOLD);
    EXPECT(RoboWeb::SpecFor(RoboWeb::ElementKind::Em).presentation & RoboWeb::PRESENTATION_ITALIC);
    EXPECT(RoboWeb::SpecFor(RoboWeb::ElementKind::Pre).presentation & RoboWeb::PRESENTATION_PRE);
    EXPECT(RoboWeb::ElementCount() > 100);
}

void TestEveryElementParses() {
    for (int index = 0; index < static_cast<int>(RoboWeb::ElementKind::Unknown); ++index) {
        const RoboWeb::ElementKind kind = static_cast<RoboWeb::ElementKind>(index);
        const RoboWeb::ElementSpec& spec = RoboWeb::SpecFor(kind);
        if (kind == RoboWeb::ElementKind::Image || kind == RoboWeb::ElementKind::Math || kind == RoboWeb::ElementKind::Svg) {
            continue;
        }
        RoboWeb::Diagnostics diagnostics;
        std::string html = "<!DOCTYPE html><" + std::string(spec.name);
        if ((spec.traits & RoboWeb::TRAIT_VOID) == 0) {
            html += ">text</" + std::string(spec.name) + ">";
        } else {
            html += ">";
        }
        RoboWeb::Document document = Parse(html, diagnostics);
        EXPECT(document.root != nullptr);
        EXPECT(CountMessages(diagnostics, RoboWeb::Severity::Error, "unknown element") == 0);
        if ((spec.traits & RoboWeb::TRAIT_OBSOLETE) != 0) {
            EXPECT(CountMessages(diagnostics, RoboWeb::Severity::Warning, "obsolete") >= 1);
        }
        if ((spec.traits & RoboWeb::TRAIT_EXPERIMENTAL) != 0) {
            EXPECT(CountMessages(diagnostics, RoboWeb::Severity::Warning, "experimental") >= 1);
        }
        EXPECT(spec.fontSizeMilli > 0);
        EXPECT(RoboWeb::DisplayName(spec.display) != nullptr);
    }
}

void TestAttributes() {
    EXPECT(RoboWeb::IsGlobalAttributeName("id"));
    EXPECT(RoboWeb::AttributeAppliesTo("id", RoboWeb::ElementKind::Div));
    EXPECT(RoboWeb::AttributeAppliesTo("href", RoboWeb::ElementKind::A));
    EXPECT(!RoboWeb::AttributeAppliesTo("href", RoboWeb::ElementKind::Div));
    EXPECT(RoboWeb::IsBooleanAttribute("disabled"));
    EXPECT(RoboWeb::IsDataAttribute("data-user"));
    EXPECT(RoboWeb::IsAriaAttribute("aria-label"));
    EXPECT(RoboWeb::IsEventHandlerName("onclick"));
    EXPECT(RoboWeb::EventHandlerCount() > 20);
    RoboWeb::Diagnostics diagnostics;
    RoboWeb::Document document = Parse("<!DOCTYPE html><div id='a' zzzz='1' onclick='bad()'></div>", diagnostics);
    RoboWeb::ValidateTree(document, diagnostics);
    EXPECT(CountMessages(diagnostics, RoboWeb::Severity::Warning, "unknown attribute 'zzzz'") == 1);
    RoboWeb::Diagnostics svgDiagnostics;
    RoboWeb::Document svgDocument = Parse(
        "<!DOCTYPE html><svg width='10' height='10' viewBox='0 0 10 10' xmlns='http://www.w3.org/2000/svg'>"
        "<rect width='10' height='10' rx='1' fill='#000'/>"
        "<?xml version='1.0'?><circle cx='1' cy='1' r='1' stroke='red' stroke-width='1'/>"
        "</svg>",
        svgDiagnostics);
    RoboWeb::ValidateTree(svgDocument, svgDiagnostics);
    EXPECT(CountMessages(svgDiagnostics, RoboWeb::Severity::Warning, "unknown attribute") == 0);
    EXPECT(CountMessages(svgDiagnostics, RoboWeb::Severity::Warning, "not allowed") == 0);
    EXPECT(CountMessages(svgDiagnostics, RoboWeb::Severity::Warning, "processing instruction") == 0);
    EXPECT(CountMessages(diagnostics, RoboWeb::Severity::Warning, "not executed") == 0);
    RoboWeb::Diagnostics duplicate;
    RoboWeb::Document duplicated = Parse("<!DOCTYPE html><div id='a' id='b'></div>", duplicate);
    (void)duplicated;
    EXPECT(duplicate.HasErrors());
    EXPECT(CountMessages(duplicate, RoboWeb::Severity::Error, "duplicate attribute 'id'") == 1);
}

void TestParserRecovery() {
    RoboWeb::Diagnostics diagnostics;
    RoboWeb::Document document = Parse("<!DOCTYPE html><p>Hi</p>", diagnostics);
    EXPECT(!diagnostics.HasErrors());
    EXPECT(!document.quirks);
    EXPECT(document.root->children.size() == 1);
    EXPECT(document.root->children[0]->kind == RoboWeb::ElementKind::Html);
    RoboWeb::Node* body = nullptr;
    for (const std::unique_ptr<RoboWeb::Node>& child : document.root->children[0]->children) {
        if (child->kind == RoboWeb::ElementKind::Body) {
            body = child.get();
        }
    }
    EXPECT(body != nullptr);
    EXPECT(body->children.size() == 1);
    EXPECT(body->children[0]->kind == RoboWeb::ElementKind::P);
    EXPECT(RoboWeb::GatherText(*body) == "Hi");

    RoboWeb::Diagnostics implied;
    RoboWeb::Document impliedDocument = Parse("<p>Hi<div>X</div>", implied);
    EXPECT(CountMessages(implied, RoboWeb::Severity::Warning, "end tag for 'p' was implied") == 1);
    EXPECT(impliedDocument.quirks);

    RoboWeb::Diagnostics stray;
    Parse("<!DOCTYPE html><div></span></div>", stray);
    EXPECT(CountMessages(stray, RoboWeb::Severity::Error, "stray end tag") == 1);

    RoboWeb::Diagnostics unknown;
    Parse("<!DOCTYPE html><notanelement></notanelement>", unknown);
    EXPECT(unknown.HasErrors());

    RoboWeb::Diagnostics custom;
    Parse("<!DOCTYPE html><my-widget>A</my-widget>", custom);
    EXPECT(!custom.HasErrors());
    EXPECT(CountMessages(custom, RoboWeb::Severity::Warning, "custom element") == 1);

    RoboWeb::Diagnostics svg;
    RoboWeb::Document svgDocument = Parse("<!DOCTYPE html><svg width='40' height='20'><rect x='0' y='0' width='40' height='20' fill='red'/></svg>", svg);
    EXPECT(CountMessages(svg, RoboWeb::Severity::Error, "SVG is not implemented") == 0);
    EXPECT(!svg.HasErrors());
    RoboWeb::LayoutResult svgLayout = RoboWeb::LayoutDocument(svgDocument, 120, 80);
    RoboWeb::Framebuffer svgFrame = RoboWeb::RenderDocument(svgLayout, 0);
    const RoboWeb::ElementBox* svgBox = RoboWeb::FindBox(svgLayout, "svg");
    EXPECT(svgBox != nullptr);
    if (svgBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(svgFrame, svgBox->x + 4, svgBox->y + 4) == 255);
    }

    RoboWeb::Diagnostics math;
    RoboWeb::Document mathDocument = Parse("<!DOCTYPE html><math><mi>x</mi><mo>+</mo><mn>1</mn></math>", math);
    EXPECT(CountMessages(math, RoboWeb::Severity::Error, "MathML is not implemented") == 0);
    EXPECT(RoboWeb::GatherText(*mathDocument.root).find("x") != std::string::npos);

    RoboWeb::Diagnostics image;
    RoboWeb::Document imageDocument = Parse("<!DOCTYPE html><image alt='picture'>", image);
    EXPECT(CountMessages(image, RoboWeb::Severity::Warning, "obsolete alias") == 1);
    EXPECT(RoboWeb::GatherText(*imageDocument.root).empty());

    RoboWeb::Diagnostics entities;
    RoboWeb::Document entityDocument = Parse("<!DOCTYPE html><p>A&amp;B&#65;&lt;</p>", entities);
    EXPECT(RoboWeb::GatherText(*entityDocument.root) == "A&BA<");

    RoboWeb::Diagnostics script;
    RoboWeb::Document scriptDocument = Parse("<!DOCTYPE html><script>1 < 2 &amp;</script>", script);
    EXPECT(RoboWeb::GatherText(*scriptDocument.root) == "1 < 2 &amp;");

    RoboWeb::Diagnostics title;
    RoboWeb::Document titleDocument = Parse("<!DOCTYPE html><title>A&amp;B</title><p>X</p>", title);
    EXPECT(titleDocument.title == "A&B");

    RoboWeb::Diagnostics comment;
    Parse("<!DOCTYPE html><!-- comment --><p>Z</p>", comment);
    EXPECT(!comment.HasErrors());

    RoboWeb::Diagnostics openComment;
    Parse("<!DOCTYPE html><!-- comment", openComment);
    EXPECT(openComment.HasErrors());
}

void TestLayoutMatchesBrowserDefaults() {
    RoboWeb::Diagnostics diagnostics;
    RoboWeb::Document document = Parse("<!DOCTYPE html><p>Hi</p>", diagnostics);
    RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(document, 400, 200);
    const RoboWeb::ElementBox* paragraph = RoboWeb::FindBox(layout, "p");
    EXPECT(paragraph != nullptr);
    if (paragraph != nullptr) {
        EXPECT(paragraph->x == 8);
        EXPECT(paragraph->y == 16);
        EXPECT(paragraph->width == 384);
        EXPECT(paragraph->height == 19);
        EXPECT(paragraph->fontSizePx == 16);
    }
    RoboWeb::Document divDocument = Parse("<!DOCTYPE html><div>Hi</div>", diagnostics);
    RoboWeb::LayoutResult divLayout = RoboWeb::LayoutDocument(divDocument, 400, 200);
    const RoboWeb::ElementBox* div = RoboWeb::FindBox(divLayout, "div");
    EXPECT(div != nullptr);
    if (div != nullptr) {
        EXPECT(div->x == 8);
        EXPECT(div->y == 8);
        EXPECT(div->height == 19);
    }
    RoboWeb::Document headings = Parse("<!DOCTYPE html><h1>H</h1>", diagnostics);
    RoboWeb::LayoutResult headingLayout = RoboWeb::LayoutDocument(headings, 400, 200);
    const RoboWeb::ElementBox* heading = RoboWeb::FindBox(headingLayout, "h1");
    EXPECT(heading != nullptr);
    if (heading != nullptr) {
        EXPECT(heading->fontSizePx == 32);
        EXPECT(heading->bold);
        EXPECT(heading->y == 21);
        EXPECT(heading->height == RoboWeb::LineHeightPx(32));
    }
    RoboWeb::Document hidden = Parse("<!DOCTYPE html><p hidden>Secret</p><p>Shown</p>", diagnostics);
    RoboWeb::LayoutResult hiddenLayout = RoboWeb::LayoutDocument(hidden, 400, 200);
    int paragraphs = 0;
    for (const RoboWeb::ElementBox& box : hiddenLayout.boxes) {
        if (box.tag == "p") {
            ++paragraphs;
        }
    }
    EXPECT(paragraphs == 1);

    RoboWeb::Document pre = Parse("<!DOCTYPE html><pre>A\nB</pre>", diagnostics);
    RoboWeb::LayoutResult preLayout = RoboWeb::LayoutDocument(pre, 400, 200);
    const RoboWeb::ElementBox* preBox = RoboWeb::FindBox(preLayout, "pre");
    EXPECT(preBox != nullptr);
    if (preBox != nullptr) {
        EXPECT(preBox->monospace);
        EXPECT(preBox->height == RoboWeb::LineHeightPx(16) * 2);
    }

    RoboWeb::Document tableDocument = Parse(
        "<!DOCTYPE html><table border='1'><caption>Sample</caption><tr><th>Element</th><th>Display</th></tr><tr><td>p</td><td>block</td></tr></table>",
        diagnostics);
    RoboWeb::LayoutResult tableLayout = RoboWeb::LayoutDocument(tableDocument, 800, 400);
    const RoboWeb::ElementBox* table = RoboWeb::FindBox(tableLayout, "table");
    EXPECT(table != nullptr);
    if (table != nullptr) {
        EXPECT(table->width < 360);
        EXPECT(table->width > 40);
        EXPECT(table->y >= 8);
    }
    const RoboWeb::ElementBox* caption = RoboWeb::FindBox(tableLayout, "caption");
    EXPECT(caption != nullptr);
    bool textAtOrigin = false;
    for (const RoboWeb::DrawOp& op : tableLayout.ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.y < 8 && op.text.find("block") != std::string::npos) {
            textAtOrigin = true;
        }
    }
    EXPECT(!textAtOrigin);
}

void TestPixelsAndInteraction() {
    RoboWeb::Diagnostics diagnostics;
    RoboWeb::Document document = Parse("<!DOCTYPE html><p><a href='#part'>Hi</a> <mark>M</mark></p>", diagnostics);
    RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(document, 200, 80);
    RoboWeb::Framebuffer framebuffer = RoboWeb::RenderDocument(layout, 0);
    bool sawBlue = false;
    bool sawYellow = false;
    bool sawBlack = false;
    for (int y = 0; y < framebuffer.height; ++y) {
        for (int x = 0; x < framebuffer.width; ++x) {
            const int red = RoboWeb::PixelRed(framebuffer, x, y);
            const int green = RoboWeb::PixelGreen(framebuffer, x, y);
            const int blue = RoboWeb::PixelBlue(framebuffer, x, y);
            if (red == 0 && green == 0 && blue == 238) {
                sawBlue = true;
            }
            if (red == 255 && green == 255 && blue == 0) {
                sawYellow = true;
            }
            if (red == 0 && green == 0 && blue == 0) {
                sawBlack = true;
            }
        }
    }
    EXPECT(sawBlue);
    EXPECT(sawYellow);
    EXPECT(sawBlack);

    RoboWeb::Document details = Parse("<!DOCTYPE html><details><summary>More</summary><p>Inside</p></details>", diagnostics);
    RoboWeb::LayoutResult closed = RoboWeb::LayoutDocument(details, 300, 200);
    EXPECT(RoboWeb::FindBox(closed, "p") == nullptr);
    const RoboWeb::ElementBox* summary = RoboWeb::FindBox(closed, "summary");
    EXPECT(summary != nullptr);
    if (summary != nullptr) {
        RoboWeb::DispatchClick(details, closed, summary->x + 2, summary->y + 2);
    }
    RoboWeb::LayoutResult opened = RoboWeb::LayoutDocument(details, 300, 200);
    EXPECT(RoboWeb::FindBox(opened, "p") != nullptr);

    RoboWeb::Document form = Parse("<!DOCTYPE html><form><input type='checkbox' name='a' id='a'><label for='a'>A</label></form>", diagnostics);
    RoboWeb::LayoutResult formLayout = RoboWeb::LayoutDocument(form, 300, 120);
    const RoboWeb::ElementBox* checkbox = RoboWeb::FindBox(formLayout, "input");
    EXPECT(checkbox != nullptr);
    if (checkbox != nullptr) {
        RoboWeb::DispatchClick(form, formLayout, checkbox->x + 2, checkbox->y + 2);
    }
    RoboWeb::Node* input = nullptr;
    std::vector<RoboWeb::Node*> stack{form.root.get()};
    while (!stack.empty()) {
        RoboWeb::Node* current = stack.back();
        stack.pop_back();
        if (current->kind == RoboWeb::ElementKind::Input) {
            input = current;
        }
        for (const std::unique_ptr<RoboWeb::Node>& child : current->children) {
            stack.push_back(child.get());
        }
    }
    EXPECT(input != nullptr && input->checked);
    if (input != nullptr) {
        form.focus = input;
        input->kind = RoboWeb::ElementKind::Input;
        RoboWeb::Document field = Parse("<!DOCTYPE html><input type='text' value='ab'>", diagnostics);
        RoboWeb::Node* textInput = nullptr;
        stack = {field.root.get()};
        while (!stack.empty()) {
            RoboWeb::Node* current = stack.back();
            stack.pop_back();
            if (current->kind == RoboWeb::ElementKind::Input) {
                textInput = current;
            }
            for (const std::unique_ptr<RoboWeb::Node>& child : current->children) {
                stack.push_back(child.get());
            }
        }
        EXPECT(textInput != nullptr && textInput->liveValue == "ab");
        if (textInput != nullptr) {
            field.focus = textInput;
            EXPECT(RoboWeb::DispatchKey(field, "c"));
            EXPECT(textInput->liveValue == "abc");
            EXPECT(RoboWeb::DispatchKey(field, "backspace"));
            EXPECT(textInput->liveValue == "ab");
            field.caretOffset = 1;
            EXPECT(RoboWeb::DispatchKey(field, "Z"));
            EXPECT(textInput->liveValue == "aZb");
            EXPECT(field.caretOffset == 2);
            EXPECT(RoboWeb::DispatchKey(field, "left"));
            EXPECT(field.caretOffset == 1);
            EXPECT(RoboWeb::DispatchKey(field, "delete"));
            EXPECT(textInput->liveValue == "ab");
            field.caretOffset = -1;
            field.caretOn = true;
            RoboWeb::LayoutResult fieldLayout = RoboWeb::LayoutDocument(field, 320, 80);
            RoboWeb::Framebuffer fieldFrame = RoboWeb::RenderDocument(fieldLayout, 0);
            const RoboWeb::ElementBox* fieldBox = RoboWeb::FindBox(fieldLayout, "input");
            EXPECT(fieldBox != nullptr);
            if (fieldBox != nullptr) {
                const int textX = fieldBox->x + RoboWeb::CONTROL_BORDER_PX + RoboWeb::CONTROL_PADDING_X_PX;
                const int textY = fieldBox->y + RoboWeb::CONTROL_BORDER_PX + RoboWeb::CONTROL_PADDING_Y_PX;
                const int caretX = textX + RoboWeb::MeasureTextPx(textInput->liveValue, fieldBox->fontSizePx, fieldBox->bold, fieldBox->italic);
                const int caretY = textY + RoboWeb::LineHeightPx(fieldBox->fontSizePx) - 1;
                EXPECT(RoboWeb::PixelRed(fieldFrame, caretX, caretY) == 0);
                EXPECT(RoboWeb::PixelGreen(fieldFrame, caretX, caretY) == 0);
                EXPECT(RoboWeb::PixelBlue(fieldFrame, caretX, caretY) == 0);
                EXPECT(RoboWeb::PointerAt(fieldLayout, textX + 2, textY + 2, 0) == RoboWeb::PointerShape::Text);
            }
        }
    }
    const RoboWeb::ElementBox* link = RoboWeb::FindBox(layout, "a");
    EXPECT(link != nullptr);
    if (link != nullptr) {
        EXPECT(RoboWeb::PointerAt(layout, link->x + 1, link->y + 1, 0) == RoboWeb::PointerShape::Hand);
        EXPECT(RoboWeb::DispatchHover(document, layout, link->x + 1, link->y + 1));
        EXPECT(document.hover == link->node);
    }
}

void TestStaticGeneration() {
    RoboWeb::Diagnostics diagnostics;
    RoboWeb::Document document = Parse("<!DOCTYPE html><div><p id='lead'>Hello</p></div>", diagnostics);
    const std::string cpp = RoboWeb::GenerateCpp(document);
    EXPECT(cpp.find("kCompiledDocument") != std::string::npos);
    EXPECT(cpp.find("StaticNode") != std::string::npos);
    EXPECT(cpp.find("BuildFromStatic") != std::string::npos);
    EXPECT(cpp.find("MakeElement") == std::string::npos);
    EXPECT(cpp.find("Hello") != std::string::npos);
    EXPECT(cpp.find("RunDocument") != std::string::npos);
    EXPECT(cpp.find("Performance contract") != std::string::npos);
    std::unique_ptr<RoboWeb::Node> rebuilt = RoboWeb::RebuildStatic(document);
    EXPECT(rebuilt != nullptr);
    EXPECT(RoboWeb::GatherText(*rebuilt).find("Hello") != std::string::npos);
    bool bodyContainsParagraph = false;
    std::vector<RoboWeb::Node*> stack{rebuilt.get()};
    while (!stack.empty()) {
        RoboWeb::Node* current = stack.back();
        stack.pop_back();
        if (current->kind == RoboWeb::ElementKind::Body && RoboWeb::GatherText(*current).find("Hello") != std::string::npos) {
            bodyContainsParagraph = true;
        }
        for (const std::unique_ptr<RoboWeb::Node>& child : current->children) {
            stack.push_back(child.get());
        }
    }
    EXPECT(bodyContainsParagraph);
}

void TestStyleRule() {
    RoboWeb::Diagnostics diagnostics;
    RoboWeb::Document document = Parse("<!DOCTYPE html><style>p { color: #ff0000; }</style><p>Red</p>", diagnostics);
    RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(document, 200, 80);
    bool red = false;
    for (const RoboWeb::DrawOp& op : layout.ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.color.red == 255 && op.color.green == 0 && op.color.blue == 0) {
            red = true;
        }
    }
    EXPECT(red);
    EXPECT(CountMessages(diagnostics, RoboWeb::Severity::Warning, "CSS is not implemented") == 0);
}

void TestCssFunctions() {
    RoboWeb::Diagnostics diagnostics;
    auto widthOf = [&](const std::string& html) {
        RoboWeb::Document document = Parse(html, diagnostics);
        RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(document, 240, 120);
        const RoboWeb::ElementBox* box = RoboWeb::FindBox(layout, "div");
        return box == nullptr ? -1 : box->width;
    };
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: calc(10px + 22px); height: 8px }</style><div></div>") == 32);
    EXPECT(widthOf("<!DOCTYPE html><style>:root { --box: 36px } div { width: var(--box); height: 8px }</style><div></div>") == 36);
    EXPECT(widthOf("<!DOCTYPE html><style>div { --n: 16px; width: var(--missing, var(--n)); height: 8px }</style><div></div>") == 16);
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: min(100px, 18px); height: 8px }</style><div></div>") == 18);
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: max(4px, 21px); height: 8px }</style><div></div>") == 21);
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: clamp(10px, 50px, 24px); height: 8px }</style><div></div>") == 24);
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: calc(sin(90deg) * 30px); height: 8px }</style><div></div>") == 30);
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: round(nearest, 13px, 5px); height: 8px }</style><div></div>") == 15);
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: mod(18px, 5px); height: 8px }</style><div></div>") == 3);
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: calc(hypot(3px, 4px)); height: 8px }</style><div></div>") == 5);
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: calc(pow(2, 3) * 3px); height: 8px }</style><div></div>") == 24);
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: calc(progress(30px, 10px, 50px) * 40px); height: 8px }</style><div></div>") == 20);
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: calc(abs(-18px)); height: 8px }</style><div></div>") == 18);
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: rem(18px, 5px); height: 8px }</style><div></div>") == 3);
    EXPECT(widthOf("<!DOCTYPE html><style>div { width: mod(-18px, 5px); height: 8px }</style><div></div>") == 2);
    EXPECT(widthOf("<!DOCTYPE html><div style='width: calc(cos(0deg) * 11px); height: 8px'></div>") == 11);
    EXPECT(widthOf("<!DOCTYPE html><div style='width: calc(sqrt(9) * 4px); height: 8px'></div>") == 12);
    EXPECT(widthOf("<!DOCTYPE html><div style='width: calc((sign(-5px) + 2) * 8px); height: 8px'></div>") == 8);
    EXPECT(widthOf("<!DOCTYPE html><div style='width: calc(log(100, 10) * 4px); height: 8px'></div>") == 8);
    EXPECT(widthOf("<!DOCTYPE html><div style='width: calc(exp(0) * 14px); height: 8px'></div>") == 14);
    EXPECT(widthOf("<!DOCTYPE html><div style='width: calc(atan2(4px, 4px) / 45deg * 10px); height: 8px'></div>") == 10);
    EXPECT(widthOf("<!DOCTYPE html><div style='width: calc(tan(45deg) * 9px); height: 8px'></div>") == 9);
    EXPECT(widthOf("<!DOCTYPE html><div style='width: calc(asin(1) / 90deg * 20px); height: 8px'></div>") == 20);
    EXPECT(widthOf("<!DOCTYPE html><div style='width:1in;height:8px'></div>") == 96);
    EXPECT(widthOf("<!DOCTYPE html><div style='width:10vw;height:8px'></div>") == 24);
    EXPECT(widthOf("<!DOCTYPE html><div style='width:10vh;height:8px'></div>") == 12);
    EXPECT(widthOf("<!DOCTYPE html><style>html { font-size: 20px }</style><div style='width:2rem;height:8px'></div>") == 40);
    EXPECT(widthOf("<!DOCTYPE html><div style='font-size:10px;width:3em;height:8px'></div>") == 30);
    RoboWeb::Document percentBox = Parse(
        "<!DOCTYPE html><div style='width:100px'><div id='inner' style='width:calc(50% + 10px);height:8px'></div></div>", diagnostics);
    RoboWeb::LayoutResult percentLayout = RoboWeb::LayoutDocument(percentBox, 240, 120);
    int innerWidth = -1;
    for (const RoboWeb::ElementBox& box : percentLayout.boxes) {
        if (box.node != nullptr && RoboWeb::AttributeValue(*box.node, "id") == "inner") {
            innerWidth = box.width;
        }
    }
    EXPECT(innerWidth == 60);
    RoboWeb::Document containerBox = Parse(
        "<!DOCTYPE html><div style='width:100px;container-type:inline-size'><div id='inner' style='width:50cqw;height:8px'></div></div>",
        diagnostics);
    RoboWeb::LayoutResult containerLayout = RoboWeb::LayoutDocument(containerBox, 240, 120);
    int containerWidth = -1;
    for (const RoboWeb::ElementBox& box : containerLayout.boxes) {
        if (box.node != nullptr && RoboWeb::AttributeValue(*box.node, "id") == "inner") {
            containerWidth = box.width;
        }
    }
    EXPECT(containerWidth == 50);
    RoboWeb::Document shifted = Parse(
        "<!DOCTYPE html><div style='width:10px;height:8px;margin:0'></div><div id='moved' style='width:10px;height:8px;margin:0;transform:translate(30px, 0px)'></div>",
        diagnostics);
    RoboWeb::LayoutResult shiftedLayout = RoboWeb::LayoutDocument(shifted, 240, 120);
    int plainX = -1;
    int movedX = -1;
    for (const RoboWeb::ElementBox& box : shiftedLayout.boxes) {
        if (box.tag == "div" && plainX < 0) {
            plainX = box.x;
        }
        if (box.node != nullptr && RoboWeb::AttributeValue(*box.node, "id") == "moved") {
            movedX = box.x;
        }
    }
    EXPECT(movedX - plainX == 30);
    RoboWeb::Document columns = Parse(
        "<!DOCTYPE html><div style='display:grid;grid-template-columns:20px 40px'><div id='a' style='height:8px'></div><div id='b' style='height:8px'></div></div>",
        diagnostics);
    RoboWeb::LayoutResult columnLayout = RoboWeb::LayoutDocument(columns, 240, 120);
    int columnA = -1;
    int columnB = -1;
    for (const RoboWeb::ElementBox& box : columnLayout.boxes) {
        if (box.node == nullptr) {
            continue;
        }
        if (RoboWeb::AttributeValue(*box.node, "id") == "a") {
            columnA = box.x;
        }
        if (RoboWeb::AttributeValue(*box.node, "id") == "b") {
            columnB = box.x;
        }
    }
    EXPECT(columnB - columnA == 20);
    RoboWeb::Document gradient = Parse(
        "<!DOCTYPE html><div style='width:20px;height:8px;background:linear-gradient(to right, #ff0000, #0000ff)'></div>", diagnostics);
    RoboWeb::LayoutResult gradientLayout = RoboWeb::LayoutDocument(gradient, 240, 120);
    RoboWeb::Framebuffer gradientFrame = RoboWeb::RenderDocument(gradientLayout, 0);
    const RoboWeb::ElementBox* gradientBox = RoboWeb::FindBox(gradientLayout, "div");
    EXPECT(gradientBox != nullptr);
    if (gradientBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(gradientFrame, gradientBox->x, gradientBox->y + 2) == 255);
        EXPECT(RoboWeb::PixelBlue(gradientFrame, gradientBox->x + gradientBox->width - 1, gradientBox->y + 2) > 200);
    }
    RoboWeb::Document gray = Parse(
        "<!DOCTYPE html><div style='width:20px;height:8px;background:#ff0000;filter:grayscale(1)'></div>", diagnostics);
    RoboWeb::LayoutResult grayLayout = RoboWeb::LayoutDocument(gray, 240, 120);
    RoboWeb::Framebuffer grayFrame = RoboWeb::RenderDocument(grayLayout, 0);
    const RoboWeb::ElementBox* grayBox = RoboWeb::FindBox(grayLayout, "div");
    EXPECT(grayBox != nullptr);
    if (grayBox != nullptr) {
        const int grayRed = RoboWeb::PixelRed(grayFrame, grayBox->x + 2, grayBox->y + 2);
        const int grayGreen = RoboWeb::PixelGreen(grayFrame, grayBox->x + 2, grayBox->y + 2);
        EXPECT(grayRed == grayGreen);
        EXPECT(grayRed < 80);
    }
    RoboWeb::Document tall = Parse(
        "<!DOCTYPE html><div style='width:30px;height:40px'><div id='half' style='height:50%'></div></div>", diagnostics);
    RoboWeb::LayoutResult tallLayout = RoboWeb::LayoutDocument(tall, 240, 120);
    int halfHeight = -1;
    for (const RoboWeb::ElementBox& box : tallLayout.boxes) {
        if (box.node != nullptr && RoboWeb::AttributeValue(*box.node, "id") == "half") {
            halfHeight = box.height;
        }
    }
    EXPECT(halfHeight == 20);
    RoboWeb::Document flexColumns = Parse(
        "<!DOCTYPE html><div style='display:grid;width:40px;grid-template-columns:1fr 1fr'><div id='a' style='height:8px'></div><div id='b' style='height:8px'></div></div>",
        diagnostics);
    RoboWeb::LayoutResult flexLayout = RoboWeb::LayoutDocument(flexColumns, 240, 120);
    int flexA = -1;
    int flexB = -1;
    for (const RoboWeb::ElementBox& box : flexLayout.boxes) {
        if (box.node == nullptr) {
            continue;
        }
        if (RoboWeb::AttributeValue(*box.node, "id") == "a") {
            flexA = box.x;
        }
        if (RoboWeb::AttributeValue(*box.node, "id") == "b") {
            flexB = box.x;
        }
    }
    EXPECT(flexB - flexA == 20);
    RoboWeb::Document radial = Parse(
        "<!DOCTYPE html><div style='width:20px;height:16px;background:radial-gradient(#ff0000, #0000ff)'></div>", diagnostics);
    RoboWeb::LayoutResult radialLayout = RoboWeb::LayoutDocument(radial, 240, 120);
    RoboWeb::Framebuffer radialFrame = RoboWeb::RenderDocument(radialLayout, 0);
    const RoboWeb::ElementBox* radialBox = RoboWeb::FindBox(radialLayout, "div");
    EXPECT(radialBox != nullptr);
    if (radialBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(radialFrame, radialBox->x + 10, radialBox->y + 8) == 255);
        EXPECT(RoboWeb::PixelBlue(radialFrame, radialBox->x, radialBox->y) > RoboWeb::PixelRed(radialFrame, radialBox->x, radialBox->y));
    }
    RoboWeb::Document scaledBox = Parse(
        "<!DOCTYPE html><div style='width:10px;height:8px;background:#ff0000;transform:scale(2)'></div>", diagnostics);
    RoboWeb::LayoutResult scaledLayout = RoboWeb::LayoutDocument(scaledBox, 240, 120);
    RoboWeb::Framebuffer scaledFrame = RoboWeb::RenderDocument(scaledLayout, 0);
    const RoboWeb::ElementBox* scaledPaint = RoboWeb::FindBox(scaledLayout, "div");
    EXPECT(scaledPaint != nullptr);
    if (scaledPaint != nullptr) {
        EXPECT(RoboWeb::PixelRed(scaledFrame, scaledPaint->x - 2, scaledPaint->y + 2) == 255);
    }
    RoboWeb::Document shadow = Parse(
        "<!DOCTYPE html><div style='width:10px;height:8px;background:#ff0000;filter:drop-shadow(12px 0px 0px #00ff00)'></div>", diagnostics);
    RoboWeb::LayoutResult shadowLayout = RoboWeb::LayoutDocument(shadow, 240, 120);
    RoboWeb::Framebuffer shadowFrame = RoboWeb::RenderDocument(shadowLayout, 0);
    const RoboWeb::ElementBox* shadowBox = RoboWeb::FindBox(shadowLayout, "div");
    EXPECT(shadowBox != nullptr);
    if (shadowBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(shadowFrame, shadowBox->x + shadowBox->width + 2, shadowBox->y + 2) == 255);
    }
    RoboWeb::Document anchored = Parse(
        "<!DOCTYPE html><div id='mark' style='anchor-name:--a;width:20px;height:10px'></div><div id='pop' style='position:absolute;left:anchor(--a right);width:8px;height:8px'></div>",
        diagnostics);
    RoboWeb::LayoutResult anchoredLayout = RoboWeb::LayoutDocument(anchored, 240, 120);
    int markRight = -1;
    int popX = -1;
    for (const RoboWeb::ElementBox& box : anchoredLayout.boxes) {
        if (box.node == nullptr) {
            continue;
        }
        if (RoboWeb::AttributeValue(*box.node, "id") == "mark") {
            markRight = box.x + box.width;
        }
        if (RoboWeb::AttributeValue(*box.node, "id") == "pop") {
            popX = box.x;
        }
    }
    EXPECT(popX == markRight);
    const char* validValues[] = {
        "display:none", "display:block", "display:inline", "display:inline-block", "display:list-item", "display:grid", "display:inline-grid",
        "position:static", "position:relative", "position:absolute", "position:fixed", "position:sticky",
        "width:10px", "width:50%", "width:1em", "width:1rem", "width:10vw", "width:calc(10px + 5px)", "width:min(1px, 2px)", "width:auto",
        "height:10px", "height:50%", "font-size:16px", "font-style:normal", "font-style:italic", "font-style:oblique",
        "font-weight:normal", "font-weight:bold", "font-weight:400",
        "text-align:left", "text-align:right", "text-align:center", "text-align:start", "text-align:end", "text-align:justify",
        "text-transform:none", "text-transform:uppercase", "text-transform:lowercase", "text-transform:capitalize",
        "white-space:normal", "white-space:nowrap", "white-space:pre", "white-space:pre-wrap", "white-space:pre-line",
        "text-decoration:none", "text-decoration:underline", "text-decoration:line-through", "text-decoration:overline",
        "container-type:normal", "container-type:size", "container-type:inline-size",
        "color:#ff0000", "color:red", "color:transparent", "background:linear-gradient(#ff0000, #0000ff)",
        "background:radial-gradient(#ff0000, #0000ff)", "filter:none", "filter:blur(2px)", "filter:grayscale(1)",
        "filter:drop-shadow(2px 2px 0 #000)", "transform:none", "transform:translate(1px, 2px)", "transform:scale(2)", "transform:rotate(90deg)",
        "grid-template-columns:1fr 1fr", "grid-template-columns:repeat(2, 10px)", "grid-template-columns:minmax(8px, 24px)",
        "grid-template-columns:auto", "grid-template-columns:min-content", "grid-template-columns:max-content", "grid-template-columns:fit-content(20px)",
        "grid-template-rows:1fr", "margin:4px", "margin:4px 8px", "margin:1px 2px 3px 4px", "padding:4px 8px",
        "font-size:1em", "font-weight:bolder", "font-weight:lighter", "line-height:normal", "line-height:1.5", "line-height:20px",
        "letter-spacing:1px", "text-indent:2em", "word-spacing:2px", "font-family:Inter", "border:1px", "border-width:2px",
        "text-underline-offset:2px", "text-decoration-color:#ff0000", "background-color:#00ff00", "background-image:linear-gradient(#ff0000, #0000ff)",
        "transform:translateX(4px)", "transform:translateY(4px)", "transform:translateZ(4px)", "transform:translate3d(1px, 2px, 3px)",
        "transform:scaleX(2)", "transform:scaleY(2)", "transform:scaleZ(2)", "transform:scale3d(2, 2, 1)",
        "transform:rotateX(90deg)", "transform:rotateY(90deg)", "transform:rotateZ(90deg)", "transform:rotate3d(0, 0, 1, 90deg)",
        "transform:skew(10deg)", "transform:skewX(10deg)", "transform:skewY(10deg)", "transform:matrix(1, 0, 0, 1, 0, 0)",
        "transform:matrix3d(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)", "transform:perspective(100px)",
        "filter:brightness(1)", "filter:contrast(1)", "filter:hue-rotate(90deg)", "filter:invert(1)", "filter:opacity(1)", "filter:saturate(1)", "filter:sepia(1)",
        "animation-duration:2s", "animation-duration:500ms", "animation-name:grow", "animation:grow 2s linear", "transition:width 1s",
        "transition-timing-function:ease-in", "top:anchor(--a bottom)", "right:10px", "bottom:10px",
        "fill:#ff0000", "stroke:#000", "stroke-width:2", "stroke-linecap:round", "stroke-linejoin:miter", "stroke-miterlimit:4",
        "fill-opacity:0.5", "stroke-opacity:1", "opacity:1", "stop-color:#fff", "flood-color:#000", "flood-opacity:0.4",
        "clip-path:url(#a)", "mask:url(#a)", "marker:url(#a)", "marker-start:url(#a)", "marker-end:url(#a)", "marker-mid:url(#a)",
        "text-anchor:middle", "dominant-baseline:middle", "--ink:#ff0000",
        "animation-timing-function:linear", "animation-timing-function:ease", "animation-timing-function:ease-in",
        "animation-timing-function:ease-out", "animation-timing-function:ease-in-out", "animation-timing-function:step-start",
        "animation-timing-function:step-end", "animation-timing-function:cubic-bezier(0, 0, 1, 1)", "animation-timing-function:steps(4)",
        "anchor-name:--a", "left:anchor(--a right)", "opacity:0.5", "opacity:50%", "visibility:visible", "visibility:hidden",
        "min-width:10px", "max-width:20px", "min-height:10px", "max-height:20px", "filter:brightness(0.5)", "filter:contrast(2)",
        "filter:invert(1)", "filter:saturate(2)", "filter:sepia(1)", "filter:hue-rotate(90deg)", "filter:opacity(0.5)"};
    for (const char* declaration : validValues) {
        RoboWeb::Diagnostics validDiagnostics;
        RoboWeb::Document valid = Parse(std::string("<!DOCTYPE html><div style='") + declaration + "'></div>", validDiagnostics);
        RoboWeb::ValidateTree(valid, validDiagnostics);
        EXPECT(validDiagnostics.ErrorCount() == 0);
    }
    RoboWeb::Diagnostics badProperty;
    RoboWeb::Document badPropertyDocument = Parse("<!DOCTYPE html><div style='made-up:1'></div>", badProperty);
    RoboWeb::ValidateTree(badPropertyDocument, badProperty);
    EXPECT(CountMessages(badProperty, RoboWeb::Severity::Error, "unknown property 'made-up'") == 1);
    RoboWeb::Diagnostics badValue;
    RoboWeb::Document badValueDocument = Parse("<!DOCTYPE html><div style='display:banana'></div>", badValue);
    RoboWeb::ValidateTree(badValueDocument, badValue);
    EXPECT(CountMessages(badValue, RoboWeb::Severity::Error, "invalid value 'banana' for property 'display'") == 1);
    RoboWeb::Document variableColor = Parse(
        "<!DOCTYPE html><style>:root { --ink: #ff0000 } div { --ink: #00ff00; width: 20px; height: 10px; background: var(--ink) }</style><div></div>",
        diagnostics);
    RoboWeb::LayoutResult variableColorLayout = RoboWeb::LayoutDocument(variableColor, 240, 120);
    RoboWeb::Framebuffer variableColorFrame = RoboWeb::RenderDocument(variableColorLayout, 0);
    const RoboWeb::ElementBox* variableColorBox = RoboWeb::FindBox(variableColorLayout, "div");
    EXPECT(variableColorBox != nullptr);
    if (variableColorBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(variableColorFrame, variableColorBox->x + 2, variableColorBox->y + 2) == 0);
        EXPECT(RoboWeb::PixelGreen(variableColorFrame, variableColorBox->x + 2, variableColorBox->y + 2) == 255);
    }
    const char* invalidValues[] = {
        "display:banana", "position:floating", "width:wide", "height:tall", "font-size:huge", "font-style:slanted",
        "font-weight:heavy", "text-align:middle", "text-transform:small-caps", "white-space:collapse",
        "text-decoration:blink", "container-type:block", "color:not-a-color", "background:sparkle",
        "filter:smear(1)", "transform:spin(1)", "grid-template-columns:sideways", "animation-timing-function:bounce",
        "animation-duration:later", "left:beside", "anchor-name:a", "opacity:clear", "visibility:gone", "min-width:wide"};
    for (const char* declaration : invalidValues) {
        RoboWeb::Diagnostics invalidDiagnostics;
        RoboWeb::Document invalid = Parse(std::string("<!DOCTYPE html><div style='") + declaration + "'></div>", invalidDiagnostics);
        RoboWeb::ValidateTree(invalid, invalidDiagnostics);
        EXPECT(invalidDiagnostics.ErrorCount() == 1);
    }
    RoboWeb::SetSvgAnimationTime(1);
    RoboWeb::Document linearGrow = Parse(
        "<!DOCTYPE html><style>@keyframes grow { from { width: 10px } to { width: 50px } }</style><div style='animation-name:grow;animation-duration:2s;animation-timing-function:linear;height:8px;background:#ff0000'></div>",
        diagnostics);
    const RoboWeb::ElementBox* linearGrowBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(linearGrow, 240, 120), "div");
    EXPECT(linearGrowBox != nullptr);
    if (linearGrowBox != nullptr) {
        EXPECT(linearGrowBox->width == 30);
    }
    RoboWeb::Document easedGrow = Parse(
        "<!DOCTYPE html><style>@keyframes grow { from { width: 10px } to { width: 50px } }</style><div style='animation-name:grow;animation-duration:2s;animation-timing-function:ease-in;height:8px'></div>",
        diagnostics);
    const RoboWeb::ElementBox* easedGrowBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(easedGrow, 240, 120), "div");
    EXPECT(easedGrowBox != nullptr);
    if (easedGrowBox != nullptr) {
        EXPECT(easedGrowBox->width > 10);
        EXPECT(easedGrowBox->width < 30);
    }
    RoboWeb::Document steppedGrow = Parse(
        "<!DOCTYPE html><style>@keyframes grow { from { width: 10px } to { width: 50px } }</style><div style='animation-name:grow;animation-duration:2s;animation-timing-function:step-end;height:8px'></div>",
        diagnostics);
    const RoboWeb::ElementBox* steppedGrowBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(steppedGrow, 240, 120), "div");
    EXPECT(steppedGrowBox != nullptr);
    if (steppedGrowBox != nullptr) {
        EXPECT(steppedGrowBox->width == 10);
    }
    RoboWeb::Document startedGrow = Parse(
        "<!DOCTYPE html><style>@keyframes grow { from { width: 10px } to { width: 50px } }</style><div style='animation-name:grow;animation-duration:2s;animation-timing-function:step-start;height:8px'></div>",
        diagnostics);
    const RoboWeb::ElementBox* startedGrowBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(startedGrow, 240, 120), "div");
    EXPECT(startedGrowBox != nullptr);
    if (startedGrowBox != nullptr) {
        EXPECT(startedGrowBox->width == 50);
    }
    RoboWeb::SetSvgAnimationTime(0);
    RoboWeb::Document shiftedX = Parse(
        "<!DOCTYPE html><div style='width:10px;height:8px'></div><div style='width:10px;height:8px;transform:translateX(30px)'></div>", diagnostics);
    RoboWeb::LayoutResult movedLayout = RoboWeb::LayoutDocument(shiftedX, 240, 120);
    int firstX = -1;
    int secondX = -1;
    for (const RoboWeb::ElementBox& box : movedLayout.boxes) {
        if (box.tag != "div") {
            continue;
        }
        if (firstX < 0) {
            firstX = box.x;
        } else if (secondX < 0) {
            secondX = box.x;
        }
    }
    EXPECT(secondX - firstX == 30);
    RoboWeb::Document skewed = Parse(
        "<!DOCTYPE html><div style='width:10px;height:16px;background:#ff0000;transform:skewX(45deg)'></div>", diagnostics);
    RoboWeb::LayoutResult skewedLayout = RoboWeb::LayoutDocument(skewed, 240, 120);
    RoboWeb::Framebuffer skewedFrame = RoboWeb::RenderDocument(skewedLayout, 0);
    const RoboWeb::ElementBox* skewedBox = RoboWeb::FindBox(skewedLayout, "div");
    EXPECT(skewedBox != nullptr);
    if (skewedBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(skewedFrame, skewedBox->x + skewedBox->width + 2, skewedBox->y + skewedBox->height - 2) == 255);
    }
    RoboWeb::Document turned = Parse(
        "<!DOCTYPE html><div style='width:20px;height:8px;background:#ff0000;transform:rotate(90deg)'></div>", diagnostics);
    RoboWeb::LayoutResult turnedLayout = RoboWeb::LayoutDocument(turned, 240, 120);
    RoboWeb::Framebuffer turnedFrame = RoboWeb::RenderDocument(turnedLayout, 0);
    const RoboWeb::ElementBox* turnedBox = RoboWeb::FindBox(turnedLayout, "div");
    EXPECT(turnedBox != nullptr);
    if (turnedBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(turnedFrame, turnedBox->x + 10, turnedBox->y - 2) == 255);
    }
    struct PaintedDiv {
        RoboWeb::Framebuffer frame;
        int x = 0;
        int y = 0;
        bool ok = false;
    };
    auto paintDiv = [&](const std::string& css) {
        PaintedDiv painted;
        RoboWeb::Document page = Parse("<!DOCTYPE html><div style='width:16px;height:10px;background:#ff0000;" + css + "'></div>", diagnostics);
        RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(page, 240, 120);
        const RoboWeb::ElementBox* box = RoboWeb::FindBox(layout, "div");
        painted.ok = box != nullptr;
        if (box != nullptr) {
            painted.x = box->x;
            painted.y = box->y;
        }
        painted.frame = RoboWeb::RenderDocument(layout, 0);
        return painted;
    };
    PaintedDiv bright = paintDiv("filter:brightness(0)");
    EXPECT(bright.ok);
    if (bright.ok) {
        EXPECT(RoboWeb::PixelRed(bright.frame, bright.x + 2, bright.y + 2) == 0);
    }
    PaintedDiv contrast = paintDiv("filter:contrast(0)");
    EXPECT(contrast.ok);
    if (contrast.ok) {
        const int gray = RoboWeb::PixelRed(contrast.frame, contrast.x + 2, contrast.y + 2);
        EXPECT(gray > 110);
        EXPECT(gray < 150);
        EXPECT(gray == RoboWeb::PixelGreen(contrast.frame, contrast.x + 2, contrast.y + 2));
    }
    PaintedDiv inverted = paintDiv("filter:invert(1)");
    EXPECT(inverted.ok);
    if (inverted.ok) {
        EXPECT(RoboWeb::PixelRed(inverted.frame, inverted.x + 2, inverted.y + 2) == 0);
        EXPECT(RoboWeb::PixelGreen(inverted.frame, inverted.x + 2, inverted.y + 2) == 255);
        EXPECT(RoboWeb::PixelBlue(inverted.frame, inverted.x + 2, inverted.y + 2) == 255);
    }
    PaintedDiv sepia = paintDiv("filter:sepia(1)");
    EXPECT(sepia.ok);
    if (sepia.ok) {
        const int sepiaRed = RoboWeb::PixelRed(sepia.frame, sepia.x + 2, sepia.y + 2);
        const int sepiaGreen = RoboWeb::PixelGreen(sepia.frame, sepia.x + 2, sepia.y + 2);
        const int sepiaBlue = RoboWeb::PixelBlue(sepia.frame, sepia.x + 2, sepia.y + 2);
        EXPECT(sepiaRed > 90);
        EXPECT(sepiaRed < 120);
        EXPECT(sepiaGreen < sepiaRed);
        EXPECT(sepiaBlue < sepiaGreen);
    }
    PaintedDiv hue = paintDiv("filter:hue-rotate(180deg)");
    EXPECT(hue.ok);
    if (hue.ok) {
        EXPECT(RoboWeb::PixelRed(hue.frame, hue.x + 2, hue.y + 2) < 30);
        EXPECT(RoboWeb::PixelGreen(hue.frame, hue.x + 2, hue.y + 2) > 80);
        EXPECT(RoboWeb::PixelBlue(hue.frame, hue.x + 2, hue.y + 2) > 80);
    }
    PaintedDiv flat = paintDiv("filter:saturate(0)");
    EXPECT(flat.ok);
    if (flat.ok) {
        const int flatRed = RoboWeb::PixelRed(flat.frame, flat.x + 2, flat.y + 2);
        EXPECT(flatRed == RoboWeb::PixelGreen(flat.frame, flat.x + 2, flat.y + 2));
        EXPECT(flatRed == RoboWeb::PixelBlue(flat.frame, flat.x + 2, flat.y + 2));
        EXPECT(flatRed < 80);
    }
    PaintedDiv faded = paintDiv("opacity:0.5");
    EXPECT(faded.ok);
    if (faded.ok) {
        EXPECT(RoboWeb::PixelRed(faded.frame, faded.x + 2, faded.y + 2) > 240);
        const int fadedGreen = RoboWeb::PixelGreen(faded.frame, faded.x + 2, faded.y + 2);
        EXPECT(fadedGreen > 100);
        EXPECT(fadedGreen < 160);
    }
    RoboWeb::Document hiddenBox = Parse(
        "<!DOCTYPE html><div style='width:16px;height:10px;background:#ff0000;visibility:hidden'></div>", diagnostics);
    RoboWeb::LayoutResult hiddenLayout = RoboWeb::LayoutDocument(hiddenBox, 240, 120);
    const RoboWeb::ElementBox* hiddenPaint = RoboWeb::FindBox(hiddenLayout, "div");
    EXPECT(hiddenPaint != nullptr);
    if (hiddenPaint != nullptr) {
        EXPECT(hiddenPaint->height == 10);
        RoboWeb::Framebuffer hiddenFrame = RoboWeb::RenderDocument(hiddenLayout, 0);
        EXPECT(RoboWeb::PixelRed(hiddenFrame, hiddenPaint->x + 2, hiddenPaint->y + 2) == 255);
        EXPECT(RoboWeb::PixelGreen(hiddenFrame, hiddenPaint->x + 2, hiddenPaint->y + 2) == 255);
    }
    RoboWeb::Document minimum = Parse(
        "<!DOCTYPE html><div style='width:40px'><div id='grown' style='min-width:80px;height:8px'></div></div>", diagnostics);
    int grownWidth = -1;
    for (const RoboWeb::ElementBox& box : RoboWeb::LayoutDocument(minimum, 240, 120).boxes) {
        if (box.node != nullptr && RoboWeb::AttributeValue(*box.node, "id") == "grown") {
            grownWidth = box.width;
        }
    }
    EXPECT(grownWidth == 80);
    RoboWeb::Document capped = Parse("<!DOCTYPE html><div style='height:40px;max-height:15px'></div>", diagnostics);
    const RoboWeb::ElementBox* cappedBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(capped, 240, 120), "div");
    EXPECT(cappedBox != nullptr);
    if (cappedBox != nullptr) {
        EXPECT(cappedBox->height == 15);
    }
    RoboWeb::Document rowGrid = Parse(
        "<!DOCTYPE html><div style='display:grid;width:20px;grid-template-columns:20px;grid-template-rows:30px 12px'><div id='top' style='height:8px'></div><div id='bottom' style='height:8px'></div></div>",
        diagnostics);
    int topY = -1;
    int bottomY = -1;
    for (const RoboWeb::ElementBox& box : RoboWeb::LayoutDocument(rowGrid, 240, 120).boxes) {
        if (box.node == nullptr) {
            continue;
        }
        if (RoboWeb::AttributeValue(*box.node, "id") == "top") {
            topY = box.y;
        }
        if (RoboWeb::AttributeValue(*box.node, "id") == "bottom") {
            bottomY = box.y;
        }
    }
    EXPECT(bottomY - topY == 30);
    RoboWeb::Document threeStops = Parse(
        "<!DOCTYPE html><div style='width:30px;height:8px;background:linear-gradient(to right, #ff0000, #00ff00, #0000ff)'></div>", diagnostics);
    RoboWeb::LayoutResult threeLayout = RoboWeb::LayoutDocument(threeStops, 240, 120);
    RoboWeb::Framebuffer threeFrame = RoboWeb::RenderDocument(threeLayout, 0);
    const RoboWeb::ElementBox* threeBox = RoboWeb::FindBox(threeLayout, "div");
    EXPECT(threeBox != nullptr);
    if (threeBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(threeFrame, threeBox->x, threeBox->y + 2) == 255);
        EXPECT(RoboWeb::PixelGreen(threeFrame, threeBox->x + 15, threeBox->y + 2) > 200);
        EXPECT(RoboWeb::PixelBlue(threeFrame, threeBox->x + 28, threeBox->y + 2) > 200);
    }
    RoboWeb::SetSvgAnimationTime(1);
    RoboWeb::Document colorGrow = Parse(
        "<!DOCTYPE html><style>@keyframes ink { from { background: #ff0000 } to { background: #0000ff } }</style><div style='animation-name:ink;animation-duration:2s;animation-timing-function:linear;width:12px;height:8px'></div>",
        diagnostics);
    RoboWeb::LayoutResult colorLayout = RoboWeb::LayoutDocument(colorGrow, 240, 120);
    RoboWeb::Framebuffer colorFrame = RoboWeb::RenderDocument(colorLayout, 0);
    const RoboWeb::ElementBox* colorBox = RoboWeb::FindBox(colorLayout, "div");
    EXPECT(colorBox != nullptr);
    if (colorBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(colorFrame, colorBox->x + 2, colorBox->y + 2) > 100);
        EXPECT(RoboWeb::PixelBlue(colorFrame, colorBox->x + 2, colorBox->y + 2) > 100);
        EXPECT(RoboWeb::PixelGreen(colorFrame, colorBox->x + 2, colorBox->y + 2) < 20);
    }
    RoboWeb::SetSvgAnimationTime(0);
}

// Each assertion here states the value a browser computes for the same markup,
// so a regression shows up as a wrong box or a wrong pixel, not a wrong flag.
void TestCssProperties() {
    RoboWeb::Diagnostics diagnostics;
    auto layoutOf = [&](const std::string& html, int width = 240, int height = 120) {
        RoboWeb::Document document = Parse(html, diagnostics);
        return RoboWeb::LayoutDocument(document, width, height);
    };
    auto boxWidth = [](const RoboWeb::LayoutResult& layout, const char* tag) {
        const RoboWeb::ElementBox* box = RoboWeb::FindBox(layout, tag);
        return box == nullptr ? -1 : box->width;
    };
    auto boxHeight = [](const RoboWeb::LayoutResult& layout, const char* tag) {
        const RoboWeb::ElementBox* box = RoboWeb::FindBox(layout, tag);
        return box == nullptr ? -1 : box->height;
    };

    // Every new property must be accepted, so a stylesheet using it stays clean.
    const char* accepted[] = {
        "box-sizing:border-box", "text-shadow:2px 2px #000", "text-shadow:none", "box-shadow:0px 0px 4px #00ff00",
        "box-shadow:1px 1px #000, 2px 2px #000", "overflow:hidden", "overflow:hidden auto", "overflow-x:scroll", "overflow-y:visible",
        "outline:2px solid #ff0000", "outline-width:thin", "outline-style:dashed", "outline-color:#ff0000", "outline-offset:2px",
        "border-radius:4px", "border-radius:4px 2px", "aspect-ratio:16 / 9", "aspect-ratio:1.5", "text-overflow:ellipsis",
        "direction:rtl", "pointer-events:none", "cursor:pointer", "list-style:square inside", "list-style-type:lower-roman",
        "list-style-position:inside", "list-style-image:none", "word-break:break-all", "overflow-wrap:break-word", "tab-size:4",
        "table-layout:fixed", "caption-side:bottom", "empty-cells:hide", "border-collapse:collapse", "border-spacing:2px 4px",
        "float:left", "clear:both", "z-index:3", "z-index:auto",
    };
    for (const char* declaration : accepted) {
        RoboWeb::Diagnostics cssDiagnostics;
        RoboWeb::Document page = Parse("<!DOCTYPE html><div></div>", cssDiagnostics);
        (void)page;
        RoboWeb::ValidateCssText(std::string("div { ") + declaration + " }", "style.css", 1, cssDiagnostics);
        EXPECT(!cssDiagnostics.HasErrors());
    }
    // A malformed value still fails, so the validator is not just a pass-through.
    const char* rejected[] = {
        "box-sizing:margin-box", "overflow:maybe", "aspect-ratio:16 // 9", "list-style-position:middle",
        "pointer-events:sometimes", "cursor:banana", "z-index:1.5", "clear:upward", "text-shadow:2px",
        "outline-style:wiggly", "border-radius:1px 2px 3px 4px 5px",
    };
    for (const char* declaration : rejected) {
        RoboWeb::Diagnostics cssDiagnostics;
        RoboWeb::ValidateCssText(std::string("div { ") + declaration + " }", "style.css", 1, cssDiagnostics);
        EXPECT(cssDiagnostics.HasErrors());
    }

    // box-sizing: content-box grows the border box by the padding, and
    // border-box makes the same width describe the border box instead.
    EXPECT(boxWidth(layoutOf("<!DOCTYPE html><div style='width:50px;height:8px'></div>"), "div") == 50);
    EXPECT(boxWidth(layoutOf("<!DOCTYPE html><div style='width:50px;height:8px;padding:5px'></div>"), "div") == 60);
    EXPECT(boxWidth(layoutOf("<!DOCTYPE html><div style='width:50px;height:8px;padding:5px;box-sizing:border-box'></div>"), "div") == 50);
    EXPECT(boxWidth(layoutOf("<!DOCTYPE html><div style='width:50px;height:8px;border:2px solid;padding:5px'></div>"), "div") == 64);
    EXPECT(boxWidth(layoutOf("<!DOCTYPE html><div style='width:50px;height:8px;border:2px solid;padding:5px;box-sizing:border-box'></div>"), "div") == 50);

    // text-shadow repeats the glyph run at the offset, so the shadow lands
    // outside the glyphs where the shifted run covers white background.
    {
        RoboWeb::Document page = Parse("<!DOCTYPE html><div style='font-size:20px;text-shadow:14px 0px #00ff00'>X</div>", diagnostics);
        RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(page, 240, 120);
        int textOps = 0;
        bool shadowFound = false;
        for (const RoboWeb::DrawOp& op : layout.ops) {
            if (op.kind != RoboWeb::DrawOp::Kind::Text) {
                continue;
            }
            ++textOps;
            if (op.color.green == 255 && op.color.red == 0 && op.color.blue == 0) {
                shadowFound = true;
            }
        }
        // One record for the glyphs and one for the shadow, offset to the right.
        EXPECT(textOps == 2);
        EXPECT(shadowFound);
    }
    // A shadow with no offset paints nothing extra, which keeps the record count
    // at one and matches the browser, where the shadow hides under the text.
    {
        RoboWeb::Document page = Parse("<!DOCTYPE html><div style='font-size:20px;text-shadow:0px 0px #00ff00'>X</div>", diagnostics);
        RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(page, 240, 120);
        int textOps = 0;
        for (const RoboWeb::DrawOp& op : layout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text) {
                ++textOps;
            }
        }
        EXPECT(textOps == 1);
    }

    // overflow: hidden clips the descendant record to the element's own box, so
    // the wide child no longer paints outside the 20px parent.
    {
        RoboWeb::Document page = Parse(
            "<!DOCTYPE html><div style='width:20px;height:10px;overflow:hidden'>"
            "<div style='width:200px;height:10px;background:#ff0000'></div></div>",
            diagnostics);
        RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(page, 240, 120);
        bool clipped = false;
        for (const RoboWeb::DrawOp& op : layout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Fill && op.color.red == 255 && op.clipped) {
                clipped = true;
                EXPECT(op.clipWidth <= 20);
            }
        }
        EXPECT(clipped);
    }
    // overflow: visible, the default, leaves the same child unclipped.
    {
        RoboWeb::Document page = Parse(
            "<!DOCTYPE html><div style='width:20px;height:10px'>"
            "<div style='width:200px;height:10px;background:#ff0000'></div></div>",
            diagnostics);
        RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(page, 240, 120);
        bool unclipped = false;
        for (const RoboWeb::DrawOp& op : layout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Fill && op.color.red == 255) {
                EXPECT(!op.clipped);
                unclipped = true;
            }
        }
        EXPECT(unclipped);
    }

    // An outline paints outside the border box and never changes layout, so the
    // box keeps its width while the frame gains red pixels around it.
    {
        RoboWeb::Document plain = Parse("<!DOCTYPE html><div style='width:40px;height:20px;background:#ffffff'></div>", diagnostics);
        RoboWeb::LayoutResult plainLayout = RoboWeb::LayoutDocument(plain, 240, 120);
        RoboWeb::Document outlined = Parse(
            "<!DOCTYPE html><div style='width:40px;height:20px;background:#ffffff;outline:2px solid #ff0000'></div>", diagnostics);
        RoboWeb::LayoutResult outlinedLayout = RoboWeb::LayoutDocument(outlined, 240, 120);
        EXPECT(boxWidth(plainLayout, "div") == boxWidth(outlinedLayout, "div"));
        EXPECT(boxHeight(plainLayout, "div") == boxHeight(outlinedLayout, "div"));
        bool outlineStroke = false;
        for (const RoboWeb::DrawOp& op : outlinedLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Stroke && op.color.red == 255 && op.color.green == 0) {
                outlineStroke = true;
            }
        }
        EXPECT(outlineStroke);
    }
    // outline: none draws nothing.
    {
        RoboWeb::Document page = Parse("<!DOCTYPE html><div style='width:40px;height:20px;outline:none'></div>", diagnostics);
        RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(page, 240, 120);
        for (const RoboWeb::DrawOp& op : layout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Stroke) {
                EXPECT(!(op.color.red == 255 && op.color.green == 0));
            }
        }
    }

    // list-style: none removes the marker, and list-style-type picks its glyph.
    {
        RoboWeb::Document plain = Parse("<!DOCTYPE html><ul><li>Item</li></ul>", diagnostics);
        RoboWeb::LayoutResult plainLayout = RoboWeb::LayoutDocument(plain, 240, 120);
        int plainMarkers = 0;
        for (const RoboWeb::DrawOp& op : plainLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text != "Item" && !op.text.empty()) {
                ++plainMarkers;
            }
        }
        EXPECT(plainMarkers == 1);
        RoboWeb::Document nonePage = Parse("<!DOCTYPE html><ul><li style='list-style:none'>Item</li></ul>", diagnostics);
        RoboWeb::LayoutResult noneLayout = RoboWeb::LayoutDocument(nonePage, 240, 120);
        int noneMarkers = 0;
        for (const RoboWeb::DrawOp& op : noneLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text != "Item" && !op.text.empty()) {
                ++noneMarkers;
            }
        }
        EXPECT(noneMarkers == 0);
    }

    // pointer-events: none takes the element out of hit testing, so a click
    // lands on the control underneath it rather than on the overlay. Both
    // boxes are absolutely positioned over the same pixels, so the control is
    // only reachable when the overlay is skipped.
    {
        const std::string pointerMarkup =
            "<!DOCTYPE html><div id='over' style='position:absolute;left:0px;top:0px;width:80px;height:40px;pointer-events:none'></div>"
            "<input id='under' style='position:absolute;left:0px;top:0px;width:80px;height:40px'>";
        RoboWeb::Document page = Parse(pointerMarkup, diagnostics);
        RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(page, 240, 120);
        const RoboWeb::ElementBox* overlay = nullptr;
        const RoboWeb::ElementBox* control = nullptr;
        for (const RoboWeb::ElementBox& box : layout.boxes) {
            if (box.node == nullptr) {
                continue;
            }
            const std::string id = RoboWeb::AttributeValue(*box.node, "id");
            if (id == "over") {
                overlay = &box;
            } else if (id == "under") {
                control = &box;
            }
        }
        EXPECT(overlay != nullptr);
        EXPECT(control != nullptr);
        if (overlay != nullptr && control != nullptr) {
            // The two boxes really do overlap, or the assertions below prove nothing.
            EXPECT(control->x < overlay->x + overlay->width && control->x + control->width > overlay->x);
            EXPECT(control->y < overlay->y + overlay->height && control->y + control->height > overlay->y);
            const int clickX = overlay->x + 4;
            const int clickY = overlay->y + 4;
            RoboWeb::DispatchClick(page, layout, clickX, clickY);
            EXPECT(page.focus != nullptr);
            EXPECT(page.focus != nullptr && RoboWeb::AttributeValue(*page.focus, "id") == "under");
            // A cursor shape over the same pixels with the overlay still skipping:
            // with the overlay skipping, plus the control's own hand cursor wins.
            EXPECT(RoboWeb::PointerAt(layout, clickX, clickY, 0) == RoboWeb::PointerShape::Text);
            // The same markup without pointer-events: none still hits the overlay,
            // so the test above is really measuring the skip.
            std::string coveredMarkup = pointerMarkup;
            const std::size_t skip = coveredMarkup.find("pointer-events:none");
            EXPECT(skip != std::string::npos);
            if (skip != std::string::npos) {
                coveredMarkup.replace(skip, std::string("pointer-events:none").size(), "pointer-events:auto");
                RoboWeb::Document covered = Parse(coveredMarkup, diagnostics);
                RoboWeb::LayoutResult coveredLayout = RoboWeb::LayoutDocument(covered, 240, 120);
                const RoboWeb::ElementBox* coveredControl = nullptr;
                for (const RoboWeb::ElementBox& box : coveredLayout.boxes) {
                    if (box.node != nullptr && RoboWeb::AttributeValue(*box.node, "id") == "under") {
                        coveredControl = &box;
                    }
                }
                EXPECT(coveredControl != nullptr);
                if (coveredControl != nullptr) {
                    RoboWeb::DispatchClick(covered, coveredLayout, coveredControl->x + 4, coveredControl->y + 4);
                    EXPECT(covered.focus == nullptr || RoboWeb::AttributeValue(*covered.focus, "id") != "under");
                    // The overlay keeps the default cursor, so the input's text
                    // cursor is really hidden by the covering box.
                    EXPECT(RoboWeb::PointerAt(coveredLayout, coveredControl->x + 4, coveredControl->y + 4, 0) == RoboWeb::PointerShape::Arrow);
                }
            }
        }
    }
    // cursor: pointer reports the hand shape over the element's own box.
    {
        RoboWeb::Document page = Parse("<!DOCTYPE html><div style='width:40px;height:20px;cursor:pointer'></div>", diagnostics);
        RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(page, 240, 120);
        const RoboWeb::ElementBox* box = RoboWeb::FindBox(layout, "div");
        EXPECT(box != nullptr);
        if (box != nullptr) {
            EXPECT(RoboWeb::PointerAt(layout, box->x + 2, box->y + 2, 0) == RoboWeb::PointerShape::Hand);
        }
    }
}

void TestScriptAndPresentation() {
    RoboWeb::Diagnostics diagnostics;
    RoboWeb::Document document = Parse(
        "<!DOCTYPE html><p id='out'>Old</p><script>document.getElementById('out').textContent = 'New';</script>"
        "<p style='display:none'>Hidden</p><div style='width:80px;height:30px;background:#00ff00'>Box</div>",
        diagnostics);
    EXPECT(!diagnostics.HasErrors());
    RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(document, 200, 160);
    EXPECT(RoboWeb::GatherText(*document.root).find("New") != std::string::npos);
    EXPECT(RoboWeb::GatherText(*document.root).find("Old") == std::string::npos);
    const RoboWeb::ElementBox* box = RoboWeb::FindBox(layout, "div");
    EXPECT(box != nullptr);
    if (box != nullptr) {
        EXPECT(box->width == 80);
        EXPECT(box->height >= 30);
    }
}

void TestHtmlBehavior() {
    RoboWeb::Diagnostics diagnostics;
    RoboWeb::Document form = Parse(
        "<!DOCTYPE html><form><input name='email' type='email' required><button>Send</button></form>", diagnostics);
    RoboWeb::LayoutResult formLayout = RoboWeb::LayoutDocument(form, 240, 120);
    const RoboWeb::ElementBox* button = RoboWeb::FindBox(formLayout, "button");
    EXPECT(button != nullptr);
    if (button != nullptr) {
        RoboWeb::DispatchClick(form, formLayout, button->x + 4, button->y + 4);
        EXPECT(form.status.find("invalid required") != std::string::npos);
    }
    RoboWeb::Document page = Parse("<!DOCTYPE html><body vlink='red'><a href='page.html'>Go</a></body>", diagnostics);
    RoboWeb::LayoutResult layout = RoboWeb::LayoutDocument(page, 200, 80);
    const RoboWeb::ElementBox* link = RoboWeb::FindBox(layout, "a");
    EXPECT(link != nullptr);
    if (link != nullptr) {
        RoboWeb::DispatchClick(page, layout, link->x + 2, link->y + 2);
        EXPECT(link->node->visited);
        EXPECT(page.status.find("open page.html") != std::string::npos);
    }
    RoboWeb::Document path = Parse(
        "<!DOCTYPE html><svg width='30' height='30'><path d='M2 2 H20 V20 H2 Z' fill='red'/></svg>", diagnostics);
    RoboWeb::LayoutResult pathLayout = RoboWeb::LayoutDocument(path, 120, 80);
    RoboWeb::Framebuffer pathFrame = RoboWeb::RenderDocument(pathLayout, 0);
    const RoboWeb::ElementBox* pathBox = RoboWeb::FindBox(pathLayout, "svg");
    EXPECT(pathBox != nullptr);
    if (pathBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(pathFrame, pathBox->x + 8, pathBox->y + 8) == 255);
    }
    RoboWeb::Document curvePath = Parse(
        "<!DOCTYPE html><svg width='40' height='40'><path d='M2 30 Q20 2 38 30 Z' fill='#ff0000'/></svg>", diagnostics);
    RoboWeb::LayoutResult curveLayout = RoboWeb::LayoutDocument(curvePath, 120, 80);
    RoboWeb::Framebuffer curveFrame = RoboWeb::RenderDocument(curveLayout, 0);
    const RoboWeb::ElementBox* curveBox = RoboWeb::FindBox(curveLayout, "svg");
    EXPECT(curveBox != nullptr);
    if (curveBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(curveFrame, curveBox->x + 20, curveBox->y + 22) == 0);
        EXPECT(RoboWeb::PixelGreen(curveFrame, curveBox->x + 20, curveBox->y + 8) == 255);
    }
    RoboWeb::Document arcPath = Parse(
        "<!DOCTYPE html><svg width='40' height='40'><path d='M8 20 A12 12 0 1 1 32 20 A12 12 0 1 1 8 20 Z' fill='#ff0000'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult arcLayout = RoboWeb::LayoutDocument(arcPath, 120, 80);
    RoboWeb::Framebuffer arcFrame = RoboWeb::RenderDocument(arcLayout, 0);
    const RoboWeb::ElementBox* arcBox = RoboWeb::FindBox(arcLayout, "svg");
    EXPECT(arcBox != nullptr);
    if (arcBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(arcFrame, arcBox->x + 20, arcBox->y + 20) == 0);
        EXPECT(RoboWeb::PixelGreen(arcFrame, arcBox->x + 2, arcBox->y + 2) == 255);
    }
    RoboWeb::Document shadowShape = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><defs><filter id='s'><feDropShadow dx='14' dy='0' stdDeviation='0' flood-color='#00ff00' flood-opacity='1'/></filter></defs><rect width='8' height='8' fill='#ff0000' filter='url(#s)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult shadowLayout = RoboWeb::LayoutDocument(shadowShape, 120, 80);
    RoboWeb::Framebuffer shadowFrame = RoboWeb::RenderDocument(shadowLayout, 0);
    const RoboWeb::ElementBox* shadowBox = RoboWeb::FindBox(shadowLayout, "svg");
    EXPECT(shadowBox != nullptr);
    if (shadowBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(shadowFrame, shadowBox->x + 2, shadowBox->y + 2) == 0);
        EXPECT(RoboWeb::PixelRed(shadowFrame, shadowBox->x + 16, shadowBox->y + 2) == 0);
        EXPECT(RoboWeb::PixelGreen(shadowFrame, shadowBox->x + 16, shadowBox->y + 2) == 255);
    }
    RoboWeb::Document roundShadow = Parse(
        "<!DOCTYPE html><svg width='48' height='24'><defs><filter id='s'><feDropShadow dx='20' dy='0' stdDeviation='0' flood-color='#00ff00' flood-opacity='1'/></filter></defs><circle cx='8' cy='8' r='6' fill='#ff0000' filter='url(#s)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult roundShadowLayout = RoboWeb::LayoutDocument(roundShadow, 140, 80);
    RoboWeb::Framebuffer roundShadowFrame = RoboWeb::RenderDocument(roundShadowLayout, 0);
    const RoboWeb::ElementBox* roundShadowBox = RoboWeb::FindBox(roundShadowLayout, "svg");
    EXPECT(roundShadowBox != nullptr);
    if (roundShadowBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(roundShadowFrame, roundShadowBox->x + 8, roundShadowBox->y + 8) == 0);
        EXPECT(RoboWeb::PixelRed(roundShadowFrame, roundShadowBox->x + 28, roundShadowBox->y + 8) == 0);
        EXPECT(RoboWeb::PixelGreen(roundShadowFrame, roundShadowBox->x + 28, roundShadowBox->y + 8) == 255);
    }
    RoboWeb::Document pathShadow = Parse(
        "<!DOCTYPE html><svg width='48' height='24'><defs><filter id='s'><feDropShadow dx='20' dy='0' stdDeviation='0' flood-color='#00ff00' flood-opacity='1'/></filter></defs><path d='M2 2 H12 V14 H2 Z' fill='#ff0000' filter='url(#s)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult pathShadowLayout = RoboWeb::LayoutDocument(pathShadow, 140, 80);
    RoboWeb::Framebuffer pathShadowFrame = RoboWeb::RenderDocument(pathShadowLayout, 0);
    const RoboWeb::ElementBox* pathShadowBox = RoboWeb::FindBox(pathShadowLayout, "svg");
    EXPECT(pathShadowBox != nullptr);
    if (pathShadowBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(pathShadowFrame, pathShadowBox->x + 6, pathShadowBox->y + 6) == 0);
        EXPECT(RoboWeb::PixelRed(pathShadowFrame, pathShadowBox->x + 26, pathShadowBox->y + 6) == 0);
        EXPECT(RoboWeb::PixelGreen(pathShadowFrame, pathShadowBox->x + 26, pathShadowBox->y + 6) == 255);
    }
    RoboWeb::Document polyShadow = Parse(
        "<!DOCTYPE html><svg width='48' height='24'><defs><filter id='s'><feDropShadow dx='20' dy='0' stdDeviation='0' flood-color='#00ff00' flood-opacity='1'/></filter></defs><polygon points='2,2 14,2 14,14 2,14' fill='#ff0000' filter='url(#s)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult polyShadowLayout = RoboWeb::LayoutDocument(polyShadow, 140, 80);
    RoboWeb::Framebuffer polyShadowFrame = RoboWeb::RenderDocument(polyShadowLayout, 0);
    const RoboWeb::ElementBox* polyShadowBox = RoboWeb::FindBox(polyShadowLayout, "svg");
    EXPECT(polyShadowBox != nullptr);
    if (polyShadowBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(polyShadowFrame, polyShadowBox->x + 6, polyShadowBox->y + 6) == 0);
        EXPECT(RoboWeb::PixelRed(polyShadowFrame, polyShadowBox->x + 26, polyShadowBox->y + 6) == 0);
        EXPECT(RoboWeb::PixelGreen(polyShadowFrame, polyShadowBox->x + 26, polyShadowBox->y + 6) == 255);
    }
    RoboWeb::Document textShadow = Parse(
        "<!DOCTYPE html><svg width='80' height='24'><defs><filter id='s'><feDropShadow dx='28' dy='0' stdDeviation='0' flood-color='#00ff00' flood-opacity='1'/></filter></defs><text x='2' y='2' dominant-baseline='hanging' fill='#ff0000' filter='url(#s)'>M</text></svg>",
        diagnostics);
    RoboWeb::LayoutResult textShadowLayout = RoboWeb::LayoutDocument(textShadow, 160, 80);
    const RoboWeb::ElementBox* textShadowBox = RoboWeb::FindBox(textShadowLayout, "svg");
    EXPECT(textShadowBox != nullptr);
    if (textShadowBox != nullptr) {
        bool sawInk = false;
        bool sawOffset = false;
        for (const RoboWeb::DrawOp& op : textShadowLayout.ops) {
            if (op.kind != RoboWeb::DrawOp::Kind::Text || op.text != "M") {
                continue;
            }
            if (op.color.red == 255 && op.color.green == 0 && op.x < textShadowBox->x + 20) {
                sawInk = true;
            }
            if (op.color.red == 0 && op.color.green == 255 && op.x >= textShadowBox->x + 20) {
                sawOffset = true;
            }
        }
        EXPECT(sawInk);
        EXPECT(sawOffset);
    }
    RoboWeb::Document imageShadow = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><defs><filter id='s'><feDropShadow dx='16' dy='0' stdDeviation='0' flood-color='#00ff00' flood-opacity='1'/></filter></defs><image href='mark.png' width='8' height='8' filter='url(#s)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult imageShadowLayout = RoboWeb::LayoutDocument(imageShadow, 140, 80);
    RoboWeb::Framebuffer imageShadowFrame = RoboWeb::RenderDocument(imageShadowLayout, 0);
    const RoboWeb::ElementBox* imageShadowBox = RoboWeb::FindBox(imageShadowLayout, "svg");
    EXPECT(imageShadowBox != nullptr);
    if (imageShadowBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(imageShadowFrame, imageShadowBox->x + 18, imageShadowBox->y + 2) == 0);
        EXPECT(RoboWeb::PixelGreen(imageShadowFrame, imageShadowBox->x + 18, imageShadowBox->y + 2) == 255);
        bool sawPicture = false;
        for (const RoboWeb::DrawOp& op : imageShadowLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Image && op.x < imageShadowBox->x + 12) {
                sawPicture = true;
            }
        }
        EXPECT(sawPicture);
    }
    RoboWeb::Document groupShadow = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><defs><filter id='s'><feDropShadow dx='16' dy='0' stdDeviation='0' flood-color='#00ff00' flood-opacity='1'/></filter></defs><g filter='url(#s)'><rect width='8' height='8' fill='#ff0000'/></g></svg>",
        diagnostics);
    RoboWeb::LayoutResult groupShadowLayout = RoboWeb::LayoutDocument(groupShadow, 140, 80);
    RoboWeb::Framebuffer groupShadowFrame = RoboWeb::RenderDocument(groupShadowLayout, 0);
    const RoboWeb::ElementBox* groupShadowBox = RoboWeb::FindBox(groupShadowLayout, "svg");
    EXPECT(groupShadowBox != nullptr);
    if (groupShadowBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(groupShadowFrame, groupShadowBox->x + 2, groupShadowBox->y + 2) == 0);
        EXPECT(RoboWeb::PixelRed(groupShadowFrame, groupShadowBox->x + 18, groupShadowBox->y + 2) == 0);
        EXPECT(RoboWeb::PixelGreen(groupShadowFrame, groupShadowBox->x + 18, groupShadowBox->y + 2) == 255);
    }
    RoboWeb::Document joinPath = Parse(
        "<!DOCTYPE html><svg width='48' height='40'><path d='M10 10 H30 V30' fill='none' stroke='#ff0000' stroke-width='8' stroke-linejoin='miter'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult joinPathLayout = RoboWeb::LayoutDocument(joinPath, 160, 80);
    RoboWeb::Framebuffer joinPathFrame = RoboWeb::RenderDocument(joinPathLayout, 0);
    const RoboWeb::ElementBox* joinPathBox = RoboWeb::FindBox(joinPathLayout, "svg");
    EXPECT(joinPathBox != nullptr);
    if (joinPathBox != nullptr) {
        // The miter tip is the outer corner of the offset rectangle: the 90°
        // corner of an 8px stroke reaches four pixels past the vertex.
        EXPECT(RoboWeb::PixelGreen(joinPathFrame, joinPathBox->x + 32, joinPathBox->y + 6) == 0);
        EXPECT(RoboWeb::PixelRed(joinPathFrame, joinPathBox->x + 32, joinPathBox->y + 6) == 255);
        EXPECT(RoboWeb::PixelGreen(joinPathFrame, joinPathBox->x + 31, joinPathBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(joinPathFrame, joinPathBox->x + 2, joinPathBox->y + 2) == 255);
    }
    RoboWeb::Document capPath = Parse(
        "<!DOCTYPE html><svg width='40' height='24'><path d='M8 12 H24' fill='none' stroke='#ff0000' stroke-width='8' stroke-linecap='square'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult capPathLayout = RoboWeb::LayoutDocument(capPath, 160, 80);
    RoboWeb::Framebuffer capPathFrame = RoboWeb::RenderDocument(capPathLayout, 0);
    const RoboWeb::ElementBox* capPathBox = RoboWeb::FindBox(capPathLayout, "svg");
    EXPECT(capPathBox != nullptr);
    if (capPathBox != nullptr) {
        // A square cap extends the stroke by half its width: 8..24 becomes
        // 4..28, so column 27 is the last painted one.
        EXPECT(RoboWeb::PixelGreen(capPathFrame, capPathBox->x + 27, capPathBox->y + 12) == 0);
        EXPECT(RoboWeb::PixelRed(capPathFrame, capPathBox->x + 27, capPathBox->y + 12) == 255);
        EXPECT(RoboWeb::PixelGreen(capPathFrame, capPathBox->x + 29, capPathBox->y + 12) == 255);
        EXPECT(RoboWeb::PixelGreen(capPathFrame, capPathBox->x + 2, capPathBox->y + 2) == 255);
    }
    RoboWeb::Document scaled = Parse(
        "<!DOCTYPE html><svg width='40' height='20' viewBox='0 0 100 100'><rect x='0' y='0' width='50' height='100' fill='red'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult scaledLayout = RoboWeb::LayoutDocument(scaled, 160, 80);
    RoboWeb::Framebuffer scaledFrame = RoboWeb::RenderDocument(scaledLayout, 0);
    const RoboWeb::ElementBox* scaledBox = RoboWeb::FindBox(scaledLayout, "svg");
    EXPECT(scaledBox != nullptr);
    if (scaledBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(scaledFrame, scaledBox->x + 14, scaledBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(scaledFrame, scaledBox->x + 4, scaledBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(scaledFrame, scaledBox->x + 30, scaledBox->y + 4) == 255);
    }
    RoboWeb::Document reused = Parse(
        "<!DOCTYPE html><svg width='30' height='30'><defs><rect id='box' x='2' y='2' width='20' height='20' fill='red'/></defs><use href='#box'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult reusedLayout = RoboWeb::LayoutDocument(reused, 120, 80);
    RoboWeb::Framebuffer reusedFrame = RoboWeb::RenderDocument(reusedLayout, 0);
    const RoboWeb::ElementBox* reusedBox = RoboWeb::FindBox(reusedLayout, "svg");
    EXPECT(reusedBox != nullptr);
    if (reusedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(reusedFrame, reusedBox->x + 8, reusedBox->y + 8) == 0);
    }
    RoboWeb::Document shifted = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><rect width='10' height='20' fill='red' transform='translate(20,0)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult shiftedLayout = RoboWeb::LayoutDocument(shifted, 160, 80);
    RoboWeb::Framebuffer shiftedFrame = RoboWeb::RenderDocument(shiftedLayout, 0);
    const RoboWeb::ElementBox* shiftedBox = RoboWeb::FindBox(shiftedLayout, "svg");
    EXPECT(shiftedBox != nullptr);
    if (shiftedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(shiftedFrame, shiftedBox->x + 4, shiftedBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(shiftedFrame, shiftedBox->x + 24, shiftedBox->y + 4) == 0);
    }
    RoboWeb::Document hidden = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><rect width='20' height='20' fill='red' display='none'/></svg>", diagnostics);
    RoboWeb::LayoutResult hiddenLayout = RoboWeb::LayoutDocument(hidden, 120, 80);
    RoboWeb::Framebuffer hiddenFrame = RoboWeb::RenderDocument(hiddenLayout, 0);
    const RoboWeb::ElementBox* hiddenBox = RoboWeb::FindBox(hiddenLayout, "svg");
    EXPECT(hiddenBox != nullptr);
    if (hiddenBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(hiddenFrame, hiddenBox->x + 4, hiddenBox->y + 4) == 255);
    }
    RoboWeb::Document linked = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><a href='page.html'><rect width='20' height='20' fill='red'/></a></svg>", diagnostics);
    RoboWeb::LayoutResult linkedLayout = RoboWeb::LayoutDocument(linked, 160, 80);
    const RoboWeb::ElementBox* linkedBox = RoboWeb::FindBox(linkedLayout, "svg");
    EXPECT(linkedBox != nullptr);
    if (linkedBox != nullptr) {
        EXPECT(RoboWeb::DispatchClick(linked, linkedLayout, linkedBox->x + 4, linkedBox->y + 4));
        EXPECT(linked.status.find("open page.html") != std::string::npos);
    }
    RoboWeb::Document foreign = Parse(
        "<!DOCTYPE html><svg width='60' height='30'><foreignObject x='0' y='0' width='60' height='30'><div style='background:red'>Hi</div></foreignObject></svg>",
        diagnostics);
    RoboWeb::LayoutResult foreignLayout = RoboWeb::LayoutDocument(foreign, 160, 80);
    RoboWeb::Framebuffer foreignFrame = RoboWeb::RenderDocument(foreignLayout, 0);
    const RoboWeb::ElementBox* foreignBox = RoboWeb::FindBox(foreignLayout, "svg");
    EXPECT(foreignBox != nullptr);
    if (foreignBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(foreignFrame, foreignBox->x + 4, foreignBox->y + 4) == 0);
    }
    RoboWeb::Document chosen = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><switch><rect width='20' height='20' fill='blue' systemLanguage='de'/><rect width='20' height='20' fill='red'/></switch></svg>",
        diagnostics);
    RoboWeb::LayoutResult chosenLayout = RoboWeb::LayoutDocument(chosen, 120, 80);
    RoboWeb::Framebuffer chosenFrame = RoboWeb::RenderDocument(chosenLayout, 0);
    const RoboWeb::ElementBox* chosenBox = RoboWeb::FindBox(chosenLayout, "svg");
    EXPECT(chosenBox != nullptr);
    if (chosenBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(chosenFrame, chosenBox->x + 4, chosenBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelBlue(chosenFrame, chosenBox->x + 4, chosenBox->y + 4) == 0);
    }
    RoboWeb::Document faded = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><rect width='20' height='20' fill='red' opacity='0.5'/></svg>", diagnostics);
    RoboWeb::LayoutResult fadedLayout = RoboWeb::LayoutDocument(faded, 120, 80);
    RoboWeb::Framebuffer fadedFrame = RoboWeb::RenderDocument(fadedLayout, 0);
    const RoboWeb::ElementBox* fadedBox = RoboWeb::FindBox(fadedLayout, "svg");
    EXPECT(fadedBox != nullptr);
    if (fadedBox != nullptr) {
        const int fadedGreen = RoboWeb::PixelGreen(fadedFrame, fadedBox->x + 4, fadedBox->y + 4);
        EXPECT(fadedGreen > 80 && fadedGreen < 200);
    }
    RoboWeb::Document invisible = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><rect width='20' height='20' fill='red' visibility='hidden'/></svg>", diagnostics);
    RoboWeb::LayoutResult invisibleLayout = RoboWeb::LayoutDocument(invisible, 120, 80);
    RoboWeb::Framebuffer invisibleFrame = RoboWeb::RenderDocument(invisibleLayout, 0);
    const RoboWeb::ElementBox* invisibleBox = RoboWeb::FindBox(invisibleLayout, "svg");
    EXPECT(invisibleBox != nullptr);
    if (invisibleBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(invisibleFrame, invisibleBox->x + 4, invisibleBox->y + 4) == 255);
    }
    RoboWeb::Document stroked = Parse(
        "<!DOCTYPE html><svg width='30' height='20'><g stroke='red' fill='none'><line x1='0' y1='8' x2='30' y2='8' stroke-width='4'/></g></svg>",
        diagnostics);
    RoboWeb::LayoutResult strokedLayout = RoboWeb::LayoutDocument(stroked, 120, 80);
    RoboWeb::Framebuffer strokedFrame = RoboWeb::RenderDocument(strokedLayout, 0);
    const RoboWeb::ElementBox* strokedBox = RoboWeb::FindBox(strokedLayout, "svg");
    EXPECT(strokedBox != nullptr);
    if (strokedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(strokedFrame, strokedBox->x + 8, strokedBox->y + 8) == 0);
    }
    RoboWeb::Document anchored = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><text x='40' y='20' text-anchor='middle' fill='red'>MMMM</text></svg>", diagnostics);
    RoboWeb::LayoutResult anchoredLayout = RoboWeb::LayoutDocument(anchored, 160, 80);
    const RoboWeb::ElementBox* anchoredBox = RoboWeb::FindBox(anchoredLayout, "svg");
    EXPECT(anchoredBox != nullptr);
    if (anchoredBox != nullptr) {
        bool shifted = false;
        for (const RoboWeb::DrawOp& op : anchoredLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("MMMM") != std::string::npos && op.x < anchoredBox->x + 40) {
                shifted = true;
            }
        }
        EXPECT(shifted);
    }
    const char* star = "50,0 21,90 98,35 2,35 79,90";
    RoboWeb::Document holed = Parse(
        std::string("<!DOCTYPE html><svg width='100' height='100'><polygon fill='red' fill-rule='evenodd' points='") + star +
            "'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult holedLayout = RoboWeb::LayoutDocument(holed, 140, 140);
    RoboWeb::Framebuffer holedFrame = RoboWeb::RenderDocument(holedLayout, 0);
    const RoboWeb::ElementBox* holedBox = RoboWeb::FindBox(holedLayout, "svg");
    EXPECT(holedBox != nullptr);
    if (holedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(holedFrame, holedBox->x + 50, holedBox->y + 45) == 255);
    }
    RoboWeb::Document solid = Parse(
        std::string("<!DOCTYPE html><svg width='100' height='100'><polygon fill='red' fill-rule='nonzero' points='") + star +
            "'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult solidLayout = RoboWeb::LayoutDocument(solid, 140, 140);
    RoboWeb::Framebuffer solidFrame = RoboWeb::RenderDocument(solidLayout, 0);
    const RoboWeb::ElementBox* solidBox = RoboWeb::FindBox(solidLayout, "svg");
    EXPECT(solidBox != nullptr);
    if (solidBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(solidFrame, solidBox->x + 50, solidBox->y + 45) == 0);
    }
    RoboWeb::Document gradient = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><linearGradient id='g'><stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient></defs><rect width='40' height='16' fill='url(#g)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult gradientLayout = RoboWeb::LayoutDocument(gradient, 160, 80);
    RoboWeb::Framebuffer gradientFrame = RoboWeb::RenderDocument(gradientLayout, 0);
    const RoboWeb::ElementBox* gradientBox = RoboWeb::FindBox(gradientLayout, "svg");
    EXPECT(gradientBox != nullptr);
    if (gradientBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(gradientFrame, gradientBox->x, gradientBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelBlue(gradientFrame, gradientBox->x, gradientBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelBlue(gradientFrame, gradientBox->x + 39, gradientBox->y + 4) > 240);
        EXPECT(RoboWeb::PixelRed(gradientFrame, gradientBox->x + 39, gradientBox->y + 4) < 20);
    }
    RoboWeb::Document marked = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><defs><marker id='m'><rect width='8' height='8' fill='red'/></marker></defs><line x1='4' y1='4' x2='30' y2='4' stroke='black' marker-end='url(#m)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult markedLayout = RoboWeb::LayoutDocument(marked, 160, 80);
    RoboWeb::Framebuffer markedFrame = RoboWeb::RenderDocument(markedLayout, 0);
    const RoboWeb::ElementBox* markedBox = RoboWeb::FindBox(markedLayout, "svg");
    EXPECT(markedBox != nullptr);
    if (markedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(markedFrame, markedBox->x + 32, markedBox->y + 6) == 0);
        EXPECT(RoboWeb::PixelRed(markedFrame, markedBox->x + 32, markedBox->y + 6) == 255);
    }
    RoboWeb::Document along = Parse(
        "<!DOCTYPE html><svg width='80' height='40'><defs><path id='p' d='M4 24 H70'/></defs><text fill='red'><textPath href='#p'>Hi</textPath></text></svg>",
        diagnostics);
    RoboWeb::LayoutResult alongLayout = RoboWeb::LayoutDocument(along, 160, 80);
    const RoboWeb::ElementBox* alongBox = RoboWeb::FindBox(alongLayout, "svg");
    EXPECT(alongBox != nullptr);
    if (alongBox != nullptr) {
        bool placed = false;
        for (const RoboWeb::DrawOp& op : alongLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "H" && op.x >= alongBox->x && op.x <= alongBox->x + 12) {
                placed = true;
            }
        }
        EXPECT(placed);
    }
    RoboWeb::Document radial = Parse(
        "<!DOCTYPE html><svg width='40' height='40'><defs><radialGradient id='g'><stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></radialGradient></defs><rect width='40' height='40' fill='url(#g)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult radialLayout = RoboWeb::LayoutDocument(radial, 160, 80);
    RoboWeb::Framebuffer radialFrame = RoboWeb::RenderDocument(radialLayout, 0);
    const RoboWeb::ElementBox* radialBox = RoboWeb::FindBox(radialLayout, "svg");
    EXPECT(radialBox != nullptr);
    if (radialBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(radialFrame, radialBox->x + 20, radialBox->y + 20) == 255);
        EXPECT(RoboWeb::PixelBlue(radialFrame, radialBox->x + 20, radialBox->y + 20) == 0);
        EXPECT(RoboWeb::PixelBlue(radialFrame, radialBox->x + 1, radialBox->y + 1) > 240);
    }
    RoboWeb::Document mids = Parse(
        "<!DOCTYPE html><svg width='40' height='40'><defs><marker id='m'><rect width='6' height='6' fill='red'/></marker></defs><polyline points='2,20 20,20 20,4' fill='none' stroke='black' marker-mid='url(#m)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult midsLayout = RoboWeb::LayoutDocument(mids, 160, 80);
    RoboWeb::Framebuffer midsFrame = RoboWeb::RenderDocument(midsLayout, 0);
    const RoboWeb::ElementBox* midsBox = RoboWeb::FindBox(midsLayout, "svg");
    EXPECT(midsBox != nullptr);
    if (midsBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(midsFrame, midsBox->x + 22, midsBox->y + 22) == 255);
    }
    RoboWeb::Document clipped = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><defs><clipPath id='c'><rect width='10' height='20'/></clipPath></defs><rect width='40' height='20' fill='red' clip-path='url(#c)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult clippedLayout = RoboWeb::LayoutDocument(clipped, 160, 80);
    RoboWeb::Framebuffer clippedFrame = RoboWeb::RenderDocument(clippedLayout, 0);
    const RoboWeb::ElementBox* clippedBox = RoboWeb::FindBox(clippedLayout, "svg");
    EXPECT(clippedBox != nullptr);
    if (clippedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(clippedFrame, clippedBox->x + 2, clippedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(clippedFrame, clippedBox->x + 20, clippedBox->y + 4) == 255);
    }
    RoboWeb::Document patterned = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><defs><pattern id='p' width='10' height='20' patternUnits='userSpaceOnUse'><rect width='5' height='20' fill='red'/></pattern></defs><rect width='40' height='20' fill='url(#p)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult patternedLayout = RoboWeb::LayoutDocument(patterned, 160, 80);
    RoboWeb::Framebuffer patternedFrame = RoboWeb::RenderDocument(patternedLayout, 0);
    const RoboWeb::ElementBox* patternedBox = RoboWeb::FindBox(patternedLayout, "svg");
    EXPECT(patternedBox != nullptr);
    if (patternedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(patternedFrame, patternedBox->x + 2, patternedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(patternedFrame, patternedBox->x + 7, patternedBox->y + 4) == 255);
    }
    RoboWeb::Document masked = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><defs><mask id='m'><rect width='10' height='20' fill='white'/></mask></defs><rect width='40' height='20' fill='red' mask='url(#m)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult maskedLayout = RoboWeb::LayoutDocument(masked, 160, 80);
    RoboWeb::Framebuffer maskedFrame = RoboWeb::RenderDocument(maskedLayout, 0);
    const RoboWeb::ElementBox* maskedBox = RoboWeb::FindBox(maskedLayout, "svg");
    EXPECT(maskedBox != nullptr);
    if (maskedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(maskedFrame, maskedBox->x + 2, maskedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(maskedFrame, maskedBox->x + 20, maskedBox->y + 4) == 255);
    }
    RoboWeb::Document styled = Parse("<!DOCTYPE html><svg><style>#box { background: red }</style></svg><div id='box'>Hi</div>", diagnostics);
    RoboWeb::LayoutResult styledLayout = RoboWeb::LayoutDocument(styled, 160, 80);
    RoboWeb::Framebuffer styledFrame = RoboWeb::RenderDocument(styledLayout, 0);
    const RoboWeb::ElementBox* styledBox = RoboWeb::FindBox(styledLayout, "div");
    EXPECT(styledBox != nullptr);
    if (styledBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(styledFrame, styledBox->x + 2, styledBox->y + 2) == 0);
    }
    RoboWeb::Document scripted = Parse("<!DOCTYPE html><svg><script>document.title = \"FromSvg\"</script></svg>", diagnostics);
    RoboWeb::LayoutDocument(scripted, 80, 40);
    EXPECT(scripted.title == "FromSvg");
    RoboWeb::SetSvgAnimationTime(1);
    RoboWeb::Document animated = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><rect width='10' height='20' fill='red'><animate attributeName='width' from='10' to='30' dur='2s'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult animatedLayout = RoboWeb::LayoutDocument(animated, 160, 80);
    RoboWeb::Framebuffer animatedFrame = RoboWeb::RenderDocument(animatedLayout, 0);
    const RoboWeb::ElementBox* animatedBox = RoboWeb::FindBox(animatedLayout, "svg");
    EXPECT(animatedBox != nullptr);
    if (animatedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(animatedFrame, animatedBox->x + 15, animatedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(animatedFrame, animatedBox->x + 28, animatedBox->y + 4) == 255);
    }
    RoboWeb::SetSvgAnimationTime(0.2);
    RoboWeb::Document flooded = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><defs><filter id='f' filterUnits='userSpaceOnUse'><feFlood x='0' y='0' width='12' height='20' flood-color='red' flood-opacity='1'/></filter></defs><rect width='40' height='20' fill='blue' filter='url(#f)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult floodedLayout = RoboWeb::LayoutDocument(flooded, 160, 80);
    RoboWeb::Framebuffer floodedFrame = RoboWeb::RenderDocument(floodedLayout, 0);
    const RoboWeb::ElementBox* floodedBox = RoboWeb::FindBox(floodedLayout, "svg");
    EXPECT(floodedBox != nullptr);
    if (floodedBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(floodedFrame, floodedBox->x + 2, floodedBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelBlue(floodedFrame, floodedBox->x + 2, floodedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(floodedFrame, floodedBox->x + 24, floodedBox->y + 4) == 255);
    }
    RoboWeb::Document offsetFilter = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><filter id='move' filterUnits='userSpaceOnUse'><feOffset in='SourceGraphic' dx='16' dy='0'/></filter></defs><rect width='8' height='16' fill='red' filter='url(#move)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult offsetLayout = RoboWeb::LayoutDocument(offsetFilter, 160, 80);
    RoboWeb::Framebuffer offsetFrame = RoboWeb::RenderDocument(offsetLayout, 0);
    const RoboWeb::ElementBox* offsetSvg = RoboWeb::FindBox(offsetLayout, "svg");
    EXPECT(offsetSvg != nullptr);
    if (offsetSvg != nullptr) {
        EXPECT(RoboWeb::PixelGreen(offsetFrame, offsetSvg->x + 2, offsetSvg->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(offsetFrame, offsetSvg->x + 18, offsetSvg->y + 4) == 0);
    }
    RoboWeb::SetSvgAnimationTime(1);
    RoboWeb::Document translated = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><rect width='8' height='16' fill='red'><animateTransform attributeName='transform' type='translate' from='0 0' to='20 0' dur='2s'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult translatedLayout = RoboWeb::LayoutDocument(translated, 160, 80);
    RoboWeb::Framebuffer translatedFrame = RoboWeb::RenderDocument(translatedLayout, 0);
    const RoboWeb::ElementBox* translatedBox = RoboWeb::FindBox(translatedLayout, "svg");
    EXPECT(translatedBox != nullptr);
    if (translatedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(translatedFrame, translatedBox->x + 2, translatedBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(translatedFrame, translatedBox->x + 12, translatedBox->y + 4) == 0);
    }
    RoboWeb::Document moved = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><defs><path id='road' d='M0 0 H20'/></defs><rect width='8' height='16' fill='red'><animateMotion dur='2s'><mpath href='#road'/></animateMotion></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult movedLayout = RoboWeb::LayoutDocument(moved, 160, 80);
    RoboWeb::Framebuffer movedFrame = RoboWeb::RenderDocument(movedLayout, 0);
    const RoboWeb::ElementBox* movedBox = RoboWeb::FindBox(movedLayout, "svg");
    EXPECT(movedBox != nullptr);
    if (movedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(movedFrame, movedBox->x + 2, movedBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(movedFrame, movedBox->x + 12, movedBox->y + 4) == 0);
    }
    RoboWeb::Document curvedMotion = Parse(
        "<!DOCTYPE html><svg width='90' height='40'><rect width='8' height='8' fill='#ff0000'><animateMotion dur='2s' path='M0 0 Q40 20 80 0'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult curvedMotionLayout = RoboWeb::LayoutDocument(curvedMotion, 180, 80);
    RoboWeb::Framebuffer curvedMotionFrame = RoboWeb::RenderDocument(curvedMotionLayout, 0);
    const RoboWeb::ElementBox* curvedMotionBox = RoboWeb::FindBox(curvedMotionLayout, "svg");
    EXPECT(curvedMotionBox != nullptr);
    if (curvedMotionBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(curvedMotionFrame, curvedMotionBox->x + 44, curvedMotionBox->y + 2) == 255);
        EXPECT(RoboWeb::PixelGreen(curvedMotionFrame, curvedMotionBox->x + 44, curvedMotionBox->y + 14) == 0);
    }
    RoboWeb::SetSvgAnimationTime(0);
    RoboWeb::Document grayed = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><defs><filter id='gray'><feColorMatrix type='saturate' values='0'/></filter></defs><rect width='20' height='20' fill='red' filter='url(#gray)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult grayedLayout = RoboWeb::LayoutDocument(grayed, 120, 80);
    RoboWeb::Framebuffer grayedFrame = RoboWeb::RenderDocument(grayedLayout, 0);
    const RoboWeb::ElementBox* grayedBox = RoboWeb::FindBox(grayedLayout, "svg");
    EXPECT(grayedBox != nullptr);
    if (grayedBox != nullptr) {
        const int grayRed = RoboWeb::PixelRed(grayedFrame, grayedBox->x + 4, grayedBox->y + 4);
        EXPECT(grayRed > 20 && grayRed < 140);
        EXPECT(RoboWeb::PixelGreen(grayedFrame, grayedBox->x + 4, grayedBox->y + 4) == grayRed);
    }
    RoboWeb::Document blurred = Parse(
        "<!DOCTYPE html><svg width='30' height='16'><defs><filter id='soft'><feGaussianBlur stdDeviation='4'/></filter></defs><rect width='8' height='16' fill='red' filter='url(#soft)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult blurredLayout = RoboWeb::LayoutDocument(blurred, 140, 80);
    RoboWeb::Framebuffer blurredFrame = RoboWeb::RenderDocument(blurredLayout, 0);
    const RoboWeb::ElementBox* blurredBox = RoboWeb::FindBox(blurredLayout, "svg");
    EXPECT(blurredBox != nullptr);
    if (blurredBox != nullptr) {
        // A Gaussian blur keeps partial alpha everywhere: the interior close to
        // the rim blends with the backdrop and the outside fades along the
        // normal curve instead of the old linear ramp.
        const int rim = RoboWeb::PixelGreen(blurredFrame, blurredBox->x + 2, blurredBox->y + 4);
        EXPECT(rim > 60 && rim < 170);
        const int middle = RoboWeb::PixelGreen(blurredFrame, blurredBox->x + 4, blurredBox->y + 8);
        EXPECT(middle < rim);
        const int fringe = RoboWeb::PixelGreen(blurredFrame, blurredBox->x + 10, blurredBox->y + 4);
        EXPECT(fringe > 40 && fringe < 230);
        EXPECT(fringe > middle);
    }
    RoboWeb::Document dilated = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><filter id='wide'><feMorphology operator='dilate' radius='4'/></filter></defs><rect x='10' width='8' height='16' fill='red' filter='url(#wide)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult dilatedLayout = RoboWeb::LayoutDocument(dilated, 160, 80);
    RoboWeb::Framebuffer dilatedFrame = RoboWeb::RenderDocument(dilatedLayout, 0);
    const RoboWeb::ElementBox* dilatedBox = RoboWeb::FindBox(dilatedLayout, "svg");
    EXPECT(dilatedBox != nullptr);
    if (dilatedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(dilatedFrame, dilatedBox->x + 2, dilatedBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(dilatedFrame, dilatedBox->x + 7, dilatedBox->y + 4) == 0);
    }
    RoboWeb::Document viewed = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><view viewBox='0 0 100 100'/><rect width='50' height='100' fill='red'/></svg>", diagnostics);
    RoboWeb::LayoutResult viewedLayout = RoboWeb::LayoutDocument(viewed, 160, 80);
    RoboWeb::Framebuffer viewedFrame = RoboWeb::RenderDocument(viewedLayout, 0);
    const RoboWeb::ElementBox* viewedBox = RoboWeb::FindBox(viewedLayout, "svg");
    EXPECT(viewedBox != nullptr);
    if (viewedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(viewedFrame, viewedBox->x + 14, viewedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(viewedFrame, viewedBox->x + 4, viewedBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(viewedFrame, viewedBox->x + 30, viewedBox->y + 4) == 255);
    }
    RoboWeb::Document transferred = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><defs><filter id='off'><feComponentTransfer><feFuncR type='linear' slope='0' intercept='0'/></feComponentTransfer></filter></defs><rect width='20' height='20' fill='red' filter='url(#off)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult transferredLayout = RoboWeb::LayoutDocument(transferred, 120, 80);
    RoboWeb::Framebuffer transferredFrame = RoboWeb::RenderDocument(transferredLayout, 0);
    const RoboWeb::ElementBox* transferredBox = RoboWeb::FindBox(transferredLayout, "svg");
    EXPECT(transferredBox != nullptr);
    if (transferredBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(transferredFrame, transferredBox->x + 4, transferredBox->y + 4) == 0);
    }
    RoboWeb::Document blended = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><defs><filter id='mix'><feFlood flood-color='blue' result='ink'/><feBlend in='SourceGraphic' in2='ink' mode='multiply'/></filter></defs><rect width='20' height='20' fill='red' filter='url(#mix)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult blendedLayout = RoboWeb::LayoutDocument(blended, 120, 80);
    RoboWeb::Framebuffer blendedFrame = RoboWeb::RenderDocument(blendedLayout, 0);
    const RoboWeb::ElementBox* blendedBox = RoboWeb::FindBox(blendedLayout, "svg");
    EXPECT(blendedBox != nullptr);
    if (blendedBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(blendedFrame, blendedBox->x + 4, blendedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelBlue(blendedFrame, blendedBox->x + 4, blendedBox->y + 4) == 0);
    }
    RoboWeb::Document arithmetic = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><defs><filter id='zero'><feComposite operator='arithmetic' k2='0' k4='0'/></filter></defs><rect width='20' height='20' fill='red' filter='url(#zero)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult arithmeticLayout = RoboWeb::LayoutDocument(arithmetic, 120, 80);
    RoboWeb::Framebuffer arithmeticFrame = RoboWeb::RenderDocument(arithmeticLayout, 0);
    const RoboWeb::ElementBox* arithmeticBox = RoboWeb::FindBox(arithmeticLayout, "svg");
    EXPECT(arithmeticBox != nullptr);
    if (arithmeticBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(arithmeticFrame, arithmeticBox->x + 4, arithmeticBox->y + 4) == 0);
    }
    RoboWeb::Document tiled = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><filter id='rep' filterUnits='userSpaceOnUse'><feTile x='0' y='0' width='32' height='16'/></filter></defs><rect width='8' height='16' fill='red' filter='url(#rep)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult tiledLayout = RoboWeb::LayoutDocument(tiled, 160, 80);
    RoboWeb::Framebuffer tiledFrame = RoboWeb::RenderDocument(tiledLayout, 0);
    const RoboWeb::ElementBox* tiledBox = RoboWeb::FindBox(tiledLayout, "svg");
    EXPECT(tiledBox != nullptr);
    if (tiledBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(tiledFrame, tiledBox->x + 2, tiledBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(tiledFrame, tiledBox->x + 20, tiledBox->y + 4) == 0);
    }
    RoboWeb::Document merged = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><filter id='both' filterUnits='userSpaceOnUse'><feFlood x='0' y='0' width='12' height='16' flood-color='red' result='blob'/><feMerge><feMergeNode in='blob'/><feMergeNode in='SourceGraphic'/></feMerge></filter></defs><rect x='20' width='10' height='16' fill='blue' filter='url(#both)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult mergedLayout = RoboWeb::LayoutDocument(merged, 160, 80);
    RoboWeb::Framebuffer mergedFrame = RoboWeb::RenderDocument(mergedLayout, 0);
    const RoboWeb::ElementBox* mergedBox = RoboWeb::FindBox(mergedLayout, "svg");
    EXPECT(mergedBox != nullptr);
    if (mergedBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(mergedFrame, mergedBox->x + 4, mergedBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelBlue(mergedFrame, mergedBox->x + 24, mergedBox->y + 4) == 255);
    }
    RoboWeb::Document clearFill = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><rect width='20' height='20' fill='red' fill-opacity='0'/></svg>", diagnostics);
    RoboWeb::LayoutResult clearFillLayout = RoboWeb::LayoutDocument(clearFill, 120, 80);
    RoboWeb::Framebuffer clearFillFrame = RoboWeb::RenderDocument(clearFillLayout, 0);
    const RoboWeb::ElementBox* clearFillBox = RoboWeb::FindBox(clearFillLayout, "svg");
    EXPECT(clearFillBox != nullptr);
    if (clearFillBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(clearFillFrame, clearFillBox->x + 4, clearFillBox->y + 4) == 255);
    }
    RoboWeb::Document clearStroke = Parse(
        "<!DOCTYPE html><svg width='30' height='20'><line x1='0' y1='8' x2='30' y2='8' stroke='red' stroke-width='4' stroke-opacity='0'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult clearStrokeLayout = RoboWeb::LayoutDocument(clearStroke, 120, 80);
    RoboWeb::Framebuffer clearStrokeFrame = RoboWeb::RenderDocument(clearStrokeLayout, 0);
    const RoboWeb::ElementBox* clearStrokeBox = RoboWeb::FindBox(clearStrokeLayout, "svg");
    EXPECT(clearStrokeBox != nullptr);
    if (clearStrokeBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(clearStrokeFrame, clearStrokeBox->x + 8, clearStrokeBox->y + 8) == 255);
    }
    RoboWeb::Document reflected = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><linearGradient id='fold' x2='0.5' spreadMethod='reflect'><stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient></defs><rect width='40' height='16' fill='url(#fold)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult reflectedLayout = RoboWeb::LayoutDocument(reflected, 160, 80);
    RoboWeb::Framebuffer reflectedFrame = RoboWeb::RenderDocument(reflectedLayout, 0);
    const RoboWeb::ElementBox* reflectedBox = RoboWeb::FindBox(reflectedLayout, "svg");
    EXPECT(reflectedBox != nullptr);
    if (reflectedBox != nullptr) {
        EXPECT(RoboWeb::PixelBlue(reflectedFrame, reflectedBox->x + 19, reflectedBox->y + 4) > 200);
        EXPECT(RoboWeb::PixelRed(reflectedFrame, reflectedBox->x + 38, reflectedBox->y + 4) > 200);
    }
    RoboWeb::Document focal = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><defs><radialGradient id='side' fx='0' fy='0.5'><stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></radialGradient></defs><rect width='40' height='20' fill='url(#side)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult focalLayout = RoboWeb::LayoutDocument(focal, 160, 80);
    RoboWeb::Framebuffer focalFrame = RoboWeb::RenderDocument(focalLayout, 0);
    const RoboWeb::ElementBox* focalBox = RoboWeb::FindBox(focalLayout, "svg");
    EXPECT(focalBox != nullptr);
    if (focalBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(focalFrame, focalBox->x + 1, focalBox->y + 10) == 0);
        EXPECT(RoboWeb::PixelRed(focalFrame, focalBox->x + 38, focalBox->y + 10) < 30);
    }
    RoboWeb::Document shiftedText = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><text x='0' y='20' dx='24' fill='red'>MMMM</text></svg>", diagnostics);
    RoboWeb::LayoutResult shiftedTextLayout = RoboWeb::LayoutDocument(shiftedText, 160, 80);
    const RoboWeb::ElementBox* shiftedTextBox = RoboWeb::FindBox(shiftedTextLayout, "svg");
    EXPECT(shiftedTextBox != nullptr);
    if (shiftedTextBox != nullptr) {
        bool moved = false;
        for (const RoboWeb::DrawOp& op : shiftedTextLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("MMMM") != std::string::npos && op.x >= shiftedTextBox->x + 20) {
                moved = true;
            }
        }
        EXPECT(moved);
    }
    RoboWeb::Document spaced = Parse(
        "<!DOCTYPE html><svg width='120' height='30'><text x='0' y='20' letter-spacing='8' fill='red'>MMMM</text></svg>", diagnostics);
    RoboWeb::LayoutResult spacedLayout = RoboWeb::LayoutDocument(spaced, 200, 80);
    const RoboWeb::ElementBox* spacedBox = RoboWeb::FindBox(spacedLayout, "svg");
    EXPECT(spacedBox != nullptr);
    if (spacedBox != nullptr) {
        bool apart = false;
        for (const RoboWeb::DrawOp& op : spacedLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "M" && op.x > spacedBox->x + 15) {
                apart = true;
            }
        }
        EXPECT(apart);
    }
    RoboWeb::Document dashed = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><line x1='0' y1='8' x2='40' y2='8' stroke='red' stroke-width='2' stroke-dasharray='4 10'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult dashedLayout = RoboWeb::LayoutDocument(dashed, 160, 80);
    RoboWeb::Framebuffer dashedFrame = RoboWeb::RenderDocument(dashedLayout, 0);
    const RoboWeb::ElementBox* dashedBox = RoboWeb::FindBox(dashedLayout, "svg");
    EXPECT(dashedBox != nullptr);
    if (dashedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(dashedFrame, dashedBox->x + 1, dashedBox->y + 8) == 0);
        EXPECT(RoboWeb::PixelGreen(dashedFrame, dashedBox->x + 8, dashedBox->y + 8) == 255);
    }
    RoboWeb::Document capped = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><line x1='16' y1='8' x2='28' y2='8' stroke='red' stroke-width='8' stroke-linecap='square'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult cappedLayout = RoboWeb::LayoutDocument(capped, 160, 80);
    RoboWeb::Framebuffer cappedFrame = RoboWeb::RenderDocument(cappedLayout, 0);
    const RoboWeb::ElementBox* cappedBox = RoboWeb::FindBox(cappedLayout, "svg");
    EXPECT(cappedBox != nullptr);
    if (cappedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(cappedFrame, cappedBox->x + 13, cappedBox->y + 8) == 0);
        EXPECT(RoboWeb::PixelGreen(cappedFrame, cappedBox->x + 10, cappedBox->y + 8) == 255);
    }
    RoboWeb::Document wideMarker = Parse(
        "<!DOCTYPE html><svg width='50' height='20'><defs><marker id='wide' markerWidth='12' markerHeight='12'><rect width='2' height='2' fill='red'/></marker></defs><line x1='4' y1='4' x2='20' y2='4' stroke='black' marker-end='url(#wide)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult wideMarkerLayout = RoboWeb::LayoutDocument(wideMarker, 160, 80);
    RoboWeb::Framebuffer wideMarkerFrame = RoboWeb::RenderDocument(wideMarkerLayout, 0);
    const RoboWeb::ElementBox* wideMarkerBox = RoboWeb::FindBox(wideMarkerLayout, "svg");
    EXPECT(wideMarkerBox != nullptr);
    if (wideMarkerBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(wideMarkerFrame, wideMarkerBox->x + 26, wideMarkerBox->y + 4) == 0);
    }
    RoboWeb::Document heavy = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><text x='4' y='20' font-weight='bold' font-style='italic' baseline-shift='12' fill='red'>Hi</text></svg>",
        diagnostics);
    RoboWeb::LayoutResult heavyLayout = RoboWeb::LayoutDocument(heavy, 160, 80);
    const RoboWeb::ElementBox* heavyBox = RoboWeb::FindBox(heavyLayout, "svg");
    EXPECT(heavyBox != nullptr);
    if (heavyBox != nullptr) {
        bool styled = false;
        for (const RoboWeb::DrawOp& op : heavyLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("Hi") != std::string::npos && op.bold && op.italic && op.y < heavyBox->y) {
                styled = true;
            }
        }
        EXPECT(styled);
    }
    RoboWeb::Document convolved = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><defs><filter id='flat'><feConvolveMatrix kernelMatrix='0 0 0 0 0 0 0 0 0' bias='1'/></filter></defs><rect width='20' height='20' fill='red' filter='url(#flat)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult convolvedLayout = RoboWeb::LayoutDocument(convolved, 120, 80);
    RoboWeb::Framebuffer convolvedFrame = RoboWeb::RenderDocument(convolvedLayout, 0);
    const RoboWeb::ElementBox* convolvedBox = RoboWeb::FindBox(convolvedLayout, "svg");
    EXPECT(convolvedBox != nullptr);
    if (convolvedBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(convolvedFrame, convolvedBox->x + 4, convolvedBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(convolvedFrame, convolvedBox->x + 4, convolvedBox->y + 4) == 255);
    }
    RoboWeb::Document displaced = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><filter id='nudge' filterUnits='userSpaceOnUse'><feFlood flood-color='black' result='map'/><feDisplacementMap in='SourceGraphic' in2='map' scale='16' xChannelSelector='R' yChannelSelector='R'/></filter></defs><rect x='16' width='8' height='16' fill='red' filter='url(#nudge)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult displacedLayout = RoboWeb::LayoutDocument(displaced, 160, 80);
    RoboWeb::Framebuffer displacedFrame = RoboWeb::RenderDocument(displacedLayout, 0);
    const RoboWeb::ElementBox* displacedBox = RoboWeb::FindBox(displacedLayout, "svg");
    EXPECT(displacedBox != nullptr);
    if (displacedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(displacedFrame, displacedBox->x + 10, displacedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(displacedFrame, displacedBox->x + 20, displacedBox->y + 4) == 255);
    }
    RoboWeb::Document imaged = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><defs><rect id='swatch' width='4' height='4' fill='red'/><filter id='stamp'><feImage href='#swatch'/></filter></defs><rect width='20' height='20' fill='blue' filter='url(#stamp)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult imagedLayout = RoboWeb::LayoutDocument(imaged, 120, 80);
    RoboWeb::Framebuffer imagedFrame = RoboWeb::RenderDocument(imagedLayout, 0);
    const RoboWeb::ElementBox* imagedBox = RoboWeb::FindBox(imagedLayout, "svg");
    EXPECT(imagedBox != nullptr);
    if (imagedBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(imagedFrame, imagedBox->x + 4, imagedBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelBlue(imagedFrame, imagedBox->x + 4, imagedBox->y + 4) == 0);
    }
    RoboWeb::Document offsetDash = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><line x1='0' y1='8' x2='40' y2='8' stroke='red' stroke-width='2' stroke-dasharray='4 10' stroke-dashoffset='4'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult offsetDashLayout = RoboWeb::LayoutDocument(offsetDash, 160, 80);
    RoboWeb::Framebuffer offsetDashFrame = RoboWeb::RenderDocument(offsetDashLayout, 0);
    const RoboWeb::ElementBox* offsetDashBox = RoboWeb::FindBox(offsetDashLayout, "svg");
    EXPECT(offsetDashBox != nullptr);
    if (offsetDashBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(offsetDashFrame, offsetDashBox->x + 1, offsetDashBox->y + 8) == 255);
    }
    RoboWeb::Document decorated = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><text x='4' y='20' font-size='8' text-decoration='underline line-through' fill='red'>Hi</text></svg>",
        diagnostics);
    RoboWeb::LayoutResult decoratedLayout = RoboWeb::LayoutDocument(decorated, 160, 80);
    const RoboWeb::ElementBox* decoratedBox = RoboWeb::FindBox(decoratedLayout, "svg");
    EXPECT(decoratedBox != nullptr);
    if (decoratedBox != nullptr) {
        bool marked = false;
        for (const RoboWeb::DrawOp& op : decoratedLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("Hi") != std::string::npos && op.underline && op.strike && op.fontSizePx <= 10) {
                marked = true;
            }
        }
        EXPECT(marked);
    }
    RoboWeb::Document stretched = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><linearGradient id='long' gradientTransform='scale(2)'><stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient></defs><rect width='40' height='16' fill='url(#long)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult stretchedLayout = RoboWeb::LayoutDocument(stretched, 160, 80);
    RoboWeb::Framebuffer stretchedFrame = RoboWeb::RenderDocument(stretchedLayout, 0);
    const RoboWeb::ElementBox* stretchedBox = RoboWeb::FindBox(stretchedLayout, "svg");
    EXPECT(stretchedBox != nullptr);
    if (stretchedBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(stretchedFrame, stretchedBox->x + 38, stretchedBox->y + 4) > 80);
    }
    RoboWeb::Document focalDisk = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><defs><radialGradient id='disk' fr='0.5'><stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></radialGradient></defs><rect width='40' height='20' fill='url(#disk)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult focalDiskLayout = RoboWeb::LayoutDocument(focalDisk, 160, 80);
    RoboWeb::Framebuffer focalDiskFrame = RoboWeb::RenderDocument(focalDiskLayout, 0);
    const RoboWeb::ElementBox* focalDiskBox = RoboWeb::FindBox(focalDiskLayout, "svg");
    EXPECT(focalDiskBox != nullptr);
    if (focalDiskBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(focalDiskFrame, focalDiskBox->x + 30, focalDiskBox->y + 10) == 255);
        EXPECT(RoboWeb::PixelBlue(focalDiskFrame, focalDiskBox->x + 30, focalDiskBox->y + 10) == 0);
    }
    RoboWeb::Document grain = Parse(
        "<!DOCTYPE html><svg width='24' height='12'><defs><filter id='noise'><feTurbulence type='turbulence' baseFrequency='1' seed='3'/></filter></defs><rect width='24' height='12' fill='red' filter='url(#noise)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult grainLayout = RoboWeb::LayoutDocument(grain, 120, 80);
    RoboWeb::Framebuffer grainFrame = RoboWeb::RenderDocument(grainLayout, 0);
    const RoboWeb::ElementBox* grainBox = RoboWeb::FindBox(grainLayout, "svg");
    EXPECT(grainBox != nullptr);
    if (grainBox != nullptr) {
        const int left = RoboWeb::PixelRed(grainFrame, grainBox->x + 1, grainBox->y + 4) + RoboWeb::PixelGreen(grainFrame, grainBox->x + 1, grainBox->y + 4);
        const int right = RoboWeb::PixelRed(grainFrame, grainBox->x + 8, grainBox->y + 4) + RoboWeb::PixelGreen(grainFrame, grainBox->x + 8, grainBox->y + 4);
        EXPECT(left != right);
    }
    RoboWeb::Document lit = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><defs><filter id='lamp'><feDiffuseLighting lighting-color='#00ff00' diffuseConstant='1'><feDistantLight azimuth='0' elevation='90'/></feDiffuseLighting></filter></defs><rect width='20' height='20' fill='red' filter='url(#lamp)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult litLayout = RoboWeb::LayoutDocument(lit, 120, 80);
    RoboWeb::Framebuffer litFrame = RoboWeb::RenderDocument(litLayout, 0);
    const RoboWeb::ElementBox* litBox = RoboWeb::FindBox(litLayout, "svg");
    EXPECT(litBox != nullptr);
    if (litBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(litFrame, litBox->x + 4, litBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelRed(litFrame, litBox->x + 4, litBox->y + 4) == 0);
    }
    RoboWeb::Document joined = Parse(
        "<!DOCTYPE html><svg width='30' height='24'><polyline points='0,8 12,8 12,20' fill='none' stroke='red' stroke-width='6' stroke-linejoin='round'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult joinedLayout = RoboWeb::LayoutDocument(joined, 120, 80);
    RoboWeb::Framebuffer joinedFrame = RoboWeb::RenderDocument(joinedLayout, 0);
    const RoboWeb::ElementBox* joinedBox = RoboWeb::FindBox(joinedLayout, "svg");
    EXPECT(joinedBox != nullptr);
    if (joinedBox != nullptr) {
        // The round join's disc is antialiased at its rim, so the pixel just
        // past the horizontal band keeps part of the stroke.
        EXPECT(RoboWeb::PixelGreen(joinedFrame, joinedBox->x + 14, joinedBox->y + 6) < 160);
        EXPECT(RoboWeb::PixelGreen(joinedFrame, joinedBox->x + 13, joinedBox->y + 6) == 0);
    }
    RoboWeb::Document hanging = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><text x='4' y='4' dominant-baseline='hanging' fill='red'>Hi</text></svg>", diagnostics);
    RoboWeb::LayoutResult hangingLayout = RoboWeb::LayoutDocument(hanging, 160, 80);
    const RoboWeb::ElementBox* hangingBox = RoboWeb::FindBox(hangingLayout, "svg");
    EXPECT(hangingBox != nullptr);
    if (hangingBox != nullptr) {
        bool top = false;
        for (const RoboWeb::DrawOp& op : hangingLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("Hi") != std::string::npos && op.y >= hangingBox->y) {
                top = true;
            }
        }
        EXPECT(top);
    }
    RoboWeb::Document scaledPattern = Parse(
        "<!DOCTYPE html><svg width='32' height='16'><defs><pattern id='grow' width='16' height='16' patternUnits='userSpaceOnUse' patternTransform='scale(2)'><rect width='4' height='16' fill='red'/></pattern></defs><rect width='32' height='16' fill='url(#grow)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult scaledPatternLayout = RoboWeb::LayoutDocument(scaledPattern, 160, 80);
    RoboWeb::Framebuffer scaledPatternFrame = RoboWeb::RenderDocument(scaledPatternLayout, 0);
    const RoboWeb::ElementBox* scaledPatternBox = RoboWeb::FindBox(scaledPatternLayout, "svg");
    EXPECT(scaledPatternBox != nullptr);
    if (scaledPatternBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(scaledPatternFrame, scaledPatternBox->x + 6, scaledPatternBox->y + 4) == 0);
    }
    RoboWeb::Document shiftedPattern = Parse(
        "<!DOCTYPE html><svg width='32' height='16'><defs><pattern id='slide' width='16' height='16' patternUnits='userSpaceOnUse' patternTransform='translate(8,0)'><rect width='4' height='16' fill='#ff0000'/></pattern></defs><rect width='32' height='16' fill='url(#slide)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult shiftedPatternLayout = RoboWeb::LayoutDocument(shiftedPattern, 160, 80);
    RoboWeb::Framebuffer shiftedPatternFrame = RoboWeb::RenderDocument(shiftedPatternLayout, 0);
    const RoboWeb::ElementBox* shiftedPatternBox = RoboWeb::FindBox(shiftedPatternLayout, "svg");
    EXPECT(shiftedPatternBox != nullptr);
    if (shiftedPatternBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(shiftedPatternFrame, shiftedPatternBox->x + 2, shiftedPatternBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(shiftedPatternFrame, shiftedPatternBox->x + 10, shiftedPatternBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelRed(shiftedPatternFrame, shiftedPatternBox->x + 10, shiftedPatternBox->y + 4) == 255);
    }
    RoboWeb::Document specular = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><defs><filter id='shine'><feSpecularLighting lighting-color='#0000ff' specularConstant='1' specularExponent='1'><feDistantLight azimuth='0' elevation='90'/></feSpecularLighting></filter></defs><rect width='20' height='20' fill='red' filter='url(#shine)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult specularLayout = RoboWeb::LayoutDocument(specular, 120, 80);
    RoboWeb::Framebuffer specularFrame = RoboWeb::RenderDocument(specularLayout, 0);
    const RoboWeb::ElementBox* specularBox = RoboWeb::FindBox(specularLayout, "svg");
    EXPECT(specularBox != nullptr);
    if (specularBox != nullptr) {
        EXPECT(RoboWeb::PixelBlue(specularFrame, specularBox->x + 4, specularBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelRed(specularFrame, specularBox->x + 4, specularBox->y + 4) == 0);
    }
    RoboWeb::Document pointLit = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><filter id='near' filterUnits='userSpaceOnUse'><feDiffuseLighting lighting-color='#00ff00'><fePointLight x='0' y='8' z='4'/></feDiffuseLighting></filter></defs><rect width='40' height='16' fill='red' filter='url(#near)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult pointLitLayout = RoboWeb::LayoutDocument(pointLit, 160, 80);
    RoboWeb::Framebuffer pointLitFrame = RoboWeb::RenderDocument(pointLitLayout, 0);
    const RoboWeb::ElementBox* pointLitBox = RoboWeb::FindBox(pointLitLayout, "svg");
    EXPECT(pointLitBox != nullptr);
    if (pointLitBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(pointLitFrame, pointLitBox->x + 2, pointLitBox->y + 8) > 180);
        EXPECT(RoboWeb::PixelGreen(pointLitFrame, pointLitBox->x + 36, pointLitBox->y + 8) < 80);
    }
    RoboWeb::Document spotLit = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><filter id='beam' filterUnits='userSpaceOnUse'><feDiffuseLighting lighting-color='#00ff00'><feSpotLight x='8' y='8' z='10' pointsAtX='8' pointsAtY='8' pointsAtZ='0' limitingConeAngle='15'/></feDiffuseLighting></filter></defs><rect width='40' height='16' fill='red' filter='url(#beam)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult spotLitLayout = RoboWeb::LayoutDocument(spotLit, 160, 80);
    RoboWeb::Framebuffer spotLitFrame = RoboWeb::RenderDocument(spotLitLayout, 0);
    const RoboWeb::ElementBox* spotLitBox = RoboWeb::FindBox(spotLitLayout, "svg");
    EXPECT(spotLitBox != nullptr);
    if (spotLitBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(spotLitFrame, spotLitBox->x + 8, spotLitBox->y + 8) > 180);
        EXPECT(RoboWeb::PixelGreen(spotLitFrame, spotLitBox->x + 36, spotLitBox->y + 8) < 40);
    }
    RoboWeb::Document boxed = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><filter id='half' filterUnits='objectBoundingBox'><feFlood x='0' y='0' width='0.5' height='1' flood-color='red'/></filter></defs><rect width='40' height='16' fill='blue' filter='url(#half)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult boxedLayout = RoboWeb::LayoutDocument(boxed, 160, 80);
    RoboWeb::Framebuffer boxedFrame = RoboWeb::RenderDocument(boxedLayout, 0);
    const RoboWeb::ElementBox* boxedBox = RoboWeb::FindBox(boxedLayout, "svg");
    EXPECT(boxedBox != nullptr);
    if (boxedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(boxedFrame, boxedBox->x + 4, boxedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(boxedFrame, boxedBox->x + 30, boxedBox->y + 4) == 255);
    }
    RoboWeb::Document current = Parse(
        "<!DOCTYPE html><svg width='20' height='20' color='red'><rect width='20' height='20' fill='currentColor'/></svg>", diagnostics);
    RoboWeb::LayoutResult currentLayout = RoboWeb::LayoutDocument(current, 120, 80);
    RoboWeb::Framebuffer currentFrame = RoboWeb::RenderDocument(currentLayout, 0);
    const RoboWeb::ElementBox* currentBox = RoboWeb::FindBox(currentLayout, "svg");
    EXPECT(currentBox != nullptr);
    if (currentBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(currentFrame, currentBox->x + 4, currentBox->y + 4) == 0);
    }
    RoboWeb::Document reversed = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><text x='60' y='20' direction='rtl' fill='red'>MMMM</text></svg>", diagnostics);
    RoboWeb::LayoutResult reversedLayout = RoboWeb::LayoutDocument(reversed, 160, 80);
    const RoboWeb::ElementBox* reversedBox = RoboWeb::FindBox(reversedLayout, "svg");
    EXPECT(reversedBox != nullptr);
    if (reversedBox != nullptr) {
        bool pulled = false;
        for (const RoboWeb::DrawOp& op : reversedLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("MMMM") != std::string::npos && op.x < reversedBox->x + 60) {
                pulled = true;
            }
        }
        EXPECT(pulled);
    }
    RoboWeb::Document aligned = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><text x='4' y='4' alignment-baseline='hanging' fill='red'>Hi</text></svg>", diagnostics);
    RoboWeb::LayoutResult alignedLayout = RoboWeb::LayoutDocument(aligned, 160, 80);
    const RoboWeb::ElementBox* alignedBox = RoboWeb::FindBox(alignedLayout, "svg");
    EXPECT(alignedBox != nullptr);
    if (alignedBox != nullptr) {
        bool top = false;
        for (const RoboWeb::DrawOp& op : alignedLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("Hi") != std::string::npos && op.y >= alignedBox->y) {
                top = true;
            }
        }
        EXPECT(top);
    }
    RoboWeb::SetSvgAnimationTime(0.8);
    RoboWeb::Document stepped = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><rect width='10' height='16' fill='red'><animate attributeName='width' from='10' to='30' dur='2s' calcMode='discrete'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult steppedLayout = RoboWeb::LayoutDocument(stepped, 160, 80);
    RoboWeb::Framebuffer steppedFrame = RoboWeb::RenderDocument(steppedLayout, 0);
    const RoboWeb::ElementBox* steppedBox = RoboWeb::FindBox(steppedLayout, "svg");
    EXPECT(steppedBox != nullptr);
    if (steppedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(steppedFrame, steppedBox->x + 15, steppedBox->y + 4) == 255);
    }
    RoboWeb::SetSvgAnimationTime(3);
    RoboWeb::Document repeated = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><rect width='10' height='16' fill='red'><animate attributeName='width' from='10' to='30' dur='2s' repeatDur='4s'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult repeatedLayout = RoboWeb::LayoutDocument(repeated, 160, 80);
    RoboWeb::Framebuffer repeatedFrame = RoboWeb::RenderDocument(repeatedLayout, 0);
    const RoboWeb::ElementBox* repeatedBox = RoboWeb::FindBox(repeatedLayout, "svg");
    EXPECT(repeatedBox != nullptr);
    if (repeatedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(repeatedFrame, repeatedBox->x + 15, repeatedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(repeatedFrame, repeatedBox->x + 28, repeatedBox->y + 4) == 255);
    }
    RoboWeb::SetSvgAnimationTime(0);
    RoboWeb::SetSvgAnimationTime(1);
    RoboWeb::Document byMotion = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><rect width='10' height='16' fill='red'><animate attributeName='width' by='20' dur='2s'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult byMotionLayout = RoboWeb::LayoutDocument(byMotion, 160, 80);
    RoboWeb::Framebuffer byMotionFrame = RoboWeb::RenderDocument(byMotionLayout, 0);
    const RoboWeb::ElementBox* byMotionBox = RoboWeb::FindBox(byMotionLayout, "svg");
    EXPECT(byMotionBox != nullptr);
    if (byMotionBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(byMotionFrame, byMotionBox->x + 15, byMotionBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(byMotionFrame, byMotionBox->x + 28, byMotionBox->y + 4) == 255);
    }
    RoboWeb::Document summed = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><rect width='10' height='16' fill='red'><animate attributeName='width' from='0' to='16' dur='2s' additive='sum'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult summedLayout = RoboWeb::LayoutDocument(summed, 160, 80);
    RoboWeb::Framebuffer summedFrame = RoboWeb::RenderDocument(summedLayout, 0);
    const RoboWeb::ElementBox* summedBox = RoboWeb::FindBox(summedLayout, "svg");
    EXPECT(summedBox != nullptr);
    if (summedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(summedFrame, summedBox->x + 12, summedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(summedFrame, summedBox->x + 24, summedBox->y + 4) == 255);
    }
    RoboWeb::SetSvgAnimationTime(0);
    RoboWeb::Document ordered = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><rect width='20' height='20' fill='red' stroke='blue' stroke-width='8' paint-order='stroke fill'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult orderedLayout = RoboWeb::LayoutDocument(ordered, 120, 80);
    RoboWeb::Framebuffer orderedFrame = RoboWeb::RenderDocument(orderedLayout, 0);
    const RoboWeb::ElementBox* orderedBox = RoboWeb::FindBox(orderedLayout, "svg");
    EXPECT(orderedBox != nullptr);
    if (orderedBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(orderedFrame, orderedBox->x + 2, orderedBox->y + 2) == 255);
        EXPECT(RoboWeb::PixelBlue(orderedFrame, orderedBox->x + 2, orderedBox->y + 2) == 0);
    }
    RoboWeb::Document fixedStroke = Parse(
        "<!DOCTYPE html><svg width='40' height='20' viewBox='0 0 10 10'><line x1='0' y1='5' x2='10' y2='5' stroke='red' stroke-width='2' vector-effect='non-scaling-stroke'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult fixedStrokeLayout = RoboWeb::LayoutDocument(fixedStroke, 160, 80);
    RoboWeb::Framebuffer fixedStrokeFrame = RoboWeb::RenderDocument(fixedStrokeLayout, 0);
    const RoboWeb::ElementBox* fixedStrokeBox = RoboWeb::FindBox(fixedStrokeLayout, "svg");
    EXPECT(fixedStrokeBox != nullptr);
    if (fixedStrokeBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(fixedStrokeFrame, fixedStrokeBox->x + 8, fixedStrokeBox->y + 13) == 255);
    }
    RoboWeb::Document turned = Parse(
        "<!DOCTYPE html><svg width='40' height='24'><defs><marker id='turn' orient='90'><rect width='8' height='2' fill='red'/></marker></defs><line x1='2' y1='8' x2='16' y2='8' stroke='black' marker-end='url(#turn)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult turnedLayout = RoboWeb::LayoutDocument(turned, 160, 80);
    RoboWeb::Framebuffer turnedFrame = RoboWeb::RenderDocument(turnedLayout, 0);
    const RoboWeb::ElementBox* turnedBox = RoboWeb::FindBox(turnedLayout, "svg");
    EXPECT(turnedBox != nullptr);
    if (turnedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(turnedFrame, turnedBox->x + 15, turnedBox->y + 12) == 0);
    }
    RoboWeb::Document contentUnits = Parse(
        "<!DOCTYPE html><svg width='32' height='16'><defs><pattern id='unit' width='16' height='16' patternUnits='userSpaceOnUse' patternContentUnits='objectBoundingBox'><rect width='1' height='1' fill='red'/></pattern></defs><rect width='32' height='16' fill='url(#unit)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult contentUnitsLayout = RoboWeb::LayoutDocument(contentUnits, 160, 80);
    RoboWeb::Framebuffer contentUnitsFrame = RoboWeb::RenderDocument(contentUnitsLayout, 0);
    const RoboWeb::ElementBox* contentUnitsBox = RoboWeb::FindBox(contentUnitsLayout, "svg");
    EXPECT(contentUnitsBox != nullptr);
    if (contentUnitsBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(contentUnitsFrame, contentUnitsBox->x + 8, contentUnitsBox->y + 4) == 0);
    }
    RoboWeb::Document inert = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><a href='page.html' pointer-events='none'><rect width='20' height='20' fill='red'/></a></svg>",
        diagnostics);
    RoboWeb::LayoutResult inertLayout = RoboWeb::LayoutDocument(inert, 160, 80);
    const RoboWeb::ElementBox* inertBox = RoboWeb::FindBox(inertLayout, "svg");
    EXPECT(inertBox != nullptr);
    if (inertBox != nullptr) {
        EXPECT(!RoboWeb::DispatchClick(inert, inertLayout, inertBox->x + 4, inertBox->y + 4));
        EXPECT(inert.status.find("open page.html") == std::string::npos);
    }
    RoboWeb::Document unsupported = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><switch><rect width='20' height='20' fill='green' requiredFeatures='http://www.w3.org/TR/SVG11/feature#SVG'/><rect width='20' height='20' fill='red'/></switch></svg>",
        diagnostics);
    RoboWeb::LayoutResult unsupportedLayout = RoboWeb::LayoutDocument(unsupported, 120, 80);
    RoboWeb::Framebuffer unsupportedFrame = RoboWeb::RenderDocument(unsupportedLayout, 0);
    const RoboWeb::ElementBox* unsupportedBox = RoboWeb::FindBox(unsupportedLayout, "svg");
    EXPECT(unsupportedBox != nullptr);
    if (unsupportedBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(unsupportedFrame, unsupportedBox->x + 4, unsupportedBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(unsupportedFrame, unsupportedBox->x + 4, unsupportedBox->y + 4) == 0);
    }
    RoboWeb::Document overflowClip = Parse(
        "<!DOCTYPE html><svg width='20' height='16' overflow='hidden'><rect width='40' height='16' fill='red'/></svg>", diagnostics);
    RoboWeb::LayoutResult overflowClipLayout = RoboWeb::LayoutDocument(overflowClip, 160, 80);
    RoboWeb::Framebuffer overflowClipFrame = RoboWeb::RenderDocument(overflowClipLayout, 0);
    const RoboWeb::ElementBox* overflowClipBox = RoboWeb::FindBox(overflowClipLayout, "svg");
    EXPECT(overflowClipBox != nullptr);
    if (overflowClipBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(overflowClipFrame, overflowClipBox->x + 4, overflowClipBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(overflowClipFrame, overflowClipBox->x + 25, overflowClipBox->y + 4) == 255);
    }
    RoboWeb::Document userMarker = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><marker id='plain' markerUnits='userSpaceOnUse'><rect width='2' height='4' fill='red'/></marker></defs><line x1='2' y1='6' x2='16' y2='6' stroke='black' stroke-width='4' marker-end='url(#plain)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult userMarkerLayout = RoboWeb::LayoutDocument(userMarker, 160, 80);
    RoboWeb::Framebuffer userMarkerFrame = RoboWeb::RenderDocument(userMarkerLayout, 0);
    const RoboWeb::ElementBox* userMarkerBox = RoboWeb::FindBox(userMarkerLayout, "svg");
    EXPECT(userMarkerBox != nullptr);
    if (userMarkerBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(userMarkerFrame, userMarkerBox->x + 21, userMarkerBox->y + 6) == 255);
    }
    RoboWeb::Document stretchedText = Parse(
        "<!DOCTYPE html><svg width='120' height='30'><text x='0' y='20' textLength='80' fill='red'>MMMM</text></svg>", diagnostics);
    RoboWeb::LayoutResult stretchedTextLayout = RoboWeb::LayoutDocument(stretchedText, 200, 80);
    const RoboWeb::ElementBox* stretchedTextBox = RoboWeb::FindBox(stretchedTextLayout, "svg");
    EXPECT(stretchedTextBox != nullptr);
    if (stretchedTextBox != nullptr) {
        bool spread = false;
        for (const RoboWeb::DrawOp& op : stretchedTextLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "M" && op.x > stretchedTextBox->x + 30) {
                spread = true;
            }
        }
        EXPECT(spread);
    }
    RoboWeb::Document fitted = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><text x='0' y='20' textLength='8' lengthAdjust='spacingAndGlyphs' fill='red'>Hi</text></svg>",
        diagnostics);
    RoboWeb::LayoutResult fittedLayout = RoboWeb::LayoutDocument(fitted, 160, 80);
    const RoboWeb::ElementBox* fittedBox = RoboWeb::FindBox(fittedLayout, "svg");
    EXPECT(fittedBox != nullptr);
    if (fittedBox != nullptr) {
        bool small = false;
        for (const RoboWeb::DrawOp& op : fittedLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("Hi") != std::string::npos && op.fontSizePx <= 10) {
                small = true;
            }
        }
        EXPECT(small);
    }
    RoboWeb::Document wordGap = Parse(
        "<!DOCTYPE html><svg width='120' height='30'><text x='0' y='20' word-spacing='24' fill='red'>M M</text></svg>", diagnostics);
    RoboWeb::LayoutResult wordGapLayout = RoboWeb::LayoutDocument(wordGap, 200, 80);
    const RoboWeb::ElementBox* wordGapBox = RoboWeb::FindBox(wordGapLayout, "svg");
    EXPECT(wordGapBox != nullptr);
    if (wordGapBox != nullptr) {
        bool apart = false;
        for (const RoboWeb::DrawOp& op : wordGapLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "M" && op.x > wordGapBox->x + 20) {
                apart = true;
            }
        }
        EXPECT(apart);
    }
    RoboWeb::Document tabulated = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><defs><filter id='zero'><feComponentTransfer><feFuncR type='table' tableValues='0 0'/></feComponentTransfer></filter></defs><rect width='20' height='20' fill='red' filter='url(#zero)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult tabulatedLayout = RoboWeb::LayoutDocument(tabulated, 120, 80);
    RoboWeb::Framebuffer tabulatedFrame = RoboWeb::RenderDocument(tabulatedLayout, 0);
    const RoboWeb::ElementBox* tabulatedBox = RoboWeb::FindBox(tabulatedLayout, "svg");
    EXPECT(tabulatedBox != nullptr);
    if (tabulatedBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(tabulatedFrame, tabulatedBox->x + 4, tabulatedBox->y + 4) == 0);
    }
    RoboWeb::Document fittedBoxRatio = Parse(
        "<!DOCTYPE html><svg width='40' height='20' viewBox='0 0 100 100' preserveAspectRatio='xMinYMin meet'><rect width='100' height='100' fill='red'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult fittedBoxRatioLayout = RoboWeb::LayoutDocument(fittedBoxRatio, 160, 80);
    RoboWeb::Framebuffer fittedBoxRatioFrame = RoboWeb::RenderDocument(fittedBoxRatioLayout, 0);
    const RoboWeb::ElementBox* fittedBoxRatioBox = RoboWeb::FindBox(fittedBoxRatioLayout, "svg");
    EXPECT(fittedBoxRatioBox != nullptr);
    if (fittedBoxRatioBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(fittedBoxRatioFrame, fittedBoxRatioBox->x + 4, fittedBoxRatioBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(fittedBoxRatioFrame, fittedBoxRatioBox->x + 30, fittedBoxRatioBox->y + 4) == 255);
    }
    RoboWeb::SetSvgAnimationTime(1);
    RoboWeb::Document listed = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><rect width='10' height='16' fill='red'><animate attributeName='width' values='10;30' dur='2s'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult listedLayout = RoboWeb::LayoutDocument(listed, 160, 80);
    RoboWeb::Framebuffer listedFrame = RoboWeb::RenderDocument(listedLayout, 0);
    const RoboWeb::ElementBox* listedBox = RoboWeb::FindBox(listedLayout, "svg");
    EXPECT(listedBox != nullptr);
    if (listedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(listedFrame, listedBox->x + 15, listedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(listedFrame, listedBox->x + 28, listedBox->y + 4) == 255);
    }
    RoboWeb::Document keyed = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><rect width='10' height='16' fill='red'><animate attributeName='width' values='10;30' keyTimes='0;0.25' dur='2s'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult keyedLayout = RoboWeb::LayoutDocument(keyed, 160, 80);
    RoboWeb::Framebuffer keyedFrame = RoboWeb::RenderDocument(keyedLayout, 0);
    const RoboWeb::ElementBox* keyedBox = RoboWeb::FindBox(keyedLayout, "svg");
    EXPECT(keyedBox != nullptr);
    if (keyedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(keyedFrame, keyedBox->x + 28, keyedBox->y + 4) == 0);
    }
    RoboWeb::SetSvgAnimationTime(3);
    RoboWeb::Document accumulated = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><rect width='10' height='16' fill='red'><animate attributeName='width' from='0' to='10' dur='2s' repeatCount='2' accumulate='sum'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult accumulatedLayout = RoboWeb::LayoutDocument(accumulated, 160, 80);
    RoboWeb::Framebuffer accumulatedFrame = RoboWeb::RenderDocument(accumulatedLayout, 0);
    const RoboWeb::ElementBox* accumulatedBox = RoboWeb::FindBox(accumulatedLayout, "svg");
    EXPECT(accumulatedBox != nullptr);
    if (accumulatedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(accumulatedFrame, accumulatedBox->x + 12, accumulatedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(accumulatedFrame, accumulatedBox->x + 20, accumulatedBox->y + 4) == 255);
    }
    RoboWeb::SetSvgAnimationTime(0);
    RoboWeb::Document contentMask = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><mask id='half' maskContentUnits='objectBoundingBox'><rect width='0.5' height='1' fill='white'/></mask></defs><rect width='40' height='16' fill='red' mask='url(#half)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult contentMaskLayout = RoboWeb::LayoutDocument(contentMask, 160, 80);
    RoboWeb::Framebuffer contentMaskFrame = RoboWeb::RenderDocument(contentMaskLayout, 0);
    const RoboWeb::ElementBox* contentMaskBox = RoboWeb::FindBox(contentMaskLayout, "svg");
    EXPECT(contentMaskBox != nullptr);
    if (contentMaskBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(contentMaskFrame, contentMaskBox->x + 4, contentMaskBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(contentMaskFrame, contentMaskBox->x + 30, contentMaskBox->y + 4) == 255);
    }
    RoboWeb::Document boxClip = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><clipPath id='halfclip' clipPathUnits='objectBoundingBox'><rect width='0.5' height='1'/></clipPath></defs><rect width='40' height='16' fill='red' clip-path='url(#halfclip)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult boxClipLayout = RoboWeb::LayoutDocument(boxClip, 160, 80);
    RoboWeb::Framebuffer boxClipFrame = RoboWeb::RenderDocument(boxClipLayout, 0);
    const RoboWeb::ElementBox* boxClipBox = RoboWeb::FindBox(boxClipLayout, "svg");
    EXPECT(boxClipBox != nullptr);
    if (boxClipBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(boxClipFrame, boxClipBox->x + 4, boxClipBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(boxClipFrame, boxClipBox->x + 30, boxClipBox->y + 4) == 255);
    }
    RoboWeb::Document spun = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><rect width='10' height='10' fill='red' transform='rotate(180)' transform-origin='5 5'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult spunLayout = RoboWeb::LayoutDocument(spun, 120, 80);
    RoboWeb::Framebuffer spunFrame = RoboWeb::RenderDocument(spunLayout, 0);
    const RoboWeb::ElementBox* spunBox = RoboWeb::FindBox(spunLayout, "svg");
    EXPECT(spunBox != nullptr);
    if (spunBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(spunFrame, spunBox->x + 4, spunBox->y + 4) == 0);
    }
    RoboWeb::SetSvgAnimationTime(1.5);
    RoboWeb::Document ended = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><rect width='10' height='16' fill='red'><animate attributeName='width' from='10' to='30' dur='2s' end='1s'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult endedLayout = RoboWeb::LayoutDocument(ended, 160, 80);
    RoboWeb::Framebuffer endedFrame = RoboWeb::RenderDocument(endedLayout, 0);
    const RoboWeb::ElementBox* endedBox = RoboWeb::FindBox(endedLayout, "svg");
    EXPECT(endedBox != nullptr);
    if (endedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(endedFrame, endedBox->x + 15, endedBox->y + 4) == 255);
    }
    RoboWeb::SetSvgAnimationTime(0);
    RoboWeb::Document regionMask = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><mask id='band' maskUnits='objectBoundingBox' x='0' y='0' width='0.5' height='1'><rect width='40' height='16' fill='white'/></mask></defs><rect width='40' height='16' fill='red' mask='url(#band)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult regionMaskLayout = RoboWeb::LayoutDocument(regionMask, 160, 80);
    RoboWeb::Framebuffer regionMaskFrame = RoboWeb::RenderDocument(regionMaskLayout, 0);
    const RoboWeb::ElementBox* regionMaskBox = RoboWeb::FindBox(regionMaskLayout, "svg");
    EXPECT(regionMaskBox != nullptr);
    if (regionMaskBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(regionMaskFrame, regionMaskBox->x + 4, regionMaskBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(regionMaskFrame, regionMaskBox->x + 30, regionMaskBox->y + 4) == 255);
    }
    RoboWeb::Document measured = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><line x1='0' y1='8' x2='40' y2='8' stroke='red' stroke-width='2' stroke-dasharray='40 40' pathLength='80'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult measuredLayout = RoboWeb::LayoutDocument(measured, 160, 80);
    RoboWeb::Framebuffer measuredFrame = RoboWeb::RenderDocument(measuredLayout, 0);
    const RoboWeb::ElementBox* measuredBox = RoboWeb::FindBox(measuredLayout, "svg");
    EXPECT(measuredBox != nullptr);
    if (measuredBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(measuredFrame, measuredBox->x + 4, measuredBox->y + 8) == 0);
        EXPECT(RoboWeb::PixelGreen(measuredFrame, measuredBox->x + 30, measuredBox->y + 8) == 255);
    }
    RoboWeb::Document styledFill = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><rect width='20' height='20' style='fill:#00ff00'/></svg>", diagnostics);
    RoboWeb::LayoutResult styledFillLayout = RoboWeb::LayoutDocument(styledFill, 120, 80);
    RoboWeb::Framebuffer styledFillFrame = RoboWeb::RenderDocument(styledFillLayout, 0);
    const RoboWeb::ElementBox* styledFillBox = RoboWeb::FindBox(styledFillLayout, "svg");
    EXPECT(styledFillBox != nullptr);
    if (styledFillBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(styledFillFrame, styledFillBox->x + 4, styledFillBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelRed(styledFillFrame, styledFillBox->x + 4, styledFillBox->y + 4) == 0);
    }
    RoboWeb::Document targeted = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><a href='page.html' target='_blank'><rect width='20' height='20' fill='red'/></a></svg>",
        diagnostics);
    RoboWeb::LayoutResult targetedLayout = RoboWeb::LayoutDocument(targeted, 160, 80);
    const RoboWeb::ElementBox* targetedBox = RoboWeb::FindBox(targetedLayout, "svg");
    EXPECT(targetedBox != nullptr);
    if (targetedBox != nullptr) {
        EXPECT(RoboWeb::DispatchClick(targeted, targetedLayout, targetedBox->x + 4, targetedBox->y + 4));
        EXPECT(targeted.status.find("in _blank") != std::string::npos);
    }
    RoboWeb::Document saved = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><a href='page.html' download='file.txt'><rect width='20' height='20' fill='red'/></a></svg>",
        diagnostics);
    RoboWeb::LayoutResult savedLayout = RoboWeb::LayoutDocument(saved, 160, 80);
    const RoboWeb::ElementBox* savedBox = RoboWeb::FindBox(savedLayout, "svg");
    EXPECT(savedBox != nullptr);
    if (savedBox != nullptr) {
        EXPECT(RoboWeb::DispatchClick(saved, savedLayout, savedBox->x + 4, savedBox->y + 4));
        EXPECT(saved.status.find("download file.txt") != std::string::npos);
    }
    RoboWeb::Document clippedShape = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><rect width='40' height='16' fill='red' clip='rect(0, 10, 16, 0)'/></svg>", diagnostics);
    RoboWeb::LayoutResult clippedShapeLayout = RoboWeb::LayoutDocument(clippedShape, 160, 80);
    RoboWeb::Framebuffer clippedShapeFrame = RoboWeb::RenderDocument(clippedShapeLayout, 0);
    const RoboWeb::ElementBox* clippedShapeBox = RoboWeb::FindBox(clippedShapeLayout, "svg");
    EXPECT(clippedShapeBox != nullptr);
    if (clippedShapeBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(clippedShapeFrame, clippedShapeBox->x + 4, clippedShapeBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(clippedShapeFrame, clippedShapeBox->x + 20, clippedShapeBox->y + 4) == 255);
    }
    RoboWeb::Document verticalText = Parse(
        "<!DOCTYPE html><svg width='40' height='40'><text x='4' y='16' writing-mode='tb' fill='red'>Hi</text></svg>", diagnostics);
    RoboWeb::LayoutResult verticalTextLayout = RoboWeb::LayoutDocument(verticalText, 160, 80);
    const RoboWeb::ElementBox* verticalTextBox = RoboWeb::FindBox(verticalTextLayout, "svg");
    EXPECT(verticalTextBox != nullptr);
    if (verticalTextBox != nullptr) {
        bool stacked = false;
        for (const RoboWeb::DrawOp& op : verticalTextLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "i" && op.y > verticalTextBox->y + 8) {
                stacked = true;
            }
        }
        EXPECT(stacked);
    }
    RoboWeb::Document fractionShift = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><filter id='half' primitiveUnits='objectBoundingBox'><feOffset dx='0.5' dy='0'/></filter></defs><rect width='8' height='16' fill='red' filter='url(#half)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult fractionShiftLayout = RoboWeb::LayoutDocument(fractionShift, 160, 80);
    RoboWeb::Framebuffer fractionShiftFrame = RoboWeb::RenderDocument(fractionShiftLayout, 0);
    const RoboWeb::ElementBox* fractionShiftBox = RoboWeb::FindBox(fractionShiftLayout, "svg");
    EXPECT(fractionShiftBox != nullptr);
    if (fractionShiftBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(fractionShiftFrame, fractionShiftBox->x + 2, fractionShiftBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(fractionShiftFrame, fractionShiftBox->x + 6, fractionShiftBox->y + 4) == 0);
    }
    RoboWeb::Document below = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><path id='line' d='M0 8 H40'/><text fill='red'><textPath href='#line' side='right'>H</textPath></text></svg>",
        diagnostics);
    RoboWeb::LayoutResult belowLayout = RoboWeb::LayoutDocument(below, 160, 80);
    const RoboWeb::ElementBox* belowBox = RoboWeb::FindBox(belowLayout, "svg");
    EXPECT(belowBox != nullptr);
    if (belowBox != nullptr) {
        bool under = false;
        for (const RoboWeb::DrawOp& op : belowLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "H" && op.y > belowBox->y + 4) {
                under = true;
            }
        }
        EXPECT(under);
    }
    RoboWeb::Document pointed = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><a href='page.html' cursor='text'><rect width='20' height='20' fill='red'/></a></svg>",
        diagnostics);
    RoboWeb::LayoutResult pointedLayout = RoboWeb::LayoutDocument(pointed, 160, 80);
    const RoboWeb::ElementBox* pointedBox = RoboWeb::FindBox(pointedLayout, "svg");
    EXPECT(pointedBox != nullptr);
    if (pointedBox != nullptr) {
        EXPECT(RoboWeb::PointerAt(pointedLayout, pointedBox->x + 4, pointedBox->y + 4, 0) == RoboWeb::PointerShape::Text);
    }
    RoboWeb::Document tabbed = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><rect id='spot' tabindex='0' width='20' height='20' fill='red'/></svg>", diagnostics);
    EXPECT(RoboWeb::DispatchKey(tabbed, "tab"));
    EXPECT(tabbed.focus != nullptr && RoboWeb::AttributeValue(*tabbed.focus, "id") == "spot");
    RoboWeb::Document exactText = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><path id='road' d='M0 20 H60'/><text fill='red'><textPath href='#road' spacing='exact'>AB</textPath></text></svg>",
        diagnostics);
    RoboWeb::LayoutResult exactTextLayout = RoboWeb::LayoutDocument(exactText, 160, 80);
    const RoboWeb::ElementBox* exactTextBox = RoboWeb::FindBox(exactTextLayout, "svg");
    EXPECT(exactTextBox != nullptr);
    if (exactTextBox != nullptr) {
        bool far = false;
        for (const RoboWeb::DrawOp& op : exactTextLayout.ops) {
            if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "B" && op.x > exactTextBox->x + 40) {
                far = true;
            }
        }
        EXPECT(far);
    }
    RoboWeb::SetSvgAnimationTime(1);
    RoboWeb::Document turnedMotion = Parse(
        "<!DOCTYPE html><svg width='30' height='30'><rect y='-2' width='8' height='2' fill='red'><animateMotion dur='2s' rotate='auto' path='M0 0 V20'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult turnedMotionLayout = RoboWeb::LayoutDocument(turnedMotion, 120, 80);
    RoboWeb::Framebuffer turnedMotionFrame = RoboWeb::RenderDocument(turnedMotionLayout, 0);
    const RoboWeb::ElementBox* turnedMotionBox = RoboWeb::FindBox(turnedMotionLayout, "svg");
    EXPECT(turnedMotionBox != nullptr);
    if (turnedMotionBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(turnedMotionFrame, turnedMotionBox->x + 1, turnedMotionBox->y + 14) == 0);
    }
    RoboWeb::SetSvgAnimationTime(0);
    RoboWeb::Document oneOctave = Parse(
        "<!DOCTYPE html><svg width='16' height='8'><defs><filter id='n'><feTurbulence baseFrequency='1' numOctaves='1' seed='2'/></filter></defs><rect width='16' height='8' filter='url(#n)'/></svg>",
        diagnostics);
    RoboWeb::Document twoOctaves = Parse(
        "<!DOCTYPE html><svg width='16' height='8'><defs><filter id='n'><feTurbulence baseFrequency='1' numOctaves='2' seed='2'/></filter></defs><rect width='16' height='8' filter='url(#n)'/></svg>",
        diagnostics);
    RoboWeb::Framebuffer oneFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(oneOctave, 80, 40), 0);
    RoboWeb::Framebuffer twoFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(twoOctaves, 80, 40), 0);
    const RoboWeb::ElementBox* oneBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(oneOctave, 80, 40), "svg");
    EXPECT(oneBox != nullptr);
    if (oneBox != nullptr) {
        const int first = RoboWeb::PixelRed(oneFrame, oneBox->x + 3, oneBox->y + 3) + RoboWeb::PixelGreen(oneFrame, oneBox->x + 3, oneBox->y + 3);
        const int second = RoboWeb::PixelRed(twoFrame, oneBox->x + 3, oneBox->y + 3) + RoboWeb::PixelGreen(twoFrame, oneBox->x + 3, oneBox->y + 3);
        EXPECT(first != second);
    }
    RoboWeb::Document mono = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><text x='4' y='20' font-family='monospace' fill='red'>Hi</text></svg>", diagnostics);
    RoboWeb::LayoutResult monoLayout = RoboWeb::LayoutDocument(mono, 160, 80);
    bool monoFace = false;
    for (const RoboWeb::DrawOp& op : monoLayout.ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("Hi") != std::string::npos && op.monospace) {
            monoFace = true;
        }
    }
    EXPECT(monoFace);
    RoboWeb::Document preserved = Parse(
        "<!DOCTYPE html><svg width='120' height='30'><text x='0' y='20' white-space='pre' fill='red'>M    M</text></svg>", diagnostics);
    RoboWeb::Document collapsed = Parse(
        "<!DOCTYPE html><svg width='120' height='30'><text x='0' y='20' fill='red'>M    M</text></svg>", diagnostics);
    bool keptSpaces = false;
    bool foldedSpaces = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(preserved, 200, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("M    M") != std::string::npos) {
            keptSpaces = true;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(collapsed, 200, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("M M") != std::string::npos && op.text.find("M    M") == std::string::npos) {
            foldedSpaces = true;
        }
    }
    EXPECT(keptSpaces && foldedSpaces);
    RoboWeb::Document gammaRed = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='g'><feComponentTransfer><feFuncR type='gamma' exponent='4' amplitude='1'/></feComponentTransfer></filter></defs><rect width='16' height='16' fill='#808080' filter='url(#g)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult gammaLayout = RoboWeb::LayoutDocument(gammaRed, 80, 40);
    RoboWeb::Framebuffer gammaFrame = RoboWeb::RenderDocument(gammaLayout, 0);
    const RoboWeb::ElementBox* gammaBox = RoboWeb::FindBox(gammaLayout, "svg");
    EXPECT(gammaBox != nullptr);
    if (gammaBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(gammaFrame, gammaBox->x + 4, gammaBox->y + 4) < 40);
        EXPECT(RoboWeb::PixelGreen(gammaFrame, gammaBox->x + 4, gammaBox->y + 4) > 100);
    }
    RoboWeb::Document quiet = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='g'><feComponentTransfer><feFuncR type='gamma' amplitude='0'/></feComponentTransfer></filter></defs><rect width='16' height='16' fill='#808080' filter='url(#g)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult quietLayout = RoboWeb::LayoutDocument(quiet, 80, 40);
    RoboWeb::Framebuffer quietFrame = RoboWeb::RenderDocument(quietLayout, 0);
    const RoboWeb::ElementBox* quietBox = RoboWeb::FindBox(quietLayout, "svg");
    EXPECT(quietBox != nullptr);
    if (quietBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(quietFrame, quietBox->x + 4, quietBox->y + 4) == 0);
    }
    RoboWeb::Document halved = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='k'><feConvolveMatrix kernelMatrix='1' divisor='2'/></filter></defs><rect width='16' height='16' fill='red' filter='url(#k)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult halvedLayout = RoboWeb::LayoutDocument(halved, 80, 40);
    RoboWeb::Framebuffer halvedFrame = RoboWeb::RenderDocument(halvedLayout, 0);
    const RoboWeb::ElementBox* halvedBox = RoboWeb::FindBox(halvedLayout, "svg");
    EXPECT(halvedBox != nullptr);
    if (halvedBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(halvedFrame, halvedBox->x + 4, halvedBox->y + 4) < 180);
        EXPECT(RoboWeb::PixelRed(halvedFrame, halvedBox->x + 4, halvedBox->y + 4) > 80);
    }
    RoboWeb::Document eaten = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='e'><feMorphology operator='erode' radius='3'/></filter></defs><rect width='8' height='8' fill='red' filter='url(#e)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult eatenLayout = RoboWeb::LayoutDocument(eaten, 80, 40);
    RoboWeb::Framebuffer eatenFrame = RoboWeb::RenderDocument(eatenLayout, 0);
    const RoboWeb::ElementBox* eatenBox = RoboWeb::FindBox(eatenLayout, "svg");
    EXPECT(eatenBox != nullptr);
    if (eatenBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(eatenFrame, eatenBox->x + 1, eatenBox->y + 1) == 255);
        EXPECT(RoboWeb::PixelGreen(eatenFrame, eatenBox->x + 3, eatenBox->y + 3) == 0);
    }
    RoboWeb::Document keptXml = Parse(
        "<!DOCTYPE html><svg width='120' height='30'><text x='0' y='20' xml:space='preserve' fill='red'>M    M</text></svg>", diagnostics);
    bool keptXmlSpaces = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(keptXml, 200, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("M    M") != std::string::npos) {
            keptXmlSpaces = true;
        }
    }
    EXPECT(keptXmlSpaces);
    RoboWeb::Document focused = Parse("<!DOCTYPE html><svg width='40' height='20'><rect id='spot' width='8' height='8' autofocus='autofocus'/></svg>", diagnostics);
    RoboWeb::ApplyAutofocus(focused);
    EXPECT(focused.focus != nullptr && RoboWeb::AttributeValue(*focused.focus, "id") == "spot");
    RoboWeb::SetSvgAnimationTime(2);
    RoboWeb::Document heldMin = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><rect width='8' height='8' fill='red'><animate attributeName='x' from='0' to='20' dur='1s' min='3s'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult heldMinLayout = RoboWeb::LayoutDocument(heldMin, 120, 40);
    RoboWeb::Framebuffer heldMinFrame = RoboWeb::RenderDocument(heldMinLayout, 0);
    const RoboWeb::ElementBox* heldMinBox = RoboWeb::FindBox(heldMinLayout, "svg");
    EXPECT(heldMinBox != nullptr);
    if (heldMinBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(heldMinFrame, heldMinBox->x + 4, heldMinBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(heldMinFrame, heldMinBox->x + 22, heldMinBox->y + 4) == 0);
    }
    RoboWeb::Document cutMax = Parse(
        "<!DOCTYPE html><svg width='50' height='16'><rect width='8' height='8' fill='red'><animate attributeName='x' from='0' to='40' dur='4s' max='1s'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult cutMaxLayout = RoboWeb::LayoutDocument(cutMax, 140, 40);
    RoboWeb::Framebuffer cutMaxFrame = RoboWeb::RenderDocument(cutMaxLayout, 0);
    const RoboWeb::ElementBox* cutMaxBox = RoboWeb::FindBox(cutMaxLayout, "svg");
    EXPECT(cutMaxBox != nullptr);
    if (cutMaxBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(cutMaxFrame, cutMaxBox->x + 4, cutMaxBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(cutMaxFrame, cutMaxBox->x + 22, cutMaxBox->y + 4) == 255);
    }
    RoboWeb::SetSvgAnimationTime(1);
    RoboWeb::Document curved = Parse(
        "<!DOCTYPE html><svg width='50' height='16'><rect width='8' height='8' fill='red'><animate attributeName='x' from='0' to='40' dur='2s' calcMode='spline' keySplines='0 0 1 0'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult curvedLayout = RoboWeb::LayoutDocument(curved, 140, 40);
    RoboWeb::Framebuffer curvedFrame = RoboWeb::RenderDocument(curvedLayout, 0);
    const RoboWeb::ElementBox* curvedBox = RoboWeb::FindBox(curvedLayout, "svg");
    EXPECT(curvedBox != nullptr);
    if (curvedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(curvedFrame, curvedBox->x + 6, curvedBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(curvedFrame, curvedBox->x + 24, curvedBox->y + 4) == 255);
    }
    RoboWeb::Document keyedMotion = Parse(
        "<!DOCTYPE html><svg width='56' height='16'><rect width='8' height='8' fill='red'><animateMotion dur='2s' path='M0 0 H40' keyPoints='0;1' keyTimes='0;0.25'/></animateMotion></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult keyedMotionLayout = RoboWeb::LayoutDocument(keyedMotion, 160, 40);
    RoboWeb::Framebuffer keyedMotionFrame = RoboWeb::RenderDocument(keyedMotionLayout, 0);
    const RoboWeb::ElementBox* keyedMotionBox = RoboWeb::FindBox(keyedMotionLayout, "svg");
    EXPECT(keyedMotionBox != nullptr);
    if (keyedMotionBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(keyedMotionFrame, keyedMotionBox->x + 4, keyedMotionBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(keyedMotionFrame, keyedMotionBox->x + 42, keyedMotionBox->y + 4) == 0);
    }
    RoboWeb::SetSvgAnimationTime(0);
    RoboWeb::Document mirrored = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><text x='4' y='16' direction='rtl' unicode-bidi='bidi-override' fill='red'>AB</text></svg>", diagnostics);
    bool reversedText = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(mirrored, 80, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("BA") != std::string::npos) {
            reversedText = true;
        }
    }
    EXPECT(reversedText);
    RoboWeb::Document tuned = Parse(
        "<!DOCTYPE html><svg width='80' height='40'><text x='4' y='30' font-size='16' font-size-adjust='1' fill='red'>Hi</text></svg>", diagnostics);
    bool enlarged = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(tuned, 160, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("Hi") != std::string::npos && op.fontSizePx > 24) {
            enlarged = true;
        }
    }
    EXPECT(enlarged);
    RoboWeb::Document namedInput = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='mix'><feFlood flood-color='blue' result='ink'/><feBlend in2='other' mode='multiply'/></filter></defs><rect width='16' height='16' fill='red' filter='url(#mix)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult namedInputLayout = RoboWeb::LayoutDocument(namedInput, 80, 40);
    RoboWeb::Framebuffer namedInputFrame = RoboWeb::RenderDocument(namedInputLayout, 0);
    const RoboWeb::ElementBox* namedInputBox = RoboWeb::FindBox(namedInputLayout, "svg");
    EXPECT(namedInputBox != nullptr);
    if (namedInputBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(namedInputFrame, namedInputBox->x + 4, namedInputBox->y + 4) > 200);
    }
    RoboWeb::Document miterSharp = Parse(
        "<!DOCTYPE html><svg width='48' height='40'><polyline points='4,28 20,16 36,28' fill='none' stroke='red' stroke-width='8' stroke-miterlimit='4'/></svg>",
        diagnostics);
    RoboWeb::Document miterCut = Parse(
        "<!DOCTYPE html><svg width='48' height='40'><polyline points='4,28 20,16 36,28' fill='none' stroke='red' stroke-width='8' stroke-miterlimit='1'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult miterSharpLayout = RoboWeb::LayoutDocument(miterSharp, 120, 80);
    RoboWeb::Framebuffer miterSharpFrame = RoboWeb::RenderDocument(miterSharpLayout, 0);
    RoboWeb::Framebuffer miterCutFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(miterCut, 120, 80), 0);
    const RoboWeb::ElementBox* miterBox = RoboWeb::FindBox(miterSharpLayout, "svg");
    EXPECT(miterBox != nullptr);
    if (miterBox != nullptr) {
        // The 73.7° vertex miters to (20,11); with miterlimit 1 the join is
        // beveled and its cut edge sits at y=12.8, leaving row 11 empty.
        // The miter tip covers only part of that pixel, so it blends instead of
        // landing fully on it.
        EXPECT(RoboWeb::PixelGreen(miterSharpFrame, miterBox->x + 19, miterBox->y + 11) < 140);
        EXPECT(RoboWeb::PixelGreen(miterCutFrame, miterBox->x + 19, miterBox->y + 11) == 255);
        EXPECT(RoboWeb::PixelGreen(miterSharpFrame, miterBox->x + 20, miterBox->y + 10) == 255);
    }
    RoboWeb::Document stitched = Parse(
        "<!DOCTYPE html><svg width='16' height='8'><defs><filter id='n'><feTurbulence baseFrequency='1' seed='3' stitchTiles='stitch'/></filter></defs><rect width='16' height='8' filter='url(#n)'/></svg>",
        diagnostics);
    RoboWeb::Document unstitched = Parse(
        "<!DOCTYPE html><svg width='16' height='8'><defs><filter id='n'><feTurbulence baseFrequency='1' seed='3'/></filter></defs><rect width='16' height='8' filter='url(#n)'/></svg>",
        diagnostics);
    RoboWeb::Framebuffer stitchedFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(stitched, 80, 40), 0);
    RoboWeb::Framebuffer unstitchedFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(unstitched, 80, 40), 0);
    const RoboWeb::ElementBox* stitchBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(stitched, 80, 40), "svg");
    EXPECT(stitchBox != nullptr);
    if (stitchBox != nullptr) {
        const int left = RoboWeb::PixelRed(stitchedFrame, stitchBox->x, stitchBox->y + 2) + RoboWeb::PixelGreen(stitchedFrame, stitchBox->x, stitchBox->y + 2);
        const int right = RoboWeb::PixelRed(stitchedFrame, stitchBox->x + 8, stitchBox->y + 2) + RoboWeb::PixelGreen(stitchedFrame, stitchBox->x + 8, stitchBox->y + 2);
        const int openLeft = RoboWeb::PixelRed(unstitchedFrame, stitchBox->x, stitchBox->y + 2) + RoboWeb::PixelGreen(unstitchedFrame, stitchBox->x, stitchBox->y + 2);
        const int openRight = RoboWeb::PixelRed(unstitchedFrame, stitchBox->x + 8, stitchBox->y + 2) + RoboWeb::PixelGreen(unstitchedFrame, stitchBox->x + 8, stitchBox->y + 2);
        EXPECT(left == right);
        EXPECT(openLeft != openRight);
    }
    RoboWeb::Document capsText = Parse(
        "<!DOCTYPE html><svg width='80' height='40'><text x='4' y='30' font-size='20' font-variant='small-caps' fill='red'>Ab</text></svg>", diagnostics);
    bool fullCap = false;
    bool smallCap = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(capsText, 160, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "A" && op.fontSizePx >= 18) {
            fullCap = true;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "B" && op.fontSizePx <= 16) {
            smallCap = true;
        }
    }
    EXPECT(fullCap && smallCap);
    int uprightI = 0;
    int uprightW = 0;
    int sidewaysI = 0;
    int sidewaysW = 0;
    RoboWeb::Document uprightGlyph = Parse(
        "<!DOCTYPE html><svg width='40' height='80'><text x='4' y='16' font-size='16' writing-mode='tb' glyph-orientation-vertical='0' fill='red'>iW</text></svg>",
        diagnostics);
    RoboWeb::Document sidewaysGlyph = Parse(
        "<!DOCTYPE html><svg width='40' height='80'><text x='4' y='16' font-size='16' writing-mode='tb' fill='red'>iW</text></svg>", diagnostics);
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(uprightGlyph, 80, 120).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "i") {
            uprightI = op.y;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "W") {
            uprightW = op.y;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(sidewaysGlyph, 80, 120).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "i") {
            sidewaysI = op.y;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "W") {
            sidewaysW = op.y;
        }
    }
    EXPECT(uprightW - uprightI > sidewaysW - sidewaysI + 4);
    RoboWeb::Document inkOffset = Parse(
        "<!DOCTYPE html><svg width='32' height='16'><defs><filter id='shift' filterUnits='userSpaceOnUse'><feFlood flood-color='#0000ff' result='ink'/><feOffset in='ink' dx='12' dy='0'/></filter></defs><rect width='8' height='8' fill='red' filter='url(#shift)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult inkOffsetLayout = RoboWeb::LayoutDocument(inkOffset, 80, 40);
    RoboWeb::Framebuffer inkOffsetFrame = RoboWeb::RenderDocument(inkOffsetLayout, 0);
    const RoboWeb::ElementBox* inkOffsetBox = RoboWeb::FindBox(inkOffsetLayout, "svg");
    EXPECT(inkOffsetBox != nullptr);
    if (inkOffsetBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(inkOffsetFrame, inkOffsetBox->x + 2, inkOffsetBox->y + 2) == 255);
        EXPECT(RoboWeb::PixelBlue(inkOffsetFrame, inkOffsetBox->x + 14, inkOffsetBox->y + 2) == 255);
    }
    RoboWeb::Document linearShade = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><linearGradient id='g' color-interpolation='linearRGB'><stop offset='0' stop-color='#000000'/><stop offset='1' stop-color='#ffffff'/></linearGradient></defs><rect width='40' height='16' fill='url(#g)'/></svg>",
        diagnostics);
    RoboWeb::Document encodedShade = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><defs><linearGradient id='g'><stop offset='0' stop-color='#000000'/><stop offset='1' stop-color='#ffffff'/></linearGradient></defs><rect width='40' height='16' fill='url(#g)'/></svg>",
        diagnostics);
    RoboWeb::Framebuffer linearShadeFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(linearShade, 120, 40), 0);
    RoboWeb::Framebuffer encodedShadeFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(encodedShade, 120, 40), 0);
    const RoboWeb::ElementBox* shadeBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(linearShade, 120, 40), "svg");
    EXPECT(shadeBox != nullptr);
    if (shadeBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(linearShadeFrame, shadeBox->x + 20, shadeBox->y + 4) > 160);
        EXPECT(RoboWeb::PixelRed(encodedShadeFrame, shadeBox->x + 20, shadeBox->y + 4) < 150);
    }
    RoboWeb::Document linearFilter = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='f' color-interpolation-filters='linearRGB'><feComponentTransfer><feFuncR type='linear' slope='0.5'/><feFuncG type='linear' slope='0.5'/><feFuncB type='linear' slope='0.5'/></feComponentTransfer></filter></defs><rect width='16' height='16' fill='#ffffff' filter='url(#f)'/></svg>",
        diagnostics);
    RoboWeb::Document encodedFilter = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='f'><feComponentTransfer><feFuncR type='linear' slope='0.5'/><feFuncG type='linear' slope='0.5'/><feFuncB type='linear' slope='0.5'/></feComponentTransfer></filter></defs><rect width='16' height='16' fill='#ffffff' filter='url(#f)'/></svg>",
        diagnostics);
    RoboWeb::Framebuffer linearFilterFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(linearFilter, 80, 40), 0);
    RoboWeb::Framebuffer encodedFilterFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(encodedFilter, 80, 40), 0);
    const RoboWeb::ElementBox* filterBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(linearFilter, 80, 40), "svg");
    EXPECT(filterBox != nullptr);
    if (filterBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(linearFilterFrame, filterBox->x + 4, filterBox->y + 4) > 160);
        EXPECT(RoboWeb::PixelRed(encodedFilterFrame, filterBox->x + 4, filterBox->y + 4) < 150);
    }
    RoboWeb::Document fadedAlpha = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='k'><feConvolveMatrix kernelMatrix='1' divisor='2' preserveAlpha='false'/></filter></defs><rect width='16' height='16' fill='red' filter='url(#k)'/></svg>",
        diagnostics);
    RoboWeb::Framebuffer fadedAlphaFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(fadedAlpha, 80, 40), 0);
    const RoboWeb::ElementBox* fadedAlphaBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(fadedAlpha, 80, 40), "svg");
    EXPECT(fadedAlphaBox != nullptr);
    if (fadedAlphaBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(fadedAlphaFrame, fadedAlphaBox->x + 4, fadedAlphaBox->y + 4) > 80);
    }
    int condensedX = 1000;
    int expandedX = 0;
    RoboWeb::Document condensedFace = Parse("<!DOCTYPE html><svg width='80' height='30'><text x='0' y='20' font-stretch='condensed' fill='red'>MW</text></svg>", diagnostics);
    RoboWeb::Document expandedFace = Parse("<!DOCTYPE html><svg width='120' height='30'><text x='0' y='20' font-width='ultra-expanded' fill='red'>MW</text></svg>", diagnostics);
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(condensedFace, 160, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "W") {
            condensedX = op.x;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(expandedFace, 200, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "W") {
            expandedX = op.x;
        }
    }
    EXPECT(expandedX > condensedX + 4);
    int flatA = 0;
    int flatB = 0;
    RoboWeb::Document turnedFlat = Parse("<!DOCTYPE html><svg width='40' height='60'><text x='4' y='16' glyph-orientation-horizontal='90' fill='red'>AB</text></svg>", diagnostics);
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(turnedFlat, 80, 100).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "A") {
            flatA = op.y;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "B") {
            flatB = op.y;
        }
    }
    EXPECT(flatB > flatA + 8);
    RoboWeb::SetSvgAnimationTime(1);
    RoboWeb::Document linearMove = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><rect width='16' height='16' fill='#000000'><animate attributeName='fill' from='#000000' to='#ffffff' dur='2s' color-interpolation='linearRGB'/></rect></svg>",
        diagnostics);
    RoboWeb::Framebuffer linearMoveFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(linearMove, 80, 40), 0);
    const RoboWeb::ElementBox* linearMoveBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(linearMove, 80, 40), "svg");
    EXPECT(linearMoveBox != nullptr);
    if (linearMoveBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(linearMoveFrame, linearMoveBox->x + 4, linearMoveBox->y + 4) > 160);
    }
    RoboWeb::SetSvgAnimationTime(0);
    RoboWeb::Document bareEdge = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='k'><feConvolveMatrix kernelMatrix='1 1 1 1 1 1 1 1 1' order='3' edgeMode='none' divisor='9'/></filter></defs><rect width='16' height='16' fill='red' filter='url(#k)'/></svg>",
        diagnostics);
    RoboWeb::Document coveredEdge = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='k'><feConvolveMatrix kernelMatrix='1 1 1 1 1 1 1 1 1' order='3' edgeMode='duplicate' divisor='9'/></filter></defs><rect width='16' height='16' fill='red' filter='url(#k)'/></svg>",
        diagnostics);
    RoboWeb::Framebuffer bareEdgeFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(bareEdge, 80, 40), 0);
    RoboWeb::Framebuffer coveredEdgeFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(coveredEdge, 80, 40), 0);
    const RoboWeb::ElementBox* bareEdgeBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(bareEdge, 80, 40), "svg");
    EXPECT(bareEdgeBox != nullptr);
    if (bareEdgeBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(bareEdgeFrame, bareEdgeBox->x + 8, bareEdgeBox->y + 8) > RoboWeb::PixelRed(bareEdgeFrame, bareEdgeBox->x, bareEdgeBox->y + 8) + 40);
        EXPECT(RoboWeb::PixelRed(coveredEdgeFrame, bareEdgeBox->x, bareEdgeBox->y + 8) > 240);
    }
    RoboWeb::Document narrowKernel = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='k'><feConvolveMatrix kernelMatrix='1' order='1' edgeMode='none'/></filter></defs><rect width='16' height='16' fill='red' filter='url(#k)'/></svg>",
        diagnostics);
    RoboWeb::Framebuffer narrowKernelFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(narrowKernel, 80, 40), 0);
    const RoboWeb::ElementBox* narrowKernelBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(narrowKernel, 80, 40), "svg");
    EXPECT(narrowKernelBox != nullptr);
    if (narrowKernelBox != nullptr && bareEdgeBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(narrowKernelFrame, narrowKernelBox->x, narrowKernelBox->y + 8) > RoboWeb::PixelRed(bareEdgeFrame, bareEdgeBox->x, bareEdgeBox->y + 8) + 40);
    }
    RoboWeb::Document spacedKernel = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='k'><feConvolveMatrix kernelMatrix='1 1 1 1 1 1 1 1 1' order='3' kernelUnitLength='4' edgeMode='none' divisor='9'/></filter></defs><rect width='16' height='16' fill='red' filter='url(#k)'/></svg>",
        diagnostics);
    RoboWeb::Framebuffer spacedKernelFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(spacedKernel, 80, 40), 0);
    const RoboWeb::ElementBox* spacedKernelBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(spacedKernel, 80, 40), "svg");
    EXPECT(spacedKernelBox != nullptr);
    if (spacedKernelBox != nullptr && bareEdgeBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(bareEdgeFrame, bareEdgeBox->x + 2, bareEdgeBox->y + 8) > RoboWeb::PixelRed(spacedKernelFrame, spacedKernelBox->x + 2, spacedKernelBox->y + 8) + 40);
    }
    RoboWeb::Document shiftedKernel = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='k'><feConvolveMatrix kernelMatrix='1 1 1 1 1 1 1 1 1' order='3' targetX='0' edgeMode='none' divisor='9'/></filter></defs><rect width='16' height='16' fill='red' filter='url(#k)'/></svg>",
        diagnostics);
    RoboWeb::Framebuffer shiftedKernelFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(shiftedKernel, 80, 40), 0);
    const RoboWeb::ElementBox* shiftedKernelBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(shiftedKernel, 80, 40), "svg");
    EXPECT(shiftedKernelBox != nullptr);
    if (shiftedKernelBox != nullptr && bareEdgeBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(shiftedKernelFrame, shiftedKernelBox->x, shiftedKernelBox->y + 8) > RoboWeb::PixelRed(bareEdgeFrame, bareEdgeBox->x, bareEdgeBox->y + 8) + 40);
    }
    RoboWeb::Document loweredKernel = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><defs><filter id='k'><feConvolveMatrix kernelMatrix='1 1 1 1 1 1 1 1 1' order='3' targetY='0' edgeMode='none' divisor='9'/></filter></defs><rect width='16' height='16' fill='red' filter='url(#k)'/></svg>",
        diagnostics);
    RoboWeb::Framebuffer loweredKernelFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(loweredKernel, 80, 40), 0);
    const RoboWeb::ElementBox* loweredKernelBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(loweredKernel, 80, 40), "svg");
    EXPECT(loweredKernelBox != nullptr);
    if (loweredKernelBox != nullptr && bareEdgeBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(loweredKernelFrame, loweredKernelBox->x + 8, loweredKernelBox->y) > RoboWeb::PixelRed(bareEdgeFrame, bareEdgeBox->x + 8, bareEdgeBox->y) + 40);
    }
    RoboWeb::SetSvgAnimationTime(1);
    RoboWeb::Document cssSlide = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><rect width='8' height='8' fill='red'><animate attributeType='CSS' attributeName='x' from='0' to='20' dur='2s'/></rect></svg>",
        diagnostics);
    RoboWeb::Document xmlSlide = Parse(
        "<!DOCTYPE html><svg width='40' height='16'><rect width='8' height='8' fill='red'><animate attributeType='XML' attributeName='x' from='0' to='20' dur='2s'/></rect></svg>",
        diagnostics);
    RoboWeb::Framebuffer cssSlideFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(cssSlide, 120, 40), 0);
    RoboWeb::Framebuffer xmlSlideFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(xmlSlide, 120, 40), 0);
    const RoboWeb::ElementBox* cssSlideBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(cssSlide, 120, 40), "svg");
    EXPECT(cssSlideBox != nullptr);
    if (cssSlideBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(cssSlideFrame, cssSlideBox->x + 4, cssSlideBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelGreen(xmlSlideFrame, cssSlideBox->x + 4, cssSlideBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(xmlSlideFrame, cssSlideBox->x + 12, cssSlideBox->y + 4) == 0);
    }
    RoboWeb::SetSvgAnimationTime(0);
    RoboWeb::Document eastLight = Parse(
        "<!DOCTYPE html><svg width='16' height='8'><defs><filter id='lamp'><feDiffuseLighting lighting-color='#00ff00' surfaceScale='8'><feDistantLight azimuth='0' elevation='0'/></feDiffuseLighting></filter></defs><rect width='16' height='8' fill='red' filter='url(#lamp)'/></svg>",
        diagnostics);
    RoboWeb::Document westLight = Parse(
        "<!DOCTYPE html><svg width='16' height='8'><defs><filter id='lamp'><feDiffuseLighting lighting-color='#00ff00' surfaceScale='8'><feDistantLight azimuth='180' elevation='0'/></feDiffuseLighting></filter></defs><rect width='16' height='8' fill='red' filter='url(#lamp)'/></svg>",
        diagnostics);
    RoboWeb::Document flatLight = Parse(
        "<!DOCTYPE html><svg width='16' height='8'><defs><filter id='lamp'><feDiffuseLighting lighting-color='#00ff00' surfaceScale='0'><feDistantLight azimuth='0' elevation='0'/></feDiffuseLighting></filter></defs><rect width='16' height='8' fill='red' filter='url(#lamp)'/></svg>",
        diagnostics);
    RoboWeb::Framebuffer eastFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(eastLight, 80, 40), 0);
    RoboWeb::Framebuffer westFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(westLight, 80, 40), 0);
    RoboWeb::Framebuffer flatFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(flatLight, 80, 40), 0);
    const RoboWeb::ElementBox* eastBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(eastLight, 80, 40), "svg");
    EXPECT(eastBox != nullptr);
    if (eastBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(eastFrame, eastBox->x + 15, eastBox->y + 4) > 80);
        EXPECT(RoboWeb::PixelGreen(eastFrame, eastBox->x, eastBox->y + 4) < 40);
        EXPECT(RoboWeb::PixelGreen(westFrame, eastBox->x, eastBox->y + 4) > 80);
        EXPECT(RoboWeb::PixelGreen(westFrame, eastBox->x + 15, eastBox->y + 4) < 40);
        EXPECT(RoboWeb::PixelGreen(flatFrame, eastBox->x + 15, eastBox->y + 4) < 40);
    }
    RoboWeb::Document stretchedPath = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><path id='road' d='M0 20 H60'/><text fill='red'><textPath href='#road' method='stretch'>AB</textPath></text></svg>",
        diagnostics);
    RoboWeb::Document alignedPath = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><path id='road' d='M0 20 H60'/><text fill='red'><textPath href='#road' method='align'>AB</textPath></text></svg>",
        diagnostics);
    int stretchedB = 0;
    int alignedB = 0;
    const RoboWeb::ElementBox* stretchedPathBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(stretchedPath, 160, 80), "svg");
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(stretchedPath, 160, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "B") {
            stretchedB = op.x;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(alignedPath, 160, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "B") {
            alignedB = op.x;
        }
    }
    EXPECT(stretchedPathBox != nullptr && stretchedB > stretchedPathBox->x + 40 && alignedB < stretchedPathBox->x + 30);
    RoboWeb::Document printedStyle = Parse("<!DOCTYPE html><style media='print'>div { background: #ff0000 }</style><div style='width:40px;height:16px'> </div>", diagnostics);
    RoboWeb::Document screenStyle = Parse("<!DOCTYPE html><style media='screen'>div { background: #ff0000 }</style><div style='width:40px;height:16px'> </div>", diagnostics);
    RoboWeb::LayoutResult printedLayout = RoboWeb::LayoutDocument(printedStyle, 160, 80);
    RoboWeb::Framebuffer printedFrame = RoboWeb::RenderDocument(printedLayout, 0);
    RoboWeb::Framebuffer screenFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(screenStyle, 160, 80), 0);
    const RoboWeb::ElementBox* printedBox = RoboWeb::FindBox(printedLayout, "div");
    const RoboWeb::ElementBox* screenBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(screenStyle, 160, 80), "div");
    EXPECT(printedBox != nullptr && screenBox != nullptr);
    if (printedBox != nullptr && screenBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(printedFrame, printedBox->x + 8, printedBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(screenFrame, screenBox->x + 8, screenBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelRed(screenFrame, screenBox->x + 8, screenBox->y + 4) == 255);
    }
    RoboWeb::Document shownLink = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><a xlink:href='page.html' xlink:show='new'><rect width='20' height='20' fill='red'/></a></svg>",
        diagnostics);
    RoboWeb::LayoutResult shownLinkLayout = RoboWeb::LayoutDocument(shownLink, 160, 80);
    const RoboWeb::ElementBox* shownLinkBox = RoboWeb::FindBox(shownLinkLayout, "svg");
    EXPECT(shownLinkBox != nullptr);
    if (shownLinkBox != nullptr) {
        EXPECT(RoboWeb::DispatchClick(shownLink, shownLinkLayout, shownLinkBox->x + 4, shownLinkBox->y + 4));
        EXPECT(shownLink.status.find("in new") != std::string::npos);
    }
    RoboWeb::Document classFill = Parse(
        "<!DOCTYPE html><style>.hot { color: #00ff00 }</style><svg width='16' height='16'><rect class='hot' width='16' height='16'/></svg>", diagnostics);
    RoboWeb::LayoutResult classFillLayout = RoboWeb::LayoutDocument(classFill, 80, 40);
    RoboWeb::Framebuffer classFillFrame = RoboWeb::RenderDocument(classFillLayout, 0);
    const RoboWeb::ElementBox* classFillBox = RoboWeb::FindBox(classFillLayout, "svg");
    EXPECT(classFillBox != nullptr);
    if (classFillBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(classFillFrame, classFillBox->x + 4, classFillBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelRed(classFillFrame, classFillBox->x + 4, classFillBox->y + 4) == 0);
    }
    RoboWeb::Document idFill = Parse(
        "<!DOCTYPE html><style>#spot { color: #00ff00 }</style><svg width='16' height='16'><rect id='spot' width='16' height='16'/></svg>", diagnostics);
    RoboWeb::LayoutResult idFillLayout = RoboWeb::LayoutDocument(idFill, 80, 40);
    RoboWeb::Framebuffer idFillFrame = RoboWeb::RenderDocument(idFillLayout, 0);
    const RoboWeb::ElementBox* idFillBox = RoboWeb::FindBox(idFillLayout, "svg");
    EXPECT(idFillBox != nullptr);
    if (idFillBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(idFillFrame, idFillBox->x + 4, idFillBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelRed(idFillFrame, idFillBox->x + 4, idFillBox->y + 4) == 0);
    }
    const std::string ppm = "P6\n2 1\n255\n";
    std::vector<unsigned char> pixels(ppm.begin(), ppm.end());
    pixels.push_back(255);
    pixels.push_back(0);
    pixels.push_back(0);
    pixels.push_back(0);
    pixels.push_back(0);
    pixels.push_back(255);
    RoboWeb::RegisterCompiledImage("split.ppm", pixels);
    RoboWeb::Document crispImage = Parse(
        "<!DOCTYPE html><svg width='8' height='4'><image href='split.ppm' width='4' height='4' image-rendering='optimizeSpeed'/></svg>", diagnostics);
    RoboWeb::Document smoothImage = Parse(
        "<!DOCTYPE html><svg width='8' height='4'><image href='split.ppm' width='4' height='4' image-rendering='optimizeQuality'/></svg>", diagnostics);
    RoboWeb::Framebuffer crispFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(crispImage, 40, 20), 0);
    RoboWeb::Framebuffer smoothFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(smoothImage, 40, 20), 0);
    const RoboWeb::ElementBox* crispBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(crispImage, 40, 20), "svg");
    EXPECT(crispBox != nullptr);
    if (crispBox != nullptr) {
        EXPECT(RoboWeb::PixelBlue(crispFrame, crispBox->x + 1, crispBox->y + 1) == 0);
        EXPECT(RoboWeb::PixelBlue(smoothFrame, crispBox->x + 1, crispBox->y + 1) > 30);
    }
    RoboWeb::Document clippedWords = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><text x='0' y='20' font-size='16' inline-size='20' text-overflow='ellipsis' fill='red'>MMMMMM</text></svg>",
        diagnostics);
    RoboWeb::Document fullWords = Parse(
        "<!DOCTYPE html><svg width='80' height='30'><text x='0' y='20' font-size='16' inline-size='20' text-overflow='clip' fill='red'>MMMMMM</text></svg>",
        diagnostics);
    bool ellipsis = false;
    bool fullWord = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(clippedWords, 160, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("...") != std::string::npos) {
            ellipsis = true;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(fullWords, 160, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text.find("MMMMMM") != std::string::npos) {
            fullWord = true;
        }
    }
    EXPECT(ellipsis && fullWord);
    RoboWeb::Document titled = Parse(
        "<!DOCTYPE html><svg width='20' height='20'><a href='page.html' xlink:title='Help'><rect width='20' height='20' fill='red'/></a></svg>", diagnostics);
    RoboWeb::LayoutResult titledLayout = RoboWeb::LayoutDocument(titled, 80, 40);
    const RoboWeb::ElementBox* titledBox = RoboWeb::FindBox(titledLayout, "svg");
    EXPECT(titledBox != nullptr);
    if (titledBox != nullptr) {
        EXPECT(RoboWeb::DispatchHover(titled, titledLayout, titledBox->x + 4, titledBox->y + 4));
        EXPECT(titled.status == "Help");
    }
    RoboWeb::Document matrixFill = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><filter id='swap'><feColorMatrix type='matrix' values='0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 1 0'/></filter><rect width='16' height='16' fill='#ff0000' filter='url(#swap)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult matrixLayout = RoboWeb::LayoutDocument(matrixFill, 80, 40);
    RoboWeb::Framebuffer matrixFrame = RoboWeb::RenderDocument(matrixLayout, 0);
    const RoboWeb::ElementBox* matrixBox = RoboWeb::FindBox(matrixLayout, "svg");
    EXPECT(matrixBox != nullptr);
    if (matrixBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(matrixFrame, matrixBox->x + 4, matrixBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelRed(matrixFrame, matrixBox->x + 4, matrixBox->y + 4) == 0);
    }
    RoboWeb::Document hueFill = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><filter id='hue'><feColorMatrix type='hueRotate' values='180'/></filter><rect width='16' height='16' fill='#ff0000' filter='url(#hue)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult hueLayout = RoboWeb::LayoutDocument(hueFill, 80, 40);
    RoboWeb::Framebuffer hueFrame = RoboWeb::RenderDocument(hueLayout, 0);
    const RoboWeb::ElementBox* hueBox = RoboWeb::FindBox(hueLayout, "svg");
    EXPECT(hueBox != nullptr);
    if (hueBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(hueFrame, hueBox->x + 4, hueBox->y + 4) < 80);
        EXPECT(RoboWeb::PixelGreen(hueFrame, hueBox->x + 4, hueBox->y + 4) > 80);
    }
    RoboWeb::Document lumaFill = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><filter id='luma'><feColorMatrix type='luminanceToAlpha'/></filter><rect width='16' height='16' fill='#ff0000' filter='url(#luma)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult lumaLayout = RoboWeb::LayoutDocument(lumaFill, 80, 40);
    RoboWeb::Framebuffer lumaFrame = RoboWeb::RenderDocument(lumaLayout, 0);
    const RoboWeb::ElementBox* lumaBox = RoboWeb::FindBox(lumaLayout, "svg");
    EXPECT(lumaBox != nullptr);
    if (lumaBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(lumaFrame, lumaBox->x + 4, lumaBox->y + 4) > 150);
    }
    RoboWeb::Document fractalNoise = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><filter id='grain'><feTurbulence type='fractalNoise' baseFrequency='1' seed='2'/></filter><rect width='16' height='16' filter='url(#grain)'/></svg>",
        diagnostics);
    RoboWeb::Document turbulenceNoise = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><filter id='grain'><feTurbulence type='turbulence' baseFrequency='1' seed='2'/></filter><rect width='16' height='16' filter='url(#grain)'/></svg>",
        diagnostics);
    RoboWeb::Framebuffer fractalFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(fractalNoise, 40, 40), 0);
    RoboWeb::Framebuffer turbulenceFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(turbulenceNoise, 40, 40), 0);
    const RoboWeb::ElementBox* fractalBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(fractalNoise, 40, 40), "svg");
    EXPECT(fractalBox != nullptr);
    if (fractalBox != nullptr) {
        bool noiseDiffers = false;
        for (int row = 0; row < 8 && !noiseDiffers; ++row) {
            for (int column = 0; column < 8; ++column) {
                if (RoboWeb::PixelRed(fractalFrame, fractalBox->x + column, fractalBox->y + row) !=
                    RoboWeb::PixelRed(turbulenceFrame, fractalBox->x + column, fractalBox->y + row)) {
                    noiseDiffers = true;
                    break;
                }
            }
        }
        EXPECT(noiseDiffers);
    }
    RoboWeb::Document plainStyle = Parse(
        "<!DOCTYPE html><style type='text/plain'>div { background: #ff0000 }</style><div style='width:40px;height:16px'> </div>", diagnostics);
    RoboWeb::Document cssStyle = Parse(
        "<!DOCTYPE html><style type='text/css'>div { background: #ff0000 }</style><div style='width:40px;height:16px'> </div>", diagnostics);
    RoboWeb::Framebuffer plainFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(plainStyle, 80, 40), 0);
    RoboWeb::Framebuffer cssFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(cssStyle, 80, 40), 0);
    const RoboWeb::ElementBox* plainBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(plainStyle, 80, 40), "div");
    EXPECT(plainBox != nullptr);
    if (plainBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(plainFrame, plainBox->x + 8, plainBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelRed(cssFrame, plainBox->x + 8, plainBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(cssFrame, plainBox->x + 8, plainBox->y + 4) == 0);
    }
    const char* doubled = "2,2 14,2 14,14 2,14 2,2 14,2 14,14 2,14";
    RoboWeb::Document evenClip = Parse(
        std::string("<!DOCTYPE html><svg width='16' height='16'><clipPath id='hole'><polygon clip-rule='evenodd' points='") + doubled +
            "'/></clipPath><rect width='16' height='16' fill='#ff0000' clip-path='url(#hole)'/></svg>",
        diagnostics);
    RoboWeb::Document nonzeroClip = Parse(
        std::string("<!DOCTYPE html><svg width='16' height='16'><clipPath id='hole'><polygon clip-rule='nonzero' points='") + doubled +
            "'/></clipPath><rect width='16' height='16' fill='#ff0000' clip-path='url(#hole)'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult evenClipLayout = RoboWeb::LayoutDocument(evenClip, 80, 40);
    RoboWeb::Framebuffer evenClipFrame = RoboWeb::RenderDocument(evenClipLayout, 0);
    RoboWeb::Framebuffer nonzeroClipFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(nonzeroClip, 80, 40), 0);
    const RoboWeb::ElementBox* evenClipBox = RoboWeb::FindBox(evenClipLayout, "svg");
    EXPECT(evenClipBox != nullptr);
    if (evenClipBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(evenClipFrame, evenClipBox->x + 8, evenClipBox->y + 8) == 255);
        EXPECT(RoboWeb::PixelGreen(nonzeroClipFrame, evenClipBox->x + 8, evenClipBox->y + 8) == 0);
        EXPECT(RoboWeb::PixelRed(nonzeroClipFrame, evenClipBox->x + 8, evenClipBox->y + 8) == 255);
    }
    RoboWeb::Document coverageShape = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><rect x='0.5' y='2' width='3' height='8' fill='#ff0000' shape-rendering='geometricPrecision'/></svg>",
        diagnostics);
    RoboWeb::Document snappedShape = Parse(
        "<!DOCTYPE html><svg width='16' height='16'><rect x='0.5' y='2' width='3' height='8' fill='#ff0000' shape-rendering='crispEdges'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult coverageLayout = RoboWeb::LayoutDocument(coverageShape, 80, 40);
    RoboWeb::Framebuffer coverageFrame = RoboWeb::RenderDocument(coverageLayout, 0);
    RoboWeb::Framebuffer snappedFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(snappedShape, 80, 40), 0);
    const RoboWeb::ElementBox* coverageBox = RoboWeb::FindBox(coverageLayout, "svg");
    EXPECT(coverageBox != nullptr);
    if (coverageBox != nullptr) {
        const int edgeGreen = RoboWeb::PixelGreen(coverageFrame, coverageBox->x, coverageBox->y + 4);
        EXPECT(edgeGreen > 40 && edgeGreen < 220);
        EXPECT(RoboWeb::PixelGreen(snappedFrame, coverageBox->x, coverageBox->y + 4) == 255);
    }
    RoboWeb::Document hintedGlyph = Parse(
        "<!DOCTYPE html><svg width='40' height='24'><text x='0.6' y='16' text-rendering='optimizeLegibility' fill='black'>A</text></svg>",
        diagnostics);
    RoboWeb::Document placedGlyph = Parse(
        "<!DOCTYPE html><svg width='40' height='24'><text x='0.6' y='16' text-rendering='geometricPrecision' fill='black'>A</text></svg>",
        diagnostics);
    int hintedX = -1;
    int placedX = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(hintedGlyph, 80, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "A") {
            hintedX = op.x;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(placedGlyph, 80, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "A") {
            placedX = op.x;
        }
    }
    EXPECT(hintedX == placedX + 1);
    RoboWeb::Document lockedPan = Parse(
        "<!DOCTYPE html><svg width='20' height='400' zoomAndPan='disable'><rect width='20' height='400' fill='#ff0000'/></svg>", diagnostics);
    RoboWeb::Document freePan = Parse(
        "<!DOCTYPE html><svg width='20' height='400' zoomAndPan='magnify'><rect width='20' height='400' fill='#ff0000'/></svg>", diagnostics);
    RoboWeb::LayoutResult lockedLayout = RoboWeb::LayoutDocument(lockedPan, 80, 40);
    RoboWeb::LayoutResult freeLayout = RoboWeb::LayoutDocument(freePan, 80, 40);
    RoboWeb::DispatchScroll(lockedPan, 25, 40, lockedLayout.contentHeight);
    RoboWeb::DispatchScroll(freePan, 25, 40, freeLayout.contentHeight);
    EXPECT(lockedPan.scrollY == 0);
    EXPECT(freePan.scrollY > 0);
    RoboWeb::SetSvgAnimationTime(2.5);
    RoboWeb::Document alwaysRestart = Parse(
        "<!DOCTYPE html><svg width='48' height='16'><rect width='8' height='8' fill='#ff0000'><animate attributeName='x' from='0' to='40' begin='0s; 2s' dur='1s' restart='always'/></rect></svg>",
        diagnostics);
    RoboWeb::Document neverRestart = Parse(
        "<!DOCTYPE html><svg width='48' height='16'><rect width='8' height='8' fill='#ff0000'><animate attributeName='x' from='0' to='40' begin='0s; 2s' dur='1s' restart='never'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult alwaysRestartLayout = RoboWeb::LayoutDocument(alwaysRestart, 80, 40);
    RoboWeb::Framebuffer alwaysRestartFrame = RoboWeb::RenderDocument(alwaysRestartLayout, 0);
    RoboWeb::Framebuffer neverRestartFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(neverRestart, 80, 40), 0);
    const RoboWeb::ElementBox* alwaysRestartBox = RoboWeb::FindBox(alwaysRestartLayout, "svg");
    EXPECT(alwaysRestartBox != nullptr);
    if (alwaysRestartBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(alwaysRestartFrame, alwaysRestartBox->x + 4, alwaysRestartBox->y + 2) == 255);
        EXPECT(RoboWeb::PixelGreen(alwaysRestartFrame, alwaysRestartBox->x + 22, alwaysRestartBox->y + 2) == 0);
        EXPECT(RoboWeb::PixelGreen(neverRestartFrame, alwaysRestartBox->x + 4, alwaysRestartBox->y + 2) == 0);
    }
    RoboWeb::SetSvgAnimationTime(1.5);
    RoboWeb::Document heldRestart = Parse(
        "<!DOCTYPE html><svg width='48' height='16'><rect width='8' height='8' fill='#ff0000'><animate attributeName='x' from='0' to='40' begin='0s; 1s' dur='2s' restart='whenNotActive'/></rect></svg>",
        diagnostics);
    RoboWeb::LayoutResult heldRestartLayout = RoboWeb::LayoutDocument(heldRestart, 80, 40);
    RoboWeb::Framebuffer heldRestartFrame = RoboWeb::RenderDocument(heldRestartLayout, 0);
    const RoboWeb::ElementBox* heldRestartBox = RoboWeb::FindBox(heldRestartLayout, "svg");
    EXPECT(heldRestartBox != nullptr);
    if (heldRestartBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(heldRestartFrame, heldRestartBox->x + 12, heldRestartBox->y + 2) == 255);
        EXPECT(RoboWeb::PixelGreen(heldRestartFrame, heldRestartBox->x + 32, heldRestartBox->y + 2) == 0);
    }
    RoboWeb::SetSvgAnimationTime(0);
    RoboWeb::Document frenchMark = Parse(
        "<!DOCTYPE html><style>rect:lang(fr) { color: #0000ff } rect:lang(en) { color: #00ff00 }</style><svg width='16' height='16'><rect width='16' height='16' lang='fr' xml:lang='en'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult frenchMarkLayout = RoboWeb::LayoutDocument(frenchMark, 80, 40);
    RoboWeb::Framebuffer frenchMarkFrame = RoboWeb::RenderDocument(frenchMarkLayout, 0);
    const RoboWeb::ElementBox* frenchMarkBox = RoboWeb::FindBox(frenchMarkLayout, "svg");
    EXPECT(frenchMarkBox != nullptr);
    if (frenchMarkBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(frenchMarkFrame, frenchMarkBox->x + 4, frenchMarkBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelBlue(frenchMarkFrame, frenchMarkBox->x + 4, frenchMarkBox->y + 4) == 0);
    }
    RoboWeb::Document germanBlock = Parse(
        "<!DOCTYPE html><style>div:lang(de) { background: #ff0000 }</style><div lang='de-AT' style='width:40px;height:16px'> </div>", diagnostics);
    RoboWeb::Framebuffer germanFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(germanBlock, 80, 40), 0);
    const RoboWeb::ElementBox* germanBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(germanBlock, 80, 40), "div");
    EXPECT(germanBox != nullptr);
    if (germanBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(germanFrame, germanBox->x + 8, germanBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(germanFrame, germanBox->x + 8, germanBox->y + 4) == 0);
    }
    RoboWeb::Document extendedLink = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><a href='page.html' xlink:type='extended'><rect width='20' height='20' fill='red'/></a></svg>",
        diagnostics);
    RoboWeb::Document simpleLink = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><a href='page.html' xlink:type='simple'><rect width='20' height='20' fill='red'/></a></svg>",
        diagnostics);
    RoboWeb::LayoutResult extendedLayout = RoboWeb::LayoutDocument(extendedLink, 80, 40);
    RoboWeb::LayoutResult simpleLayout = RoboWeb::LayoutDocument(simpleLink, 80, 40);
    const RoboWeb::ElementBox* extendedBox = RoboWeb::FindBox(extendedLayout, "svg");
    EXPECT(extendedBox != nullptr);
    if (extendedBox != nullptr) {
        RoboWeb::DispatchClick(extendedLink, extendedLayout, extendedBox->x + 4, extendedBox->y + 4);
        RoboWeb::DispatchClick(simpleLink, simpleLayout, extendedBox->x + 4, extendedBox->y + 4);
    }
    EXPECT(extendedLink.status.find("open") == std::string::npos);
    EXPECT(simpleLink.status.find("open") != std::string::npos);
    RoboWeb::Document dataColor = Parse(
        "<!DOCTYPE html><div data-ink='#ff0000' onload='this.style.background = this.dataset.ink' style='width:40px;height:16px'> </div>",
        diagnostics);
    RoboWeb::LayoutResult dataColorLayout = RoboWeb::LayoutDocument(dataColor, 80, 40);
    RoboWeb::Framebuffer dataColorFrame = RoboWeb::RenderDocument(dataColorLayout, 0);
    const RoboWeb::ElementBox* dataColorBox = RoboWeb::FindBox(dataColorLayout, "div");
    EXPECT(dataColorBox != nullptr);
    if (dataColorBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(dataColorFrame, dataColorBox->x + 8, dataColorBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(dataColorFrame, dataColorBox->x + 8, dataColorBox->y + 4) == 0);
    }
    RoboWeb::Document dataLabel = Parse(
        "<!DOCTYPE html><svg width='40' height='24'><text y='16' data-place-name='Go' onload='this.textContent = this.dataset.placeName'></text></svg>",
        diagnostics);
    bool sawPlaceName = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(dataLabel, 80, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "Go") {
            sawPlaceName = true;
        }
    }
    EXPECT(sawPlaceName);
    RoboWeb::Diagnostics currentSvg;
    RoboWeb::Document currentVersion = Parse("<!DOCTYPE html><svg version='1.1' width='16' height='16'><rect width='16' height='16' fill='#ff0000'/></svg>", currentSvg);
    RoboWeb::ValidateTree(currentVersion, currentSvg);
    EXPECT(CountMessages(currentSvg, RoboWeb::Severity::Warning, "not 1.0 or 1.1") == 0);
    RoboWeb::Diagnostics futureSvg;
    RoboWeb::Document futureVersion = Parse("<!DOCTYPE html><svg version='1.2' width='16' height='16'><rect width='16' height='16' fill='#ff0000'/></svg>", futureSvg);
    RoboWeb::ValidateTree(futureVersion, futureSvg);
    EXPECT(CountMessages(futureSvg, RoboWeb::Severity::Warning, "not 1.0 or 1.1") == 1);
    RoboWeb::Framebuffer futureFrame = RoboWeb::RenderDocument(RoboWeb::LayoutDocument(futureVersion, 80, 40), 0);
    const RoboWeb::ElementBox* futureBox = RoboWeb::FindBox(RoboWeb::LayoutDocument(futureVersion, 80, 40), "svg");
    EXPECT(futureBox != nullptr);
    if (futureBox != nullptr) {
        EXPECT(RoboWeb::PixelRed(futureFrame, futureBox->x + 4, futureBox->y + 4) == 255);
        EXPECT(RoboWeb::PixelGreen(futureFrame, futureBox->x + 4, futureBox->y + 4) == 0);
    }
    RoboWeb::Diagnostics nestedSvg;
    RoboWeb::Document nestedVersion = Parse("<!DOCTYPE html><svg version='1.1'><svg version='1.1'></svg></svg>", nestedSvg);
    RoboWeb::ValidateTree(nestedVersion, nestedSvg);
    EXPECT(CountMessages(nestedSvg, RoboWeb::Severity::Warning, "only allowed on the root svg") == 1);
    RoboWeb::Diagnostics knownProfile;
    RoboWeb::Document tinyProfile = Parse("<!DOCTYPE html><svg baseProfile='tiny'></svg>", knownProfile);
    RoboWeb::ValidateTree(tinyProfile, knownProfile);
    EXPECT(CountMessages(knownProfile, RoboWeb::Severity::Warning, "not a known profile") == 0);
    RoboWeb::Diagnostics unknownProfile;
    RoboWeb::Document fancyProfile = Parse("<!DOCTYPE html><svg baseProfile='fancy'></svg>", unknownProfile);
    RoboWeb::ValidateTree(fancyProfile, unknownProfile);
    EXPECT(CountMessages(unknownProfile, RoboWeb::Severity::Warning, "not a known profile") == 1);
    RoboWeb::Document roleLink = Parse(
        "<!DOCTYPE html><svg width='40' height='20'><a href='page.html' xlink:arcrole='http://example.com/mother'><rect width='20' height='20' fill='red'/></a></svg>",
        diagnostics);
    RoboWeb::LayoutResult roleLayout = RoboWeb::LayoutDocument(roleLink, 80, 40);
    const RoboWeb::ElementBox* roleBox = RoboWeb::FindBox(roleLayout, "svg");
    EXPECT(roleBox != nullptr);
    if (roleBox != nullptr) {
        RoboWeb::DispatchClick(roleLink, roleLayout, roleBox->x + 4, roleBox->y + 4);
    }
    EXPECT(roleLink.status.find("open page.html") != std::string::npos);
    EXPECT(roleLink.status.find("role http://example.com/mother") != std::string::npos);
    RoboWeb::Document asyncImage = Parse(
        "<!DOCTYPE html><svg width='8' height='4'><image href='split.ppm' width='4' height='4' decoding='async'/></svg>", diagnostics);
    RoboWeb::Document syncImage = Parse(
        "<!DOCTYPE html><svg width='8' height='4'><image href='split.ppm' width='4' height='4' decoding='sync'/></svg>", diagnostics);
    RoboWeb::LayoutResult asyncLayout = RoboWeb::LayoutDocument(asyncImage, 40, 20);
    RoboWeb::LayoutResult syncLayout = RoboWeb::LayoutDocument(syncImage, 40, 20);
    RoboWeb::Framebuffer asyncFirst = RoboWeb::RenderDocument(asyncLayout, 0, false);
    RoboWeb::Framebuffer asyncSettled = RoboWeb::RenderDocument(asyncLayout, 0, true);
    RoboWeb::Framebuffer syncFirst = RoboWeb::RenderDocument(syncLayout, 0, false);
    const RoboWeb::ElementBox* asyncBox = RoboWeb::FindBox(asyncLayout, "svg");
    EXPECT(asyncBox != nullptr);
    if (asyncBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(asyncFirst, asyncBox->x + 1, asyncBox->y + 1) == 255);
        EXPECT(RoboWeb::PixelGreen(asyncSettled, asyncBox->x + 1, asyncBox->y + 1) < 40);
        EXPECT(RoboWeb::PixelGreen(syncFirst, asyncBox->x + 1, asyncBox->y + 1) < 40);
    }
    RoboWeb::Document orderedFetches = Parse(
        "<!DOCTYPE html><svg width='8' height='4'><image href='late.ppm' fetchpriority='low' crossorigin='anonymous'/><image href='soon.ppm' fetchpriority='high' crossorigin='use-credentials'/></svg>",
        diagnostics);
    RoboWeb::LayoutDocument(orderedFetches, 40, 20);
    const std::vector<RoboWeb::PlannedFetch>& fetchPlan = RoboWeb::ResourceFetchPlan();
    EXPECT(fetchPlan.size() == 2);
    if (fetchPlan.size() == 2) {
        EXPECT(fetchPlan[0].url == "soon.ppm");
        EXPECT(fetchPlan[0].mode == "include");
        EXPECT(fetchPlan[1].url == "late.ppm");
        EXPECT(fetchPlan[1].mode == "anonymous");
    }
    RoboWeb::Document fraction = Parse("<!DOCTYPE html><math><mfrac><mn>1</mn><mn>2</mn></mfrac></math>", diagnostics);
    RoboWeb::LayoutResult fractionLayout = RoboWeb::LayoutDocument(fraction, 80, 80);
    RoboWeb::Framebuffer fractionFrame = RoboWeb::RenderDocument(fractionLayout, 0);
    int numeratorY = -1;
    int denominatorY = -1;
    int numeratorX = 0;
    for (const RoboWeb::DrawOp& op : fractionLayout.ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "1") {
            numeratorY = op.y;
            numeratorX = op.x;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "2") {
            denominatorY = op.y;
        }
    }
    EXPECT(numeratorY >= 0 && denominatorY > numeratorY + 4);
    if (denominatorY > numeratorY) {
        EXPECT(RoboWeb::PixelGreen(fractionFrame, numeratorX, denominatorY - 1) == 0);
    }
    RoboWeb::Document squareRoot = Parse("<!DOCTYPE html><math><msqrt><mi>x</mi></msqrt></math>", diagnostics);
    RoboWeb::LayoutResult squareLayout = RoboWeb::LayoutDocument(squareRoot, 80, 40);
    RoboWeb::Framebuffer squareFrame = RoboWeb::RenderDocument(squareLayout, 0);
    const RoboWeb::ElementBox* squareBox = RoboWeb::FindBox(squareLayout, "msqrt");
    int squareX = -1;
    EXPECT(squareBox != nullptr);
    for (const RoboWeb::DrawOp& op : squareLayout.ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "x") {
            squareX = op.x;
        }
    }
    if (squareBox != nullptr) {
        EXPECT(squareX > squareBox->x + 2);
        EXPECT(RoboWeb::PixelGreen(squareFrame, squareBox->x, squareBox->y + 4) == 0);
    }
    RoboWeb::Document cubeRoot = Parse("<!DOCTYPE html><math><mroot><mi>x</mi><mn>3</mn></mroot></math>", diagnostics);
    int rootBaseY = -1;
    int rootIndexY = -1;
    int rootBaseX = 0;
    int rootIndexX = 0;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(cubeRoot, 80, 60).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "x") {
            rootBaseY = op.y;
            rootBaseX = op.x;
        }
        if (op.text == "3") {
            rootIndexY = op.y;
            rootIndexX = op.x;
        }
    }
    EXPECT(rootIndexY >= 0 && rootIndexY < rootBaseY);
    EXPECT(rootIndexX < rootBaseX);
    RoboWeb::Document powerTwo = Parse("<!DOCTYPE html><math><msup><mi>X</mi><mn>2</mn></msup></math>", diagnostics);
    RoboWeb::Document powerShift = Parse(
        "<!DOCTYPE html><math><msup superscriptshift='10'><mi>X</mi><mn>2</mn></msup></math>", diagnostics);
    int baseY = -1;
    int raisedY = -1;
    int baseX = 0;
    int raisedX = 0;
    int shiftedBaseY = -1;
    int shiftedRaisedY = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(powerTwo, 80, 60).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "X") {
            baseY = op.y;
            baseX = op.x;
        }
        if (op.text == "2") {
            raisedY = op.y;
            raisedX = op.x;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(powerShift, 80, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "X") {
            shiftedBaseY = op.y;
        }
        if (op.text == "2") {
            shiftedRaisedY = op.y;
        }
    }
    EXPECT(raisedY >= 0 && raisedY < baseY);
    EXPECT(raisedX > baseX);
    EXPECT(shiftedBaseY - shiftedRaisedY > baseY - raisedY);
    RoboWeb::Document subOne = Parse("<!DOCTYPE html><math><msub><mi>X</mi><mn>1</mn></msub></math>", diagnostics);
    int subBaseY = -1;
    int subY = -1;
    int subBaseX = 0;
    int subX = 0;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(subOne, 80, 60).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "X") {
            subBaseY = op.y;
            subBaseX = op.x;
        }
        if (op.text == "1") {
            subY = op.y;
            subX = op.x;
        }
    }
    EXPECT(subY > subBaseY);
    EXPECT(subX > subBaseX);
    RoboWeb::Document bothScripts = Parse("<!DOCTYPE html><math><msubsup><mi>B</mi><mn>0</mn><mn>1</mn></msubsup></math>", diagnostics);
    int bothBaseY = -1;
    int bothSubY = -1;
    int bothSupY = -1;
    int bothBaseX = 0;
    int bothSubX = 0;
    int bothSupX = 0;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(bothScripts, 80, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "B") {
            bothBaseY = op.y;
            bothBaseX = op.x;
        }
        if (op.text == "0") {
            bothSubY = op.y;
            bothSubX = op.x;
        }
        if (op.text == "1") {
            bothSupY = op.y;
            bothSupX = op.x;
        }
    }
    EXPECT(bothSupY >= 0 && bothSupY < bothBaseY && bothBaseY < bothSubY);
    EXPECT(bothSupX > bothBaseX && bothSubX > bothBaseX);
    RoboWeb::Document accentOver = Parse("<!DOCTYPE html><math><mover accent='true'><mi>A</mi><mo>+</mo></mover></math>", diagnostics);
    RoboWeb::Document limitOver = Parse("<!DOCTYPE html><math><mover><mi>A</mi><mo>+</mo></mover></math>", diagnostics);
    int accentBase = -1;
    int accentMark = -1;
    int accentSize = 0;
    int limitBase = -1;
    int limitMark = -1;
    int limitSize = 0;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(accentOver, 80, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A") {
            accentBase = op.y;
        }
        if (op.text == "+") {
            accentMark = op.y;
            accentSize = op.fontSizePx;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(limitOver, 80, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A") {
            limitBase = op.y;
        }
        if (op.text == "+") {
            limitMark = op.y;
            limitSize = op.fontSizePx;
        }
    }
    EXPECT(accentMark >= 0 && accentMark < accentBase);
    EXPECT(accentSize > limitSize);
    EXPECT(accentBase - accentMark - RoboWeb::LineHeightPx(accentSize) < limitBase - limitMark - RoboWeb::LineHeightPx(limitSize));
    RoboWeb::Document underLimit = Parse("<!DOCTYPE html><math><munder><mi>C</mi><mo>-</mo></munder></math>", diagnostics);
    int underBase = -1;
    int underMark = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(underLimit, 80, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "C") {
            underBase = op.y;
        }
        if (op.text == "-") {
            underMark = op.y;
        }
    }
    EXPECT(underMark > underBase);
    RoboWeb::Document bothLimits = Parse("<!DOCTYPE html><math><munderover><mo>S</mo><mi>n</mi><mi>k</mi></munderover></math>", diagnostics);
    int sumY = -1;
    int lowerY = -1;
    int upperY = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(bothLimits, 80, 100).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "S") {
            sumY = op.y;
        }
        if (op.text == "n") {
            lowerY = op.y;
        }
        if (op.text == "k") {
            upperY = op.y;
        }
    }
    EXPECT(upperY >= 0 && upperY < sumY && sumY < lowerY);
    RoboWeb::Document gapSpace = Parse("<!DOCTYPE html><math><mn>1</mn><mspace width='40px' height='30px' depth='10px'></mspace><mn>2</mn></math>", diagnostics);
    RoboWeb::LayoutResult gapLayout = RoboWeb::LayoutDocument(gapSpace, 160, 80);
    int spaceOne = -1;
    int spaceTwo = -1;
    for (const RoboWeb::DrawOp& op : gapLayout.ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "1") {
            spaceOne = op.x;
        }
        if (op.text == "2") {
            spaceTwo = op.x;
        }
    }
    const RoboWeb::ElementBox* spaceBox = RoboWeb::FindBox(gapLayout, "mspace");
    EXPECT(spaceTwo > spaceOne + 30);
    EXPECT(spaceBox != nullptr && spaceBox->height >= 36);
    RoboWeb::Document phantom = Parse("<!DOCTYPE html><math><mi>a</mi><mphantom><mi>bbbb</mi></mphantom><mi>c</mi></math>", diagnostics);
    RoboWeb::LayoutResult phantomLayout = RoboWeb::LayoutDocument(phantom, 160, 40);
    int phantomA = -1;
    int phantomC = -1;
    bool phantomVisible = false;
    for (const RoboWeb::DrawOp& op : phantomLayout.ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "a") {
            phantomA = op.x;
        }
        if (op.text == "c") {
            phantomC = op.x;
        }
        if (op.text.find('b') != std::string::npos) {
            phantomVisible = true;
        }
    }
    EXPECT(!phantomVisible && phantomC > phantomA + 15);
    RoboWeb::Document encloseBox = Parse("<!DOCTYPE html><math><menclose notation='box'><mi>x</mi></menclose></math>", diagnostics);
    RoboWeb::LayoutResult encloseLayout = RoboWeb::LayoutDocument(encloseBox, 80, 40);
    RoboWeb::Framebuffer encloseFrame = RoboWeb::RenderDocument(encloseLayout, 0);
    const RoboWeb::ElementBox* encloseBounds = RoboWeb::FindBox(encloseLayout, "menclose");
    EXPECT(encloseBounds != nullptr);
    if (encloseBounds != nullptr) {
        EXPECT(RoboWeb::PixelGreen(encloseFrame, encloseBounds->x, encloseBounds->y) == 0);
        EXPECT(RoboWeb::PixelGreen(encloseFrame, encloseBounds->x + encloseBounds->width - 1, encloseBounds->y + 2) == 0);
    }
    RoboWeb::Document divided = Parse("<!DOCTYPE html><math><menclose notation='longdiv'><mi>x</mi></menclose></math>", diagnostics);
    RoboWeb::LayoutResult dividedLayout = RoboWeb::LayoutDocument(divided, 80, 40);
    RoboWeb::Framebuffer dividedFrame = RoboWeb::RenderDocument(dividedLayout, 0);
    const RoboWeb::ElementBox* dividedBox = RoboWeb::FindBox(dividedLayout, "menclose");
    EXPECT(dividedBox != nullptr);
    if (dividedBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(dividedFrame, dividedBox->x, dividedBox->y) == 0);
        EXPECT(RoboWeb::PixelGreen(dividedFrame, dividedBox->x + dividedBox->width - 1, dividedBox->y + 2) == 255);
    }
    RoboWeb::Document circled = Parse("<!DOCTYPE html><math><menclose notation='circle'><mi>x</mi></menclose></math>", diagnostics);
    RoboWeb::LayoutResult circledLayout = RoboWeb::LayoutDocument(circled, 80, 40);
    RoboWeb::Framebuffer circledFrame = RoboWeb::RenderDocument(circledLayout, 0);
    const RoboWeb::ElementBox* circledBox = RoboWeb::FindBox(circledLayout, "menclose");
    EXPECT(circledBox != nullptr);
    if (circledBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(circledFrame, circledBox->x + circledBox->width / 2, circledBox->y) == 0);
        EXPECT(RoboWeb::PixelGreen(circledFrame, circledBox->x, circledBox->y) == 255);
    }
    RoboWeb::Document padded = Parse("<!DOCTYPE html><math><mpadded lspace='24px' voffset='-10px' width='80px'><mi>a</mi></mpadded><mi>z</mi></math>", diagnostics);
    RoboWeb::LayoutResult paddedLayout = RoboWeb::LayoutDocument(padded, 200, 80);
    const RoboWeb::ElementBox* paddedBox = RoboWeb::FindBox(paddedLayout, "mpadded");
    int paddedA = -1;
    int paddedAY = -1;
    int paddedZ = -1;
    for (const RoboWeb::DrawOp& op : paddedLayout.ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "a") {
            paddedA = op.x;
            paddedAY = op.y;
        }
        if (op.text == "z") {
            paddedZ = op.x;
        }
    }
    EXPECT(paddedBox != nullptr && paddedA >= paddedBox->x + 20 && paddedAY >= paddedBox->y + 8 && paddedZ >= paddedA + 40);
    RoboWeb::Document twiceWide = Parse("<!DOCTYPE html><math><mpadded width='2width'><mi>aa</mi></mpadded><mi>z</mi></math>", diagnostics);
    RoboWeb::Document single = Parse("<!DOCTYPE html><math><mi>aa</mi><mi>z</mi></math>", diagnostics);
    int doubledA = -1;
    int doubledZ = -1;
    int singleA = -1;
    int singleZ = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(twiceWide, 200, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "aa") {
            doubledA = op.x;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "z") {
            doubledZ = op.x;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(single, 200, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "aa") {
            singleA = op.x;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "z") {
            singleZ = op.x;
        }
    }
    EXPECT(doubledZ - doubledA > singleZ - singleA + 4);
    RoboWeb::Document fenced = Parse("<!DOCTYPE html><math><mfenced open='[' close=']' separators=','><mi>a</mi><mi>b</mi></mfenced></math>", diagnostics);
    int fenceOpen = -1;
    int fenceA = -1;
    int fenceComma = -1;
    int fenceB = -1;
    int fenceClose = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(fenced, 160, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "[") {
            fenceOpen = op.x;
        }
        if (op.text == "a") {
            fenceA = op.x;
        }
        if (op.text == ",") {
            fenceComma = op.x;
        }
        if (op.text == "b") {
            fenceB = op.x;
        }
        if (op.text == "]") {
            fenceClose = op.x;
        }
    }
    EXPECT(fenceOpen >= 0 && fenceOpen < fenceA && fenceA < fenceComma && fenceComma < fenceB && fenceB < fenceClose);
    RoboWeb::Document gridMath = Parse("<!DOCTYPE html><math><mtable frame='solid' rowlines='solid' columnlines='solid' columnalign='left' width='90px'><mtr><mtd><mi>A</mi></mtd><mtd><mi>BB</mi></mtd></mtr><mtr><mtd><mi>C</mi></mtd><mtd><mi>D</mi></mtd></mtr></mtable></math>", diagnostics);
    RoboWeb::LayoutResult gridLayout = RoboWeb::LayoutDocument(gridMath, 180, 80);
    RoboWeb::Framebuffer gridFrame = RoboWeb::RenderDocument(gridLayout, 0);
    const RoboWeb::ElementBox* gridBox = RoboWeb::FindBox(gridLayout, "mtable");
    int gridA = -1;
    int gridAY = -1;
    int gridBB = -1;
    int gridC = -1;
    int gridCY = -1;
    for (const RoboWeb::DrawOp& op : gridLayout.ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A") {
            gridA = op.x;
            gridAY = op.y;
        }
        if (op.text == "BB") {
            gridBB = op.x;
        }
        if (op.text == "C") {
            gridC = op.x;
            gridCY = op.y;
        }
    }
    EXPECT(gridBox != nullptr && gridBox->width >= 80);
    EXPECT(gridBB > gridA && gridCY > gridAY && gridC == gridA);
    if (gridBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(gridFrame, gridBox->x, gridBox->y) == 0);
        EXPECT(RoboWeb::PixelGreen(gridFrame, gridBB - 1, gridAY) == 0);
        EXPECT(RoboWeb::PixelGreen(gridFrame, gridA, gridCY - 2) == 0);
    }
    RoboWeb::Document rightMath = Parse("<!DOCTYPE html><math><mtable columnalign='right'><mtr><mtd><mi>W</mi></mtd><mtd><mi>WW</mi></mtd></mtr><mtr><mtd><mi>Q</mi></mtd><mtd><mi>I</mi></mtd></mtr></mtable></math>", diagnostics);
    RoboWeb::Document leftMath = Parse("<!DOCTYPE html><math><mtable columnalign='left'><mtr><mtd><mi>W</mi></mtd><mtd><mi>WW</mi></mtd></mtr><mtr><mtd><mi>Q</mi></mtd><mtd><mi>I</mi></mtd></mtr></mtable></math>", diagnostics);
    int rightWide = -1;
    int rightNarrow = -1;
    int leftWide = -1;
    int leftNarrow = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(rightMath, 160, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "WW") {
            rightWide = op.x;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "I") {
            rightNarrow = op.x;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(leftMath, 160, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "WW") {
            leftWide = op.x;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "I") {
            leftNarrow = op.x;
        }
    }
    EXPECT(leftNarrow == leftWide && rightNarrow > rightWide);
    RoboWeb::Document spanMath = Parse("<!DOCTYPE html><math><mtable><mtr><mtd columnspan='2'><mi>AB</mi></mtd></mtr><mtr><mtd><mi>C</mi></mtd><mtd><mi>D</mi></mtd></mtr></mtable></math>", diagnostics);
    int spanAB = -1;
    int spanC = -1;
    int spanD = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(spanMath, 160, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "AB") {
            spanAB = op.x;
        }
        if (op.text == "C") {
            spanC = op.x;
        }
        if (op.text == "D") {
            spanD = op.x;
        }
    }
    EXPECT(spanAB > spanC && spanAB < spanD);
    RoboWeb::Document tintedMath = Parse("<!DOCTYPE html><math><mstyle mathcolor='#00ff00' fontsize='22px' fontweight='bold'><mn>M</mn></mstyle></math>", diagnostics);
    bool tinted = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(tintedMath, 80, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "M") {
            tinted = op.color.green == 255 && op.color.blue == 0 && op.fontSizePx >= 20 && op.bold;
        }
    }
    EXPECT(tinted);
    RoboWeb::Document scriptStack = Parse("<!DOCTYPE html><math><mmultiscripts><mi>R</mi><mn>5</mn><mn>6</mn><mprescripts></mprescripts><mn>7</mn><mn>8</mn></mmultiscripts></math>", diagnostics);
    int scriptBase = -1;
    int scriptBaseY = -1;
    int scriptPost = -1;
    int scriptPostY = -1;
    int scriptPre = -1;
    int scriptPreY = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(scriptStack, 160, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "R") {
            scriptBase = op.x;
            scriptBaseY = op.y;
        }
        if (op.text == "6") {
            scriptPost = op.x;
            scriptPostY = op.y;
        }
        if (op.text == "8") {
            scriptPre = op.x;
            scriptPreY = op.y;
        }
    }
    EXPECT(scriptPre >= 0 && scriptPre < scriptBase && scriptPost > scriptBase);
    EXPECT(scriptPostY < scriptBaseY && scriptPreY < scriptBaseY);
    RoboWeb::Document spacedOp = Parse("<!DOCTYPE html><math><mn>4</mn><mo lspace='28px' rspace='22px'>+</mo><mn>9</mn></math>", diagnostics);
    int leadDigit = -1;
    int spacedPlus = -1;
    int trailDigit = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(spacedOp, 200, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "4") {
            leadDigit = op.x;
        }
        if (op.text == "+") {
            spacedPlus = op.x;
        }
        if (op.text == "9") {
            trailDigit = op.x;
        }
    }
    EXPECT(spacedPlus > leadDigit + 20 && trailDigit > spacedPlus + 16);
    RoboWeb::Document infixOp = Parse("<!DOCTYPE html><math><mn>4</mn><mo form='infix'>+</mo><mn>9</mn></math>", diagnostics);
    RoboWeb::Document prefixOp = Parse("<!DOCTYPE html><math><mn>4</mn><mo form='prefix'>+</mo><mn>9</mn></math>", diagnostics);
    int infixGap = 0;
    int prefixGap = 0;
    int infixDigit = -1;
    int infixPlus = -1;
    int prefixDigit = -1;
    int prefixPlus = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(infixOp, 160, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "4") {
            infixDigit = op.x;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "+") {
            infixPlus = op.x;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(prefixOp, 160, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "4") {
            prefixDigit = op.x;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "+") {
            prefixPlus = op.x;
        }
    }
    infixGap = infixPlus - infixDigit;
    prefixGap = prefixPlus - prefixDigit;
    EXPECT(infixGap > prefixGap);
    RoboWeb::Document bigOp = Parse("<!DOCTYPE html><math><mo largeop='true'>S</mo></math>", diagnostics);
    RoboWeb::Document smallOp = Parse("<!DOCTYPE html><math><mo>S</mo></math>", diagnostics);
    int bigSize = 0;
    int smallSize = 0;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(bigOp, 80, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "S") {
            bigSize = op.fontSizePx;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(smallOp, 80, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "S") {
            smallSize = op.fontSizePx;
        }
    }
    EXPECT(bigSize > smallSize);
    RoboWeb::Document tallBar = Parse("<!DOCTYPE html><math><mo stretchy='true' minsize='40px'>|</mo></math>", diagnostics);
    RoboWeb::LayoutResult tallLayout = RoboWeb::LayoutDocument(tallBar, 80, 80);
    RoboWeb::Framebuffer tallFrame = RoboWeb::RenderDocument(tallLayout, 0);
    const RoboWeb::ElementBox* tallBox = RoboWeb::FindBox(tallLayout, "mo");
    EXPECT(tallBox != nullptr && tallBox->height >= 36);
    if (tallBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(tallFrame, tallBox->x, tallBox->y + 4) == 0);
    }
    RoboWeb::Document quoted = Parse("<!DOCTYPE html><math><ms lquote='[' rquote=']'>ab</ms></math>", diagnostics);
    bool quotedText = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(quoted, 80, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "[ab]") {
            quotedText = true;
        }
    }
    EXPECT(quotedText);
    RoboWeb::Document broken = Parse("<!DOCTYPE html><math><merror><mtext>bad</mtext></merror></math>", diagnostics);
    bool brokenRed = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(broken, 80, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "bad") {
            brokenRed = op.color.red == 255 && op.color.green == 0;
        }
    }
    EXPECT(brokenRed);
    RoboWeb::Document noted = Parse("<!DOCTYPE html><math><semantics><mi>x</mi><annotation>secret</annotation></semantics></math>", diagnostics);
    bool notedSymbol = false;
    bool notedSecret = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(noted, 120, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "x") {
            notedSymbol = true;
        }
        if (op.text.find("secret") != std::string::npos) {
            notedSecret = true;
        }
    }
    EXPECT(notedSymbol && !notedSecret);
    RoboWeb::Document blockMath = Parse("<!DOCTYPE html><span>A</span><math display='block'><mi>B</mi></math><span>C</span>", diagnostics);
    int blockA = -1;
    int blockB = -1;
    int blockC = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(blockMath, 120, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A") {
            blockA = op.y;
        }
        if (op.text == "B") {
            blockB = op.y;
        }
        if (op.text == "C") {
            blockC = op.y;
        }
    }
    EXPECT(blockA >= 0 && blockB > blockA && blockC > blockB);
    RoboWeb::Document upright = Parse("<!DOCTYPE html><math><mi>x</mi><mi>sin</mi><mi mathvariant='normal'>y</mi><mi mathvariant='bold'>z</mi></math>", diagnostics);
    bool uprightItalic = false;
    bool uprightWord = false;
    bool uprightNormal = false;
    bool uprightBold = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(upright, 160, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "x") {
            uprightItalic = op.italic;
        }
        if (op.text == "sin") {
            uprightWord = !op.italic;
        }
        if (op.text == "y") {
            uprightNormal = !op.italic;
        }
        if (op.text == "z") {
            uprightBold = op.bold && !op.italic;
        }
    }
    EXPECT(uprightItalic && uprightWord && uprightNormal && uprightBold);
    RoboWeb::Document toggled = Parse("<!DOCTYPE html><math><maction actiontype='toggle' selection='1'><mi>A</mi><mi>B</mi></maction></math>", diagnostics);
    RoboWeb::LayoutResult toggledLayout = RoboWeb::LayoutDocument(toggled, 80, 40);
    const RoboWeb::ElementBox* toggledBox = RoboWeb::FindBox(toggledLayout, "mi");
    bool toggledFirst = false;
    bool toggledSecond = false;
    for (const RoboWeb::DrawOp& op : toggledLayout.ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "A") {
            toggledFirst = true;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "B") {
            toggledSecond = true;
        }
    }
    EXPECT(toggledFirst && !toggledSecond && toggledBox != nullptr);
    if (toggledBox != nullptr) {
        RoboWeb::DispatchClick(toggled, toggledLayout, toggledBox->x + 1, toggledBox->y + 1);
    }
    EXPECT(toggled.status.find("selection 2") != std::string::npos);
    bool toggledAfter = false;
    bool toggledBefore = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(toggled, 80, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "B") {
            toggledAfter = true;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "A") {
            toggledBefore = true;
        }
    }
    EXPECT(toggledAfter && !toggledBefore);
    RoboWeb::Document statusMath = Parse("<!DOCTYPE html><math><maction actiontype='statusline'><mi>E</mi><mtext>saved</mtext></maction></math>", diagnostics);
    RoboWeb::LayoutResult statusLayout = RoboWeb::LayoutDocument(statusMath, 80, 40);
    const RoboWeb::ElementBox* statusBox = RoboWeb::FindBox(statusLayout, "mi");
    EXPECT(statusBox != nullptr);
    if (statusBox != nullptr) {
        EXPECT(RoboWeb::DispatchHover(statusMath, statusLayout, statusBox->x + 1, statusBox->y + 1));
    }
    EXPECT(statusMath.status.find("saved") != std::string::npos);
    RoboWeb::Document rightward = Parse("<!DOCTYPE html><math dir='rtl'><mn>1</mn><mo>+</mo><mn>2</mn></math>", diagnostics);
    int rightOne = -1;
    int rightTwo = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(rightward, 120, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "1") {
            rightOne = op.x;
        }
        if (op.text == "2") {
            rightTwo = op.x;
        }
    }
    EXPECT(rightTwo >= 0 && rightTwo < rightOne);
    RoboWeb::Document compactSum = Parse("<!DOCTYPE html><math><munder displaystyle='false'><mo>S</mo><mi>A</mi></munder></math>", diagnostics);
    RoboWeb::Document displaySum = Parse("<!DOCTYPE html><math><munder displaystyle='true'><mo>S</mo><mi>A</mi></munder></math>", diagnostics);
    int compactBase = -1;
    int compactScript = -1;
    int compactSize = 0;
    int displayBaseY = -1;
    int displayScriptY = -1;
    int displaySize = 0;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(compactSum, 120, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "S") {
            compactBase = op.x;
            compactSize = op.fontSizePx;
        }
        if (op.text == "A") {
            compactScript = op.x;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(displaySum, 120, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "S") {
            displayBaseY = op.y;
            displaySize = op.fontSizePx;
        }
        if (op.text == "A") {
            displayScriptY = op.y;
        }
    }
    EXPECT(compactScript > compactBase && displayScriptY > displayBaseY && displaySize > compactSize);
    RoboWeb::Document depthMath = Parse("<!DOCTYPE html><math scriptlevel='+1'><mn scriptlevel='0'>A</mn><mn>B</mn></math>", diagnostics);
    int resetSize = 0;
    int nestedSize = 0;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(depthMath, 120, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A") {
            resetSize = op.fontSizePx;
        }
        if (op.text == "B") {
            nestedSize = op.fontSizePx;
        }
    }
    EXPECT(resetSize > nestedSize && nestedSize > 0);
    RoboWeb::Document linkedRoot = Parse("<!DOCTYPE html><math><msqrt href='root.html'><mi>x</mi></msqrt></math>", diagnostics);
    RoboWeb::LayoutResult linkedRootLayout = RoboWeb::LayoutDocument(linkedRoot, 80, 40);
    const RoboWeb::ElementBox* linkedRootBox = RoboWeb::FindBox(linkedRootLayout, "msqrt");
    EXPECT(linkedRootBox != nullptr);
    if (linkedRootBox != nullptr) {
        RoboWeb::DispatchClick(linkedRoot, linkedRootLayout, linkedRootBox->x + 4, linkedRootBox->y + 4);
    }
    EXPECT(linkedRoot.status.find("open root.html") != std::string::npos);
    RoboWeb::Document linkedGamma = Parse("<!DOCTYPE html><math><a href='gamma.html'><mi>G</mi></a></math>", diagnostics);
    RoboWeb::LayoutResult linkedGammaLayout = RoboWeb::LayoutDocument(linkedGamma, 80, 40);
    const RoboWeb::ElementBox* linkedGammaBox = RoboWeb::FindBox(linkedGammaLayout, "mi");
    EXPECT(linkedGammaBox != nullptr);
    if (linkedGammaBox != nullptr) {
        RoboWeb::DispatchClick(linkedGamma, linkedGammaLayout, linkedGammaBox->x + 1, linkedGammaBox->y + 1);
    }
    EXPECT(linkedGamma.status.find("open gamma.html") != std::string::npos);
    RoboWeb::Document keptRow = Parse("<!DOCTYPE html><math><mrow><mi>MMMM</mi><mi>NNNN</mi></mrow></math>", diagnostics);
    RoboWeb::Document splitRow = Parse("<!DOCTYPE html><math><mi>MMMM</mi><mi>NNNN</mi></math>", diagnostics);
    int keptFirst = -1;
    int keptSecond = -1;
    int splitFirst = -1;
    int splitSecond = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(keptRow, 20, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "MMMM") {
            keptFirst = op.y;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "NNNN") {
            keptSecond = op.y;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(splitRow, 20, 80).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "MMMM") {
            splitFirst = op.y;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "NNNN") {
            splitSecond = op.y;
        }
    }
    EXPECT(keptFirst >= 0 && keptSecond == keptFirst && splitSecond > splitFirst);
    RoboWeb::Document literalText = Parse("<!DOCTYPE html><math><mtext>a  b</mtext><mi>a  b</mi></math>", diagnostics);
    bool literalSpaces = false;
    bool collapsedLiteral = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(literalText, 160, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "a  b") {
            literalSpaces = true;
        }
        if (op.text == "a b") {
            collapsedLiteral = true;
        }
    }
    EXPECT(literalSpaces && collapsedLiteral);
    RoboWeb::Document digitMath = Parse("<!DOCTYPE html><math><mn>8</mn><mi>n</mi></math>", diagnostics);
    bool digitUpright = false;
    bool nameItalic = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(digitMath, 80, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "8") {
            digitUpright = !op.italic;
        }
        if (op.text == "n") {
            nameItalic = op.italic;
        }
    }
    EXPECT(digitUpright && nameItalic);
    RoboWeb::Document paintedMath = Parse("<!DOCTYPE html><math><mn mathcolor='#00ff00' mathbackground='#ff0000' mathsize='30px'>Q</mn></math>", diagnostics);
    bool paintedInk = false;
    bool paintedBack = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(paintedMath, 80, 50).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "Q") {
            paintedInk = op.color.green == 255 && op.color.blue == 0 && op.fontSizePx >= 28;
        }
        if (op.kind == RoboWeb::DrawOp::Kind::Fill && op.color.red == 255 && op.color.green == 0) {
            paintedBack = true;
        }
    }
    EXPECT(paintedInk && paintedBack);
    RoboWeb::Document mathData = Parse("<!DOCTYPE html><math><mn data-place-name='euler' onload='this.textContent = this.dataset.placeName'>0</mn></math>", diagnostics);
    bool mathDataText = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(mathData, 120, 40).ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.text == "euler") {
            mathDataText = true;
        }
    }
    EXPECT(mathDataText);
    RoboWeb::Document tracked = Parse("<!DOCTYPE html><style>span.wide { letter-spacing: 20px; }</style><span class='wide'>AB</span>Z", diagnostics);
    RoboWeb::Document packed = Parse("<!DOCTYPE html><span>AB</span>Z", diagnostics);
    int trackedMark = -1;
    int trackedTail = -1;
    int packedMark = -1;
    int packedTail = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(tracked, 200, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "AB") {
            trackedMark = op.x;
        }
        if (op.text == "Z") {
            trackedTail = op.x;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(packed, 200, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "AB") {
            packedMark = op.x;
        }
        if (op.text == "Z") {
            packedTail = op.x;
        }
    }
    EXPECT(trackedTail - trackedMark > packedTail - packedMark + 10);
    RoboWeb::Document looseLines = Parse("<!DOCTYPE html><style>p.loose { line-height: 40px; }</style><p class='loose'>A<br>B</p>", diagnostics);
    RoboWeb::Document doubleLines = Parse("<!DOCTYPE html><style>p.double { line-height: 2; }</style><p class='double'>A<br>B</p>", diagnostics);
    RoboWeb::Document tightLines = Parse("<!DOCTYPE html><p>A<br>B</p>", diagnostics);
    int looseTop = -1;
    int looseBottom = -1;
    int doubleTop = -1;
    int doubleBottom = -1;
    int tightTop = -1;
    int tightBottom = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(looseLines, 120, 120).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A") {
            looseTop = op.y;
        }
        if (op.text == "B") {
            looseBottom = op.y;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(doubleLines, 120, 120).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A") {
            doubleTop = op.y;
        }
        if (op.text == "B") {
            doubleBottom = op.y;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(tightLines, 120, 120).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A") {
            tightTop = op.y;
        }
        if (op.text == "B") {
            tightBottom = op.y;
        }
    }
    EXPECT(looseBottom - looseTop >= 36);
    EXPECT(doubleBottom - doubleTop > tightBottom - tightTop + 8);
    RoboWeb::Document wordWide = Parse("<!DOCTYPE html><style>span.words { word-spacing: 20px; }</style><span class='words'>A B</span>Z", diagnostics);
    RoboWeb::Document wordPlain = Parse("<!DOCTYPE html><span>A B</span>Z", diagnostics);
    int wordMark = -1;
    int wordTail = -1;
    int plainMark = -1;
    int plainTail = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(wordWide, 220, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A B") {
            wordMark = op.x;
        }
        if (op.text == "Z") {
            wordTail = op.x;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(wordPlain, 220, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A B") {
            plainMark = op.x;
        }
        if (op.text == "Z") {
            plainTail = op.x;
        }
    }
    EXPECT(wordTail - wordMark > plainTail - plainMark + 10);
    RoboWeb::Document indentedBlock = Parse("<!DOCTYPE html><style>p.indented { text-indent: 28px; }</style><p class='indented'>A<br>B</p>", diagnostics);
    RoboWeb::Document hangingBlock = Parse("<!DOCTYPE html><style>p.hanging { text-indent: 28px hanging; }</style><p class='hanging'>A<br>B</p>", diagnostics);
    RoboWeb::Document flatBlock = Parse("<!DOCTYPE html><p>A<br>B</p>", diagnostics);
    int indentFirst = -1;
    int indentSecond = -1;
    int hangFirst = -1;
    int hangSecond = -1;
    int flatFirst = -1;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(indentedBlock, 160, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A") {
            indentFirst = op.x;
        }
        if (op.text == "B") {
            indentSecond = op.x;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(hangingBlock, 160, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A") {
            hangFirst = op.x;
        }
        if (op.text == "B") {
            hangSecond = op.x;
        }
    }
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(flatBlock, 160, 80).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "A") {
            flatFirst = op.x;
        }
    }
    EXPECT(indentFirst > flatFirst + 20 && indentSecond < indentFirst - 16);
    EXPECT(hangSecond > hangFirst + 20 && hangFirst < hangSecond - 16);
    RoboWeb::Document shapedText = Parse("<!DOCTYPE html><style>span.up { text-transform: uppercase; } span.down { text-transform: lowercase; } span.cap { text-transform: capitalize; }</style><span class='up'>Ab</span><span class='down'>Ab</span><span class='cap'>ab cd</span>", diagnostics);
    bool sawUpper = false;
    bool sawLower = false;
    bool sawCapital = false;
    for (const RoboWeb::DrawOp& op : RoboWeb::LayoutDocument(shapedText, 220, 40).ops) {
        if (op.kind != RoboWeb::DrawOp::Kind::Text) {
            continue;
        }
        if (op.text == "AB") {
            sawUpper = true;
        }
        if (op.text == "ab") {
            sawLower = true;
        }
        if (op.text == "Ab Cd") {
            sawCapital = true;
        }
    }
    EXPECT(sawUpper && sawLower && sawCapital);
    RoboWeb::Document inkText = Parse("<!DOCTYPE html><style>span.ink { color: #000000; text-decoration: underline; text-decoration-color: #0000ff; }</style><span class='ink'>A</span>", diagnostics);
    RoboWeb::LayoutResult inkLayout = RoboWeb::LayoutDocument(inkText, 80, 40);
    const RoboWeb::DrawOp* inkOp = nullptr;
    for (const RoboWeb::DrawOp& op : inkLayout.ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.underline && op.hasDecorationColor && op.decorationColor.blue == 255 && op.color.blue == 0) {
            inkOp = &op;
        }
    }
    EXPECT(inkOp != nullptr);
    if (inkOp != nullptr) {
        RoboWeb::Framebuffer inkFrame = RoboWeb::RenderDocument(inkLayout, 0);
        EXPECT(RoboWeb::PixelBlue(inkFrame, inkOp->x, inkOp->y + RoboWeb::LineHeightPx(inkOp->fontSizePx) - 2) == 255);
        EXPECT(RoboWeb::PixelGreen(inkFrame, inkOp->x, inkOp->y + RoboWeb::LineHeightPx(inkOp->fontSizePx) - 2) == 0);
    }
    RoboWeb::Document dropUnderlineText = Parse("<!DOCTYPE html><style>span.drop { color: #000000; text-decoration: underline; text-decoration-color: #0000ff; text-underline-offset: 6px; }</style><span class='drop'>A</span>", diagnostics);
    RoboWeb::LayoutResult dropUnderlineLayout = RoboWeb::LayoutDocument(dropUnderlineText, 80, 48);
    const RoboWeb::DrawOp* dropUnderlineOp = nullptr;
    for (const RoboWeb::DrawOp& op : dropUnderlineLayout.ops) {
        if (op.kind == RoboWeb::DrawOp::Kind::Text && op.underline && op.underlineOffsetPx == 6) {
            dropUnderlineOp = &op;
        }
    }
    EXPECT(dropUnderlineOp != nullptr);
    if (dropUnderlineOp != nullptr) {
        RoboWeb::Framebuffer dropUnderlineFrame = RoboWeb::RenderDocument(dropUnderlineLayout, 0);
        const int dropUnderlineLine = dropUnderlineOp->y + RoboWeb::LineHeightPx(dropUnderlineOp->fontSizePx) - 2;
        EXPECT(RoboWeb::PixelBlue(dropUnderlineFrame, dropUnderlineOp->x, dropUnderlineLine + 6) == 255);
        EXPECT(RoboWeb::PixelGreen(dropUnderlineFrame, dropUnderlineOp->x, dropUnderlineLine + 6) == 0);
        EXPECT(RoboWeb::PixelBlue(dropUnderlineFrame, dropUnderlineOp->x, dropUnderlineLine) == 0 || RoboWeb::PixelGreen(dropUnderlineFrame, dropUnderlineOp->x, dropUnderlineLine) != 0);
    }
    RoboWeb::Document moving = Parse("<!DOCTYPE html><marquee>Move</marquee>", diagnostics);
    EXPECT(RoboWeb::FindBox(RoboWeb::LayoutDocument(moving, 200, 80), "marquee") != nullptr);
    RoboWeb::Document frames = Parse("<!DOCTYPE html><frameset cols='50%,50%'><frame src='a'><frame src='b'></frameset>", diagnostics);
    EXPECT(RoboWeb::FindBox(RoboWeb::LayoutDocument(frames, 300, 200), "frame") != nullptr);
}

// Covers the SVG specification surface the runtime implements: the element and
// attribute catalogs, the color syntaxes CSS shares with SVG, shape geometry
// (rounded corners and dashed strokes), the affine transform stack, the
// presentation-attribute cascade, gradient and marker transforms, and the
// clip-path engine that masks every fill, stroke and text draw.
void TestSvgSpecification() {
    RoboWeb::Diagnostics diagnostics;
    struct SvgFrame {
        RoboWeb::LayoutResult layout;
        RoboWeb::Framebuffer framebuffer;
        int originX = 0;
        int originY = 0;
    };
    auto render = [&](const std::string& markup, int width, int height) {
        RoboWeb::Document document = Parse("<!DOCTYPE html>" + markup, diagnostics);
        SvgFrame frame;
        frame.layout = RoboWeb::LayoutDocument(document, width, height);
        frame.framebuffer = RoboWeb::RenderDocument(frame.layout, 0);
        const RoboWeb::ElementBox* box = RoboWeb::FindBox(frame.layout, "svg");
        if (box != nullptr) {
            frame.originX = box->x;
            frame.originY = box->y;
        }
        return frame;
    };
    auto pixelRed = [](const SvgFrame& frame, int x, int y) {
        return static_cast<int>(RoboWeb::PixelRed(frame.framebuffer, frame.originX + x, frame.originY + y));
    };
    auto pixelGreen = [](const SvgFrame& frame, int x, int y) {
        return static_cast<int>(RoboWeb::PixelGreen(frame.framebuffer, frame.originX + x, frame.originY + y));
    };
    auto pixelBlue = [](const SvgFrame& frame, int x, int y) {
        return static_cast<int>(RoboWeb::PixelBlue(frame.framebuffer, frame.originX + x, frame.originY + y));
    };
    auto near = [](int value, int wanted, int tolerance) { return value >= wanted - tolerance && value <= wanted + tolerance; };
    auto matches = [&](const SvgFrame& frame, int x, int y, int red, int green, int blue, int tolerance) {
        return near(pixelRed(frame, x, y), red, tolerance) && near(pixelGreen(frame, x, y), green, tolerance) &&
               near(pixelBlue(frame, x, y), blue, tolerance);
    };
    auto isRed = [&](const SvgFrame& frame, int x, int y) { return matches(frame, x, y, 255, 0, 0, 2); };
    auto isGreen = [&](const SvgFrame& frame, int x, int y) { return matches(frame, x, y, 0, 255, 0, 2); };
    auto isBlue = [&](const SvgFrame& frame, int x, int y) { return matches(frame, x, y, 0, 0, 255, 2); };
    auto isWhite = [&](const SvgFrame& frame, int x, int y) { return matches(frame, x, y, 255, 255, 255, 2); };
    auto redPixels = [&](const SvgFrame& frame) {
        int count = 0;
        for (int y = 0; y < frame.framebuffer.height; ++y) {
            for (int x = 0; x < frame.framebuffer.width; ++x) {
                if (RoboWeb::PixelRed(frame.framebuffer, x, y) > 200 && RoboWeb::PixelGreen(frame.framebuffer, x, y) < 60 &&
                    RoboWeb::PixelBlue(frame.framebuffer, x, y) < 60) {
                    ++count;
                }
            }
        }
        return count;
    };
    auto differs = [&](const SvgFrame& first, const SvgFrame& second) {
        if (first.framebuffer.pixels.size() != second.framebuffer.pixels.size()) {
            return true;
        }
        for (std::size_t index = 0; index < first.framebuffer.pixels.size(); ++index) {
            if (first.framebuffer.pixels[index] != second.framebuffer.pixels[index]) {
                return true;
            }
        }
        return false;
    };

    // Element, attribute and color catalogs.
    {
        std::string catalogError;
        EXPECT(RoboWeb::SvgCatalogSelfCheck(catalogError));
        EXPECT(RoboWeb::SvgAttributeCatalogSelfCheck(catalogError));
        EXPECT(RoboWeb::SvgElementCount() > 60);
        EXPECT(RoboWeb::SvgAttributeCount() > 100);
        EXPECT(RoboWeb::FindSvgElement("fegaussianblur") == RoboWeb::SvgElementKind::FeGaussianBlur);
        EXPECT(RoboWeb::FindSvgElement("lineargradient") == RoboWeb::SvgElementKind::LinearGradient);
        EXPECT(RoboWeb::FindSvgElement("clippath") == RoboWeb::SvgElementKind::ClipPath);
        EXPECT(RoboWeb::FindSvgElement("notanelement") == RoboWeb::SvgElementKind::Unknown);
        EXPECT(RoboWeb::IsSvgElementName("foreignobject"));
        EXPECT(!RoboWeb::IsSvgElementName("div"));
        EXPECT(RoboWeb::SvgElementNeverRendered(RoboWeb::SvgElementKind::ClipPath));
        EXPECT(RoboWeb::SvgElementNeverRendered(RoboWeb::SvgElementKind::Filter));
        EXPECT(!RoboWeb::SvgElementNeverRendered(RoboWeb::SvgElementKind::Rect));
        EXPECT(RoboWeb::IsSvgAttributeName("stroke-dasharray"));
        EXPECT(RoboWeb::IsSvgPresentationAttribute("fill-opacity"));
        EXPECT(!RoboWeb::IsSvgPresentationAttribute("d"));
        EXPECT(RoboWeb::SvgAttributeAppliesTo("d", RoboWeb::SvgElementKind::Path));
        EXPECT(RoboWeb::SvgAttributeAppliesTo("r", RoboWeb::SvgElementKind::Circle));
        EXPECT(!RoboWeb::SvgAttributeAppliesTo("r", RoboWeb::SvgElementKind::Rect));
        std::uint8_t red = 0;
        std::uint8_t green = 0;
        std::uint8_t blue = 0;
        EXPECT(RoboWeb::LookupSvgColorKeyword("rebeccapurple", red, green, blue) && red == 102 && green == 51 && blue == 153);
        EXPECT(RoboWeb::LookupSvgColorKeyword("cornflowerblue", red, green, blue) && red == 100 && green == 149 && blue == 237);
        EXPECT(!RoboWeb::LookupSvgColorKeyword("notacolor", red, green, blue));
    }

    // Color syntaxes shared with CSS Color 4.
    {
        SvgFrame hslFrame = render("<svg width='20' height='20'><rect width='20' height='20' fill='hsl(120, 100%, 50%)'/></svg>", 80, 48);
        EXPECT(matches(hslFrame, 10, 10, 0, 255, 0, 2));
        SvgFrame turnFrame = render("<svg width='20' height='20'><rect width='20' height='20' fill='hsl(0.5turn, 100%, 50%)'/></svg>", 80, 48);
        EXPECT(matches(turnFrame, 10, 10, 0, 255, 255, 2));
        SvgFrame percentFrame = render("<svg width='20' height='20'><rect width='20' height='20' fill='rgb(100%, 0%, 0%)'/></svg>", 80, 48);
        EXPECT(isRed(percentFrame, 10, 10));
        SvgFrame shortHex = render("<svg width='20' height='20'><rect width='20' height='20' fill='#0f0'/></svg>", 80, 48);
        EXPECT(isGreen(shortHex, 10, 10));
        SvgFrame alphaHex = render("<svg width='20' height='20'><rect width='20' height='20' fill='#0000ff80'/></svg>", 80, 48);
        EXPECT(matches(alphaHex, 10, 10, 127, 127, 255, 2));
        SvgFrame shortAlphaHex = render("<svg width='20' height='20'><rect width='20' height='20' fill='#0f08'/></svg>", 80, 48);
        EXPECT(matches(shortAlphaHex, 10, 10, 119, 255, 119, 2));
        SvgFrame rgbaFrame = render("<svg width='20' height='20'><rect width='20' height='20' fill='rgba(255, 0, 0, 0.5)'/></svg>", 80, 48);
        EXPECT(matches(rgbaFrame, 10, 10, 255, 127, 127, 2));
        SvgFrame transparentFrame = render("<svg width='20' height='20'><rect width='20' height='20' fill='transparent'/></svg>", 80, 48);
        EXPECT(isWhite(transparentFrame, 10, 10));
        SvgFrame keywordFrame = render("<svg width='20' height='20'><rect width='20' height='20' fill='rebeccapurple'/></svg>", 80, 48);
        EXPECT(matches(keywordFrame, 10, 10, 102, 51, 153, 2));
        SvgFrame currentColorFrame = render("<svg width='20' height='20'><rect width='20' height='20' color='blue' fill='currentColor'/></svg>", 80, 48);
        EXPECT(isBlue(currentColorFrame, 10, 10));
    }

    // Rounded rectangle geometry, radii defaults and clamping.
    {
        SvgFrame rounded = render("<svg width='20' height='20'><rect width='20' height='20' rx='6' ry='6' fill='red'/></svg>", 80, 48);
        EXPECT(isRed(rounded, 10, 10));
        EXPECT(isRed(rounded, 10, 1));
        EXPECT(isRed(rounded, 1, 10));
        EXPECT(isWhite(rounded, 0, 0));
        EXPECT(isWhite(rounded, 19, 0));
        EXPECT(isWhite(rounded, 0, 19));
        EXPECT(isWhite(rounded, 19, 19));
        SvgFrame autoRadius = render("<svg width='20' height='20'><rect width='20' height='20' rx='6' fill='red'/></svg>", 80, 48);
        EXPECT(isWhite(autoRadius, 0, 0) && isRed(autoRadius, 10, 10) && isRed(autoRadius, 10, 0));
        SvgFrame clamped = render("<svg width='20' height='10'><rect width='20' height='10' ry='20' fill='red'/></svg>", 80, 48);
        EXPECT(isWhite(clamped, 0, 0));
        // The clamped caps are elliptical, so the extreme columns are mostly
        // covered rather than fully inked; the corners stay empty.
        EXPECT(pixelRed(clamped, 0, 5) > 200 && pixelGreen(clamped, 0, 5) < 80);
        EXPECT(isRed(clamped, 10, 5));
        EXPECT(isRed(clamped, 18, 5));
        EXPECT(pixelRed(clamped, 19, 5) > 200 && pixelGreen(clamped, 19, 5) < 80);
        EXPECT(isWhite(clamped, 19, 9));
        SvgFrame oval = render("<svg width='20' height='20'><rect width='20' height='20' rx='10' ry='4' fill='red'/></svg>", 80, 48);
        EXPECT(isWhite(oval, 0, 0));
        EXPECT(isRed(oval, 0, 10));
        EXPECT(isRed(oval, 10, 1));
        SvgFrame roundedStroke = render("<svg width='20' height='20'><rect width='20' height='20' rx='6' fill='none' stroke='red' stroke-width='2'/></svg>", 80, 48);
        EXPECT(isWhite(roundedStroke, 19, 19));
        EXPECT(isWhite(roundedStroke, 0, 19));
        EXPECT(isRed(roundedStroke, 10, 0));
        EXPECT(isRed(roundedStroke, 0, 10));
    }

    // Dashed strokes: the pattern walks the outline in pixels, odd lists are
    // duplicated, and non-positive patterns mean "no dashes".
    {
        SvgFrame solidLine = render("<svg width='40' height='8'><line x1='0' y1='4' x2='40' y2='4' stroke='red' stroke-width='2'/></svg>", 120, 40);
        SvgFrame dashedLine = render(
            "<svg width='40' height='8'><line x1='0' y1='4' x2='40' y2='4' stroke='red' stroke-width='2' stroke-dasharray='10 10'/></svg>", 120, 40);
        EXPECT(isRed(solidLine, 12, 4));
        EXPECT(isRed(dashedLine, 2, 4));
        EXPECT(isWhite(dashedLine, 12, 4));
        EXPECT(isRed(dashedLine, 22, 4));
        EXPECT(redPixels(dashedLine) > 0 && redPixels(dashedLine) < redPixels(solidLine));
        SvgFrame oddLine = render(
            "<svg width='40' height='8'><line x1='0' y1='4' x2='40' y2='4' stroke='red' stroke-width='2' stroke-dasharray='5 5 5'/></svg>", 120, 40);
        EXPECT(isRed(oddLine, 2, 4));
        EXPECT(isWhite(oddLine, 7, 4));
        EXPECT(isRed(oddLine, 12, 4));
        SvgFrame negativeLine = render(
            "<svg width='40' height='8'><line x1='0' y1='4' x2='40' y2='4' stroke='red' stroke-width='2' stroke-dasharray='-5 5'/></svg>", 120, 40);
        EXPECT(isRed(negativeLine, 12, 4));
        SvgFrame zeroLine = render(
            "<svg width='40' height='8'><line x1='0' y1='4' x2='40' y2='4' stroke='red' stroke-width='2' stroke-dasharray='0 0'/></svg>", 120, 40);
        EXPECT(isRed(zeroLine, 12, 4));
        SvgFrame shiftedLine = render(
            "<svg width='40' height='8'><line x1='0' y1='4' x2='40' y2='4' stroke='red' stroke-width='2' stroke-dasharray='10 10' stroke-dashoffset='10'/></svg>", 120, 40);
        EXPECT(isWhite(shiftedLine, 2, 4));
        EXPECT(isRed(shiftedLine, 12, 4));
        SvgFrame solidRect = render("<svg width='40' height='8'><rect width='40' height='8' fill='none' stroke='red' stroke-width='2'/></svg>", 120, 40);
        SvgFrame dashedRect = render(
            "<svg width='40' height='8'><rect width='40' height='8' fill='none' stroke='red' stroke-width='2' stroke-dasharray='6 6'/></svg>", 120, 40);
        EXPECT(redPixels(dashedRect) > 0 && redPixels(dashedRect) < redPixels(solidRect));
        SvgFrame solidCircle = render("<svg width='20' height='20'><circle cx='10' cy='10' r='8' fill='none' stroke='red' stroke-width='2'/></svg>", 80, 48);
        SvgFrame dashedCircle = render(
            "<svg width='20' height='20'><circle cx='10' cy='10' r='8' fill='none' stroke='red' stroke-width='2' stroke-dasharray='4 6'/></svg>", 80, 48);
        EXPECT(redPixels(dashedCircle) > 0 && redPixels(dashedCircle) < redPixels(solidCircle));
        SvgFrame dashedPath = render(
            "<svg width='40' height='8'><path d='M2 4 L38 4' fill='none' stroke='red' stroke-width='2' stroke-dasharray='6 6'/></svg>", 120, 40);
        EXPECT(redPixels(dashedPath) > 0 && redPixels(dashedPath) < redPixels(solidLine));
    }

    // The affine transform stack, including skew and combined lists.
    {
        SvgFrame translated = render("<svg width='40' height='20'><rect width='10' height='10' fill='red' transform='translate(20 2)'/></svg>", 120, 48);
        EXPECT(isWhite(translated, 4, 4));
        EXPECT(isRed(translated, 24, 6));
        SvgFrame matrixTranslated = render("<svg width='40' height='20'><rect width='10' height='10' fill='red' transform='matrix(1 0 0 1 20 2)'/></svg>", 120, 48);
        EXPECT(isWhite(matrixTranslated, 4, 4));
        EXPECT(isRed(matrixTranslated, 24, 6));
        SvgFrame scaled = render("<svg width='40' height='20'><rect width='10' height='10' fill='red' transform='scale(2)'/></svg>", 120, 48);
        EXPECT(isRed(scaled, 18, 18));
        SvgFrame skewed = render("<svg width='40' height='20'><rect width='10' height='10' fill='red' transform='skewX(45)'/></svg>", 120, 48);
        EXPECT(isWhite(skewed, 2, 9));
        EXPECT(isRed(skewed, 14, 9));
        SvgFrame skewedY = render("<svg width='40' height='20'><rect width='10' height='10' fill='red' transform='skewY(45)'/></svg>", 120, 48);
        EXPECT(isWhite(skewedY, 9, 4));
        EXPECT(isRed(skewedY, 9, 14));
        SvgFrame rotatedSquare = render("<svg width='20' height='20'><rect width='10' height='10' fill='red' transform='rotate(90 5 5)'/></svg>", 80, 48);
        EXPECT(isRed(rotatedSquare, 1, 1) && isRed(rotatedSquare, 5, 5) && isRed(rotatedSquare, 8, 8));
        SvgFrame combined = render("<svg width='40' height='20'><rect width='10' height='10' fill='red' transform='translate(10 0) scale(2)'/></svg>", 120, 48);
        EXPECT(isWhite(combined, 4, 4));
        EXPECT(isRed(combined, 14, 4));
        EXPECT(isWhite(combined, 34, 4));
    }

    // Presentation attributes are the weakest source; stylesheets and inline
    // styles override them, and !important overrides a normal inline style.
    {
        SvgFrame ruleWins = render(
            "<style>rect.hot { fill: lime }</style><svg width='20' height='20'><rect class='hot' width='20' height='20' fill='red'/></svg>", 80, 48);
        EXPECT(isGreen(ruleWins, 10, 10));
        SvgFrame cssGreen = render("<svg width='20' height='20'><rect width='20' height='20' fill='green'/></svg>", 80, 48);
        EXPECT(matches(cssGreen, 10, 10, 0, 128, 0, 2));
        SvgFrame inlineWins = render(
            "<style>rect.hot { fill: lime }</style><svg width='20' height='20'><rect class='hot' style='fill: blue' width='20' height='20' fill='red'/></svg>", 80, 48);
        EXPECT(isBlue(inlineWins, 10, 10));
        SvgFrame importantWins = render(
            "<style>rect.hot { fill: lime !important }</style><svg width='20' height='20'><rect class='hot' style='fill: blue' width='20' height='20' fill='red'/></svg>",
            80, 48);
        EXPECT(isGreen(importantWins, 10, 10));
        SvgFrame attributeDefault = render("<svg width='20' height='20'><rect width='20' height='20' fill='red'/></svg>", 80, 48);
        EXPECT(isRed(attributeDefault, 10, 10));
        SvgFrame strokeRule = render(
            "<style>rect { stroke: lime; stroke-width: 4 }</style><svg width='20' height='20'><rect width='20' height='20' fill='none' stroke='red' stroke-width='1'/></svg>",
            80, 48);
        EXPECT(isGreen(strokeRule, 10, 1));
        SvgFrame hiddenShape = render(
            "<style>rect { visibility: hidden }</style><svg width='20' height='20'><rect width='20' height='20' fill='red'/></svg>", 80, 48);
        EXPECT(isWhite(hiddenShape, 10, 10));
        SvgFrame displayNone = render("<svg width='20' height='20'><rect width='20' height='20' fill='red' display='none'/></svg>", 80, 48);
        EXPECT(isWhite(displayNone, 10, 10));
    }

    // Gradient transforms reshape the gradient axis in gradient space.
    {
        SvgFrame plainGradient = render(
            "<svg width='20' height='20'><defs><linearGradient id='g'><stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient></defs>"
            "<rect width='20' height='20' fill='url(#g)'/></svg>",
            80, 48);
        EXPECT(pixelRed(plainGradient, 1, 10) > pixelBlue(plainGradient, 1, 10));
        EXPECT(pixelBlue(plainGradient, 18, 10) > pixelRed(plainGradient, 18, 10));
        EXPECT(pixelBlue(plainGradient, 1, 1) < 200);
        SvgFrame turnedGradient = render(
            "<svg width='20' height='20'><defs><linearGradient id='g' gradientTransform='rotate(90 0.5 0.5)'><stop offset='0' stop-color='red'/>"
            "<stop offset='1' stop-color='blue'/></linearGradient></defs><rect width='20' height='20' fill='url(#g)'/></svg>",
            80, 48);
        EXPECT(pixelRed(turnedGradient, 10, 1) > pixelBlue(turnedGradient, 10, 1));
        EXPECT(pixelBlue(turnedGradient, 10, 18) > pixelRed(turnedGradient, 10, 18));
        SvgFrame plainRadial = render(
            "<svg width='20' height='20'><defs><radialGradient id='r'><stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></radialGradient></defs>"
            "<rect width='20' height='20' fill='url(#r)'/></svg>",
            80, 48);
        EXPECT(pixelBlue(plainRadial, 18, 18) > pixelRed(plainRadial, 18, 18));
        SvgFrame stretchedRadial = render(
            "<svg width='20' height='20'><defs><radialGradient id='r' gradientTransform='scale(2 2)'><stop offset='0' stop-color='red'/>"
            "<stop offset='1' stop-color='blue'/></radialGradient></defs><rect width='20' height='20' fill='url(#r)'/></svg>",
            80, 48);
        EXPECT(pixelRed(stretchedRadial, 18, 18) > pixelRed(plainRadial, 18, 18));
    }

    // Marker orientation and the markerUnits scaling.
    {
        SvgFrame autoMarkers = render(
            "<svg width='40' height='40'><defs><marker id='m' orient='auto' markerWidth='6' markerHeight='6' refX='3' refY='3'>"
            "<rect width='6' height='3' fill='red'/></marker></defs><line x1='4' y1='4' x2='36' y2='36' stroke='black' marker-end='url(#m)'/></svg>",
            120, 80);
        SvgFrame fixedMarkers = render(
            "<svg width='40' height='40'><defs><marker id='m' orient='0' markerWidth='6' markerHeight='6' refX='3' refY='3'>"
            "<rect width='6' height='3' fill='red'/></marker></defs><line x1='4' y1='4' x2='36' y2='36' stroke='black' marker-end='url(#m)'/></svg>",
            120, 80);
        EXPECT(redPixels(autoMarkers) > 0);
        EXPECT(redPixels(fixedMarkers) > 0);
        EXPECT(differs(autoMarkers, fixedMarkers));
        SvgFrame strokeUnitMarker = render(
            "<svg width='40' height='10'><defs><marker id='m' markerWidth='4' markerHeight='4' markerUnits='strokeWidth'>"
            "<rect x='-4' y='-2' width='4' height='4' fill='red'/></marker></defs>"
            "<line x1='0' y1='5' x2='30' y2='5' stroke='black' stroke-width='3' marker-end='url(#m)'/></svg>",
            120, 40);
        SvgFrame userUnitMarker = render(
            "<svg width='40' height='10'><defs><marker id='m' markerWidth='4' markerHeight='4' markerUnits='userSpaceOnUse'>"
            "<rect x='-4' y='-2' width='4' height='4' fill='red'/></marker></defs>"
            "<line x1='0' y1='5' x2='30' y2='5' stroke='black' stroke-width='3' marker-end='url(#m)'/></svg>",
            120, 40);
        EXPECT(isRed(strokeUnitMarker, 20, 5));
        EXPECT(!isRed(userUnitMarker, 20, 5));
        EXPECT(redPixels(strokeUnitMarker) > redPixels(userUnitMarker));
    }

    // clip-path masking: shapes, transforms, nesting, units and text.
    {
        SvgFrame circleClip = render(
            "<svg width='20' height='20'><defs><clipPath id='c'><circle cx='10' cy='10' r='6'/></clipPath></defs>"
            "<rect width='20' height='20' fill='red' clip-path='url(#c)'/></svg>",
            80, 48);
        EXPECT(isRed(circleClip, 10, 10));
        EXPECT(isRed(circleClip, 10, 5));
        EXPECT(isWhite(circleClip, 1, 1));
        EXPECT(isWhite(circleClip, 19, 19));
        SvgFrame pathClip = render(
            "<svg width='20' height='20'><defs><clipPath id='c'><path d='M0 0 L20 0 L10 20 Z'/></clipPath></defs>"
            "<rect width='20' height='20' fill='red' clip-path='url(#c)'/></svg>",
            80, 48);
        EXPECT(isRed(pathClip, 10, 4));
        EXPECT(isWhite(pathClip, 1, 18));
        EXPECT(isWhite(pathClip, 19, 18));
        SvgFrame polygonClip = render(
            "<svg width='20' height='20'><defs><clipPath id='c'><polygon points='0,10 10,0 20,10 10,20'/></clipPath></defs>"
            "<rect width='20' height='20' fill='red' clip-path='url(#c)'/></svg>",
            80, 48);
        EXPECT(isRed(polygonClip, 10, 10));
        EXPECT(isWhite(polygonClip, 0, 0));
        SvgFrame transformedClip = render(
            "<svg width='20' height='20'><defs><clipPath id='c'><rect width='10' height='20' transform='translate(10 0)'/></clipPath></defs>"
            "<rect width='20' height='20' fill='red' clip-path='url(#c)'/></svg>",
            80, 48);
        EXPECT(isWhite(transformedClip, 5, 10));
        EXPECT(isRed(transformedClip, 15, 10));
        SvgFrame clipPathTransform = render(
            "<svg width='20' height='20'><defs><clipPath id='c' transform='translate(10 0)'><rect width='10' height='20'/></clipPath></defs>"
            "<rect width='20' height='20' fill='red' clip-path='url(#c)'/></svg>",
            80, 48);
        EXPECT(isWhite(clipPathTransform, 5, 10));
        EXPECT(isRed(clipPathTransform, 15, 10));
        SvgFrame nestedClip = render(
            "<svg width='20' height='20'><defs><clipPath id='top'><rect width='20' height='10'/></clipPath>"
            "<clipPath id='left'><rect width='10' height='20'/></clipPath></defs>"
            "<g clip-path='url(#top)'><rect width='20' height='20' fill='red' clip-path='url(#left)'/></g></svg>",
            80, 48);
        EXPECT(isRed(nestedClip, 5, 5));
        EXPECT(isWhite(nestedClip, 15, 5));
        EXPECT(isWhite(nestedClip, 5, 15));
        SvgFrame emptyClip = render(
            "<svg width='20' height='20'><defs><clipPath id='c'/></defs><rect width='20' height='20' fill='red' clip-path='url(#c)'/></svg>", 80, 48);
        EXPECT(isWhite(emptyClip, 10, 10));
        SvgFrame bogusClip = render(
            "<svg width='20' height='20'><defs><rect id='c' width='4' height='4'/></defs>"
            "<rect width='20' height='20' fill='red' clip-path='url(#c)'/></svg>",
            80, 48);
        EXPECT(isRed(bogusClip, 19, 19));
        SvgFrame boundingClip = render(
            "<svg width='20' height='20'><defs><clipPath id='c' clipPathUnits='objectBoundingBox'><circle cx='0.5' cy='0.5' r='0.5'/></clipPath></defs>"
            "<rect width='20' height='20' fill='red' clip-path='url(#c)'/></svg>",
            80, 48);
        EXPECT(isRed(boundingClip, 10, 10));
        EXPECT(isWhite(boundingClip, 0, 0));
        SvgFrame groupBoundingClip = render(
            "<svg width='20' height='20'><defs><clipPath id='c' clipPathUnits='objectBoundingBox'><rect width='0.5' height='1'/></clipPath></defs>"
            "<g clip-path='url(#c)'><rect width='20' height='20' fill='red'/></g></svg>",
            80, 48);
        EXPECT(isRed(groupBoundingClip, 4, 10));
        EXPECT(isWhite(groupBoundingClip, 15, 10));
        SvgFrame clippedStroke = render(
            "<svg width='20' height='20'><defs><clipPath id='c'><rect width='10' height='20'/></clipPath></defs>"
            "<circle cx='10' cy='10' r='8' fill='none' stroke='red' stroke-width='3' clip-path='url(#c)'/></svg>",
            80, 48);
        EXPECT(isRed(clippedStroke, 2, 10));
        EXPECT(isWhite(clippedStroke, 18, 10));
        SvgFrame groupClip = render(
            "<svg width='20' height='20'><defs><clipPath id='c'><rect width='10' height='20'/></clipPath></defs>"
            "<g clip-path='url(#c)'><rect width='20' height='20' fill='red'/><circle cx='16' cy='10' r='4' fill='blue'/></g></svg>",
            80, 48);
        EXPECT(isRed(groupClip, 4, 10));
        EXPECT(isWhite(groupClip, 15, 10));
        SvgFrame openText = render("<svg width='40' height='20'><text x='0' y='14' font-size='14' fill='red'>MMMM</text></svg>", 120, 48);
        SvgFrame clippedText = render(
            "<svg width='40' height='20'><defs><clipPath id='c'><rect width='8' height='20'/></clipPath></defs>"
            "<text x='0' y='14' font-size='14' fill='red' clip-path='url(#c)'>MMMM</text></svg>",
            120, 48);
        EXPECT(redPixels(openText) > 0);
        EXPECT(redPixels(clippedText) > 0);
        EXPECT(redPixels(clippedText) < redPixels(openText));
    }

    // Exact stroke geometry: the stroke is an outline of the path, so joins,
    // caps and dash runs land on real Euclidean measurements.
    {
        SvgFrame miterJoin = render(
            "<svg width='20' height='20'><path d='M2 6 H10 V14' fill='none' stroke='red' stroke-width='8' stroke-linejoin='miter'/></svg>",
            80, 48);
        SvgFrame bevelJoin = render(
            "<svg width='20' height='20'><path d='M2 6 H10 V14' fill='none' stroke='red' stroke-width='8' stroke-linejoin='bevel'/></svg>",
            80, 48);
        SvgFrame roundJoin = render(
            "<svg width='20' height='20'><path d='M2 6 H10 V14' fill='none' stroke='red' stroke-width='8' stroke-linejoin='round'/></svg>",
            80, 48);
        // The 90° corner at (10,6) miters to the offset rectangle corner
        // (14,2); a bevel cuts across at (10,2)-(14,6) and a round join keeps
        // only the quarter disc of radius 4 around the vertex.
        EXPECT(isRed(miterJoin, 13, 2));
        EXPECT(isRed(miterJoin, 13, 3));
        EXPECT(isWhite(miterJoin, 15, 3));
        EXPECT(isWhite(bevelJoin, 13, 3));
        EXPECT(isRed(bevelJoin, 11, 4));
        EXPECT(isWhite(bevelJoin, 13, 4));
        EXPECT(pixelRed(roundJoin, 12, 3) > 200 && pixelGreen(roundJoin, 12, 3) < 60);
        EXPECT(pixelGreen(roundJoin, 13, 3) > 190);
        SvgFrame miterClip = render(
            "<svg width='48' height='40'><polyline points='4,28 20,16 36,28' fill='none' stroke='red' stroke-width='8' stroke-miterlimit='1' stroke-linejoin='miter-clip'/></svg>",
            120, 80);
        SvgFrame miterBevel = render(
            "<svg width='48' height='40'><polyline points='4,28 20,16 36,28' fill='none' stroke='red' stroke-width='8' stroke-miterlimit='1' stroke-linejoin='bevel'/></svg>",
            120, 80);
        // miterlimit 1 clips the 5-unit spike at 4 units: row 12 is inside the
        // clipped quad, row 11 stays empty just as it does for a bevel.
        EXPECT(pixelRed(miterClip, 20, 12) > 200 && pixelGreen(miterClip, 20, 12) < 110);
        EXPECT(isWhite(miterClip, 19, 11));
        EXPECT(pixelGreen(miterBevel, 20, 12) > 150);
    }
    {
        SvgFrame buttCap = render(
            "<svg width='30' height='20'><line x1='8' y1='10' x2='20' y2='10' stroke='red' stroke-width='6'/></svg>", 80, 48);
        SvgFrame squareCap = render(
            "<svg width='30' height='20'><line x1='8' y1='10' x2='20' y2='10' stroke='red' stroke-width='6' stroke-linecap='square'/></svg>",
            80, 48);
        SvgFrame roundCap = render(
            "<svg width='30' height='20'><line x1='8' y1='10' x2='20' y2='10' stroke='red' stroke-width='6' stroke-linecap='round'/></svg>",
            80, 48);
        EXPECT(isRed(buttCap, 8, 10));
        EXPECT(isWhite(buttCap, 5, 10));
        EXPECT(isRed(squareCap, 5, 10));
        EXPECT(isWhite(squareCap, 4, 10));
        EXPECT(pixelRed(roundCap, 5, 10) > 200 && pixelGreen(roundCap, 5, 10) < 60);
        EXPECT(isWhite(roundCap, 4, 10));
        SvgFrame hairline = render(
            "<svg width='20' height='20'><line x1='2' y1='10' x2='18' y2='10' stroke='red'/></svg>", 80, 48);
        SvgFrame portrait = render(
            "<svg width='20' height='20'><line x1='10' y1='2' x2='10' y2='18' stroke='red'/></svg>", 80, 48);
        SvgFrame invisible = render(
            "<svg width='20' height='20'><line x1='2' y1='10' x2='18' y2='10' stroke='red' stroke-width='0'/></svg>", 80, 48);
        // A unit stroke straddles the line it follows, so a browser paints two
        // rows (or columns) at half coverage each, and nothing beyond them.
        EXPECT(pixelGreen(hairline, 10, 9) > 100 && pixelGreen(hairline, 10, 9) < 160);
        EXPECT(pixelGreen(hairline, 10, 10) > 100 && pixelGreen(hairline, 10, 10) < 160);
        EXPECT(isWhite(hairline, 10, 8));
        EXPECT(isWhite(hairline, 10, 11));
        EXPECT(pixelGreen(portrait, 9, 10) > 100 && pixelGreen(portrait, 9, 10) < 160);
        EXPECT(pixelGreen(portrait, 10, 10) > 100 && pixelGreen(portrait, 10, 10) < 160);
        EXPECT(isWhite(portrait, 8, 10));
        EXPECT(isWhite(portrait, 11, 10));
        EXPECT(isWhite(invisible, 10, 10));
    }
    {
        // Coverage antialiasing: an edge blends by the fraction of the pixel the
        // shape covers, so hard edges become the soft ones a browser paints.
        SvgFrame unitLine = render("<svg width='20' height='20'><line x1='2' y1='10' x2='18' y2='10' stroke='red'/></svg>", 80, 48);
        // A unit stroke straddles its line: half coverage on each side row.
        EXPECT(pixelGreen(unitLine, 10, 9) > 120 && pixelGreen(unitLine, 10, 9) < 136);
        EXPECT(pixelGreen(unitLine, 10, 10) > 120 && pixelGreen(unitLine, 10, 10) < 136);
        EXPECT(isWhite(unitLine, 10, 8));
        EXPECT(isWhite(unitLine, 10, 11));
        // crispEdges is the one rendering mode that snaps the same stroke onto
        // a single row instead of antialiasing it.
        SvgFrame crispLine = render(
            "<svg width='20' height='20'><line x1='2' y1='10' x2='18' y2='10' stroke='red' shape-rendering='crispEdges'/></svg>",
            80, 48);
        EXPECT(isRed(crispLine, 10, 10));
        EXPECT(isWhite(crispLine, 10, 9));
        EXPECT(isWhite(crispLine, 10, 11));
        // A fractional rectangle edge covers half of its first row.
        SvgFrame fractional = render("<svg width='12' height='20'><rect y='5.5' width='12' height='8' fill='red'/></svg>", 80, 48);
        EXPECT(pixelGreen(fractional, 6, 5) > 120 && pixelGreen(fractional, 6, 5) < 136);
        EXPECT(isRed(fractional, 6, 6));
        EXPECT(isRed(fractional, 6, 12));
        EXPECT(pixelGreen(fractional, 6, 13) > 120 && pixelGreen(fractional, 6, 13) < 136);
        EXPECT(isWhite(fractional, 6, 14));
        // A circle blends its rim: each pixel row is covered by the ellipse
        // chord that crosses it.
        SvgFrame rimCircle = render("<svg width='20' height='20'><circle cx='10' cy='10' r='6' fill='red'/></svg>", 80, 48);
        EXPECT(isRed(rimCircle, 10, 10));
        EXPECT(isWhite(rimCircle, 1, 1));
        EXPECT(pixelGreen(rimCircle, 5, 6) > 0 && pixelGreen(rimCircle, 5, 6) < 120);
        EXPECT(pixelGreen(rimCircle, 4, 8) > 0 && pixelGreen(rimCircle, 4, 8) < 120);
        // The exact Gaussian blur of a box: a pixel's alpha is the product of
        // its two edge profiles, 0.5*erfc(distance / (sigma*sqrt(2))).
        SvgFrame softBlur = render(
            "<svg width='30' height='16'><defs><filter id='aaSoft'><feGaussianBlur stdDeviation='4'/></filter></defs>"
            "<rect width='8' height='16' fill='red' filter='url(#aaSoft)'/></svg>",
            140, 80);
        const double blurSigma = 4.0;
        auto gaussianCdf = [](double value) { return 0.5 * std::erfc(-value / std::sqrt(2.0)); };
        auto boxCoverage = [&](double x, double y) {
            return (gaussianCdf((8.0 - x) / blurSigma) - gaussianCdf((0.0 - x) / blurSigma)) *
                   (gaussianCdf((16.0 - y) / blurSigma) - gaussianCdf((0.0 - y) / blurSigma));
        };
        auto blurredAlpha = [&](int x, int y) { return 255.0 - pixelGreen(softBlur, x, y); };
        EXPECT(std::fabs(blurredAlpha(2, 4) - 255.0 * boxCoverage(2.5, 4.5)) < 3);
        EXPECT(std::fabs(blurredAlpha(4, 8) - 255.0 * boxCoverage(4.5, 8.5)) < 3);
        EXPECT(std::fabs(blurredAlpha(10, 4) - 255.0 * boxCoverage(10.5, 4.5)) < 3);
    }
    {
        // Adaptive curve flattening: a quadratic that spans the whole viewport
        // is subdivided finely enough to follow the true curve, where a fixed
        // eight-segment outline would cut the corner by many pixels.
        SvgFrame curve = render("<svg width='1000' height='1000' viewBox='0 0 1000 1000'>"
                                "<path d='M0 0 Q500 1000 1000 0 L1000 1000 L0 1000 Z' fill='red'/></svg>",
                                1100, 1050);
        // The curve passes through (187.5, 304.7) at that column.
        EXPECT(isWhite(curve, 187, 290));
        EXPECT(isWhite(curve, 187, 297));
        EXPECT(pixelRed(curve, 187, 304) > 200 && pixelGreen(curve, 187, 304) > 120);
        EXPECT(isRed(curve, 187, 308));
        // The same path scaled down to 200 px keeps its smooth outline.
        SvgFrame scaledCurve = render("<svg width='200' height='200' viewBox='0 0 1000 1000'>"
                                      "<path d='M0 0 Q500 1000 1000 0 L1000 1000 L0 1000 Z' fill='red'/></svg>",
                                      300, 260);
        EXPECT(isWhite(scaledCurve, 37, 58));
        EXPECT(isRed(scaledCurve, 37, 62));
        EXPECT(isWhite(scaledCurve, 100, 96));
        EXPECT(isRed(scaledCurve, 100, 100));
    }
    {
        // SVG 2 geometry properties are CSS properties: a stylesheet beats the
        // presentation attribute and an inline style beats the stylesheet.
        RoboWeb::Diagnostics geometryDiagnostics;
        Parse("<style>circle { cx: 12; cy: 10; r: 6 }</style><svg width='20' height='20'><circle fill='red' cx='2' cy='2' r='2'/></svg>", geometryDiagnostics);
        EXPECT(!geometryDiagnostics.HasErrors());
        SvgFrame cssCircle = render(
            "<style>circle { cx: 12; cy: 10; r: 6 }</style><svg width='20' height='20'><circle fill='red' cx='2' cy='2' r='2'/></svg>", 80, 48);
        EXPECT(isRed(cssCircle, 12, 10));
        EXPECT(isWhite(cssCircle, 2, 2));
        SvgFrame cssPath = render(
            "<style>path { d: path('M2 2 H18 V18 H2 Z') }</style><svg width='20' height='20'><path fill='red'/></svg>", 80, 48);
        EXPECT(isRed(cssPath, 10, 10));
        EXPECT(isWhite(cssPath, 0, 0));
        SvgFrame inlineRect = render(
            "<style>rect { width: 4 }</style><svg width='20' height='20'><rect width='20' height='20' style='width:10px' fill='red'/></svg>",
            80, 48);
        EXPECT(isRed(inlineRect, 8, 10));
        EXPECT(isWhite(inlineRect, 12, 10));
    }
    {
        // Diagonal dashes are measured along the line itself: 10 units of
        // length cover 7.07 pixels of each axis, not 10.
        SvgFrame diagonal = render(
            "<svg width='40' height='40'><line x1='0' y1='0' x2='40' y2='40' stroke='red' stroke-width='2' stroke-dasharray='10 10'/></svg>",
            120, 80);
        EXPECT(isRed(diagonal, 2, 2));
        EXPECT(isWhite(diagonal, 10, 10));
        EXPECT(isRed(diagonal, 16, 16));
        EXPECT(isWhite(diagonal, 26, 26));
        SvgFrame cornerDash = render(
            "<svg width='30' height='30'><path d='M2 6 H20 V20' fill='none' stroke='red' stroke-width='8' stroke-dasharray='22 10'/></svg>",
            80, 48);
        SvgFrame brokenCorner = render(
            "<svg width='30' height='30'><path d='M2 6 H20 V20' fill='none' stroke='red' stroke-width='8' stroke-dasharray='16 10'/></svg>",
            80, 48);
        // One run crosses the corner, so its miter join is painted; when the
        // gap falls on the corner the join disappears with the dash.
        EXPECT(isRed(cornerDash, 22, 4));
        EXPECT(isRed(cornerDash, 20, 8));
        EXPECT(isWhite(cornerDash, 20, 16));
        EXPECT(isRed(brokenCorner, 14, 6));
        EXPECT(isWhite(brokenCorner, 22, 4));
        EXPECT(isRed(brokenCorner, 20, 14));
        SvgFrame dots = render(
            "<svg width='30' height='20'><line x1='2' y1='10' x2='22' y2='10' stroke='red' stroke-width='4' stroke-linecap='round' stroke-dasharray='0 10'/></svg>",
            80, 48);
        EXPECT(isRed(dots, 2, 10));
        EXPECT(isRed(dots, 12, 10));
        EXPECT(isRed(dots, 22, 10));
        EXPECT(isWhite(dots, 7, 10));
    }

    // Gradient templates: geometry, units, transform and spreadMethod travel
    // through the href chain, and spreadMethod applies to radial gradients too.
    {
        SvgFrame inherited = render(
            "<svg width='20' height='20'><defs>"
            "<linearGradient id='base' x1='0' y1='0' x2='0' y2='1'>"
            "<stop offset='0' stop-color='#ff0000'/><stop offset='0.25' stop-color='#ff0000'/>"
            "<stop offset='0.75' stop-color='#0000ff'/><stop offset='1' stop-color='#0000ff'/>"
            "</linearGradient>"
            "<linearGradient id='derived' xlink:href='#base'/>"
            "</defs><rect width='20' height='20' fill='url(#derived)'/></svg>",
            80, 48);
        EXPECT(isRed(inherited, 10, 2));
        EXPECT(isBlue(inherited, 10, 17));
        SvgFrame radialRepeat = render(
            "<svg width='40' height='40'><defs><radialGradient id='r' spreadMethod='repeat' cx='0.5' cy='0.5' r='0.25'>"
            "<stop offset='0' stop-color='#ff0000'/><stop offset='1' stop-color='#0000ff'/></radialGradient></defs>"
            "<rect width='40' height='40' fill='url(#r)'/></svg>",
            120, 48);
        SvgFrame radialReflect = render(
            "<svg width='40' height='40'><defs><radialGradient id='r' spreadMethod='reflect' cx='0.5' cy='0.5' r='0.25'>"
            "<stop offset='0' stop-color='#ff0000'/><stop offset='1' stop-color='#0000ff'/></radialGradient></defs>"
            "<rect width='40' height='40' fill='url(#r)'/></svg>",
            120, 48);
        SvgFrame radialPad = render(
            "<svg width='40' height='40'><defs><radialGradient id='r' cx='0.5' cy='0.5' r='0.25'>"
            "<stop offset='0' stop-color='#ff0000'/><stop offset='1' stop-color='#0000ff'/></radialGradient></defs>"
            "<rect width='40' height='40' fill='url(#r)'/></svg>",
            120, 48);
        // Pixel (7,20) sits 1.25-1.3 radii from the centre: repeat keeps the
        // fraction, reflect mirrors it, and pad clamps to the last stop.
        EXPECT(matches(radialRepeat, 7, 20, 179, 0, 77, 30));
        EXPECT(matches(radialReflect, 7, 20, 77, 0, 179, 30));
        EXPECT(matches(radialPad, 7, 20, 0, 0, 255, 2));
    }

    // Masks render their whole content (not only rectangles), apply to every
    // painted element, and honour the luminance and alpha mask types.
    {
        SvgFrame circleMask = render(
            "<svg width='20' height='20'><defs><mask id='m'><circle cx='10' cy='10' r='6' fill='#ffffff'/></mask></defs>"
            "<rect width='20' height='20' fill='#ff0000' mask='url(#m)'/></svg>",
            80, 48);
        EXPECT(isRed(circleMask, 10, 10));
        EXPECT(isWhite(circleMask, 2, 2));
        EXPECT(isWhite(circleMask, 18, 2));
        SvgFrame circleMasked = render(
            "<svg width='20' height='20'><defs><mask id='m'><rect width='10' height='20' fill='#ffffff'/></mask></defs>"
            "<circle cx='10' cy='10' r='8' fill='#ff0000' mask='url(#m)'/></svg>",
            80, 48);
        EXPECT(isRed(circleMasked, 4, 10));
        EXPECT(isWhite(circleMasked, 16, 10));
        SvgFrame alphaMask = render(
            "<svg width='20' height='20'><defs><mask id='m' mask-type='alpha'><rect width='20' height='20' fill='rgba(0, 0, 0, 0.5)'/></mask></defs>"
            "<rect width='20' height='20' fill='#ff0000' mask='url(#m)'/></svg>",
            80, 48);
        EXPECT(matches(alphaMask, 10, 10, 255, 128, 128, 14));
        SvgFrame grayMask = render(
            "<svg width='20' height='20'><defs><mask id='m'><rect width='20' height='20' fill='#808080'/></mask></defs>"
            "<rect width='20' height='20' fill='#ff0000' mask='url(#m)'/></svg>",
            80, 48);
        // Luminance of #808080 is about 0.216, so only that fraction of the
        // red paint survives over the white background.
        EXPECT(matches(grayMask, 10, 10, 255, 200, 200, 14));
    }
}

void TestXml() {
    RoboWeb::Diagnostics diagnostics;
    RoboWeb::XmlParseResult parsed = RoboWeb::ParseXml(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><!--note--><message id=\"m\"><warning>Hello &amp; World</warning></message>",
        "fixture.xml", diagnostics);
    EXPECT(parsed.wellFormed);
    EXPECT(diagnostics.ErrorCount() == 0);
    const std::string serialized = RoboWeb::SerializeXml(*parsed.document.root);
    EXPECT(serialized.find("Hello &amp; World") != std::string::npos);
    EXPECT(serialized.find("<warning>") != std::string::npos);
    RoboWeb::Diagnostics brokenDiagnostics;
    RoboWeb::XmlParseResult broken = RoboWeb::ParseXml("<message></warning>", "fixture.xml", brokenDiagnostics);
    EXPECT(!broken.wellFormed);
    EXPECT(brokenDiagnostics.ErrorCount() > 0);
    bool parserError = false;
    for (const std::unique_ptr<RoboWeb::Node>& child : broken.document.root->children) {
        if (child->tagName == "parsererror") {
            parserError = true;
        }
    }
    EXPECT(parserError);
    RoboWeb::Diagnostics entityDiagnostics;
    RoboWeb::XmlParseResult entity = RoboWeb::ParseXml(
        "<?xml version=\"1.0\"?><!DOCTYPE body [<!ENTITY warning \"Careful\"><!ATTLIST item id ID #IMPLIED><!ENTITY logo SYSTEM \"logo.png\" NDATA gif>]><body>&warning;<item id=\"one\">&#65;</item></body>",
        "fixture.xml", entityDiagnostics);
    EXPECT(entity.wellFormed);
    EXPECT(RoboWeb::SerializeText(*entity.document.root).find("Careful") != std::string::npos);
    EXPECT(RoboWeb::SerializeText(*entity.document.root).find("A") != std::string::npos);
    EXPECT(entity.unparsedEntities["logo"] == "logo.png");
    EXPECT(!entity.idAttributes.empty());
    RoboWeb::XmlParseResult marked = RoboWeb::ParseXml(
        "<root xml:lang=\"en-US\" xmlns:ex=\"urn:example\">A<![CDATA[<b>]]><!--hidden--><?pi yes?><item id=\"a\">2</item><item id=\"b\">3</item><ex:item>9</ex:item></root>",
        "fixture.xml", diagnostics);
    EXPECT(marked.wellFormed);
    RoboWeb::XPathEnvironment environment;
    environment.sourceRoot = marked.document.root.get();
    environment.idAttributes = marked.idAttributes;
    RoboWeb::XPathNode documentNode{marked.document.root.get(), false, "", ""};
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("count(/root/item)", documentNode, 1, 1, environment)) == 2);
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("string(/root/item[2])", documentNode, 1, 1, environment)) == "3");
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("sum(/root/item)", documentNode, 1, 1, environment)) == 5);
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("/root/item[1]/@id", documentNode, 1, 1, environment)) == "a");
    EXPECT(RoboWeb::XPathToBoolean(RoboWeb::EvaluateXPath("starts-with(/root/item[1], '2')", documentNode, 1, 1, environment)));
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("substring('12345', 2, 3)", documentNode, 1, 1, environment)) == "234");
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("substring-before('a-b', '-')", documentNode, 1, 1, environment)) == "a");
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("substring-after('a-b', '-')", documentNode, 1, 1, environment)) == "b");
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("translate('abc', 'ac', 'A')", documentNode, 1, 1, environment)) == "Ab");
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("normalize-space('  a   b  ')", documentNode, 1, 1, environment)) == "a b");
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("floor(1.9)", documentNode, 1, 1, environment)) == 1);
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("ceiling(1.1)", documentNode, 1, 1, environment)) == 2);
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("round(1.5)", documentNode, 1, 1, environment)) == 2);
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("concat('a', 'b')", documentNode, 1, 1, environment)) == "ab");
    EXPECT(RoboWeb::XPathToBoolean(RoboWeb::EvaluateXPath("contains('abc', 'b') and not(false())", documentNode, 1, 1, environment)));
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("choose(false(), 'a', 'b')", documentNode, 1, 1, environment)) == "b");
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("count(/root/item[1]/following-sibling::item)", documentNode, 1, 1, environment)) == 1);
    EXPECT(RoboWeb::XPathToBoolean(RoboWeb::EvaluateXPath("lang('en')", RoboWeb::XPathNode{marked.document.root->children[0].get(), false, "", ""}, 1, 1, environment)));
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("local-name(/root/ex:item)", documentNode, 1, 1, environment)) == "item");
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("namespace-uri(/root/ex:item)", documentNode, 1, 1, environment)) == "urn:example");
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("string-length('ab')", documentNode, 1, 1, environment)) == 2);
    EXPECT(RoboWeb::SerializeText(*marked.document.root).find("<b>") != std::string::npos);
    const std::string sheet =
        "<?xml version=\"1.0\"?>"
        "<xsl:stylesheet version=\"1.0\" xmlns:xsl=\"http://www.w3.org/1999/XSL/Transform\" xmlns:exsl=\"http://exslt.org/common\" xmlns:math=\"http://exslt.org/math\" xmlns:set=\"http://exslt.org/sets\" xmlns:str=\"http://exslt.org/strings\" xmlns:regexp=\"http://exslt.org/regular-expressions\">"
        "<xsl:output method=\"xml\"/>"
        "<xsl:strip-space elements=\"root\"/>"
        "<xsl:decimal-format decimal-separator=\".\" grouping-separator=\",\"/>"
        "<xsl:attribute-set name=\"box\"><xsl:attribute name=\"kind\">row</xsl:attribute></xsl:attribute-set>"
        "<xsl:key name=\"by-id\" match=\"item\" use=\"@id\"/>"
        "<xsl:variable name=\"label\" select=\"'Items'\"/>"
        "<xsl:template match=\"/\">"
        "<list kind=\"{$label}\">"
        "<xsl:attribute name=\"count\"><xsl:value-of select=\"count(root/item)\"/></xsl:attribute>"
        "<xsl:for-each select=\"root/item\"><xsl:sort select=\".\" data-type=\"number\" order=\"descending\"/>"
        "<row><xsl:value-of select=\".\"/></row></xsl:for-each>"
        "<xsl:if test=\"count(root/item) &gt; 1\"><many/></xsl:if>"
        "<xsl:choose><xsl:when test=\"root/item\"><yes/></xsl:when><xsl:otherwise><no/></xsl:otherwise></xsl:choose>"
        "<xsl:call-template name=\"extra\"><xsl:with-param name=\"n\" select=\"2\"/></xsl:call-template>"
        "<xsl:element name=\"built\" use-attribute-sets=\"box\"><xsl:value-of select=\"$label\"/></xsl:element>"
        "<xsl:value-of select=\"format-number(1234.5, '#,##0.00')\"/>"
        "<xsl:value-of select=\"key('by-id', 'b')\"/>"
        "<xsl:comment>note</xsl:comment>"
        "<xsl:processing-instruction name=\"mark\">go</xsl:processing-instruction>"
        "<xsl:for-each select=\"root/item[1]\"><xsl:number value=\"position()\" format=\"A\"/></xsl:for-each>"
        "<xsl:value-of select=\"exsl:object-type($label)\"/>"
        "<xsl:value-of select=\"math:max(root/item)\"/>"
        "<xsl:value-of select=\"math:min(root/item)\"/>"
        "<xsl:if test=\"set:has-same-node(root/item[1], root/item[1])\"><same/></xsl:if>"
        "<xsl:value-of select=\"str:concat(root/item)\"/>"
        "<xsl:value-of select=\"regexp:replace('a1b', '[0-9]', 'g', '#')\"/>"
        "<xsl:if test=\"regexp:test('abc', 'b')\"><hit/></xsl:if>"
        "<xsl:if test=\"function-available('count') and element-available('xsl:if')\"><ready/></xsl:if>"
        "<xsl:if test=\"system-property('xsl:version') = '1.0'\"><v1/></xsl:if>"
        "<xsl:message>hello</xsl:message>"
        "</list></xsl:template>"
        "<xsl:template name=\"extra\"><xsl:param name=\"n\"/>"
        "<extra><xsl:value-of select=\"$n\"/></extra></xsl:template>"
        "</xsl:stylesheet>";
    RoboWeb::XsltResult transformed = RoboWeb::TransformXml("<root><item id=\"a\">2</item><item id=\"b\">3</item></root>", sheet, "sheet.xsl");
    EXPECT(transformed.ok);
    EXPECT(transformed.output.find("<row>3</row>") != std::string::npos);
    EXPECT(transformed.output.find("<row>2</row>") != std::string::npos);
    EXPECT(transformed.output.find("<many/>") != std::string::npos);
    EXPECT(transformed.output.find("<yes/>") != std::string::npos);
    EXPECT(transformed.output.find("<extra>2</extra>") != std::string::npos);
    EXPECT(transformed.output.find("kind=\"row\"") != std::string::npos);
    EXPECT(transformed.output.find("1,234.50") != std::string::npos);
    EXPECT(transformed.output.find(">3<") != std::string::npos);
    EXPECT(transformed.output.find("<!--note-->") != std::string::npos);
    EXPECT(transformed.output.find("<?mark go?>") != std::string::npos);
    EXPECT(transformed.output.find(">A<") != std::string::npos || transformed.output.find("A") != std::string::npos);
    EXPECT(transformed.output.find("string") != std::string::npos);
    EXPECT(transformed.output.find("<same/>") != std::string::npos);
    EXPECT(transformed.output.find("23") != std::string::npos);
    EXPECT(transformed.output.find("a#b") != std::string::npos);
    EXPECT(transformed.output.find("<hit/>") != std::string::npos);
    EXPECT(transformed.output.find("<ready/>") != std::string::npos);
    EXPECT(transformed.output.find("<v1/>") != std::string::npos);
    EXPECT(!transformed.messages.empty());
    RoboWeb::Diagnostics linkDiagnostics;
    RoboWeb::XmlParseResult linked = RoboWeb::ParseXml(
        "<?xml-stylesheet type=\"text/xsl\" href=\"sheet.xsl\"?><root/>", "fixture.xml", linkDiagnostics);
    EXPECT(linked.wellFormed);
    EXPECT(linked.stylesheetLinks.size() == 1);
    EXPECT(linked.stylesheetLinks[0].second == "sheet.xsl");
    RoboWeb::Diagnostics externalDiagnostics;
    RoboWeb::ParseXml("<!DOCTYPE root [<!ENTITY ext SYSTEM \"http://example.invalid/secret\">]><root>&ext;</root>", "fixture.xml", externalDiagnostics);
    EXPECT(externalDiagnostics.ErrorCount() > 0);
    RoboWeb::Diagnostics twoDiagnostics;
    RoboWeb::XmlParseResult two = RoboWeb::ParseXml("<a/><b/>", "fixture.xml", twoDiagnostics);
    EXPECT(!two.wellFormed);
    environment.sourceRoot = entity.document.root.get();
    environment.idAttributes = entity.idAttributes;
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("string(id('one'))", documentNode, 1, 1, environment)) == "A");
    const std::string firstId = RoboWeb::XPathToString(RoboWeb::EvaluateXPath("generate-id(/root/item[1])", RoboWeb::XPathNode{marked.document.root.get(), false, "", ""}, 1, 1, environment));
    const std::string secondId = RoboWeb::XPathToString(RoboWeb::EvaluateXPath("generate-id(/root/item[2])", RoboWeb::XPathNode{marked.document.root.get(), false, "", ""}, 1, 1, environment));
    EXPECT(!firstId.empty());
    EXPECT(firstId != secondId);
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("5 div 2", documentNode, 1, 1, environment)) == 2.5);
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("5 mod 2", documentNode, 1, 1, environment)) == 1);
    EXPECT(RoboWeb::XPathToString(RoboWeb::EvaluateXPath("name(/root/item[1]/parent::*)", RoboWeb::XPathNode{marked.document.root.get(), false, "", ""}, 1, 1, environment)) == "root");
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("count(/root/item[1]/ancestor::root)", RoboWeb::XPathNode{marked.document.root.get(), false, "", ""}, 1, 1, environment)) == 1);
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("count(/root/item[1]/preceding-sibling::comment())", RoboWeb::XPathNode{marked.document.root.get(), false, "", ""}, 1, 1, environment)) == 1);
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("count(/root/descendant::item)", RoboWeb::XPathNode{marked.document.root.get(), false, "", ""}, 1, 1, environment)) == 2);
    EXPECT(RoboWeb::XPathToNumber(RoboWeb::EvaluateXPath("count(/root/namespace::*)", RoboWeb::XPathNode{marked.document.root.get(), false, "", ""}, 1, 1, environment)) >= 1);
    std::ofstream included("Build/included.xsl");
    included << "<?xml version=\"1.0\"?><xsl:stylesheet version=\"1.0\" xmlns:xsl=\"http://www.w3.org/1999/XSL/Transform\">"
                "<xsl:template name=\"from-file\"><file>yes</file></xsl:template></xsl:stylesheet>";
    included.close();
    std::ofstream imported("Build/imported.xsl");
    imported << "<?xml version=\"1.0\"?><xsl:stylesheet version=\"1.0\" xmlns:xsl=\"http://www.w3.org/1999/XSL/Transform\">"
                "<xsl:template match=\"item\">imported</xsl:template></xsl:stylesheet>";
    imported.close();
    const std::string extraSheet =
        "<?xml version=\"1.0\"?>"
        "<xsl:stylesheet version=\"1.0\" xmlns:xsl=\"http://www.w3.org/1999/XSL/Transform\" xmlns:out=\"http://www.w3.org/1999/XSL/Transform\" "
        "xmlns:exsl=\"http://exslt.org/common\" xmlns:math=\"http://exslt.org/math\" xmlns:set=\"http://exslt.org/sets\" "
        "xmlns:str=\"http://exslt.org/strings\" xmlns:regexp=\"http://exslt.org/regular-expressions\">"
        "<xsl:import href=\"imported.xsl\"/>"
        "<xsl:include href=\"included.xsl\"/>"
        "<xsl:namespace-alias stylesheet-prefix=\"out\" result-prefix=\"xsl\"/>"
        "<xsl:output method=\"xml\"/>"
        "<xsl:template match=\"/\">"
        "<wrap>"
        "<xsl:copy-of select=\"root/item[1]\"/>"
        "<xsl:apply-templates select=\"root/item[1]\"/>"
        "<xsl:call-template name=\"from-file\"/>"
        "<out:stylesheet/>"
        "<xsl:value-of select=\"string(set:difference(root/item, root/item[1]))\"/>"
        "<xsl:value-of select=\"math:highest(root/item)\"/>"
        "<xsl:value-of select=\"count(str:split('a-b', '-'))\"/>"
        "<xsl:value-of select=\"count(str:tokenize('a b', ' '))\"/>"
        "<xsl:value-of select=\"count(regexp:match('a1b2', '[0-9]'))\"/>"
        "<xsl:variable name=\"frag\"><piece>Z</piece></xsl:variable>"
        "<xsl:value-of select=\"exsl:node-set($frag)\"/>"
        "<xsl:unknown><xsl:fallback><fell/></xsl:fallback></xsl:unknown>"
        "</wrap></xsl:template>"
        "<xsl:template match=\"item\"><xsl:copy><xsl:value-of select=\"current()/@id\"/></xsl:copy><xsl:apply-imports/></xsl:template>"
        "</xsl:stylesheet>";
    RoboWeb::XsltResult extra = RoboWeb::TransformXml("<root><item id=\"a\">2</item><item id=\"b\">3</item></root>", extraSheet, "Build/main.xsl");
    EXPECT(extra.ok);
    EXPECT(extra.output.find("<item id=\"a\">2</item>") != std::string::npos);
    EXPECT(extra.output.find("<item>a</item>") != std::string::npos);
    EXPECT(extra.output.find("imported") != std::string::npos);
    EXPECT(extra.output.find("<file>yes</file>") != std::string::npos);
    EXPECT(extra.output.find("<xsl:stylesheet/>") != std::string::npos);
    EXPECT(extra.output.find(">3<") != std::string::npos || extra.output.find("32") != std::string::npos);
    EXPECT(extra.output.find("Z") != std::string::npos);
    EXPECT(extra.output.find("<fell/>") != std::string::npos);
    RoboWeb::XsltResult plain = RoboWeb::TransformXml(
        "<root><item>Hi</item></root>",
        "<?xml version=\"1.0\"?><xsl:stylesheet version=\"1.0\" xmlns:xsl=\"http://www.w3.org/1999/XSL/Transform\"><xsl:output method=\"text\"/>"
        "<xsl:template match=\"/\"><xsl:value-of select=\"root/item\"/></xsl:template></xsl:stylesheet>",
        "sheet.xsl");
    EXPECT(plain.ok);
    EXPECT(plain.output == "Hi");
    const std::string entitySource =
        "<?xml version=\"1.0\"?><!DOCTYPE body [<!ENTITY logo SYSTEM \"logo.png\" NDATA gif>]><body/>";
    RoboWeb::XsltResult entityUrl = RoboWeb::TransformXml(
        entitySource,
        "<?xml version=\"1.0\"?><xsl:stylesheet version=\"1.0\" xmlns:xsl=\"http://www.w3.org/1999/XSL/Transform\">"
        "<xsl:template match=\"/\"><xsl:value-of select=\"unparsed-entity-url('logo')\"/></xsl:template></xsl:stylesheet>",
        "sheet.xsl");
    EXPECT(entityUrl.output.find("logo.png") != std::string::npos);
}

void TestDiagnosticFormat() {
    RoboWeb::Diagnostics diagnostics;
    diagnostics.Error("page.html", 4, 2, "duplicate attribute 'id'");
    diagnostics.Warning("page.html", 5, 1, "element 'center' is obsolete");
    std::ostringstream output;
    diagnostics.Print(output);
    const std::string text = output.str();
    EXPECT(text.find("page.html:4:2: error: duplicate attribute 'id'") != std::string::npos);
    EXPECT(text.find("page.html:5:1: warning: element 'center' is obsolete") != std::string::npos);
}

RoboWeb::Node* FindKind(RoboWeb::Node& node, RoboWeb::ElementKind kind) {
    if (node.kind == kind) {
        return &node;
    }
    for (const std::unique_ptr<RoboWeb::Node>& child : node.children) {
        RoboWeb::Node* found = FindKind(*child, kind);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

bool SameFrame(const RoboWeb::Framebuffer& left, const RoboWeb::Framebuffer& right) {
    return left.width == right.width && left.height == right.height && left.pixels == right.pixels;
}

void TestFastFrame() {
    RoboWeb::Diagnostics diagnostics;
    RoboWeb::Document document = Parse("<!DOCTYPE html><button>Save</button><input value=\"ab\"><p>Plain</p>", diagnostics);
    RoboWeb::ResetFrameStatistics();
    RoboWeb::ViewCache cache;
    RoboWeb::AdvanceFrame(cache, document, 320, 200, RoboWeb::FrameCause::Viewport, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 1);
    EXPECT(RoboWeb::FrameStatistics().fullRasterCount == 1);
    EXPECT(RoboWeb::FrameStatistics().dirtyRasterCount == 0);
    RoboWeb::Node* button = FindKind(*document.root, RoboWeb::ElementKind::Button);
    EXPECT(button != nullptr);
    document.hover = button;
    RoboWeb::FrameUpdate hovered = RoboWeb::AdvanceFrame(cache, document, 320, 200, RoboWeb::FrameCause::Hover, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 1);
    EXPECT(RoboWeb::FrameStatistics().dirtyRasterCount == 1);
    EXPECT(RoboWeb::FrameStatistics().fullRasterCount == 1);
    EXPECT(hovered.paint && !hovered.full);
    RoboWeb::Document hoveredReference = Parse("<!DOCTYPE html><button>Save</button><input value=\"ab\"><p>Plain</p>", diagnostics);
    hoveredReference.hover = FindKind(*hoveredReference.root, RoboWeb::ElementKind::Button);
    EXPECT(SameFrame(cache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(hoveredReference, 320, 200), 0, true)));
    document.hover = nullptr;
    RoboWeb::AdvanceFrame(cache, document, 320, 200, RoboWeb::FrameCause::Hover, button, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 1);
    RoboWeb::Document idleReference = Parse("<!DOCTYPE html><button>Save</button><input value=\"ab\"><p>Plain</p>", diagnostics);
    EXPECT(SameFrame(cache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(idleReference, 320, 200), 0, true)));
    RoboWeb::Node* paragraph = FindKind(*document.root, RoboWeb::ElementKind::P);
    const int dirtyBeforePlain = RoboWeb::FrameStatistics().dirtyRasterCount;
    document.hover = paragraph;
    RoboWeb::FrameUpdate plain = RoboWeb::AdvanceFrame(cache, document, 320, 200, RoboWeb::FrameCause::Hover, nullptr, true);
    EXPECT(!plain.paint);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 1);
    EXPECT(RoboWeb::FrameStatistics().dirtyRasterCount == dirtyBeforePlain);
    document.hover = nullptr;
    RoboWeb::Node* field = FindKind(*document.root, RoboWeb::ElementKind::Input);
    document.focus = field;
    document.caretOn = true;
    RoboWeb::AdvanceFrame(cache, document, 320, 200, RoboWeb::FrameCause::Content, nullptr, true);
    const int layoutsAfterFocus = RoboWeb::FrameStatistics().layoutCount;
    document.caretOn = false;
    RoboWeb::FrameUpdate blink = RoboWeb::AdvanceFrame(cache, document, 320, 200, RoboWeb::FrameCause::Caret, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == layoutsAfterFocus);
    EXPECT(blink.paint && !blink.full);
    RoboWeb::Document caretReference = Parse("<!DOCTYPE html><button>Save</button><input value=\"ab\"><p>Plain</p>", diagnostics);
    caretReference.focus = FindKind(*caretReference.root, RoboWeb::ElementKind::Input);
    caretReference.caretOn = false;
    EXPECT(SameFrame(cache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(caretReference, 320, 200), 0, true)));
    RoboWeb::Document tall = Parse("<!DOCTYPE html><p>One</p><p>Two</p><p>Three</p><p>Four</p><p>Five</p><p>Six</p><p>Seven</p><p>Eight</p>", diagnostics);
    RoboWeb::ViewCache tallCache;
    RoboWeb::AdvanceFrame(tallCache, tall, 200, 40, RoboWeb::FrameCause::Viewport, nullptr, true);
    const int layoutsBeforeScroll = RoboWeb::FrameStatistics().layoutCount;
    const int fullBeforeScroll = RoboWeb::FrameStatistics().fullRasterCount;
    const int dirtyBeforeScroll = RoboWeb::FrameStatistics().dirtyRasterCount;
    tall.scrollY = 30;
    RoboWeb::FrameUpdate scrolled = RoboWeb::AdvanceFrame(tallCache, tall, 200, 40, RoboWeb::FrameCause::Scroll, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == layoutsBeforeScroll);
    EXPECT(RoboWeb::FrameStatistics().fullRasterCount == fullBeforeScroll);
    EXPECT(RoboWeb::FrameStatistics().dirtyRasterCount == dirtyBeforeScroll + 1);
    EXPECT(!scrolled.full && scrolled.scrollDelta == 30);
    EXPECT(SameFrame(tallCache.frame, RoboWeb::RenderDocument(tallCache.layout, tall.scrollY, true)));
    RoboWeb::Document vertical = Parse("<!DOCTYPE html><p>One</p><p>Two</p><p>Three</p><p>Four</p><p>Five</p>", diagnostics);
    RoboWeb::ViewCache verticalCache;
    RoboWeb::AdvanceFrame(verticalCache, vertical, 220, 50, RoboWeb::FrameCause::Viewport, nullptr, true);
    const int layoutsBeforeHeight = RoboWeb::FrameStatistics().layoutCount;
    const int fullBeforeHeight = RoboWeb::FrameStatistics().fullRasterCount;
    RoboWeb::FrameUpdate taller = RoboWeb::AdvanceFrame(verticalCache, vertical, 220, 90, RoboWeb::FrameCause::Viewport, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == layoutsBeforeHeight);
    EXPECT(RoboWeb::FrameStatistics().fullRasterCount == fullBeforeHeight);
    EXPECT(taller.paint && taller.full);
    EXPECT(!verticalCache.dependsOnViewportHeight);
    EXPECT(SameFrame(verticalCache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(vertical, 220, 90), 0, true)));
    RoboWeb::AdvanceFrame(verticalCache, vertical, 220, 36, RoboWeb::FrameCause::Viewport, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == layoutsBeforeHeight);
    EXPECT(SameFrame(verticalCache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(vertical, 220, 36), 0, true)));
    RoboWeb::Document fieldPage = Parse("<!DOCTYPE html><input value=\"ab\"><input type=\"checkbox\">", diagnostics);
    RoboWeb::ViewCache fieldCache;
    RoboWeb::ResetFrameStatistics();
    RoboWeb::AdvanceFrame(fieldCache, fieldPage, 320, 120, RoboWeb::FrameCause::Viewport, nullptr, true);
    RoboWeb::Node* textField = FindKind(*fieldPage.root, RoboWeb::ElementKind::Input);
    EXPECT(textField != nullptr);
    fieldPage.focus = textField;
    textField->liveValue = "abc";
    fieldPage.caretOffset = 3;
    fieldPage.caretOn = true;
    const int layoutsBeforeType = RoboWeb::FrameStatistics().layoutCount;
    RoboWeb::AdvanceFrame(fieldCache, fieldPage, 320, 120, RoboWeb::FrameCause::Content, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == layoutsBeforeType);
    RoboWeb::Document typedReference = Parse("<!DOCTYPE html><input value=\"ab\"><input type=\"checkbox\">", diagnostics);
    RoboWeb::Node* typedField = FindKind(*typedReference.root, RoboWeb::ElementKind::Input);
    typedReference.focus = typedField;
    typedField->liveValue = "abc";
    typedReference.caretOffset = 3;
    typedReference.caretOn = true;
    EXPECT(SameFrame(fieldCache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(typedReference, 320, 120), 0, true)));
    RoboWeb::Node* check = textField->parent;
    while (check != nullptr && !(check->kind == RoboWeb::ElementKind::Input && RoboWeb::AttributeValue(*check, "type") == "checkbox")) {
        if (check->kind == RoboWeb::ElementKind::Input && check != textField) {
            break;
        }
        check = nullptr;
    }
    RoboWeb::Node* checkBox = nullptr;
    std::vector<RoboWeb::Node*> checkStack{fieldPage.root.get()};
    while (!checkStack.empty()) {
        RoboWeb::Node* current = checkStack.back();
        checkStack.pop_back();
        if (current->kind == RoboWeb::ElementKind::Input && RoboWeb::AttributeValue(*current, "type") == "checkbox") {
            checkBox = current;
        }
        for (const std::unique_ptr<RoboWeb::Node>& child : current->children) {
            checkStack.push_back(child.get());
        }
    }
    EXPECT(checkBox != nullptr);
    checkBox->checked = true;
    fieldPage.status = "checked";
    const int layoutsBeforeCheck = RoboWeb::FrameStatistics().layoutCount;
    RoboWeb::AdvanceFrame(fieldCache, fieldPage, 320, 120, RoboWeb::FrameCause::Content, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == layoutsBeforeCheck);
    RoboWeb::Document checkedReference = Parse("<!DOCTYPE html><input value=\"ab\"><input type=\"checkbox\" checked>", diagnostics);
    RoboWeb::Node* checkedField = FindKind(*checkedReference.root, RoboWeb::ElementKind::Input);
    checkedReference.focus = checkedField;
    checkedField->liveValue = "abc";
    checkedReference.caretOffset = 3;
    checkedReference.caretOn = true;
    checkedReference.status = "checked";
    EXPECT(SameFrame(fieldCache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(checkedReference, 320, 120), 0, true)));
    RoboWeb::Diagnostics scripted;
    RoboWeb::Document scriptPage = Parse("<!DOCTYPE html><script>document.title = \"Hi\";</script><p id=\"lead\"></p>", scripted);
    const std::string scriptCpp = RoboWeb::GenerateCpp(scriptPage);
    EXPECT(scriptCpp.find("document.title = \"Hi\"") != std::string::npos);
    EXPECT(scriptCpp.find("compiledScript") != std::string::npos);
    scriptPage.compiledScript = [](RoboWeb::Document& page) { page.title = "FromCpp"; };
    scriptPage.scriptsRan = false;
    RoboWeb::LayoutDocument(scriptPage, 80, 40);
    EXPECT(scriptPage.title == "FromCpp");
    RoboWeb::Diagnostics slow;
    RoboWeb::Document interpreted = Parse("<!DOCTYPE html><script>eval(\"no\")</script><p>Hi</p>", slow);
    RoboWeb::WarnSlowPaths(interpreted, slow);
    EXPECT(slow.WarningCount() >= 1);
    bool warned = false;
    for (const RoboWeb::Diagnostic& item : slow.Items()) {
        if (item.message.find("will not have C++ performance") != std::string::npos) {
            warned = true;
        }
    }
    EXPECT(warned);
    RoboWeb::Diagnostics fast;
    RoboWeb::Document assigned = Parse("<!DOCTYPE html><script>document.title = \"Hi\";</script>", fast);
    RoboWeb::WarnSlowPaths(assigned, fast);
    EXPECT(fast.WarningCount() == 0);
    RoboWeb::Document detailsPage = Parse("<!DOCTYPE html><details><summary>More</summary><p>Inside</p></details>", diagnostics);
    RoboWeb::ViewCache detailsCache;
    RoboWeb::ResetFrameStatistics();
    RoboWeb::AdvanceFrame(detailsCache, detailsPage, 220, 160, RoboWeb::FrameCause::Viewport, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 1);
    RoboWeb::Node* details = FindKind(*detailsPage.root, RoboWeb::ElementKind::Details);
    EXPECT(details != nullptr);
    details->open = true;
    RoboWeb::AdvanceFrame(detailsCache, detailsPage, 220, 160, RoboWeb::FrameCause::Content, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 2);
    details->open = false;
    RoboWeb::AdvanceFrame(detailsCache, detailsPage, 220, 160, RoboWeb::FrameCause::Content, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 2);
    details->open = true;
    RoboWeb::AdvanceFrame(detailsCache, detailsPage, 220, 160, RoboWeb::FrameCause::Content, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 2);
    RoboWeb::Document detailsOpen = Parse("<!DOCTYPE html><details open><summary>More</summary><p>Inside</p></details>", diagnostics);
    EXPECT(SameFrame(detailsCache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(detailsOpen, 220, 160), 0, true)));
    RoboWeb::Document shortPage = Parse("<!DOCTYPE html><p>Hi</p>", diagnostics);
    RoboWeb::ViewCache shortCache;
    RoboWeb::ResetFrameStatistics();
    RoboWeb::AdvanceFrame(shortCache, shortPage, 180, 70, RoboWeb::FrameCause::Viewport, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 1);
    EXPECT(!shortCache.reflowsWithWidth);
    RoboWeb::AdvanceFrame(shortCache, shortPage, 280, 70, RoboWeb::FrameCause::Viewport, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 1);
    EXPECT(SameFrame(shortCache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(shortPage, 280, 70), 0, true)));
    RoboWeb::AdvanceFrame(shortCache, shortPage, 180, 70, RoboWeb::FrameCause::Viewport, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 1);
    EXPECT(SameFrame(shortCache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(shortPage, 180, 70), 0, true)));
    RoboWeb::Document wrapped = Parse("<!DOCTYPE html><p><span>Alpha</span> <span>Beta</span> <span>Gamma</span> <span>Delta</span> <span>Epsilon</span> <span>Zeta</span> <span>Eta</span> <span>Theta</span></p>", diagnostics);
    RoboWeb::ViewCache wrappedCache;
    RoboWeb::ResetFrameStatistics();
    RoboWeb::AdvanceFrame(wrappedCache, wrapped, 90, 200, RoboWeb::FrameCause::Viewport, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 1);
    EXPECT(wrappedCache.reflowsWithWidth);
    RoboWeb::AdvanceFrame(wrappedCache, wrapped, 420, 200, RoboWeb::FrameCause::Viewport, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 2);
    EXPECT(SameFrame(wrappedCache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(wrapped, 420, 200), 0, true)));
    RoboWeb::AdvanceFrame(wrappedCache, wrapped, 90, 200, RoboWeb::FrameCause::Viewport, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 2);
    EXPECT(SameFrame(wrappedCache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(wrapped, 90, 200), 0, true)));
    RoboWeb::Document linkPage = Parse("<!DOCTYPE html><body vlink=\"#551A8B\"><a href=\"https://example.test\">Go</a></body>", diagnostics);
    RoboWeb::ViewCache linkCache;
    RoboWeb::ResetFrameStatistics();
    RoboWeb::AdvanceFrame(linkCache, linkPage, 200, 80, RoboWeb::FrameCause::Viewport, nullptr, true);
    RoboWeb::Node* anchor = FindKind(*linkPage.root, RoboWeb::ElementKind::A);
    EXPECT(anchor != nullptr);
    anchor->visited = true;
    const int layoutsBeforeVisit = RoboWeb::FrameStatistics().layoutCount;
    RoboWeb::AdvanceFrame(linkCache, linkPage, 200, 80, RoboWeb::FrameCause::Content, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == layoutsBeforeVisit);
    RoboWeb::Document visitedPage = Parse("<!DOCTYPE html><body vlink=\"#551A8B\"><a href=\"https://example.test\">Go</a></body>", diagnostics);
    FindKind(*visitedPage.root, RoboWeb::ElementKind::A)->visited = true;
    EXPECT(SameFrame(linkCache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(visitedPage, 200, 80), 0, true)));
    RoboWeb::Document bakeSource = Parse("<!DOCTYPE html><p>Hi</p>", diagnostics);
    RoboWeb::LayoutResult bakedLayout = RoboWeb::LayoutDocument(bakeSource, 180, 70);
    RoboWeb::BakedStorage bakedStorage;
    RoboWeb::CaptureBakedFrame(bakeSource, bakedLayout, RoboWeb::GeometryStamp(bakeSource), bakedStorage);
    RoboWeb::Document bakedPage = Parse("<!DOCTYPE html><p>Hi</p>", diagnostics);
    bakedPage.baked = &bakedStorage.frame;
    RoboWeb::ViewCache bakedCache;
    RoboWeb::ResetFrameStatistics();
    RoboWeb::AdvanceFrame(bakedCache, bakedPage, 180, 70, RoboWeb::FrameCause::Viewport, nullptr, true);
    EXPECT(RoboWeb::FrameStatistics().layoutCount == 0);
    RoboWeb::Document bakedReference = Parse("<!DOCTYPE html><p>Hi</p>", diagnostics);
    EXPECT(SameFrame(bakedCache.frame, RoboWeb::RenderDocument(RoboWeb::LayoutDocument(bakedReference, 180, 70), 0, true)));
    const std::string bakedCpp = RoboWeb::GenerateCpp(bakedReference, {}, {}, 80, 40);
    EXPECT(bakedCpp.find("kBakedFrame") != std::string::npos);
    EXPECT(bakedCpp.find("document.baked") != std::string::npos);
    RoboWeb::Document colored = Parse("<!DOCTYPE html><script>document.getElementById(\"lead\").style.color = \"red\";</script><p id=\"lead\">Hi</p>", diagnostics);
    const std::string coloredCpp = RoboWeb::GenerateCpp(colored);
    EXPECT(coloredCpp.find("color:red") != std::string::npos);
    EXPECT(coloredCpp.find("compiledScript") != std::string::npos);
    EXPECT(RoboWeb::RegisterWebFontView("probe-face", 400, false, RoboWeb::kInterRegular, RoboWeb::kInterRegularSize));
    EXPECT(RoboWeb::MeasureTextPx("A", 16, false, false, "probe-face") > 0);
    static const unsigned char kRedPixel[] = {'P', '6', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 255, 0, 0};
    RoboWeb::RegisterCompiledImageView("unit-red.ppm", kRedPixel, sizeof(kRedPixel));
    RoboWeb::DecodedImage image = RoboWeb::DecodeImageFile("unit-red.ppm");
    EXPECT(image.ok && image.width == 1 && image.rgba[0] == 255 && image.rgba[1] == 0);
    {
        std::ofstream cssFile("Build/page-link.css");
        cssFile << "p { color: #112233; background: #abcdef; }\n";
        cssFile.close();
        std::ofstream scriptFile("Build/page-link.js");
        scriptFile << "document.title = \"Linked\";\n";
        scriptFile.close();
        RoboWeb::HtmlParser parser;
        RoboWeb::Document linked = parser.Parse(
            "<!DOCTYPE html><link rel=\"stylesheet\" href=\"page-link.css\"><script src=\"page-link.js\"></script><p id=\"lead\">Hi</p>",
            "Build/page.html", diagnostics);
        std::string loadError;
        EXPECT(RoboWeb::LoadExternalScripts(linked, loadError));
        EXPECT(RoboWeb::GatherText(*linked.root).find("document.title") != std::string::npos);
        std::vector<RoboWeb::CompiledWebFont> linkedFonts;
        EXPECT(RoboWeb::CompileLinkedStyles(linked, linkedFonts, loadError));
        bool foundColor = false;
        for (const RoboWeb::ElementStyle& rule : linked.styleRules) {
            if (rule.selector == "p" && rule.hasColor && rule.colorRed == 0x11 && rule.colorGreen == 0x22 && rule.colorBlue == 0x33 &&
                rule.hasBackground && rule.backgroundRed == 0xab) {
                foundColor = true;
            }
        }
        EXPECT(foundColor);
        std::string extraError;
        EXPECT(RoboWeb::CompileStylesheetText(linked, "h1 { color: #010203; }\n", linkedFonts, extraError));
        const std::string linkedCpp = RoboWeb::GenerateCpp(linked);
        EXPECT(linkedCpp.find("document.title = \"Linked\"") != std::string::npos);
        EXPECT(linkedCpp.find("colorRed = 17;") != std::string::npos);
        EXPECT(linkedCpp.find("colorRed = 1;") != std::string::npos);
        RoboWeb::Document missingCss = parser.Parse("<!DOCTYPE html><link rel=\"stylesheet\" href=\"missing-page.css\"><p>Hi</p>", "Build/page.html", diagnostics);
        std::string missingError;
        EXPECT(!RoboWeb::CompileLinkedStyles(missingCss, linkedFonts, missingError));
        EXPECT(missingError.find("stylesheet was not loaded") != std::string::npos);
        RoboWeb::Document missingScript = parser.Parse("<!DOCTYPE html><script src=\"missing-page.js\"></script>", "Build/page.html", diagnostics);
        EXPECT(!RoboWeb::LoadExternalScripts(missingScript, missingError));
        EXPECT(missingError.find("script was not loaded") != std::string::npos);
    }
}

}  // namespace

int main() {
    TestCatalog();
    TestEveryElementParses();
    TestAttributes();
    TestParserRecovery();
    TestLayoutMatchesBrowserDefaults();
    TestPixelsAndInteraction();
    TestStyleRule();
    TestCssFunctions();
    TestCssProperties();
    TestScriptAndPresentation();
    TestHtmlBehavior();
    TestSvgSpecification();
    TestStaticGeneration();
    TestXml();
    TestDiagnosticFormat();
    TestFastFrame();
    if (gFailed != 0) {
        std::cerr << gFailed << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "All RoboWebCompiler tests passed\n";
    return 0;
}
