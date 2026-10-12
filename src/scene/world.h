#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "collection_service.h"
#include "instance.h"

namespace sk {

// Elemento de la lista plana que consume el render: matriz modelo y
// forma de un Part visible.
struct RenderPart {
    Mat4 model;
    int shape = 0;
    InstanceId id = kInvalidId;
};

// Nombres de los nodos estructurales protegidos (por dimension y
// globales). Se usan para reconocerlos en la migracion v1 y al crear la
// estructura por defecto.
inline constexpr const char* kDimName = "dimension01";
inline constexpr const char* kWorkspaceName = "Workspace";
inline constexpr const char* kCameraName = "Camera";
inline constexpr const char* kLightingName = "Lighting";
inline constexpr const char* kScriptsName = "Scripts";
inline constexpr const char* kAssetsName = "Assets";

// Arbol de objetos (reemplaza a Scene). Contiene las Dimensions (cada una
// es una Instance), el arbol global Scripts (Server/Shared/Player/
// Character) y Assets (las 7 carpetas). Los ids no se reutilizan; los
// nombres son unicos entre hermanos; los nodos estructurales son
// protegidos (no se renombran, no se borran, no se mueven).
class World {
public:
    using AddEvent = std::function<void(InstanceId id, InstanceId parent)>;
    using IdEvent = std::function<void(InstanceId id)>;
    using ReparentEvent =
        std::function<void(InstanceId id, InstanceId oldParent,
                           InstanceId newParent)>;

    World() = default;

    const ClassRegistry& registry() const { return ClassRegistry::get(); }

    // --- Estructura ---
    void clear();
    // Crea lo que falte: dimension01 (con Workspace/Camera/Lighting+
    // efectos/Scripts), el arbol global Scripts y Assets. Idempotente.
    void ensureStructure();
    // Crea una Dimension completa (Workspace, Camera, Lighting con Sky,
    // Atmosphere, Clouds y los 6 efectos, y Scripts) y la devuelve.
    InstanceId createDimension(const std::string& name);

    // --- Consultas ---
    Instance* find(InstanceId id);
    const Instance* find(InstanceId id) const;
    bool exists(InstanceId id) const { return pool_.count(id) != 0; }
    InstanceId findChild(InstanceId parent, const std::string& name) const;
    // Cambia el nombre manteniendo el indice (parent, nombre) -> id sin
    // validar ni emitir eventos. Para undo/redo de renombrados.
    void setNameIndexed(InstanceId id, const std::string& name);

    const std::vector<InstanceId>& roots() const { return roots_; }
    std::vector<InstanceId> dimensions() const;
    InstanceId globalScripts() const { return globalScripts_; }
    InstanceId assetsRoot() const { return assetsRoot_; }
    size_t aliveCount() const;

    bool isProtected(InstanceId id) const;
    // id y todos sus descendientes (id primero).
    std::vector<InstanceId> subtree(InstanceId id) const;

    // --- Mutaciones (emiten eventos) ---
    // Crea una clase bajo parent (kInvalidId = raiz del mundo). El nombre
    // se deduce de la clase con sufijo numerico si hay colision.
    InstanceId create(const std::string& className, InstanceId parent);
    // Alta con nombre y proteccion explicitos (nodos estructurales).
    InstanceId createNamed(const std::string& className,
                           const std::string& name, InstanceId parent,
                           bool protectedNode = false);

    // Borra el subarbol. Devuelve false si no existe o es protegido.
    bool remove(InstanceId id);
    // Reubica (valida ciclos, protegidos y que el destino exista).
    bool reparent(InstanceId id, InstanceId newParent);
    // Renombra (auto-numera si choca con un hermano). false si protegido.
    bool rename(InstanceId id, const std::string& name);
    // Clona el subarbol bajo parent remapeando ids. Devuelve el nuevo id.
    InstanceId clone(InstanceId id, InstanceId parent);

    // --- Propiedades ---
    bool setBool(InstanceId id, const std::string& prop, bool value);
    bool setInt(InstanceId id, const std::string& prop, long long value);
    bool setFloat(InstanceId id, const std::string& prop, double value);
    bool setString(InstanceId id, const std::string& prop,
                   const std::string& value);
    bool setVec3(InstanceId id, const std::string& prop, const Vec3& value);
    bool setRef(InstanceId id, const std::string& prop, InstanceId value);
    bool setTransform(InstanceId id, const Vec3& position, const Vec3& rotation,
                      const Vec3& scale);

