#pragma once

#include <vector>

#include "../math/math.h"
#include "../scene/commands.h"
#include "../scene/gizmo.h"
#include "../scene/instance.h"
#include "../scene/picking.h"
#include "../scene/world.h"

namespace sk::editor {

// Arrastre del gizmo de herramientas (mover / escalar / rotar). Encapsula
// el GizmoDrag y su ciclo de vida; el resaltado bajo el puntero tambien
// vive aqui (hoverAxis). No dibuja ni registra: el llamador lee drag() y
// hoverAxis().
class GizmoController {
public:
    bool active() const { return drag_.active; }
    GizmoDrag& drag() { return drag_; }
    const GizmoDrag& drag() const { return drag_; }

    // Recalcula el eje bajo el puntero. canHover=false lo limpia. Devuelve
    // true si cambio (hay que repintar).
    bool updateHover(bool canHover, const World& world, int tool,
                     const Vec3& origin, float scaleMax, const Vec3& viewDir,
                     const Mat4& viewProj, int vpW, int vpH, const Vec2& mouse);
    int hoverAxis() const { return hoverAxis_; }
    void clearHover() { hoverAxis_ = -1; }

    // Inicia el arrastre en el eje `axis` (ya calculado por el llamador).
    // Falla si alguna parte de la seleccion esta Locked. Devuelve true si
    // empezo.
    bool begin(const World& world, int tool, int axis,
               const std::vector<InstanceId>& selection, const Vec3& origin,
               float scaleMax, const Vec3& viewDir, const Ray& ray);

    // Aplica el delta de transform del frame a la seleccion.
    bool update(World& world, const Ray& ray, float moveStep, float rotStep);

    // Cierra el arrastre registrando el undo (delta de transform).
    int commit(World& world, UndoStack& undo);

    // Cancela el arrastre revirtiendo a las poses iniciales (sin undo).
    void cancel(World& world);

    // Mango a resaltar al dibujar: el hover, o el agarrado si hay arrastre.
    int paintAxis() const;

private:
    GizmoDrag drag_;
    int hoverAxis_ = -1;
};

} // namespace sk::editor
