#include "commands.h"

#include <algorithm>

namespace sk {

namespace {

size_t indexInParent(const World& world, InstanceId parent, InstanceId id) {
    if (parent == kInvalidId) {
        const std::vector<InstanceId>& r = world.roots();
        auto it = std::find(r.begin(), r.end(), id);
        return it == r.end() ? r.size()
                             : static_cast<size_t>(it - r.begin());
    }
    const Instance* p = world.find(parent);
    if (!p) return 0;
    const std::vector<InstanceId>& c = p->children();
    auto it = std::find(c.begin(), c.end(), id);
    return it == c.end() ? c.size() : static_cast<size_t>(it - c.begin());
}

size_t childCount(const World& world, InstanceId parent) {
    if (parent == kInvalidId) return world.roots().size();
    const Instance* p = world.find(parent);
    return p ? p->children().size() : 0;
}

std::vector<Instance> captureSubtree(const World& world, InstanceId id) {
    std::vector<Instance> out;
    for (InstanceId sid : world.subtree(id)) {
        const Instance* inst = world.find(sid);
        if (inst) out.push_back(*inst);
    }
    return out;
}

// --- Crear ---------------------------------------------------------------
class CreateCommand final : public Command {
public:
    CreateCommand(const World& world, const std::string& className,
                  InstanceId parent, const Vec3& renderPos, int shapeOverride)
        : parent_(parent) {
        const ClassDef* cls = world.registry().find(className);
        if (!cls) return;
        id_ = world.allocateId();
        valid_ = true;
        proto_ = Instance(id_, cls, world.uniqueName(parent, className));
        if (isRenderableClass(className)) {
            proto_.setPosition(renderPos);
            if (shapeOverride >= 0) {
                proto_.setShape(static_cast<Shape>(shapeOverride));
            }
        }
        index_ = childCount(world, parent);
    }

    void apply(World& world) override {
        if (!valid_ || world.exists(id_)) return;
        world.insertInstance(proto_);
        world.adoptChild(parent_, id_, index_);
        world.markChanged(id_);
    }
    void undo(World& world) override {
        if (!valid_) return;
        world.detachChild(parent_, id_);
        world.eraseInstance(id_);
    }
    const char* label() const override { return "Insertar"; }

private:
    bool valid_ = false;
    InstanceId id_ = kInvalidId;
    Instance proto_;
    InstanceId parent_ = kInvalidId;
    size_t index_ = 0;
};

// --- Borrar --------------------------------------------------------------
class RemoveCommand final : public Command {
public:
    RemoveCommand(const World& world, InstanceId id) : id_(id) {
        const Instance* inst = world.find(id);
        if (!inst) return;
        valid_ = true;
        parent_ = inst->parentId();
        index_ = indexInParent(world, parent_, id);
        nodes_ = captureSubtree(world, id);
    }

    void apply(World& world) override {
        if (valid_) world.remove(id_);
    }
    void undo(World& world) override {
        if (!valid_) return;
        world.restoreInstances(nodes_, parent_, index_);
        for (const Instance& n : nodes_) {
            for (const std::string& t : n.getTags()) {
                world.collection().addTag(n.id(), t);
            }
        }
        world.markChanged(id_);
    }
    const char* label() const override { return "Borrar"; }

private:
    bool valid_ = false;
    InstanceId id_ = kInvalidId;
    InstanceId parent_ = kInvalidId;
    size_t index_ = 0;
    std::vector<Instance> nodes_;
};

// --- Reubicar ------------------------------------------------------------
class ReparentCommand final : public Command {
public:
    ReparentCommand(const World& world, InstanceId id, InstanceId newParent,
                    int index = -1)
        : id_(id), newParent_(newParent) {
        const Instance* inst = world.find(id);
        if (!inst) return;
        if (newParent != kInvalidId && !world.exists(newParent)) return;
        if (inst->isProtected()) return;
        valid_ = true;
        oldParent_ = inst->parentId();
        oldName_ = inst->name();
        oldIndex_ = indexInParent(world, oldParent_, id);
        if (oldParent_ == newParent) {
            // Reordenar entre hermanos: conserva el nombre (si se pidiera
            // uno unico chocaria consigo mismo).
            newName_ = oldName_;
        } else {
            newName_ = world.uniqueName(newParent, oldName_);
        }
        const size_t count = childCount(world, newParent);
        if (index < 0 || static_cast<size_t>(index) > count) {
            newIndex_ = count;
        } else {
            newIndex_ = static_cast<size_t>(index);
        }
    }

