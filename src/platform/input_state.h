#pragma once

namespace sk {

// Estado del teclado y del foco por eventos. Sustituye las consultas
// Win32 de teclado (asincronas) y de foco que se hacian en cada frame: la
// ventana avisa aqui cuando llega WM_KEYDOWN/WM_KEYUP, WM_ACTIVATE y
// WM_SETFOCUS/WM_KILLFOCUS, y el resto del programa solo lee el estado ya
// calculado.
//
// Los flancos (pressed) son por frame: newFrame() limpia los flancos al
// empezar a drenar la cola de mensajes; onKeyDown marca "pressed" solo en
// la transicion de arriba a abajo.
//
// Es logica pura (sin windows.h): los codigos de tecla son los VK_ de
// Win32, pero se declaran aqui para no depender de la cabecera.
class InputState {
public:
    static constexpr int kShift = 0x10;
    static constexpr int kControl = 0x11;
    static constexpr int kMenu = 0x12; // Alt
    static constexpr int kLShift = 0xA0;
    static constexpr int kRShift = 0xA1;
    static constexpr int kLControl = 0xA2;
    static constexpr int kRControl = 0xA3;
    static constexpr int kLMenu = 0xA4;
    static constexpr int kRMenu = 0xA5;

    // Limpia los flancos del frame anterior. Se llama al principio de
    // cada drenaje de mensajes.
    void newFrame();

    // Transiciones de tecla (desde WM_KEYDOWN/WM_KEYUP).
    void onKeyDown(int vk);
    void onKeyUp(int vk);

    // Olvida todas las teclas (al perder el primer plano, para no dejar
    // una tecla "pegada" que nunca recibe su WM_KEYUP).
    void clearKeys();

    // Foco: la ventana esta en primer plano (WASD/atajos) y si el teclado
    // esta en un campo de texto (EDIT).
    void setActive(bool value) { active_ = value; }
    bool active() const { return active_; }
    void setTextFocus(bool value) { textFocus_ = value; }
    bool textFocus() const { return textFocus_; }

    bool down(int vk) const {
        return vk >= 0 && vk < kKeyCount && down_[vk];
    }
    bool pressed(int vk) const {
        return vk >= 0 && vk < kKeyCount && pressed_[vk];
    }

    bool ctrl() const {
        return down(kControl) || down(kLControl) || down(kRControl);
    }
    bool shift() const {
        return down(kShift) || down(kLShift) || down(kRShift);
    }
    bool alt() const {
        return down(kMenu) || down(kLMenu) || down(kRMenu);
    }

private:
    static constexpr int kKeyCount = 256;
    bool down_[kKeyCount] = {};
    bool pressed_[kKeyCount] = {};
    bool active_ = true;
    bool textFocus_ = false;
};

} // namespace sk
