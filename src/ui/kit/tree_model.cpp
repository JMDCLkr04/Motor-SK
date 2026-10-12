#include "tree_model.h"

#include <algorithm>

namespace sk {
namespace ui {

namespace {

int indexInList(const std::vector<NodeId>& list, NodeId id) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == id) return static_cast<int>(i);
    }
    return -1;
}

bool isDimensionClass(const Instance* inst) {
    return inst && inst->className() == "Dimension";
}

}  // namespace

// Icono de una categoria por su indice. Las 19 primeras entradas de IconId
// son las categorias, en el mismo orden que ClassRegistry::categories().
IconId iconForCategory(int categoryIndex) {
    if (categoryIndex >= 0 && categoryIndex < 19) {
        return static_cast<IconId>(categoryIndex);
    }
    return IconId::Part;
}

// Mapa clase -> IconId. Lo que no este aqui usa el icono de su categoria
// (respaldo). Nunca Part fijo salvo para clases desconocidas.
IconId iconForClass(const std::string& className) {
    struct Entry {
        const char* name;
        IconId icon;
    };
    static const Entry kMap[] = {
        {"Part", IconId::Part},
        {"MeshPart", IconId::Part},
        {"Model", IconId::Model},
        {"Folder", IconId::Folder},
        {"Workspace", IconId::Workspace},
        {"Camera", IconId::Camera},
        {"Lighting", IconId::Lighting},
        {"Script", IconId::Script},
        {"LocalScript", IconId::LocalScript},
        {"ModuleScript", IconId::ModuleScript},
        {"Scripts", IconId::CatScripts},
        {"Light", IconId::Light},
        {"Sound", IconId::Sound},
        {"Dimension", IconId::Dimension},
        {"Attachment", IconId::Attachment},
        {"Decal", IconId::Decal},
        {"Texture", IconId::Decal},
        {"ParticleEmitter", IconId::ParticleEmitter},
        {"Assets", IconId::CatResources},
    };
    for (const Entry& e : kMap) {
        if (className == e.name) return e.icon;
    }
    const ClassDef* def = ClassRegistry::get().find(className);
    if (def) return iconForCategory(def->category);
    return IconId::Part;
}

void TreeModel::setWorld(const World* world) {
    world_ = world;
    expanded_.clear();
    dirty_ = true;
}

std::string TreeModel::textOf(NodeId id) const {
    if (id == kDimGroupId) return "Dimensions";
    if (id == kTreeRootId) return std::string();
    const Instance* inst = world_ ? world_->find(id) : nullptr;
    return inst ? inst->name() : std::string();
}

IconId TreeModel::iconOf(NodeId id) const {
    if (id == kDimGroupId) return IconId::Dimension;
    const Instance* inst = world_ ? world_->find(id) : nullptr;
    if (!inst) return IconId::Part;
    // El nodo Assets estructural usa el icono de recursos; el resto, el de
    // su clase.
    if (id == world_->assetsRoot()) return IconId::CatResources;
    return iconForClass(inst->className());
}

bool TreeModel::isProtected(NodeId id) const {
    if (id == kTreeRootId || id == kDimGroupId) return true;
    const Instance* inst = world_ ? world_->find(id) : nullptr;
    return inst && inst->isProtected();
}

NodeId TreeModel::parentOf(NodeId id) const {
    if (id == kTreeRootId) return kInvalidId;
    if (id == kDimGroupId) return kTreeRootId;
    const Instance* inst = world_ ? world_->find(id) : nullptr;
    if (!inst) return kInvalidId;
    if (inst->parentId() == kInvalidId) {
        return isDimensionClass(inst) ? kDimGroupId : kTreeRootId;
    }
    return inst->parentId();
}

std::vector<NodeId> TreeModel::childrenOf(NodeId id) const {
    std::vector<NodeId> out;
    if (!world_) return out;
    if (id == kTreeRootId) {
        out.push_back(kDimGroupId);
        for (InstanceId root : world_->roots()) {
            const Instance* inst = world_->find(root);
            if (isDimensionClass(inst)) continue;  // van dentro del grupo
            out.push_back(root);
        }
        return out;
    }
    if (id == kDimGroupId) return world_->dimensions();
    const Instance* inst = world_->find(id);
    if (inst) out = inst->children();
    return out;
}

bool TreeModel::hasChildren(NodeId id) const {
    if (id == kTreeRootId) return true;
    return !childrenOf(id).empty();
}

void TreeModel::setExpanded(NodeId id, bool expanded) {
    const bool had = expanded_.count(id) != 0;
    if (expanded == had) return;
    if (expanded) {
        expanded_.insert(id);
    } else {
        expanded_.erase(id);
    }
    dirty_ = true;
}

void TreeModel::toggleExpanded(NodeId id) { setExpanded(id, !isExpanded(id)); }

