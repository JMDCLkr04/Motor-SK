#include "demo_window.h"

#include <windows.h>
#include <windowsx.h>

#include <vector>

#include "../theme.h"
#include "disclosure.h"
#include "icon_atlas.h"
#include "paint_buffer.h"
#include "place_popup.h"
#include "scroll_bar.h"
#include "tab_strip.h"

namespace sk {
namespace ui {

namespace {

const WCHAR* kDemoClass = L"MotorSKUIDemo";

struct DemoState {
    PaintBuffer pb;
    ScrollBar vbar;
    TabStrip tabs;
    int scrollY = 0;
    int contentH = 2000;
};

LRESULT CALLBACK demoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DemoState* s = reinterpret_cast<DemoState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            s = new DemoState();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            s->vbar.attach(hwnd, 1001, true);
            s->vbar.setRange(0, 2000 - 600);
            s->vbar.setPage(600);
            s->tabs.attach(hwnd, 1002, true);
            s->tabs.setItems({{"Properties"}, {"Output"}});
            break;
        }
        case WM_SIZE: {
            if (s) {
                RECT r{};
                GetClientRect(hwnd, &r);
                s->vbar.setRect({r.right - 14, 60, r.right - 4, r.bottom - 10});
                s->tabs.setRect({4, 4, r.right - 4, 30});
                s->vbar.setPage(r.bottom - 70);
                s->vbar.setRange(0, s->contentH - (r.bottom - 70));
                s->vbar.update();
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            if (s) {
                s->vbar.onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
                s->scrollY = s->vbar.pos();
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            break;
        }
        case WM_PAINT: {
            if (!s) {
                DefWindowProcW(hwnd, msg, wParam, lParam);
                break;
            }
            PAINTSTRUCT ps;
            if (s->pb.beginPaint(hwnd, &ps)) {
                HDC hdc = s->pb.dc();
                RECT r{};
                GetClientRect(hwnd, &r);
                FillRect(hdc, &r, theme::backgroundBrush());

                // Dibuja contenido desplazado
                HRGN old = CreateRectRgn(0, 0, r.right - 14, r.bottom);
                SelectClipRgn(hdc, old);

                int y = 40 - s->scrollY;
                RECT dr1{10, y, 60, y + 14};
                drawDisclosure(hdc, dr1, true);
                y += 20;
                // Iconos
                drawIcon(hdc, IconId::Part, 10, y, 24, RGB(200, 200, 210));
                drawIcon(hdc, IconId::Folder, 40, y, 20, RGB(180, 180, 200));
                drawIcon(hdc, IconId::Camera, 70, y, 16, RGB(160, 160, 190));
                y += 40;
                TextOutA(hdc, 10, y, "UI Demo: scrollbar rounded, disclosure, icons tint, popup on right/up", 70);
                y += 20;
                TextOutA(hdc, 10, y, "GDI double buffer + invalid rect only", 37);
                SelectClipRgn(hdc, nullptr);
                DeleteObject(old);

                s->vbar.paint(hdc);
                s->tabs.paint(hdc);
                s->pb.endPaint();
            } else {
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (s) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                if (s->vbar.hitTest(x, y)) s->vbar.onMouseDown(x, y);
                int ht = s->tabs.hitTest(x, y);
                if (ht == 1000) {
                    RECT ar{x - 10, y - 10, x + 10, y + 10};
                    static bool pop = false;
                    if (!pop) {
                        WNDCLASSEXW wcp{};
                        wcp.cbSize = sizeof(wcp);
                        wcp.lpszClassName = L"SKPop";
                        wcp.lpfnWndProc = DefWindowProcW;
                        wcp.hbrBackground = theme::popupBackgroundBrush();
                        RegisterClassExW(&wcp);
                        HWND hpop = placePopup(hwnd, ar, 140, 80, L"SKPop", L"popup", DefWindowProcW, nullptr);
                        ShowWindow(hpop, SW_SHOW);
                        pop = true;
                    }
                } else if (ht >= 0) {
                    s->tabs.setSelected(ht);
                }
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (s) s->vbar.onMouseUp();
            break;
        }
        case WM_MOUSEMOVE: {
            if (s) s->vbar.onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY: {
            delete s;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            PostQuitMessage(0);
            break;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

} // namespace

bool runUIDemo() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = demoWndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = theme::backgroundBrush();
    wc.lpszClassName = kDemoClass;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kDemoClass, L"Motor SK UI Demo", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 900, 700, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return false;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

} // namespace ui
} // namespace sk
