// Modelo del arbol del Explorer: logica pura (SIN windows.h) para que
// entre en la biblioteca motorsk_model y se pueda probar sin UI. Lee del
// World por id (no copia texto) y mantiene el conjunto de nodos
// expandidos por id (no por texto). El aplanado es iterativo con pila
// explicita (aguanta 200+ niveles) y SIEMPRE en preorden.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "icons.h"
#include "kit.h"
#include "scene/world.h"

namespace sk {
namespace ui {

// Nodos sinteticos del Explorer que no son Instance del World. Se eligen
// fuera del rango de ids de Instance (1..N) para no colisionar.
inline constexpr NodeId kTreeRootId = 0xFFFFFFFFu;  // raiz virtual (no se pinta)
inline constexpr NodeId kDimGroupId = 0xFFFFFFFEu;  // grupo "Dimensions"

// Fila visible del arbol. Identifica el nodo por id (el texto se lee del
// World con textOf) y guarda lo minimo para pintar: nivel, icono y
// banderas. No copia el nombre.
struct TreeNode {
    NodeId id = 0;
    NodeId parentId = 0;
    int level = 0;
    IconId icon = IconId::Part;
    bool hasChildren = false;
    bool expanded = false;
    bool protectedNode = false;
};

// Zona de una fila al soltar: antes, dentro o despues.
enum class DropZone { Before, Inside, After };

// Resultado de un destino de arrastre. reason es un literal estable
// ("ciclo", "protegido", "el mismo", "inexistente" o "" si es valido).
struct DropDecision {
    bool valid = false;
    const char* reason = "";
    NodeId newParent = kInvalidId;  // InstanceId destino; kInvalidId si no vale
    int index = -1;                 // posicion entre hermanos (-1 = al final)
};

// Icono de una clase del motor; el de su categoria como respaldo.
IconId iconForClass(const std::string& className);
// Icono de una categoria (indice de ClassRegistry::categories()).
IconId iconForCategory(int categoryIndex);

class TreeModel {
public:
    TreeModel() = default;
    explicit TreeModel(const World* world) : world_(world) {}

    void setWorld(const World* world);
    const World* world() const { return world_; }

    void invalidate() { dirty_ = true; }
    void expandAll();
    void collapseAll();

    // Lista plana visible (preorden). Reconstruye si hay cambios.
    const std::vector<TreeNode>& rows() const;

    // Lecturas por id (del World / ClassRegistry).
    std::string textOf(NodeId id) const;
    IconId iconOf(NodeId id) const;
    bool isProtected(NodeId id) const;
    bool hasChildren(NodeId id) const;
    NodeId parentOf(NodeId id) const;
    std::vector<NodeId> childrenOf(NodeId id) const;
    // Indice de fila visible de un id; -1 si no esta visible. O(n): solo
    // se usa en operaciones puntuales (seleccionar/desplazar), no por frame.
    int indexOf(NodeId id) const;

    bool isExpanded(NodeId id) const { return expanded_.count(id) != 0; }
    void setExpanded(NodeId id, bool expanded);
    void toggleExpanded(NodeId id);
    void expandTo(NodeId id);

    // --- Eventos del World: solo tocan lo afectado ---
    // El texto se lee siempre del World, asi que un renombrado no
    // reconstruye nada; el resto marca la lista como sucia (se regenera
    // de forma perezosa, nunca en el bucle de pintado).
    void onAdded(InstanceId id, InstanceId parent);
    void onRemoved(InstanceId id);
    void onRenamed(InstanceId id);
    void onReparented(InstanceId id, InstanceId oldParent,
                      InstanceId newParent);

    // Calcula a donde iria un arrastre de source sobre target en zone.
    DropDecision dropTarget(NodeId source, NodeId target, DropZone zone) const;

private:
    void flatten() const;

    const World* world_ = nullptr;
    std::unordered_set<NodeId> expanded_;
    mutable std::vector<TreeNode> rows_;
    mutable bool dirty_ = true;
};

}  // namespace ui
}  // namespace sk