    void apply(World& world) override {
        if (!valid_) return;
        world.detachChild(oldParent_, id_);
        if (Instance* i = world.find(id_)) i->setName(newName_);
        world.adoptChild(newParent_, id_, newIndex_);
        world.markChanged(id_);
    }
    void undo(World& world) override {
        if (!valid_) return;
        world.detachChild(newParent_, id_);
        if (Instance* i = world.find(id_)) i->setName(oldName_);
        world.adoptChild(oldParent_, id_, oldIndex_);
        world.markChanged(id_);
    }
    const char* label() const override { return "Mover"; }

private:
    bool valid_ = false;
    InstanceId id_ = kInvalidId;
    InstanceId oldParent_ = kInvalidId;
    InstanceId newParent_ = kInvalidId;
    size_t oldIndex_ = 0;
    size_t newIndex_ = 0;
    std::string oldName_;
    std::string newName_;
};

// --- Renombrar -----------------------------------------------------------
class RenameCommand final : public Command {
public:
    RenameCommand(const World& world, InstanceId id, const std::string& name)
        : id_(id) {
        const Instance* inst = world.find(id);
        if (!inst || inst->isProtected()) return;
        valid_ = true;
        oldName_ = inst->name();
        newName_ = world.uniqueName(inst->parentId(), name);
    }

    void apply(World& world) override {
        if (!valid_) return;
        world.setNameIndexed(id_, newName_);
        world.markChanged(id_);
    }
    void undo(World& world) override {
        if (!valid_) return;
        world.setNameIndexed(id_, oldName_);
        world.markChanged(id_);
    }
    const char* label() const override { return "Renombrar"; }

private:
    bool valid_ = false;
    InstanceId id_ = kInvalidId;
    std::string oldName_;
    std::string newName_;
};

// --- Propiedad -----------------------------------------------------------
class PropertyCommand final : public Command {
public:
    PropertyCommand(const World& world, InstanceId id, const std::string& prop,
                    const PropertyValue& value)
        : id_(id) {
        const Instance* inst = world.find(id);
        if (!inst) return;
        const int ci = world.registry().indexOf(inst->className());
        slot_ = world.registry().valueIndex(ci, prop);
        if (slot_ < 0) return;
        valid_ = true;
        oldValue_ = inst->valueBySlot(slot_);
        newValue_ = value;
    }

    void apply(World& world) override {
        if (valid_) world.setValue(id_, slot_, newValue_);
    }
    void undo(World& world) override {
        if (valid_) world.setValue(id_, slot_, oldValue_);
    }
    const char* label() const override { return "Propiedad"; }

private:
    bool valid_ = false;
    InstanceId id_ = kInvalidId;
    int slot_ = -1;
    PropertyValue oldValue_;
    PropertyValue newValue_;
};

// --- Atributo ------------------------------------------------------------
class AttributeCommand final : public Command {
public:
    AttributeCommand(const World& world, InstanceId id, const Attribute& attr,
                     bool remove)
        : id_(id), attr_(attr), remove_(remove) {
        const Instance* inst = world.find(id);
        if (!inst) return;
        valid_ = true;
        wasPresent_ = inst->hasAttribute(attr.name);
        old_ = inst->getAttribute(attr.name);
    }

    void apply(World& world) override {
        if (!valid_) return;
        if (remove_) world.removeAttribute(id_, attr_.name);
        else world.setAttribute(id_, attr_);
    }
    void undo(World& world) override {
        if (!valid_) return;
        if (remove_) {
            world.setAttribute(id_, old_);
        } else if (wasPresent_) {
            world.setAttribute(id_, old_);
        } else {
            world.removeAttribute(id_, attr_.name);
        }
    }
    const char* label() const override { return "Atributo"; }

private:
    bool valid_ = false;
    InstanceId id_ = kInvalidId;
    Attribute attr_;
    Attribute old_;
    bool remove_ = false;
    bool wasPresent_ = false;
};

// --- Tag -----------------------------------------------------------------
class TagCommand final : public Command {
public:
    TagCommand(InstanceId id, const std::string& tag, bool add)
        : id_(id), tag_(tag), add_(add) {}

