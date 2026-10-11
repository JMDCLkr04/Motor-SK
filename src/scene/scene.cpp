#include "scene.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace sk {

Mat4 SceneObject::modelMatrix() const {
    // sk::scale explicito: el miembro scale oculta a la funcion free.
    return translate(position) * rotateX(rotation.x) * rotateY(rotation.y) *
           rotateZ(rotation.z) * sk::scale(scale);
}

bool isRenderableClass(const std::string& className) {
    return className == "Part" || className == "MeshPart" ||
           className == "SpawnLocation" || className == "Seat";
}

SceneObject& Scene::addPart(Shape shape) {
    SceneObject obj;
    obj.className = "Part";
    obj.parent = kWorkspaceName;
    obj.shape = shape;

    size_t n = 0;
    for (const SceneObject& o : objects_) {
        if (isRenderableClass(o.className)) ++n;
    }
    obj.name = "Part";
    if (findByName(obj.name)) {
        size_t i = 1;
        while (findByName("Part" + std::to_string(i))) ++i;
        obj.name = "Part" + std::to_string(i);
    }
    obj.position = {static_cast<float>(n % 4) * 1.6f - 2.4f, 0.5f,
                    static_cast<float>(n / 4) * 1.6f};
    objects_.push_back(std::move(obj));
    return objects_.back();
}

SceneObject& Scene::addNode(const std::string& className,
                            const std::string& parent) {
    SceneObject obj;
    obj.className = className.empty() ? "Part" : className;
    obj.parent = parent;
    if (obj.parent.empty()) {
        for (const SceneObject& o : objects_) {
            if (o.className == "Dimension") {
                obj.parent = o.name;
                break;
            }
        }
    }
    const std::string base = obj.className;
    obj.name = base;
    if (findByName(obj.name)) {
        size_t i = 1;
        while (findByName(base + std::to_string(i))) ++i;
        obj.name = base + std::to_string(i);
    }
    if (isRenderableClass(obj.className)) obj.position = {0.0f, 0.5f, 0.0f};
    objects_.push_back(std::move(obj));
    return objects_.back();
}

void Scene::ensureStructure() {
    SceneObject* dim = nullptr;
    for (SceneObject& o : objects_) {
        if (o.className == "Dimension") {
            dim = &o;
            break;
        }
    }
    if (!dim) {
        SceneObject d;
        d.name = kDimensionName;
        d.className = "Dimension";
        d.parent = "";
        objects_.push_back(std::move(d));
        dim = &objects_.back();
    }
    const std::string dimName = dim->name;

    auto ensureChild = [&](const char* nm, const char* cls) -> SceneObject* {
        for (SceneObject& o : objects_) {
            if (o.name == nm) return &o;
        }
        SceneObject c;
        c.name = nm;
        c.className = cls;
        c.parent = dimName;
        c.protectedNode = true;
        objects_.push_back(std::move(c));
        return &objects_.back();
    };

    ensureChild(kWorkspaceName, "Workspace");
    ensureChild(kCameraName, "Camera");

    bool lightingNew = false;
    {
        bool found = false;
        for (const SceneObject& o : objects_) {
            if (o.name == kLightingName) {
                found = true;
                break;
            }
        }
        ensureChild(kLightingName, "Lighting");
        lightingNew = !found;
    }

    ensureChild(kScriptsName, "Scripts");

    if (lightingNew) {
        // Hijos por defecto de Lighting (se pueden borrar y reinsertar).
        static const char* kLightingChildren[] = {
            "Sky", "Atmosphere", "Clouds", "BloomEffect", "BlurEffect",
            "ColorCorrectionEffect", "ColorGradingEffect", "DepthOfFieldEffect",
            "SunRaysEffect"};
        for (const char* child : kLightingChildren) {
            SceneObject c;
            c.name = child;
            c.className = child;
            c.parent = kLightingName;
            objects_.push_back(std::move(c));
        }
    }

    // Migracion: cualquier objeto que hoy cuelga directamente de la
    // dimension (parent vacio) pasa a Workspace.
    for (SceneObject& o : objects_) {
        if (o.className == "Dimension") continue;
        if (o.parent.empty()) o.parent = kWorkspaceName;
    }
}

