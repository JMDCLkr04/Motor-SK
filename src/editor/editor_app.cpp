#include "editor_app.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "editor_commands.h"
#include "layout.h"

#include "../core/log.h"
#include "../project/scripts.h"
#include "../scene/picking.h"
#include "../scene/shape.h"
#include "../ui/kit/demo_window.h"
#include "../ui/kit/icon_atlas.h"

namespace sk::editor {
namespace {

int parseFramesArg(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0) {
            return std::atoi(argv[i + 1]);
        }
    }
    return -1;
}

// --proyecto <ruta>: abre un proyecto directamente (para pruebas).
std::string parseProjectArg(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--proyecto") == 0) {
            return argv[i + 1];
        }
    }
    return "";
}

// --play: abre la ventana de Play/Run al arrancar (pruebas).
bool parsePlayArg(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--play") == 0) return true;
    }
    return false;
}

// --stats: imprime cada 5 s los frames dibujados y el tiempo medio.
bool parseStatsArg(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--stats") == 0) return true;
    }
    return false;
}

// --debug: emite los registros de diagnostico del bucle (posiciones en
// pantalla, puntas del gizmo, snap, ...).
bool parseDebugArg(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0) return true;
    }
    return false;
}

// Nombres de las herramientas de la banda (indices 0-3), para los
// registros de cambio y de arrastre del gizmo.
const char* kToolNames[] = {"seleccionar", "mover", "escalar", "rotar"};

} // namespace

double nowSeconds() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) /
           static_cast<double>(frequency.QuadPart);
}

const char* toolName(int tool) { return kToolNames[tool]; }

int EditorApp::run(int argc, char** argv) {
    // DPI per-monitor v2: sin esto Windows estira la ventana cuando se
    // cambia la escala del escritorio y el borde derecho queda con
    // artefactos. Hay que declararlo antes de crear cualquier ventana.
    if (!SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }

    maxFrames_ = parseFramesArg(argc, argv);
    startupProject_ = parseProjectArg(argc, argv);
    startupPlay_ = parsePlayArg(argc, argv);
    statsEnabled_ = parseStatsArg(argc, argv);
    sk::debugLogs() = parseDebugArg(argc, argv);

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ui-demo") == 0) {
            sk::ui::runUIDemo();
            return 0;
        }
        if (std::strcmp(argv[i], "--gdi-check") == 0) {
            return sk::ui::runGdiCheck() ? 0 : 3;
        }
    }

    // Medal y OBS registran capas implicitas de Vulkan cuyos hooks
    // interceptan la creacion de swapchain y corrompen el heap de este
    // proceso (confirmado con AddressSanitizer). Cada capa declara una
    // variable de deshabilitacion que se lee al crear la instancia.
    _putenv_s("DISABLE_VULKAN_MEDAL_OBS_CAPTURE", "1");
    _putenv_s("DISABLE_VULKAN_OBS_CAPTURE", "1");
    SK_INFO("Capas de captura de Medal/OBS desactivadas");

    sk::ui::initIconAtlas();

    if (!initialize()) {
        shutdown();
        return 1;
    }

    int exitCode = 0;
    while (window_.pumpEvents() &&
           (maxFrames_ < 0 || frameCount_ < maxFrames_)) {
        if (!frame(window_.input())) {
            exitCode = 1;
            break;
        }
    }

    writeSceneNow(); // volcar cualquier cambio pendiente al salir
    const double elapsed = nowSeconds() - startTime_;
    SK_INFO("saliendo tras %d frames (%.1fs)", frameCount_, elapsed);
    shutdown();
    return exitCode;
}