    void apply(World& world) override {
        if (add_) world.addTag(id_, tag_);
        else world.removeTag(id_, tag_);
    }
    void undo(World& world) override {
        if (add_) world.removeTag(id_, tag_);
        else world.addTag(id_, tag_);
    }
    const char* label() const override { return "Etiqueta"; }

private:
    InstanceId id_ = kInvalidId;
    std::string tag_;
    bool add_ = false;
};

// --- Transformacion ------------------------------------------------------
class TransformCommand final : public Command {
public:
    TransformCommand(const World& world, InstanceId id, const Vec3& p,
                     const Vec3& r, const Vec3& s)
        : id_(id), newP_(p), newR_(r), newS_(s) {
        const Instance* inst = world.find(id);
        if (!inst) return;
        valid_ = true;
        oldP_ = inst->position();
        oldR_ = inst->rotation();
        oldS_ = inst->scale();
    }

    // Variante con el estado "antes" explicito.
    TransformCommand(InstanceId id, const Vec3& op, const Vec3& orr,
                     const Vec3& os, const Vec3& np, const Vec3& nr,
                     const Vec3& ns)
        : id_(id), oldP_(op), oldR_(orr), oldS_(os), newP_(np), newR_(nr),
          newS_(ns), valid_(true) {}

    void apply(World& world) override {
        if (valid_) world.setTransform(id_, newP_, newR_, newS_);
    }
    void undo(World& world) override {
        if (valid_) world.setTransform(id_, oldP_, oldR_, oldS_);
    }
    const char* label() const override { return "Transformar"; }

private:
    bool valid_ = false;
    InstanceId id_ = kInvalidId;
    Vec3 oldP_, oldR_, oldS_;
    Vec3 newP_, newR_, newS_;
};

// --- Compuesto -----------------------------------------------------------
class CompoundCommand final : public Command {
public:
    explicit CompoundCommand(std::vector<CommandPtr> parts)
        : parts_(std::move(parts)) {}

    void apply(World& world) override {
        for (CommandPtr& c : parts_) c->apply(world);
    }
    void undo(World& world) override {
        for (auto it = parts_.rbegin(); it != parts_.rend(); ++it) {
            (*it)->undo(world);
        }
    }
    const char* label() const override { return "Edicion"; }

private:
    std::vector<CommandPtr> parts_;
};

// --- Insertar subarbol (copiar/cortar/importar/clonar) -------------------
class InsertSubtreeCommand final : public Command {
public:
    InsertSubtreeCommand(const World& world, std::vector<Instance> nodes,
                         std::vector<InstanceId> newIds, InstanceId parent,
                         Vec3 offset, std::string prefix,
                         std::vector<InstanceId>* outRoots)
        : nodes_(std::move(nodes)), newIds_(std::move(newIds)),
          parent_(parent), offset_(offset) {
        if (nodes_.empty() || nodes_.size() != newIds_.size()) return;
        valid_ = true;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            map_[nodes_[i].id()] = newIds_[i];
        }
        newParent_.resize(nodes_.size());
        names_.resize(nodes_.size());
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const InstanceId op = nodes_[i].parentId();
            auto it = map_.find(op);
            if (it == map_.end()) {
                newParent_[i] = parent_;
                names_[i] =
                    world.uniqueName(parent_, prefix + nodes_[i].name());
                if (outRoots) outRoots->push_back(newIds_[i]);
            } else {
                newParent_[i] = it->second;
                names_[i] = nodes_[i].name();
            }
        }
    }

    void apply(World& world) override {
        if (!valid_) return;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            if (world.exists(newIds_[i])) continue;
            const ClassDef* cls = world.registry().find(nodes_[i].className());
            Instance inst(newIds_[i], cls, names_[i]);
            inst.mutableValues() = nodes_[i].values();
            inst.mutableAttributes() = nodes_[i].getAttributes();
            inst.setTags(nodes_[i].getTags());
            inst.setProtected(nodes_[i].isProtected());
            if (inst.isRenderable()) {
                inst.setPosition(inst.position() + offset_);
            }
            world.insertInstance(inst);
        }
        for (size_t i = 0; i < nodes_.size(); ++i) {
            Instance* p = world.find(newParent_[i]);
            const size_t idx = p ? p->children().size() : 0;
            world.adoptChild(newParent_[i], newIds_[i], idx);
            for (const std::string& t : nodes_[i].getTags()) {
                world.collection().addTag(newIds_[i], t);
            }
            world.markChanged(newIds_[i]);
        }
    }
    void undo(World& world) override {
        if (!valid_) return;
        for (size_t i = nodes_.size(); i-- > 0;) {
            world.collection().clearTags(newIds_[i]);
            world.detachChild(newParent_[i], newIds_[i]);
            world.eraseInstance(newIds_[i]);
        }
    }
    const char* label() const override { return "Pegar"; }

