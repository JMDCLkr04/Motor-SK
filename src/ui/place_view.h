#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>
#include <commctrl.h>

#include "../scene/instance.h"
#include "../scene/world.h"
#include "kit/tree_model.h"
#include "kit/tree_widget.h"
#include "output_panel.h"
#include "properties_panel.h"

namespace sk {

struct Project;

// Paneles laterales de la division PLACE: a la izquierda el grupo
// "Properties | Output" (widgets dibujados a mano) y a la derecha el
// Explorer (arbol del World), sobre la vista 3D. Son tres ventanas hijas
// de la principal (con WS_CLIPCHILDREN para que el present no las tape).
//
// El Explorer muestra el arbol del World: el grupo "Dimensions" con cada
// dimension y sus objetos y el grupo "Scripts" con el arbol global de
// scripts. Ademas, si hay proyecto, bajo las raices del nodo Scripts
// aparecen los scripts en disco (nodos sinteticos con id >=
// ui::kScriptNodeBase, resueltos por una ui::ScriptsTree). Properties lee
// las propiedades de la Instance seleccionada por id.
class PlaceView {
public:
    bool create(void* parentHwnd);
    void destroy();

    // Rutas base que alimentan el grupo sintetico Assets del Explorer.
    void setScriptsRoot(const std::string& path) { scriptsRoot_ = path; }
    void setAssetsRoot(const std::string& path) { assetsRoot_ = path; }

    // Proyecto del arbol de scripts en disco. Con el fijado, el Explorer
    // lista bajo cada raiz del nodo Scripts los ficheros y carpetas del
    // proyecto. Pasar nullptr al cerrar el proyecto.
    void setProject(const Project* project);

    // Reconstruye la parte de scripts del arbol (tras crear/renombrar/
    // borrar un script o carpeta). Conserva el estado de expansion.
    void refreshScripts();

    // World que alimenta el Explorer y las Properties. No se posee. Al
    // fijarlo, el Explorer se suscribe a los eventos del World (alta,
    // baja y reubicacion) para refrescar solo lo que cambia.
    void setWorld(World* world);

    // Panel Output (pestana izquierda): agrega una linea con el color
    // segun el nivel (0 info, 1 aviso, 2 error). El dueno engancha aqui
    // el sink de SK_INFO/WARN/ERROR.
    void appendOutput(int level, const std::string& message);

    // Activa la pestana izquierda indicada (p. ej. Output al ejecutar un
    // test, o Properties al seleccionar un objeto).
    void activateProperties() { switchLeftTab(0); }
    void activateOutput() { switchLeftTab(1); }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // width/height son los de la ventana principal; los paneles ocupan
    // desde editor::Layout::topBandHeight() hasta el borde inferior.
    void resize(int width, int height);

    // Reconstruye todo el arbol a partir del World y su jerarquia. Util
    // tras cargar, insertar, deshacer/rehacer o re-agrupar. Deja el caret
    // en el ultimo objeto.
    void rebuildTree(const World& world);
    void clearObjects();

    // Properties: refresca el panel con la instancia, la raiz o el estado
    // "Sin objeto seleccionado". La seleccion multiple (mismo o distinto
    // class) se pasa con showSelection.
    void showObject(const Instance& instance, bool relayout = true);
    void showRoot();
    void showNoSelection();
    void showSelection(const std::vector<InstanceId>& ids);

    // Seleccion multiple proveniente del viewport: mueve el caret del
    // arbol al objeto "primary" sin disparar onSelectionChanged.
    void syncTreeSelection(InstanceId id);

    // Se invoca con el id del nodo seleccionado (kInvalidId si no hay
    // seleccion). El dueno decide que hacer con el World.
    void setOnSelectionChanged(std::function<void(InstanceId)> cb) {
        onSelectionChanged_ = std::move(cb);
    }

    // Arrastrar un objeto del Explorer y soltarlo: dentro de otro (Inside),
    // antes o despues (reordenar entre hermanos). index < 0 = al final.
    void setOnReparent(
        std::function<void(InstanceId child, InstanceId newParent, int index)>
            cb) {
        onReparent_ = std::move(cb);
    }

    // Tecla Supr en el Explorer: el dueno borra los objetos indicados
    // (con undo y guardado).
    void setOnDeleteRequested(
        std::function<void(const std::vector<InstanceId>&)> cb) {
        onDelete_ = std::move(cb);
    }

    // Campo Nombre editado: el dueno valida y renombra (con undo y
    // guardado). El panel vuelve a leer el nombre del World despues.
    void setOnNameEdited(
        std::function<void(InstanceId id, const std::string& newName)> cb) {
        onNameEdited_ = std::move(cb);
    }

