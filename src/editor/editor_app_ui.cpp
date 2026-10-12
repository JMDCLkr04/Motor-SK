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

    place_.setOnTransformEdited(
        [this](InstanceId id, int fieldId, float value) {
            onTransformEdited(id, fieldId, value);
        });

    // Properties Nombre editable: al perder el foco o con Enter se
    // renombra el objeto en el World, en el Explorer y en los paneles.
    place_.setOnNameEdited([this](InstanceId id, const std::string& newName) {
        return doRename(id, newName);
    });

    place_.setOnBoolEdited([this](InstanceId id, int fieldId, bool value) {
        onBoolEdited(id, fieldId, value);
    });
    place_.setOnFloatEdited([this](InstanceId id, int fieldId, float value) {
        onFloatEdited(id, fieldId, value);
    });
    place_.setOnPivotEdited([this](InstanceId id, int fieldId, float value) {
        onPivotEdited(id, fieldId, value);
    });
    place_.setOnAttributesEdited([this](InstanceId id,
                                        const std::string& text) {
        onAttributesEdited(id, text);
    });
    place_.setOnTagsEdited([this](InstanceId id, const std::string& text) {
        onTagsEdited(id, text);
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

// Properties Transform editable: al perder el foco un campo se aplica al
// objeto, se guarda la escena y se reescriben los campos con el formato
// normalizado.
void EditorApp::onTransformEdited(InstanceId id, int fieldId, float value) {
    Instance* object = world_.find(id);
    if (!object) return;
    const int row = (fieldId - 200) / 3;
    const int axis = (fieldId - 200) % 3;
    static const char* kRows[] = {"position", "size", "orientation"};
    static const char* kAxes[] = {"x", "y", "z"};
    Vec3 p = object->position();
    Vec3 r = object->rotation();
    Vec3 s = object->scale();
    Vec3* target = &p;
    if (row == 1) target = &s;      // Size
    else if (row == 2) target = &r; // Orientation
    if (std::fabs((&target->x)[axis] - value) < 1e-6f) {
        return; // sin cambio: no ensucia la historia ni guarda
    }
    (&target->x)[axis] = value;
    undo_.push(world_, makeTransformCommand(world_, id, p, r, s));
    saveScene();
    if (const Instance* o = world_.find(id)) place_.showObject(*o);
    SK_INFO("Properties %s: %s.%s = %.3g", worldName(id).c_str(), kRows[row],
            kAxes[axis], value);
}

// Properties checkboxes (ids del panel: 400 CastShadow, 401 Locked,
// 402 CanCollide, 403 Anchored): se aplican al objeto y se guarda.
void EditorApp::onBoolEdited(InstanceId id, int fieldId, bool value) {
    Instance* object = world_.find(id);
    if (!object) return;
    const char* field = nullptr;
    bool def = false;
    switch (fieldId) {
    case 400: field = "CastShadow"; def = true; break;
    case 401: field = "Locked"; def = false; break;
    case 402: field = "CanCollide"; def = true; break;
    case 403: field = "Anchored"; def = false; break;
    default: return;
    }
    if (object->propBool(field, def) == value) {
        return; // sin cambio: no ensucia la historia ni guarda
    }
    PropertyValue pv;
    pv.b = value;
    undo_.push(world_, makePropertyCommand(world_, id, field, pv));
    saveScene();
    SK_INFO("Properties %s: %s = %s", worldName(id).c_str(), field,
            value ? "true" : "false");
}

// Properties Reflectance (410) / Transparency (411): el panel ya reclama
// el valor a 0..1; aqui se aplica y se reescriben los campos.
void EditorApp::onFloatEdited(InstanceId id, int fieldId, float value) {
    Instance* object = world_.find(id);
    if (!object) return;
    const char* field = nullptr;
    if (fieldId == 410) field = "Reflectance";
    else if (fieldId == 411) field = "Transparency";
    else return;
    if (std::fabs(object->propFloat(field) - value) < 1e-6f) {
        return; // sin cambio: no ensucia la historia ni guarda
    }
    PropertyValue pv;
    pv.f = value;
    undo_.push(world_, makePropertyCommand(world_, id, field, pv));
    saveScene();
    if (const Instance* o = world_.find(id)) place_.showObject(*o);
    SK_INFO("Properties %s: %s = %.2f", worldName(id).c_str(), field, value);
}

// Properties Pivot (500..502 Position, 503..505 Orientation): mueve el
// origen de la part sin tocar su position en el mundo.
void EditorApp::onPivotEdited(InstanceId id, int fieldId, float value) {
    Instance* object = world_.find(id);
    if (!object) return;
    const int idx = fieldId - 500;
    if (idx < 0 || idx >= 6) return;
    const char* field = idx < 3 ? "PivotPosition" : "PivotRotation";
    Vec3 v = object->propVec3(field);
    if (std::fabs((&v.x)[idx % 3] - value) < 1e-6f) {
        return; // sin cambio: no ensucia la historia ni guarda
    }
    (&v.x)[idx % 3] = value;
    PropertyValue pv;
    pv.v = v;
    undo_.push(world_, makePropertyCommand(world_, id, field, pv));
    saveScene();
    if (const Instance* o = world_.find(id)) place_.showObject(*o);
    SK_INFO("Properties %s: pivot[%d] = %.3g", worldName(id).c_str(), idx,
            value);
}

// Attributes: parsea "nombre | tipo | valor" por linea al objeto.
void EditorApp::onAttributesEdited(InstanceId id, const std::string& text) {
    Instance* object = world_.find(id);
    if (!object) return;
    std::vector<Attribute> parsed;
    std::string line;
    std::istringstream stream(text);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Recorta y salta lineas vacias.
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t");
        line = line.substr(a, b - a + 1);
        if (line.empty()) continue;
        Attribute attr;
        size_t p1 = line.find('|');
        size_t p2 = p1 == std::string::npos ? std::string::npos
                                            : line.find('|', p1 + 1);
        if (p1 == std::string::npos) {
            attr.name = line;
        } else if (p2 == std::string::npos) {
            attr.name = line.substr(0, p1);
            attr.value = line.substr(p1 + 1);
        } else {
            attr.name = line.substr(0, p1);
            attr.type = line.substr(p1 + 1, p2 - p1 - 1);
            attr.value = line.substr(p2 + 1);
        }
        auto trim = [](std::string s) {
            size_t f = s.find_first_not_of(" \t");
            size_t l = s.find_last_not_of(" \t");
            return f == std::string::npos ? std::string()
                                          : s.substr(f, l - f + 1);
        };
        attr.name = trim(attr.name);
        attr.type = trim(attr.type);
        attr.value = trim(attr.value);
        if (attr.type.empty()) attr.type = "text";
        if (!attr.name.empty()) parsed.push_back(std::move(attr));
    }
    if (parsed == object->getAttributes()) return;
    std::vector<CommandPtr> parts;
    for (const Attribute& old : object->getAttributes()) {
        bool still = false;
        for (const Attribute& n : parsed) {
            if (n.name == old.name) {
                still = true;
                break;
            }
        }
        if (!still) {
            parts.push_back(makeAttributeCommand(world_, id, old, true));
        }
    }
    for (const Attribute& n : parsed) {
        const Attribute prev = object->getAttribute(n.name);
        const bool present = object->hasAttribute(n.name);
        if (!present || !(prev == n)) {
            parts.push_back(makeAttributeCommand(world_, id, n, false));
        }
    }
    if (parts.empty()) return;
    undo_.push(world_, makeCompoundCommand(std::move(parts)));
    saveScene();
    SK_INFO("Attributes de %s actualizados", worldName(id).c_str());
}

// Tags: una etiqueta por linea.
void EditorApp::onTagsEdited(InstanceId id, const std::string& text) {
    Instance* object = world_.find(id);
    if (!object) return;
    std::vector<std::string> tags;
    std::string line;
    std::istringstream stream(text);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t");
        tags.push_back(line.substr(a, b - a + 1));
    }
    if (tags == object->getTags()) return;
    std::vector<CommandPtr> parts;
    for (const std::string& old : object->getTags()) {
        if (std::find(tags.begin(), tags.end(), old) == tags.end()) {
            parts.push_back(makeTagCommand(world_, id, old, false));
        }
    }
    for (const std::string& t : tags) {
        if (!object->hasTag(t)) {
            parts.push_back(makeTagCommand(world_, id, t, true));
        }
    }
    if (parts.empty()) return;
    undo_.push(world_, makeCompoundCommand(std::move(parts)));
    saveScene();
    SK_INFO("Tags de %s actualizados", worldName(id).c_str());
}

} // namespace sk::editor
