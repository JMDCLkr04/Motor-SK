#pragma once

#include <functional>
#include <string>
#include <vector>

#include <windows.h>
#include <commctrl.h>

#include "../scene/instance.h"
#include "../scene/world.h"
#include "kit/tree_model.h"
#include "kit/tree_widget.h"

namespace sk {

// Fila de fondo (cajita) del panel Properties: rectangulo en coordenadas
// de contenido (sin el desplazamiento) y sombreado. shade: 0 = base de
// la cajita, 1 = fila alterna mas clara, 2 = cabecera de la cajita.
struct RowBand {
    RECT rc;
    int shade;
};

// Paneles laterales de la division PLACE: Properties (Nombre editable,
// fila Parent de solo lectura y Transform) a la izquierda y el Explorer
// (arbol del World) a la derecha, sobre la vista 3D. Son dos ventanas
// hijas de la principal (con WS_CLIPCHILDREN para que el present no las
// tape).
//
// El Explorer muestra el arbol del World: el grupo "Dimensions" con cada
// dimension y sus objetos y el grupo "Scripts" con el arbol global de
// scripts. Los nodos de escena guardan su InstanceId en lParam (>= 1);
// los grupos sinteticos y las entradas de disco usan 0. Properties lee
// las propiedades de la Instance seleccionada por id.
class PlaceView {
public:
    static constexpr int kExplorerWidth = 260;
    static constexpr int kPropertiesWidth = 300;

    bool create(void* parentHwnd);
    void destroy();

    // Rutas base que alimentan el grupo sintetico Assets del Explorer.
    void setScriptsRoot(const std::string& path) { scriptsRoot_ = path; }
    void setAssetsRoot(const std::string& path) { assetsRoot_ = path; }

    // World que alimenta el Explorer y las Properties. No se posee. Al
    // fijarlo, el Explorer se suscribe a los eventos del World (alta,
    // baja y reubicacion) para refrescar solo lo que cambia.
    void setWorld(World* world);

    // Panel Output (pestana izquierda): agrega una linea con el color
    // segun el nivel (0 info, 1 aviso, 2 error). El dueno engancha aqui
    // el sink de SK_INFO/WARN/ERROR.
    void appendOutput(int level, const std::string& message);

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // width/height son los de la ventana principal; los paneles ocupan
    // desde WorkspaceTabs::kTopBandHeight hasta el borde inferior.
    void resize(int width, int height);

    // Reconstruye todo el arbol a partir del World y su jerarquia. Util
    // tras cargar, insertar, deshacer/rehacer o re-agrupar. Deja el caret
    // en el ultimo objeto.
    void rebuildTree(const World& world);
    void clearObjects();

    // Properties: refresca el panel con la instancia, la raiz o el estado
    // "Sin objeto seleccionado". showObject refresca valores y, salvo que
    // relayout sea false, recoloca los controles (el dueno lo pasa false
    // mientras arrastra un objeto/gizmo para no rehacer el panel cada
    // frame, que es lo que hace que parpadee).
    void showObject(const Instance& instance, bool relayout = true);
    void showRoot();
    void showNoSelection();

    // Seleccion multiple proveniente del viewport: mueve el caret del
    // arbol al objeto "primary" sin disparar onSelectionChanged.
    void syncTreeSelection(InstanceId id);

    // Properties con varios objetos seleccionados.
    void showMultiple(int count);

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

    // Campo Transform (ids 200..208) editado.
    void setOnTransformEdited(
        std::function<void(InstanceId, int id, float value)> cb) {
        onTransformEdited_ = std::move(cb);
    }

    // Campo Nombre (id 100) editado: si devuelve false se restaura el
    // texto anterior.
    void setOnNameEdited(
        std::function<bool(InstanceId id, const std::string& newName)> cb) {
        onNameEdited_ = std::move(cb);
    }

    // Checkbox de Appearance/Data/Collision (ids 400..403).
    void setOnBoolEdited(
        std::function<void(InstanceId, int id, bool value)> cb) {
        onBoolEdited_ = std::move(cb);
    }

    // Campo float de Appearance (ids 410..411): Reflectance/Transparency.
    void setOnFloatEdited(
        std::function<void(InstanceId, int id, float value)> cb) {
        onFloatEdited_ = std::move(cb);
    }

