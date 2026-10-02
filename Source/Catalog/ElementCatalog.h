#pragma once

#include "Core/Constants.h"

#include <string>

namespace RoboWeb {

enum class ElementKind {
    A = 0,
    Abbr,
    Acronym,
    Address,
    Area,
    Article,
    Aside,
    Audio,
    B,
    Base,
    Bdi,
    Bdo,
    Big,
    Blockquote,
    Body,
    Br,
    Button,
    Canvas,
    Caption,
    Center,
    Cite,
    Code,
    Col,
    Colgroup,
    Content,
    Data,
    Datalist,
    Dd,
    Del,
    Details,
    Dfn,
    Dialog,
    Dir,
    Div,
    Dl,
    Dt,
    Em,
    Embed,
    FencedFrame,
    Fieldset,
    Figcaption,
    Figure,
    Font,
    Footer,
    Form,
    Frame,
    Frameset,
    Geolocation,
    H1,
    H2,
    H3,
    H4,
    H5,
    H6,
    Head,
    Header,
    Hgroup,
    Hr,
    Html,
    I,
    Iframe,
    Image,
    Img,
    Input,
    Ins,
    Kbd,
    Label,
    Legend,
    Li,
    Link,
    Main,
    Map,
    Mark,
    Marquee,
    Menu,
    MenuItem,
    Meta,
    Meter,
    Nav,
    Nobr,
    Noembed,
    Noframes,
    Noscript,
    Object,
    Ol,
    Optgroup,
    Option,
    Output,
    P,
    Param,
    Picture,
    Plaintext,
    Pre,
    Progress,
    Q,
    Rb,
    Rp,
    Rt,
    Rtc,
    Ruby,
    S,
    Samp,
    Script,
    Search,
    Section,
    Select,
    SelectedContent,
    Shadow,
    Slot,
    Small,
    Source,
    Span,
    Strike,
    Strong,
    Style,
    Sub,
    Summary,
    Sup,
    Table,
    Tbody,
    Td,
    Template,
    Textarea,
    Tfoot,
    Th,
    Thead,
    Time,
    Title,
    Tr,
    Track,
    Tt,
    U,
    Ul,
    Var,
    Video,
    Wbr,
    Xmp,
    Math,
    Svg,
    Unknown,
    Count
};

enum class DisplayKind {
    None,
    Block,
    Inline,
    InlineBlock,
    ListItem,
    Table,
    TableRow,
    TableCell,
    TableCaption,
    TableRowGroup,
    TableColumn,
    TableColumnGroup,
    Replaced,
    LineBreak,
    WordBreak
};

struct ElementSpec {
    ElementKind kind;
    const char* name;
    unsigned traits;
    DisplayKind display;
    unsigned presentation;
    int marginTopMilli;
    int marginBottomMilli;
    int marginLeftPx;
    int marginRightPx;
    int paddingLeftPx;
    int fontSizeMilli;
};

const ElementSpec& SpecFor(ElementKind kind);
ElementKind FindElement(const std::string& lowercaseName);
bool IsKnownElementName(const std::string& lowercaseName);
int ElementCount();
bool CatalogSelfCheck(std::string& error);
bool ClosesParagraph(ElementKind kind);
bool IsBlockContainer(ElementKind kind);
bool IsTablePart(ElementKind kind);
bool IsMetadataElement(ElementKind kind);
bool IsHeading(ElementKind kind);
const char* DisplayName(DisplayKind display);

}
