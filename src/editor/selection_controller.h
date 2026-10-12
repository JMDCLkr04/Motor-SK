#pragma once

#include <vector>

#include "../math/math.h"
#include "../scene/instance.h"
#include "../scene/world.h"
#include "layout.h"

namespace sk::editor {

// Seleccion en el viewport de PLACE: click, Ctrl+click (multi) y marquee
// (arrastre rectangular). Guarda el estado de la pulsacion sin resolver
// (pending_) y sabe filtrar el rectangulo al viewport usando Layout.
class SelectionController {
public:
    bool pending() const { return pending_; } // press sin superar umbral
    bool active() const { return active_; }    // marquee en curso
    Vec2 start() const { return start_; }
    InstanceId pressHit() const { return pressHit_; }
    bool pressCtrl() const { return pressCtrl_; }

    // Registra el inicio de un click (objeto pickHit bajo el puntero).
    void beginPress(const Vec2& p, bool ctrl, InstanceId pickHit);

    // El press supero el umbral sin objeto: pasa a marquee.
    void beginMarquee();

    // Descarta pending/active sin resolver (Esc, soltar tras arrastre...).
    void reset();

    // Click (o Ctrl+click) resuelto al soltar. `current` es la seleccion
    // actual; devuelve la nueva y actualiza newPrimary.
    static std::vector<InstanceId> resolveClick(
        const std::vector<InstanceId>& current, InstanceId hit, bool ctrl,
        InstanceId& newPrimary);

    // Rectangulo clamado al viewport (false si queda vacio). x0<x1, y0<y1.
    static bool marqueeRect(const Vec2& a, const Vec2& b, int vpW, int vpH,
                            float& x0, float& y0, float& x1, float& y1);

    // Selecciona los objetos del rectangulo; actualiza newPrimary.
    static std::vector<InstanceId> resolveMarquee(
        const World& world, const Mat4& viewProj, const Vec2& a, const Vec2& b,
        int vpW, int vpH, const std::vector<InstanceId>& current,
        InstanceId& newPrimary);

private:
    bool pending_ = false;
    bool active_ = false;
    Vec2 start_{};
    InstanceId pressHit_ = kInvalidId;
    bool pressCtrl_ = false;
};

} // namespace sk::editor
