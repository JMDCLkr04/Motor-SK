#include "scroll_bar.h"

#include <algorithm>
#include <cmath>

#include "../theme.h"

namespace sk {
namespace ui {

ScrollBar::ScrollBar() = default;

void ScrollBar::attach(HWND parent, int id, bool vertical) {
    parent_ = parent;
    id_ = id;
    vertical_ = vertical;
}

void ScrollBar::setRange(int minVal, int maxVal) {
    min_ = minVal;
    max_ = maxVal;
    clampPos();
    update();
}

void ScrollBar::setPage(int page) {
    page_ = std::max<int>(1,page);
    clampPos();
    update();
}

void ScrollBar::setPos(int pos) {
    pos_ = pos;
    clampPos();
    update();
}

int ScrollBar::pos() const { return pos_; }

void ScrollBar::setVisible(bool v) {
    visible_ = v;
    if (parent_) InvalidateRect(parent_, &rc_, FALSE);
}

RECT ScrollBar::rect() const { return rc_; }

void ScrollBar::setRect(const RECT& r) {
    rc_ = r;
    update();
}

void ScrollBar::paint(HDC hdc) {
    if (!visible_) return;
    HBRUSH rail = CreateSolidBrush(theme::scrollRail());
    HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
    HBRUSH oldB = static_cast<HBRUSH>(SelectObject(hdc, rail));
    HPEN oldP = static_cast<HPEN>(SelectObject(hdc, pen));
    RoundRect(hdc, rc_.left, rc_.top, rc_.right, rc_.bottom, 10, 10);
    SelectObject(hdc, oldB);
    SelectObject(hdc, oldP);
    DeleteObject(pen);
    DeleteObject(rail);

    if (max_ <= min_) return;
    float ratio = thumbRatio();
    int len = vertical_ ? (rc_.bottom - rc_.top) : (rc_.right - rc_.left);
    int thumbLen = std::max<int>(10,(int)(len * ratio));
    int maxThumb = len - thumbLen;
    int track = std::max<int>(0,max_ - min_ - page_);
    int tpos = track == 0 ? 0 : (pos_ * maxThumb / track);
    RECT tr{};
    if (vertical_) {
        tr.left = rc_.left + 1;
        tr.right = rc_.right - 1;
        tr.top = rc_.top + tpos + 1;
        tr.bottom = tr.top + thumbLen - 2;
    } else {
        tr.top = rc_.top + 1;
        tr.bottom = rc_.bottom - 1;
        tr.left = rc_.left + tpos + 1;
        tr.right = tr.left + thumbLen - 2;
    }
    COLORREF tc = dragging_ ? theme::scrollThumbDrag()
                             : (hitTest(tr.left, tr.top) ? theme::scrollThumbHover()
                                                         : theme::scrollThumb());
    HBRUSH tbrush = CreateSolidBrush(tc);
    HPEN tpen = CreatePen(PS_SOLID, 1, theme::border());
    HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, tbrush));
    HPEN op = static_cast<HPEN>(SelectObject(hdc, tpen));
    RoundRect(hdc, tr.left, tr.top, tr.right, tr.bottom, 8, 8);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(tpen);
    DeleteObject(tbrush);
}

bool ScrollBar::hitTest(int x, int y) const {
    POINT p{x, y};
    return PtInRect(&rc_, p);
}

void ScrollBar::onMouseDown(int x, int y) {
    if (max_ <= min_ || !visible_) return;
    dragging_ = true;
    dragStart_ = vertical_ ? y : x;
    SetCapture(parent_);
}

void ScrollBar::onMouseUp() {
    if (dragging_) {
        ReleaseCapture();
        dragging_ = false;
        if (parent_) InvalidateRect(parent_, &rc_, FALSE);
    }
}

void ScrollBar::onMouseMove(int x, int y) {
    if (!dragging_) return;
    int mv = (vertical_ ? (y - dragStart_) : (x - dragStart_));
    dragStart_ = vertical_ ? y : x;
    int len = vertical_ ? (rc_.bottom - rc_.top) : (rc_.right - rc_.left);
    float ratio = thumbRatio();
    int thumbLen = std::max<int>(10,(int)(len * ratio));
    int maxThumb = len - thumbLen;
    int track = std::max<int>(0,max_ - min_ - page_);
    if (maxThumb == 0 || track == 0) return;
    int dpos = mv * track / maxThumb;
    pos_ += dpos;
    clampPos();
    if (onScroll_) onScroll_(pos_);
    if (parent_) InvalidateRect(parent_, &rc_, FALSE);
}

void ScrollBar::onMouseWheel(int delta, WPARAM) {
    int step = std::max<int>(1,page_ / 3);
    if (delta < 0) pos_ += step;
    else pos_ -= step;
    clampPos();
    if (onScroll_) onScroll_(pos_);
    if (parent_) InvalidateRect(parent_, &rc_, FALSE);
}

void ScrollBar::onKey(int key) {
    if (max_ <= min_) return;
    int step = std::max<int>(1,page_ / 10);
    if (key == VK_DOWN || key == VK_RIGHT) pos_ += step;
    if (key == VK_UP || key == VK_LEFT) pos_ -= step;
    if (key == VK_NEXT) pos_ += page_;
    if (key == VK_PRIOR) pos_ -= page_;
    clampPos();
    if (onScroll_) onScroll_(pos_);
    if (parent_) InvalidateRect(parent_, &rc_, FALSE);
}

void ScrollBar::update() {
    bool v = (max_ > min_ && (max_ - min_) > page_);
    if (v != visible_) {
        visible_ = v;
        if (parent_) InvalidateRect(parent_, &rc_, FALSE);
    } else {
        if (parent_) InvalidateRect(parent_, &rc_, FALSE);
    }
    clampPos();
}

void ScrollBar::recalc() {}

void ScrollBar::clampPos() {
    int track = std::max<int>(0,max_ - min_ - page_);
    if (pos_ < 0) pos_ = 0;
    if (pos_ > track) pos_ = track;
}

float ScrollBar::thumbRatio() const {
    int total = max_ - min_ + page_;
    if (total <= 0) return 1.0f;
    return (float)page_ / (float)total;
}

} // namespace ui
} // namespace sk


