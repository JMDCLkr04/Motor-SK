#pragma once

#include <memory>

namespace sk {

class Scene;
class Camera;
class Window;
class Renderer;

namespace ui {

// Ventana separada de Play/Run. Renderiza la escena con su propio
// renderer Vulkan (sin tocar el editor, que sigue visible). "Play"
// simula (de momento solo dibuja); "Run" ademas ejecutaria los
// ModelScripts cuando exista el interprete (aqui solo lo indica el
// titulo y el log).
class PlayWindow {
public:
    PlayWindow();
    ~PlayWindow();

    PlayWindow(const PlayWindow&) = delete;
    PlayWindow& operator=(const PlayWindow&) = delete;

    // Abre la ventana y arranca su renderer. runScripts elige el modo.
    bool open(const Scene& scene, const Camera& camera, bool runScripts);
    void close();
    bool active() const { return active_; }

    // Dibuja un frame de la escena (se llama desde el bucle principal
    // mientras active()).
    void renderFrame(const Scene& scene, const Camera& camera);

private:
    std::unique_ptr<Window> window_;
    std::unique_ptr<Renderer> renderer_;
    bool active_ = false;
    bool run_ = false;
};

} // namespace ui
} // namespace sk
