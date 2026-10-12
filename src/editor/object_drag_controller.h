#pragma once

#include <vector>

#include "../math/math.h"
#include "../scene/commands.h"
#include "../scene/instance.h"
#include "../scene/picking.h"
#include "../scene/snapping.h"
#include "../scene/world.h"

namespace sk::editor {

// Arrastre de objetos en el viewport: al mantener pulsado el cuerpo de una
// parte (sin tocar un asa del gizmo) y mover, el grupo seleccionado se
// apoya en la cara de lo que tiene debajo (surface snapping) o en el suelo
// Y=0. Guarda las poses iniciales para el undo y para revertir con Esc.
class ObjectDragController {
public:
    struct Start {
        InstanceId id = kInvalidId;
        Vec3 pos{};
        Vec3 rot{};
    };

    bool active() const { return active_; }
    InstanceId pressHit() const { return pressHit_; }
    const std::vector<Start>& starts() const { return starts_; }

    // Empieza el arrastre de `selection` (las partes Locked no viajan).
    // pressHit es el objeto bajo el punto de presion y pressRay su rayo
    // (para fijar el punto de agarre).
    void begin(World& world, const std::vector<InstanceId>& selection,
               InstanceId pressHit, const Ray& pressRay);

    // Aplica el arrastre del frame. Devuelve true si la escena cambio.
    bool update(World& world, const Ray& ray0, const Ray& ray1,
                bool alignSurface, float grid);

    // Cierra el arrastre registrando el undo (delta de transform).
    int commit(World& world, UndoStack& undo);

    // Cancela revirtiendo a las poses iniciales (sin undo).
    void cancel(World& world);

    void reset();

private:
    bool active_ = false;
    InstanceId pressHit_ = kInvalidId;
    Vec3 grabOffset_{};
    std::vector<Start> starts_;
};

} // namespace sk::editor
