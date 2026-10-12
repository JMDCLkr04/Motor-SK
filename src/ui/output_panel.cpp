#include "output_panel.h"

#include <windowsx.h>

#include <algorithm>

#include "theme.h"
#include "kit/kit.h"

namespace sk {
namespace ui {

namespace {

const wchar_t* kOutputClass = L"MOTORSK_OUTPUTPANEL";

COLORREF levelColor(int level) {
    switch (level) {
        case 1:  return RGB(240, 200, 100);  // aviso
        case 2:  return RGB(240, 120, 120);  // error
        default: return theme::text();       // info
    }
}

COLORREF levelTag(int level) {
    switch (level) {
        case 1:  return RGB(214, 168, 62);
        case 2:  return RGB(214, 96, 96);
        default: return RGB(120, 170, 240);
    }
}

const wchar_t* levelName(int level) {
    switch (level) {
        case 1:  return L"AVISO";
        case 2:  return L"ERROR";
        default: return L"INFO";
    }
}

} // namespace

OutputPanel::OutputPanel() {
    vbar_.setOnScroll([this](int p) {
        scrollY_ = p;
        stick_ = (p >= maxScrollPos());
        invalidate();
    });
}

OutputPanel::~OutputPanel() { destroy(); }

void OutputPanel::create(HWND parent, int id) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(inst, kOutputClass, &wc)) {
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.hbrBackground = theme::listBackgroundBrush();
        wc.lpszClassName = kOutputClass;
        RegisterClassExW(&wc);
    }
    hwnd_ = CreateWindowExW(0, kOutputClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0,
                            0, 100, 100, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            inst, this);
    if (hwnd_) {
        vbar_.attach(hwnd_, 1, true);
        layout();
        invalidate();
    }
}