bool Scene::isProtectedName(const std::string& name) const {
    const SceneObject* o = findByName(name);
    return o && o->protectedNode;
}

// Valores del archivo de escena (ASCII, en minusculas); la etiqueta
// que se muestra en la interfaz vive en workspace_tabs.
const char* shapeName(Shape shape) {
    switch (shape) {
        case Shape::Rhombus:    return "rombo";
        case Shape::Sphere:     return "esfera";
        case Shape::Cylinder:   return "cilindro";
        case Shape::Wedge:      return "cuna";
        case Shape::CornerWedge:return "cuna-esquina";
        case Shape::Capsule:    return "capsula";
        case Shape::Cube:
        default:                return "cubo";
    }
}

Shape shapeFromName(const std::string& name) {
    if (name == "rombo") return Shape::Rhombus;
    if (name == "esfera") return Shape::Sphere;
    if (name == "cilindro") return Shape::Cylinder;
    if (name == "cuna") return Shape::Wedge;
    if (name == "cuna-esquina") return Shape::CornerWedge;
    if (name == "capsula") return Shape::Capsule;
    return Shape::Cube;
}

void Scene::clear() {
    objects_.clear();
}

const SceneObject* Scene::findByName(const std::string& name) const {
    for (const SceneObject& object : objects_) {
        if (object.name == name) return &object;
    }
    return nullptr;
}

SceneObject* Scene::findByName(const std::string& name) {
    return const_cast<SceneObject*>(
        static_cast<const Scene*>(this)->findByName(name));
}

bool Scene::renameObject(const std::string& oldName,
                         const std::string& newName) {
    if (newName.empty() || oldName == newName) return false;
    if (findByName(newName)) return false;
    SceneObject* object = findByName(oldName);
    if (!object || object->protectedNode) return false;
    object->name = newName;
    return true;
}

bool Scene::removeObject(const std::string& name) {
    for (auto it = objects_.begin(); it != objects_.end(); ++it) {
        if (it->name == name) {
            if (it->protectedNode) return false;
            objects_.erase(it);
            return true;
        }
    }
    return false;
}

SceneObject& Scene::addCopy(const SceneObject& src, const Vec3& offset) {
    SceneObject obj = src;
    obj.protectedNode = false;
    obj.name = "Part";
    if (findByName(obj.name)) {
        size_t i = 1;
        while (findByName("Part" + std::to_string(i))) ++i;
        obj.name = "Part" + std::to_string(i);
    }
    obj.position = src.position + offset;
    objects_.push_back(std::move(obj));
    return objects_.back();
}

namespace {

// "  a b " -> "a b" (recorta espacios por ambos lados).
std::string trim(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

void writeVec3(std::ofstream& file, const char* key, const Vec3& v) {
    char buf[96]{};
    snprintf(buf, sizeof(buf), "%s: %.6g %.6g %.6g\n", key, v.x, v.y, v.z);
    file << buf;
}

// "1.5 0 -2" -> Vec3. Devuelve false si no hay al menos 3 numeros
// (en ese caso out no se toca).
bool parseVec3(const std::string& text, Vec3& out) {
    const char* p = text.c_str();
    char* end = nullptr;
    float v[3]{};
    for (int i = 0; i < 3; ++i) {
        v[i] = strtof(p, &end);
        if (end == p) return false;
        p = end;
    }
    out = {v[0], v[1], v[2]};
    return true;
}

} // namespace

bool Scene::loadFromFile(const std::string& path) {
    objects_.clear();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        ensureStructure();
        return true; // escena nueva
    }

    std::ifstream file(path);
    if (!file.is_open()) return false;

