#include "tab_strip.h"

#include <windows.h>

#include "../theme.h"
#include "icon_atlas.h"

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
    if (selected_ < 0 || selected_ >= (int)items_.size()) selected_ = items_.empty() ? -1 : 0;
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

int TabStrip::hitTest(int x, int y) {
    POINT p{x, y};
    if (!PtInRect(&rc_, p)) return -1;
    int cur = rc_.left + 6;
    for (int i = 0; i < (int)items_.size(); ++i) {
        SIZE sz{};
        GetTextExtentPoint32A(GetDC(parent_), items_[i].text, (int)strlen(items_[i].text), &sz);
        int w = sz.cx + 20;
        RECT tr{cur, rc_.top + 2, cur + w, rc_.bottom - 2};
        if (PtInRect(&tr, p)) return i;
        cur += w + 4;
    }
    if (showPlus_) {
        RECT pr{cur, rc_.top + 4, cur + 20, rc_.bottom - 4};
        if (PtInRect(&pr, p)) return 1000; // plus
    }
    return -1;
}

void TabStrip::paint(HDC hdc) {
    HBRUSH bg = CreateSolidBrush(theme::tabBackground());
    FillRect(hdc, &rc_, bg);
    DeleteObject(bg);

    int cur = rc_.left + 6;
    HFONT ofh = static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
    for (int i = 0; i < (int)items_.size(); ++i) {
        const bool active = (i == selected_);
        SIZE sz{};
        GetTextExtentPoint32A(hdc, items_[i].text, (int)strlen(items_[i].text), &sz);
        int w = sz.cx + 20;
        int ttop = rc_.top + 2;
        int tbot = rc_.bottom - 2;
        int tleft = cur;
        int tright = cur + w;

        HBRUSH tbg = CreateSolidBrush(active ? theme::tabActive() : theme::tabHover());
        HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
        HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, tbg));
        HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
        RoundRect(hdc, tleft, ttop, tright, tbot, 8, 8);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(pen);
        DeleteObject(tbg);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, active ? theme::tabTextActive() : theme::tabText());
        RECT txt{tleft + 8, ttop, tright, tbot};
        DrawTextA(hdc, items_[i].text, -1, &txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (items_[i].closeable && active) {
            int iy = (ttop + tbot - 12) / 2;
            drawIcon(hdc, IconId::UiStop, tright - 16, iy, 12, theme::tabText());
        }
        cur += w + 4;
    }
    SelectObject(hdc, ofh);
    if (showPlus_) {
        drawIcon(hdc, IconId::UiPlus, cur, rc_.top + 4, 16, theme::tabText());
    }
}

} // namespace ui
} // namespace sk




