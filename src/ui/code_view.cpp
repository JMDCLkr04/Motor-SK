#include "code_view.h"

#include <windows.h>

#include "../core/log.h"
#include "../editor/layout.h"
#include "theme.h"

namespace sk {

namespace {

const char* kPanelClass = "MotorSKCodePanel";

constexpr int kHeaderHeight = 36;

// Panel central de CODE. Cabecera "Codigo" y, centrado, el aviso del
// editor pendiente. Sin controles: el organizador de scripts vive en el
// Explorer compartido y el editor de texto llega en un paso posterior.
void paintPanel(HDC hdc, const RECT& rc) {
    FillRect(hdc, &rc, theme::backgroundBrush());

    RECT header{0, 0, rc.right, kHeaderHeight};
    FillRect(hdc, &header, theme::headerBackgroundBrush());

    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, theme::uiHeaderFont()));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, theme::text());
    RECT textRc{12, 0, rc.right, kHeaderHeight};
    DrawTextA(hdc, "Codigo", -1, &textRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);

    HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    MoveToEx(hdc, 0, kHeaderHeight, nullptr);
    LineTo(hdc, rc.right, kHeaderHeight);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    // Aviso central (gris, mismo tono deshabilitado del tema).
    RECT body{kHeaderHeight, kHeaderHeight, rc.right, rc.bottom};
    HFONT oldFont2 = static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
    SetTextColor(hdc, theme::textDisabled());
    DrawTextA(hdc, "Editor de codigo: proximamente", -1, &body,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldFont2);
}

} // namespace

long long __stdcall CodeView::wndProc(void* hwndPtr, unsigned int msg,
                                      unsigned long long wParam, long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        auto* self = static_cast<CodeView*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            paintPanel(hdc, rc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1; // pinta WM_PAINT (evita parpadeo)
        default:
            break;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

bool CodeView::create(void* parentHwnd) {
    parent_ = parentHwnd;

    static bool registered = false;
    if (!registered) {
        HINSTANCE inst = GetModuleHandleW(nullptr);

        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&CodeView::wndProc);
        wc.hInstance = inst;
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = theme::backgroundBrush();
        wc.lpszClassName = kPanelClass;
        RegisterClassExA(&wc);

        registered = true;
    }

    HWND parent = static_cast<HWND>(parent_);
    RECT rc{};
    GetClientRect(parent, &rc);
    const editor::Layout layout =
        editor::Layout::compute(rc.right, rc.bottom);

    hwnd_ = CreateWindowExA(0, kPanelClass, "Codigo", WS_CHILD | WS_CLIPCHILDREN,
                            layout.viewport.x, layout.viewport.y,
                            layout.viewport.w, layout.viewport.h, parent,
                            nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        SK_ERROR("CodeView: CreateWindowEx fallo (error %lu)", GetLastError());
        return false;
    }
    SK_INFO("Division CODE lista (area central del editor)");
    return true;
}

void CodeView::destroy() {
    project_ = nullptr;
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
}

void CodeView::open(const Project& project) { project_ = &project; }

void CodeView::close() { project_ = nullptr; }

void CodeView::setVisible(bool visible) {
    visible_ = visible;
    if (hwnd_) {
        ShowWindow(static_cast<HWND>(hwnd_), visible ? SW_SHOW : SW_HIDE);
    }
}

void CodeView::resize(int width, int height) {
    if (!hwnd_) return;
    const editor::Layout layout = editor::Layout::compute(width, height);
    MoveWindow(static_cast<HWND>(hwnd_), layout.viewport.x, layout.viewport.y,
               layout.viewport.w, layout.viewport.h, TRUE);
}

} // namespace sk