    // Campo Pivot (ids 500..505): Position/Orientation del pivote.
    void setOnPivotEdited(
        std::function<void(InstanceId, int id, float value)> cb) {
        onPivotEdited_ = std::move(cb);
    }

    // Seccion Attributes (id 700): texto multilinea "nombre | tipo | valor".
    void setOnAttributesEdited(
        std::function<void(InstanceId, const std::string&)> cb) {
        onAttributesEdited_ = std::move(cb);
    }

    // Seccion Tags (id 701): una etiqueta por linea.
    void setOnTagsEdited(
        std::function<void(InstanceId, const std::string&)> cb) {
        onTagsEdited_ = std::move(cb);
    }

    // Boton "+" del Explorer: insertar una clase bajo el nodo (parent).
    void setOnInsertClass(
        std::function<void(InstanceId parent, const std::string& className)>
            cb) {
        onInsertClass_ = std::move(cb);
    }

    // Renombra el nodo del Explorer (id -> newName) sin disparar
    // onSelectionChanged: el caret sigue en el mismo nodo.
    void renameTreeItem(InstanceId id, const std::string& newName);

    // Quita el nodo del Explorer (por id) sin disparar
    // onSelectionChanged: el dueno ya gestiona la seleccion.
    void removeTreeItem(InstanceId id);

    // Estado de un checkbox de bool (ids 400..403): cambia el valor y
    // repinta la casilla solo si cambio.
    void setCheck(int id, bool on);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);
    void layoutPanels(int width, int height);
    void layoutProperties();
    void updateSummaries();
    void notifySelection();
    void scrollBy(int dy);
    void switchLeftTab(int tab);

    void* parent_ = nullptr;
    void* explorer_ = nullptr;
    void* properties_ = nullptr;
    void* output_ = nullptr;      // EDIT multiline (pestana Output)
    int leftTab_ = 0;             // 0 Properties, 1 Output
    ui::TreeModel treeModel_;     // modelo puro del Explorer
    ui::TreeWidget treeWidget_;   // arbol dibujado a mano
    World* worldEvents_ = nullptr;  // World cuyos eventos escucha el Explorer
    const World* world_ = nullptr;
    std::string scriptsRoot_;  // carpeta scripts/ del proyecto
    std::string assetsRoot_;   // carpeta assets/ del proyecto (o raiz)
    bool visible_ = false;
    bool suppressNotify_ = false;  // syncTreeSelection mueve el caret a mano
    InstanceId selectedId_ = kInvalidId;  // objeto seleccionado (0 = ninguno)
    std::string selectedName_;   // ultimo nombre mostrado (para restaurar)
    // Ultimo texto mostrado en cada campo Transform.
    std::string fieldText_[9];
    bool catOpen_[7] = {true, true, true, true, true, true, true};
    bool rowOpen_[3] = {false, false, false};
    bool pivotRowOpen_[2] = {false, false};
    bool checkState_[4] = {};
    int triCat_[7][4] = {};
    int triRow_[3][4] = {};
    int triPivotRow_[2][4] = {};
    std::string summaryText_[3];
    std::string pivotSummaryText_[2];
    bool showCategories_ = true;
    std::string floatText_[2];
    std::string pivotText_[6];
    std::vector<RowBand> bands_;
    std::vector<RECT> boxes_;
    int scrollY_ = 0;
    int scrollContent_ = 0;
    std::function<void(InstanceId)> onSelectionChanged_;
    std::function<void(InstanceId, InstanceId, int)> onReparent_;
    std::function<void(InstanceId, int, float)> onTransformEdited_;
    std::function<bool(InstanceId, const std::string&)> onNameEdited_;
    std::function<void(InstanceId, int, bool)> onBoolEdited_;
    std::function<void(InstanceId, int, float)> onFloatEdited_;
    std::function<void(InstanceId, int, float)> onPivotEdited_;
    std::function<void(InstanceId, const std::string&)> onAttributesEdited_;
    std::function<void(InstanceId, const std::string&)> onTagsEdited_;
    std::function<void(InstanceId, const std::string&)> onInsertClass_;
    std::function<void(const std::vector<InstanceId>&)> onDelete_;

    // Abre el selector de clase del boton "+" para el nodo indicado,
    // anclado a un punto de pantalla.
    void openClassPicker(InstanceId parentId, POINT screenPoint);
};

} // namespace sk
