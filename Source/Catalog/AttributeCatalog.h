#pragma once

#include "Catalog/ElementCatalog.h"

#include <string>

namespace RoboWeb {

struct AttributeRule {
    const char* name;
    const char* elements;
    unsigned flags;
};

constexpr unsigned ATTRIBUTE_GLOBAL = 1u << 0;
constexpr unsigned ATTRIBUTE_BOOLEAN = 1u << 1;
constexpr unsigned ATTRIBUTE_OBSOLETE = 1u << 2;
constexpr unsigned ATTRIBUTE_EVENT = 1u << 3;
constexpr unsigned ATTRIBUTE_URL = 1u << 4;

const AttributeRule* FindAttributeRule(const std::string& lowercaseName);
bool IsGlobalAttributeName(const std::string& lowercaseName);
bool IsBooleanAttribute(const std::string& lowercaseName);
bool IsEventHandlerName(const std::string& lowercaseName);
bool IsDataAttribute(const std::string& lowercaseName);
bool IsAriaAttribute(const std::string& lowercaseName);
bool AttributeAppliesTo(const std::string& lowercaseName, ElementKind kind);
bool IsKnownAttribute(const std::string& lowercaseName);
const char* const* EventHandlerNames();
int EventHandlerCount();

}