private:
    bool valid_ = false;
    std::vector<Instance> nodes_;
    std::vector<InstanceId> newIds_;
    InstanceId parent_ = kInvalidId;
    Vec3 offset_{};
    std::unordered_map<InstanceId, InstanceId> map_;
    std::vector<InstanceId> newParent_;
    std::vector<std::string> names_;
};

// --- Agrupar -------------------------------------------------------------
class GroupCommand final : public Command {
public:
    GroupCommand(const World& world, InstanceId parent,
                 const std::vector<InstanceId>& members,
                 InstanceId* outGroup)
        : parent_(parent) {
        const ClassDef* cls = world.registry().find("Model");
        if (!cls) return;
        group_ = world.allocateId();
        valid_ = true;
        groupName_ = world.uniqueName(parent, "Model");
        for (InstanceId m : members) {
            const Instance* inst = world.find(m);
            if (!inst || inst->isProtected() || m == group_) continue;
            Member mb;
            mb.id = m;
            mb.parent = inst->parentId();
            mb.index = indexInParent(world, mb.parent, m);
            members_.push_back(mb);
        }
        if (outGroup) *outGroup = group_;
    }

    void apply(World& world) override {
        if (!valid_ || world.exists(group_)) return;
        const ClassDef* cls = world.registry().find("Model");
        world.insertInstance(Instance(group_, cls, groupName_));
        world.adoptChild(parent_, group_,
                         childCount(world, parent_));
        for (const Member& m : members_) {
            world.detachChild(m.parent, m.id);
            world.adoptChild(group_, m.id,
                             world.find(group_)->children().size());
            world.markChanged(m.id);
        }
        world.markChanged(group_);
    }
    void undo(World& world) override {
        if (!valid_) return;
        for (size_t i = members_.size(); i-- > 0;) {
            const Member& m = members_[i];
            world.detachChild(group_, m.id);
            world.adoptChild(m.parent, m.id, m.index);
            world.markChanged(m.id);
        }
        world.detachChild(parent_, group_);
        world.eraseInstance(group_);
    }
    const char* label() const override { return "Agrupar"; }

private:
    struct Member {
        InstanceId id = kInvalidId;
        InstanceId parent = kInvalidId;
        size_t index = 0;
    };
    bool valid_ = false;
    InstanceId parent_ = kInvalidId;
    InstanceId group_ = kInvalidId;
    std::string groupName_;
    std::vector<Member> members_;
};

// --- Desagrupar ----------------------------------------------------------
class UngroupCommand final : public Command {
public:
    UngroupCommand(const World& world, InstanceId group) : group_(group) {
        const Instance* g = world.find(group);
        if (!g || g->isProtected()) return;
        valid_ = true;
        parent_ = g->parentId();
        index_ = indexInParent(world, parent_, group);
        groupInst_ = *g;
        groupInst_.clearChildren();
        for (InstanceId c : g->children()) {
            Member m;
            m.id = c;
            m.index = indexInParent(world, group, c);
            members_.push_back(m);
        }
    }

    void apply(World& world) override {
        if (!valid_) return;
        for (const Member& m : members_) {
            world.detachChild(group_, m.id);
            world.adoptChild(parent_, m.id, childCount(world, parent_));
            world.markChanged(m.id);
        }
        world.detachChild(parent_, group_);
        world.eraseInstance(group_);
    }
    void undo(World& world) override {
        if (!valid_) return;
        world.insertInstance(groupInst_);
        world.adoptChild(parent_, group_, index_);
        for (const Member& m : members_) {
            world.detachChild(parent_, m.id);
            world.adoptChild(group_, m.id, m.index);
            world.markChanged(m.id);
        }
        world.markChanged(group_);
    }
    const char* label() const override { return "Desagrupar"; }

private:
    struct Member {
        InstanceId id = kInvalidId;
        size_t index = 0;
    };
    bool valid_ = false;
    InstanceId group_ = kInvalidId;
    InstanceId parent_ = kInvalidId;
    size_t index_ = 0;
    Instance groupInst_;
    std::vector<Member> members_;
};

} // namespace