    // Punto de entrada unico de Properties: escribe una propiedad por
    // nombre respetando el tipo declarado en el registro. Emite onChanged.
    bool setProperty(InstanceId id, const std::string& prop,
                     const PropertyValue& value);

    // Attributes y Tags (actualizan el CollectionService y emiten).
    bool setAttribute(InstanceId id, const Attribute& attr);
    bool removeAttribute(InstanceId id, const std::string& name);
    bool setTags(InstanceId id, const std::vector<std::string>& tags);
    bool addTag(InstanceId id, const std::string& tag);
    bool removeTag(InstanceId id, const std::string& tag);

    // Nombre unico entre los hermanos de parent partiendo de "base".
    std::string uniqueName(InstanceId parent, const std::string& base) const;

    // --- Lista plana para el render (cacheada, regenerada por eventos) ---
    const std::vector<RenderPart>& renderParts() const;
    void invalidateRender() { renderDirty_ = true; }

    // --- Eventos ---
    void onAdded(AddEvent cb) { onAdded_ = std::move(cb); }
    void onRemoved(IdEvent cb) { onRemoved_ = std::move(cb); }
    void onChanged(IdEvent cb) { onChanged_ = std::move(cb); }
    void onReparented(ReparentEvent cb) { onReparented_ = std::move(cb); }

    CollectionService& collection() { return collection_; }
    const CollectionService& collection() const { return collection_; }

    // --- Persistencia (atomica) ---
    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;

    // Reserva un id nuevo (no reutilizable).
    InstanceId allocateId() const { return nextId_++; }

    // Inserccion de bajo nivel (deserializacion y undo). No comprueba
    // nada: el llamador es responsable.
    Instance& insertInstance(Instance inst);
    void adoptChild(InstanceId parent, InstanceId child, size_t index);
    void detachChild(InstanceId parent, InstanceId child);
    void addRoot(InstanceId id) { roots_.push_back(id); }
    void removeRoot(InstanceId id);
    void markChanged(InstanceId id) { emitChanged(id); }
    void rebuildCollection();
    // Escribe un valor por hueco (undo/redo). Emite onChanged.
    bool setValue(InstanceId id, int slot, const PropertyValue& value);
    // Quita del pool sin tocar jerarquia ni eventos (undo de un alta).
    void eraseInstance(InstanceId id);
    // Reinserta un subarbol ya capturado (undo de un borrado). El primer
    // nodo es la raiz; los demas conservan su enlace interno.
    void restoreInstances(const std::vector<Instance>& nodes, InstanceId parent,
                          size_t index);
    void setGlobalRoots(InstanceId scripts, InstanceId assets) {
        globalScripts_ = scripts;
        assetsRoot_ = assets;
    }

private:
    void emitAdded(InstanceId id, InstanceId parent);
    void emitRemoved(InstanceId id);
    void emitChanged(InstanceId id);
    void emitReparented(InstanceId id, InstanceId oldP, InstanceId newP);
    void indexChild(InstanceId parent, const std::string& name, InstanceId id);
    void unindexChild(InstanceId parent, const std::string& name);
    void rebuildNameIndex();
    void refreshRenderCache() const;
    bool loadV2(const std::vector<std::string>& lines);
    bool loadV1(const std::vector<std::string>& lines);

    std::unordered_map<InstanceId, Instance> pool_;
    std::vector<InstanceId> roots_;
    // Indice (padre, nombre) -> id para findChild/uniqueName en tiempo
    // constante amortizado. Se mantiene en todas las mutaciones.
    std::unordered_map<InstanceId,
                       std::unordered_map<std::string, InstanceId>>
        childNames_;
    // Siguiente sufijo a probar por (padre, nombre base); evita recontar
    // desde 1 en cada alta (mutable: uniqueName es const).
    mutable std::unordered_map<InstanceId, std::unordered_map<std::string, int>>
        nameCounters_;
    mutable InstanceId nextId_ = 1;
    InstanceId globalScripts_ = kInvalidId;
    InstanceId assetsRoot_ = kInvalidId;
    CollectionService collection_;

    mutable std::vector<RenderPart> renderCache_;
    mutable bool renderDirty_ = true;
    bool suppressEvents_ = false;

    AddEvent onAdded_;
    IdEvent onRemoved_;
    IdEvent onChanged_;
    ReparentEvent onReparented_;
};

} // namespace sk
