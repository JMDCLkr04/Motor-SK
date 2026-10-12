#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/config.h"
#include "core/log.h"
#include "math/math.h"
#include "platform/window.h"
#include "project/project.h"
#include "project/scripts.h"
#include "render/renderer.h"
#include "scene/camera.h"
#include "scene/commands.h"
#include "scene/gizmo.h"
#include "scene/instance.h"
#include "scene/picking.h"
#include "scene/shape.h"
#include "scene/snapping.h"
#include "scene/world.h"
#include "ui/code_view.h"
#include "ui/docs_view.h"
#include "ui/kit/demo_window.h"
#include "ui/kit/icon_atlas.h"
#include "ui/place_view.h"
#include "ui/play_window.h"
#include "ui/projects_panel.h"
#include "ui/prompt.h"
#include "ui/workspace_tabs.h"

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

double nowSeconds() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

// Nombres de las herramientas de la banda (indices 0-3), para los
// registros de cambio y de arrastre del gizmo.
const char* kToolNames[] = {"seleccionar", "mover", "escalar", "rotar"};

} // namespace

int main(int argc, char** argv) {
    // DPI per-monitor v2: sin esto Windows estira la ventana cuando se
    // cambia la escala del escritorio y el borde derecho queda con
    // artefactos. Hay que declararlo antes de crear cualquier ventana.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }

    const int maxFrames = parseFramesArg(argc, argv);
    const std::string startupProject = parseProjectArg(argc, argv);
    const bool startupPlay = parsePlayArg(argc, argv);
    const bool statsEnabled = parseStatsArg(argc, argv);

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ui-demo") == 0) {
            sk::ui::runUIDemo();
            return 0;
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

    sk::Window window;
    if (!window.create(1280, 720, "Motor SK")) {
        return 1;
    }

    sk::Renderer renderer;
    if (!renderer.init(window)) {
        SK_ERROR("No se pudo inicializar el renderer");
        MessageBoxA(static_cast<HWND>(window.nativeHandle()),
                    "No se pudo inicializar Vulkan.\n"
                    "Comprueba que hay un driver o ICD de Vulkan instalado.",
                    "Motor SK", MB_OK | MB_ICONERROR);
        return 1;
    }

    // La carpeta raiz de proyectos se crea sola en el primer arranque.
    sk::project::ensureRootFolder();

    sk::Config config;

    // modo menu: panel Win32 visible; modo vista3d: render + camara.
    bool view3d = false;
    sk::Project active;
    sk::World world; // arbol de objetos de la division PLACE

    sk::ProjectsPanel panel;
    sk::WorkspaceTabs workspace;
    sk::PlaceView place;
    sk::CodeView code;
    sk::DocsView docs;
    sk::ui::PlayWindow play;

    // Seleccion activa (ids de instancia). "primary" es el ultimo objeto
    // tocado: el que el Explorer marca con el caret.
    std::vector<sk::InstanceId> selection;
    sk::InstanceId primary = sk::kInvalidId;
    // Log de posiciones en pantalla: al cambiar la escena, para que las
    // pruebas sepan donde hacer click.
    bool logProjection = false;

    // --- Redibujado bajo demanda ---
    // Solo se llama a drawFrame cuando algo ha cambiado; en reposo el
    // bucle duerme en MsgWaitForMultipleObjects (0% CPU).
    bool needsRedraw = true;
    bool continuous = false; // interaccion sostenida (camara/drag/play)
    // Version del contenido (escena + seleccion + herramienta): models,
    // shapeIds y outlines (mas el marquee/gizmo) solo se reconstruyen
    // cuando cambia, no en cada frame.
    unsigned long long contentVersion = 0;
    unsigned long long builtVersion = ~0ull;
    std::vector<sk::Mat4> modelCache;
    std::vector<int> shapeCache;
    std::vector<sk::Mat4> outlineCache;
    std::vector<sk::LineVertex> lastGizmo;
    int lastDrawnHover = -1;

    // --- Guardado diferido ---
    // Las ediciones marcan la escena como sucia; el bucle escribe 500 ms
    // despues del ultimo cambio (o al cerrar/salir/Guardar). Nunca a
    // mitad de un arrastre.
    bool sceneDirty = false;
    double lastSceneChange = 0.0;

    // --- Limite de FPS (60; 30 si el dispositivo Vulkan es software) ---
    const double fpsTarget = renderer.softwareDevice() ? 30.0 : 60.0;
    const double frameInterval = 1.0 / fpsTarget;
    double lastDrawWall = 0.0;

    // --- --stats ---
    double statsWindowStart = nowSeconds();
    unsigned long long statsFrames = 0;
    double statsAccum = 0.0;

    // Ruta absoluta de la escena del proyecto (escenas/inicio.scene).
    auto scenePath = [](const sk::Project& p) {
        return p.folder + "/" + p.scene;
    };
    auto saveScene = [&]() {
        // Guardado diferido: marca la escena como sucia y pide redibujado.
        // El bucle escribe en disco 500 ms despues del ultimo cambio (o al
        // cerrar el proyecto / salir / menu Guardar), sin escribir a mitad
        // de un arrastre.
        sceneDirty = true;
        lastSceneChange = nowSeconds();
        needsRedraw = true;
        ++contentVersion;
    };
    auto writeSceneNow = [&]() {
        if (active.folder.empty()) {
            sceneDirty = false;
            return;
        }
        const std::string path = scenePath(active);
        if (!world.saveToFile(path)) {
            SK_ERROR("No se pudo guardar la escena: %s", path.c_str());
        }
        sceneDirty = false;
    };

    // Historia de deshacer/rehacer (Ctrl+Z / Ctrl+Y): comandos con deltas
    // sobre el World. push ejecuta; undo/redo aplican el delta inverso.
    sk::UndoStack undo;

    // Nombre actual de una instancia ("" si no existe).
    auto worldName = [&](sk::InstanceId id) -> std::string {
        const sk::Instance* o = world.find(id);
        return o ? o->name() : std::string();
    };

    // Workspace de la primera dimension (padre por defecto de lo nuevo).
    auto defaultParent = [&]() -> sk::InstanceId {
        const std::vector<sk::InstanceId> dims = world.dimensions();
        if (dims.empty()) return sk::kInvalidId;
        return world.findChild(dims.front(), sk::kWorkspaceName);
    };

    // Posicion de la proxima Part: rejilla 4 columnas, elevada 0.5.
    auto nextPartPos = [&]() -> sk::Vec3 {
        const int n = static_cast<int>(world.renderParts().size());
        return {static_cast<float>(n % 4) * 1.6f - 2.4f, 0.5f,
                static_cast<float>(n / 4) * 1.6f};
    };

    // Todos los ids del World (raices y descendientes).
    auto allInstances = [&]() {
        std::vector<sk::InstanceId> out;
        for (sk::InstanceId r : world.roots()) {
            for (sk::InstanceId s : world.subtree(r)) out.push_back(s);
        }
        return out;
    };

    // Copia (valor) de un subarbol, raiz primero.
    auto captureNodes = [&](sk::InstanceId id) {
        std::vector<sk::Instance> out;
        for (sk::InstanceId s : world.subtree(id)) {
            if (const sk::Instance* o = world.find(s)) out.push_back(*o);
        }
        return out;
    };

    // Ids seleccionados que no estan cubiertos por otro seleccionado
    // (evita borrar/copiar dos veces un subarbol). Excluye protegidos.
    auto topLevelSelection = [&]() {
        std::vector<sk::InstanceId> out;
        std::unordered_set<sk::InstanceId> sel(selection.begin(),
                                               selection.end());
        for (sk::InstanceId id : selection) {
            if (!world.exists(id) || world.isProtected(id)) continue;
            bool covered = false;
            sk::InstanceId p = world.find(id)->parentId();
            while (p != sk::kInvalidId) {
                if (sel.count(p)) {
                    covered = true;
                    break;
                }
                const sk::Instance* pi = world.find(p);
                p = pi ? pi->parentId() : sk::kInvalidId;
            }
            if (!covered) out.push_back(id);
        }
        return out;
    };

    auto rebuildExplorer = [&]() { place.rebuildTree(world); };

    // Pinta Properties + caret del Explorer tras cambiar la seleccion.
    auto applySelection = [&](std::vector<sk::InstanceId> ids,
                              sk::InstanceId newPrimary) {
        selection = std::move(ids);
        primary = newPrimary;
        needsRedraw = true;
        ++contentVersion; // el contorno de seleccion cambia
        place.syncTreeSelection(primary);
        if (selection.empty()) {
            place.showNoSelection();
        } else if (selection.size() == 1) {
            const sk::Instance* object = world.find(selection.front());
            if (object) {
                place.showObject(*object);
            } else {
                place.showNoSelection();
            }
        } else {
            place.showMultiple(static_cast<int>(selection.size()));
        }
    };

    // Renombrar una Part (campo Nombre de Properties o tecla F2): lo
    // aplica al World, al Explorer y al resto de paneles. Devuelve false
    // y no toca nada si el cambio no es valido (vacio, igual, duplicado,
    // protegido o inexistente).
    auto doRename = [&](sk::InstanceId id, const std::string& newName) -> bool {
        const sk::Instance* object = world.find(id);
        if (!object || object->isProtected()) return false;
        const std::string oldName = object->name();
        if (newName.empty() || oldName == newName) return false;
        if (world.findChild(object->parentId(), newName) != sk::kInvalidId) {
            return false;
        }
        undo.push(world, sk::makeRenameCommand(world, id, newName));
        const sk::Instance* renamed = world.find(id);
        const std::string finalName = renamed ? renamed->name() : newName;
        place.renameTreeItem(id, finalName);
        saveScene();
        if (renamed) place.showObject(*renamed); // campo al dia
        code.refreshProperties(); // "Asociado a" en CODE
        SK_INFO("Part renombrado: %s -> %s", oldName.c_str(),
                finalName.c_str());
        return true;
    };

    auto openProject = [&](const std::string& folder) {
        sk::Project p;
        if (!sk::project::load(folder, p)) {
            SK_ERROR("No se pudo abrir el proyecto: %s", folder.c_str());
            return;
        }
        config.addRecent(folder);
        config.save();
        active = std::move(p);
        world.clear();
        world.loadFromFile(scenePath(active)); // migra v1 -> v2 si hace falta
        world.ensureStructure();               // garantiza la estructura base
        play.close();
        undo.clear(); // cada proyecto parte con historia nueva
        // Raices de los grupos sinteticos del Explorer: scripts del
        // proyecto y biblioteca de assets.
        place.setScriptsRoot(sk::scripts::rootFolder(active));
        place.setAssetsRoot(active.folder + "/assets");
        place.setWorld(&world);
        place.rebuildTree(world); // Explorer con la jerarquia
        view3d = true;
        panel.setVisible(false);
        workspace.setVisible(true);
        const int tab = workspace.active();
        docs.setVisible(false);
        workspace.setDocsActive(false);
        place.setVisible(tab == 0);
        code.setVisible(tab == 1);
        code.open(active);
        SetWindowTextA(static_cast<HWND>(window.nativeHandle()),
                       ("Motor SK - " + active.name).c_str());
        logProjection = true;
        needsRedraw = true;
        ++contentVersion; // reconstruir caches para el proyecto abierto
        SK_INFO("Proyecto abierto: %s", active.name.c_str());
    };

    // Cierra el proyecto y vuelve al panel de proyectos (menu Archivo >
    // Cerrar, o Esc). Guarda la escena por si quedo alguna mutacion sin
    // persistir y limpia la seleccion para el siguiente proyecto.
    auto closeProject = [&]() {
        writeSceneNow(); // guardado inmediato: se abandona el proyecto
        play.close();
        view3d = false;
        selection.clear();
        primary = sk::kInvalidId;
        needsRedraw = true;
        panel.setVisible(true);
        panel.refresh();
        workspace.setVisible(false);
        place.setVisible(false);
        code.setVisible(false);
        docs.setVisible(false);
        SetWindowTextA(static_cast<HWND>(window.nativeHandle()), "Motor SK");
        SK_INFO("Proyecto cerrado");
    };

    if (!panel.create(window.nativeHandle(), &config, openProject)) {
        renderer.shutdown();
        window.destroy();
        return 1;
    }

    if (!workspace.create(window.nativeHandle())) {
        workspace.destroy();
        panel.destroy();
        renderer.shutdown();
        window.destroy();
        return 1;
    }

    if (!place.create(window.nativeHandle())) {
        place.destroy();
        workspace.destroy();
        panel.destroy();
        renderer.shutdown();
        window.destroy();
        return 1;
    }
    place.setWorld(&world);

    // El panel Output de PLACE recibe copia de todos los SK_INFO/WARN/ERROR.
    sk::setLogSink([&place](sk::LogLevel level, const std::string& message) {
        place.appendOutput(static_cast<int>(level), message);
    });

    if (!code.create(window.nativeHandle())) {
        code.destroy();
        place.destroy();
        workspace.destroy();
        panel.destroy();
        renderer.shutdown();
        window.destroy();
        return 1;
    }

    if (!docs.create(window.nativeHandle())) {
        docs.destroy();
        code.destroy();
        place.destroy();
        workspace.destroy();
        panel.destroy();
        renderer.shutdown();
        window.destroy();
        return 1;
    }
    docs.setVisible(false);

    // Solo la division PLACE muestra Explorer/Properties y solo CODE
    // muestra el organizador de scripts; GUI vendra despues.
    workspace.setOnTabChanged([&](int tab) {
        docs.setVisible(false);
        workspace.setDocsActive(false);
        place.setVisible(tab == 0);
        code.setVisible(tab == 1);
    });

    // Boton "libro" de la navbar: muestra la documentacion de clases en
    // lugar del viewport 3D (oculta PLACE/CODE).
    workspace.setOnDocs([&]() {
        place.setVisible(false);
        code.setVisible(false);
        docs.setVisible(true);
        workspace.setDocsActive(true);
    });

    // Atajo "Part": anade el objeto al World bajo el Workspace y refresca
    // el Explorer (insertar lo selecciona, lo que refresca Properties).
    workspace.setOnAddPart([&](int shape) {
        const sk::Shape s = (shape >= 0 && shape < static_cast<int>(sk::Shape::Count))
                                ? static_cast<sk::Shape>(shape)
                                : sk::Shape::Cube;
        const sk::InstanceId parent = defaultParent();
        if (parent == sk::kInvalidId) return;
        undo.push(world, sk::makeCreateCommand(world, "Part", parent,
                                               nextPartPos(),
                                               static_cast<int>(s)));
        sk::InstanceId created = sk::kInvalidId;
        if (const sk::Instance* p = world.find(parent)) {
            if (!p->children().empty()) created = p->children().back();
        }
        place.rebuildTree(world);
        if (created != sk::kInvalidId) applySelection({created}, created);
        saveScene();
        logProjection = true;
        SK_INFO("Part anadido: %s (%s)", worldName(created).c_str(),
                sk::shapeName(s));
    });

    // Menu "Archivo" de la barra de menus: cada comando decide aqui. Los
    // tres dialogos informativos (configuracion, atajos, autosaves) son
    // un placeholder hasta que se implementen sus subsistemas.
    workspace.setOnFileCommand([&](int id) {
        const HWND hwnd = static_cast<HWND>(window.nativeHandle());
        switch (id) {
        case sk::kFileClose:
            if (view3d) closeProject();
            break;
        case sk::kFileSave:
            if (view3d) {
                writeSceneNow();
                SK_INFO("Escena guardada");
            }
            break;
        case sk::kFileSaveAs: {
            if (!view3d || active.folder.empty()) break;
            writeSceneNow(); // la copia parte del estado actual en disco
            const std::string newName = sk::ui::promptText(
                hwnd, "Guardar como", active.name.c_str());
            if (newName.empty() || newName == active.name) break;
            std::string newFolder;
            if (!sk::project::saveAs(active.folder, newName, newFolder)) {
                MessageBoxA(hwnd,
                            "No se pudo guardar como: nombre invalido o ya "
                            "existe un proyecto con ese nombre.",
                            "Guardar como", MB_OK | MB_ICONWARNING);
            } else {
                openProject(newFolder);
            }
            break;
        }
        case sk::kFileImport:
        case sk::kFileImportAs: {
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
            if (id == sk::kFileImportAs) {
                prefix = sk::ui::promptText(hwnd, "Importar como", "");
                if (!prefix.empty()) prefix += " ";
            }

            sk::World incoming;
            if (!incoming.loadFromFile(ofn.lpstrFile)) {
                SK_ERROR("Importar: no se pudo leer %s", ofn.lpstrFile);
                break;
            }
            incoming.ensureStructure();
            const std::vector<sk::InstanceId> inDims = incoming.dimensions();
            const sk::InstanceId inWs =
                inDims.empty()
                    ? sk::kInvalidId
                    : incoming.findChild(inDims.front(), sk::kWorkspaceName);

            std::vector<sk::Instance> nodes;
            std::vector<sk::InstanceId> newIds;
            if (inWs != sk::kInvalidId) {
                const sk::Instance* wsInst = incoming.find(inWs);
                std::vector<sk::InstanceId> tops;
                if (wsInst) tops = wsInst->children();
                for (sk::InstanceId top : tops) {
                    for (sk::InstanceId sid : incoming.subtree(top)) {
                        if (const sk::Instance* o = incoming.find(sid)) {
                            nodes.push_back(*o);
                            newIds.push_back(world.allocateId());
                        }
                    }
                }
            }
            if (nodes.empty()) {
                SK_INFO("Importar: %s esta vacia", ofn.lpstrFile);
                break;
            }
            std::vector<sk::InstanceId> roots;
            undo.push(world, sk::makeInsertSubtreeCommand(
                                 world, nodes, newIds, defaultParent(),
                                 sk::Vec3{}, prefix, &roots));
            place.rebuildTree(world);
            if (!roots.empty()) applySelection(roots, roots.back());
            saveScene();
            logProjection = true;
            SK_INFO("Importados %d objeto(s) de %s",
                    static_cast<int>(nodes.size()), ofn.lpstrFile);
            break;
        }
        case sk::kFileEditorSettings:
            MessageBoxA(hwnd,
                        "La configuracion del editor todavia esta en "
                        "desarrollo.\n\n"
                        "Aqui se podran ajustar la camara, la rejilla y el "
                        "aspecto del editor.",
                        "Configuracion del editor", MB_OK | MB_ICONINFORMATION);
            break;
        case sk::kFileShortcuts:
            MessageBoxA(
                hwnd,
                "Atajos actuales (la personalizacion llegara en un proximo "
                "paso):\n\n"
                "Herramientas .......... 1, 2, 3, 4\n"
                "Camara ................ WASD + clic derecho, rueda, Q/E\n"
                "Foco en la seleccion .. F\n"
                "Renombrar parte ....... F2\n"
                "Referencia de clases .. F1\n"
                "Agrupar/Desagrupar .... Ctrl+G / Ctrl+U\n"
                "Bloquear (Lock) ....... Ctrl+L\n"
                "Anclar (Anchor) ....... Alt+A\n"
                "Deshacer/Rehacer ...... Ctrl+Z / Ctrl+Y\n"
                "Copiar/Cortar/Pegar ... Ctrl+C / Ctrl+X / Ctrl+V\n"
                "Clonar ................ Ctrl+D\n"
                "Rotar 90 (Y/X) ........ Ctrl+R / Ctrl+T\n"
                "Eliminar .............. Suprimir / Retroceso\n"
                "Volver al panel ....... Esc",
                "Personalizar atajos", MB_OK | MB_ICONINFORMATION);
            break;
        case sk::kFileAutosaves:
            MessageBoxA(hwnd,
                        "No hay guardados automaticos todavia.\n\n"
                        "El guardado automatico llegara en un proximo paso.",
                        "Guardados automaticos", MB_OK | MB_ICONINFORMATION);
            break;
        case sk::kFileExit:
            PostMessageA(static_cast<HWND>(window.nativeHandle()), WM_CLOSE, 0,
                         0);
            break;
        default:
            break;
        }
    });

    // Cambio de herramienta (botones de la banda o teclas 1-4).
    workspace.setOnToolChanged([&](int tool) {
        needsRedraw = true;
        ++contentVersion; // el gizmo (y su resaltado) cambia
        SK_INFO("herramienta: %s", kToolNames[tool]);
    });

    // Seleccion en el Explorer -> Properties. El estado de seleccion (multi)
    // vive aqui y el arbol lo refleja.
    place.setOnSelectionChanged([&](sk::InstanceId id) {
        if (id != sk::kInvalidId) {
            if (const sk::Instance* object = world.find(id)) {
                selection = {id};
                primary = id;
                place.showObject(*object);
                return;
            }
        }
        selection.clear();
        primary = sk::kInvalidId;
        place.showNoSelection();
    });

    // Properties Transform editable: al perder el foco un campo se
    // aplica al objeto, se guarda la escena y se reescriben los campos
    // con el formato normalizado.
    place.setOnTransformEdited([&](sk::InstanceId id, int fieldId,
                                   float value) {
        sk::Instance* object = world.find(id);
        if (!object) return;
        const int row = (fieldId - 200) / 3;
        const int axis = (fieldId - 200) % 3;
        static const char* kRows[] = {"position", "size", "orientation"};
        static const char* kAxes[] = {"x", "y", "z"};
        sk::Vec3 p = object->position();
        sk::Vec3 r = object->rotation();
        sk::Vec3 s = object->scale();
        sk::Vec3* target = &p;
        if (row == 1) target = &s;          // Size
        else if (row == 2) target = &r;     // Orientation
        if (std::fabs((&target->x)[axis] - value) < 1e-6f) {
            return; // sin cambio: no ensucia la historia ni guarda
        }
        (&target->x)[axis] = value;
        undo.push(world, sk::makeTransformCommand(world, id, p, r, s));
        saveScene();
        if (const sk::Instance* o = world.find(id)) place.showObject(*o);
        SK_INFO("Properties %s: %s.%s = %.3g", worldName(id).c_str(), kRows[row],
                kAxes[axis], value);
    });

    // Properties Nombre editable: al perder el foco o con Enter se
    // renombra el objeto en el World, en el Explorer y en los paneles.
    // Nombre duplicado/vacio lo rechaza y el panel restaura el anterior.
    place.setOnNameEdited([&](sk::InstanceId id, const std::string& newName) {
        return doRename(id, newName);
    });

    // Properties checkboxes (ids del panel: 400 CastShadow, 401 Locked,
    // 402 CanCollide, 403 Anchored): se aplican al objeto y se guarda.
    place.setOnBoolEdited([&](sk::InstanceId id, int fieldId, bool value) {
        sk::Instance* object = world.find(id);
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
        sk::PropertyValue pv;
        pv.b = value;
        undo.push(world, sk::makePropertyCommand(world, id, field, pv));
        saveScene();
        SK_INFO("Properties %s: %s = %s", worldName(id).c_str(), field,
                value ? "true" : "false");
    });

    // Properties Reflectance (410) / Transparency (411): el panel ya
    // reclama el valor a 0..1; aqui se aplica y se reescriben los campos.
    place.setOnFloatEdited([&](sk::InstanceId id, int fieldId, float value) {
        sk::Instance* object = world.find(id);
        if (!object) return;
        const char* field = nullptr;
        if (fieldId == 410) field = "Reflectance";
        else if (fieldId == 411) field = "Transparency";
        else return;
        if (std::fabs(object->propFloat(field) - value) < 1e-6f) {
            return; // sin cambio: no ensucia la historia ni guarda
        }
        sk::PropertyValue pv;
        pv.f = value;
        undo.push(world, sk::makePropertyCommand(world, id, field, pv));
        saveScene();
        if (const sk::Instance* o = world.find(id)) place.showObject(*o);
        SK_INFO("Properties %s: %s = %.2f", worldName(id).c_str(), field, value);
    });

    // Properties Pivot (500..502 Position, 503..505 Orientation): mueve
    // el origen de la part sin tocar su position en el mundo.
    place.setOnPivotEdited([&](sk::InstanceId id, int fieldId, float value) {
        sk::Instance* object = world.find(id);
        if (!object) return;
        const int idx = fieldId - 500;
        if (idx < 0 || idx >= 6) return;
        const char* field = idx < 3 ? "PivotPosition" : "PivotRotation";
        sk::Vec3 v = object->propVec3(field);
        if (std::fabs((&v.x)[idx % 3] - value) < 1e-6f) {
            return; // sin cambio: no ensucia la historia ni guarda
        }
        (&v.x)[idx % 3] = value;
        sk::PropertyValue pv;
        pv.v = v;
        undo.push(world, sk::makePropertyCommand(world, id, field, pv));
        saveScene();
        if (const sk::Instance* o = world.find(id)) place.showObject(*o);
        SK_INFO("Properties %s: pivot[%d] = %.3g", worldName(id).c_str(), idx,
                value);
    });

    // Attributes: parsea "nombre | tipo | valor" por linea al objeto.
    place.setOnAttributesEdited(
        [&](sk::InstanceId id, const std::string& text) {
            sk::Instance* object = world.find(id);
            if (!object) return;
            std::vector<sk::Attribute> parsed;
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
                sk::Attribute attr;
                size_t p1 = line.find('|');
                size_t p2 = p1 == std::string::npos
                                ? std::string::npos
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
            std::vector<sk::CommandPtr> parts;
            for (const sk::Attribute& old : object->getAttributes()) {
                bool still = false;
                for (const sk::Attribute& n : parsed) {
                    if (n.name == old.name) {
                        still = true;
                        break;
                    }
                }
                if (!still) {
                    parts.push_back(
                        sk::makeAttributeCommand(world, id, old, true));
                }
            }
            for (const sk::Attribute& n : parsed) {
                const sk::Attribute prev = object->getAttribute(n.name);
                const bool present = object->hasAttribute(n.name);
                if (!present || !(prev == n)) {
                    parts.push_back(
                        sk::makeAttributeCommand(world, id, n, false));
                }
            }
            if (parts.empty()) return;
            undo.push(world, sk::makeCompoundCommand(std::move(parts)));
            saveScene();
            SK_INFO("Attributes de %s actualizados", worldName(id).c_str());
        });

    // Tags: una etiqueta por linea.
    place.setOnTagsEdited([&](sk::InstanceId id, const std::string& text) {
        sk::Instance* object = world.find(id);
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
        std::vector<sk::CommandPtr> parts;
        for (const std::string& old : object->getTags()) {
            if (std::find(tags.begin(), tags.end(), old) == tags.end()) {
                parts.push_back(sk::makeTagCommand(world, id, old, false));
            }
        }
        for (const std::string& t : tags) {
            if (!object->hasTag(t)) {
                parts.push_back(sk::makeTagCommand(world, id, t, true));
            }
        }
        if (parts.empty()) return;
        undo.push(world, sk::makeCompoundCommand(std::move(parts)));
        saveScene();
        SK_INFO("Tags de %s actualizados", worldName(id).c_str());
    });

    // Boton "+" del Explorer: inserta una clase bajo el nodo indicado.
    place.setOnInsertClass([&](sk::InstanceId parentId,
                               const std::string& className) {
        if (parentId == sk::kInvalidId || !world.exists(parentId)) return;
        undo.push(world, sk::makeCreateCommand(world, className, parentId,
                                               nextPartPos()));
        sk::InstanceId created = sk::kInvalidId;
        if (const sk::Instance* p = world.find(parentId)) {
            if (!p->children().empty()) created = p->children().back();
        }
        saveScene();
        place.rebuildTree(world);
        if (created != sk::kInvalidId) {
            applySelection({created}, created);
        }
        SK_INFO("Insertado %s \"%s\" en %s", className.c_str(),
                worldName(created).c_str(), worldName(parentId).c_str());
    });

    // CODE: el panel Properties consulta el objeto asociado a un script.
    code.setOnQueryAssoc([&](const std::string& rel) -> std::string {
        for (sk::InstanceId id : allInstances()) {
            const sk::Instance* o = world.find(id);
            if (!o || !o->hasAttribute("Script")) continue;
            if (o->getAttribute("Script").value == rel) return o->name();
        }
        return "";
    });

    // Borrar un script (o una carpeta con sus hijos) limpia las
    // asociaciones de la escena que apuntaban a esa rel.
    code.setOnScriptRemoved([&](const std::string& rel) {
        bool changed = false;
        for (sk::InstanceId id : allInstances()) {
            sk::Instance* o = world.find(id);
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
    code.setOnScriptRenamed([&](const std::string& oldRel,
                                 const std::string& newRel) {
        bool changed = false;
        for (sk::InstanceId id : allInstances()) {
            sk::Instance* o = world.find(id);
            if (!o || !o->hasAttribute("Script")) continue;
            const std::string value = o->getAttribute("Script").value;
            if (value == oldRel) {
                sk::Attribute a = o->getAttribute("Script");
                a.value = newRel;
                o->setAttribute(a);
                changed = true;
            } else if (value.rfind(oldRel + "/", 0) == 0) {
                sk::Attribute a = o->getAttribute("Script");
                a.value = newRel + value.substr(oldRel.size());
                o->setAttribute(a);
                changed = true;
            }
        }
        if (changed) saveScene();
    });

    if (!startupProject.empty()) {
        // Arranque en modo vista 3D (pruebas/atajos).
        openProject(startupProject);
    }

    sk::Camera camera;

    // Botones Play/Run/Stop de la banda: Play y Run abren una ventana
    // aparte con su propio renderer (Run ademas ejecutaria los scripts);
    // Stop la cierra. Requieren un proyecto abierto.
    workspace.setOnPlay([&]() {
        if (active.folder.empty()) return;
        play.open(world, camera, false);
    });
    workspace.setOnRun([&]() {
        if (active.folder.empty()) return;
        play.open(world, camera, true);
    });
    workspace.setOnStop([&]() { play.close(); });

    // Pruebas: si se pidio --play, abre la ventana de ejecucion ya.
    if (startupPlay && !active.folder.empty()) {
        play.open(world, camera, true);
    }

    const double startTime = nowSeconds();
    double lastTime = startTime;
    int frame = 0;
    bool escWasDown = false;
    bool camKeysWasDown = false;
    int toolKeyWasDown = 0;
    bool deleteKeyWasDown = false;
    // Portapapeles interno de subarboles (Ctrl+C/X) y estado de flancos de
    // los atajos de copia/pega/clonado/rotacion.
    std::vector<std::vector<sk::Instance>> clipboard;
    bool clipboardFromCut = false;
    bool copyKeyWasDown = false;
    bool pasteKeyWasDown = false;
    bool cutKeyWasDown = false;
    bool dupKeyWasDown = false;
    bool rotYKeyWasDown = false;
    bool rotXKeyWasDown = false;
    bool undoKeyWasDown = false;
    bool redoKeyWasDown = false;
    bool f2KeyWasDown = false;
    bool fKeyWasDown = false;
    bool f1KeyWasDown = false;
    bool groupKeyWasDown = false;
    bool ungroupKeyWasDown = false;
    bool lockKeyWasDown = false;
    bool anchorKeyWasDown = false;
    auto edge = [](bool now, bool& was) {
        const bool rising = now && !was;
        was = now;
        return rising;
    };

    // Arrastre de seleccion (marquee) en el viewport de PLACE.
    bool dragPending = false;  // pressed dentro del viewport, sin mover
    bool dragActive = false;   // supero el umbral: es un arrastre
    sk::Vec2 dragStart{};

    // Gizmo de herramientas: arrastre activo + eje bajo el puntero.
    sk::GizmoDrag gizmoDrag;

    // Arrastre de objeto: mantener pulsado el cuerpo de una parte (sin
    // tocar un asa del gizmo) y arrastrarla con el puntero. La caja del
    // grupo se apoya en la cara del otro parte bajo el puntero (o en el
    // suelo Y=0 si no hay nada): ver scene/snapping.h.
    struct ObjDragStart {
        sk::InstanceId id = sk::kInvalidId;
        sk::Vec3 pos;
        sk::Vec3 rot;
    };
    bool objDragActive = false;
    std::vector<ObjDragStart> objDragStart;
    // Punto de agarre menos centro del lead al empezar (mundo): fija la
    // posicion del grupo respecto al puntero durante el arrastre.
    sk::Vec3 grabOffset{};
    sk::InstanceId pressHit = sk::kInvalidId; // objeto bajo el punto de presion
    bool pressCtrl = false;
    // Cara bajo el puntero durante el arrastre: se registra una sola vez
    // por destino para que las pruebas vean donde se apoya la parte sin
    // repetir el log en cada frame.
    bool snapLogged = false;
    sk::InstanceId snapSurface = sk::kInvalidId;
    sk::Vec3 snapNormal{};
    int hoverAxis = -1;
    // Registro "gizmo eje X punta (x,y)": una sola vez por punta, para
    // que las pruebas sepan donde pulsar sin repetir el log en cada frame.
    int lastHoverTool = -1;
    int lastHoverAxis = -1;
    int lastHoverTx = -1;
    int lastHoverTy = -1;
    sk::InstanceId lastHoverPrimary = sk::kInvalidId;
    bool cursorForced = false;
    // Puntas de los ejes logeadas al cambiar de herramienta o de
    // seleccion (y tras cada arrastre): las pruebas leen de aqui donde
    // poner el puntero sin tener que barrer el viewport.
    int lastTipsTool = -1;
    sk::InstanceId lastTipsPrimary = sk::kInvalidId;
    auto inViewport = [&](const sk::Vec2& p) {
        if (!view3d || !place.visible()) return false;
        const float x0 = static_cast<float>(sk::PlaceView::kPropertiesWidth);
        const float x1 = static_cast<float>(window.framebufferWidth()) -
                         static_cast<float>(sk::PlaceView::kExplorerWidth);
        const float y0 = static_cast<float>(sk::WorkspaceTabs::kTopBandHeight);
        return p.x >= x0 && p.x <= x1 && p.y >= y0 &&
               p.y <= static_cast<float>(window.framebufferHeight());
    };

    // Re-agrupar desde el Explorer: suelta X sobre Y y Y queda dentro de X
    // (o el grupo Dimensions = la dimension raiz). Valida que el padre
    // exista, que no sea el propio hijo y que no se formen ciclos.
    place.setOnReparent([&](sk::InstanceId child, sk::InstanceId parent) {
        if (!view3d || child == sk::kInvalidId) return;
        const sk::Instance* object = world.find(child);
        if (!object) return;
        if (parent == sk::kInvalidId) {
            const std::vector<sk::InstanceId> dims = world.dimensions();
            if (dims.empty()) return;
            parent = dims.front();
        }
        if (child == parent) return;
        if (!world.exists(parent)) return;
        if (object->parentId() == parent) return;
        // Ciclo: el nuevo padre no puede ser descendiente del hijo.
        for (sk::InstanceId s : world.subtree(child)) {
            if (s == parent) return;
        }
        undo.push(world, sk::makeReparentCommand(world, child, parent));
        saveScene();
        place.rebuildTree(world);
        applySelection({child}, child);
        logProjection = true;
        SK_INFO("Explorer: %s cuelga de %s", worldName(child).c_str(),
                worldName(parent).c_str());
    });

    // Deshacer/rehacer: aplica el comando, poda la seleccion a los ids que
    // siguen existiendo y refresca el Explorer y los paneles.
    auto doUndo = [&]() {
        if (!undo.canUndo()) return;
        undo.undo(world);
        std::vector<sk::InstanceId> ids;
        for (sk::InstanceId id : selection) {
            if (world.exists(id)) ids.push_back(id);
        }
        sk::InstanceId newPrimary =
            world.exists(primary) ? primary
                                  : (ids.empty() ? sk::kInvalidId : ids.front());
        place.rebuildTree(world);
        applySelection(std::move(ids), newPrimary);
        saveScene();
        logProjection = true;
        SK_INFO("undo");
    };

    auto doRedo = [&]() {
        if (!undo.canRedo()) return;
        undo.redo(world);
        std::vector<sk::InstanceId> ids;
        for (sk::InstanceId id : selection) {
            if (world.exists(id)) ids.push_back(id);
        }
        sk::InstanceId newPrimary =
            world.exists(primary) ? primary
                                  : (ids.empty() ? sk::kInvalidId : ids.front());
        place.rebuildTree(world);
        applySelection(std::move(ids), newPrimary);
        saveScene();
        logProjection = true;
        SK_INFO("redo");
    };

    while (window.pumpEvents() && (maxFrames < 0 || frame < maxFrames)) {
        if (window.consumeResized()) {
            SK_INFO("resize: %dx%d", window.framebufferWidth(), window.framebufferHeight());
            renderer.invalidateSwapchain();
            panel.resize(window.framebufferWidth(), window.framebufferHeight());
            workspace.resize(window.framebufferWidth(), window.framebufferHeight());
            place.resize(window.framebufferWidth(), window.framebufferHeight());
            code.resize(window.framebufferWidth(), window.framebufferHeight());
            docs.resize(window.framebufferWidth(), window.framebufferHeight());
            needsRedraw = true;
        }
        // Restaurado/descubierto (WM_PAINT): repinta el viewport.
        if (window.consumeRepaintRequested()) {
            needsRedraw = true;
        }

        if (!view3d) {
            // Menu: solo se procesan mensajes (los controles Win32 pintan
            // encima; el renderer no se llama hasta abrir un proyecto).
            // En reposo el hilo duerme sin gastar CPU.
            ++frame;
            if (maxFrames >= 0) {
                Sleep(1); // modo test: no bloquear para llegar a --frames
            } else {
                window.waitEvents(INFINITE);
            }
            continue;
        }

        // Esc: cancela el arrastre del gizmo si esta en curso; si no, el
        // del objeto; si no, el marquee; si no, vuelve al panel de
        // proyectos. Solo con la ventana enfocada (keyDown usa
        // GetAsyncKeyState, que es global).
        const bool escDown = window.keyDown(VK_ESCAPE) && window.hasFocus();
        if (escDown && !escWasDown && gizmoDrag.active) {
            sk::gizmoRevert(gizmoDrag, world);
            gizmoDrag.active = false;
            lastTipsTool = -1; // las puntas vuelven a su sitio
            SK_INFO("gizmo cancelado");
            applySelection(selection, primary);
        } else if (escDown && !escWasDown && objDragActive) {
            // Revierte el arrastre del objeto a sus poses de inicio
            // (posicion y rotacion: el snap de superficie puede haber
            // girado el grupo).
            for (const ObjDragStart& start : objDragStart) {
                if (sk::Instance* object = world.find(start.id)) {
                    object->setPosition(start.pos);
                    object->setRotation(start.rot);
                    world.markChanged(start.id);
                }
            }
            objDragActive = false;
            objDragStart.clear();
            dragPending = false;
            SK_INFO("drag cancelado");
            applySelection(selection, primary);
        } else if (escDown && !escWasDown && (dragPending || dragActive)) {
            dragPending = false;
            dragActive = false;
            SK_INFO("marquee cancelado");
        } else if (escDown && !escWasDown) {
            closeProject();
            escWasDown = escDown;
            continue;
        }
        escWasDown = escDown;

        const double now = nowSeconds();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        if (dt > 0.1f) dt = 0.1f; // proteccion contra picos (ventana arrastrada)

        const sk::Mat4 viewBefore = camera.view();
        camera.update(dt, window);
        // Si la camara cambio (WASD, giro con clic derecho, rueda), repinta.
        {
            const float* a = viewBefore.m;
            const float* b = camera.view().m;
            for (int i = 0; i < 16; ++i) {
                if (std::fabs(a[i] - b[i]) > 1e-6f) {
                    needsRedraw = true;
                    break;
                }
            }
        }

        // Log al empezar a mover la camara (mismo criterio de foco que
        // Camera::update): las pruebas comprueban que WASD/Q/E no
        // funcionan con otra aplicacion en primer plano.
        const bool camKeys = window.hasFocus() && !window.textInputFocused() &&
                             (window.keyDown('W') || window.keyDown('A') ||
                              window.keyDown('S') || window.keyDown('D') ||
                              window.keyDown('Q') || window.keyDown('E'));
        if (camKeys && !camKeysWasDown) {
            const sk::Vec3 p = camera.position();
            SK_INFO("camara (%.2f,%.2f,%.2f)", p.x, p.y, p.z);
        }
        camKeysWasDown = camKeys;

        // Teclas 1-4: cambiar de herramienta (solo con la ventana
        // enfocada, en PLACE, sin escribir en un campo de texto y sin
        // un arrastre del gizmo en curso).
        const bool toolKeysOk = window.hasFocus() && !window.textInputFocused() &&
                                workspace.visible() && place.visible() &&
                                !gizmoDrag.active;
        int toolKeyNow = 0;
        if (toolKeysOk) {
            if (window.keyDown('1')) toolKeyNow = 1;
            else if (window.keyDown('2')) toolKeyNow = 2;
            else if (window.keyDown('3')) toolKeyNow = 3;
            else if (window.keyDown('4')) toolKeyNow = 4;
        }
        if (toolKeyNow != 0 && toolKeyNow != toolKeyWasDown) {
            workspace.setTool(toolKeyNow - 1);
        }
        toolKeyWasDown = toolKeyNow;

        // Suprimir / Retroceso: elimina la parte o todas las partes
        // seleccionadas (solo con la ventana enfocada, en PLACE, sin
        // escribir en un campo y sin un arrastre en curso).
        const bool delKeyDown =
            window.hasFocus() && !window.textInputFocused() &&
            workspace.visible() && place.visible() && !gizmoDrag.active &&
            !objDragActive &&
            (window.keyDown(VK_DELETE) || window.keyDown(VK_BACK));
        if (delKeyDown && !deleteKeyWasDown && !selection.empty()) {
            // Borrar un padre se lleva a sus descendientes detras.
            const std::vector<sk::InstanceId> tops = topLevelSelection();
            int count = 0;
            for (sk::InstanceId id : tops) {
                count += static_cast<int>(world.subtree(id).size());
            }
            if (!tops.empty()) {
                std::vector<sk::CommandPtr> parts;
                for (sk::InstanceId id : tops) {
                    parts.push_back(sk::makeRemoveCommand(world, id));
                }
                undo.push(world, sk::makeCompoundCommand(std::move(parts)));
                applySelection({}, sk::kInvalidId);
                place.rebuildTree(world);
                saveScene();
                SK_INFO("Eliminados %d objetos", count);
            }
        }
        deleteKeyWasDown = delKeyDown;

        const float aspect = (window.framebufferHeight() > 0)
            ? static_cast<float>(window.framebufferWidth()) /
              static_cast<float>(window.framebufferHeight())
            : 16.0f / 9.0f;
        const sk::Mat4 projection = sk::perspective(sk::radians(60.0f), aspect, 0.1f, 200.0f);
        const sk::Mat4 viewProj = projection * camera.view();

        // --- Seleccion en el viewport (click / Ctrl+click / marquee) ---
        const int vpW = window.framebufferWidth();
        const int vpH = window.framebufferHeight();
        const std::optional<sk::Vec2> pressed = window.consumeLeftPressed();
        const std::optional<sk::Vec2> released = window.consumeLeftReleased();

        // --- Portapapeles y rotacion (Ctrl+C/X/V/D/R/T) ---
        {
            const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool ok = window.hasFocus() && !window.textInputFocused() &&
                            workspace.visible() && place.visible() &&
                            !gizmoDrag.active && !objDragActive;

            // Copiar (Ctrl+C): los subarboles seleccionados al portapapeles.
            const bool copyDown = ok && ctrlDown && window.keyDown('C');
            if (edge(copyDown, copyKeyWasDown) && !selection.empty()) {
                clipboard.clear();
                for (sk::InstanceId id : topLevelSelection()) {
                    clipboard.push_back(captureNodes(id));
                }
                clipboardFromCut = false;
                SK_INFO("Copiados %d objeto(s)",
                        static_cast<int>(clipboard.size()));
            }

            // Cortar (Ctrl+X): copia y borra; el pegado posterior va al raton.
            const bool cutDown = ok && ctrlDown && window.keyDown('X');
            if (edge(cutDown, cutKeyWasDown) && !selection.empty()) {
                const std::vector<sk::InstanceId> tops = topLevelSelection();
                if (!tops.empty()) {
                    clipboard.clear();
                    for (sk::InstanceId id : tops) {
                        clipboard.push_back(captureNodes(id));
                    }
                    clipboardFromCut = true;
                    std::vector<sk::CommandPtr> parts;
                    for (sk::InstanceId id : tops) {
                        parts.push_back(sk::makeRemoveCommand(world, id));
                    }
                    undo.push(world, sk::makeCompoundCommand(std::move(parts)));
                    applySelection({}, sk::kInvalidId);
                    place.rebuildTree(world);
                    saveScene();
                    SK_INFO("Cortados %d objeto(s)",
                            static_cast<int>(clipboard.size()));
                }
            }

            // Pegar (Ctrl+V): si vino de copiar, encima del original; si
            // vino de cortar, bajo el puntero (mismo padre, desplazado).
            const bool pasteDown = ok && ctrlDown && window.keyDown('V');
            if (edge(pasteDown, pasteKeyWasDown) && !clipboard.empty()) {
                sk::Vec3 mouseOffset{};
                if (clipboardFromCut && !clipboard.front().empty()) {
                    const float planeY = clipboard.front().front().position().y;
                    const sk::Ray ray = sk::rayFromCamera(
                        camera, aspect, 60.0f, window.mousePos(), vpW, vpH);
                    if (std::fabs(ray.dir.y) > 1e-6f) {
                        const float t = (planeY - ray.origin.y) / ray.dir.y;
                        if (t > 0.0f) {
                            const sk::Vec3 hit = ray.origin + ray.dir * t;
                            mouseOffset = {
                                hit.x - clipboard.front().front().position().x,
                                0.0f,
                                hit.z - clipboard.front().front().position().z};
                        }
                    }
                }
                // Ids padre de las raices del portapapeles: si todas
                // comparten padre y sigue existiendo, se pega ahi.
                sk::InstanceId sharedParent = sk::kInvalidId;
                bool sameParent = true;
                for (size_t i = 0; i < clipboard.size(); ++i) {
                    if (clipboard[i].empty()) continue;
                    const sk::InstanceId p = clipboard[i].front().parentId();
                    if (i == 0 || sharedParent == sk::kInvalidId) {
                        sharedParent = p;
                    } else if (p != sharedParent) {
                        sameParent = false;
                    }
                }
                const sk::InstanceId target =
                    (sameParent && world.exists(sharedParent))
                        ? sharedParent
                        : defaultParent();
                const sk::Vec3 off = clipboardFromCut
                                         ? mouseOffset
                                         : sk::Vec3{0.0f, 1.0f, 0.0f};
                std::vector<sk::Instance> nodes;
                std::vector<sk::InstanceId> newIds;
                for (const std::vector<sk::Instance>& subtree : clipboard) {
                    for (const sk::Instance& n : subtree) {
                        nodes.push_back(n);
                        newIds.push_back(world.allocateId());
                    }
                }
                if (!nodes.empty()) {
                    std::vector<sk::InstanceId> roots;
                    undo.push(world, sk::makeInsertSubtreeCommand(
                                         world, nodes, newIds, target, off, "",
                                         &roots));
                    place.rebuildTree(world);
                    if (!roots.empty()) {
                        applySelection(roots, roots.back());
                    }
                    saveScene();
                    SK_INFO("Pegados %d objeto(s)",
                            static_cast<int>(roots.size()));
                }
            }

            // Clonar en el sitio (Ctrl+D).
            const bool dupDown = ok && ctrlDown && window.keyDown('D');
            if (edge(dupDown, dupKeyWasDown) && !selection.empty()) {
                std::vector<sk::CommandPtr> parts;
                std::vector<sk::InstanceId> newRoots;
                for (sk::InstanceId id : selection) {
                    const sk::Instance* object = world.find(id);
                    if (!object) continue;
                    sk::InstanceId root = sk::kInvalidId;
                    parts.push_back(sk::makeCloneCommand(
                        world, id, object->parentId(), sk::Vec3{}, &root));
                    if (root != sk::kInvalidId) newRoots.push_back(root);
                }
                if (!parts.empty()) {
                    undo.push(world,
                              sk::makeCompoundCommand(std::move(parts)));
                    place.rebuildTree(world);
                    applySelection(newRoots,
                                   newRoots.empty() ? sk::kInvalidId
                                                    : newRoots.back());
                    saveScene();
                    SK_INFO("Clonados %d objeto(s)",
                            static_cast<int>(newRoots.size()));
                }
            }

            // Rotar 90 grados: Ctrl+R en Y, Ctrl+T en X.
            const bool rotYDown = ok && ctrlDown && window.keyDown('R');
            if (edge(rotYDown, rotYKeyWasDown) && !selection.empty()) {
                std::vector<sk::CommandPtr> parts;
                for (sk::InstanceId id : selection) {
                    const sk::Instance* object = world.find(id);
                    if (!object) continue;
                    sk::Vec3 r = object->rotation();
                    r.y += 90.0f;
                    parts.push_back(sk::makeTransformCommand(
                        world, id, object->position(), r, object->scale()));
                }
                if (!parts.empty()) {
                    undo.push(world,
                              sk::makeCompoundCommand(std::move(parts)));
                    saveScene();
                    applySelection(selection, primary);
                    SK_INFO("Rotado +90 en Y");
                }
            }
            const bool rotXDown = ok && ctrlDown && window.keyDown('T');
            if (edge(rotXDown, rotXKeyWasDown) && !selection.empty()) {
                std::vector<sk::CommandPtr> parts;
                for (sk::InstanceId id : selection) {
                    const sk::Instance* object = world.find(id);
                    if (!object) continue;
                    sk::Vec3 r = object->rotation();
                    r.x += 90.0f;
                    parts.push_back(sk::makeTransformCommand(
                        world, id, object->position(), r, object->scale()));
                }
                if (!parts.empty()) {
                    undo.push(world,
                              sk::makeCompoundCommand(std::move(parts)));
                    saveScene();
                    applySelection(selection, primary);
                    SK_INFO("Rotado +90 en X");
                }
            }
        }

        // --- Deshacer/rehacer (Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z),
        // renombrar con F2 y foco de camara en la seleccion (F) ---
        {
            const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool keysOk = window.hasFocus() && !window.textInputFocused() &&
                                workspace.visible() && place.visible() &&
                                !gizmoDrag.active && !objDragActive;
            if (keysOk) {
                // Deshacer y rehacer: solo PLACE y sin escribir en un
                // campo (escribir Ctrl+Z en un EDIT no debe revertir).
                const bool undoDown = ctrlDown && window.keyDown('Z');
                if (edge(undoDown, undoKeyWasDown)) doUndo();
                const bool redoDown = ctrlDown &&
                                      (window.keyDown('Y') ||
                                       (shiftDown && window.keyDown('Z')));
                if (edge(redoDown, redoKeyWasDown)) doRedo();

                // F2: renombrar la parte activa con un dialogo de texto.
                if (edge(window.keyDown(VK_F2), f2KeyWasDown) &&
                    primary != sk::kInvalidId) {
                    const sk::Instance* object = world.find(primary);
                    if (object) {
                        const std::string newName = sk::ui::promptText(
                            static_cast<HWND>(window.nativeHandle()),
                            "Renombrar Parte", object->name().c_str());
                        if (!newName.empty()) {
                            doRename(primary, newName);
                        }
                    }
                }

                // F: enfoca la camara en la seleccion (centro del grupo).
                if (edge(window.keyDown('F'), fKeyWasDown) &&
                    !selection.empty()) {
                    sk::Vec3 minV{1e30f, 1e30f, 1e30f};
                    sk::Vec3 maxV{-1e30f, -1e30f, -1e30f};
                    bool any = false;
                    for (sk::InstanceId id : selection) {
                        const sk::Instance* object = world.find(id);
                        if (!object) continue;
                        any = true;
                        const sk::Vec3 pos = object->position();
                        const sk::Vec3 scale = object->scale();
                        for (int a = 0; a < 3; ++a) {
                            const float half = std::fabs((&scale.x)[a]) * 0.5f;
                            const float lo = (&pos.x)[a] - half;
                            const float hi = (&pos.x)[a] + half;
                            (&minV.x)[a] = std::fmin((&minV.x)[a], lo);
                            (&maxV.x)[a] = std::fmax((&maxV.x)[a], hi);
                        }
                    }
                    if (any) {
                        const sk::Vec3 center = (minV + maxV) * 0.5f;
                        camera.focus(center, sk::length(maxV - minV) * 0.5f);
                        SK_INFO("camera foco: %d objeto(s) centro "
                                "(%.2f,%.2f,%.2f)",
                                static_cast<int>(selection.size()), center.x,
                                center.y, center.z);
                    }
                }

                // F1: abre la Referencia de clases en la clase del objeto
                // activo (o "Part" si no hay seleccion).
                if (edge(window.keyDown(VK_F1), f1KeyWasDown)) {
                    const sk::Instance* object = world.find(primary);
                    const std::string className =
                        object ? object->className() : std::string("Part");
                    place.setVisible(false);
                    code.setVisible(false);
                    docs.setVisible(true);
                    docs.showClass(className);
                    workspace.setDocsActive(true);
                    SK_INFO("Referencia de clases: %s", className.c_str());
                }

                // Ctrl+G: agrupa la seleccion bajo un Model nuevo.
                const bool groupDown = ctrlDown && window.keyDown('G');
                if (edge(groupDown, groupKeyWasDown) && !selection.empty()) {
                    std::vector<sk::InstanceId> members;
                    for (sk::InstanceId id : selection) {
                        if (!world.isProtected(id)) members.push_back(id);
                    }
                    if (!members.empty()) {
                        sk::InstanceId parent = sk::kInvalidId;
                        if (const sk::Instance* first =
                                world.find(members.front())) {
                            parent = first->parentId();
                        }
                        if (parent == sk::kInvalidId) parent = defaultParent();
                        sk::InstanceId groupId = sk::kInvalidId;
                        undo.push(world, sk::makeGroupCommand(
                                             world, parent, members,
                                             &groupId));
                        saveScene();
                        place.rebuildTree(world);
                        applySelection({groupId}, groupId);
                        logProjection = true;
                        SK_INFO("Grupo %s con %d objeto(s)",
                                worldName(groupId).c_str(),
                                static_cast<int>(members.size()));
                    }
                }

                // Ctrl+U: desagrupa los Model/Folder seleccionados (sus
                // hijos suben al padre del grupo).
                const bool ungroupDown = ctrlDown && window.keyDown('U');
                if (edge(ungroupDown, ungroupKeyWasDown) &&
                    !selection.empty()) {
                    std::vector<sk::CommandPtr> parts;
                    std::vector<sk::InstanceId> groups;
                    for (sk::InstanceId id : selection) {
                        const sk::Instance* object = world.find(id);
                        if (!object || world.isProtected(id)) continue;
                        if (object->className() != "Model" &&
                            object->className() != "Folder") {
                            continue;
                        }
                        groups.push_back(id);
                        parts.push_back(sk::makeUngroupCommand(world, id));
                    }
                    if (!parts.empty()) {
                        undo.push(world,
                                  sk::makeCompoundCommand(std::move(parts)));
                        saveScene();
                        place.rebuildTree(world);
                        applySelection({}, sk::kInvalidId);
                        logProjection = true;
                        SK_INFO("Desagrupado %d contenedor(es)",
                                static_cast<int>(groups.size()));
                    }
                }

                // Ctrl+L: bloquea/desbloquea (Data.Locked) la seleccion.
                const bool lockDown = ctrlDown && window.keyDown('L');
                if (edge(lockDown, lockKeyWasDown) && !selection.empty()) {
                    std::vector<sk::CommandPtr> parts;
                    for (sk::InstanceId id : selection) {
                        const sk::Instance* o = world.find(id);
                        if (!o || world.isProtected(id)) continue;
                        sk::PropertyValue pv;
                        pv.b = !o->propBool("Locked", false);
                        parts.push_back(
                            sk::makePropertyCommand(world, id, "Locked", pv));
                    }
                    if (!parts.empty()) {
                        undo.push(world,
                                  sk::makeCompoundCommand(std::move(parts)));
                        saveScene();
                        if (const sk::Instance* o = world.find(selection.front()))
                            place.showObject(*o);
                        SK_INFO("Bloqueado: %d objeto(s)",
                                static_cast<int>(selection.size()));
                    }
                }

                // Alt+A: ancla/desancla (Data.Anchored) la seleccion.
                const bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
                const bool anchorDown = altDown && window.keyDown('A');
                if (edge(anchorDown, anchorKeyWasDown) && !selection.empty()) {
                    std::vector<sk::CommandPtr> parts;
                    for (sk::InstanceId id : selection) {
                        const sk::Instance* o = world.find(id);
                        if (!o || world.isProtected(id)) continue;
                        sk::PropertyValue pv;
                        pv.b = !o->propBool("Anchored", false);
                        parts.push_back(
                            sk::makePropertyCommand(world, id, "Anchored", pv));
                    }
                    if (!parts.empty()) {
                        undo.push(world,
                                  sk::makeCompoundCommand(std::move(parts)));
                        saveScene();
                        if (const sk::Instance* o = world.find(selection.front()))
                            place.showObject(*o);
                        SK_INFO("Anclado: %d objeto(s)",
                                static_cast<int>(selection.size()));
                    }
                }
            }
        }

        // --- Gizmo de herramientas: estado, hover y arrastre ---
        const int tool = workspace.tool();
        const sk::Instance* primaryObj = world.find(primary);
        const bool gizmoReady =
            place.visible() && workspace.visible() && tool >= 1 && primaryObj;
        const sk::Vec3 gizmoOrigin =
            primaryObj ? primaryObj->position() : sk::Vec3{};
        float gizmoScaleMax = 1.0f;
        if (primaryObj) {
            const sk::Vec3 gscale = primaryObj->scale();
            gizmoScaleMax = std::fmax(
                std::fabs(gscale.x),
                std::fmax(std::fabs(gscale.y), std::fabs(gscale.z)));
        }
        // Durante el arrastre el tamano visual se congela (L del arrastre).
        if (gizmoDrag.active) gizmoScaleMax = 2.0f * gizmoDrag.L - 2.0f;
        const sk::Vec3 gizmoViewDir =
            sk::normalize(camera.position() - gizmoOrigin);

        // Eje bajo el puntero (solo con el raton en el viewport y sin
        // arrastre en curso). El hit test ignora los colores, asi que las
        // lineas de sondeo se construyen sin resaltar.
        if (gizmoReady && !gizmoDrag.active && !objDragActive &&
            inViewport(window.mousePos())) {
            const std::vector<sk::GizmoLine> probe =
                sk::buildGizmo(tool, gizmoOrigin, gizmoScaleMax, gizmoViewDir, -1);
            hoverAxis = sk::hitTestGizmo(probe, window.mousePos(), viewProj, vpW, vpH);
        } else {
            hoverAxis = -1;
        }
        // El resaltado del gizmo cambia al pasar de un eje a otro: repinta.
        if (hoverAxis != lastDrawnHover) {
            needsRedraw = true;
            lastDrawnHover = hoverAxis;
        }

        // Puntas de los ejes al cambiar de herramienta o de objeto
        // primario (o tras un arrastre, que deja la marca sin marcar).
        if (gizmoReady && !gizmoDrag.active && !objDragActive &&
            (tool != lastTipsTool || primary != lastTipsPrimary)) {
            sk::Vec3 ndc;
            int px[3] = {-1, -1, -1};
            int py[3] = {-1, -1, -1};
            for (int axis = 0; axis < 3; ++axis) {
                const sk::Vec3 tip =
                    sk::gizmoHandlePoint(tool, axis, gizmoOrigin, gizmoScaleMax);
                if (sk::transformPoint(viewProj, tip, ndc)) {
                    px[axis] = static_cast<int>((ndc.x * 0.5f + 0.5f) * vpW);
                    py[axis] = static_cast<int>((ndc.y * 0.5f + 0.5f) * vpH);
                }
            }
            SK_INFO("gizmo puntas X(%d,%d) Y(%d,%d) Z(%d,%d)", px[0], py[0],
                    px[1], py[1], px[2], py[2]);
            if (tool == 1 || tool == 2) {
                // Mangos negativos (3..5), en un registro aparte para no
                // mezclarlos con el recuento de "gizmo puntas".
                int nx[3] = {-1, -1, -1};
                int ny[3] = {-1, -1, -1};
                for (int axis = 0; axis < 3; ++axis) {
                    const sk::Vec3 tip = sk::gizmoHandlePoint(
                        tool, axis + 3, gizmoOrigin, gizmoScaleMax);
                    if (sk::transformPoint(viewProj, tip, ndc)) {
                        nx[axis] = static_cast<int>((ndc.x * 0.5f + 0.5f) * vpW);
                        ny[axis] = static_cast<int>((ndc.y * 0.5f + 0.5f) * vpH);
                    }
                }
                SK_INFO("puntas neg X(%d,%d) Y(%d,%d) Z(%d,%d)", nx[0], ny[0],
                        nx[1], ny[1], nx[2], ny[2]);
            }
            lastTipsTool = tool;
            lastTipsPrimary = primary;
            lastHoverAxis = -1; // que el hover vuelva a registrar
        }

        if (pressed && inViewport(*pressed)) {
            bool startedGizmo = false;
            // El eje se recalcula con el punto exacto del press: el hover
            // (hoverAxis) se deriva de mousePos y puede haberse perdido si
            // un movimiento real del raton llego entre el hover y el click.
            int pressAxis = -1;
            if (gizmoReady && !gizmoDrag.active) {
                const std::vector<sk::GizmoLine> probe = sk::buildGizmo(
                    tool, gizmoOrigin, gizmoScaleMax, gizmoViewDir, -1);
                pressAxis = sk::hitTestGizmo(probe, *pressed, viewProj, vpW, vpH);
            }
            if (pressAxis >= 0) {
                // Part bloqueada (Locked): no se arrastra con el raton,
                // aunque sus campos Transform sigan siendo editables.
                bool anyLocked = false;
                for (sk::InstanceId id : selection) {
                    const sk::Instance* object = world.find(id);
                    if (object && object->propBool("Locked", false)) {
                        anyLocked = true;
                        break;
                    }
                }
                if (!anyLocked) {
                    const sk::Ray ray = sk::rayFromCamera(
                        camera, aspect, 60.0f, *pressed, vpW, vpH);
                    startedGizmo = sk::gizmoBegin(
                        gizmoDrag, tool, pressAxis, world, selection,
                        gizmoOrigin, gizmoScaleMax, gizmoViewDir, ray);
                }
                if (startedGizmo) {
                    // El arrastre del gizmo sustituye al marquee. El undo
                    // se registra al soltar (delta transform).
                    dragPending = false;
                    dragActive = false;
                    if (pressAxis < 3) {
                        SK_INFO("gizmo %s: arrastre eje %c", kToolNames[tool],
                                "XYZ"[pressAxis]);
                    } else {
                        SK_INFO("gizmo %s: arrastre eje -%c", kToolNames[tool],
                                "XYZ"[pressAxis - 3]);
                    }
                }
            }
            if (!startedGizmo && !gizmoDrag.active) {
                dragPending = true;
                dragActive = false;
                dragStart = *pressed;
                // Cuerpo de una parte bajo el puntero: candidato a arrastre
                // de objeto (se confirma al superar el umbral de arrastre).
                pressCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                pressHit = sk::kInvalidId;
                if (!pressCtrl) {
                    pressHit = sk::pickObject(
                        world, sk::rayFromCamera(camera, aspect, 60.0f,
                                                 *pressed, vpW, vpH));
                }
            }
        }

        // Arrastre del objeto: la caja del grupo arrastrado se apoya en
        // la cara del parte que esta bajo el puntero (cualquier cara:
        // suelo, techo, pared o inclinada), con la rotacion del lead
        // alineada a esa cara y la posicion en su plano a la rejilla;
        // sin nada debajo el rayo cae al plano del suelo Y=0. Alt
        // conserva la orientacion y Shift desactiva la rejilla.
        if (objDragActive) {
            const sk::Ray ray0 = sk::rayFromCamera(
                camera, aspect, 60.0f, dragStart, vpW, vpH);
            const sk::Ray ray1 = sk::rayFromCamera(
                camera, aspect, 60.0f, window.mousePos(), vpW, vpH);
            std::vector<sk::InstanceId> dragged;
            std::vector<sk::DragPose> startPose;
            dragged.reserve(objDragStart.size());
            startPose.reserve(objDragStart.size());
            int leadIndex = -1;
            for (const ObjDragStart& start : objDragStart) {
                if (start.id == pressHit) {
                    leadIndex = static_cast<int>(dragged.size());
                }
                dragged.push_back(start.id);
                startPose.push_back({start.id, start.pos, start.rot});
            }
            const bool alignSurface = (GetKeyState(VK_MENU) & 0x8000) == 0;
            const bool snapGrid = (GetKeyState(VK_SHIFT) & 0x8000) == 0;
            sk::DragResult result;
            sk::computeSurfaceDrag(world, ray0, ray1, dragged, leadIndex,
                                   grabOffset, startPose, alignSurface,
                                   snapGrid ? workspace.moveStep() : 0.0f,
                                   result);
            for (const sk::DragPose& pose : result.poses) {
                if (sk::Instance* object = world.find(pose.id)) {
                    object->setPosition(pose.position);
                    object->setRotation(pose.rotation);
                    world.markChanged(pose.id);
                }
            }
            ++contentVersion; // la escena cambio: rehacer models/contornos
            // Registro de la cara de destino: una sola vez por cara (las
            // pruebas leen donde se apoya la parte sin spam de logs).
            if (!snapLogged || result.surfaceId != snapSurface ||
                sk::length(result.surfaceNormal - snapNormal) > 1e-3f) {
                if (result.onSurface) {
                    SK_INFO("snap %s: cara de %s normal (%.2f,%.2f,%.2f)",
                            worldName(pressHit).c_str(),
                            worldName(result.surfaceId).c_str(),
                            result.surfaceNormal.x, result.surfaceNormal.y,
                            result.surfaceNormal.z);
                } else if (result.groundHit) {
                    SK_INFO("snap %s: suelo Y=0", worldName(pressHit).c_str());
                } else {
                    SK_INFO("snap %s: cielo (x/z del plano de camara, "
                                          "apoyo en el suelo)",
                            worldName(pressHit).c_str());
                }
                snapLogged = true;
                snapSurface = result.surfaceId;
                snapNormal = result.surfaceNormal;
            }
            if (selection.size() == 1) {
                if (const sk::Instance* object = world.find(selection.front())) {
                    place.showObject(*object, false); // Transform al dia sin relayout
                }
            }
        }

        // Arrastre del gizmo: el delta se aplica a toda la seleccion.
        if (gizmoDrag.active) {
            const sk::Ray ray = sk::rayFromCamera(
                camera, aspect, 60.0f, window.mousePos(), vpW, vpH);
            if (sk::gizmoUpdate(gizmoDrag, ray, world, workspace.moveStep(),
                            static_cast<float>(workspace.rotateStep())) &&
                selection.size() == 1 &&
                primaryObj) {
                place.showObject(*primaryObj, false); // Transform al dia (sin relayout)
            }
            ++contentVersion; // la escena cambio: rehacer models/contornos
        }

        // Registro al apuntar a un eje (una sola vez por punta: las
        // pruebas hacen hover, leen la punta y arrastran).
        if (hoverAxis >= 0 && gizmoReady && !gizmoDrag.active) {
            const sk::Vec3 tip = sk::gizmoHandlePoint(
                tool, hoverAxis, gizmoOrigin, gizmoScaleMax);
            int tx = -1;
            int ty = -1;
            sk::Vec3 ndc;
            if (sk::transformPoint(viewProj, tip, ndc)) {
                tx = static_cast<int>((ndc.x * 0.5f + 0.5f) * vpW);
                ty = static_cast<int>((ndc.y * 0.5f + 0.5f) * vpH);
            }
            if (hoverAxis != lastHoverAxis || tool != lastHoverTool ||
                primary != lastHoverPrimary || tx != lastHoverTx ||
                ty != lastHoverTy) {
                if (hoverAxis < 3) {
                    SK_INFO("gizmo eje %c punta (%d,%d)", "XYZ"[hoverAxis], tx,
                            ty);
                } else {
                    SK_INFO("gizmo eje -%c punta (%d,%d)", "XYZ"[hoverAxis - 3],
                            tx, ty);
                }
                lastHoverTool = tool;
                lastHoverAxis = hoverAxis;
                lastHoverPrimary = primary;
                lastHoverTx = tx;
                lastHoverTy = ty;
            }
        } else if (hoverAxis < 0) {
            lastHoverAxis = -1;
        }

        // Cursor de cruz de mover sobre el gizmo o mientras se arrastra.
        const bool wantMoveCursor =
            gizmoDrag.active || objDragActive || hoverAxis >= 0;
        if (wantMoveCursor) {
            SetCursor(LoadCursorA(nullptr, IDC_SIZEALL));
            cursorForced = true;
        } else if (cursorForced) {
            SetCursor(LoadCursorA(nullptr, IDC_ARROW));
            cursorForced = false;
        }

        if (dragPending && !dragActive && !objDragActive) {
            const int thX = GetSystemMetrics(SM_CXDRAG);
            const int thY = GetSystemMetrics(SM_CYDRAG);
            const sk::Vec2 pos = window.mousePos();
            if (std::abs(pos.x - dragStart.x) > thX ||
                std::abs(pos.y - dragStart.y) > thY) {
                if (pressHit != sk::kInvalidId) {
                    const sk::Instance* pressedObj = world.find(pressHit);
                    if (pressedObj && pressedObj->propBool("Locked", false)) {
                        // Part Locked: el cuerpo no inicia arrastre.
                        dragPending = false;
                        SK_INFO("drag %s: bloqueada (Locked)",
                                worldName(pressHit).c_str());
                    } else {
                        // Es el cuerpo de una parte: arrastre de objeto en
                        // vez de marquee (si no estaba seleccionado, pasa
                        // a serlo).
                        if (std::find(selection.begin(), selection.end(),
                                      pressHit) == selection.end()) {
                            applySelection({pressHit}, pressHit);
                        }
                        objDragActive = true;
                        objDragStart.clear();
                        for (sk::InstanceId id : selection) {
                            const sk::Instance* object = world.find(id);
                            // Una parte Locked no viaja con el arrastre.
                            if (!object ||
                                object->propBool("Locked", false)) {
                                continue;
                            }
                            objDragStart.push_back(
                                {id, object->position(), object->rotation()});
                        }
                        // Punto de agarre (donde se pulso sobre el cuerpo)
                        // menos el centro del lead: fija la posicion del
                        // grupo respecto al puntero durante todo el
                        // arrastre.
                        const sk::Ray ray0 = sk::rayFromCamera(
                            camera, aspect, 60.0f, dragStart, vpW, vpH);
                        const sk::Instance* hitObj = world.find(pressHit);
                        grabOffset = {};
                        if (hitObj) {
                            sk::Vec3 grabPoint{};
                            sk::Vec3 grabNormal{};
                            if (sk::rayObjectFace(*hitObj, ray0, grabPoint,
                                                  grabNormal)) {
                                grabOffset = grabPoint - hitObj->position();
                            }
                        }
                        snapLogged = false; // el primer frame registra la cara
                        snapSurface = sk::kInvalidId;
                        snapNormal = {};
                        dragPending = false;
                        SK_INFO("drag %s: arrastre", worldName(pressHit).c_str());
                    }
                } else {
                    dragActive = true;
                    SK_INFO("marquee (%d,%d)-(%d,%d)",
                            static_cast<int>(dragStart.x),
                            static_cast<int>(dragStart.y),
                            static_cast<int>(pos.x), static_cast<int>(pos.y));
                }
            }
        }
        if (released && objDragActive) {
            // Fin del arrastre: registrar el cambio (undo) y guardar.
            std::vector<sk::CommandPtr> parts;
            for (const ObjDragStart& start : objDragStart) {
                const sk::Instance* object = world.find(start.id);
                if (!object) continue;
                SK_INFO("drag %s: pos (%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f) "
                        "rot (%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f)",
                        worldName(start.id).c_str(), start.pos.x, start.pos.y,
                        start.pos.z, object->position().x,
                        object->position().y, object->position().z,
                        start.rot.x, start.rot.y, start.rot.z,
                        object->rotation().x, object->rotation().y,
                        object->rotation().z);
                parts.push_back(sk::makeTransformCommandExplicit(
                    start.id, start.pos, start.rot, object->scale(),
                    object->position(), object->rotation(),
                    object->scale()));
            }
            objDragActive = false;
            objDragStart.clear();
            pressHit = sk::kInvalidId;
            dragPending = false;
            lastTipsTool = -1; // las puntas del gizmo siguen al objeto
            if (!parts.empty()) {
                undo.push(world, sk::makeCompoundCommand(std::move(parts)));
            }
            saveScene();
            applySelection(selection, primary);
        } else if (released && gizmoDrag.active) {
            // Fin del arrastre: registrar el cambio (undo), guardar y
            // refrescar.
            std::vector<sk::CommandPtr> parts;
            for (const sk::GizmoSnapshot& snap : gizmoDrag.snapshots) {
                const sk::Instance* object = world.find(snap.id);
                if (!object) continue;
                SK_INFO("gizmo %s: %s pos (%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f) "
                        "rot (%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f) "
                        "esc (%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f)",
                        kToolNames[gizmoDrag.tool], worldName(snap.id).c_str(),
                        snap.position.x, snap.position.y, snap.position.z,
                        object->position().x, object->position().y,
                        object->position().z, snap.rotation.x, snap.rotation.y,
                        snap.rotation.z, object->rotation().x,
                        object->rotation().y, object->rotation().z,
                        snap.scale.x, snap.scale.y, snap.scale.z,
                        object->scale().x, object->scale().y,
                        object->scale().z);
                parts.push_back(sk::makeTransformCommandExplicit(
                    snap.id, snap.position, snap.rotation, snap.scale,
                    object->position(), object->rotation(), object->scale()));
            }
            gizmoDrag.active = false;
            lastTipsTool = -1; // las puntas han podido cambiar
            if (!parts.empty()) {
                undo.push(world, sk::makeCompoundCommand(std::move(parts)));
            }
            saveScene();
            applySelection(selection, primary);
        } else if (released && (dragPending || dragActive)) {
            const bool wasDrag = dragActive;
            dragPending = false;
            dragActive = false;
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (wasDrag) {
                // Rectangulo de arrastre, clamado al viewport.
                float x0 = std::fmin(dragStart.x, released->x);
                float y0 = std::fmin(dragStart.y, released->y);
                float x1 = std::fmax(dragStart.x, released->x);
                float y1 = std::fmax(dragStart.y, released->y);
                const float vx0 = static_cast<float>(sk::PlaceView::kPropertiesWidth);
                const float vx1 = static_cast<float>(vpW) -
                                  static_cast<float>(sk::PlaceView::kExplorerWidth);
                const float vy0 = static_cast<float>(sk::WorkspaceTabs::kTopBandHeight);
                const float vy1 = static_cast<float>(vpH);
                x0 = std::fmax(x0, vx0);
                y0 = std::fmax(y0, vy0);
                x1 = std::fmin(x1, vx1);
                y1 = std::fmin(y1, vy1);
                std::vector<sk::InstanceId> hits;
                if (x0 < x1 && y0 < y1) {
                    hits = sk::selectInRect(world, viewProj,
                                            sk::Vec2{x0, y0}, sk::Vec2{x1, y1},
                                            vpW, vpH);
                }
                SK_INFO("marquee -> %d objetos", static_cast<int>(hits.size()));
                sk::InstanceId newPrimary = primary;
                if (std::find(hits.begin(), hits.end(), primary) == hits.end()) {
                    newPrimary = hits.empty() ? sk::kInvalidId : hits.front();
                }
                applySelection(std::move(hits), newPrimary);
            } else {
                // Click simple: el objeto mas cercano bajo el puntero.
                const sk::InstanceId hit = sk::pickObject(
                    world, sk::rayFromCamera(camera, aspect, 60.0f, *released,
                                             vpW, vpH));
                SK_INFO("pick (%d,%d) -> %s", static_cast<int>(released->x),
                        static_cast<int>(released->y),
                        hit == sk::kInvalidId ? "(nada)"
                                              : worldName(hit).c_str());
                std::vector<sk::InstanceId> ids = selection;
                sk::InstanceId newPrimary = primary;
                if (hit == sk::kInvalidId) {
                    ids.clear();       // click en vacio: deselecciona
                    newPrimary = sk::kInvalidId;
                } else if (ctrl) {
                    const auto it = std::find(ids.begin(), ids.end(), hit);
                    if (it != ids.end()) {
                        ids.erase(it); // quitar de la seleccion
                        if (newPrimary == hit) {
                            newPrimary = ids.empty() ? sk::kInvalidId
                                                     : ids.front();
                        }
                    } else {
                        ids.push_back(hit); // anadir (Ctrl)
                        newPrimary = hit;
                    }
                } else {
                    ids = {hit};
                    newPrimary = hit;
                }
                applySelection(std::move(ids), newPrimary);
            }
        }

        if (logProjection && place.visible()) {
            logProjection = false;
            for (sk::InstanceId id : allInstances()) {
                const sk::Instance* object = world.find(id);
                if (!object || !object->isRenderable()) continue;
                sk::Vec3 ndc;
                if (sk::transformPoint(viewProj, object->position(), ndc)) {
                    const float px = (ndc.x * 0.5f + 0.5f) * vpW;
                    const float py = (ndc.y * 0.5f + 0.5f) * vpH;
                    SK_INFO("%s pantalla (%d,%d)", object->name().c_str(),
                            static_cast<int>(px), static_cast<int>(py));
                }
            }
        }

        // --- Dibujo bajo demanda ---
        // Solo se llama a drawFrame si algo cambio o hay interaccion
        // sostenida (camara, arrastres, ventana Play). En reposo no se
        // dibuja nada y el bucle duerme sin consumir CPU.
        const bool dragging = gizmoDrag.active || objDragActive ||
                              dragActive || dragPending;
        const bool camKeysHeld =
            window.hasFocus() && !window.textInputFocused() &&
            (window.keyDown('W') || window.keyDown('A') ||
             window.keyDown('S') || window.keyDown('D') ||
             window.keyDown('Q') || window.keyDown('E'));
        continuous = camKeysHeld || window.mouseRightDown() || dragging ||
                     play.active();

        if (needsRedraw || continuous) {
            // models/shapeIds/outlines se cachean: solo se reconstruyen
            // cuando cambia la version de contenido (escena/seleccion).
            if (contentVersion != builtVersion) {
                builtVersion = contentVersion;
                modelCache.clear();
                shapeCache.clear();
                const std::vector<sk::RenderPart>& parts = world.renderParts();
                modelCache.reserve(parts.size());
                shapeCache.reserve(parts.size());
                for (const sk::RenderPart& part : parts) {
                    modelCache.push_back(part.model);
                    shapeCache.push_back(part.shape);
                }
                // Contorno celeste: el cubo alambre un 2% mas grande que
                // el objeto, para que asome por el borde de la malla.
                outlineCache.clear();
                outlineCache.reserve(selection.size());
                for (sk::InstanceId id : selection) {
                    const sk::Instance* object = world.find(id);
                    if (object) {
                        outlineCache.push_back(
                            object->modelMatrix() *
                            sk::scale(sk::Vec3{1.02f, 1.02f, 1.02f}));
                    }
                }
            }

            // Rectangulo de arrastre mientras dura el marquee. Se muestra
            // igual que la seleccion al soltar: clamado al viewport para
            // que el rectangulo visible coincida con lo que se selecciona.
            sk::ScreenRect marquee;
            if (dragActive && inViewport(dragStart)) {
                const sk::Vec2 pos = window.mousePos();
                float x0 = std::fmin(dragStart.x, pos.x);
                float y0 = std::fmin(dragStart.y, pos.y);
                float x1 = std::fmax(dragStart.x, pos.x);
                float y1 = std::fmax(dragStart.y, pos.y);
                x0 = std::fmax(x0, static_cast<float>(
                                       sk::PlaceView::kPropertiesWidth));
                y0 = std::fmax(y0, static_cast<float>(
                                       sk::WorkspaceTabs::kTopBandHeight));
                x1 = std::fmin(x1, static_cast<float>(
                                       vpW - sk::PlaceView::kExplorerWidth));
                y1 = std::fmin(y1, static_cast<float>(vpH));
                marquee.valid = true;
                marquee.x0 = x0;
                marquee.y0 = y0;
                marquee.x1 = x1;
                marquee.y1 = y1;
            }

            // Lineas del gizmo: un vertice por extremo de segmento (el
            // renderer clama al buffer si hubiera mas de kGizmoCapacity).
            std::vector<sk::LineVertex> gizmoLines;
            if (gizmoReady) {
                // Resalta el mango bajo el puntero o el mango agarrado.
                int paintAxis = hoverAxis;
                if (gizmoDrag.active) {
                    paintAxis = gizmoDrag.axis +
                                (gizmoDrag.axisSign < 0 ? 3 : 0);
                }
                const std::vector<sk::GizmoLine> geo = sk::buildGizmo(
                    gizmoDrag.active ? gizmoDrag.tool : tool, gizmoOrigin,
                    gizmoScaleMax, gizmoViewDir, paintAxis);
                gizmoLines.reserve(geo.size() * 2);
                for (const sk::GizmoLine& line : geo) {
                    gizmoLines.push_back(
                        {{line.a.x, line.a.y, line.a.z},
                         {line.color.x, line.color.y, line.color.z}});
                    gizmoLines.push_back(
                        {{line.b.x, line.b.y, line.b.z},
                         {line.color.x, line.color.y, line.color.z}});
                }
            }
            // El buffer GPU del gizmo solo se reescribe si cambio.
            const bool gizmoChanged =
                gizmoLines.size() != lastGizmo.size() ||
                (gizmoLines.size() != 0 &&
                 std::memcmp(gizmoLines.data(), lastGizmo.data(),
                             gizmoLines.size() * sizeof(sk::LineVertex)) != 0);
            if (gizmoChanged) lastGizmo = gizmoLines;

            const double drawStart = statsEnabled ? nowSeconds() : 0.0;
            if (!renderer.drawFrame(viewProj, modelCache, shapeCache,
                                    outlineCache, marquee, gizmoLines,
                                    gizmoChanged)) {
                SK_ERROR("drawFrame fallo");
                return 1;
            }
            needsRedraw = false;
            lastDrawWall = nowSeconds();
            if (statsEnabled) {
                statsAccum += lastDrawWall - drawStart;
                ++statsFrames;
            }
        }

        // Ventana Play/Run: su propio renderer, mismo bucle principal.
        play.renderFrame(world, camera);

        // --- Guardado diferido: 500 ms tras el ultimo cambio, nunca a
        // mitad de un arrastre. Al cerrar/salir/Guardar se vuelca aparte.
        if (sceneDirty && !dragging && (now - lastSceneChange) >= 0.5) {
            writeSceneNow();
        }

        // --- --stats: cada 5 s, frames dibujados y tiempo medio ---
        if (statsEnabled && (now - statsWindowStart) >= 5.0) {
            const double avgMs =
                statsFrames ? (statsAccum / statsFrames) * 1000.0 : 0.0;
            SK_INFO("stats: %llu frames dibujados, %.2f ms/frame",
                    statsFrames, avgMs);
            statsWindowStart = now;
            statsFrames = 0;
            statsAccum = 0.0;
        }

        ++frame;

        // --- Espera del bucle ---
        // Modo test (--frames): no bloquea para llegar al conteo. En
        // interaccion: mantiene el limite de FPS. Con guardado pendiente:
        // espera lo que falta. En reposo: duerme sin gastar CPU.
        if (maxFrames >= 0) {
            // sin espera
        } else if (continuous) {
            const double wait = (lastDrawWall + frameInterval) - nowSeconds();
            if (wait > 0.0005) {
                window.waitEvents(static_cast<unsigned long>(wait * 1000.0));
            }
        } else if (sceneDirty) {
            const double wait = (lastSceneChange + 0.5) - nowSeconds();
            if (wait > 0.0005) {
                window.waitEvents(static_cast<unsigned long>(wait * 1000.0));
            }
        } else {
            window.waitEvents(INFINITE);
        }
    }

    writeSceneNow(); // volcar cualquier cambio pendiente al salir

    code.destroy();
    place.destroy();
    workspace.destroy();
    panel.destroy();
    renderer.shutdown();
    window.destroy();
    sk::ui::shutdownIconAtlas();

    const double elapsed = nowSeconds() - startTime;
    SK_INFO("saliendo tras %d frames (%.1fs)", frame, elapsed);
    return 0;
}
