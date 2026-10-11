#include "play_window.h"

#include <vector>

#include "../core/log.h"
#include "../platform/window.h"
#include "../render/renderer.h"
#include "../scene/camera.h"
#include "../scene/scene.h"

namespace sk {
namespace ui {

PlayWindow::PlayWindow() = default;

PlayWindow::~PlayWindow() { close(); }

bool PlayWindow::open(const Scene& scene, const Camera& camera,
                      bool runScripts) {
    (void)scene;
    (void)camera;
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

void PlayWindow::renderFrame(const Scene& scene, const Camera& camera) {
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
    const Mat4 viewProj = projection * camera.view();

    std::vector<Mat4> models;
    std::vector<int> shapes;
    models.reserve(scene.objects().size());
    shapes.reserve(scene.objects().size());
    for (const SceneObject& object : scene.objects()) {
        if (!isRenderableClass(object.className)) continue;
        models.push_back(object.modelMatrix());
        shapes.push_back(static_cast<int>(object.shape));
    }

    if (!renderer_->drawFrame(viewProj, models, shapes, {}, {}, {})) {
        SK_ERROR("PlayWindow: drawFrame fallo");
        close();
    }
}

} // namespace ui
} // namespace sk
