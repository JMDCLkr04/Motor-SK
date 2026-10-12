#include "tab_strip.h"

#include <windows.h>

#include "../theme.h"
#include "icon_atlas.h"
#include "kit.h"

namespace sk {
namespace ui {

TabStrip::TabStrip() = default;

void TabStrip::attach(HWND parent, int id, bool showPlus) {
    parent_ = parent;
    id_ = id;
    showPlus_ = showPlus;
}

void TabStrip::setItems(const std::vector<TabStripItem>& items) {
    items_ = items;
    if (selected_ < 0 || selected_ >= (int)items_.size())
        selected_ = items_.empty() ? -1 : 0;
    if (parent_) InvalidateRect(parent_, &rc_, FALSE);
}

void TabStrip::addItem(const TabStripItem& item) {
    items_.push_back(item);
    if (parent_) InvalidateRect(parent_, &rc_, FALSE);
}

void TabStrip::setSelected(int index) {
    if (index >= 0 && index < (int)items_.size()) {
        selected_ = index;
        if (onSelected_) onSelected_(index);
        if (parent_) InvalidateRect(parent_, &rc_, FALSE);
    }
}

int TabStrip::selected() const { return selected_; }

void TabStrip::setRect(const RECT& r) {
    rc_ = r;
    if (parent_) InvalidateRect(parent_, &rc_, FALSE);
}

void TabStrip::recomputeLayout() {
    xs_.clear();
    ws_.clear();
    HDC dc = GetDC(parent_ ? parent_ : nullptr);
    HFONT oldF = dc ? static_cast<HFONT>(SelectObject(dc, theme::uiFont()))
                    : nullptr;
    int W = 0;
    for (auto& it : items_) {
        SIZE s{};
        std::wstring w = widen(it.text);
        GetTextExtentPoint32W(dc, w.c_str(), (int)w.size(), &s);
        int tw = s.cx + 24 + (it.closeable ? 16 : 0);
        ws_.push_back(tw);
        W += tw + 4;
    }
    if (dc) {
        SelectObject(dc, oldF);
        ReleaseDC(parent_, dc);
    }

    const int avail = (rc_.right - rc_.left) - 6 - (showPlus_ ? 20 : 0);
    if (W > avail) {
        if (offset_ > W - avail) offset_ = W - avail;
        if (offset_ < 0) offset_ = 0;
    } else {
        offset_ = 0;
    }

    int x = rc_.left + 6 - offset_;
    xs_.reserve(items_.size());
    for (size_t i = 0; i < items_.size(); ++i) {
        xs_.push_back(x);
        x += ws_[i] + 4;
    }
    plusX_ = rc_.right - 20;
}

TabHitResult TabStrip::hitTest(int x, int y) {
    POINT p{x, y};
    if (!PtInRect(&rc_, p)) return {};
    recomputeLayout();
    const int midy = rc_.top + (rc_.bottom - rc_.top) / 2;
    for (int i = 0; i < (int)items_.size(); ++i) {
        RECT tr{xs_[i], rc_.top + 2, xs_[i] + ws_[i], rc_.bottom - 2};
        if (!PtInRect(&tr, p)) continue;
        if (items_[i].closeable) {
            RECT cr{xs_[i] + ws_[i] - 20, midy - 7, xs_[i] + ws_[i] - 8,
                    midy + 7};
            if (PtInRect(&cr, p)) return {TabHit::Close, i, cr};
        }
        return {TabHit::Tab, i, {}};
    }
    if (showPlus_) {
        RECT pr{plusX_, rc_.top + 4, plusX_ + 16, rc_.bottom - 4};
        if (PtInRect(&pr, p)) return {TabHit::Plus, -1, {}};
    }
    return {};
}

void TabStrip::paint(HDC hdc) {
    recomputeLayout();
    HBRUSH bg = CreateSolidBrush(theme::tabBackground());
    FillRect(hdc, &rc_, bg);
    DeleteObject(bg);

    HBRUSH ofb = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    HPEN ofp = static_cast<HPEN>(SelectObject(hdc, GetStockObject(NULL_PEN)));

    HFONT oldF = static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
    SetBkMode(hdc, TRANSPARENT);

    const int midy = rc_.top + (rc_.bottom - rc_.top) / 2;
    for (int i = 0; i < (int)items_.size(); ++i) {
        const bool active = (i == selected_);
        const bool hover = (i == hover_);
        const int tleft = xs_[i];
        const int tright = tleft + ws_[i];
        const int ttop = rc_.top + 2;
        const int tbot = rc_.bottom - 2;

        COLORREF fill = active ? theme::tabActive()
                               : (hover ? theme::tabHover()
                                        : theme::boxBackground());
        HBRUSH tbg = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
        SelectObject(hdc, tbg);
        SelectObject(hdc, pen);
        RoundRect(hdc, tleft, ttop, tright, tbot, 8, 8);
        SelectObject(hdc, ofb);
        SelectObject(hdc, ofp);
        DeleteObject(pen);
        DeleteObject(tbg);

        if (active) {
            HBRUSH bar = CreateSolidBrush(theme::accent());
            RECT under{tleft + 4, tbot - 3, tright - 4, tbot};
            FillRect(hdc, &under, bar);
            DeleteObject(bar);
        }

        SetTextColor(hdc, active ? theme::tabTextActive() : theme::tabText());
        RECT txt{tleft + 8, ttop, tright - (items_[i].closeable ? 22 : 6), tbot};
        std::wstring w = widen(items_[i].text);
        DrawTextW(hdc, w.c_str(), (int)w.size(), &txt,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (items_[i].closeable) {
            const bool ch = hover && hoverClose_;
            drawIcon(hdc, IconId::UiStop, xs_[i] + ws_[i] - 18, midy - 6, 12,
                     ch ? theme::text() : RGB(150, 150, 160));
        }
    }

    if (showPlus_) {
        drawIcon(hdc, IconId::UiPlus, plusX_, rc_.top + 4, 16,
                 hoverPlus_ ? theme::text() : theme::tabText());
    }

    SelectObject(hdc, oldF);
    SelectObject(hdc, ofb);
    SelectObject(hdc, ofp);
}

void TabStrip::onMouseMove(int x, int y) {
    const int prevHover = hover_;
    const bool prevClose = hoverClose_;
    const bool prevPlus = hoverPlus_;
    TabHitResult h = hitTest(x, y);
    hover_ = (h.kind == TabHit::Tab || h.kind == TabHit::Close) ? h.index : -1;
    hoverClose_ = (h.kind == TabHit::Close);
    hoverPlus_ = (h.kind == TabHit::Plus);
    if (hover_ != prevHover || hoverClose_ != prevClose || hoverPlus_ != prevPlus) {
        if (parent_) InvalidateRect(parent_, &rc_, FALSE);
    }
}

void TabStrip::onMouseLeave() {
    if (hover_ >= 0 || hoverClose_ || hoverPlus_) {
        hover_ = -1;
        hoverClose_ = false;
        hoverPlus_ = false;
        if (parent_) InvalidateRect(parent_, &rc_, FALSE);
    }
}

bool TabStrip::onMouseDown(int x, int y) {
    TabHitResult h = hitTest(x, y);
    switch (h.kind) {
        case TabHit::Close:
            if (onClose_) onClose_(h.index);
            if (parent_) InvalidateRect(parent_, &rc_, FALSE);
            return true;
        case TabHit::Plus:
            if (onPlus_) onPlus_();
            if (parent_) InvalidateRect(parent_, &rc_, FALSE);
            return true;
        case TabHit::Tab:
            setSelected(h.index);
            return true;
        default:
            return false;
    }
}

void TabStrip::onMouseWheel(int delta) {
    recomputeLayout();
    offset_ += delta > 0 ? -24 : 24;
    recomputeLayout(); // clampa contra el ancho util
    if (parent_) InvalidateRect(parent_, &rc_, FALSE);
}

} // namespace ui
} // namespace sk