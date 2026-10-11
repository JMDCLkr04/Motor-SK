#pragma once

#include <string>

namespace sk {
namespace catalog {

// Fuente unica de datos de las clases del motor. Alimenta la vista de
// Referencia de clases, las descripciones y valores por defecto de
// Properties, el buscador del boton "+" del Explorer y los iconos de cada
// clase. No se mantienen copias en otros sitios.
//
// El documento `docs/clases.md` describe las clases y sus propiedades; los
// valores por defecto de cada propiedad viven aqui (DocsView los muestra).

// Categoria de clases (19 en total). "icon" es el emoji que se usa como
// logo provisional de cada clase.
struct Category {
    const wchar_t* icon;
    const char* name;
};

// Definicion de una propiedad de una clase.
struct PropDef {
    const char* name;
    const char* type;   // "text", "number", "bool", "Vector3", "color", ...
    const char* def;    // valor por defecto (texto)
    const char* desc;
};

// Definicion de una clase.
struct ClassDef {
    int cat;               // indice en categories()
    const char* name;
    const char* base;      // clase de la que hereda ("" = clase base)
    bool abstractCls;      // las clases abstractas no se pueden insertar
    const char* shortDesc; // descripcion corta (Explorer / lista)
    const char* longDesc;  // descripcion detallada (Referencia de clases)
    const PropDef* props;  // tabla de propiedades (nullptr si no hay)
    int propCount;
};

int categoryCount();
const Category* categories();

int classCount();
const ClassDef* classes();

// Busca una clase por nombre; nullptr si no existe.
const ClassDef* findClass(const std::string& name);

// Indice de la categoria de una clase dada su nombre; -1 si no existe.
int categoryOf(const std::string& className);

// true si la clase puede insertarse con el boton "+" (no abstracta y no
// es un servicio/evento del sistema).
bool insertable(const ClassDef& c);

// true si la clase es un valor de Light.Type (no se inserta; se elige en
// Properties sobre una clase Light).
bool isLightType(const std::string& className);

} // namespace catalog
} // namespace sk
