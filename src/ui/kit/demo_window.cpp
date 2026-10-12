#include "demo_window.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../theme.h"
#include "disclosure.h"
#include "icon_atlas.h"
#include "kit.h"
#include "list_widget.h"
#include "paint_buffer.h"
#include "place_popup.h"
#include "scroll_bar.h"
#include "tab_strip.h"

namespace sk {
namespace ui {

namespace {

const WCHAR* kDemoClass = L"MotorSKUIDemo";

struct DemoRow {
    std::string label;
    IconId icon = IconId::Part;
    bool header = false;
    bool expanded = false;
};

struct DemoArea {
    int x = 0, y = 0, w = 0, h = 0; // zona de contenido derecho
};

struct DemoState {
    PaintBuffer pb;
    ScrollBar vbar;
    TabStrip tabs;
    ListWidget list;
    int scrollY = 0;
    int rowH = 26;
    int hoverRow = -1;
    bool trackingLeave = false;
    std::vector<DemoRow> rows;
    DemoArea area;
};

void buildRows(DemoState* s) {
    s->rows = {
        {"Descripción", IconId::UiBook, true, true},
        {"Organización", IconId::CatOrganization, true, true},
        {"Elemento de Part", IconId::Part, false, false},
        {"Archivo Principal", IconId::Folder, false, false},
        {"Cámara en Tercera Persona", IconId::Camera, false, false},
        {"Luz Volumétrica", IconId::Lighting, false, false},
        {"Espacio Dimensional", IconId::Dimension, false, false},
        {"Velocidad Angular", IconId::UiStop, false, false},
        {"Configuración Avanzada", IconId::Model, false, false},
    };
}

HBRUSH rightBg() { return theme::boxBackgroundBrush(); }

void invalidateRightRow(HWND hwnd, const DemoState* s, int row) {
    if (row < 0 || row >= (int)s->rows.size()) return;
    RECT r{s->area.x - 2, s->area.y + row * s->rowH - s->scrollY,
           s->area.x + s->area.w + 2,
           s->area.y + row * s->rowH - s->scrollY + s->rowH};
    r.top -= 1;
    r.bottom += 1;
    InvalidateRect(hwnd, &r, FALSE);
}

int hitRightRow(const DemoState* s, int y) {
    if (y < s->area.y || y >= s->area.y + s->area.h) return -1;
    const int row = (y - s->area.y + s->scrollY) / s->rowH;
    if (row < 0 || row >= (int)s->rows.size()) return -1;
    return row;
}

LRESULT CALLBACK demoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DemoState* s =
        reinterpret_cast<DemoState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            s = new DemoState();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(s));
            buildRows(s);

            int cw = cs->cx;
            int ch = cs->cy;
            const int listW = (cw - 16) * 55 / 100;
            s->area = {4 + listW + 8, 34, cw - 4 - listW - 8 - 14, ch - 40};

            s->vbar.attach(hwnd, 1001, true);
            s->vbar.setRange(0, (int)((int)s->rows.size() * s->rowH) - s->area.h);
            s->vbar.setPage(s->area.h);

            s->tabs.attach(hwnd, 1002, true);
            s->tabs.setItems({{"Propiedades", true},
                              {"Descripción", true},
                              {"Organización", true}});
            s->tabs.setSelected(0);

