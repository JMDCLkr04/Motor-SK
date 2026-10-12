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

bool PaintBuffer::beginPaint(HWND hwnd, PAINTSTRUCT* ps) {
    windowDc_ = BeginPaint(hwnd, &ps_);
    if (ps) *ps = ps_;
    rc_ = ps_.rcPaint;
    GetClientRect(hwnd, &rc_);
    memDc_ = CreateCompatibleDC(windowDc_);
    bmp_ = CreateCompatibleBitmap(windowDc_, rc_.right - rc_.left, rc_.bottom - rc_.top);
    oldBmp_ = static_cast<HBITMAP>(SelectObject(memDc_, bmp_));
    SetViewportOrgEx(memDc_, -ps_.rcPaint.left, -ps_.rcPaint.top, nullptr);
    return true;
}

void PaintBuffer::endPaint() {
    SetViewportOrgEx(memDc_, 0, 0, nullptr);
    if (windowDc_ && memDc_) {
        BitBlt(windowDc_, ps_.rcPaint.left, ps_.rcPaint.top, ps_.rcPaint.right - ps_.rcPaint.left,
               ps_.rcPaint.bottom - ps_.rcPaint.top, memDc_, ps_.rcPaint.left, ps_.rcPaint.top, SRCCOPY);
    }
    if (memDc_) {
        SelectObject(memDc_, oldBmp_);
        DeleteDC(memDc_);
        memDc_ = nullptr;
        oldBmp_ = nullptr;
    }
    if (bmp_) {
        DeleteObject(bmp_);
        bmp_ = nullptr;
    }
    if (windowDc_) {
        EndPaint(reinterpret_cast<HWND>(nullptr), &ps_);
        windowDc_ = nullptr;
    }
}

void PaintBuffer::blit() {
    if (windowDc_ && memDc_) {
        BitBlt(windowDc_, ps_.rcPaint.left, ps_.rcPaint.top, ps_.rcPaint.right - ps_.rcPaint.left,
               ps_.rcPaint.bottom - ps_.rcPaint.top, memDc_, ps_.rcPaint.left, ps_.rcPaint.top, SRCCOPY);
    }
}

} // namespace ui
} // namespace sk
