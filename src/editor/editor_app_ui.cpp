#include "editor_app.h"

#include <windows.h>

#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "editor_commands.h"

#include "../core/log.h"
#include "../project/scripts.h"
#include "../scene/property_value.h"
#include "../scene/shape.h"
#include "../ui/prompt.h"

namespace sk::editor {

// Conecta todos los callbacks de los paneles con los metodos del editor.
// Va aparte del resto del ciclo de vida para que la seccion de "cableado"
// quede junta y facil de leer.
void EditorApp::wireUi() {
    // El panel Output de PLACE recibe copia de todos los SK_INFO/WARN/ERROR.
    sk::setLogSink([this](sk::LogLevel level, const std::string& message) {
        place_.appendOutput(static_cast<int>(level), message);
    });

    // Solo la division PLACE muestra Explorer/Properties y solo CODE
    // muestra el organizador de scripts; GUI vendra despues.
    workspace_.setOnTabChanged([this](int tab) {
        docs_.setVisible(false);
        workspace_.setDocsActive(false);
        place_.setVisible(tab == 0);
        code_.setVisible(tab == 1);
    });

    // Boton "libro" de la navbar: muestra la documentacion de clases en
    // lugar del viewport 3D (oculta PLACE/CODE).
    workspace_.setOnDocs([this]() {
        place_.setVisible(false);
        code_.setVisible(false);
        docs_.setVisible(true);
        workspace_.setDocsActive(true);
    });

    // Atajo "Part": anade el objeto al World bajo el Workspace y refresca
    // el Explorer (insertar lo selecciona, lo que refresca Properties).
    workspace_.setOnAddPart([this](int shape) { onAddPart(shape); });

    // Menu "Archivo" de la barra de menus: cada comando decide aqui.
    workspace_.setOnFileCommand([this](int id) { onFileCommand(id); });

    // Cambio de herramienta (botones de la banda o teclas 1-4).
    workspace_.setOnToolChanged([this](int tool) {
        needsRedraw_ = true;
        ++contentVersion_; // el gizmo (y su resaltado) cambia
        SK_INFO("herramienta: %s", toolName(tool));
    });

    // Botones Play/Run/Stop de la banda: Play y Run abren una ventana
    // aparte con su propio renderer (Run ademas ejecuta los scripts);
    // Stop la cierra. Requieren un proyecto abierto.
    workspace_.setOnPlay([this]() {
        if (active_.folder.empty()) return;
        play_.open(world_, camera_, false);
    });
    workspace_.setOnRun([this]() {
        if (active_.folder.empty()) return;
        play_.open(world_, camera_, true);
    });
    workspace_.setOnStop([this]() { play_.close(); });

    // Re-agrupar desde el Explorer.
    place_.setOnReparent([this](InstanceId child, InstanceId parent, int index) {
        onReparent(child, parent, index);
    });

    // Seleccion en el Explorer -> Properties. El estado de seleccion
    // (multi) vive aqui y el arbol lo refleja.
    place_.setOnSelectionChanged([this](InstanceId id) {
        if (id != kInvalidId) {
            if (const Instance* object = world_.find(id)) {
                selection_ = {id};
                primary_ = id;
                place_.showObject(*object);
                return;
            }
        }
        selection_.clear();
        primary_ = kInvalidId;
        place_.showNoSelection();
    });

    // Properties: una unica ruta para toda fila editable (Position, Size,
    // Orientation, bools, floats, enums, ...). El panel entrega ya el
    // PropertyValue tipado.
    place_.setOnPropertyEdited(
        [this](InstanceId id, const std::string& prop,
               const PropertyValue& value) {
            onPropertyEdited(id, prop, value);
        });

    // Properties Nombre editable: al perder el foco o con Enter se
    // renombra el objeto en el World, en el Explorer y en los paneles.
    place_.setOnNameEdited([this](InstanceId id, const std::string& newName) {
        doRename(id, newName);
    });

    // Attributes y Tags: operaciones granulares (fila a fila).
    place_.setOnAttributeSet([this](InstanceId id, const Attribute& attr) {
        onAttributeSet(id, attr);
    });
    place_.setOnAttributeRemoved(
        [this](InstanceId id, const std::string& name) {
            onAttributeRemoved(id, name);
        });
    place_.setOnTagAdded([this](InstanceId id, const std::string& tag) {
        onTagAdded(id, tag);
    });
    place_.setOnTagRemoved([this](InstanceId id, const std::string& tag) {
        onTagRemoved(id, tag);
    });

    // Boton "+" del Explorer: inserta una clase bajo el nodo indicado.
    place_.setOnInsertClass([this](InstanceId parentId,
                                   const std::string& className) {
        onInsertClass(parentId, className);
    });

    // Tecla Supr en el Explorer: borra los nodos indicados.
    place_.setOnDeleteRequested([this](const std::vector<InstanceId>& ids) {
        onDeleteRequested(ids);
    });

    // CODE: el panel Properties consulta el objeto asociado a un script.
    code_.setOnQueryAssoc([this](const std::string& rel) -> std::string {
        for (InstanceId id : allInstances()) {
            const Instance* o = world_.find(id);
            if (!o || !o->hasAttribute("Script")) continue;
            if (o->getAttribute("Script").value == rel) return o->name();
        }
        return "";
    });

    // Borrar un script (o una carpeta con sus hijos) limpia las
    // asociaciones de la escena que apuntaban a esa rel.
    code_.setOnScriptRemoved([this](const std::string& rel) {
        bool changed = false;
        for (InstanceId id : allInstances()) {
            Instance* o = world_.find(id);
            if (!o || !o->hasAttribute("Script")) continue;
            const std::string value = o->getAttribute("Script").value;
            if (value == rel || value.rfind(rel + "/", 0) == 0) {
                o->removeAttribute("Script");
                changed = true;
            }
        }
        if (changed) saveScene();
    });

    // Renombrar (archivo o carpeta) reescribe las rels afectadas.
    code_.setOnScriptRenamed([this](const std::string& oldRel,
                                    const std::string& newRel) {
        bool changed = false;
        for (InstanceId id : allInstances()) {
            Instance* o = world_.find(id);
            if (!o || !o->hasAttribute("Script")) continue;
            const std::string value = o->getAttribute("Script").value;
            if (value == oldRel) {
                Attribute a = o->getAttribute("Script");
                a.value = newRel;
                o->setAttribute(a);
                changed = true;
            } else if (value.rfind(oldRel + "/", 0) == 0) {
                Attribute a = o->getAttribute("Script");
                a.value = newRel + value.substr(oldRel.size());
                o->setAttribute(a);
                changed = true;
            }
        }
        if (changed) saveScene();
    });
}

// Atajo "Part": crea la Part bajo el Workspace y la selecciona.
void EditorApp::onAddPart(int shape) {
    const Shape s =
        (shape >= 0 && shape < static_cast<int>(Shape::Count))
            ? static_cast<Shape>(shape)
            : Shape::Cube;
    const InstanceId parent = defaultParent();
    if (parent == kInvalidId) return;
    undo_.push(world_, makeCreateCommand(world_, "Part", parent, nextPartPos(),
                                         static_cast<int>(s)));
    InstanceId created = kInvalidId;
    if (const Instance* p = world_.find(parent)) {
        if (!p->children().empty()) created = p->children().back();
    }
    place_.rebuildTree(world_);
    if (created != kInvalidId) applySelection({created}, created);
    saveScene();
    logProjection_ = true;
    SK_INFO("Part anadido: %s (%s)", worldName(created).c_str(), shapeName(s));
}

// Menu "Archivo" de la barra de menus.
void EditorApp::onFileCommand(int id) {
    const HWND hwnd = static_cast<HWND>(window_.nativeHandle());
    switch (id) {
    case kFileClose:
        if (view3d_) closeProject();
        break;
    case kFileSave:
        if (view3d_) {
            writeSceneNow();
            SK_INFO("Escena guardada");
        }
        break;
    case kFileSaveAs: {
        if (!view3d_ || active_.folder.empty()) break;
        writeSceneNow(); // la copia parte del estado actual en disco
        const std::string newName =
            sk::ui::promptText(hwnd, "Guardar como", active_.name.c_str());
        if (newName.empty() || newName == active_.name) break;
        std::string newFolder;
        if (!sk::project::saveAs(active_.folder, newName, newFolder)) {
            MessageBoxA(hwnd,
                        "No se pudo guardar como: nombre invalido o ya "
                        "existe un proyecto con ese nombre.",
                        "Guardar como", MB_OK | MB_ICONWARNING);
        } else {
            openProject(newFolder);
        }
        break;
    }
    case kFileImport:
    case kFileImportAs: {
        char filename[MAX_PATH]{};
        OPENFILENAMEA ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter =
            "Escenas Motor SK (*.scene)\0*.scene\0"
            "Todos los archivos (*.*)\0*.*\0";
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameA(&ofn)) break;

        // "Importar como" pregunta un prefijo de nombre; los objetos
        // entran renombrados "prefijo <nombre original>".
        std::string prefix;
        if (id == kFileImportAs) {
            prefix = sk::ui::promptText(hwnd, "Importar como", "");
            if (!prefix.empty()) prefix += " ";
        }

        World incoming;
        if (!incoming.loadFromFile(ofn.lpstrFile)) {
            SK_ERROR("Importar: no se pudo leer %s", ofn.lpstrFile);
            break;
        }
        incoming.ensureStructure();
        const std::vector<InstanceId> inDims = incoming.dimensions();
        const InstanceId inWs =
            inDims.empty()
                ? kInvalidId
                : incoming.findChild(inDims.front(), kWorkspaceName);

        std::vector<Instance> nodes;
        std::vector<InstanceId> newIds;
        if (inWs != kInvalidId) {
            const Instance* wsInst = incoming.find(inWs);
            std::vector<InstanceId> tops;
            if (wsInst) tops = wsInst->children();
            for (InstanceId top : tops) {
                for (InstanceId sid : incoming.subtree(top)) {
                    if (const Instance* o = incoming.find(sid)) {
                        nodes.push_back(*o);
                        newIds.push_back(world_.allocateId());
                    }
                }
            }
        }
        if (nodes.empty()) {
            SK_INFO("Importar: %s esta vacia", ofn.lpstrFile);
            break;
        }
        std::vector<InstanceId> roots;
        undo_.push(world_, makeInsertSubtreeCommand(world_, nodes, newIds,
                                                    defaultParent(), Vec3{},
                                                    prefix, &roots));
        place_.rebuildTree(world_);
        if (!roots.empty()) applySelection(roots, roots.back());
        saveScene();
        logProjection_ = true;
        SK_INFO("Importados %d objeto(s) de %s",
                static_cast<int>(nodes.size()), ofn.lpstrFile);
        break;
    }
    case kFileEditorSettings:
        MessageBoxA(hwnd,
                    "La configuracion del editor todavia esta en "
                    "desarrollo.\n\n"
                    "Aqui se podran ajustar la camara, la rejilla y el "
                    "aspecto del editor.",
                    "Configuracion del editor", MB_OK | MB_ICONINFORMATION);
        break;
    case kFileShortcuts:
        // Lee la tabla unica de atajos (ShortcutMap): el dialogo y el
        // manejador de teclado comparten la misma fuente de verdad.
        MessageBoxA(hwnd, ShortcutMap::describe().c_str(),
                    "Personalizar atajos", MB_OK | MB_ICONINFORMATION);
        break;
    case kFileAutosaves:
        MessageBoxA(hwnd,
                    "No hay guardados automaticos todavia.\n\n"
                    "El guardado automatico llegara en un proximo paso.",
                    "Guardados automaticos", MB_OK | MB_ICONINFORMATION);
        break;
    case kFileExit:
        PostMessageA(static_cast<HWND>(window_.nativeHandle()), WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
}

// Boton "+" del Explorer: inserta una clase bajo el nodo indicado.
void EditorApp::onInsertClass(InstanceId parentId,
                              const std::string& className) {
    if (parentId == kInvalidId || !world_.exists(parentId)) return;
    undo_.push(world_, makeCreateCommand(world_, className, parentId,
                                         nextPartPos()));
    InstanceId created = kInvalidId;
    if (const Instance* p = world_.find(parentId)) {
        if (!p->children().empty()) created = p->children().back();
    }
    saveScene();
    place_.rebuildTree(world_);
    if (created != kInvalidId) {
        applySelection({created}, created);
    }
    SK_INFO("Insertado %s \"%s\" en %s", className.c_str(),
            worldName(created).c_str(), worldName(parentId).c_str());
}

// Tecla Supr en el Explorer: borra los nodos indicados (filtrando
// protegidos y descendientes de otros de la propia lista).
void EditorApp::onDeleteRequested(const std::vector<InstanceId>& ids) {
    std::vector<InstanceId> tops;
    for (InstanceId id : ids) {
        const Instance* o = world_.find(id);
        if (!o || o->isProtected()) continue;
        bool covered = false;
        for (InstanceId a : ids) {
            if (a == id) continue;
            const std::vector<InstanceId> sub = world_.subtree(a);
            if (std::find(sub.begin(), sub.end(), id) != sub.end()) {
                covered = true;
                break;
            }
        }
        if (!covered) tops.push_back(id);
    }
    performDelete(tops);
}

// Re-agrupar desde el Explorer: soltar X sobre Y (Inside) deja a X dentro
// de Y; soltar entre filas (Before/After) reordena entre los hermanos con
// index. Valida padre, ciclos y reordenamiento real.
void EditorApp::onReparent(InstanceId child, InstanceId parent, int index) {
    if (!view3d_ || child == kInvalidId) return;
    const Instance* object = world_.find(child);
    if (!object || object->isProtected()) return;
    if (parent == kInvalidId) return; // sin padre valido: no-op
    if (child == parent) return;
    if (!world_.exists(parent)) return;
    const bool sameParent = object->parentId() == parent;
    if (sameParent && index < 0) return; // nada que reordenar
    // Ciclo: el nuevo padre no puede ser descendiente del hijo.
    if (!sameParent) {
        for (InstanceId s : world_.subtree(child)) {
            if (s == parent) return;
        }
    }
    undo_.push(world_, makeReparentCommand(world_, child, parent, index));
    saveScene();
    place_.rebuildTree(world_);
    applySelection({child}, child);
    logProjection_ = true;
    SK_INFO("Explorer: %s %s %s", worldName(child).c_str(),
            sameParent ? "reordenado bajo" : "cuelga de",
            worldName(parent).c_str());
}

// Properties: entrada unica para toda fila editable. El panel entrega el
// PropertyValue ya tipado; aqui se comprueba que cambia, se aplica como
// comando (undo + guardado) y se deja que el panel relea el valor canonico.
void EditorApp::onPropertyEdited(InstanceId id, const std::string& prop,
                                 const PropertyValue& value) {
    Instance* object = world_.find(id);
    if (!object) return;
    const int ci = world_.registry().indexOf(object->className());
    const int slot = world_.registry().valueIndex(ci, prop);
    if (slot < 0) return;
    const PropertyDef& def = world_.registry().valueProp(ci, slot);
    const PropertyValue cur = object->valueBySlot(slot);
    if (serializePropertyValue(cur, def.type) ==
        serializePropertyValue(value, def.type)) {
        return; // sin cambio: no ensucia la historia ni guarda
    }
    undo_.push(world_, makePropertyCommand(world_, id, prop, value));
    saveScene();
    SK_INFO("Properties %s: %s", worldName(id).c_str(), prop.c_str());
}

// Attributes: alta o modificacion de un atributo concreto.
void EditorApp::onAttributeSet(InstanceId id, const Attribute& attr) {
    Instance* object = world_.find(id);
    if (!object || attr.name.empty()) return;
    if (object->hasAttribute(attr.name) &&
        object->getAttribute(attr.name) == attr) {
        return; // sin cambio
    }
    undo_.push(world_, makeAttributeCommand(world_, id, attr, false));
    saveScene();
    SK_INFO("Attribute %s de %s", attr.name.c_str(), worldName(id).c_str());
}

// Attributes: baja de un atributo.
void EditorApp::onAttributeRemoved(InstanceId id, const std::string& name) {
    Instance* object = world_.find(id);
    if (!object || !object->hasAttribute(name)) return;
    Attribute attr = object->getAttribute(name);
    undo_.push(world_, makeAttributeCommand(world_, id, attr, true));
    saveScene();
    SK_INFO("Attribute %s de %s eliminado", name.c_str(),
            worldName(id).c_str());
}

// Tags: alta de una etiqueta.
void EditorApp::onTagAdded(InstanceId id, const std::string& tag) {
    Instance* object = world_.find(id);
    if (!object || tag.empty() || object->hasTag(tag)) return;
    undo_.push(world_, makeTagCommand(world_, id, tag, true));
    saveScene();
    SK_INFO("Tag %s de %s", tag.c_str(), worldName(id).c_str());
}

// Tags: baja de una etiqueta.
void EditorApp::onTagRemoved(InstanceId id, const std::string& tag) {
    Instance* object = world_.find(id);
    if (!object || !object->hasTag(tag)) return;
    undo_.push(world_, makeTagCommand(world_, id, tag, false));
    saveScene();
    SK_INFO("Tag %s de %s eliminado", tag.c_str(), worldName(id).c_str());
}

} // namespace sk::editor
