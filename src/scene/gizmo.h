#pragma once

#include <vector>

#include "../math/math.h"
#include "instance.h"
#include "picking.h"

namespace sk {

class World;

// Linea de gizmo en espacio mundo con su mango interactivo: 0..2 =
// direccion positiva del eje 0..2 (X/Y/Z), 3..5 = direccion negativa,
// -1 = adorno sin interaccion (p.ej. el circulo central).
struct GizmoLine {
    Vec3 a{};
    Vec3 b{};
    Vec3 color{};
    int axis = -1;
};

// Geometria del gizmo de `tool` (1 mover, 2 escalar, 3 rotar) centrado
// en `origin`.
std::vector<GizmoLine> buildGizmo(int tool, const Vec3& origin, float scaleMax,
                                  const Vec3& viewDir, int hoveredAxis);

int hitTestGizmo(const std::vector<GizmoLine>& lines, const Vec2& pointer,
                 const Mat4& viewProj, int width, int height);

Vec3 gizmoHandlePoint(int tool, int handle, const Vec3& origin, float scaleMax);

// --- Arrastre: afecta a TODOS los objetos seleccionados. ---

struct GizmoSnapshot {
    InstanceId id = kInvalidId;
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct GizmoDrag {
    bool active = false;
    int tool = 0;
    int axis = -1;
    int axisSign = 1;
    Vec3 origin{};
    Vec3 axisDir{};
    Vec3 planeN{};
    float L = 1.0f;
    float s0 = 0.0f;
    std::vector<GizmoSnapshot> snapshots;
};

bool gizmoBegin(GizmoDrag& drag, int tool, int handle, const World& world,
                const std::vector<InstanceId>& selection, const Vec3& origin,
                float scaleMax, const Vec3& viewDir, const Ray& ray);

bool gizmoUpdate(GizmoDrag& drag, const Ray& ray, World& world, float moveStep,
                 float rotStep);

void gizmoRevert(GizmoDrag& drag, World& world);

} // namespace sk
