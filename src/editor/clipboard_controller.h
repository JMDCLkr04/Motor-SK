#pragma once

#include <vector>

#include "../scene/commands.h"
#include "../scene/instance.h"
#include "../scene/world.h"

namespace sk::editor {

// Portapapeles interno de subarboles (Ctrl+C / Ctrl+X / Ctrl+V). Guarda
// una copia por valor de cada subarbol seleccionado (profundidad incluida),
// de modo que el borrado/corte posterior no lo invalida.
class ClipboardController {
public:
    // Vacia el portapapeles.
    void clear();

    bool empty() const { return clipboard_.empty(); }
    int count() const { return static_cast<int>(clipboard_.size()); }
    bool fromCut() const { return fromCut_; }

    // Subarbol de la primera entrada (el original bajo el raton en un
    // pegado tras cortar). Vacio si no hay nada.
    const std::vector<Instance>& first() const { return clipboard_.front(); }

    // Copia los subarboles de `tops` (ya filtrados a raices).
    void copy(const World& world, const std::vector<InstanceId>& tops);

    // Copia y borra de golpe via un comando compuesto (undo unico).
    void cut(World& world, UndoStack& undo, const std::vector<InstanceId>& tops);

    // Id del padre comun de las raices del portapapeles (kInvalidId si no
    // comparten padre o si el padre ya no existe).
    InstanceId sharedParent(const World& world) const;

    // Pega el contenido bajo `target` desplazado por `offset`. Rellena
    // outRoots con las raices nuevas. Devuelve false si no habia nada.
    bool paste(World& world, UndoStack& undo, InstanceId target,
               const Vec3& offset, std::vector<InstanceId>& outRoots);

private:
    std::vector<std::vector<Instance>> clipboard_;
    bool fromCut_ = false;
};

} // namespace sk::editor
