#pragma once

#include <windows.h>
#include <functional>

namespace sk {
namespace ui {

// Barra de scroll vertical/horizontal redondeada, dibujada a mano.
// Sin flechas. Visible solo cuando hay desbordamiento.
class ScrollBar {
public:
    ScrollBar();

    void attach(HWND parent, int id, bool vertical);
    void setRange(int minVal, int maxVal);
    void setPage(int page);
    void setPos(int pos);
    int pos() const;
    void setOnScroll(std::function<void(int)> cb) { onScroll_ = std::move(cb); }

    bool visible() const { return visible_; }
    bool dragging() const { return dragging_; }
    void setVisible(bool v);
    RECT rect() const;
    void setRect(const RECT& r);

    void paint(HDC hdc);
    bool hitTest(int x, int y) const;
    void onMouseDown(int x, int y);
    void onMouseUp();
    void onMouseMove(int x, int y);
    void onMouseWheel(int delta, WPARAM flags = 0);
    void onKey(int key);
    // El capture se perdio (WM_CAPTURECHANGED): abandonar el arrastre.
    void onCaptureChanged();
    void update();

private:
    void recalc();
    void clampPos();
    float thumbRatio() const;
    // Hay desbordamiento (rango mayor que la pagina): solo entonces se
    // dibuja y se permite arrastrar el pulgar.
    bool overflowing() const;

    HWND parent_ = nullptr;
    int id_ = 0;
    bool vertical_ = true;
    int min_ = 0;
    int max_ = 0;
    int page_ = 1;
    int pos_ = 0;
    bool visible_ = true;
    RECT rc_ = {};
    bool dragging_ = false;
    int dragStart_ = 0;
    int dragOffset_ = 0;
    std::function<void(int)> onScroll_;
};

} // namespace ui
} // namespace sk