    // Propiedad generica editada (cualquier fila de Properties salvo el
    // Nombre): el dueno la convierte en comando (undo + guardado).
    void setOnPropertyEdited(
        std::function<void(InstanceId, const std::string&,
                           const PropertyValue&)>
            cb) {
        onPropertyEdited_ = std::move(cb);
    }

    // Secciones Attributes y Tags: operaciones granulares para no reescribir
    // todo el bloque en cada clic.
    void setOnAttributeSet(
        std::function<void(InstanceId, const Attribute&)> cb) {
        onAttributeSet_ = std::move(cb);
    }
    void setOnAttributeRemoved(
        std::function<void(InstanceId, const std::string&)> cb) {
        onAttributeRemoved_ = std::move(cb);
    }
    void setOnTagAdded(std::function<void(InstanceId, const std::string&)> cb) {
        onTagAdded_ = std::move(cb);
    }
    void setOnTagRemoved(
        std::function<void(InstanceId, const std::string&)> cb) {
        onTagRemoved_ = std::move(cb);
    }

    // Boton "+" del Explorer: insertar una clase bajo el nodo (parent).
    void setOnInsertClass(
        std::function<void(InstanceId parent, const std::string& className)>
            cb) {
        onInsertClass_ = std::move(cb);
    }

    // Operaciones del arbol de scripts en disco (nodos sinteticos): crear
    // un script dentro de la carpeta rel, renombrar el nodo rel, borrarlo
    // o seleccionarlo.
    void setOnScriptCreate(std::function<void(const std::string& rel)> cb) {
        onScriptCreate_ = std::move(cb);
    }
    void setOnScriptRename(
        std::function<void(const std::string& rel, const std::string& newName)>
            cb) {
        onScriptRename_ = std::move(cb);
    }
    void setOnScriptDelete(std::function<void(const std::string& rel)> cb) {
        onScriptDelete_ = std::move(cb);
    }
    void setOnScriptSelected(std::function<void(const std::string& rel)> cb) {
        onScriptSelected_ = std::move(cb);
    }

    // Renombra el nodo del Explorer (id -> newName) sin disparar
    // onSelectionChanged: el caret sigue en el mismo nodo.
    void renameTreeItem(InstanceId id, const std::string& newName);

    // Quita el nodo del Explorer (por id) sin disparar
    // onSelectionChanged: el dueno ya gestiona la seleccion.
    void removeTreeItem(InstanceId id);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam,
                                       long long lParam);
    void layoutPanels(int width, int height);
    void layoutChildren(int panelH);
    void notifySelection();
    void switchLeftTab(int tab);
    void rebuildScriptsTree();

    void* parent_ = nullptr;
    void* explorer_ = nullptr;
    void* properties_ = nullptr;   // contenedor de las pestanas izquierdas
    int leftTab_ = 0;              // 0 Properties, 1 Output
    ui::TreeModel treeModel_;      // modelo puro del Explorer
    ui::TreeWidget treeWidget_;    // arbol dibujado a mano
    ui::PropertiesPanel propsPanel_;  // pestana Properties
    ui::OutputPanel outputPanel_;     // pestana Output
    World* worldEvents_ = nullptr;    // World cuyos eventos escucha el Explorer
    const World* world_ = nullptr;
    std::string scriptsRoot_;  // carpeta scripts/ del proyecto
    std::string assetsRoot_;   // carpeta assets/ del proyecto (o raiz)
    const Project* project_ = nullptr;              // proyecto abierto (o null)
    std::unique_ptr<ui::ScriptsTree> scriptsTree_;  // arbol de scripts en disco
    bool visible_ = false;
    bool suppressNotify_ = false;  // syncTreeSelection mueve el caret a mano

    std::function<void(InstanceId)> onSelectionChanged_;
    std::function<void(InstanceId, InstanceId, int)> onReparent_;
    std::function<void(InstanceId, const std::string&)> onNameEdited_;
    std::function<void(InstanceId, const std::string&, const PropertyValue&)>
        onPropertyEdited_;
    std::function<void(InstanceId, const Attribute&)> onAttributeSet_;
    std::function<void(InstanceId, const std::string&)> onAttributeRemoved_;
    std::function<void(InstanceId, const std::string&)> onTagAdded_;
    std::function<void(InstanceId, const std::string&)> onTagRemoved_;
    std::function<void(InstanceId, const std::string&)> onInsertClass_;
    std::function<void(const std::vector<InstanceId>&)> onDelete_;
    std::function<void(const std::string&)> onScriptCreate_;
    std::function<void(const std::string&, const std::string&)> onScriptRename_;
    std::function<void(const std::string&)> onScriptDelete_;
    std::function<void(const std::string&)> onScriptSelected_;

    // Abre el selector de clase del boton "+" para el nodo indicado,
    // anclado a un punto de pantalla.
    void openClassPicker(InstanceId parentId, POINT screenPoint);
};

} // namespace sk