void OutputPanel::destroy() {
    if (hwnd_) {
        vbar_.attach(nullptr, 0, true);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void OutputPanel::setRect(const RECT& r) {
    if (!hwnd_) return;
    MoveWindow(hwnd_, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    layout();
    invalidate();
}

void OutputPanel::invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

int OutputPanel::maxScrollPos() const {
    if (!hwnd_) return 0;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int view = std::max<int>(0, static_cast<int>(rc.bottom) - kToolbarH);
    const int content = static_cast<int>(buffer_.size()) * kLineH;
    return std::max<int>(0, content - view);
}

void OutputPanel::layout() {
    if (!hwnd_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int view = std::max<int>(0, static_cast<int>(rc.bottom) - kToolbarH);
    const int content = static_cast<int>(buffer_.size()) * kLineH;
    const int maxPos = std::max<int>(0, content - view);
    if (scrollY_ > maxPos) scrollY_ = maxPos;
    if (scrollY_ < 0) scrollY_ = 0;
    vbar_.setRect(RECT{rc.right - kBarW, kToolbarH, rc.right, rc.bottom});
    vbar_.setPage(view);
    vbar_.setRange(0, content);
    vbar_.setPos(scrollY_);
    vbar_.update();
}

void OutputPanel::updateScroll(bool toEnd) {
    layout();
    if (toEnd) {
        scrollY_ = maxScrollPos();
        vbar_.setPos(scrollY_);
    }
}

void OutputPanel::addLine(int level, const std::string& text) {
    buffer_.add(level, text);
    if (stick_) {
        updateScroll(true);
    } else {
        layout();
    }
    invalidate();
}

void OutputPanel::clear() {
    buffer_.clear();
    scrollY_ = 0;
    stick_ = true;
    layout();
    invalidate();
}

void OutputPanel::trackLeave() {
    if (hwnd_ && !trackingLeave_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        trackingLeave_ = true;
    }
}

RECT OutputPanel::clearRect() const {
    RECT rc{};
    if (hwnd_) GetClientRect(hwnd_, &rc);
    RECT out{};
    out.right = rc.right - kBarW - 6;
    out.left = out.right - 74;
    out.top = 3;
    out.bottom = kToolbarH - 3;
    return out;
}

void OutputPanel::paint(HDC hdc) {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    FillRect(hdc, &rc, theme::listBackgroundBrush());

    // Barra superior con el boton "Limpiar".
    RECT tb{0, 0, rc.right, kToolbarH};
    FillRect(hdc, &tb, theme::headerBackgroundBrush());
    HPEN sep = CreatePen(PS_SOLID, 1, theme::border());
    HPEN osep = static_cast<HPEN>(SelectObject(hdc, sep));
    MoveToEx(hdc, 0, kToolbarH - 1, nullptr);
    LineTo(hdc, rc.right, kToolbarH - 1);
    SelectObject(hdc, osep);
    DeleteObject(sep);

    const RECT cr = clearRect();
    HBRUSH cb = CreateSolidBrush(hoverClear_ ? theme::surfaceHover()
                                             : theme::surface());
    HPEN cp = CreatePen(PS_SOLID, 1, theme::border());
    HBRUSH ocb = static_cast<HBRUSH>(SelectObject(hdc, cb));
    HPEN ocp = static_cast<HPEN>(SelectObject(hdc, cp));
    RoundRect(hdc, cr.left, cr.top, cr.right, cr.bottom, 10, 10);
    SelectObject(hdc, ocb);
    SelectObject(hdc, ocp);
    DeleteObject(cp);
    DeleteObject(cb);

    const bool empty = buffer_.size() == 0;
    HFONT of = static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, empty ? theme::textDisabled() : theme::text());
    {
        RECT lr = cr;
        DrawTextW(hdc, L"Limpiar", -1, &lr,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Lista virtualizada.
    const int body = rc.right - (vbar_.visible() ? kBarW : 0);
    const int save = SaveDC(hdc);
    IntersectClipRect(hdc, 0, kToolbarH, body, rc.bottom);

    if (empty) {
        RECT er{8, kToolbarH + 4, body - 8, kToolbarH + 4 + kLineH};
        DrawTextW(hdc, L"Sin mensajes.", -1, &er,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    const int first = scrollY_ / kLineH;
    for (int i = first; i < static_cast<int>(buffer_.size()); ++i) {
        const int y = kToolbarH + i * kLineH - scrollY_;
        if (y >= rc.bottom) break;
        const OutputLine& line = buffer_.at(static_cast<size_t>(i));

        RECT tagr{6, y, 48, y + kLineH};
        SetTextColor(hdc, levelTag(line.level));
        DrawTextW(hdc, levelName(line.level), -1, &tagr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT tr{52, y, body - 6, y + kLineH};
        SetTextColor(hdc, levelColor(line.level));
        std::wstring w = widen(line.text);
        DrawTextW(hdc, w.c_str(), static_cast<int>(w.size()), &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                      DT_NOPREFIX);
    }
    RestoreDC(hdc, save);
    SelectObject(hdc, of);

    vbar_.paint(hdc);
}

LRESULT CALLBACK OutputPanel::wndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam) {
    OutputPanel* self = reinterpret_cast<OutputPanel*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OutputPanel*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_SIZE:
            self->layout();
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
        case WM_MOUSEWHEEL:
            self->vbar_.onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            break;
        case WM_MOUSEMOVE: {
            self->trackLeave();
            self->vbar_.onMouseMove(GET_X_LPARAM(lParam),
                                    GET_Y_LPARAM(lParam));
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const RECT cr = self->clearRect();
            const bool hov = x >= cr.left && x < cr.right && y >= cr.top &&
                             y < cr.bottom;
            if (hov != self->hoverClear_) {
                self->hoverClear_ = hov;
                self->invalidate();
            }
            break;
        }
        case WM_MOUSELEAVE:
            self->trackingLeave_ = false;
            if (self->hoverClear_) {
                self->hoverClear_ = false;
                self->invalidate();
            }
            break;
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (self->vbar_.hitTest(x, y)) {
                self->vbar_.onMouseDown(x, y);
            } else {
                const RECT cr = self->clearRect();
                if (x >= cr.left && x < cr.right && y >= cr.top &&
                    y < cr.bottom) {
                    self->clear();
                }
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

} // namespace ui
} // namespace sk
