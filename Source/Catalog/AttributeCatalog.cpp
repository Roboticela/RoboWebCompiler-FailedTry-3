#include "Catalog/AttributeCatalog.h"

#include <cstring>

namespace RoboWeb {

namespace {

constexpr unsigned kGlobal = ATTRIBUTE_GLOBAL;
constexpr unsigned kBool = ATTRIBUTE_BOOLEAN;
constexpr unsigned kOld = ATTRIBUTE_OBSOLETE;
constexpr unsigned kEvent = ATTRIBUTE_EVENT;
constexpr unsigned kUrl = ATTRIBUTE_URL;

const AttributeRule kRules[] = {
    {"accept", "form input", 0},
    {"accept-charset", "form", 0},
    {"accesskey", "*", kGlobal},
    {"action", "form", kUrl},
    {"align", "caption col colgroup div h1 h2 h3 h4 h5 h6 hr iframe img legend marquee object p table tbody td tfoot th thead tr", kOld},
    {"allow", "iframe", 0},
    {"allowfullscreen", "iframe", kBool},
    {"alpha", "input", 0},
    {"alt", "area img input", 0},
    {"anchor", "*", kGlobal},
    {"as", "link", 0},
    {"async", "script", kBool},
    {"autocapitalize", "*", kGlobal},
    {"autocomplete", "form input select textarea", 0},
    {"autocorrect", "*", kGlobal},
    {"autofocus", "*", kGlobal | kBool},
    {"autoplay", "audio video", kBool},
    {"autolocate", "geolocation", kBool},
    {"background", "body table td th", kOld | kUrl},
    {"baseprofile", "svg", 0},
    {"behavior", "marquee", kOld},
    {"bgcolor", "body col colgroup marquee table tbody td tfoot th thead tr", kOld},
    {"border", "img object table", kOld},
    {"capture", "input", 0},
    {"cellpadding", "table", kOld},
    {"cellspacing", "table", kOld},
    {"charset", "meta script", 0},
    {"checked", "input", kBool},
    {"cite", "blockquote del ins q", kUrl},
    {"class", "*", kGlobal},
    {"clear", "br", kOld},
    {"color", "font hr", kOld},
    {"cols", "textarea frameset", 0},
    {"colspan", "td th", 0},
    {"command", "button", 0},
    {"commandfor", "button", 0},
    {"content", "meta", 0},
    {"contenteditable", "*", kGlobal},
    {"controls", "audio video", kBool},
    {"coords", "area", 0},
    {"crossorigin", "audio img link script video", 0},
    {"data", "object", kUrl},
    {"datetime", "del ins time", 0},
    {"decoding", "img", 0},
    {"default", "track", kBool},
    {"defer", "script", kBool},
    {"dir", "*", kGlobal},
    {"direction", "marquee", kOld},
    {"dirname", "input textarea", 0},
    {"disabled", "button fieldset input optgroup option select textarea", kBool},
    {"download", "a area", 0},
    {"draggable", "*", kGlobal},
    {"enctype", "form", 0},
    {"enterkeyhint", "*", kGlobal},
    {"exportparts", "*", kGlobal},
    {"face", "font", kOld},
    {"fetchpriority", "img link script", 0},
    {"for", "label output", 0},
    {"form", "button fieldset input object output select textarea", 0},
    {"formaction", "button input", kUrl},
    {"formenctype", "button input", 0},
    {"formmethod", "button input", 0},
    {"formnovalidate", "button input", kBool},
    {"formtarget", "button input", 0},
    {"frameborder", "iframe frame", kOld},
    {"headers", "td th", 0},
    {"headingoffset", "*", kGlobal},
    {"headingreset", "*", kGlobal | kBool},
    {"height", "canvas embed fencedframe iframe img input object td th video marquee", 0},
    {"hidden", "*", kGlobal},
    {"high", "meter", 0},
    {"href", "a area base link", kUrl},
    {"hreflang", "a link", 0},
    {"hspace", "img object marquee", kOld},
    {"http-equiv", "meta", 0},
    {"id", "*", kGlobal},
    {"imagesizes", "link", 0},
    {"imagesrcset", "link", 0},
    {"inert", "*", kGlobal | kBool},
    {"inputmode", "*", kGlobal},
    {"integrity", "link script", 0},
    {"is", "*", kGlobal},
    {"ismap", "img", kBool},
    {"itemid", "*", kGlobal},
    {"itemprop", "*", kGlobal},
    {"itemref", "*", kGlobal},
    {"itemscope", "*", kGlobal | kBool},
    {"itemtype", "*", kGlobal},
    {"kind", "track", 0},
    {"label", "optgroup option track", 0},
    {"lang", "*", kGlobal},
    {"list", "input", 0},
    {"loading", "img iframe", 0},
    {"loop", "audio marquee video", 0},
    {"low", "meter", 0},
    {"marginheight", "iframe frame", kOld},
    {"marginwidth", "iframe frame", kOld},
    {"max", "input meter progress", 0},
    {"maxlength", "input textarea", 0},
    {"media", "a area link source style", 0},
    {"method", "form", 0},
    {"min", "input meter", 0},
    {"minlength", "input textarea", 0},
    {"multiple", "input select", kBool},
    {"muted", "audio video", kBool},
    {"name", "a button embed fieldset form iframe input map meta object output param select slot textarea", 0},
    {"nomodule", "script", kBool},
    {"nonce", "*", kGlobal},
    {"noresize", "frame", kOld | kBool},
    {"novalidate", "form", kBool},
    {"nowrap", "td th", kOld | kBool},
    {"open", "details dialog", kBool},
    {"optimum", "meter", 0},
    {"part", "*", kGlobal},
    {"pattern", "input", 0},
    {"ping", "a area", kUrl},
    {"placeholder", "input textarea", 0},
    {"playsinline", "video", kBool},
    {"popover", "*", kGlobal},
    {"popovertarget", "button input", 0},
    {"popovertargetaction", "button input", 0},
    {"poster", "video", kUrl},
    {"preload", "audio video", 0},
    {"readonly", "input textarea", kBool},
    {"referrerpolicy", "a area iframe img link script", 0},
    {"rel", "a area form link", 0},
    {"required", "input select textarea", kBool},
    {"reversed", "ol", kBool},
    {"role", "*", kGlobal},
    {"rows", "textarea frameset", 0},
    {"rowspan", "td th", 0},
    {"rules", "table", kOld},
    {"sandbox", "iframe fencedframe", 0},
    {"scope", "th", 0},
    {"scrolling", "iframe frame", kOld},
    {"selected", "option", kBool},
    {"shadowrootclonable", "template", kBool},
    {"shadowrootdelegatesfocus", "template", kBool},
    {"shadowrootmode", "template", 0},
    {"shadowrootserializable", "template", kBool},
    {"shape", "area", 0},
    {"size", "font hr input select", 0},
    {"sizes", "img link source", 0},
    {"slot", "*", kGlobal},
    {"span", "col colgroup", 0},
    {"spellcheck", "*", kGlobal},
    {"src", "audio embed fencedframe frame iframe img input script source track video", kUrl},
    {"srcdoc", "iframe", 0},
    {"srclang", "track", 0},
    {"srcset", "img source", 0},
    {"start", "ol", 0},
    {"step", "input", 0},
    {"style", "*", kGlobal},
    {"summary", "table", kOld},
    {"tabindex", "*", kGlobal},
    {"target", "a area base form", 0},
    {"text", "body", kOld},
    {"title", "*", kGlobal},
    {"translate", "*", kGlobal},
    {"truespeed", "marquee", kOld | kBool},
    {"type", "a button embed input li link menu object ol param script source style ul", 0},
    {"usemap", "img object", 0},
    {"valign", "col colgroup tbody td tfoot th thead tr", kOld},
    {"value", "button data input li meter option param progress", 0},
    {"version", "svg", 0},
    {"valuetype", "param", kOld},
    {"virtualkeyboardpolicy", "*", kGlobal},
    {"vlink", "body", kOld},
    {"vspace", "img object marquee", kOld},
    {"watch", "geolocation", kBool},
    {"width", "canvas col colgroup embed fencedframe hr iframe img input marquee object pre table td th video", 0},
    {"wrap", "textarea", 0},
    {"writingsuggestions", "*", kGlobal},
    {"xmlns", "html", 0},
    {"alink", "body", kOld},
    {"link", "body", kOld},
    {"xml:lang", "*", kGlobal | kOld},
    {"xml:base", "*", kGlobal | kOld | kUrl},
    {"scrollamount", "marquee", kOld},
    {"scrolldelay", "marquee", kOld},
    {"frame", "table", kOld},
    {"char", "col colgroup tbody td tfoot th thead tr", kOld},
    {"charoff", "col colgroup tbody td tfoot th thead tr", kOld},
    {"axis", "td th", kOld},
    {"abbr", "td th", 0},
    {"manifest", "html", kOld | kUrl},
    {"blocking", "link script style", 0},
    {"imagesrcset", "link", 0},
};

const char* kEvents[] = {
    "onabort", "onanimationcancel", "onanimationend", "onanimationiteration", "onanimationstart",
    "onauxclick", "onbeforeinput", "onbeforematch", "onbeforetoggle", "onblur", "oncancel",
    "oncanplay", "oncanplaythrough", "onchange", "onclick", "onclose", "oncommand",
    "oncontentvisibilityautostatechange", "oncontextlost", "oncontextmenu", "oncontextrestored",
    "oncopy", "oncuechange", "oncut", "ondblclick", "ondrag", "ondragend", "ondragenter",
    "ondragleave", "ondragover", "ondragstart", "ondrop", "ondurationchange", "onemptied",
    "onended", "onerror", "onfocus", "onfocusin", "onfocusout", "onformdata", "onfullscreenchange",
    "onfullscreenerror", "ongotpointercapture", "oninput", "oninvalid", "onkeydown", "onkeypress",
    "onkeyup", "onload", "onloadeddata", "onloadedmetadata", "onloadstart", "onlostpointercapture",
    "onmousedown", "onmouseenter", "onmouseleave", "onmousemove", "onmouseout", "onmouseover",
    "onmouseup", "onpaste", "onpause", "onplay", "onplaying", "onpointercancel", "onpointerdown",
    "onpointerenter", "onpointerleave", "onpointermove", "onpointerout", "onpointerover",
    "onpointerrawupdate", "onpointerup", "onprogress", "onratechange", "onreset", "onresize",
    "onscroll", "onscrollend", "onsecuritypolicyviolation", "onseeked", "onseeking", "onselect",
    "onselectionchange", "onselectstart", "onslotchange", "onstalled", "onsubmit", "onsuspend",
    "ontimeupdate", "ontoggle", "ontouchcancel", "ontouchend", "ontouchmove", "ontouchstart",
    "ontransitioncancel", "ontransitionend", "ontransitionrun", "ontransitionstart",
    "onvolumechange", "onwaiting", "onwheel",
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

const AttributeRule* FindAttributeRule(const std::string& lowercaseName) {
    for (const AttributeRule& rule : kRules) {
        if (lowercaseName == rule.name) {
            return &rule;
        }
    }
    return nullptr;
}

bool IsGlobalAttributeName(const std::string& lowercaseName) {
    const AttributeRule* rule = FindAttributeRule(lowercaseName);
    return rule != nullptr && (rule->flags & ATTRIBUTE_GLOBAL) != 0;
}

bool IsBooleanAttribute(const std::string& lowercaseName) {
    const AttributeRule* rule = FindAttributeRule(lowercaseName);
    return rule != nullptr && (rule->flags & ATTRIBUTE_BOOLEAN) != 0;
}

bool IsEventHandlerName(const std::string& lowercaseName) {
    if (lowercaseName.size() < 3 || lowercaseName[0] != 'o' || lowercaseName[1] != 'n') {
        return false;
    }
    for (const char* eventName : kEvents) {
        if (lowercaseName == eventName) {
            return true;
        }
    }
    return false;
}

bool IsDataAttribute(const std::string& lowercaseName) {
    return lowercaseName.size() > 5 && lowercaseName.compare(0, 5, "data-") == 0;
}

bool IsAriaAttribute(const std::string& lowercaseName) {
    return lowercaseName == "role" || (lowercaseName.size() > 5 && lowercaseName.compare(0, 5, "aria-") == 0);
}

bool AttributeAppliesTo(const std::string& lowercaseName, ElementKind kind) {
    if (IsDataAttribute(lowercaseName) || IsAriaAttribute(lowercaseName) || IsEventHandlerName(lowercaseName)) {
        return true;
    }
    const AttributeRule* rule = FindAttributeRule(lowercaseName);
    if (rule == nullptr) {
        return false;
    }
    if ((rule->flags & ATTRIBUTE_GLOBAL) != 0) {
        return true;
    }
    return ListContains(rule->elements, SpecFor(kind).name);
}

bool IsKnownAttribute(const std::string& lowercaseName) {
    if (IsDataAttribute(lowercaseName) || IsAriaAttribute(lowercaseName) || IsEventHandlerName(lowercaseName)) {
        return true;
    }
    return FindAttributeRule(lowercaseName) != nullptr;
}

bool IsSvgAttribute(const std::string& lowercaseName) {
    return ListContains(
        "cx cy d fill filter gradientunits in offset opacity r result rx ry stddeviation stop-color stroke "
        "stroke-miterlimit stroke-width transform viewbox x1 x2 xmlns:xlink y1 y2",
        lowercaseName.c_str());
}

const char* const* EventHandlerNames() {
    return kEvents;
}

int EventHandlerCount() {
    return static_cast<int>(sizeof(kEvents) / sizeof(kEvents[0]));
}

}
