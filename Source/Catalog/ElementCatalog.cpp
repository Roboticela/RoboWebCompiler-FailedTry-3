#include "Catalog/ElementCatalog.h"

#include <cstring>

namespace RoboWeb {

namespace {

constexpr unsigned kVoid = TRAIT_VOID;
constexpr unsigned kOld = TRAIT_OBSOLETE;
constexpr unsigned kExp = TRAIT_EXPERIMENTAL;
constexpr unsigned kRaw = TRAIT_RAW_TEXT;
constexpr unsigned kRc = TRAIT_RC_DATA;
constexpr unsigned kPlain = TRAIT_PLAIN_TEXT;
constexpr unsigned kBold = PRESENTATION_BOLD;
constexpr unsigned kItalic = PRESENTATION_ITALIC;
constexpr unsigned kUnder = PRESENTATION_UNDERLINE;
constexpr unsigned kStrike = PRESENTATION_STRIKE;
constexpr unsigned kMono = PRESENTATION_MONOSPACE;
constexpr unsigned kCenter = PRESENTATION_CENTER;
constexpr unsigned kNowrap = PRESENTATION_NOWRAP;
constexpr unsigned kPre = PRESENTATION_PRE;
constexpr unsigned kMark = PRESENTATION_MARK;
constexpr unsigned kSub = PRESENTATION_SUB;
constexpr unsigned kSup = PRESENTATION_SUP;
constexpr unsigned kQuote = PRESENTATION_QUOTE;

constexpr int kEm = MARGIN_MILLI_BLOCK;
constexpr int kSide = BLOCKQUOTE_SIDE_PX;
constexpr int kList = LIST_PADDING_PX;
constexpr int kSmall = SMALLER_FONT_MILLI;
constexpr int kLarge = LARGER_FONT_MILLI;

const ElementSpec kSpecs[] = {
    {ElementKind::A, "a", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Abbr, "abbr", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Acronym, "acronym", kOld, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Address, "address", 0, DisplayKind::Block, kItalic, kEm, kEm, 0, 0, 0, 1000},
    {ElementKind::Area, "area", kVoid, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Article, "article", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Aside, "aside", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Audio, "audio", 0, DisplayKind::Replaced, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::B, "b", 0, DisplayKind::Inline, kBold, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Base, "base", kVoid, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Bdi, "bdi", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Bdo, "bdo", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Big, "big", kOld, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, kLarge},
    {ElementKind::Blockquote, "blockquote", 0, DisplayKind::Block, 0, kEm, kEm, kSide, kSide, 0, 1000},
    {ElementKind::Body, "body", 0, DisplayKind::Block, 0, 0, 0, BODY_MARGIN_PX, BODY_MARGIN_PX, 0, 1000},
    {ElementKind::Br, "br", kVoid, DisplayKind::LineBreak, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Button, "button", 0, DisplayKind::InlineBlock, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Canvas, "canvas", 0, DisplayKind::Replaced, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Caption, "caption", 0, DisplayKind::TableCaption, kCenter, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Center, "center", kOld, DisplayKind::Block, kCenter, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Cite, "cite", 0, DisplayKind::Inline, kItalic, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Code, "code", 0, DisplayKind::Inline, kMono, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Col, "col", kVoid, DisplayKind::TableColumn, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Colgroup, "colgroup", 0, DisplayKind::TableColumnGroup, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Content, "content", kOld, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Data, "data", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Datalist, "datalist", 0, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Dd, "dd", 0, DisplayKind::Block, 0, 0, 0, DESCRIPTION_OFFSET_PX, 0, 0, 1000},
    {ElementKind::Del, "del", 0, DisplayKind::Inline, kStrike, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Details, "details", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Dfn, "dfn", 0, DisplayKind::Inline, kItalic, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Dialog, "dialog", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Dir, "dir", kOld, DisplayKind::Block, 0, kEm, kEm, 0, 0, kList, 1000},
    {ElementKind::Div, "div", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Dl, "dl", 0, DisplayKind::Block, 0, kEm, kEm, 0, 0, 0, 1000},
    {ElementKind::Dt, "dt", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Em, "em", 0, DisplayKind::Inline, kItalic, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Embed, "embed", kVoid, DisplayKind::Replaced, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::FencedFrame, "fencedframe", kExp, DisplayKind::Replaced, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Fieldset, "fieldset", 0, DisplayKind::Block, 0, 0, 0, 2, 2, 0, 1000},
    {ElementKind::Figcaption, "figcaption", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Figure, "figure", 0, DisplayKind::Block, 0, kEm, kEm, FIGURE_SIDE_PX, FIGURE_SIDE_PX, 0, 1000},
    {ElementKind::Font, "font", kOld, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Footer, "footer", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Form, "form", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Frame, "frame", kOld | kVoid, DisplayKind::Replaced, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Frameset, "frameset", kOld, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Geolocation, "geolocation", kExp, DisplayKind::InlineBlock, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::H1, "h1", 0, DisplayKind::Block, kBold, MARGIN_MILLI_H1, MARGIN_MILLI_H1, 0, 0, 0, FONT_MILLI_H1},
    {ElementKind::H2, "h2", 0, DisplayKind::Block, kBold, MARGIN_MILLI_H2, MARGIN_MILLI_H2, 0, 0, 0, FONT_MILLI_H2},
    {ElementKind::H3, "h3", 0, DisplayKind::Block, kBold, MARGIN_MILLI_H3, MARGIN_MILLI_H3, 0, 0, 0, FONT_MILLI_H3},
    {ElementKind::H4, "h4", 0, DisplayKind::Block, kBold, MARGIN_MILLI_H4, MARGIN_MILLI_H4, 0, 0, 0, FONT_MILLI_H4},
    {ElementKind::H5, "h5", 0, DisplayKind::Block, kBold, MARGIN_MILLI_H5, MARGIN_MILLI_H5, 0, 0, 0, FONT_MILLI_H5},
    {ElementKind::H6, "h6", 0, DisplayKind::Block, kBold, MARGIN_MILLI_H6, MARGIN_MILLI_H6, 0, 0, 0, FONT_MILLI_H6},
    {ElementKind::Head, "head", 0, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Header, "header", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Hgroup, "hgroup", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Hr, "hr", kVoid, DisplayKind::Block, 0, MARGIN_MILLI_HR, MARGIN_MILLI_HR, 0, 0, 0, 1000},
    {ElementKind::Html, "html", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::I, "i", 0, DisplayKind::Inline, kItalic, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Iframe, "iframe", 0, DisplayKind::Replaced, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Image, "image", kOld | kVoid, DisplayKind::Replaced, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Img, "img", kVoid, DisplayKind::Replaced, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Input, "input", kVoid, DisplayKind::InlineBlock, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Ins, "ins", 0, DisplayKind::Inline, kUnder, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Kbd, "kbd", 0, DisplayKind::Inline, kMono, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Label, "label", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Legend, "legend", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Li, "li", 0, DisplayKind::ListItem, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Link, "link", kVoid, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Main, "main", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Map, "map", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Mark, "mark", 0, DisplayKind::Inline, kMark, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Marquee, "marquee", kOld, DisplayKind::InlineBlock, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Menu, "menu", 0, DisplayKind::Block, 0, kEm, kEm, 0, 0, kList, 1000},
    {ElementKind::MenuItem, "menuitem", kOld, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Meta, "meta", kVoid, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Meter, "meter", 0, DisplayKind::InlineBlock, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Nav, "nav", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Nobr, "nobr", kOld, DisplayKind::Inline, kNowrap, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Noembed, "noembed", kOld, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Noframes, "noframes", kOld, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Noscript, "noscript", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Object, "object", 0, DisplayKind::Replaced, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Ol, "ol", 0, DisplayKind::Block, 0, kEm, kEm, 0, 0, kList, 1000},
    {ElementKind::Optgroup, "optgroup", 0, DisplayKind::Block, kBold, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Option, "option", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Output, "output", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::P, "p", 0, DisplayKind::Block, 0, kEm, kEm, 0, 0, 0, 1000},
    {ElementKind::Param, "param", kOld | kVoid, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Picture, "picture", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Plaintext, "plaintext", kOld | kPlain, DisplayKind::Block, kMono | kPre, kEm, kEm, 0, 0, 0, 1000},
    {ElementKind::Pre, "pre", 0, DisplayKind::Block, kMono | kPre, kEm, kEm, 0, 0, 0, 1000},
    {ElementKind::Progress, "progress", 0, DisplayKind::InlineBlock, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Q, "q", 0, DisplayKind::Inline, kQuote, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Rb, "rb", kOld, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Rp, "rp", 0, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Rt, "rt", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, kSmall},
    {ElementKind::Rtc, "rtc", kOld, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Ruby, "ruby", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::S, "s", 0, DisplayKind::Inline, kStrike, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Samp, "samp", 0, DisplayKind::Inline, kMono, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Script, "script", kRaw, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Search, "search", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Section, "section", 0, DisplayKind::Block, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Select, "select", 0, DisplayKind::InlineBlock, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::SelectedContent, "selectedcontent", kExp, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Shadow, "shadow", kOld, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Slot, "slot", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Small, "small", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, kSmall},
    {ElementKind::Source, "source", kVoid, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Span, "span", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Strike, "strike", kOld, DisplayKind::Inline, kStrike, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Strong, "strong", 0, DisplayKind::Inline, kBold, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Style, "style", kRaw, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Sub, "sub", 0, DisplayKind::Inline, kSub, 0, 0, 0, 0, 0, kSmall},
    {ElementKind::Summary, "summary", 0, DisplayKind::ListItem, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Sup, "sup", 0, DisplayKind::Inline, kSup, 0, 0, 0, 0, 0, kSmall},
    {ElementKind::Table, "table", 0, DisplayKind::Table, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Tbody, "tbody", 0, DisplayKind::TableRowGroup, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Td, "td", 0, DisplayKind::TableCell, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Template, "template", 0, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Textarea, "textarea", kRc, DisplayKind::InlineBlock, kPre, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Tfoot, "tfoot", 0, DisplayKind::TableRowGroup, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Th, "th", 0, DisplayKind::TableCell, kBold | kCenter, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Thead, "thead", 0, DisplayKind::TableRowGroup, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Time, "time", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Title, "title", kRc, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Tr, "tr", 0, DisplayKind::TableRow, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Track, "track", kVoid, DisplayKind::None, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Tt, "tt", kOld, DisplayKind::Inline, kMono, 0, 0, 0, 0, 0, 1000},
    {ElementKind::U, "u", 0, DisplayKind::Inline, kUnder, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Ul, "ul", 0, DisplayKind::Block, 0, kEm, kEm, 0, 0, kList, 1000},
    {ElementKind::Var, "var", 0, DisplayKind::Inline, kItalic, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Video, "video", 0, DisplayKind::Replaced, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Wbr, "wbr", kVoid, DisplayKind::WordBreak, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Xmp, "xmp", kOld | kRaw, DisplayKind::Block, kMono | kPre, kEm, kEm, 0, 0, 0, 1000},
    {ElementKind::Math, "math", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Svg, "svg", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
    {ElementKind::Unknown, "", 0, DisplayKind::Inline, 0, 0, 0, 0, 0, 0, 1000},
};

const ElementKind kParagraphClosers[] = {
    ElementKind::Address, ElementKind::Article, ElementKind::Aside, ElementKind::Blockquote,
    ElementKind::Center, ElementKind::Details, ElementKind::Dialog, ElementKind::Dir,
    ElementKind::Div, ElementKind::Dl, ElementKind::Fieldset, ElementKind::Figcaption,
    ElementKind::Figure, ElementKind::Footer, ElementKind::Form, ElementKind::H1,
    ElementKind::H2, ElementKind::H3, ElementKind::H4, ElementKind::H5, ElementKind::H6,
    ElementKind::Header, ElementKind::Hgroup, ElementKind::Hr, ElementKind::Main,
    ElementKind::Menu, ElementKind::Nav, ElementKind::Ol, ElementKind::P, ElementKind::Pre,
    ElementKind::Search, ElementKind::Section, ElementKind::Table, ElementKind::Ul,
    ElementKind::Plaintext, ElementKind::Xmp,
};

bool SameName(const char* left, const std::string& right) {
    return std::strcmp(left, right.c_str()) == 0;
}

}  // namespace

const ElementSpec& SpecFor(ElementKind kind) {
    const int index = static_cast<int>(kind);
    if (index < 0 || index >= static_cast<int>(ElementKind::Count)) {
        return kSpecs[static_cast<int>(ElementKind::Unknown)];
    }
    return kSpecs[index];
}

ElementKind FindElement(const std::string& lowercaseName) {
    for (const ElementSpec& spec : kSpecs) {
        if (spec.name[0] != '\0' && SameName(spec.name, lowercaseName)) {
            return spec.kind;
        }
    }
    return ElementKind::Unknown;
}

bool IsKnownElementName(const std::string& lowercaseName) {
    const ElementKind kind = FindElement(lowercaseName);
    return kind != ElementKind::Unknown && kind != ElementKind::Math && kind != ElementKind::Svg;
}

int ElementCount() {
    return static_cast<int>(ElementKind::Count);
}

bool CatalogSelfCheck(std::string& error) {
    const int count = static_cast<int>(sizeof(kSpecs) / sizeof(kSpecs[0]));
    if (count != static_cast<int>(ElementKind::Count)) {
        error = "element catalog size does not match ElementKind";
        return false;
    }
    for (int index = 0; index < count; ++index) {
        if (static_cast<int>(kSpecs[index].kind) != index) {
            error = "element catalog is out of order at " + std::to_string(index);
            return false;
        }
        if (kSpecs[index].kind != ElementKind::Unknown && FindElement(kSpecs[index].name) != kSpecs[index].kind) {
            error = std::string("element lookup failed for ") + kSpecs[index].name;
            return false;
        }
    }
    return true;
}

bool ClosesParagraph(ElementKind kind) {
    for (ElementKind closer : kParagraphClosers) {
        if (closer == kind) {
            return true;
        }
    }
    return false;
}

bool IsBlockContainer(ElementKind kind) {
    const DisplayKind display = SpecFor(kind).display;
    return display == DisplayKind::Block || display == DisplayKind::ListItem || display == DisplayKind::Table ||
           display == DisplayKind::TableCaption;
}

bool IsTablePart(ElementKind kind) {
    const DisplayKind display = SpecFor(kind).display;
    return display == DisplayKind::Table || display == DisplayKind::TableRow || display == DisplayKind::TableCell ||
           display == DisplayKind::TableCaption || display == DisplayKind::TableRowGroup ||
           display == DisplayKind::TableColumn || display == DisplayKind::TableColumnGroup;
}

bool IsMetadataElement(ElementKind kind) {
    return kind == ElementKind::Base || kind == ElementKind::Link || kind == ElementKind::Meta ||
           kind == ElementKind::Title || kind == ElementKind::Style || kind == ElementKind::Script ||
           kind == ElementKind::Noscript;
}

bool IsHeading(ElementKind kind) {
    return kind == ElementKind::H1 || kind == ElementKind::H2 || kind == ElementKind::H3 ||
           kind == ElementKind::H4 || kind == ElementKind::H5 || kind == ElementKind::H6;
}

const char* DisplayName(DisplayKind display) {
    switch (display) {
    case DisplayKind::None:
        return "none";
    case DisplayKind::Block:
        return "block";
    case DisplayKind::Inline:
        return "inline";
    case DisplayKind::InlineBlock:
        return "inline-block";
    case DisplayKind::ListItem:
        return "list-item";
    case DisplayKind::Table:
        return "table";
    case DisplayKind::TableRow:
        return "table-row";
    case DisplayKind::TableCell:
        return "table-cell";
    case DisplayKind::TableCaption:
        return "table-caption";
    case DisplayKind::TableRowGroup:
        return "table-row-group";
    case DisplayKind::TableColumn:
        return "table-column";
    case DisplayKind::TableColumnGroup:
        return "table-column-group";
    case DisplayKind::Replaced:
        return "replaced";
    case DisplayKind::LineBreak:
        return "line-break";
    case DisplayKind::WordBreak:
        return "word-break";
    }
    return "inline";
}

}
