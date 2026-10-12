#include "property_model.h"

#include <algorithm>
#include <cctype>

#include "scene/property_value.h"

namespace sk {

namespace {

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool matchesSearch(const PropertyRow& row, const std::string& needle) {
    if (needle.empty()) return true;
    const std::string n = toLower(needle);
    return toLower(row.name).find(n) != std::string::npos ||
           toLower(row.desc).find(n) != std::string::npos;
}

// El objeto tiene la propiedad `prop` en su clase.
bool hasProp(const ClassRegistry& reg, const Instance& inst,
             const std::string& prop) {
    const int ci = reg.indexOf(inst.className());
    return reg.valueIndex(ci, prop) >= 0;
}

// Orden canonico: el que pide el diseno del panel. Las secciones no
// listadas se anaden despues en el orden en que aparecen.
int sectionPriority(const std::string& section) {
    const std::vector<std::string>& order = propertySectionOrder();
    for (int i = 0; i < static_cast<int>(order.size()); ++i) {
        if (order[i] == section) return i;
    }
    return static_cast<int>(order.size());
}

} // namespace

const std::vector<std::string>& propertySectionOrder() {
    static const std::vector<std::string> kOrder = {
        "Appearance", "Data",  "Transform", "Pivot", "Collision",
        "Model",      "Workspace", "Camera", "Lighting", "Sky",
        "Atmosphere", "Light", "Humanoid", "Spawn", "Sound",
    };
    return kOrder;
}

std::vector<PropertyRow> buildPropertyRows(
    const ClassRegistry& reg,
    const std::vector<const Instance*>& selection,
    const std::string& search) {
    std::vector<PropertyRow> rows;
    if (selection.empty() || !selection.front() || !selection.front()->cls()) {
        return rows;
    }
    const Instance* primary = selection.front();
    const std::vector<PropertyDef>& eff = primary->cls()->effective;

    // Propiedades comunes a TODA la seleccion, en el orden de la clase del
    // primer objeto. Name/Parent se omiten (el panel los pinta aparte).
    for (const PropertyDef& def : eff) {
        const std::string name = def.name;
        if (name == "Name" || name == "Parent") continue;

        bool common = true;
        for (const Instance* inst : selection) {
            if (!inst || !hasProp(reg, *inst, name)) {
                common = false;
                break;
            }
        }
        if (!common) continue;

        PropertyRow row;
        row.section = def.section ? def.section : "Data";
        row.name = name;
        row.type = def.type;
        row.readOnly = def.readOnly;
        row.desc = def.desc ? def.desc : "";
        row.enumOptions = def.enumOptions ? def.enumOptions : "";
        row.def = serializePropertyValue(defaultPropertyValue(def), def.type);

        std::string first;
        bool mixed = false;
        bool anyDiffers = false;
        for (size_t i = 0; i < selection.size(); ++i) {
            const Instance* inst = selection[i];
            const int slot = inst->slotOf(name);
            const PropertyValue& val = inst->valueBySlot(slot);
            const std::string text = serializePropertyValue(val, def.type);
            if (i == 0) {
                first = text;
                row.ref = val.ref;
            } else if (text != first) {
                mixed = true;
            }
            if (propertyValueDiffersFromDefault(val, def)) anyDiffers = true;
        }
        row.mixed = mixed;
        row.value = mixed ? std::string() : first;
        row.isDefault = !mixed && !anyDiffers;

        if (matchesSearch(row, search)) rows.push_back(std::move(row));
    }

    // Orden estable por seccion canonica (conserva el orden de las
    // propiedades dentro de cada seccion).
    std::stable_sort(rows.begin(), rows.end(),
                     [](const PropertyRow& a, const PropertyRow& b) {
                         return sectionPriority(a.section) <
                                sectionPriority(b.section);
                     });
    return rows;
}

std::vector<std::string> propertyRowSections(
    const std::vector<PropertyRow>& rows) {
    std::vector<std::string> sections;
    for (const PropertyRow& row : rows) {
        if (std::find(sections.begin(), sections.end(), row.section) ==
            sections.end()) {
            sections.push_back(row.section);
        }
    }
    return sections;
}

} // namespace sk
