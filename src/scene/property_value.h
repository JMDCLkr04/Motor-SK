#pragma once

#include <string>

#include "class_registry.h"
#include "instance.h"

namespace sk {

// Logica pura de conversion entre PropertyValue y su texto canonico (el
// mismo que usa el archivo de escena v2). La comparten la persistencia
// (World) y el panel Properties, para que "valor por defecto", "mixto" y
// "restaurar" tengan una unica fuente de verdad.
//
// Formato canonico: bool "true"/"false", int/float numerico sin ceros de
// relleno, Vec3/Color "x y z" (tambien acepta comas al leer), enum/string
// el propio texto y RefObject el id (vacio = ninguno).

std::string serializePropertyValue(const PropertyValue& value, PropType type);

// Valor por defecto declarado por el registro para esa propiedad.
PropertyValue defaultPropertyValue(const PropertyDef& def);

// Lee un valor desde texto. Devuelve false solo si el texto esta vacio o
// mal formado de forma insalvable (p. ej. Vec3 sin ningun numero).
bool parsePropertyValue(const std::string& text, PropType type,
                        PropertyValue& out);

// true si el valor difiere del default del registro (define si el panel
// muestra el boton "restaurar").
bool propertyValueDiffersFromDefault(const PropertyValue& value,
                                     const PropertyDef& def);

// Utilidad compartida de lectura de "x, y, z" (acepta comas o espacios).
bool parseVec3Text(const std::string& text, Vec3& out);

} // namespace sk
