#include "list_widget.h"

#include <windowsx.h>

#include <algorithm>

#include "../theme.h"
#include "kit.h"

namespace sk {
namespace ui {

namespace {
const wchar_t* kListClass = L"MOTORSK_LISTWIDGET";
COLORREF rowHover() { return RGB(44, 44, 52); }
COLORREF rowSelected() { return RGB(56, 88, 150); }
COLORREF cellSeparator() { return RGB(30, 30, 36); }
COLORREF headerText() { return RGB(200, 200, 210); }
} // namespace

ListWidget::ListWidget() {
    vbar_.setOnScroll([this](int p) { scrollY_ = p; invalidate(); });
}

void ListWidget::create(HWND parent, int x, int y, int w, int h) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(inst, kListClass, &wc)) {
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.hbrBackground = theme::listBackgroundBrush();
        wc.lpszClassName = kListClass;
        RegisterClassExW(&wc);
    }
    hwnd_ = CreateWindowExW(0, kListClass, L"",
                            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, x, y, w, h,
                            parent, nullptr, inst, this);
    vbar_.attach(hwnd_, 101, true);
    layoutVBar();
    invalidate();
}

void ListWidget::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void ListWidget::setColumns(const std::vector<std::string>& names,
                            const std::vector<int>& widths) {
    header_ = names;
    colw_ = widths;
    invalidate();
}

void ListWidget::setColumnWidths(const std::vector<int>& w) {
    colw_ = w;
    invalidate();
}

void ListWidget::setItems(const std::vector<std::vector<std::string>>& rows) {
    rows_ = rows;
    if (selected_ >= (int)rows_.size()) selected_ = -1;
    if (hoverRow_ >= (int)rows_.size()) hoverRow_ = -1;
    layoutVBar();
    invalidate();
}

void ListWidget::setRowHeight(int h) {
    rowh_ = std::max<int>(12, h);
    layoutVBar();
    invalidate();
}

void ListWidget::setSelectedRow(int row) {
    if (row < 0) {
        if (selected_ != -1) {
            selected_ = -1;
            invalidate();
        }
        return;
    }
    if (row < 0 || row >= (int)rows_.size()) return;
    const int prev = selected_;
    selected_ = row;
    if (prev != selected_) {
        invalidateRow(prev);
        invalidateRow(selected_);
    }
    ensureVisible(row);
    if (onClick_) onClick_(row);
}

void ListWidget::invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ListWidget::invalidateRow(int row) {
    if (!hwnd_ || row < 0) return;
    RECT r;
    GetClientRect(hwnd_, &r);
    const int rowY = headerH_ + row * rowh_ - scrollY_;
    RECT rr{0, rowY, r.right, rowY + rowh_};
    InvalidateRect(hwnd_, &rr, FALSE);
}

void ListWidget::trackLeave() {
    if (hwnd_ && !trackingLeave_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        trackingLeave_ = true;
    }
}

void ListWidget::layoutVBar() {
    if (!hwnd_) return;
    RECT r;
    GetClientRect(hwnd_, &r);
    vbar_.setRect({r.right - 12, 0, r.right, r.bottom});
    vbar_.setPage(r.bottom - headerH_);
    vbar_.setRange(0, std::max<int>(0, (int)rows_.size() * rowh_ -
                                          (r.bottom - headerH_)));
    vbar_.update();
}

void ListWidget::ensureVisible(int row) {
    if (!hwnd_ || row < 0) return;
    RECT r;
    GetClientRect(hwnd_, &r);
    const int view = r.bottom - headerH_;
    if (view <= 0) return;
    const int top = row * rowh_;
    const int bot = (row + 1) * rowh_;
    if (scrollY_ > top) {
        vbar_.setPos(top);
    } else if (scrollY_ + view < bot) {
        vbar_.setPos(bot - view);
    }
}

int ListWidget::hitRow(int y) const {
    const int y2 = y - headerH_;
    if (y2 < 0) return -1;
    return (y2 + scrollY_) / rowh_;
}

