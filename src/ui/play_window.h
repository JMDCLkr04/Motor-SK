#pragma once

#include <memory>

#include "../scene/camera.h"

namespace sk {

class World;
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
    bool open(const World& world, const Camera& camera, bool runScripts);
    void close();
    bool active() const { return active_; }

    // Dibuja un frame de la escena (se llama desde el bucle principal
    // mientras active()). Usa la camara propia (arranca en el SpawnLocation).
    void renderFrame(const World& world);

private:
    std::unique_ptr<Window> window_;
    std::unique_ptr<Renderer> renderer_;
    // Camara de la prueba: independiente de la del editor; arranca en el
    // SpawnLocation (o (0,20,0)) al abrir.
    Camera camera_;
    bool active_ = false;
    bool run_ = false;
};

} // namespace ui
} // namespace sk
