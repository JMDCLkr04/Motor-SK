#pragma once

#include <string>
#include <vector>

#include "scene/class_registry.h"
#include "scene/instance.h"

namespace sk {

// Fila de Properties derivada del ClassRegistry y de la seleccion actual.
// El panel la pinta; toda la logica (que propiedades son comunes, cuando un
// valor esta "mixto", cuando coincide con el default, ...) vive aqui y es
// pura (sin Win32) para poder probarla en motorsk_model.
struct PropertyRow {
    std::string section;      // "Transform", "Appearance", ...
    std::string name;         // nombre de la propiedad
    PropType type = PropType::String;
    bool readOnly = false;
    bool mixed = false;       // los valores no coinciden entre los objetos
    bool isDefault = true;    // coincide con el valor por defecto del registro
    std::string value;        // texto canonico (vacio cuando mixed)
    std::string def;          // default canonico del registro
    std::string desc;         // tooltip (descripcion del registro)
    std::string enumOptions;  // "Cube,Rhombus,..." (solo Enum)
    InstanceId ref = kInvalidId; // solo RefObject (para resolver el nombre)
};

// Propiedades comunes a toda la seleccion (1..N objetos), agrupadas por
// seccion. Name y Parent se omiten (los pinta el panel aparte). `search`
// filtra por nombre o descripcion sin distinguir mayusculas; vacio = todo.
std::vector<PropertyRow> buildPropertyRows(
    const ClassRegistry& reg,
    const std::vector<const Instance*>& selection,
    const std::string& search = std::string());

// Secciones presentes en las filas, en orden de aparicion.
std::vector<std::string> propertyRowSections(
    const std::vector<PropertyRow>& rows);

// Orden canonico de las secciones del panel (las desconocidas van al
// final, en orden de aparicion). Expuesto para las pruebas.
const std::vector<std::string>& propertySectionOrder();

} // namespace sk