LRESULT CALLBACK ListWidget::wndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {
    ListWidget* self =
        reinterpret_cast<ListWidget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ListWidget*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_SIZE:
            self->layoutVBar();
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            if (self->pb_.beginPaint(hwnd, &ps)) {
                self->paint(self->pb_.dc());
                self->pb_.endPaint();
            } else {
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            self->vbar_.onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            self->scrollY_ = self->vbar_.pos();
            self->invalidate();
            break;
        }
        case WM_MOUSEMOVE: {
            self->trackLeave();
            self->vbar_.onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            const int row = self->hitRow(GET_Y_LPARAM(lParam));
            if (row != self->hoverRow_) {
                self->invalidateRow(self->hoverRow_);
                self->hoverRow_ = row;
                self->invalidateRow(self->hoverRow_);
            }
            break;
        }
        case WM_MOUSELEAVE:
            self->trackingLeave_ = false;
            self->invalidateRow(self->hoverRow_);
            self->hoverRow_ = -1;
            break;
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (self->vbar_.hitTest(x, y)) {
                self->vbar_.onMouseDown(x, y);
            } else {
                self->setSelectedRow(self->hitRow(y));
            }
            break;
        }
        case WM_LBUTTONDBLCLK: {
            const int row = self->hitRow(GET_Y_LPARAM(lParam));
            if (row >= 0 && row < (int)self->rows_.size() && self->onDoubleClick_) {
                self->onDoubleClick_(row);
            }
            break;
        }
        case WM_LBUTTONUP:
            self->vbar_.onMouseUp();
            break;
        case WM_CAPTURECHANGED:
            self->vbar_.onCaptureChanged();
            break;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void ListWidget::paint(HDC hdc) {
    RECT r;
    GetClientRect(hwnd_, &r);
    FillRect(hdc, &r, theme::listBackgroundBrush());

    const int vw = vbar_.visible() ? 12 : 0;
    const int body = r.right - vw;
    const int ncols = (int)header_.size();
    auto colX = [&](int j) {
        if (colw_.empty() || ncols == 0) return 0;
        int x = 0;
        for (int k = 0; k < j; ++k) x += colw_[k];
        return x;
    };

    // --- Cabecera: fondo gris oscuro, esquinas redondeadas, separadores ---
    HBRUSH hbg = CreateSolidBrush(theme::headerBackground());
    HPEN hpen = CreatePen(PS_SOLID, 1, theme::border());
    HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, hbg));
    HPEN op = static_cast<HPEN>(SelectObject(hdc, hpen));
    RoundRect(hdc, 0, 0, body - 1, headerH_, 8, 8);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(hpen);
    DeleteObject(hbg);

    HFONT of = static_cast<HFONT>(SelectObject(hdc, theme::uiHeaderFont()));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, headerText());
    for (int j = 0; j < ncols; ++j) {
        const int x0 = colX(j);
        const int x1 = (j + 1 < ncols) ? colX(j + 1) : body;
        RECT cr{x0 + 6, 0, x1 - 2, headerH_};
        std::wstring t = widen(header_[j]);
        DrawTextW(hdc, t.c_str(), (int)t.size(), &cr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (j + 1 < ncols) {
            HPEN sep = CreatePen(PS_SOLID, 1, theme::border());
            HPEN osep = static_cast<HPEN>(SelectObject(hdc, sep));
            MoveToEx(hdc, x1, 3, nullptr);
            LineTo(hdc, x1, headerH_ - 3);
            SelectObject(hdc, osep);
            DeleteObject(sep);
        }
    }
    SelectObject(hdc, of);

    // --- Filas (todas las columnas) ---
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, theme::uiFont());
    const int first = scrollY_ / rowh_;
    for (int i = first; i < (int)rows_.size(); ++i) {
        const int y = headerH_ + i * rowh_ - scrollY_;
        if (y >= r.bottom) break;

        RECT fr{0, y, body - 1, y + rowh_};
        if (i == selected_) {
            HBRUSH sb = CreateSolidBrush(rowSelected());
            FillRect(hdc, &fr, sb);
            DeleteObject(sb);
        } else if (i == hoverRow_) {
            HBRUSH hb2 = CreateSolidBrush(rowHover());
            FillRect(hdc, &fr, hb2);
            DeleteObject(hb2);
        }

        SetTextColor(hdc,
                     (i == selected_) ? RGB(240, 240, 246) : theme::text());
        HPEN rowline = CreatePen(PS_SOLID, 1, cellSeparator());
        HPEN orow = static_cast<HPEN>(SelectObject(hdc, rowline));
        MoveToEx(hdc, 1, fr.bottom - 1, nullptr);
        LineTo(hdc, body - 2, fr.bottom - 1);
        SelectObject(hdc, orow);
        DeleteObject(rowline);

        for (int j = 0; j < (int)rows_[i].size(); ++j) {
            const int x0 = colX(j);
            const int x1 = (j + 1 < ncols) ? colX(j + 1) : body;
            RECT cr{x0 + 6, y + 2, x1 - 4, y + rowh_ - 2};
            std::wstring t = widen(rows_[i][j]);
            ExtTextOutW(hdc, cr.left, y + 4, ETO_CLIPPED, &cr, t.c_str(),
                        (int)t.size(), nullptr);
        }
    }

    vbar_.paint(hdc);
}

} // namespace ui
} // namespace sk