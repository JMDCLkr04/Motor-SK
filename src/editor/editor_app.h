#pragma once

#include <string>
#include <vector>

#include "clipboard_controller.h"
#include "gizmo_controller.h"
#include "object_drag_controller.h"
#include "selection_controller.h"
#include "shortcut_map.h"

#include "../core/config.h"
#include "../math/math.h"
#include "../platform/input_state.h"
#include "../platform/window.h"
#include "../project/project.h"
#include "../render/renderer.h"
#include "../scene/camera.h"
#include "../scene/commands.h"
#include "../scene/instance.h"
#include "../scene/world.h"
#include "../ui/code_view.h"
#include "../ui/docs_view.h"
#include "../ui/place_view.h"
#include "../ui/play_window.h"
#include "../ui/projects_panel.h"
#include "../ui/workspace_tabs.h"

namespace sk::editor {

// Reloj monotono en segundos (QPC). Compartido por los ficheros del app.
double nowSeconds();

// Nombre de la herramienta de la banda (indices 0-3), para los registros.
const char* toolName(int tool);

// Aplicacion del editor: es el unico dueno del estado (ventana, renderer,
// World, seleccion, camara, historial, paneles y controladores) y del
// bucle principal. main() solo la construye y llama a run().
class EditorApp {
public:
    EditorApp() = default;
    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    // Procesa los argumentos, crea ventana/renderer, monta la UI y corre el
    // bucle. Devuelve el codigo de salida del proceso.
    int run(int argc, char** argv);

private:
    // Datos derivados del frame que comparten los manejadores (atajos,
    // interaccion y dibujo).
    struct FrameCtx {
        const InputState* in = nullptr;
        int vpW = 0;
        int vpH = 0;
        float aspect = 1.0f;
        Mat4 viewProj{};
        bool gizmoReady = false;
        int tool = 0;
        Vec3 gizmoOrigin{};
        float gizmoScaleMax = 1.0f;
        Vec3 gizmoViewDir{};
    };

    // --- Ciclo de vida ---
    bool initialize();
    void shutdown();
    void wireUi();
    // Un frame completo. Devuelve false solo si el render falla.
    bool frame(const InputState& in);
    // Atajos de teclado que dependen del encuadre (portapapeles, rotacion,
    // undo/redo, renombrar, foco, F1 y bloqueo/anclaje).
    void frameShortcuts(const FrameCtx& ctx);
    // Puntero: hover/press del gizmo, arrastre de objeto/gizmo, cursor,
    // umbral y suelta (click/Ctrl+click/marquee).
    void frameInteraction(const FrameCtx& ctx);
    // Devuelve false si drawFrame falla.
    bool frameDraw(const FrameCtx& ctx);

    // --- Proyecto y escena ---
    std::string scenePath(const Project& p) const;
    void saveScene();
    void writeSceneNow();
    void openProject(const std::string& folder);
    void closeProject();

    // --- World / seleccion ---
    std::string worldName(InstanceId id) const;
    InstanceId defaultParent() const;
    Vec3 nextPartPos() const;
    std::vector<InstanceId> allInstances() const;
    std::vector<InstanceId> topLevelSelection() const;
    void applySelection(std::vector<InstanceId> ids, InstanceId newPrimary);
    bool inViewport(const Vec2& p) const;

    // --- Comandos (delegan en EditorCommands) ---
    void performDelete(const std::vector<InstanceId>& tops);
    bool doRename(InstanceId id, const std::string& newName);
    void doUndo();
    void doRedo();

    // --- Callbacks de UI (los conecta wireUi) ---
    void onAddPart(int shape);
    void onFileCommand(int id);
    void onInsertClass(InstanceId parentId, const std::string& className);
    void onDeleteRequested(const std::vector<InstanceId>& ids);
    void onReparent(InstanceId child, InstanceId parent, int index);
    void onPropertyEdited(InstanceId id, const std::string& prop,
                          const PropertyValue& value);
    void onAttributeSet(InstanceId id, const Attribute& attr);
    void onAttributeRemoved(InstanceId id, const std::string& name);
    void onTagAdded(InstanceId id, const std::string& tag);
    void onTagRemoved(InstanceId id, const std::string& tag);

    // --- Estado ---
    Window window_;
    Renderer renderer_;
    Config config_;
    Project active_;
    bool view3d_ = false;

    World world_;
    UndoStack undo_;

    ProjectsPanel panel_;
    WorkspaceTabs workspace_;
    PlaceView place_;
    CodeView code_;
    DocsView docs_;
    ui::PlayWindow play_;
    Camera camera_;

    std::vector<InstanceId> selection_;
    InstanceId primary_ = kInvalidId;

    ClipboardController clipboard_;
    SelectionController selectionCtrl_;
    ObjectDragController objDrag_;
    GizmoController gizmo_;

    bool logProjection_ = false;
    bool needsRedraw_ = true;
    bool continuous_ = false;
    unsigned long long contentVersion_ = 0;
    unsigned long long builtVersion_ = ~0ull;
    std::vector<Mat4> modelCache_;
    std::vector<int> shapeCache_;
    std::vector<Mat4> outlineCache_;
    std::vector<LineVertex> lastGizmo_;
    int lastDrawnHover_ = -1;

    bool sceneDirty_ = false;
    double lastSceneChange_ = 0.0;

    bool statsEnabled_ = false;
    double frameInterval_ = 0.0;
    double lastDrawWall_ = 0.0;
    double statsWindowStart_ = 0.0;
    unsigned long long statsFrames_ = 0;
    double statsAccum_ = 0.0;

    int maxFrames_ = -1;
    std::string startupProject_;
    bool startupPlay_ = false;
    double startTime_ = 0.0;
    int frameCount_ = 0;
    double lastTime_ = 0.0;

    // Contabilidad de los registros de diagnostico.
    int lastHoverTool_ = -1;
    int lastHoverAxis_ = -1;
    int lastHoverTx_ = -1;
    int lastHoverTy_ = -1;
    InstanceId lastHoverPrimary_ = kInvalidId;
    bool cursorForced_ = false;
    int lastTipsTool_ = -1;
    InstanceId lastTipsPrimary_ = kInvalidId;
};

} // namespace sk::editor