void TreeModel::expandAll() {
    if (!world_) return;
    // Recorrido explicito (pila) por todo el arbol; marca expandido cada
    // nodo con hijos.
    std::vector<NodeId> stack;
    stack.push_back(kTreeRootId);
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        const std::vector<NodeId> kids = childrenOf(id);
        if (!kids.empty() && id != kTreeRootId) expanded_.insert(id);
        for (NodeId k : kids) stack.push_back(k);
    }
    dirty_ = true;
}

void TreeModel::collapseAll() {
    expanded_.clear();
    dirty_ = true;
}

void TreeModel::expandTo(NodeId id) {
    NodeId p = parentOf(id);
    int guard = 0;
    while (p != kInvalidId && p != kTreeRootId && guard++ < 100000) {
        expanded_.insert(p);
        p = parentOf(p);
    }
    dirty_ = true;
}

void TreeModel::flatten() const {
    rows_.clear();
    if (!world_) {
        dirty_ = false;
        return;
    }
    // DFS en preorden con pila explicita (sin recursion): se empujan los
    // hijos en orden inverso para que salgan en orden natural.
    std::vector<NodeId> stack;
    std::vector<int> levels;
    const std::vector<NodeId> top = childrenOf(kTreeRootId);
    for (auto it = top.rbegin(); it != top.rend(); ++it) {
        stack.push_back(*it);
        levels.push_back(0);
    }
    while (!stack.empty()) {
        const NodeId id = stack.back();
        const int level = levels.back();
        stack.pop_back();
        levels.pop_back();

        const std::vector<NodeId> kids = childrenOf(id);
        const bool has = !kids.empty();
        const bool open = expanded_.count(id) != 0;
        TreeNode row;
        row.id = id;
        row.parentId = parentOf(id);
        row.level = level;
        row.icon = iconOf(id);
        row.hasChildren = has;
        row.expanded = open;
        row.protectedNode = isProtected(id);
        rows_.push_back(row);

        if (has && open) {
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
                stack.push_back(*it);
                levels.push_back(level + 1);
            }
        }
    }
    dirty_ = false;
}

const std::vector<TreeNode>& TreeModel::rows() const {
    if (dirty_) flatten();
    return rows_;
}

int TreeModel::indexOf(NodeId id) const {
    const std::vector<TreeNode>& r = rows();
    for (size_t i = 0; i < r.size(); ++i) {
        if (r[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

void TreeModel::onAdded(InstanceId, InstanceId) { dirty_ = true; }

void TreeModel::onRemoved(InstanceId id) {
    expanded_.erase(id);
    dirty_ = true;
}

void TreeModel::onRenamed(InstanceId) {
    // El texto se lee del World en textOf(): no hay nada que reconstruir.
    // El widget repinta solo esa fila.
}

void TreeModel::onReparented(InstanceId, InstanceId, InstanceId) {
    dirty_ = true;
}

DropDecision TreeModel::dropTarget(NodeId source, NodeId target,
                                   DropZone zone) const {
    DropDecision d;
    if (!world_) {
        d.reason = "inexistente";
        return d;
    }
    auto known = [&](NodeId id) {
        return id == kDimGroupId || (id != kTreeRootId && world_->exists(id));
    };
    if (source == kInvalidId || !known(source)) {
        d.reason = "inexistente";
        return d;
    }
    if (target == kInvalidId || !known(target)) {
        d.reason = "inexistente";
        return d;
    }
    if (source == target) {
        d.reason = "el mismo";
        return d;
    }
    if (isProtected(source)) {
        d.reason = "protegido";
        return d;
    }
    // Ciclo: el destino no puede estar dentro del subarbol del origen.
    NodeId cur = target;
    int guard = 0;
    while (cur != kInvalidId && cur != kTreeRootId && guard++ < 100000) {
        if (cur == source) {
            d.reason = "ciclo";
            return d;
        }
        cur = parentOf(cur);
    }

    NodeId newParent = kInvalidId;
    int index = -1;
    if (zone == DropZone::Inside) {
        if (!world_->exists(target)) {
            // El grupo "Dimensions" no admite hijos directos por arrastre.
            d.reason = "protegido";
            return d;
        }
        newParent = target;
    } else {
        newParent = parentOf(target);
        if (newParent == kTreeRootId || newParent == kDimGroupId ||
            newParent == kInvalidId) {
            // Reordenar en la raiz sintetica no es una operacion valida.
            d.reason = "protegido";
            return d;
        }
        std::vector<NodeId> sib = childrenOf(newParent);
        // El indice se calcula tras extraer el origen de la lista de
        // hermanos (asi apply, que detacha y luego inserta, acierta).
        sib.erase(std::remove(sib.begin(), sib.end(), source), sib.end());
        const int di = indexInList(sib, target);
        if (di < 0) {
            d.reason = "inexistente";
            return d;
        }
        index = di + (zone == DropZone::After ? 1 : 0);
    }

    d.valid = true;
    d.reason = "";
    d.newParent = newParent;
    d.index = index;
    return d;
}

}  // namespace ui
}  // namespace sk
