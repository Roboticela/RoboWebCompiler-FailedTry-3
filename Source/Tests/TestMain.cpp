#include "Catalog/AttributeCatalog.h"
#include "Catalog/ElementCatalog.h"
#include "Compiler/CodeGenerator.h"
#include "Diagnostics/Diagnostics.h"
#include "Dom/Document.h"
#include "Layout/LayoutEngine.h"
#include "Parser/HtmlParser.h"
#include "Render/BitmapFont.h"
#include "Render/ImageDecoder.h"
#include "Render/Renderer.h"
#include "Runtime/Interaction.h"
#include "Validate/TreeValidator.h"

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
    EXPECT(cpp.find("BuildCompiledDocument") != std::string::npos);
    EXPECT(cpp.find("MakeElement") != std::string::npos);
    EXPECT(cpp.find("MakeText") != std::string::npos);
    EXPECT(cpp.find("Hello") != std::string::npos);
    EXPECT(cpp.find("RunDocument") != std::string::npos);
    EXPECT(cpp.find("StaticNode") == std::string::npos);
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
        EXPECT(RoboWeb::PixelGreen(joinPathFrame, joinPathBox->x + 31, joinPathBox->y + 4) == 0);
        EXPECT(RoboWeb::PixelRed(joinPathFrame, joinPathBox->x + 31, joinPathBox->y + 4) == 255);
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
        EXPECT(RoboWeb::PixelGreen(capPathFrame, capPathBox->x + 30, capPathBox->y + 12) == 0);
        EXPECT(RoboWeb::PixelRed(capPathFrame, capPathBox->x + 30, capPathBox->y + 12) == 255);
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
        EXPECT(RoboWeb::PixelGreen(blurredFrame, blurredBox->x + 2, blurredBox->y + 4) == 0);
        const int fringe = RoboWeb::PixelGreen(blurredFrame, blurredBox->x + 10, blurredBox->y + 4);
        EXPECT(fringe > 40 && fringe < 230);
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
        EXPECT(RoboWeb::PixelGreen(cappedFrame, cappedBox->x + 10, cappedBox->y + 8) == 0);
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
        EXPECT(RoboWeb::PixelGreen(joinedFrame, joinedBox->x + 14, joinedBox->y + 6) == 0);
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
        EXPECT(RoboWeb::PixelGreen(miterSharpFrame, miterBox->x + 20, miterBox->y + 10) == 0);
        EXPECT(RoboWeb::PixelGreen(miterCutFrame, miterBox->x + 20, miterBox->y + 10) == 255);
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

}  // namespace

int main() {
    TestCatalog();
    TestEveryElementParses();
    TestAttributes();
    TestParserRecovery();
    TestLayoutMatchesBrowserDefaults();
    TestPixelsAndInteraction();
    TestStyleRule();
    TestScriptAndPresentation();
    TestHtmlBehavior();
    TestStaticGeneration();
    TestDiagnosticFormat();
    if (gFailed != 0) {
        std::cerr << gFailed << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "All RoboWebCompiler tests passed\n";
    return 0;
}