            s->list.create(hwnd, 4, 34, listW, ch - 40);
            s->list.setRowHeight(20);
            s->list.setColumns(
                {"Nombre", "Descripción", "Organización"},
                {110, 170, 130});
            std::vector<std::vector<std::string>> rows;
            rows.reserve(5000);
            for (int i = 0; i < 5000; ++i) {
                rows.push_back({"Fila " + std::to_string(i),
                                "Descripción " + std::to_string(i),
                                "Organización " + std::to_string(i)});
            }
            s->list.setItems(rows);
            break;
        }
        case WM_SIZE: {
            if (s) {
                RECT r{};
                GetClientRect(hwnd, &r);
                const int cw = r.right - r.left;
                const int ch = r.bottom - r.top;
                const int listW = (cw - 16) * 55 / 100;
                s->area = {4 + listW + 8, 34, cw - 4 - listW - 8 - 14, ch - 40};
                s->vbar.setRect({r.right - 14, s->area.y, r.right - 4,
                                 s->area.y + s->area.h});
                s->vbar.setPage(s->area.h);
                s->vbar.setRange(
                    0, std::max<int>(0, (int)s->rows.size() * s->rowH -
                                            s->area.h));
                s->vbar.update();
                s->tabs.setRect({4, 4, r.right - 4, 30});
                SetWindowPos(s->list.hwnd(), nullptr, 4, 34, listW, ch - 40,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            if (s) {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);
                if (pt.y <= s->tabs.rect().bottom && pt.y >= s->tabs.rect().top) {
                    s->tabs.onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
                } else {
                    s->vbar.onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
                    s->scrollY = s->vbar.pos();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (!s) break;
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (!s->trackingLeave) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                s->trackingLeave = true;
            }
            s->tabs.onMouseMove(x, y);
            s->vbar.onMouseMove(x, y);
            const int row = hitRightRow(s, y);
            if (row != s->hoverRow) {
                invalidateRightRow(hwnd, s, s->hoverRow);
                s->hoverRow = row;
                invalidateRightRow(hwnd, s, s->hoverRow);
            }
            break;
        }
        case WM_MOUSELEAVE:
            if (s) {
                s->trackingLeave = false;
                s->tabs.onMouseLeave();
                invalidateRightRow(hwnd, s, s->hoverRow);
                s->hoverRow = -1;
            }
            break;
        case WM_LBUTTONDOWN: {
            if (s) {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                if (s->tabs.onMouseDown(x, y)) break;
                if (s->vbar.hitTest(x, y)) s->vbar.onMouseDown(x, y);
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (s) s->vbar.onMouseUp();
            break;
        }
        case WM_CAPTURECHANGED:
            if (s) s->vbar.onCaptureChanged();
            break;
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

                // Contenido derecho (repintado parcial por hover).
                int y = s->area.y - s->scrollY;
                HRGN old = CreateRectRgn(s->area.x, s->area.y,
                                         s->area.x + s->area.w,
                                         s->area.y + s->area.h);
                SelectClipRgn(hdc, old);
                for (int i = 0; i < (int)s->rows.size(); ++i) {
                    if (y + s->rowH < s->area.y) {
                        y += s->rowH;
                        continue;
                    }
                    if (y > s->area.y + s->area.h) break;
                    RECT rowR{s->area.x, y, s->area.x + s->area.w,
                              y + s->rowH};
                    if (s->rows[i].header) {
                        HBRUSH hb = CreateSolidBrush(theme::boxHeader());
                        FillRect(hdc, &rowR, hb);
                        DeleteObject(hb);
                    } else if (i == s->hoverRow) {
                        HBRUSH hb = CreateSolidBrush(theme::surfaceHover());
                        FillRect(hdc, &rowR, hb);
                        DeleteObject(hb);
                    }
                    drawIcon(hdc, s->rows[i].icon, s->area.x + 6, y + 4, 16,
                             s->rows[i].header ? theme::accent()
                                               : theme::text());
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, theme::text());
                    HFONT of = static_cast<HFONT>(
                        SelectObject(hdc, s->rows[i].header
                                              ? theme::uiHeaderFont()
                                              : theme::uiFont()));
                    std::wstring t = widen(s->rows[i].label);
                    ExtTextOutW(hdc, s->area.x + 26, y + 4, ETO_CLIPPED,
                                &rowR, t.c_str(), (int)t.size(), nullptr);
                    SelectObject(hdc, of);
                    y += s->rowH;
                }
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

// --- --gdi-check --------------------------------------------------------
const WCHAR* kGdiClass = L"MotorSKGdiCheck";

struct GdiState {
    PaintBuffer pb;
    int paints = 0;
};

LRESULT CALLBACK gdiCheckProc(HWND hwnd, UINT msg, WPARAM wParam,
                              LPARAM lParam) {
    GdiState* s =
        reinterpret_cast<GdiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        (void)lParam;
        s = new GdiState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(s));
        return TRUE;
    }
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            if (s) ++s->paints;
            if (s && s->pb.beginPaint(hwnd, &ps)) {
                RECT r{};
                GetClientRect(hwnd, &r);
                FillRect(s->pb.dc(), &r, theme::backgroundBrush());
                s->pb.endPaint();
            } else {
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
            }
            return 0;
        }
        case WM_DESTROY: {
            delete s;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

bool runUIDemo() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = demoWndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = theme::backgroundBrush();
    wc.lpszClassName = kDemoClass;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, kDemoClass, L"Motor SK UI Demo",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1000, 720,
                                nullptr, nullptr, inst, nullptr);
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

bool runGdiCheck() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = gdiCheckProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = theme::backgroundBrush();
    wc.lpszClassName = kGdiClass;
    RegisterClassW(&wc);

    const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    HWND hwnd = CreateWindowExW(0, kGdiClass, L"GDI check",
                                WS_OVERLAPPED | WS_CLIPCHILDREN, -20000, -20000,
                                120, 80, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return false;
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    for (int i = 0; i < 1000; ++i) {
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
    }
    const int paints = [](HWND h) {
        auto* st = reinterpret_cast<GdiState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        return st ? st->paints : 0;
    }(hwnd);
    DestroyWindow(hwnd);
    const DWORD after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);

    std::printf("gdi-check: repintados=%d antes=%lu despues=%lu\n", paints,
                static_cast<unsigned long>(before),
                static_cast<unsigned long>(after));
    const bool ok = (after > before && after - before > 5) ? false : true;
    std::printf("gdi-check: %s\n", ok ? "PASS" : "FAIL (crecimiento > 5)");
    return ok;
}

} // namespace ui
} // namespace sk