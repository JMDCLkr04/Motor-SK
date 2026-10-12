#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "../editor/output_buffer.h"
#include "kit/paint_buffer.h"
#include "kit/scroll_bar.h"

namespace sk {
namespace ui {

// Contenido de la pestana Output del panel izquierdo: lista virtualizada de
// mensajes con color por nivel (info/aviso/error), autoscroll que se pausa
// cuando el usuario sube, boton "Limpiar" y buffer circular de 5.000 lineas.
// Es un widget autonomo (PaintBuffer + ScrollBar del kit) para que el marco
// lo pueda alojar en cualquier vista.
class OutputPanel {
public:
    OutputPanel();
    ~OutputPanel();

    void create(HWND parent, int id);
    void destroy();
    HWND hwnd() const { return hwnd_; }
    void setRect(const RECT& r);

    // Anade una linea al panel (nivel 0 info, 1 aviso, 2 error). Si el
    // usuario no ha subido, se desplaza automaticamente al final.
    void addLine(int level, const std::string& text);
    void clear();

    size_t lineCount() const { return buffer_.size(); }

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    void paint(HDC hdc);
    void layout();
    void invalidate();
    void updateScroll(bool toEnd);
    void trackLeave();
    int maxScrollPos() const;
    RECT clearRect() const;

    HWND hwnd_ = nullptr;
    PaintBuffer pb_;
    ScrollBar vbar_;
    OutputBuffer buffer_;
    int scrollY_ = 0;
    bool stick_ = true;         // autoscroll pegado al final
    bool hoverClear_ = false;
    bool trackingLeave_ = false;

    static constexpr int kToolbarH = 26;
    static constexpr int kLineH = 18;
    static constexpr int kBarW = 12;
};

} // namespace ui
} // namespace sk
