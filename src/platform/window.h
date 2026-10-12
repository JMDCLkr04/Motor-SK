#pragma once

#include <optional>

#include "../math/math.h"
#include "input_state.h"

namespace sk {

// Ventana Win32 con estado de input para la cámara libre.
// El teclado (WASD/shift) se mantiene por eventos WM_KEYDOWN/WM_KEYUP en
// input(); el ratón (clicks, rueda, deltas de movimiento) llega por
// mensajes. Ya no se consultan las APIs de teclado ni el foco en cada
// frame: el estado lo mantiene InputState por eventos.
class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(int width, int height, const char* title);
    void destroy();

    // Procesa mensajes pendientes. Devuelve false cuando se pidió cerrar.
    bool pumpEvents();

    // Espera a que llegue un mensaje o expire el timeout (INFINITE = sin
    // limite). Devuelve false si se pidió cerrar. En reposo evita que el
    // hilo gire sobre PeekMessage consumiendo CPU.
    bool waitEvents(unsigned long timeoutMs);

    bool shouldClose() const { return shouldClose_; }

    // Ventanas secundarias (Play/Run): al cerrarse no deben terminar la
    // aplicacion (no postean WM_QUIT). closed() avisa de su cierre.
    void setQuitOnClose(bool value) { quitOnClose_ = value; }
    bool closed() const { return closed_; }

    // Estado del teclado y del foco, actualizado por eventos. Sustituye a
    // keyDown()/hasFocus()/textInputFocused(): down()/pressed()/ctrl()...
    // leen el estado cacheado; active() es la ventana en primer plano y
    // textFocus() si el teclado esta en un campo de texto (EDIT).
    const InputState& input() const { return input_; }

    bool mouseRightDown() const { return rmbDown_; }
    bool mouseLeftDown() const { return lmbDown_; }

    // Posición actual del ratón en coordenadas de cliente de la ventana.
    Vec2 mousePos() const { return mousePos_; }

    // Delta acumulado desde la última consulta y rueda acumulada.
    // Son "consume": vacian el acumulador para el próximo frame.
    Vec2 consumeMouseDelta() const;
    float consumeWheel() const;

    // Clicks del botón izquierdo: consumen el evento (una sola vez) con
    // la posición de cliente donde ocurrieron.
    std::optional<Vec2> consumeLeftPressed();
    std::optional<Vec2> consumeLeftReleased();

    int framebufferWidth() const { return width_; }
    int framebufferHeight() const { return height_; }

    // true una sola vez tras un WM_SIZE (para recrear el swapchain).
    bool consumeResized() {
        bool r = resized_;
        resized_ = false;
        return r;
    }

    // true una sola vez tras un WM_PAINT (restaurado, descubierto, etc.):
    // obliga a repintar aunque no haya cambiado nada mas.
    bool consumeRepaintRequested() {
        bool r = repaintRequested_;
        repaintRequested_ = false;
        return r;
    }

    void* nativeHandle() const { return hwnd_; }

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam);

    void* hwnd_ = nullptr;
    void* instance_ = nullptr;

    InputState input_;

    bool shouldClose_ = false;
    bool closed_ = false;
    bool quitOnClose_ = true;
    bool rmbDown_ = false;
    bool lmbDown_ = false;
    bool resized_ = false;
    bool repaintRequested_ = false;

    bool tracking_ = false;
    mutable Vec2 pendingDelta_;
    mutable float pendingWheel_ = 0.0f;
    mutable std::optional<Vec2> pendingPress_;
    mutable std::optional<Vec2> pendingRelease_;
    Vec2 mousePos_{};
    int lastX_ = 0;
    int lastY_ = 0;

    int width_ = 0;
    int height_ = 0;
};

} // namespace sk