bool EditorApp::initialize() {
    if (!window_.create(1280, 720, "Motor SK")) {
        return false;
    }

    if (!renderer_.init(window_)) {
        SK_ERROR("No se pudo inicializar el renderer");
        MessageBoxA(static_cast<HWND>(window_.nativeHandle()),
                    "No se pudo inicializar Vulkan.\n"
                    "Comprueba que hay un driver o ICD de Vulkan instalado.",
                    "Motor SK", MB_OK | MB_ICONERROR);
        return false;
    }

    // La carpeta raiz de proyectos se crea sola en el primer arranque.
    sk::project::ensureRootFolder();

    const bool created =
        panel_.create(window_.nativeHandle(), &config_,
                      [this](const std::string& folder) {
                          openProject(folder);
                      }) &&
        workspace_.create(window_.nativeHandle()) &&
        place_.create(window_.nativeHandle()) &&
        code_.create(window_.nativeHandle()) &&
        docs_.create(window_.nativeHandle());
    if (!created) {
        return false;
    }
    place_.setWorld(&world_);
    docs_.setVisible(false);

    wireUi();

    if (!startupProject_.empty()) {
        openProject(startupProject_); // arranque en modo vista 3D (pruebas)
    }

    const double fpsTarget = renderer_.softwareDevice() ? 30.0 : 60.0;
    frameInterval_ = 1.0 / fpsTarget;
    startTime_ = nowSeconds();
    lastTime_ = startTime_;
    statsWindowStart_ = startTime_;

    // Pruebas: si se pidio --play, abre la ventana de ejecucion ya.
    if (startupPlay_ && !active_.folder.empty()) {
        play_.open(world_, camera_, true);
    }
    return true;
}

void EditorApp::shutdown() {
    code_.destroy();
    docs_.destroy();
    place_.destroy();
    workspace_.destroy();
    panel_.destroy();
    renderer_.shutdown();
    window_.destroy();
    sk::ui::shutdownIconAtlas();
}

// --- Proyecto y escena -------------------------------------------------

std::string EditorApp::scenePath(const Project& p) const {
    return p.folder + "/" + p.scene;
}

void EditorApp::saveScene() {
    // Guardado diferido: marca la escena como sucia y pide redibujado. El
    // bucle escribe en disco 500 ms despues del ultimo cambio (o al cerrar
    // el proyecto / salir / menu Guardar), sin escribir a mitad de un
    // arrastre.
    sceneDirty_ = true;
    lastSceneChange_ = nowSeconds();
    needsRedraw_ = true;
    ++contentVersion_;
}

void EditorApp::writeSceneNow() {
    if (active_.folder.empty()) {
        sceneDirty_ = false;
        return;
    }
    const std::string path = scenePath(active_);
    if (!world_.saveToFile(path)) {
        SK_ERROR("No se pudo guardar la escena: %s", path.c_str());
    }
    sceneDirty_ = false;
}

void EditorApp::openProject(const std::string& folder) {
    Project p;
    if (!sk::project::load(folder, p)) {
        SK_ERROR("No se pudo abrir el proyecto: %s", folder.c_str());
        return;
    }
    config_.addRecent(folder);
    config_.save();
    active_ = std::move(p);
    world_.clear();
    world_.loadFromFile(scenePath(active_)); // migra v1 -> v2 si hace falta
    world_.ensureStructure();                // garantiza la estructura base
    play_.close();
    undo_.clear(); // cada proyecto parte con historia nueva
    // Raices de los grupos sinteticos del Explorer: scripts del proyecto
    // y biblioteca de assets.
    place_.setScriptsRoot(sk::scripts::rootFolder(active_));
    place_.setAssetsRoot(active_.folder + "/assets");
    place_.setWorld(&world_);
    place_.rebuildTree(world_); // Explorer con la jerarquia
    view3d_ = true;
    panel_.setVisible(false);
    workspace_.setVisible(true);
    const int tab = workspace_.active();
    docs_.setVisible(false);
    workspace_.setDocsActive(false);
    place_.setVisible(tab == 0);
    code_.setVisible(tab == 1);
    code_.open(active_);
    SetWindowTextA(static_cast<HWND>(window_.nativeHandle()),
                   ("Motor SK - " + active_.name).c_str());
    logProjection_ = true;
    needsRedraw_ = true;
    ++contentVersion_; // reconstruir caches para el proyecto abierto
    SK_INFO("Proyecto abierto: %s", active_.name.c_str());
}

void EditorApp::closeProject() {
    writeSceneNow(); // guardado inmediato: se abandona el proyecto
    play_.close();
    view3d_ = false;
    selection_.clear();
    primary_ = kInvalidId;
    needsRedraw_ = true;
    panel_.setVisible(true);
    panel_.refresh();
    workspace_.setVisible(false);
    place_.setVisible(false);
    code_.setVisible(false);
    docs_.setVisible(false);
    SetWindowTextA(static_cast<HWND>(window_.nativeHandle()), "Motor SK");
    SK_INFO("Proyecto cerrado");
}

// --- World / seleccion -------------------------------------------------

std::string EditorApp::worldName(InstanceId id) const {
    const Instance* o = world_.find(id);
    return o ? o->name() : std::string();
}

