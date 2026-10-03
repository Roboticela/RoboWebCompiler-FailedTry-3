#pragma once

#include <cstdint>
#include <string>

namespace RoboWeb {

// Every element defined by the SVG specification, in the order documented by
// MDN's SVG element reference. Tag names are stored lowercase because the HTML
// parser lowercases foreign-content tag names.
enum class SvgElementKind {
    A = 0,
    Animate,
    AnimateMotion,
    AnimateTransform,
    Circle,
    ClipPath,
    Defs,
    Desc,
    Ellipse,
    FeBlend,
    FeColorMatrix,
    FeComponentTransfer,
    FeComposite,
    FeConvolveMatrix,
    FeDiffuseLighting,
    FeDisplacementMap,
    FeDistantLight,
    FeDropShadow,
    FeFlood,
    FeFuncA,
    FeFuncB,
    FeFuncG,
    FeFuncR,
    FeGaussianBlur,
    FeImage,
    FeMerge,
    FeMergeNode,
    FeMorphology,
    FeOffset,
    FePointLight,
    FeSpecularLighting,
    FeSpotLight,
    FeTile,
    FeTurbulence,
    Filter,
    ForeignObject,
    G,
    Image,
    Line,
    LinearGradient,
    Marker,
    Mask,
    Metadata,
    Mpath,
    Path,
    Pattern,
    Polygon,
    Polyline,
    RadialGradient,
    Rect,
    Script,
    Set,
    Stop,
    Style,
    Svg,
    Switch,
    Symbol,
    Text,
    TextPath,
    Title,
    Tspan,
    Use,
    View,
    Unknown,
    Count
};

constexpr unsigned SVG_TRAIT_NEVER_RENDERED = 1u << 0;
constexpr unsigned SVG_TRAIT_SHAPE = 1u << 1;
constexpr unsigned SVG_TRAIT_CONTAINER = 1u << 2;
constexpr unsigned SVG_TRAIT_TEXT_CONTENT = 1u << 3;
constexpr unsigned SVG_TRAIT_PAINT_SERVER = 1u << 4;
constexpr unsigned SVG_TRAIT_ANIMATION = 1u << 5;
constexpr unsigned SVG_TRAIT_FILTER_PRIMITIVE = 1u << 6;
constexpr unsigned SVG_TRAIT_LIGHT_SOURCE = 1u << 7;
constexpr unsigned SVG_TRAIT_DESCRIPTIVE = 1u << 8;
constexpr unsigned SVG_TRAIT_GRAPHICS = 1u << 9;

struct SvgElementSpec {
    SvgElementKind kind;
    const char* name;
    unsigned traits;
};

const SvgElementSpec& SvgSpecFor(SvgElementKind kind);
SvgElementKind FindSvgElement(const std::string& lowercaseName);
bool IsSvgElementName(const std::string& lowercaseName);
bool SvgElementNeverRendered(SvgElementKind kind);
int SvgElementCount();
bool SvgCatalogSelfCheck(std::string& error);

// Attribute flags describe how an attribute participates in the SVG model.
constexpr unsigned SVG_ATTR_GLOBAL = 1u << 0;         // id, class, style, lang, tabindex, ...
constexpr unsigned SVG_ATTR_PRESENTATION = 1u << 1;   // CSS presentation attributes (fill, stroke, opacity, ...)
constexpr unsigned SVG_ATTR_GEOMETRY = 1u << 2;       // geometry properties (x, y, width, cx, d, ...)
constexpr unsigned SVG_ATTR_ANIMATION = 1u << 3;      // SMIL animation attributes
constexpr unsigned SVG_ATTR_FILTER = 1u << 4;         // filter primitive and light-source attributes
constexpr unsigned SVG_ATTR_DEPRECATED = 1u << 5;     // still honored, but obsolete
constexpr unsigned SVG_ATTR_URL = 1u << 6;            // value is a URL reference

struct SvgAttributeRule {
    const char* name;
    const char* elements;
    unsigned flags;
};

const SvgAttributeRule* FindSvgAttributeRule(const std::string& lowercaseName);
bool IsSvgAttributeName(const std::string& lowercaseName);
bool IsSvgPresentationAttribute(const std::string& lowercaseName);
bool SvgAttributeAppliesTo(const std::string& lowercaseName, SvgElementKind kind);
int SvgAttributeCount();
bool SvgAttributeCatalogSelfCheck(std::string& error);

// CSS color keywords shared by HTML and SVG. Returns false for unknown names.
bool LookupSvgColorKeyword(const std::string& lowercaseName, std::uint8_t& red, std::uint8_t& green, std::uint8_t& blue);

}
