#include "Catalog/SvgCatalog.h"

#include <cstring>

namespace RoboWeb {

namespace {

const SvgElementSpec kElements[] = {
    {SvgElementKind::A, "a", SVG_TRAIT_CONTAINER},
    {SvgElementKind::Animate, "animate", SVG_TRAIT_ANIMATION | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::AnimateMotion, "animatemotion", SVG_TRAIT_ANIMATION | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::AnimateTransform, "animatetransform", SVG_TRAIT_ANIMATION | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Circle, "circle", SVG_TRAIT_SHAPE | SVG_TRAIT_GRAPHICS},
    {SvgElementKind::ClipPath, "clippath", SVG_TRAIT_CONTAINER | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Defs, "defs", SVG_TRAIT_CONTAINER | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Desc, "desc", SVG_TRAIT_DESCRIPTIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Ellipse, "ellipse", SVG_TRAIT_SHAPE | SVG_TRAIT_GRAPHICS},
    {SvgElementKind::FeBlend, "feblend", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeColorMatrix, "fecolormatrix", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeComponentTransfer, "fecomponenttransfer", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeComposite, "fecomposite", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeConvolveMatrix, "feconvolvematrix", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeDiffuseLighting, "fediffuselighting", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeDisplacementMap, "fedisplacementmap", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeDistantLight, "fedistantlight", SVG_TRAIT_LIGHT_SOURCE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeDropShadow, "fedropshadow", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeFlood, "feflood", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeFuncA, "fefunca", SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeFuncB, "fefuncb", SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeFuncG, "fefuncg", SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeFuncR, "fefuncr", SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeGaussianBlur, "fegaussianblur", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeImage, "feimage", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeMerge, "femerge", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeMergeNode, "femergenode", SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeMorphology, "femorphology", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeOffset, "feoffset", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FePointLight, "fepointlight", SVG_TRAIT_LIGHT_SOURCE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeSpecularLighting, "fespecularlighting", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeSpotLight, "fespotlight", SVG_TRAIT_LIGHT_SOURCE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeTile, "fetile", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::FeTurbulence, "feturbulence", SVG_TRAIT_FILTER_PRIMITIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Filter, "filter", SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::ForeignObject, "foreignobject", SVG_TRAIT_GRAPHICS},
    {SvgElementKind::G, "g", SVG_TRAIT_CONTAINER},
    {SvgElementKind::Image, "image", SVG_TRAIT_GRAPHICS},
    {SvgElementKind::Line, "line", SVG_TRAIT_SHAPE | SVG_TRAIT_GRAPHICS},
    {SvgElementKind::LinearGradient, "lineargradient", SVG_TRAIT_PAINT_SERVER | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Marker, "marker", SVG_TRAIT_CONTAINER | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Mask, "mask", SVG_TRAIT_CONTAINER | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Metadata, "metadata", SVG_TRAIT_DESCRIPTIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Mpath, "mpath", SVG_TRAIT_ANIMATION | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Path, "path", SVG_TRAIT_SHAPE | SVG_TRAIT_GRAPHICS},
    {SvgElementKind::Pattern, "pattern", SVG_TRAIT_PAINT_SERVER | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Polygon, "polygon", SVG_TRAIT_SHAPE | SVG_TRAIT_GRAPHICS},
    {SvgElementKind::Polyline, "polyline", SVG_TRAIT_SHAPE | SVG_TRAIT_GRAPHICS},
    {SvgElementKind::RadialGradient, "radialgradient", SVG_TRAIT_PAINT_SERVER | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Rect, "rect", SVG_TRAIT_SHAPE | SVG_TRAIT_GRAPHICS},
    {SvgElementKind::Script, "script", SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Set, "set", SVG_TRAIT_ANIMATION | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Stop, "stop", SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Style, "style", SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Svg, "svg", SVG_TRAIT_CONTAINER | SVG_TRAIT_GRAPHICS},
    {SvgElementKind::Switch, "switch", SVG_TRAIT_CONTAINER},
    {SvgElementKind::Symbol, "symbol", SVG_TRAIT_CONTAINER | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Text, "text", SVG_TRAIT_TEXT_CONTENT | SVG_TRAIT_GRAPHICS},
    {SvgElementKind::TextPath, "textpath", SVG_TRAIT_TEXT_CONTENT | SVG_TRAIT_GRAPHICS},
    {SvgElementKind::Title, "title", SVG_TRAIT_DESCRIPTIVE | SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Tspan, "tspan", SVG_TRAIT_TEXT_CONTENT | SVG_TRAIT_GRAPHICS},
    {SvgElementKind::Use, "use", SVG_TRAIT_GRAPHICS},
    {SvgElementKind::View, "view", SVG_TRAIT_NEVER_RENDERED},
    {SvgElementKind::Unknown, "", 0},
};

constexpr unsigned kGlobal = SVG_ATTR_GLOBAL;
constexpr unsigned kPresentation = SVG_ATTR_PRESENTATION;
constexpr unsigned kGeometry = SVG_ATTR_GEOMETRY;
constexpr unsigned kAnimation = SVG_ATTR_ANIMATION;
constexpr unsigned kFilter = SVG_ATTR_FILTER;
constexpr unsigned kDeprecated = SVG_ATTR_DEPRECATED;
constexpr unsigned kUrl = SVG_ATTR_URL;

const SvgAttributeRule kAttributes[] = {
    // Core and conditional processing attributes.
    {"autofocus", "*", kGlobal},
    {"class", "*", kGlobal},
    {"id", "*", kGlobal},
    {"lang", "*", kGlobal},
    {"style", "*", kGlobal},
    {"tabindex", "*", kGlobal},
    {"xml:base", "*", kGlobal | kDeprecated},
    {"xml:lang", "*", kGlobal},
    {"xml:space", "*", kGlobal},
    {"requiredextensions", "*", kGlobal},
    {"requiredfeatures", "*", kGlobal | kDeprecated},
    {"systemlanguage", "*", kGlobal},
    {"externallyresourcesrequired", "*", kGlobal | kDeprecated},

    // Presentation attributes: accepted on every SVG element, inherited by
    // descendants exactly like their CSS counterparts.
    {"alignment-baseline", "*", kPresentation},
    {"baseline-shift", "*", kPresentation},
    {"clip", "*", kPresentation | kDeprecated},
    {"clip-path", "*", kPresentation | kUrl},
    {"clip-rule", "*", kPresentation},
    {"color", "*", kPresentation},
    {"color-interpolation", "*", kPresentation},
    {"color-interpolation-filters", "*", kPresentation},
    {"color-rendering", "*", kPresentation},
    {"cursor", "*", kPresentation},
    {"direction", "*", kPresentation},
    {"display", "*", kPresentation},
    {"dominant-baseline", "*", kPresentation},
    {"fill", "*", kPresentation},
    {"fill-opacity", "*", kPresentation},
    {"fill-rule", "*", kPresentation},
    {"filter", "*", kPresentation | kUrl},
    {"flood-color", "*", kPresentation},
    {"flood-opacity", "*", kPresentation},
    {"font-family", "*", kPresentation},
    {"font-size", "*", kPresentation},
    {"font-size-adjust", "*", kPresentation},
    {"font-stretch", "*", kPresentation},
    {"font-style", "*", kPresentation},
    {"font-variant", "*", kPresentation},
    {"font-weight", "*", kPresentation},
    {"font-width", "*", kPresentation},
    {"glyph-orientation-horizontal", "*", kPresentation | kDeprecated},
    {"glyph-orientation-vertical", "*", kPresentation | kDeprecated},
    {"image-rendering", "*", kPresentation},
    {"inline-size", "*", kPresentation},
    {"letter-spacing", "*", kPresentation},
    {"lighting-color", "*", kPresentation},
    {"marker-end", "*", kPresentation | kUrl},
    {"marker-mid", "*", kPresentation | kUrl},
    {"marker-start", "*", kPresentation | kUrl},
    {"mask", "*", kPresentation | kUrl},
    {"mask-type", "*", kPresentation},
    {"opacity", "*", kPresentation},
    {"overflow", "*", kPresentation},
    {"paint-order", "*", kPresentation},
    {"pointer-events", "*", kPresentation},
    {"shape-rendering", "*", kPresentation},
    {"stop-color", "*", kPresentation},
    {"stop-opacity", "*", kPresentation},
    {"stroke", "*", kPresentation},
    {"stroke-dasharray", "*", kPresentation},
    {"stroke-dashoffset", "*", kPresentation},
    {"stroke-linecap", "*", kPresentation},
    {"stroke-linejoin", "*", kPresentation},
    {"stroke-miterlimit", "*", kPresentation},
    {"stroke-opacity", "*", kPresentation},
    {"stroke-width", "*", kPresentation},
    {"text-anchor", "*", kPresentation},
    {"text-decoration", "*", kPresentation},
    {"text-overflow", "*", kPresentation},
    {"text-rendering", "*", kPresentation},
    {"transform", "*", kPresentation},
    {"transform-origin", "*", kPresentation},
    {"unicode-bidi", "*", kPresentation},
    {"vector-effect", "*", kPresentation},
    {"visibility", "*", kPresentation},
    {"white-space", "*", kPresentation},
    {"word-spacing", "*", kPresentation},
    {"writing-mode", "*", kPresentation},

    // Geometry properties.
    {"cx", "circle ellipse", kGeometry},
    {"cy", "circle ellipse", kGeometry},
    {"d", "path", kGeometry},
    {"r", "circle", kGeometry},
    {"rx", "ellipse rect", kGeometry},
    {"ry", "ellipse rect", kGeometry},
    {"x", "feimage feblend fecolormatrix fecomponenttransfer fecomposite feconvolvematrix fediffuselighting fedisplacementmap fedropshadow feflood fegaussianblur femerge femorphology feoffset fespecularlighting fetile feturbulence filter foreignobject image mask pattern rect svg symbol text textpath tspan use view", kGeometry},
    {"y", "feimage feblend fecolormatrix fecomponenttransfer fecomposite feconvolvematrix fediffuselighting fedisplacementmap fedropshadow feflood fegaussianblur femerge femorphology feoffset fespecularlighting fetile feturbulence filter foreignobject image mask pattern rect svg symbol text textpath tspan use view", kGeometry},
    {"width", "feimage feblend fecolormatrix fecomponenttransfer fecomposite feconvolvematrix fediffuselighting fedisplacementmap fedropshadow feflood fegaussianblur femerge femorphology feoffset fespecularlighting fetile feturbulence filter foreignobject image mask pattern rect svg symbol use", kGeometry},
    {"height", "feimage feblend fecolormatrix fecomponenttransfer fecomposite feconvolvematrix fediffuselighting fedisplacementmap fedropshadow feflood fegaussianblur femerge femorphology feoffset fespecularlighting fetile feturbulence filter foreignobject image mask pattern rect svg symbol use", kGeometry},
    {"x1", "line lineargradient", kGeometry},
    {"y1", "line lineargradient", kGeometry},
    {"x2", "line lineargradient", kGeometry},
    {"y2", "line lineargradient", kGeometry},
    {"points", "polygon polyline", kGeometry},
    {"pathlength", "circle ellipse line path polygon polyline rect", kGeometry},
    {"dx", "feoffset text tspan", kGeometry},
    {"dy", "feoffset text tspan", kGeometry},
    {"rotate", "animatemotion text tspan", kGeometry},
    {"textlength", "text textpath tspan", kGeometry},
    {"lengthadjust", "text textpath tspan", kGeometry},
    {"startoffset", "textpath", kGeometry},
    {"method", "textpath", kGeometry},
    {"spacing", "textpath", kGeometry},
    {"side", "textpath", kGeometry},

    // Structural and document attributes.
    {"viewbox", "image marker pattern symbol svg view feimage", kGlobal},
    {"preserveaspectratio", "image marker pattern symbol svg view feimage", kGlobal},
    {"zoomandpan", "svg view", kGlobal | kDeprecated},
    {"version", "svg", kGlobal | kDeprecated},
    {"baseprofile", "svg", kGlobal | kDeprecated},
    {"contentscripttype", "svg", kGlobal | kDeprecated},
    {"contentstyletype", "svg", kGlobal | kDeprecated},
    {"xmlns", "svg", kGlobal},
    {"xmlns:xlink", "svg", kGlobal},
    {"xlink:href", "*", kGlobal | kDeprecated | kUrl},
    {"href", "*", kGlobal | kUrl},
    {"target", "a", kGlobal},
    {"rel", "a", kGlobal},
    {"hreflang", "a", kGlobal},
    {"ping", "a", kGlobal | kUrl},
    {"referrerpolicy", "a image script", kGlobal},
    {"download", "a", kGlobal},
    {"crossorigin", "image script feimage", kGlobal},
    {"decoding", "image", kGlobal},
    {"fetchpriority", "image script", kGlobal},
    {"viewtarget", "view", kGlobal},
    {"media", "style", kGlobal},
    {"type", "animatetransform fecolormatrix fecomponenttransfer fefuncr fefuncg fefuncb fefunca feimage feturbulence script style", kGlobal},

    // Gradient and paint-server attributes.
    {"gradientunits", "lineargradient radialgradient", kGlobal},
    {"gradienttransform", "lineargradient radialgradient", kGlobal},
    {"spreadmethod", "lineargradient radialgradient", kGlobal},
    {"fx", "radialgradient", kGeometry},
    {"fy", "radialgradient", kGeometry},
    {"fr", "radialgradient", kGeometry},
    {"offset", "stop fefunca fefuncb fefuncg fefuncr", kGlobal},
    {"patternunits", "pattern", kGlobal},
    {"patterncontentunits", "pattern", kGlobal},
    {"patterntransform", "pattern", kGlobal},
    {"markerunits", "marker", kGlobal},
    {"refx", "marker symbol", kGlobal},
    {"refy", "marker symbol", kGlobal},
    {"markerwidth", "marker", kGlobal},
    {"markerheight", "marker", kGlobal},
    {"orient", "marker", kGlobal},
    {"maskunits", "mask", kGlobal},
    {"maskcontentunits", "mask", kGlobal},
    {"clippathunits", "clippath", kGlobal},
    {"filterunits", "filter", kGlobal},
    {"primitiveunits", "filter", kGlobal},
    {"filterres", "filter", kGlobal | kDeprecated},

    // Filter primitive attributes.
    {"in", "feblend fecolormatrix fecomponenttransfer fecomposite feconvolvematrix fediffuselighting fedisplacementmap fedropshadow feflood fegaussianblur feimage femerge femergenode femorphology feoffset fespecularlighting fetile feturbulence", kFilter},
    {"in2", "feblend fecomposite fedisplacementmap", kFilter},
    {"result", "feblend fecolormatrix fecomponenttransfer fecomposite feconvolvematrix fediffuselighting fedisplacementmap fedropshadow feflood fegaussianblur feimage femerge femorphology feoffset fespecularlighting fetile feturbulence", kFilter},
    {"mode", "feblend", kFilter},
    {"operator", "fecomposite femorphology", kFilter},
    {"k1", "fecomposite", kFilter},
    {"k2", "fecomposite", kFilter},
    {"k3", "fecomposite", kFilter},
    {"k4", "fecomposite", kFilter},
    {"values", "animate fecolormatrix", kFilter | kAnimation},
    {"order", "feconvolvematrix", kFilter},
    {"kernelmatrix", "feconvolvematrix", kFilter},
    {"divisor", "feconvolvematrix", kFilter},
    {"bias", "feconvolvematrix", kFilter},
    {"targetx", "feconvolvematrix", kFilter},
    {"targety", "feconvolvematrix", kFilter},
    {"edgemode", "feconvolvematrix fegaussianblur", kFilter},
    {"kernelunitlength", "feconvolvematrix fediffuselighting fespecularlighting", kFilter},
    {"preservealpha", "feconvolvematrix", kFilter},
    {"surfacescale", "fediffuselighting fespecularlighting", kFilter},
    {"diffuseconstant", "fediffuselighting", kFilter},
    {"specularconstant", "fespecularlighting", kFilter},
    {"specularexponent", "fespecularlighting fespotlight", kFilter},
    {"scale", "fedisplacementmap", kFilter},
    {"xchannelselector", "fedisplacementmap", kFilter},
    {"ychannelselector", "fedisplacementmap", kFilter},
    {"azimuth", "fedistantlight", kFilter},
    {"elevation", "fedistantlight", kFilter},
    {"stddeviation", "fegaussianblur fedropshadow", kFilter},
    {"radius", "femorphology", kFilter},
    {"z", "fepointlight fespotlight", kFilter},
    {"pointsatx", "fespotlight", kFilter},
    {"pointsaty", "fespotlight", kFilter},
    {"pointsatz", "fespotlight", kFilter},
    {"limitingconeangle", "fespotlight", kFilter},
    {"basefrequency", "feturbulence", kFilter},
    {"numoctaves", "feturbulence", kFilter},
    {"seed", "feturbulence", kFilter},
    {"stitchtiles", "feturbulence", kFilter},
    {"tablevalues", "fefuncr fefuncg fefuncb fefunca", kFilter},
    {"slope", "fefuncr fefuncg fefuncb fefunca", kFilter},
    {"intercept", "fefuncr fefuncg fefuncb fefunca", kFilter},
    {"amplitude", "fefuncr fefuncg fefuncb fefunca", kFilter},
    {"exponent", "fefuncr fefuncg fefuncb fefunca", kFilter},

    // Animation attributes.
    {"attributename", "animate animatemotion animatetransform set", kAnimation},
    {"attributetype", "animate animatemotion animatetransform set", kAnimation},
    {"begin", "animate animatemotion animatetransform set", kAnimation},
    {"dur", "animate animatemotion animatetransform set", kAnimation},
    {"end", "animate animatemotion animatetransform set", kAnimation},
    {"min", "animate animatemotion animatetransform set", kAnimation},
    {"max", "animate animatemotion animatetransform set", kAnimation},
    {"restart", "animate animatemotion animatetransform set", kAnimation},
    {"repeatcount", "animate animatemotion animatetransform set", kAnimation},
    {"repeatdur", "animate animatemotion animatetransform set", kAnimation},
    {"calcmode", "animate animatemotion animatetransform", kAnimation},
    {"keytimes", "animate animatemotion animatetransform", kAnimation},
    {"keysplines", "animate animatetransform", kAnimation},
    {"from", "animate animatetransform", kAnimation},
    {"to", "animate animatetransform set", kAnimation},
    {"by", "animate animatetransform", kAnimation},
    {"additive", "animate animatemotion animatetransform", kAnimation},
    {"accumulate", "animate animatemotion animatetransform", kAnimation},
    {"path", "animatemotion", kAnimation},
    {"keypoints", "animatemotion", kAnimation},
    {"origin", "animatemotion", kAnimation},
};

// CSS color keywords (CSS Color Module Level 4). Values are 0xRRGGBB.
struct ColorKeyword {
    const char* name;
    unsigned rgb;
};

const ColorKeyword kColorKeywords[] = {
    {"aliceblue", 0xf0f8ff}, {"antiquewhite", 0xfaebd7}, {"aqua", 0x00ffff}, {"aquamarine", 0x7fffd4},
    {"azure", 0xf0ffff}, {"beige", 0xf5f5dc}, {"bisque", 0xffe4c4}, {"black", 0x000000},
    {"blanchedalmond", 0xffebcd}, {"blue", 0x0000ff}, {"blueviolet", 0x8a2be2}, {"brown", 0xa52a2a},
    {"burlywood", 0xdeb887}, {"cadetblue", 0x5f9ea0}, {"chartreuse", 0x7fff00}, {"chocolate", 0xd2691e},
    {"coral", 0xff7f50}, {"cornflowerblue", 0x6495ed}, {"cornsilk", 0xfff8dc}, {"crimson", 0xdc143c},
    {"cyan", 0x00ffff}, {"darkblue", 0x00008b}, {"darkcyan", 0x008b8b}, {"darkgoldenrod", 0xb8860b},
    {"darkgray", 0xa9a9a9}, {"darkgreen", 0x006400}, {"darkgrey", 0xa9a9a9}, {"darkkhaki", 0xbdb76b},
    {"darkmagenta", 0x8b008b}, {"darkolivegreen", 0x556b2f}, {"darkorange", 0xff8c00}, {"darkorchid", 0x9932cc},
    {"darkred", 0x8b0000}, {"darksalmon", 0xe9967a}, {"darkseagreen", 0x8fbc8f}, {"darkslateblue", 0x483d8b},
    {"darkslategray", 0x2f4f4f}, {"darkslategrey", 0x2f4f4f}, {"darkturquoise", 0x00ced1}, {"darkviolet", 0x9400d3},
    {"deeppink", 0xff1493}, {"deepskyblue", 0x00bfff}, {"dimgray", 0x696969}, {"dimgrey", 0x696969},
    {"dodgerblue", 0x1e90ff}, {"firebrick", 0xb22222}, {"floralwhite", 0xfffaf0}, {"forestgreen", 0x228b22},
    {"fuchsia", 0xff00ff}, {"gainsboro", 0xdcdcdc}, {"ghostwhite", 0xf8f8ff}, {"gold", 0xffd700},
    {"goldenrod", 0xdaa520}, {"gray", 0x808080}, {"green", 0x008000}, {"greenyellow", 0xadff2f},
    {"grey", 0x808080}, {"honeydew", 0xf0fff0}, {"hotpink", 0xff69b4}, {"indianred", 0xcd5c5c},
    {"indigo", 0x4b0082}, {"ivory", 0xfffff0}, {"khaki", 0xf0e68c}, {"lavender", 0xe6e6fa},
    {"lavenderblush", 0xfff0f5}, {"lawngreen", 0x7cfc00}, {"lemonchiffon", 0xfffacd}, {"lightblue", 0xadd8e6},
    {"lightcoral", 0xf08080}, {"lightcyan", 0xe0ffff}, {"lightgoldenrodyellow", 0xfafad2}, {"lightgray", 0xd3d3d3},
    {"lightgreen", 0x90ee90}, {"lightgrey", 0xd3d3d3}, {"lightpink", 0xffb6c1}, {"lightsalmon", 0xffa07a},
    {"lightseagreen", 0x20b2aa}, {"lightskyblue", 0x87cefa}, {"lightslategray", 0x778899}, {"lightslategrey", 0x778899},
    {"lightsteelblue", 0xb0c4de}, {"lightyellow", 0xffffe0}, {"lime", 0x00ff00}, {"limegreen", 0x32cd32},
    {"linen", 0xfaf0e6}, {"magenta", 0xff00ff}, {"maroon", 0x800000}, {"mediumaquamarine", 0x66cdaa},
    {"mediumblue", 0x0000cd}, {"mediumorchid", 0xba55d3}, {"mediumpurple", 0x9370db}, {"mediumseagreen", 0x3cb371},
    {"mediumslateblue", 0x7b68ee}, {"mediumspringgreen", 0x00fa9a}, {"mediumturquoise", 0x48d1cc},
    {"mediumvioletred", 0xc71585}, {"midnightblue", 0x191970}, {"mintcream", 0xf5fffa}, {"mistyrose", 0xffe4e1},
    {"moccasin", 0xffe4b5}, {"navajowhite", 0xffdead}, {"navy", 0x000080}, {"oldlace", 0xfdf5e6},
    {"olive", 0x808000}, {"olivedrab", 0x6b8e23}, {"orange", 0xffa500}, {"orangered", 0xff4500},
    {"orchid", 0xda70d6}, {"palegoldenrod", 0xeee8aa}, {"palegreen", 0x98fb98}, {"paleturquoise", 0xafeeee},
    {"palevioletred", 0xdb7093}, {"papayawhip", 0xffefd5}, {"peachpuff", 0xffdab9}, {"peru", 0xcd853f},
    {"pink", 0xffc0cb}, {"plum", 0xdda0dd}, {"powderblue", 0xb0e0e6}, {"purple", 0x800080},
    {"rebeccapurple", 0x663399}, {"red", 0xff0000}, {"rosybrown", 0xbc8f8f}, {"royalblue", 0x4169e1},
    {"saddlebrown", 0x8b4513}, {"salmon", 0xfa8072}, {"sandybrown", 0xf4a460}, {"seagreen", 0x2e8b57},
    {"seashell", 0xfff5ee}, {"sienna", 0xa0522d}, {"silver", 0xc0c0c0}, {"skyblue", 0x87ceeb},
    {"slateblue", 0x6a5acd}, {"slategray", 0x708090}, {"slategrey", 0x708090}, {"snow", 0xfffafa},
    {"springgreen", 0x00ff7f}, {"steelblue", 0x4682b4}, {"tan", 0xd2b48c}, {"teal", 0x008080},
    {"thistle", 0xd8bfd8}, {"tomato", 0xff6347}, {"turquoise", 0x40e0d0}, {"violet", 0xee82ee},
    {"wheat", 0xf5deb3}, {"white", 0xffffff}, {"whitesmoke", 0xf5f5f5}, {"yellow", 0xffff00},
    {"yellowgreen", 0x9acd32},
};

bool ListContains(const char* list, const char* name) {
    if (list[0] == '*' && list[1] == '\0') {
        return true;
    }
    const std::size_t length = std::strlen(name);
    const char* cursor = list;
    while (*cursor != '\0') {
        while (*cursor == ' ') {
            ++cursor;
        }
        const char* start = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            ++cursor;
        }
        const std::size_t tokenLength = static_cast<std::size_t>(cursor - start);
        if (tokenLength == length && std::strncmp(start, name, length) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

const SvgElementSpec& SvgSpecFor(SvgElementKind kind) {
    const int index = static_cast<int>(kind);
    if (index < 0 || index >= SvgElementCount()) {
        return kElements[static_cast<int>(SvgElementKind::Unknown)];
    }
    return kElements[index];
}

SvgElementKind FindSvgElement(const std::string& lowercaseName) {
    for (const SvgElementSpec& spec : kElements) {
        if (lowercaseName == spec.name) {
            return spec.kind;
        }
    }
    return SvgElementKind::Unknown;
}

bool IsSvgElementName(const std::string& lowercaseName) {
    return FindSvgElement(lowercaseName) != SvgElementKind::Unknown;
}

bool SvgElementNeverRendered(SvgElementKind kind) {
    return (SvgSpecFor(kind).traits & SVG_TRAIT_NEVER_RENDERED) != 0;
}

int SvgElementCount() {
    return static_cast<int>(sizeof(kElements) / sizeof(kElements[0]));
}

bool SvgCatalogSelfCheck(std::string& error) {
    for (int index = 0; index < SvgElementCount(); ++index) {
        const SvgElementSpec& spec = kElements[index];
        if (spec.kind != static_cast<SvgElementKind>(index)) {
            error = std::string("svg element table is out of order at ") + spec.name;
            return false;
        }
        if (spec.kind == SvgElementKind::Unknown) {
            continue;
        }
        if (spec.name == nullptr || spec.name[0] == '\0') {
            error = "svg element table has an empty name";
            return false;
        }
        if (FindSvgElement(spec.name) != spec.kind) {
            error = std::string("svg element lookup mismatch for ") + spec.name;
            return false;
        }
    }
    if (SvgElementCount() != static_cast<int>(SvgElementKind::Count)) {
        error = "svg element count does not match the Count sentinel";
        return false;
    }
    return true;
}

const SvgAttributeRule* FindSvgAttributeRule(const std::string& lowercaseName) {
    for (const SvgAttributeRule& rule : kAttributes) {
        if (lowercaseName == rule.name) {
            return &rule;
        }
    }
    return nullptr;
}

bool IsSvgAttributeName(const std::string& lowercaseName) {
    return FindSvgAttributeRule(lowercaseName) != nullptr;
}

bool IsSvgPresentationAttribute(const std::string& lowercaseName) {
    for (const SvgAttributeRule& rule : kAttributes) {
        if (lowercaseName == rule.name) {
            return (rule.flags & SVG_ATTR_PRESENTATION) != 0;
        }
    }
    return false;
}

bool SvgAttributeAppliesTo(const std::string& lowercaseName, SvgElementKind kind) {
    const std::string element = SvgSpecFor(kind).name;
    for (const SvgAttributeRule& rule : kAttributes) {
        if (lowercaseName != rule.name) {
            continue;
        }
        if (ListContains(rule.elements, element.c_str())) {
            return true;
        }
    }
    return false;
}

int SvgAttributeCount() {
    return static_cast<int>(sizeof(kAttributes) / sizeof(kAttributes[0]));
}

bool SvgAttributeCatalogSelfCheck(std::string& error) {
    for (const SvgAttributeRule& rule : kAttributes) {
        if (rule.name == nullptr || rule.name[0] == '\0') {
            error = "svg attribute table has an empty name";
            return false;
        }
        if (rule.elements == nullptr || rule.elements[0] == '\0') {
            error = std::string("svg attribute '") + rule.name + "' has no element list";
            return false;
        }
        if (rule.elements[0] != '*') {
            const char* cursor = rule.elements;
            while (*cursor != '\0') {
                while (*cursor == ' ') {
                    ++cursor;
                }
                const char* start = cursor;
                while (*cursor != '\0' && *cursor != ' ') {
                    ++cursor;
                }
                if (cursor == start) {
                    break;
                }
                const std::string element(start, static_cast<std::size_t>(cursor - start));
                if (FindSvgElement(element) == SvgElementKind::Unknown) {
                    error = std::string("svg attribute '") + rule.name + "' names unknown element '" + element + "'";
                    return false;
                }
            }
        }
    }
    return true;
}

bool LookupSvgColorKeyword(const std::string& lowercaseName, std::uint8_t& red, std::uint8_t& green, std::uint8_t& blue) {
    for (const ColorKeyword& keyword : kColorKeywords) {
        if (lowercaseName != keyword.name) {
            continue;
        }
        red = static_cast<std::uint8_t>((keyword.rgb >> 16) & 0xff);
        green = static_cast<std::uint8_t>((keyword.rgb >> 8) & 0xff);
        blue = static_cast<std::uint8_t>(keyword.rgb & 0xff);
        return true;
    }
    return false;
}

}
