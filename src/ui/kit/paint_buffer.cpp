#include "paint_buffer.h"

namespace sk {
namespace ui {

PaintBuffer::PaintBuffer() = default;

PaintBuffer::~PaintBuffer() {
    if (memDc_) {
        if (oldBmp_) SelectObject(memDc_, oldBmp_);
        DeleteDC(memDc_);
        memDc_ = nullptr;
        oldBmp_ = nullptr;
    }
    if (bmp_) {
        DeleteObject(bmp_);
        bmp_ = nullptr;
    }
}

void PaintBuffer::ensureBacking(HWND hwnd, HDC windowDc, int cw, int ch) {
    // Se crea el DC/bitmap la primera vez y se recrea solo si el cliente
    // crece; un mismo widget conserva su buffer entre repintados.
    if (!memDc_) memDc_ = CreateCompatibleDC(windowDc);
    if (!bmp_ || hwnd != hwnd_ || cw > bmpW_ || ch > bmpH_) {
        if (oldBmp_) SelectObject(memDc_, oldBmp_);
        if (bmp_) DeleteObject(bmp_);
        if (cw < 2) cw = 2;
        if (ch < 2) ch = 2;
        bmp_ = CreateCompatibleBitmap(windowDc, cw, ch);
        oldBmp_ = static_cast<HBITMAP>(SelectObject(memDc_, bmp_));
        bmpW_ = cw;
        bmpH_ = ch;
    }
    hwnd_ = hwnd;
}

bool PaintBuffer::beginPaint(HWND hwnd, PAINTSTRUCT* ps) {
    windowDc_ = BeginPaint(hwnd, &ps_);
    if (!windowDc_) return false;
    if (ps) *ps = ps_;
    RECT c{};
    GetClientRect(hwnd, &c);
    const int cw = c.right - c.left;
    const int ch = c.bottom - c.top;
    if (cw <= 0 || ch <= 0) {
        EndPaint(hwnd, &ps_);
        windowDc_ = nullptr;
        return false;
    }
    ensureBacking(hwnd, windowDc_, cw, ch);
    return true;
}

void PaintBuffer::endPaint() {
    if (windowDc_ && memDc_) blit();
    if (windowDc_) {
        EndPaint(hwnd_, &ps_);
        windowDc_ = nullptr;
    }
}

void PaintBuffer::blit() {
    if (!windowDc_ || !memDc_) return;
    BitBlt(windowDc_, ps_.rcPaint.left, ps_.rcPaint.top,
           ps_.rcPaint.right - ps_.rcPaint.left,
           ps_.rcPaint.bottom - ps_.rcPaint.top, memDc_, ps_.rcPaint.left,
           ps_.rcPaint.top, SRCCOPY);
}

} // namespace ui
} // namespace sk