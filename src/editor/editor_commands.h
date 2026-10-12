#pragma once

#include <string>
#include <vector>

#include "../math/math.h"
#include "../scene/commands.h"
#include "../scene/instance.h"
#include "../scene/world.h"

namespace sk::editor {

// Comandos del editor: una sola implementacion para el atajo de teclado,
// el boton y el menu (todos llaman aqui). Cada funcion ejecuta la accion
// via UndoStack y devuelve lo justo para que el llamador refresque la UI
// (Explorer, Properties, camara); no toca widgets.
struct EditorCommands {
    // Borra los subarboles `tops` (ya filtrados a raices) como un unico
    // paso de undo. Devuelve cuantos objetos se borraron.
    static int remove(World& world, UndoStack& undo,
                      const std::vector<InstanceId>& tops);

    // Clona en el sitio (Ctrl+D). newRoots recibe las raices nuevas.
    static bool duplicate(World& world, UndoStack& undo,
                          const std::vector<InstanceId>& ids,
                          std::vector<InstanceId>& newRoots);

    // Crea un Model bajo parent con los miembros (Ctrl+G).
    static bool group(World& world, UndoStack& undo, InstanceId parent,
                      const std::vector<InstanceId>& members,
                      InstanceId& groupId);

    // Desagrupa los Model/Folder de la lista (Ctrl+U). groups recibe los
    // contenedores desagrupados.
    static bool ungroup(World& world, UndoStack& undo,
                        const std::vector<InstanceId>& ids,
                        std::vector<InstanceId>& groups);

    // Invierte una propiedad booleana (Locked / Anchored) de la seleccion.
    static bool toggleBool(World& world, UndoStack& undo,
                           const std::vector<InstanceId>& ids,
                           const char* field, bool def);

    // Renombra validando (vacio, igual, duplicado, protegido o
    // inexistente). finalName recibe el nombre aplicado (autonumerado si
    // hacia falta).
    static bool rename(World& world, UndoStack& undo, InstanceId id,
                       const std::string& newName, std::string& finalName);

    // Centro y radio de la caja envolvente de la seleccion (para enfocar
    // la camara con F). false si ningun id existe.
    static bool bounds(const World& world,
                       const std::vector<InstanceId>& ids, Vec3& center,
                       float& radius);
};

} // namespace sk::editor
