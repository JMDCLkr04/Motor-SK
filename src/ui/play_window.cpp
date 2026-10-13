#include "play_window.h"

#include <vector>

#include "../core/log.h"
#include "../platform/window.h"
#include "../render/renderer.h"
#include "../scene/camera.h"
#include "../scene/spawn.h"
#include "../scene/world.h"

namespace sk {
namespace ui {

PlayWindow::PlayWindow() = default;

PlayWindow::~PlayWindow() { close(); }

bool PlayWindow::open(const World& world, const Camera& camera,
                      bool runScripts) {
    // La prueba arranca en su propio punto de aparicion (SpawnLocation) en
    // vez de heredar la vista del editor.
    (void)camera;
    const Vec3 spawn = playSpawnPosition(world);
    camera_.setPosition(spawn);
    SK_DEBUG("Play/Run: camara inicial en (%.1f,%.1f,%.1f)", spawn.x, spawn.y,
             spawn.z);
    if (active_) return true;

    window_ = std::make_unique<Window>();
    window_->setQuitOnClose(false); // cerrar Play no cierra el editor
    if (!window_->create(960, 600,
                         runScripts ? "Motor SK - Run" : "Motor SK - Play")) {
        window_.reset();
        return false;
    }

    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->init(*window_)) {
        SK_ERROR("PlayWindow: no se pudo iniciar el renderer");
        renderer_.reset();
        window_->destroy();
        window_.reset();
        return false;
    }

    run_ = runScripts;
    active_ = true;
    SK_INFO("%s: ventana independiente abierta",
            runScripts ? "Run (ejecutar scripts)" : "Play (simular)");
    return true;
}

void PlayWindow::close() {
    if (renderer_) {
        renderer_->shutdown();
        renderer_.reset();
    }
    if (window_) {
        window_->destroy();
        window_.reset();
    }
    if (active_) SK_INFO("Play/Run cerrado");
    active_ = false;
    run_ = false;
}

void PlayWindow::renderFrame(const World& world) {
    if (!active_ || !window_ || !renderer_) return;
    if (window_->closed()) {
        close();
        return;
    }
    if (window_->consumeResized()) renderer_->invalidateSwapchain();

    const int w = window_->framebufferWidth();
    const int h = window_->framebufferHeight();
    if (w <= 0 || h <= 0) return;

    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    const Mat4 projection = perspective(radians(60.0f), aspect, 0.1f, 200.0f);
    const Mat4 viewProj = projection * camera_.view();

    std::vector<Mat4> models;
    std::vector<int> shapes;
    const std::vector<RenderPart>& parts = world.renderParts();
    models.reserve(parts.size());
    shapes.reserve(parts.size());
    for (const RenderPart& part : parts) {
        models.push_back(part.model);
        shapes.push_back(part.shape);
    }

    if (!renderer_->drawFrame(viewProj, models, shapes, {}, {}, {})) {
        SK_ERROR("PlayWindow: drawFrame fallo");
        close();
    }
}

} // namespace ui
} // namespace sk
