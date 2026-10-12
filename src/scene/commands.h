#pragma once

#include <memory>
#include <string>
#include <vector>

#include "instance.h"
#include "world.h"

namespace sk {

// Comando con deltas: guarda lo justo para deshacer/rehacer una accion.
// apply() es la ejecucion (tambien la del redo); undo() la revierte.
class Command {
public:
    virtual ~Command() = default;
    virtual void apply(World& world) = 0;
    virtual void undo(World& world) = 0;
    virtual const char* label() const = 0;
};

using CommandPtr = std::unique_ptr<Command>;

// Pilas de deshacer/rehacer (limite 100). push ejecuta el comando.
class UndoStack {
public:
    explicit UndoStack(size_t limit = 100) : limit_(limit) {}

    // Ejecuta el comando y lo apila; descarta la rama de rehacer.
    void push(World& world, CommandPtr cmd);

    bool canUndo() const { return index_ > 0; }
    bool canRedo() const { return index_ < stack_.size(); }
    bool undo(World& world);
    bool redo(World& world);
    void clear();

    const char* undoLabel() const {
        return canUndo() ? stack_[index_ - 1]->label() : "";
    }
    const char* redoLabel() const {
        return canRedo() ? stack_[index_]->label() : "";
    }

private:
    std::vector<CommandPtr> stack_;
    size_t index_ = 0;
    size_t limit_;
};

// --- Constructores de comandos ------------------------------------------
// El estado "antes" se lee al construir; apply() realiza la accion.
CommandPtr makeCreateCommand(const World& world, const std::string& className,
                             InstanceId parent, const Vec3& renderPos,
                             int shapeOverride = -1);
CommandPtr makeRemoveCommand(const World& world, InstanceId id);
CommandPtr makeReparentCommand(const World& world, InstanceId id,
                               InstanceId newParent);
// Variante con posicion entre hermanos (reordenar); index < 0 = al final.
CommandPtr makeReparentCommand(const World& world, InstanceId id,
                               InstanceId newParent, int index);
CommandPtr makeRenameCommand(const World& world, InstanceId id,
                             const std::string& newName);
CommandPtr makePropertyCommand(const World& world, InstanceId id,
                               const std::string& prop,
                               const PropertyValue& newValue);
CommandPtr makeAttributeCommand(const World& world, InstanceId id,
                                const Attribute& attr, bool remove);
CommandPtr makeTagCommand(const World& world, InstanceId id,
                          const std::string& tag, bool add);
CommandPtr makeTransformCommand(const World& world, InstanceId id,
                                const Vec3& position, const Vec3& rotation,
                                const Vec3& scale);

// Variante con estado "antes" explicito (un arrastre ya aplicado).
CommandPtr makeTransformCommandExplicit(InstanceId id, const Vec3& oldP,
                                        const Vec3& oldR, const Vec3& oldS,
                                        const Vec3& newP, const Vec3& newR,
                                        const Vec3& newS);

// Agrupa varios comandos en una sola entrada de deshacer.
CommandPtr makeCompoundCommand(std::vector<CommandPtr> parts);

// Clona el subarbol de id bajo parent, desplazado por offset (Ctrl+D).
CommandPtr makeCloneCommand(const World& world, InstanceId id,
                            InstanceId parent, const Vec3& offset,
                            InstanceId* outRoot = nullptr);

// Inserta un subarbol capturado (copiar/cortar/importar). newIds debe venir
// de world.allocateId(), uno por nodo, en el mismo orden que nodes.
CommandPtr makeInsertSubtreeCommand(const World& world,
                                    const std::vector<Instance>& nodes,
                                    const std::vector<InstanceId>& newIds,
                                    InstanceId parent, const Vec3& offset,
                                    const std::string& prefix,
                                    std::vector<InstanceId>* outRoots = nullptr);

// Crea un Model bajo parent y reparenta ahi los miembros.
CommandPtr makeGroupCommand(const World& world, InstanceId parent,
                            const std::vector<InstanceId>& members,
                            InstanceId* outGroup = nullptr);

// Desagrupa: sube los hijos de `group` a su padre y borra el grupo.
CommandPtr makeUngroupCommand(const World& world, InstanceId group);

// Crea una Dimension completa (Workspace, Camera, Lighting con sus efectos
// y Scripts) como raiz nueva, en una sola entrada de deshacer. outRoot
// recibe el id creado (kInvalidId si falla).
CommandPtr makeCreateDimensionCommand(const World& world,
                                      const std::string& name,
                                      InstanceId* outRoot = nullptr);

} // namespace sk
