#pragma once

#include <vector>

#include "../math/math.h"
#include "instance.h"
#include "picking.h"

namespace sk {

class World;

// --- Arrastre de partes estilo Roblox Studio (surface snapping) ---

// Paso de rejilla por defecto, en unidades de mundo.
constexpr float kDragGrid = 1.0f;

// Pose (posicion + rotacion en grados) de un objeto arrastrado.
struct DragPose {
    InstanceId id = kInvalidId;
    Vec3 position{};
    Vec3 rotation{};
};

// Salida del calculo de un frame de arrastre.
struct DragResult {
    bool onSurface = false;
    InstanceId surfaceId = kInvalidId;
    Vec3 surfaceNormal{};
    bool groundHit = false;
    std::vector<DragPose> poses;
};

void computeSurfaceDrag(const World& world, const Ray& ray0, const Ray& ray1,
                        const std::vector<InstanceId>& dragged, int leadIndex,
                        const Vec3& grabOffset,
                        const std::vector<DragPose>& startPose,
                        bool alignSurface, float grid, DragResult& out);

} // namespace sk

