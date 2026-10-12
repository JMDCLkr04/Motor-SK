#include "selection_controller.h"

#include <algorithm>
#include <cmath>

#include "../scene/picking.h"

namespace sk::editor {

void SelectionController::beginPress(const Vec2& p, bool ctrl,
                                     InstanceId pickHit) {
    pending_ = true;
    active_ = false;
    start_ = p;
    pressCtrl_ = ctrl;
    pressHit_ = pickHit;
}

void SelectionController::beginMarquee() {
    pending_ = false;
    active_ = true;
}

void SelectionController::reset() {
    pending_ = false;
    active_ = false;
    pressHit_ = kInvalidId;
    pressCtrl_ = false;
}

std::vector<InstanceId> SelectionController::resolveClick(
    const std::vector<InstanceId>& current, InstanceId hit, bool ctrl,
    InstanceId& newPrimary) {
    std::vector<InstanceId> ids = current;
    if (hit == kInvalidId) {
        ids.clear(); // click en vacio: deselecciona
        newPrimary = kInvalidId;
        return ids;
    }
    if (ctrl) {
        const auto it = std::find(ids.begin(), ids.end(), hit);
        if (it != ids.end()) {
            ids.erase(it); // quitar de la seleccion
            if (newPrimary == hit) {
                newPrimary = ids.empty() ? kInvalidId : ids.front();
            }
        } else {
            ids.push_back(hit); // anadir (Ctrl)
            newPrimary = hit;
        }
        return ids;
    }
    newPrimary = hit;
    return {hit};
}

bool SelectionController::marqueeRect(const Vec2& a, const Vec2& b, int vpW,
                                      int vpH, float& x0, float& y0, float& x1,
                                      float& y1) {
    const Layout vpLayout = Layout::compute(vpW, vpH);
    x0 = std::fmax(std::fmin(a.x, b.x),
                   static_cast<float>(vpLayout.viewport.x));
    y0 = std::fmax(std::fmin(a.y, b.y),
                   static_cast<float>(vpLayout.topBar.bottom()));
    x1 = std::fmin(std::fmax(a.x, b.x),
                   static_cast<float>(vpLayout.rightPanel.x));
    y1 = std::fmin(std::fmax(a.y, b.y), static_cast<float>(vpH));
    return x0 < x1 && y0 < y1;
}

std::vector<InstanceId> SelectionController::resolveMarquee(
    const World& world, const Mat4& viewProj, const Vec2& a, const Vec2& b,
    int vpW, int vpH, const std::vector<InstanceId>& current,
    InstanceId& newPrimary) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    std::vector<InstanceId> hits;
    if (marqueeRect(a, b, vpW, vpH, x0, y0, x1, y1)) {
        hits = selectInRect(world, viewProj, Vec2{x0, y0}, Vec2{x1, y1}, vpW,
                            vpH);
    }
    if (std::find(hits.begin(), hits.end(), newPrimary) == hits.end()) {
        newPrimary = hits.empty() ? kInvalidId : hits.front();
    }
    (void)current;
    return hits;
}

} // namespace sk::editor
