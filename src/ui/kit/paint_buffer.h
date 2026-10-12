#pragma once

#include <windows.h>

namespace sk {
namespace ui {

// Doble buffer de pintado: un bitmap cacheado del mismo tamano que el
// cliente. Los widgets dibujan en coordenadas de cliente (SIN
// SetViewportOrgEx) y endPaint/blit copian SOLO el rectangulo de
// invalidacion (ps.rcPaint) tomado de las mismas coordenadas, asi el
// repintado parcial nunca sale desplazado.
class PaintBuffer {
public:
    PaintBuffer();
    ~PaintBuffer();

    bool beginPaint(HWND hwnd, PAINTSTRUCT* ps);
    void endPaint();

    // Copia de nuevo el rectangulo de invalidacion (si se dibujo mas
    // contenido despues de un endPaint externo).
    void blit();

    HDC dc() const { return memDc_; }
    const RECT& rect() const { return ps_.rcPaint; }

private:
    void ensureBacking(HWND hwnd, HDC windowDc, int cw, int ch);

    HWND hwnd_ = nullptr;
    PAINTSTRUCT ps_{};
    HDC windowDc_ = nullptr;
    HDC memDc_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HBITMAP oldBmp_ = nullptr;
    int bmpW_ = 0;
    int bmpH_ = 0;
};

} // namespace ui
} // namespace sk