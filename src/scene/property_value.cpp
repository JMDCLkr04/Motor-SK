#include "property_value.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace sk {

namespace {

std::string trimNumber(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

long long defaultInt(const char* def) { return std::atoll(def ? def : "0"); }
double defaultFloat(const char* def) { return std::atof(def ? def : "0"); }
bool defaultBool(const char* def) {
    return def && (std::strcmp(def, "true") == 0 ||
                   std::strcmp(def, "1") == 0);
}

std::string serializeVec3(const Vec3& v) {
    return trimNumber(v.x) + " " + trimNumber(v.y) + " " + trimNumber(v.z);
}

} // namespace

bool parseVec3Text(const std::string& text, Vec3& out) {
    std::string t = text;
    for (char& c : t) {
        if (c == ',') c = ' ';
    }
    std::istringstream ss(t);
    float x = 0, y = 0, z = 0;
    if (!(ss >> x)) return false;
    ss >> y >> z;
    out = {x, y, z};
    return true;
}

std::string serializePropertyValue(const PropertyValue& val, PropType type) {
    switch (type) {
        case PropType::Bool:   return val.b ? "true" : "false";
        case PropType::Int:    return std::to_string(val.i);
        case PropType::Float:  return trimNumber(val.f);
        case PropType::String: return val.s;
        case PropType::Vec3:   return serializeVec3(val.v);
        case PropType::Color:  return serializeVec3(val.v);
        case PropType::Enum:   return val.s;
        case PropType::RefObject:
            return val.ref == kInvalidId ? std::string()
                                         : std::to_string(val.ref);
    }
    return {};
}

PropertyValue defaultPropertyValue(const PropertyDef& def) {
    PropertyValue v;
    switch (def.type) {
        case PropType::Bool:  v.b = defaultBool(def.def); break;
        case PropType::Int:   v.i = defaultInt(def.def); break;
        case PropType::Float: v.f = defaultFloat(def.def); break;
        case PropType::String: v.s = def.def ? def.def : ""; break;
        case PropType::Vec3:
        case PropType::Color:
            parseVec3Text(def.def ? def.def : "0 0 0", v.v);
            break;
        case PropType::Enum:  v.s = def.def ? def.def : ""; break;
        case PropType::RefObject: v.ref = kInvalidId; break;
    }
    return v;
}

bool parsePropertyValue(const std::string& text, PropType type,
                        PropertyValue& out) {
    switch (type) {
        case PropType::Bool:
            out.b = (text == "true" || text == "1");
            break;
        case PropType::Int:
            out.i = std::atoll(text.c_str());
            break;
        case PropType::Float:
            out.f = std::atof(text.c_str());
            break;
        case PropType::String:
            out.s = text;
            break;
        case PropType::Vec3:
        case PropType::Color:
            if (!parseVec3Text(text, out.v)) return false;
            break;
        case PropType::Enum:
            out.s = text;
            break;
        case PropType::RefObject:
            out.ref = text.empty() ? kInvalidId
                                   : static_cast<InstanceId>(
                                         std::strtoul(text.c_str(), nullptr, 10));
            break;
    }
    return true;
}

bool propertyValueDiffersFromDefault(const PropertyValue& val,
                                     const PropertyDef& def) {
    const PropertyValue defVal = defaultPropertyValue(def);
    switch (def.type) {
        case PropType::Bool:  return val.b != defVal.b;
        case PropType::Int:   return val.i != defVal.i;
        case PropType::Float: return std::abs(val.f - defVal.f) > 1e-6;
        case PropType::String: return val.s != defVal.s;
        case PropType::Vec3:
        case PropType::Color:
            return std::abs(val.v.x - defVal.v.x) > 1e-6 ||
                   std::abs(val.v.y - defVal.v.y) > 1e-6 ||
                   std::abs(val.v.z - defVal.v.z) > 1e-6;
        case PropType::Enum:  return val.s != defVal.s;
        case PropType::RefObject: return val.ref != defVal.ref;
    }
    return false;
}

} // namespace sk
