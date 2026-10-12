#pragma once

#include <string>
#include <vector>

namespace sk {

// Tipo de valor de una propiedad. Determina como se interpreta y como
// se serializa (archivo de escena v2 y panel Properties).
enum class PropType {
    Bool,       // casilla
    Int,        // entero
    Float,      // decimal
    String,     // texto
    Vec3,       // vector de 3 floats
    Color,      // color (se guarda como Vec3 0..1)
    Enum,       // valor de una lista (options)
    RefObject,  // referencia a otra Instance (InstanceId)
};

const char* propTypeName(PropType type);

// Categoria del catalogo (19 en total). "icon" es el emoji provisional.
struct Category {
    const wchar_t* icon;
    const char* name;
};

// Definicion de una propiedad de una clase.
struct PropertyDef {
    const char* name;
    PropType type;
    const char* def;         // valor por defecto (texto canonico)
    const char* section;     // "Transform", "Appearance", "Data", ...
    const char* desc;        // descripcion corta
    bool readOnly;
    const char* enumOptions; // "Point,Spot,Surface" (solo Enum), o nullptr
};

// Definicion de una clase del motor. La tabla vive en class_registry.cpp.
// "insertable" y las listas "effective" (propiedades heredadas de la
// cadena de bases, incluyendo Name/Parent inherentes) se calculan una
// sola vez al construir el registro.
struct ClassDef {
    const char* name;
    int category;                 // indice en categories()
    const char* base;             // clase base ("" = raiz)
    bool abstractCls;             // las abstractas no se pueden insertar
    const char* shortDesc;        // descripcion (Explorer / lista)
    const char* longDesc;         // descripcion detallada (Referencia)
    const wchar_t* icon;          // id de icono (emoji); nullptr = el de la categoria
    const PropertyDef* props;     // propiedades propias (nullptr si no hay)
    int propCount;
    const char* const* suggestedChildren; // clases sugeridas (no restringe)
    int suggestedCount;

    // --- calculado por ClassRegistry ---
    bool insertable = true;
    std::vector<PropertyDef> effective; // Name/Parent + heredadas + propias
};

// Tabla C++ (no un archivo que se parsee) con TODAS las clases del motor.
// Fuente unica de datos: alimenta la Referencia de clases, las
// descripciones y valores por defecto de Properties, el buscador "+" y
// los iconos. El registro se construye en el primer acceso (singleton).
class ClassRegistry {
public:
    static const ClassRegistry& get();

    int categoryCount() const { return static_cast<int>(categories_.size()); }
    const Category* categories() const { return categories_.data(); }

    int classCount() const { return static_cast<int>(classes_.size()); }
    const ClassDef* classes() const { return classes_.data(); }

    // Clase por nombre; nullptr si no existe.
    const ClassDef* find(const std::string& name) const;
    // Indice de la clase; -1 si no existe.
    int indexOf(const std::string& name) const;
    // Clase por indice (valido en [0, classCount())).
    const ClassDef& at(int index) const { return classes_[index]; }

    bool isRegistered(const std::string& name) const { return find(name) != nullptr; }
    bool isInsertable(const std::string& name) const;
    // true si el nombre es un valor de Light.Type (Point/Spot/Surface).
    bool isLightType(const std::string& name) const;

    // Indice de la categoria de una clase (por nombre); -1 si no existe.
    int categoryOf(const std::string& className) const;

    // --- Propiedades almacenables (excluye Name/Parent) ---
    // Numero de valores propios de la clase (para dimensionar el vector).
    int valueCount(int classIndex) const;
    // Definicion del valor i (0 <= i < valueCount).
    const PropertyDef& valueProp(int classIndex, int valueIndex) const;
    // Indice del valor llamado "prop" dentro de la clase; -1 si no existe
    // (name="Position" da su hueco en el vector de valores).
    int valueIndex(int classIndex, const std::string& prop) const;
    // Busca en la lista efectiva (incluye Name/Parent). nullptr si no esta.
    const PropertyDef* findProp(int classIndex, const std::string& prop) const;

private:
    ClassRegistry();
    std::vector<Category> categories_;
    std::vector<ClassDef> classes_;
};

} // namespace sk
