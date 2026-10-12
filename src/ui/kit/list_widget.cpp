#include "list_widget.h"

#include <algorithm>

#include "../theme.h"

namespace sk {
namespace ui {

ListWidget::ListWidget() {
    vbar_.setOnScroll([this](int p){ scrollY_ = p; invalidate(); });
}

void ListWidget::create(HWND parent, int x, int y, int w, int h, int cols) {
    cols_ = cols;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSA wc{};
    if (!GetClassInfoA(inst, "MOTORSK_LISTWIDGET", &wc)) {
        wc.style = CS_VREDRAW | CS_HREDRAW;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hbrBackground = theme::listBackgroundBrush();
        wc.lpszClassName = "MOTORSK_LISTWIDGET";
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        RegisterClassA(&wc);
    }
    hwnd_ = CreateWindowA("MOTORSK_LISTWIDGET", "", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                          x, y, w, h, parent, nullptr, inst, this);
    vbar_.attach(hwnd_, 101, true);
    invalidate();
}

void ListWidget::destroy() { if (hwnd_) DestroyWindow(hwnd_); }

void ListWidget::setItems(const std::vector<std::vector<const char*>>& rows) {
    rows_.clear();
    for (auto& r : rows) {
        std::vector<std::string> v;
        for (auto c : r) v.push_back(c ? c : "");
        rows_.push_back(std::move(v));
    }
    RECT rc; GetClientRect(hwnd_, &rc);
    vbar_.setPage(rc.bottom);
    vbar_.setRange(0, std::max<int>(0, (int)rows_.size() * rowh_ - rc.bottom));
    vbar_.update();
    invalidate();
}

void ListWidget::setColumnWidths(const std::vector<int>& w) { colw_ = w; invalidate(); }

void ListWidget::invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE); }

int ListWidget::hitRow(int y) { return (y + scrollY_) / rowh_; }

LRESULT CALLBACK ListWidget::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ListWidget* self = reinterpret_cast<ListWidget*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<ListWidget*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcA(hwnd, msg, wParam, lParam);
    switch (msg) {
        case WM_SIZE: {
            RECT r; GetClientRect(hwnd, &r);
            self->vbar_.setRect({r.right - 12, 0, r.right, r.bottom});
            self->vbar_.setPage(r.bottom);
            self->vbar_.setRange(0, std::max<int>(0, (int)self->rows_.size() * self->rowh_ - r.bottom));
            self->vbar_.update();
            break;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            if (self->pb_.beginPaint(hwnd, &ps)) {
                self->paint(self->pb_.dc());
                self->pb_.endPaint();
            } else { BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); }
            break;
        }
        case WM_MOUSEWHEEL: {
            self->vbar_.onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            self->scrollY_ = self->vbar_.pos();
            self->invalidate();
            break;
        }
        default: return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void ListWidget::paint(HDC hdc) {
    RECT r; GetClientRect(hwnd_, &r);
    FillRect(hdc, &r, theme::listBackgroundBrush());
    int y = -scrollY_;
    for (size_t i = 0; i < rows_.size() && y < r.bottom; ++i) {
        if (y + rowh_ > 0) {
            RECT tr{0, y, r.right - 12, y + rowh_};
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, theme::text());
            HFONT of = (HFONT)SelectObject(hdc, theme::uiFont());
            int x = 4;
            int cw = colw_.empty() ? (r.right / cols_) : colw_[0];
            std::string txt = rows_[i].empty() ? std::string("") : rows_[i][0];
            TextOutA(hdc, x, y + 4, txt.c_str(), (int)txt.size());
            SelectObject(hdc, of);
        }
        y += rowh_;
    }
    vbar_.paint(hdc);
}

} // namespace ui
} // namespace sk


