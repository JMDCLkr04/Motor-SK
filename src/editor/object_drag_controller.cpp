#include "object_drag_controller.h"

namespace sk::editor {

void ObjectDragController::begin(World& world,
                                 const std::vector<InstanceId>& selection,
                                 InstanceId pressHit, const Ray& pressRay) {
    active_ = true;
    pressHit_ = pressHit;
    starts_.clear();
    for (InstanceId id : selection) {
        const Instance* object = world.find(id);
        // Una parte Locked no viaja con el arrastre.
        if (!object || object->propBool("Locked", false)) continue;
        starts_.push_back({id, object->position(), object->rotation()});
    }
    // Punto de agarre (donde se pulso sobre el cuerpo) menos el centro del
    // lead: fija la posicion del grupo respecto al puntero.
    grabOffset_ = {};
    if (const Instance* hit = world.find(pressHit)) {
        Vec3 grabPoint{};
        Vec3 grabNormal{};
        if (rayObjectFace(*hit, pressRay, grabPoint, grabNormal)) {
            grabOffset_ = grabPoint - hit->position();
        }
    }
}

bool ObjectDragController::update(World& world, const Ray& ray0,
                                  const Ray& ray1, bool alignSurface,
                                  float grid) {
    if (!active_) return false;
    std::vector<InstanceId> dragged;
    std::vector<DragPose> startPose;
    dragged.reserve(starts_.size());
    startPose.reserve(starts_.size());
    int leadIndex = -1;
    for (const Start& start : starts_) {
        if (start.id == pressHit_) leadIndex = static_cast<int>(dragged.size());
        dragged.push_back(start.id);
        startPose.push_back({start.id, start.pos, start.rot});
    }
    DragResult result;
    computeSurfaceDrag(world, ray0, ray1, dragged, leadIndex, grabOffset_,
                       startPose, alignSurface, grid, result);
    for (const DragPose& pose : result.poses) {
        if (Instance* object = world.find(pose.id)) {
            object->setPosition(pose.position);
            object->setRotation(pose.rotation);
            world.markChanged(pose.id);
        }
    }
    return true;
}

int ObjectDragController::commit(World& world, UndoStack& undo) {
    if (!active_) return 0;
    std::vector<CommandPtr> parts;
    for (const Start& start : starts_) {
        const Instance* object = world.find(start.id);
        if (!object) continue;
        parts.push_back(makeTransformCommandExplicit(
            start.id, start.pos, start.rot, object->scale(),
            object->position(), object->rotation(), object->scale()));
    }
    active_ = false;
    pressHit_ = kInvalidId;
    starts_.clear();
    if (!parts.empty()) {
        undo.push(world, makeCompoundCommand(std::move(parts)));
    }
    return static_cast<int>(parts.size());
}

void ObjectDragController::cancel(World& world) {
    if (!active_) return;
    // Revierte posicion y rotacion (el snap de superficie pudo girar el
    // grupo).
    for (const Start& start : starts_) {
        if (Instance* object = world.find(start.id)) {
            object->setPosition(start.pos);
            object->setRotation(start.rot);
            world.markChanged(start.id);
        }
    }
    active_ = false;
    pressHit_ = kInvalidId;
    starts_.clear();
}

void ObjectDragController::reset() {
    active_ = false;
    pressHit_ = kInvalidId;
    grabOffset_ = {};
    starts_.clear();
}

} // namespace sk::editor
