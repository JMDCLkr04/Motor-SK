#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../math/math.h"
#include "class_registry.h"
#include "shape.h"

namespace sk {

// Identificador de una Instance. 0 = invalido. Los ids NO se reutilizan
// durante la sesion: cada alta recibe uno nuevo, aunque se borren nodos.
using InstanceId = uint32_t;
inline constexpr InstanceId kInvalidId = 0;

// Attribute tipado de un objeto (seccion Attributes de Properties). El
// tipo es "text", "number", "bool", "vector3" o "color"; el valor se
// guarda como texto y cada tipo lo interpreta a su manera.
struct Attribute {
    std::string name;
    std::string type = "text";
    std::string value;
    bool operator==(const Attribute&) const = default;
};

// Valor de una propiedad. El tipo activo lo determina la definicion de
// la propiedad en el ClassRegistry (Instance no lo duplica).
struct PropertyValue {
    bool b = false;
    long long i = 0;
    double f = 0.0;
    std::string s;
    Vec3 v{};
    InstanceId ref = kInvalidId;
};

// Nodo del arbol de escena. Guarda clase, nombre, padre (id), hijos
// ordenados, valores de propiedades (vector indexado por la propiedad
// de la clase), Attributes y Tags. La transformacion es en coordenadas
// de mundo y SIN herencia (el editor mueve a los descendientes).
class Instance {
public:
    Instance() = default;
    Instance(InstanceId id, const ClassDef* cls, std::string name);

    InstanceId id() const { return id_; }
    const ClassDef* cls() const { return cls_; }
    const std::string& className() const { return cls_ ? className_ : empty_; }

    const std::string& name() const { return name_; }
    void setName(const std::string& n) { name_ = n; }

    InstanceId parentId() const { return parent_; }
    // Baja el enlace sin tocar la lista de hijos del padre ni emitir
    // eventos: solo lo usan World y la deserializacion.
    void setParentId(InstanceId p) { parent_ = p; }

    const std::vector<InstanceId>& children() const { return children_; }
    void addChild(InstanceId child) { children_.push_back(child); }
    void insertChild(size_t index, InstanceId child);
    bool removeChild(InstanceId child);
    void clearChildren() { children_.clear(); }

    bool isProtected() const { return protected_; }
    void setProtected(bool p) { protected_ = p; }
    bool alive() const { return alive_; }
    void setAlive(bool a) { alive_ = a; }

    // --- Valores de propiedades ---
    const std::vector<PropertyValue>& values() const { return values_; }
    PropertyValue& valueBySlot(int slot) { return values_[slot]; }
    const PropertyValue& valueBySlot(int slot) const { return values_[slot]; }
    // Hueco del valor "prop" dentro del vector; -1 si la clase no la tiene.
    int slotOf(const std::string& prop) const;

    bool propBool(const std::string& prop, bool def = false) const;
    long long propInt(const std::string& prop, long long def = 0) const;
    double propFloat(const std::string& prop, double def = 0.0) const;
    std::string propString(const std::string& prop,
                           const std::string& def = "") const;
    Vec3 propVec3(const std::string& prop, const Vec3& def = {}) const;
    InstanceId propRef(const std::string& prop) const;

    // Devuelve false si la clase no tiene esa propiedad.
    bool setPropBool(const std::string& prop, bool value);
    bool setPropInt(const std::string& prop, long long value);
    bool setPropFloat(const std::string& prop, double value);
    bool setPropString(const std::string& prop, const std::string& value);
    bool setPropVec3(const std::string& prop, const Vec3& value);
    bool setPropRef(const std::string& prop, InstanceId value);

    // --- Transformacion (mundo, sin herencia) ---
    Vec3 position() const;
    Vec3 rotation() const;
    Vec3 scale() const;
    void setPosition(const Vec3& p);
    void setRotation(const Vec3& r);
    void setScale(const Vec3& s);

    Shape shape() const;
    void setShape(Shape shape);

    // true si la clase se dibuja en el viewport 3D.
    bool isRenderable() const;

    // T * Rx * Ry * Rz * S (rotaciones en este orden).
    Mat4 modelMatrix() const;

    // --- Attributes (API tipo Roblox) ---
    bool hasAttribute(const std::string& name) const;
    Attribute getAttribute(const std::string& name) const;
    const std::vector<Attribute>& getAttributes() const { return attributes_; }
    void setAttribute(const Attribute& attr);
    void removeAttribute(const std::string& name);

    // --- Tags (API tipo Roblox) ---
    void addTag(const std::string& tag);
    void removeTag(const std::string& tag);
    bool hasTag(const std::string& tag) const;
    const std::vector<std::string>& getTags() const { return tags_; }
    void setTags(std::vector<std::string> tags) { tags_ = std::move(tags); }

    // Acceso directo para World/persistencia.
    std::vector<PropertyValue>& mutableValues() { return values_; }
    std::vector<Attribute>& mutableAttributes() { return attributes_; }

private:
    InstanceId id_ = kInvalidId;
    const ClassDef* cls_ = nullptr;
    std::string className_;
    std::string name_;
    InstanceId parent_ = kInvalidId;
    std::vector<InstanceId> children_;
    bool protected_ = false;
    bool alive_ = true;
    std::vector<PropertyValue> values_;
    std::vector<Attribute> attributes_;
    std::vector<std::string> tags_;
    static const std::string empty_;
};

// true si la clase se dibuja en el viewport 3D (Part y sus derivadas
// con geometria).
bool isRenderableClass(const std::string& className);

} // namespace sk