InstanceId EditorApp::defaultParent() const {
    const std::vector<InstanceId> dims = world_.dimensions();
    if (dims.empty()) return kInvalidId;
    return world_.findChild(dims.front(), kWorkspaceName);
}

Vec3 EditorApp::nextPartPos() const {
    const int n = static_cast<int>(world_.renderParts().size());
    return {static_cast<float>(n % 4) * 1.6f - 2.4f, 0.5f,
            static_cast<float>(n / 4) * 1.6f};
}

std::vector<InstanceId> EditorApp::allInstances() const {
    std::vector<InstanceId> out;
    for (InstanceId r : world_.roots()) {
        for (InstanceId s : world_.subtree(r)) out.push_back(s);
    }
    return out;
}

std::vector<InstanceId> EditorApp::topLevelSelection() const {
    std::vector<InstanceId> out;
    std::unordered_set<InstanceId> sel(selection_.begin(), selection_.end());
    for (InstanceId id : selection_) {
        if (!world_.exists(id) || world_.isProtected(id)) continue;
        bool covered = false;
        InstanceId p = world_.find(id)->parentId();
        while (p != kInvalidId) {
            if (sel.count(p)) {
                covered = true;
                break;
            }
            const Instance* pi = world_.find(p);
            p = pi ? pi->parentId() : kInvalidId;
        }
        if (!covered) out.push_back(id);
    }
    return out;
}

void EditorApp::applySelection(std::vector<InstanceId> ids,
                               InstanceId newPrimary) {
    selection_ = std::move(ids);
    primary_ = newPrimary;
    needsRedraw_ = true;
    ++contentVersion_; // el contorno de seleccion cambia
    place_.syncTreeSelection(primary_);
    // El panel recibe la lista completa y decide filas comunes / "mixto".
    place_.showSelection(selection_);
}

bool EditorApp::inViewport(const Vec2& p) const {
    if (!view3d_ || !place_.visible()) return false;
    const Layout layout = Layout::compute(window_.framebufferWidth(),
                                          window_.framebufferHeight());
    return layout.inViewport(static_cast<int>(p.x), static_cast<int>(p.y));
}

// --- Comandos ----------------------------------------------------------

void EditorApp::performDelete(const std::vector<InstanceId>& tops) {
    const int count = EditorCommands::remove(world_, undo_, tops);
    if (count == 0) return;
    applySelection({}, kInvalidId);
    place_.rebuildTree(world_);
    saveScene();
    SK_INFO("Eliminados %d objetos", count);
}

bool EditorApp::doRename(InstanceId id, const std::string& newName) {
    const Instance* before = world_.find(id);
    const std::string oldName = before ? before->name() : std::string();
    std::string finalName;
    if (!EditorCommands::rename(world_, undo_, id, newName, finalName)) {
        return false;
    }
    place_.renameTreeItem(id, finalName);
    saveScene();
    if (const Instance* renamed = world_.find(id)) {
        place_.showObject(*renamed); // campo al dia
    }
    code_.refreshProperties(); // "Asociado a" en CODE
    SK_INFO("Part renombrado: %s -> %s", oldName.c_str(), finalName.c_str());
    return true;
}

void EditorApp::doUndo() {
    if (!undo_.canUndo()) return;
    undo_.undo(world_);
    std::vector<InstanceId> ids;
    for (InstanceId id : selection_) {
        if (world_.exists(id)) ids.push_back(id);
    }
    const InstanceId newPrimary =
        world_.exists(primary_) ? primary_
                                : (ids.empty() ? kInvalidId : ids.front());
    place_.rebuildTree(world_);
    applySelection(std::move(ids), newPrimary);
    saveScene();
    logProjection_ = true;
    SK_INFO("undo");
}

void EditorApp::doRedo() {
    if (!undo_.canRedo()) return;
    undo_.redo(world_);
    std::vector<InstanceId> ids;
    for (InstanceId id : selection_) {
        if (world_.exists(id)) ids.push_back(id);
    }
    const InstanceId newPrimary =
        world_.exists(primary_) ? primary_
                                : (ids.empty() ? kInvalidId : ids.front());
    place_.rebuildTree(world_);
    applySelection(std::move(ids), newPrimary);
    saveScene();
    logProjection_ = true;
    SK_INFO("redo");
}

} // namespace sk::editor
