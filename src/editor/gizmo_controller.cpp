#include "gizmo_controller.h"

namespace sk::editor {

bool GizmoController::updateHover(bool canHover, const World& /*world*/,
                                  int tool, const Vec3& origin, float scaleMax,
                                  const Vec3& viewDir, const Mat4& viewProj,
                                  int vpW, int vpH, const Vec2& mouse) {
    const int before = hoverAxis_;
    if (!canHover) {
        hoverAxis_ = -1;
    } else {
        const std::vector<GizmoLine> probe =
            buildGizmo(tool, origin, scaleMax, viewDir, -1);
        hoverAxis_ = hitTestGizmo(probe, mouse, viewProj, vpW, vpH);
    }
    return hoverAxis_ != before;
}

bool GizmoController::begin(const World& world, int tool, int axis,
                            const std::vector<InstanceId>& selection,
                            const Vec3& origin, float scaleMax,
                            const Vec3& viewDir, const Ray& ray) {
    if (axis < 0) return false;
    // Una parte Locked no se arrastra con el raton (sus campos Transform
    // siguen siendo editables desde Properties).
    for (InstanceId id : selection) {
        const Instance* object = world.find(id);
        if (object && object->propBool("Locked", false)) return false;
    }
    return gizmoBegin(drag_, tool, axis, world, selection, origin, scaleMax,
                      viewDir, ray);
}

bool GizmoController::update(World& world, const Ray& ray, float moveStep,
                             float rotStep) {
    if (!drag_.active) return false;
    return gizmoUpdate(drag_, ray, world, moveStep, rotStep);
}

int GizmoController::commit(World& world, UndoStack& undo) {
    if (!drag_.active) return 0;
    std::vector<CommandPtr> parts;
    for (const GizmoSnapshot& snap : drag_.snapshots) {
        const Instance* object = world.find(snap.id);
        if (!object) continue;
        parts.push_back(makeTransformCommandExplicit(
            snap.id, snap.position, snap.rotation, snap.scale,
            object->position(), object->rotation(), object->scale()));
    }
    drag_.active = false;
    if (!parts.empty()) {
        undo.push(world, makeCompoundCommand(std::move(parts)));
    }
    return static_cast<int>(parts.size());
}

void GizmoController::cancel(World& world) {
    if (!drag_.active) return;
    gizmoRevert(drag_, world);
    drag_.active = false;
}

int GizmoController::paintAxis() const {
    if (drag_.active) {
        return drag_.axis + (drag_.axisSign < 0 ? 3 : 0);
    }
    return hoverAxis_;
}

} // namespace sk::editor