// =========================================================================
// UndoStack
// =========================================================================
void UndoStack::push(World& world, CommandPtr cmd) {
    if (!cmd) return;
    cmd->apply(world);
    if (index_ < stack_.size()) stack_.resize(index_);
    stack_.push_back(std::move(cmd));
    ++index_;
    if (stack_.size() > limit_) {
        stack_.erase(stack_.begin());
        --index_;
    }
}

bool UndoStack::undo(World& world) {
    if (index_ == 0) return false;
    --index_;
    stack_[index_]->undo(world);
    return true;
}

bool UndoStack::redo(World& world) {
    if (index_ >= stack_.size()) return false;
    stack_[index_]->apply(world);
    ++index_;
    return true;
}

void UndoStack::clear() {
    stack_.clear();
    index_ = 0;
}

// =========================================================================
// Constructores
// =========================================================================
CommandPtr makeCreateCommand(const World& world, const std::string& className,
                             InstanceId parent, const Vec3& renderPos,
                             int shapeOverride) {
    return std::make_unique<CreateCommand>(world, className, parent, renderPos,
                                           shapeOverride);
}

CommandPtr makeRemoveCommand(const World& world, InstanceId id) {
    return std::make_unique<RemoveCommand>(world, id);
}

CommandPtr makeReparentCommand(const World& world, InstanceId id,
                               InstanceId newParent) {
    return std::make_unique<ReparentCommand>(world, id, newParent);
}

CommandPtr makeReparentCommand(const World& world, InstanceId id,
                               InstanceId newParent, int index) {
    return std::make_unique<ReparentCommand>(world, id, newParent, index);
}

CommandPtr makeRenameCommand(const World& world, InstanceId id,
                             const std::string& newName) {
    return std::make_unique<RenameCommand>(world, id, newName);
}

CommandPtr makePropertyCommand(const World& world, InstanceId id,
                               const std::string& prop,
                               const PropertyValue& newValue) {
    return std::make_unique<PropertyCommand>(world, id, prop, newValue);
}

CommandPtr makeAttributeCommand(const World& world, InstanceId id,
                                const Attribute& attr, bool remove) {
    return std::make_unique<AttributeCommand>(world, id, attr, remove);
}

CommandPtr makeTagCommand(const World& world, InstanceId id,
                          const std::string& tag, bool add) {
    (void)world;
    return std::make_unique<TagCommand>(id, tag, add);
}

CommandPtr makeTransformCommand(const World& world, InstanceId id,
                                const Vec3& position, const Vec3& rotation,
                                const Vec3& scale) {
    return std::make_unique<TransformCommand>(world, id, position, rotation,
                                              scale);
}

CommandPtr makeTransformCommandExplicit(InstanceId id, const Vec3& oldP,
                                        const Vec3& oldR, const Vec3& oldS,
                                        const Vec3& newP, const Vec3& newR,
                                        const Vec3& newS) {
    return std::make_unique<TransformCommand>(id, oldP, oldR, oldS, newP, newR,
                                              newS);
}

CommandPtr makeCompoundCommand(std::vector<CommandPtr> parts) {
    return std::make_unique<CompoundCommand>(std::move(parts));
}

CommandPtr makeCloneCommand(const World& world, InstanceId id,
                            InstanceId parent, const Vec3& offset,
                            InstanceId* outRoot) {
    std::vector<Instance> nodes = captureSubtree(world, id);
    std::vector<InstanceId> newIds;
    newIds.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) newIds.push_back(world.allocateId());
    std::vector<InstanceId> roots;
    auto cmd = std::make_unique<InsertSubtreeCommand>(
        world, std::move(nodes), std::move(newIds), parent, offset, "",
        outRoot ? &roots : nullptr);
    if (outRoot && !roots.empty()) *outRoot = roots.front();
    return cmd;
}

CommandPtr makeInsertSubtreeCommand(const World& world,
                                    const std::vector<Instance>& nodes,
                                    const std::vector<InstanceId>& newIds,
                                    InstanceId parent, const Vec3& offset,
                                    const std::string& prefix,
                                    std::vector<InstanceId>* outRoots) {
    return std::make_unique<InsertSubtreeCommand>(world, nodes, newIds, parent,
                                                  offset, prefix, outRoots);
}

CommandPtr makeGroupCommand(const World& world, InstanceId parent,
                            const std::vector<InstanceId>& members,
                            InstanceId* outGroup) {
    return std::make_unique<GroupCommand>(world, parent, members, outGroup);
}

CommandPtr makeUngroupCommand(const World& world, InstanceId group) {
    return std::make_unique<UngroupCommand>(world, group);
}

} // namespace sk
