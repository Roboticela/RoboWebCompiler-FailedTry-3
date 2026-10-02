#include "Catalog/AttributeCatalog.h"
#include "Catalog/ElementCatalog.h"
#include "Compiler/CodeGenerator.h"
#include "Diagnostics/Diagnostics.h"
#include "Dom/Document.h"
#include "Layout/LayoutEngine.h"
#include "Parser/HtmlParser.h"
#include "Render/BitmapFont.h"
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
    RoboWeb::Document scaled = Parse(
        "<!DOCTYPE html><svg width='40' height='20' viewBox='0 0 100 100'><rect x='0' y='0' width='50' height='100' fill='red'/></svg>",
        diagnostics);
    RoboWeb::LayoutResult scaledLayout = RoboWeb::LayoutDocument(scaled, 160, 80);
    RoboWeb::Framebuffer scaledFrame = RoboWeb::RenderDocument(scaledLayout, 0);
    const RoboWeb::ElementBox* scaledBox = RoboWeb::FindBox(scaledLayout, "svg");
    EXPECT(scaledBox != nullptr);
    if (scaledBox != nullptr) {
        EXPECT(RoboWeb::PixelGreen(scaledFrame, scaledBox->x + 4, scaledBox->y + 4) == 0);
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
