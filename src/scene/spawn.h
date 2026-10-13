#pragma once

#include "world.h"

namespace sk {

// Posicion inicial de la camara de Play/Run: la del primer SpawnLocation de
// la escena (si existe), o (0,20,0) como respaldo. Logica pura: se puede
// probar sin Win32 ni Vulkan.
inline Vec3 playSpawnPosition(const World& world) {
    for (const RenderPart& part : world.renderParts()) {
        const Instance* object = world.find(part.id);
        if (object && object->className() == "SpawnLocation") {
            return object->position();
        }
    }
    return Vec3{0.0f, 20.0f, 0.0f};
}

} // namespace sk
