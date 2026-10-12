#include "class_catalog.h"

#include <cstring>
#include <vector>

#include "class_registry.h"

namespace sk {
namespace catalog {

// Adaptador de solo lectura sobre ClassRegistry (la fuente unica de datos
// de las clases del motor). Construye una vez los arreglos que consume la
// interfaz (Referencia de clases, boton "+" e iconos) y a partir de ahi
// devuelve punteros estables a esa copia. Las cadenas apuntan a los
// literales del registro, que viven toda la ejecucion.

namespace {

std::vector<Category> g_categories;
std::vector<ClassDef> g_classes;
// Almacen de las tablas de propiedades de cada clase (una por clase para
// que los punteros que guarda ClassDef::props no se invaliden).
std::vector<std::vector<PropDef>> g_props;
bool g_built = false;

void build() {
    if (g_built) return;
    g_built = true;

    const ClassRegistry& reg = ClassRegistry::get();

    g_categories.reserve(static_cast<size_t>(reg.categoryCount()));
    for (int i = 0; i < reg.categoryCount(); ++i) {
        const ::sk::Category& src = reg.categories()[i];
        Category c{};
        c.icon = src.icon;
        c.name = src.name;
        g_categories.push_back(c);
    }

    const int n = reg.classCount();
    g_classes.reserve(static_cast<size_t>(n));
    g_props.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const ::sk::ClassDef& src = reg.at(i);

        ClassDef c{};
        c.cat = src.category;
        c.name = src.name;
        c.base = src.base;
        c.abstractCls = src.abstractCls;
        c.shortDesc = src.shortDesc;
        c.longDesc = src.longDesc;

        std::vector<PropDef>& props = g_props[static_cast<size_t>(i)];
        props.reserve(src.effective.size());
        for (const ::sk::PropertyDef& p : src.effective) {
            PropDef out{};
            out.name = p.name;
            out.type = propTypeName(p.type);
            out.def = p.def;
            out.desc = p.desc;
            props.push_back(out);
        }
        c.props = props.empty() ? nullptr : props.data();
        c.propCount = static_cast<int>(props.size());

        g_classes.push_back(c);
    }
}

} // namespace

int categoryCount() {
    build();
    return static_cast<int>(g_categories.size());
}

const Category* categories() {
    build();
    return g_categories.data();
}

int classCount() {
    build();
    return static_cast<int>(g_classes.size());
}

const ClassDef* classes() {
    build();
    return g_classes.data();
}

const ClassDef* findClass(const std::string& name) {
    build();
    for (const ClassDef& c : g_classes) {
        if (name == c.name) return &c;
    }
    return nullptr;
}

int categoryOf(const std::string& className) {
    build();
    const ClassDef* c = findClass(className);
    return c ? c->cat : -1;
}

bool insertable(const ClassDef& c) {
    return ClassRegistry::get().isInsertable(c.name);
}

bool isLightType(const std::string& className) {
    return ClassRegistry::get().isLightType(className);
}

} // namespace catalog
} // namespace sk