    SceneObject current;
    bool haveObject = false;
    std::string line;
    while (std::getline(file, line)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = trim(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));

        if (key == "objeto") {
            if (haveObject) objects_.push_back(current);
            current = SceneObject{};
            current.name = value;
            haveObject = true;
        } else if (!haveObject) {
            continue; // "version" y claves sueltas antes del primer objeto
        } else if (key == "clase") {
            current.className = value;
        } else if (key == "protegido") {
            current.protectedNode = (value == "true" || value == "1");
        } else if (key == "parent") {
            current.parent = value;
        } else if (key == "posicion") {
            parseVec3(value, current.position);
        } else if (key == "rotacion") {
            parseVec3(value, current.rotation);
        } else if (key == "escala") {
            parseVec3(value, current.scale);
        } else if (key == "forma") {
            current.shape = shapeFromName(value);
        } else if (key == "script") {
            current.script = value;
        } else if (key == "castshadow") {
            current.castShadow = (value == "true" || value == "1");
        } else if (key == "reflectance") {
            current.reflectance = std::strtof(value.c_str(), nullptr);
        } else if (key == "transparency") {
            current.transparency = std::strtof(value.c_str(), nullptr);
        } else if (key == "locked") {
            current.locked = (value == "true" || value == "1");
        } else if (key == "pivotposition") {
            parseVec3(value, current.pivotPosition);
        } else if (key == "pivotrotation") {
            parseVec3(value, current.pivotRotation);
        } else if (key == "cancollide") {
            current.canCollide = (value == "true" || value == "1");
        } else if (key == "anchored") {
            current.anchored = (value == "true" || value == "1");
        } else if (key == "atributo") {
            // "Nombre|tipo|valor"
            const size_t p1 = value.find('|');
            const size_t p2 = p1 == std::string::npos
                                  ? std::string::npos
                                  : value.find('|', p1 + 1);
            if (p1 != std::string::npos && p2 != std::string::npos) {
                Attribute a;
                a.name = value.substr(0, p1);
                a.type = value.substr(p1 + 1, p2 - p1 - 1);
                a.value = value.substr(p2 + 1);
                current.attributes.push_back(std::move(a));
            }
        } else if (key == "tag") {
            if (!value.empty()) current.tags.push_back(value);
        }
    }
    if (haveObject) objects_.push_back(current);

    ensureStructure();
    return true;
}

bool Scene::saveToFile(const std::string& path) const {
    const std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fsPath.parent_path(), ec);
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;

    file << "version: 1\n";
    for (const SceneObject& object : objects_) {
        file << "objeto: " << object.name << "\n";
        file << "clase: " << object.className << "\n";
        if (object.protectedNode) file << "protegido: true\n";
        if (!object.parent.empty()) file << "parent: " << object.parent << "\n";
        writeVec3(file, "posicion", object.position);
        writeVec3(file, "rotacion", object.rotation);
        writeVec3(file, "escala", object.scale);
        file << "forma: " << shapeName(object.shape) << "\n";
        if (!object.script.empty()) file << "script: " << object.script << "\n";
        file << "castshadow: " << (object.castShadow ? "true" : "false") << "\n";
        file << "reflectance: " << object.reflectance << "\n";
        file << "transparency: " << object.transparency << "\n";
        file << "locked: " << (object.locked ? "true" : "false") << "\n";
        writeVec3(file, "pivotposition", object.pivotPosition);
        writeVec3(file, "pivotrotation", object.pivotRotation);
        file << "cancollide: " << (object.canCollide ? "true" : "false") << "\n";
        file << "anchored: " << (object.anchored ? "true" : "false") << "\n";
        for (const Attribute& a : object.attributes) {
            file << "atributo: " << a.name << "|" << a.type << "|" << a.value
                 << "\n";
        }
        for (const std::string& tag : object.tags) {
            file << "tag: " << tag << "\n";
        }
    }
    return file.good();
}

} // namespace sk
