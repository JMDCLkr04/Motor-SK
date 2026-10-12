#include "clipboard_controller.h"

namespace sk::editor {

void ClipboardController::clear() {
    clipboard_.clear();
    fromCut_ = false;
}

void ClipboardController::copy(const World& world,
                               const std::vector<InstanceId>& tops) {
    clipboard_.clear();
    for (InstanceId id : tops) {
        std::vector<Instance> nodes;
        for (InstanceId s : world.subtree(id)) {
            if (const Instance* o = world.find(s)) nodes.push_back(*o);
        }
        clipboard_.push_back(std::move(nodes));
    }
    fromCut_ = false;
}

void ClipboardController::cut(World& world, UndoStack& undo,
                              const std::vector<InstanceId>& tops) {
    if (tops.empty()) return;
    copy(world, tops);
    fromCut_ = true;
    std::vector<CommandPtr> parts;
    for (InstanceId id : tops) {
        parts.push_back(makeRemoveCommand(world, id));
    }
    undo.push(world, makeCompoundCommand(std::move(parts)));
}

InstanceId ClipboardController::sharedParent(const World& world) const {
    InstanceId shared = kInvalidId;
    for (size_t i = 0; i < clipboard_.size(); ++i) {
        if (clipboard_[i].empty()) continue;
        const InstanceId p = clipboard_[i].front().parentId();
        if (i == 0) {
            shared = p;
        } else if (p != shared) {
            return kInvalidId;
        }
    }
    if (!world.exists(shared)) return kInvalidId;
    return shared;
}

bool ClipboardController::paste(World& world, UndoStack& undo,
                                InstanceId target, const Vec3& offset,
                                std::vector<InstanceId>& outRoots) {
    std::vector<Instance> nodes;
    std::vector<InstanceId> newIds;
    for (const std::vector<Instance>& subtree : clipboard_) {
        for (const Instance& n : subtree) {
            nodes.push_back(n);
            newIds.push_back(world.allocateId());
        }
    }
    outRoots.clear();
    if (nodes.empty()) return false;
    undo.push(world, makeInsertSubtreeCommand(world, nodes, newIds, target,
                                              offset, "", &outRoots));
    return true;
}

} // namespace sk::editor
