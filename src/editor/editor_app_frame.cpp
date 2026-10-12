#include "editor_app.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "editor_commands.h"
#include "shortcut_map.h"

#include "../core/log.h"
#include "../scene/picking.h"
#include "../ui/prompt.h"

namespace sk::editor {
namespace {

// True si el atajo de `action` (tabla unica ShortcutMap) se pulso este
// frame: la tecla coincide y los modificadores que exige estan bajos.
bool shortcutPressed(const InputState& in, const char* action, int vk) {
    return ShortcutMap::matches(action, vk, in.ctrl(), in.shift(), in.alt()) &&
           in.pressed(vk);
}

} // namespace

// Un frame completo: eventos ya bombeados, estado y dibujo bajo demanda.
bool EditorApp::frame(const InputState& in) {
    if (window_.consumeResized()) {
        SK_INFO("resize: %dx%d", window_.framebufferWidth(),
                window_.framebufferHeight());
        renderer_.invalidateSwapchain();
        panel_.resize(window_.framebufferWidth(), window_.framebufferHeight());
        workspace_.resize(window_.framebufferWidth(),
                          window_.framebufferHeight());
        place_.resize(window_.framebufferWidth(), window_.framebufferHeight());
        code_.resize(window_.framebufferWidth(), window_.framebufferHeight());
        docs_.resize(window_.framebufferWidth(), window_.framebufferHeight());
        needsRedraw_ = true;
    }
    // Restaurado/descubierto (WM_PAINT): repinta el viewport.
    if (window_.consumeRepaintRequested()) {
        needsRedraw_ = true;
    }

    if (!view3d_) {
        // Menu: solo se procesan mensajes (los controles Win32 pintan
        // encima; el renderer no se llama hasta abrir un proyecto). En
        // reposo el hilo duerme sin gastar CPU.
        ++frameCount_;
        if (maxFrames_ >= 0) {
            Sleep(1); // modo test: no bloquear para llegar a --frames
        } else {
            window_.waitEvents(INFINITE);
        }
        return true;
    }

    // Esc: cancela el arrastre del gizmo si esta en curso; si no, el del
    // objeto; si no, el marquee; si no, vuelve al panel de proyectos.
    const bool escPressed = in.pressed(VK_ESCAPE) && in.active();
    if (escPressed && gizmo_.active()) {
        gizmo_.cancel(world_);
        lastTipsTool_ = -1; // las puntas vuelven a su sitio
        SK_INFO("gizmo cancelado");
        applySelection(selection_, primary_);
    } else if (escPressed && objDrag_.active()) {
        objDrag_.cancel(world_);
        selectionCtrl_.reset();
        SK_INFO("drag cancelado");
        applySelection(selection_, primary_);
    } else if (escPressed &&
               (selectionCtrl_.pending() || selectionCtrl_.active())) {
        selectionCtrl_.reset();
        SK_INFO("marquee cancelado");
    } else if (escPressed) {
        closeProject();
        return true;
    }

    const double now = nowSeconds();
    float dt = static_cast<float>(now - lastTime_);
    lastTime_ = now;
    if (dt > 0.1f) dt = 0.1f; // proteccion contra picos (ventana arrastrada)

    const Mat4 viewBefore = camera_.view();
    camera_.update(dt, window_);
    // Si la camara cambio (WASD, giro con clic derecho, rueda), repinta.
    {
        const float* a = viewBefore.m;
        const float* b = camera_.view().m;
        for (int i = 0; i < 16; ++i) {
            if (std::fabs(a[i] - b[i]) > 1e-6f) {
                needsRedraw_ = true;
                break;
            }
        }
    }

    // Log al empezar a mover la camara (mismo criterio de foco que
    // Camera::update): las pruebas comprueban que WASD/Q/E no funcionan
    // con otra aplicacion en primer plano.
    const bool camKeys =
        in.active() && !in.textFocus() &&
        (in.pressed('W') || in.pressed('A') || in.pressed('S') ||
         in.pressed('D') || in.pressed('Q') || in.pressed('E'));
    if (camKeys) {
        const Vec3 p = camera_.position();
        SK_DEBUG("camara (%.2f,%.2f,%.2f)", p.x, p.y, p.z);
    }

    // Teclas 1-4: cambiar de herramienta (solo con la ventana enfocada, en
    // PLACE, sin escribir en un campo de texto y sin un arrastre en curso).
    const bool toolKeysOk = in.active() && !in.textFocus() &&
                            workspace_.visible() && place_.visible() &&
                            !gizmo_.active();
    if (toolKeysOk) {
        if (shortcutPressed(in, "tool1", '1')) workspace_.setTool(0);
        else if (shortcutPressed(in, "tool2", '2')) workspace_.setTool(1);
        else if (shortcutPressed(in, "tool3", '3')) workspace_.setTool(2);
        else if (shortcutPressed(in, "tool4", '4')) workspace_.setTool(3);
    }

    // Suprimir / Retroceso: elimina la parte o todas las partes
    // seleccionadas (solo con la ventana enfocada, en PLACE, sin escribir
    // en un campo y sin un arrastre en curso).
    const bool delOk =
        in.active() && !in.textFocus() && workspace_.visible() &&
        place_.visible() && !gizmo_.active() && !objDrag_.active();
    if (delOk && !selection_.empty() &&
        (in.pressed(VK_DELETE) || in.pressed(VK_BACK))) {
        performDelete(topLevelSelection());
    }

    // Datos del frame que comparten los manejadores de atajos, interaccion
    // y dibujo. El gizmo se recalcula despues de los atajos porque estos
    // pueden haber cambiado la seleccion.
    FrameCtx ctx;
    ctx.in = &in;
    ctx.vpW = window_.framebufferWidth();
    ctx.vpH = window_.framebufferHeight();
    ctx.aspect = (ctx.vpH > 0)
                     ? static_cast<float>(ctx.vpW) / static_cast<float>(ctx.vpH)
                     : 16.0f / 9.0f;
    ctx.viewProj = perspective(radians(60.0f), ctx.aspect, 0.1f, 200.0f) *
                   camera_.view();

    frameShortcuts(ctx);

    // Gizmo de herramientas: origen, escala y direccion de vista a partir
    // del objeto primario (ya definitivo tras los atajos).
    const int tool = workspace_.tool();
    const Instance* primaryObj = world_.find(primary_);
    ctx.tool = tool;
    ctx.gizmoReady = place_.visible() && workspace_.visible() && tool >= 1 &&
                     primaryObj;
    ctx.gizmoOrigin = primaryObj ? primaryObj->position() : Vec3{};
    float gizmoScaleMax = 1.0f;
    if (primaryObj) {
        const Vec3 gscale = primaryObj->scale();
        gizmoScaleMax = std::fmax(std::fabs(gscale.x),
                                  std::fmax(std::fabs(gscale.y),
                                            std::fabs(gscale.z)));
    }
    // Durante el arrastre el tamano visual se congela (L del arrastre).
    if (gizmo_.active()) gizmoScaleMax = 2.0f * gizmo_.drag().L - 2.0f;
    ctx.gizmoScaleMax = gizmoScaleMax;
    ctx.gizmoViewDir = normalize(camera_.position() - ctx.gizmoOrigin);

    frameInteraction(ctx);

    // --- Dibujo bajo demanda ---
    const bool dragging = gizmo_.active() || objDrag_.active() ||
                          selectionCtrl_.active() || selectionCtrl_.pending();
    const bool camKeysHeld =
        in.active() && !in.textFocus() &&
        (in.down('W') || in.down('A') || in.down('S') || in.down('D') ||
         in.down('Q') || in.down('E'));
    continuous_ = camKeysHeld || window_.mouseRightDown() || dragging ||
                  play_.active();

    if (!frameDraw(ctx)) {
        return false; // drawFrame fallo
    }

    // Ventana Play/Run: su propio renderer, mismo bucle principal.
    play_.renderFrame(world_, camera_);
    // Stop activo solo mientras hay un test en marcha.
    workspace_.setRunning(play_.active());

    // --- Guardado diferido: 500 ms tras el ultimo cambio, nunca a mitad
    // de un arrastre. Al cerrar/salir/Guardar se vuelca aparte. ---
    if (sceneDirty_ && !dragging && (now - lastSceneChange_) >= 0.5) {
        writeSceneNow();
    }

    // --- --stats: cada 5 s, frames dibujados y tiempo medio ---
    if (statsEnabled_ && (now - statsWindowStart_) >= 5.0) {
        const double avgMs =
            statsFrames_ ? (statsAccum_ / statsFrames_) * 1000.0 : 0.0;
        SK_INFO("stats: %llu frames dibujados, %.2f ms/frame", statsFrames_,
                avgMs);
        statsWindowStart_ = now;
        statsFrames_ = 0;
        statsAccum_ = 0.0;
    }

    ++frameCount_;

    // --- Espera del bucle ---
    // Modo test (--frames): no bloquea para llegar al conteo. En
    // interaccion: mantiene el limite de FPS. Con guardado pendiente:
    // espera lo que falta. En reposo: duerme sin gastar CPU.
    if (maxFrames_ >= 0) {
        // sin espera
    } else if (continuous_) {
        const double wait = (lastDrawWall_ + frameInterval_) - nowSeconds();
        if (wait > 0.0005) {
            window_.waitEvents(static_cast<unsigned long>(wait * 1000.0));
        }
    } else if (sceneDirty_) {
        const double wait = (lastSceneChange_ + 0.5) - nowSeconds();
        if (wait > 0.0005) {
            window_.waitEvents(static_cast<unsigned long>(wait * 1000.0));
        }
    } else {
        window_.waitEvents(INFINITE);
    }
    return true;
}

// Atajos que dependen del encuadre (aspecto/viewProj/vpW/vpH): portapapeles,
// rotacion, deshacer/rehacer, F2, foco, F1, agrupar y bloquear/anclar.
void EditorApp::frameShortcuts(const FrameCtx& ctx) {
    const InputState& in = *ctx.in;

    // --- Portapapeles y rotacion (Ctrl+C/X/V/D/R/T) ---
    {
        const bool ok = in.active() && !in.textFocus() &&
                        workspace_.visible() && place_.visible() &&
                        !gizmo_.active() && !objDrag_.active();

        // Copiar (Ctrl+C): los subarboles seleccionados al portapapeles.
        if (ok && shortcutPressed(in, "copy", 'C') && !selection_.empty()) {
            clipboard_.copy(world_, topLevelSelection());
            SK_INFO("Copiados %d objeto(s)", clipboard_.count());
        }

        // Cortar (Ctrl+X): copia y borra; el pegado posterior va al raton.
        if (ok && shortcutPressed(in, "cut", 'X') && !selection_.empty()) {
            const std::vector<InstanceId> tops = topLevelSelection();
            if (!tops.empty()) {
                clipboard_.cut(world_, undo_, tops);
                applySelection({}, kInvalidId);
                place_.rebuildTree(world_);
                saveScene();
                SK_INFO("Cortados %d objeto(s)", clipboard_.count());
            }
        }

        // Pegar (Ctrl+V): si vino de copiar, encima del original; si vino
        // de cortar, bajo el puntero (mismo padre, desplazado).
        if (ok && shortcutPressed(in, "paste", 'V') && !clipboard_.empty()) {
            Vec3 mouseOffset{};
            if (clipboard_.fromCut() && !clipboard_.first().empty()) {
                const float planeY = clipboard_.first().front().position().y;
                const Ray ray = rayFromCamera(camera_, ctx.aspect, 60.0f,
                                              window_.mousePos(), ctx.vpW,
                                              ctx.vpH);
                if (std::fabs(ray.dir.y) > 1e-6f) {
                    const float t = (planeY - ray.origin.y) / ray.dir.y;
                    if (t > 0.0f) {
                        const Vec3 hit = ray.origin + ray.dir * t;
                        mouseOffset = {
                            hit.x - clipboard_.first().front().position().x,
                            0.0f,
                            hit.z - clipboard_.first().front().position().z};
                    }
                }
            }
            // Si todas las raices comparten padre y sigue existiendo, se
            // pega ahi; si no, bajo el Workspace.
            const InstanceId shared = clipboard_.sharedParent(world_);
            const InstanceId target =
                shared != kInvalidId ? shared : defaultParent();
            const Vec3 off = clipboard_.fromCut() ? mouseOffset
                                                  : Vec3{0.0f, 1.0f, 0.0f};
            std::vector<InstanceId> roots;
            if (clipboard_.paste(world_, undo_, target, off, roots)) {
                place_.rebuildTree(world_);
                if (!roots.empty()) {
                    applySelection(roots, roots.back());
                }
                saveScene();
                SK_INFO("Pegados %d objeto(s)", static_cast<int>(roots.size()));
            }
        }

        // Clonar en el sitio (Ctrl+D).
        if (ok && shortcutPressed(in, "duplicate", 'D') && !selection_.empty()) {
            std::vector<InstanceId> newRoots;
            if (EditorCommands::duplicate(world_, undo_, selection_,
                                          newRoots)) {
                place_.rebuildTree(world_);
                applySelection(newRoots, newRoots.empty() ? kInvalidId
                                                          : newRoots.back());
                saveScene();
                SK_INFO("Clonados %d objeto(s)",
                        static_cast<int>(newRoots.size()));
            }
        }

        // Rotar 90 grados: Ctrl+R en Y, Ctrl+T en X.
        if (ok && shortcutPressed(in, "rotateY", 'R') && !selection_.empty()) {
            std::vector<CommandPtr> parts;
            for (InstanceId id : selection_) {
                const Instance* object = world_.find(id);
                if (!object) continue;
                Vec3 r = object->rotation();
                r.y += 90.0f;
                parts.push_back(makeTransformCommand(world_, id,
                                                     object->position(), r,
                                                     object->scale()));
            }
            if (!parts.empty()) {
                undo_.push(world_, makeCompoundCommand(std::move(parts)));
                saveScene();
                applySelection(selection_, primary_);
                SK_INFO("Rotado +90 en Y");
            }
        }
        if (ok && shortcutPressed(in, "rotateX", 'T') && !selection_.empty()) {
            std::vector<CommandPtr> parts;
            for (InstanceId id : selection_) {
                const Instance* object = world_.find(id);
                if (!object) continue;
                Vec3 r = object->rotation();
                r.x += 90.0f;
                parts.push_back(makeTransformCommand(world_, id,
                                                     object->position(), r,
                                                     object->scale()));
            }
            if (!parts.empty()) {
                undo_.push(world_, makeCompoundCommand(std::move(parts)));
                saveScene();
                applySelection(selection_, primary_);
                SK_INFO("Rotado +90 en X");
            }
        }
    }

    // --- Deshacer/rehacer (Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z), renombrar con
    // F2, foco de camara en la seleccion (F), F1, agrupar/desagrupar y
    // bloquear/anclar ---
    {
        const bool keysOk = in.active() && !in.textFocus() &&
                            workspace_.visible() && place_.visible() &&
                            !gizmo_.active() && !objDrag_.active();
        if (!keysOk) return;

        // Deshacer y rehacer: solo PLACE y sin escribir en un campo (escribir
        // Ctrl+Z en un EDIT no debe revertir).
        if (shortcutPressed(in, "undo", 'Z')) doUndo();
        if (shortcutPressed(in, "redo", 'Y') ||
            (in.shift() && in.pressed('Z'))) {
            doRedo();
        }

        // F2: renombrar la parte activa con un dialogo de texto.
        if (shortcutPressed(in, "rename", VK_F2) && primary_ != kInvalidId) {
            const Instance* object = world_.find(primary_);
            if (object) {
                const std::string newName = sk::ui::promptText(
                    static_cast<HWND>(window_.nativeHandle()),
                    "Renombrar Parte", object->name().c_str());
                if (!newName.empty()) {
                    doRename(primary_, newName);
                }
            }
        }

        // F: enfoca la camara en la seleccion (centro del grupo).
        if (shortcutPressed(in, "focus", 'F') && !selection_.empty()) {
            Vec3 center{};
            float radius = 0.0f;
            if (EditorCommands::bounds(world_, selection_, center, radius)) {
                camera_.focus(center, radius);
                SK_INFO("camera foco: %d objeto(s) centro (%.2f,%.2f,%.2f)",
                        static_cast<int>(selection_.size()), center.x, center.y,
                        center.z);
            }
        }

        // F1: abre la Referencia de clases en la clase del objeto activo
        // (o "Part" si no hay seleccion).
        if (shortcutPressed(in, "classes", VK_F1)) {
            const Instance* object = world_.find(primary_);
            const std::string className =
                object ? object->className() : std::string("Part");
            place_.setVisible(false);
            code_.setVisible(false);
            docs_.setVisible(true);
            docs_.showClass(className);
            workspace_.setDocsActive(true);
            SK_INFO("Referencia de clases: %s", className.c_str());
        }

        // Ctrl+G: agrupa la seleccion bajo un Model nuevo.
        if (shortcutPressed(in, "group", 'G') && !selection_.empty()) {
            std::vector<InstanceId> members;
            for (InstanceId id : selection_) {
                if (!world_.isProtected(id)) members.push_back(id);
            }
            if (!members.empty()) {
                InstanceId parent = kInvalidId;
                if (const Instance* first = world_.find(members.front())) {
                    parent = first->parentId();
                }
                if (parent == kInvalidId) parent = defaultParent();
                InstanceId groupId = kInvalidId;
                if (EditorCommands::group(world_, undo_, parent, members,
                                          groupId)) {
                    saveScene();
                    place_.rebuildTree(world_);
                    applySelection({groupId}, groupId);
                    logProjection_ = true;
                    SK_INFO("Grupo %s con %d objeto(s)",
                            worldName(groupId).c_str(),
                            static_cast<int>(members.size()));
                }
            }
        }

        // Ctrl+U: desagrupa los Model/Folder seleccionados (sus hijos suben
        // al padre del grupo).
        if (shortcutPressed(in, "ungroup", 'U') && !selection_.empty()) {
            std::vector<InstanceId> groups;
            if (EditorCommands::ungroup(world_, undo_, selection_, groups)) {
                saveScene();
                place_.rebuildTree(world_);
                applySelection({}, kInvalidId);
                logProjection_ = true;
                SK_INFO("Desagrupado %d contenedor(es)",
                        static_cast<int>(groups.size()));
            }
        }

        // Ctrl+L: bloquea/desbloquea (Data.Locked) la seleccion.
        if (shortcutPressed(in, "lock", 'L') && !selection_.empty()) {
            if (EditorCommands::toggleBool(world_, undo_, selection_, "Locked",
                                           false)) {
                saveScene();
                if (const Instance* o = world_.find(selection_.front())) {
                    place_.showObject(*o);
                }
                SK_INFO("Bloqueado: %d objeto(s)",
                        static_cast<int>(selection_.size()));
            }
        }

        // Alt+A: ancla/desancla (Data.Anchored) la seleccion.
        if (shortcutPressed(in, "anchor", 'A') && !selection_.empty()) {
            if (EditorCommands::toggleBool(world_, undo_, selection_,
                                           "Anchored", false)) {
                saveScene();
                if (const Instance* o = world_.find(selection_.front())) {
                    place_.showObject(*o);
                }
                SK_INFO("Anclado: %d objeto(s)",
                        static_cast<int>(selection_.size()));
            }
        }
    }
}

} // namespace sk::editor
