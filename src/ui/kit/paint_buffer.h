#pragma once

#include <windows.h>

namespace sk {
namespace ui {

class PaintBuffer {
public:
    PaintBuffer();
    ~PaintBuffer();

    bool beginPaint(HWND hwnd, PAINTSTRUCT* ps);
    void endPaint();

    HDC dc() const { return memDc_; }
    const RECT& rect() const { return ps_.rcPaint; }

    // Blit the buffer to the window.
    void blit();

private:
    PAINTSTRUCT ps_{};
    HDC windowDc_ = nullptr;
    HDC memDc_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HBITMAP oldBmp_ = nullptr;
    RECT rc_{};
};

} // namespace ui
} // namespace sk
