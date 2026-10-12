#include "editor_commands.h"

#include <algorithm>
#include <cmath>

namespace sk::editor {

int EditorCommands::remove(World& world, UndoStack& undo,
                           const std::vector<InstanceId>& tops) {
    if (tops.empty()) return 0;
    int count = 0;
    for (InstanceId id : tops) {
        count += static_cast<int>(world.subtree(id).size());
    }
    std::vector<CommandPtr> parts;
    for (InstanceId id : tops) {
        parts.push_back(makeRemoveCommand(world, id));
    }
    undo.push(world, makeCompoundCommand(std::move(parts)));
    return count;
}

bool EditorCommands::duplicate(World& world, UndoStack& undo,
                               const std::vector<InstanceId>& ids,
                               std::vector<InstanceId>& newRoots) {
    std::vector<CommandPtr> parts;
    newRoots.clear();
    for (InstanceId id : ids) {
        const Instance* object = world.find(id);
        if (!object) continue;
        InstanceId root = kInvalidId;
        parts.push_back(makeCloneCommand(world, id, object->parentId(), Vec3{},
                                         &root));
        if (root != kInvalidId) newRoots.push_back(root);
    }
    if (parts.empty()) return false;
    undo.push(world, makeCompoundCommand(std::move(parts)));
    return true;
}

bool EditorCommands::group(World& world, UndoStack& undo, InstanceId parent,
                           const std::vector<InstanceId>& members,
                           InstanceId& groupId) {
    groupId = kInvalidId;
    if (members.empty()) return false;
    undo.push(world, makeGroupCommand(world, parent, members, &groupId));
    return groupId != kInvalidId;
}

bool EditorCommands::ungroup(World& world, UndoStack& undo,
                             const std::vector<InstanceId>& ids,
                             std::vector<InstanceId>& groups) {
    std::vector<CommandPtr> parts;
    groups.clear();
    for (InstanceId id : ids) {
        const Instance* object = world.find(id);
        if (!object || world.isProtected(id)) continue;
        if (object->className() != "Model" && object->className() != "Folder") {
            continue;
        }
        groups.push_back(id);
        parts.push_back(makeUngroupCommand(world, id));
    }
    if (parts.empty()) return false;
    undo.push(world, makeCompoundCommand(std::move(parts)));
    return true;
}

bool EditorCommands::toggleBool(World& world, UndoStack& undo,
                                const std::vector<InstanceId>& ids,
                                const char* field, bool def) {
    std::vector<CommandPtr> parts;
    for (InstanceId id : ids) {
        const Instance* o = world.find(id);
        if (!o || world.isProtected(id)) continue;
        PropertyValue pv;
        pv.b = !o->propBool(field, def);
        parts.push_back(makePropertyCommand(world, id, field, pv));
    }
    if (parts.empty()) return false;
    undo.push(world, makeCompoundCommand(std::move(parts)));
    return true;
}

bool EditorCommands::rename(World& world, UndoStack& undo, InstanceId id,
                            const std::string& newName,
                            std::string& finalName) {
    const Instance* object = world.find(id);
    if (!object || object->isProtected()) return false;
    const std::string oldName = object->name();
    if (newName.empty() || oldName == newName) return false;
    if (world.findChild(object->parentId(), newName) != kInvalidId) {
        return false;
    }
    undo.push(world, makeRenameCommand(world, id, newName));
    const Instance* renamed = world.find(id);
    finalName = renamed ? renamed->name() : newName;
    return true;
}

bool EditorCommands::bounds(const World& world,
                            const std::vector<InstanceId>& ids, Vec3& center,
                            float& radius) {
    Vec3 minV{1e30f, 1e30f, 1e30f};
    Vec3 maxV{-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (InstanceId id : ids) {
        const Instance* object = world.find(id);
        if (!object) continue;
        any = true;
        const Vec3 pos = object->position();
        const Vec3 scale = object->scale();
        for (int a = 0; a < 3; ++a) {
            const float half = std::fabs((&scale.x)[a]) * 0.5f;
            const float lo = (&pos.x)[a] - half;
            const float hi = (&pos.x)[a] + half;
            (&minV.x)[a] = std::fmin((&minV.x)[a], lo);
            (&maxV.x)[a] = std::fmax((&maxV.x)[a], hi);
        }
    }
    if (!any) return false;
    center = (minV + maxV) * 0.5f;
    radius = length(maxV - minV) * 0.5f;
    return true;
}

} // namespace sk::editor
