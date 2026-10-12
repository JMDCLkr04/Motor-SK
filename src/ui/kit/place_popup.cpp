#include "place_popup.h"

#include <windows.h>

namespace sk {
namespace ui {

HWND placePopup(HWND parent, const RECT& anchorRect, int width, int height,
                const WCHAR* wndClass, const WCHAR* title, WNDPROC proc,
                void* userData) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wndClass, title,
                                WS_POPUP | WS_BORDER, 0, 0, width, height,
                                parent, nullptr, inst, userData);
    if (!hwnd) return nullptr;

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(parent ? parent : hwnd, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(mon, &mi);
    RECT work = mi.rcWork;

    // Intentar a la derecha y debajo del anclaje.
    int x = anchorRect.right + 2;
    int y = anchorRect.bottom + 2;
    if (x + width > work.right) {
        x = anchorRect.left - width - 2;
        if (x < work.left) x = work.right - width;
    }
    if (y + height > work.bottom) {
        y = anchorRect.top - height - 2;
        if (y < work.top) y = work.bottom - height;
    }
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    if (x + width > work.right) x = work.right - width;
    if (y + height > work.bottom) y = work.bottom - height;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    return hwnd;
}

} // namespace ui
} // namespace sk
