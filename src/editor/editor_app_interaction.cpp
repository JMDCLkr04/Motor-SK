#include "editor_app.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

#include "../core/log.h"
#include "../scene/gizmo.h"
#include "../scene/picking.h"

namespace sk::editor {

// Puntero en el viewport: hover/press del gizmo, arrastre de objeto/gizmo,
// cursor, umbral de arrastre y suelta (click/Ctrl+click/marquee).
void EditorApp::frameInteraction(const FrameCtx& ctx) {
    const InputState& in = *ctx.in;

    // Eje bajo el puntero (solo con el raton en el viewport y sin arrastre
    // en curso): lo lleva el GizmoController (hit test sin resaltar).
    const bool canHoverGizmo =
        ctx.gizmoReady && !gizmo_.active() && !objDrag_.active() &&
        inViewport(window_.mousePos());
    if (gizmo_.updateHover(canHoverGizmo, world_, ctx.tool, ctx.gizmoOrigin,
                           ctx.gizmoScaleMax, ctx.gizmoViewDir, ctx.viewProj,
                           ctx.vpW, ctx.vpH, window_.mousePos())) {
        // El resaltado del gizmo cambio de eje: repinta.
        needsRedraw_ = true;
        lastDrawnHover_ = gizmo_.hoverAxis();
    }
    const int hoverAxis = gizmo_.hoverAxis();

    // Puntas de los ejes al cambiar de herramienta o de objeto primario (o
    // tras un arrastre, que deja la marca sin marcar).
    if (ctx.gizmoReady && !gizmo_.active() && !objDrag_.active() &&
        (ctx.tool != lastTipsTool_ || primary_ != lastTipsPrimary_)) {
        Vec3 ndc;
        int px[3] = {-1, -1, -1};
        int py[3] = {-1, -1, -1};
        for (int axis = 0; axis < 3; ++axis) {
            const Vec3 tip = gizmoHandlePoint(ctx.tool, axis, ctx.gizmoOrigin,
                                              ctx.gizmoScaleMax);
            if (transformPoint(ctx.viewProj, tip, ndc)) {
                px[axis] = static_cast<int>((ndc.x * 0.5f + 0.5f) * ctx.vpW);
                py[axis] = static_cast<int>((ndc.y * 0.5f + 0.5f) * ctx.vpH);
            }
        }
        SK_DEBUG("gizmo puntas X(%d,%d) Y(%d,%d) Z(%d,%d)", px[0], py[0],
                 px[1], py[1], px[2], py[2]);
        if (ctx.tool == 1 || ctx.tool == 2) {
            // Mangos negativos (3..5), en un registro aparte para no
            // mezclarlos con el recuento de "gizmo puntas".
            int nx[3] = {-1, -1, -1};
            int ny[3] = {-1, -1, -1};
            for (int axis = 0; axis < 3; ++axis) {
                const Vec3 tip = gizmoHandlePoint(ctx.tool, axis + 3,
                                                  ctx.gizmoOrigin,
                                                  ctx.gizmoScaleMax);
                if (transformPoint(ctx.viewProj, tip, ndc)) {
                    nx[axis] = static_cast<int>((ndc.x * 0.5f + 0.5f) * ctx.vpW);
                    ny[axis] = static_cast<int>((ndc.y * 0.5f + 0.5f) * ctx.vpH);
                }
            }
            SK_DEBUG("puntas neg X(%d,%d) Y(%d,%d) Z(%d,%d)", nx[0], ny[0],
                     nx[1], ny[1], nx[2], ny[2]);
        }
        lastTipsTool_ = ctx.tool;
        lastTipsPrimary_ = primary_;
        lastHoverAxis_ = -1; // que el hover vuelva a registrar
    }

    const std::optional<Vec2> pressed = window_.consumeLeftPressed();
    const std::optional<Vec2> released = window_.consumeLeftReleased();

    if (pressed && inViewport(*pressed)) {
        bool startedGizmo = false;
        // El eje se recalcula con el punto exacto del press: el hover se
        // deriva de mousePos y puede haberse perdido.
        int pressAxis = -1;
        if (ctx.gizmoReady && !gizmo_.active()) {
            const std::vector<GizmoLine> probe =
                buildGizmo(ctx.tool, ctx.gizmoOrigin, ctx.gizmoScaleMax,
                           ctx.gizmoViewDir, -1);
            pressAxis =
                hitTestGizmo(probe, *pressed, ctx.viewProj, ctx.vpW, ctx.vpH);
        }
        if (pressAxis >= 0) {
            // Part bloqueada (Locked): no se arrastra con el raton (el
            // propio controlador lo comprueba).
            const Ray ray = rayFromCamera(camera_, ctx.aspect, 60.0f, *pressed,
                                          ctx.vpW, ctx.vpH);
            startedGizmo = gizmo_.begin(world_, ctx.tool, pressAxis, selection_,
                                        ctx.gizmoOrigin, ctx.gizmoScaleMax,
                                        ctx.gizmoViewDir, ray);
            if (startedGizmo) {
                // El arrastre del gizmo sustituye al marquee. El undo se
                // registra al soltar (delta transform).
                selectionCtrl_.reset();
                if (pressAxis < 3) {
                    SK_INFO("gizmo %s: arrastre eje %c", toolName(ctx.tool),
                            "XYZ"[pressAxis]);
                } else {
                    SK_INFO("gizmo %s: arrastre eje -%c", toolName(ctx.tool),
                            "XYZ"[pressAxis - 3]);
                }
            }
        }
        if (!startedGizmo && !gizmo_.active()) {
            // Cuerpo de una parte bajo el puntero: candidato a arrastre de
            // objeto (se confirma al superar el umbral).
            const bool ctrl = in.ctrl();
            InstanceId pickHit = kInvalidId;
            if (!ctrl) {
                pickHit = pickObjectExcept(
                    world_,
                    rayFromCamera(camera_, ctx.aspect, 60.0f, *pressed,
                                  ctx.vpW, ctx.vpH),
                    hiddenFromActive());
            }
            selectionCtrl_.beginPress(*pressed, ctrl, pickHit);
        }
    }

    // Arrastre del objeto: la caja del grupo se apoya en la cara del parte
    // bajo el puntero (o en el suelo Y=0). Alt conserva la orientacion y
    // Shift desactiva la rejilla.
    if (objDrag_.active()) {
        const Ray ray0 = rayFromCamera(camera_, ctx.aspect, 60.0f,
                                       selectionCtrl_.start(), ctx.vpW, ctx.vpH);
        const Ray ray1 = rayFromCamera(camera_, ctx.aspect, 60.0f,
                                       window_.mousePos(), ctx.vpW, ctx.vpH);
        objDrag_.update(world_, ray0, ray1, !in.alt(),
                        in.shift() ? 0.0f : workspace_.moveStep());
        ++contentVersion_; // la escena cambio: rehacer models/contornos
        if (selection_.size() == 1) {
            if (const Instance* object = world_.find(selection_.front())) {
                place_.showObject(*object, false); // Transform al dia
            }
        }
    }

    // Arrastre del gizmo: el delta se aplica a toda la seleccion.
    if (gizmo_.active()) {
        const Ray ray = rayFromCamera(camera_, ctx.aspect, 60.0f,
                                      window_.mousePos(), ctx.vpW, ctx.vpH);
        if (gizmo_.update(world_, ray, workspace_.moveStep(),
                          static_cast<float>(workspace_.rotateStep())) &&
            selection_.size() == 1) {
            if (const Instance* primaryObj = world_.find(primary_)) {
                place_.showObject(*primaryObj, false); // Transform al dia
            }
        }
        ++contentVersion_; // la escena cambio: rehacer models/contornos
    }

    // Registro al apuntar a un eje (una sola vez por punta: las pruebas
    // hacen hover, leen la punta y arrastran).
    if (hoverAxis >= 0 && ctx.gizmoReady && !gizmo_.active()) {
        const Vec3 tip = gizmoHandlePoint(ctx.tool, hoverAxis, ctx.gizmoOrigin,
                                          ctx.gizmoScaleMax);
        int tx = -1;
        int ty = -1;
        Vec3 ndc;
        if (transformPoint(ctx.viewProj, tip, ndc)) {
            tx = static_cast<int>((ndc.x * 0.5f + 0.5f) * ctx.vpW);
            ty = static_cast<int>((ndc.y * 0.5f + 0.5f) * ctx.vpH);
        }
        if (hoverAxis != lastHoverAxis_ || ctx.tool != lastHoverTool_ ||
            primary_ != lastHoverPrimary_ || tx != lastHoverTx_ ||
            ty != lastHoverTy_) {
            if (hoverAxis < 3) {
                SK_DEBUG("gizmo eje %c punta (%d,%d)", "XYZ"[hoverAxis], tx, ty);
            } else {
                SK_DEBUG("gizmo eje -%c punta (%d,%d)", "XYZ"[hoverAxis - 3],
                         tx, ty);
            }
            lastHoverTool_ = ctx.tool;
            lastHoverAxis_ = hoverAxis;
            lastHoverPrimary_ = primary_;
            lastHoverTx_ = tx;
            lastHoverTy_ = ty;
        }
    } else if (hoverAxis < 0) {
        lastHoverAxis_ = -1;
    }

    // Cursor de cruz de mover sobre el gizmo o mientras se arrastra.
    const bool wantMoveCursor =
        gizmo_.active() || objDrag_.active() || hoverAxis >= 0;
    if (wantMoveCursor) {
        SetCursor(LoadCursorA(nullptr, IDC_SIZEALL));
        cursorForced_ = true;
    } else if (cursorForced_) {
        SetCursor(LoadCursorA(nullptr, IDC_ARROW));
        cursorForced_ = false;
    }

    // Umbral de arrastre: si el puntero se aleja del punto de presion,
    // decide entre arrastre de objeto (cuerpo de una parte) o marquee.
    if (selectionCtrl_.pending() && !selectionCtrl_.active() &&
        !objDrag_.active()) {
        const int thX = GetSystemMetrics(SM_CXDRAG);
        const int thY = GetSystemMetrics(SM_CYDRAG);
        const Vec2 pos = window_.mousePos();
        const Vec2 ds = selectionCtrl_.start();
        if (std::abs(pos.x - ds.x) > thX || std::abs(pos.y - ds.y) > thY) {
            const InstanceId pressHit = selectionCtrl_.pressHit();
            if (pressHit != kInvalidId) {
                const Instance* pressedObj = world_.find(pressHit);
                if (pressedObj && pressedObj->propBool("Locked", false)) {
                    // Part Locked: el cuerpo no inicia arrastre.
                    selectionCtrl_.reset();
                    SK_DEBUG("drag %s: bloqueada (Locked)",
                             worldName(pressHit).c_str());
                } else {
                    // Es el cuerpo de una parte: arrastre de objeto en vez
                    // de marquee (si no estaba seleccionado, pasa a serlo).
                    if (std::find(selection_.begin(), selection_.end(),
                                  pressHit) == selection_.end()) {
                        applySelection({pressHit}, pressHit);
                    }
                    const Ray ray0 = rayFromCamera(camera_, ctx.aspect, 60.0f,
                                                   ds, ctx.vpW, ctx.vpH);
                    objDrag_.begin(world_, selection_, pressHit, ray0);
                    selectionCtrl_.reset();
                    SK_DEBUG("drag %s: arrastre", worldName(pressHit).c_str());
                }
            } else {
                selectionCtrl_.beginMarquee();
                SK_DEBUG("marquee (%d,%d)-(%d,%d)", static_cast<int>(ds.x),
                         static_cast<int>(ds.y), static_cast<int>(pos.x),
                         static_cast<int>(pos.y));
            }
        }
    }

    // Suelta: fin de arrastre (undo + refresco) o resolucion del click /
    // marquee.
    if (released && objDrag_.active()) {
        objDrag_.commit(world_, undo_);
        lastTipsTool_ = -1; // las puntas del gizmo siguen al objeto
        saveScene();
        applySelection(selection_, primary_);
    } else if (released && gizmo_.active()) {
        gizmo_.commit(world_, undo_);
        lastTipsTool_ = -1; // las puntas han podido cambiar
        saveScene();
        applySelection(selection_, primary_);
    } else if (released &&
               (selectionCtrl_.pending() || selectionCtrl_.active())) {
        const bool wasDrag = selectionCtrl_.active();
        const Vec2 ds = selectionCtrl_.start();
        selectionCtrl_.reset();
        const bool ctrl = in.ctrl();
        if (wasDrag) {
            // Rectangulo de arrastre, clamado al viewport.
            std::vector<InstanceId> hits =
                SelectionController::resolveMarquee(world_, ctx.viewProj, ds,
                                                    *released, ctx.vpW, ctx.vpH,
                                                    selection_, primary_);
            // Solo los objetos de la dimension activa son seleccionables.
            hits.erase(std::remove_if(hits.begin(), hits.end(),
                                      [this](InstanceId id) {
                                          return !inActiveDimension(id);
                                      }),
                       hits.end());
            SK_INFO("marquee -> %d objetos", static_cast<int>(hits.size()));
            applySelection(std::move(hits), primary_);
        } else {
            // Click simple: el objeto mas cercano bajo el puntero (solo la
            // dimension activa).
            const InstanceId hit = pickObjectExcept(
                world_,
                rayFromCamera(camera_, ctx.aspect, 60.0f, *released, ctx.vpW,
                              ctx.vpH),
                hiddenFromActive());
            SK_INFO("pick (%d,%d) -> %s", static_cast<int>(released->x),
                    static_cast<int>(released->y),
                    hit == kInvalidId ? "(nada)" : worldName(hit).c_str());
            std::vector<InstanceId> ids = SelectionController::resolveClick(
                selection_, hit, ctrl, primary_);
            applySelection(std::move(ids), primary_);
        }
    }
}

// Dibujo bajo demanda: caches, marquee, gizmo y drawFrame. Devuelve false
// solo si drawFrame falla.
bool EditorApp::frameDraw(const FrameCtx& ctx) {
    if (logProjection_ && place_.visible()) {
        logProjection_ = false;
        for (InstanceId id : allInstances()) {
            const Instance* object = world_.find(id);
            if (!object || !object->isRenderable()) continue;
            Vec3 ndc;
            if (transformPoint(ctx.viewProj, object->position(), ndc)) {
                const float px = (ndc.x * 0.5f + 0.5f) * ctx.vpW;
                const float py = (ndc.y * 0.5f + 0.5f) * ctx.vpH;
                SK_DEBUG("%s pantalla (%d,%d)", object->name().c_str(),
                         static_cast<int>(px), static_cast<int>(py));
            }
        }
    }

    if (!(needsRedraw_ || continuous_)) return true;

    // models/shapeIds/outlines se cachean: solo se reconstruyen cuando
    // cambia la version de contenido (escena/seleccion).
    if (contentVersion_ != builtVersion_) {
        builtVersion_ = contentVersion_;
        modelCache_.clear();
        shapeCache_.clear();
        const std::vector<RenderPart>& parts = world_.renderParts();
        modelCache_.reserve(parts.size());
        shapeCache_.reserve(parts.size());
        for (const RenderPart& part : parts) {
            if (!inActiveDimension(part.id)) continue; // solo la dimension activa
            modelCache_.push_back(part.model);
            shapeCache_.push_back(part.shape);
        }
        // Contorno celeste: el cubo alambre un 2% mas grande que el objeto,
        // para que asome por el borde de la malla.
        outlineCache_.clear();
        outlineCache_.reserve(selection_.size());
        for (InstanceId id : selection_) {
            const Instance* object = world_.find(id);
            if (object) {
                outlineCache_.push_back(object->modelMatrix() *
                                        scale(Vec3{1.02f, 1.02f, 1.02f}));
            }
        }
    }

    // Rectangulo de arrastre mientras dura el marquee. Se muestra igual que
    // la seleccion al soltar: clamado al viewport para que el rectangulo
    // visible coincida con lo que se selecciona.
    ScreenRect marquee;
    if (selectionCtrl_.active() && inViewport(selectionCtrl_.start())) {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        SelectionController::marqueeRect(selectionCtrl_.start(),
                                         window_.mousePos(), ctx.vpW, ctx.vpH,
                                         x0, y0, x1, y1);
        marquee.valid = true;
        marquee.x0 = x0;
        marquee.y0 = y0;
        marquee.x1 = x1;
        marquee.y1 = y1;
    }

    // Lineas del gizmo: un vertice por extremo de segmento (el renderer
    // clama al buffer si hubiera mas de kGizmoCapacity).
    std::vector<LineVertex> gizmoLines;
    if (ctx.gizmoReady) {
        // Resalta el mango bajo el puntero o el mango agarrado.
        const int paintAxis = gizmo_.paintAxis();
        const std::vector<GizmoLine> geo = buildGizmo(
            gizmo_.active() ? gizmo_.drag().tool : ctx.tool, ctx.gizmoOrigin,
            ctx.gizmoScaleMax, ctx.gizmoViewDir, paintAxis);
        gizmoLines.reserve(geo.size() * 2);
        for (const GizmoLine& line : geo) {
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
        gizmoLines.size() != lastGizmo_.size() ||
        (gizmoLines.size() != 0 &&
         std::memcmp(gizmoLines.data(), lastGizmo_.data(),
                     gizmoLines.size() * sizeof(LineVertex)) != 0);
    if (gizmoChanged) lastGizmo_ = gizmoLines;

    const double drawStart = statsEnabled_ ? nowSeconds() : 0.0;
    if (!renderer_.drawFrame(ctx.viewProj, modelCache_, shapeCache_,
                             outlineCache_, marquee, gizmoLines,
                             gizmoChanged)) {
        SK_ERROR("drawFrame fallo");
        return false;
    }
    needsRedraw_ = false;
    lastDrawWall_ = nowSeconds();
    if (statsEnabled_) {
        statsAccum_ += lastDrawWall_ - drawStart;
        ++statsFrames_;
    }
    return true;
}

} // namespace sk::editor
