#include "class_picker.h"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "../core/log.h"
#include "../scene/class_registry.h"
#include "theme.h"

namespace sk {

namespace {

constexpr const char* kPickerClass = "MotorSKClassPicker";
constexpr int kWidth = 280;
constexpr int kHeight = 360;
constexpr int kEditH = 24;

HWND g_picker = nullptr;

// Estado por ventana del popup.
struct State {
    HWND edit = nullptr;
    HWND list = nullptr;
    std::function<void(const std::string&)> onPick;
    std::vector<std::string> filtered;  // nombres de clase visibles
};

// Lista completa de clases insertables (se construye una vez).
const std::vector<std::string>& insertableClasses() {
    static std::vector<std::string> names;
    if (names.empty()) {
        const ClassRegistry& r = ClassRegistry::get();
        for (int i = 0; i < r.classCount(); ++i) {
            const ClassDef& c = r.at(i);
            if (c.insertable) names.push_back(c.name);
        }
    }
    return names;
}

const wchar_t* iconFor(const std::string& className) {
    const ClassRegistry& r = ClassRegistry::get();
    const ClassDef* c = r.find(className);
    if (!c) return L"\u25A0";
    return r.categories()[c->category].icon;
}

std::string toLower(const std::string& s) {
    std::string r = s;
    for (char& ch : r) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return r;
}

void refilter(State* st) {
    if (!st || !st->list || !st->edit) return;
    char query[128]{};
    GetWindowTextA(st->edit, query, static_cast<int>(sizeof(query)));
    const std::string needle = toLower(query);

    SendMessageA(st->list, WM_SETREDRAW, FALSE, 0);
    SendMessageA(st->list, LB_RESETCONTENT, 0, 0);
    st->filtered.clear();
    for (const std::string& name : insertableClasses()) {
        if (!needle.empty() && toLower(name).find(needle) == std::string::npos) {
            continue;
        }
        // Texto visible: "<icono> Nombre", sin emoji si el codepoint es
        // simple. El icono es un emoji (UTF-16); lo anadimos como wchar.
        std::wstring w; 
        w += iconFor(name);
        w += L" ";
        w += std::wstring(name.begin(), name.end());
        SendMessageW(st->list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(w.c_str()));
        st->filtered.push_back(name);
    }
    if (!st->filtered.empty()) SendMessageA(st->list, LB_SETCURSEL, 0, 0);
    SendMessageA(st->list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->list, nullptr, TRUE);
}

void pick(State* st) {
    if (!st) return;
    const int sel = static_cast<int>(SendMessageA(st->list, LB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(st->filtered.size())) return;
    std::string className = st->filtered[sel];
    SK_DEBUG("selector '+': elegido '%s'", className.c_str());
    auto cb = st->onPick;
    DestroyWindow(g_picker);
    g_picker = nullptr;
    if (cb) cb(className);
}

// El foco del popup vive en el EDIT (para escribir el filtro). El EDIT
// consume WM_KEYDOWN y no lo reenvia, asi que Esc/Enter/flechas se
// interceptan aqui por subclase. Enter elige, Esc cierra, las flechas
// mueven la seleccion de la lista sin sacar el foco del cajetin.
LRESULT CALLBACK editProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam,
                          UINT_PTR idSubclass, DWORD_PTR refData) {
    State* st = reinterpret_cast<State*>(refData);
    (void)idSubclass;
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_ESCAPE) {
            SK_DEBUG("selector '+': Esc cierra");
            if (g_picker) DestroyWindow(g_picker);
            return 0;
        }
        if (wParam == VK_RETURN) {
            pick(st);
            return 0;
        }
        if (wParam == VK_DOWN || wParam == VK_UP) {
            if (!st || !st->list) return 0;
            const int count =
                static_cast<int>(SendMessageA(st->list, LB_GETCOUNT, 0, 0));
            if (count <= 0) return 0;
            int sel =
                static_cast<int>(SendMessageA(st->list, LB_GETCURSEL, 0, 0));
            sel += (wParam == VK_DOWN) ? 1 : -1;
            if (sel < 0) sel = 0;
            if (sel >= count) sel = count - 1;
            SendMessageA(st->list, LB_SETCURSEL, sel, 0);
            SendMessageA(st->list, LB_SETTOPINDEX, sel, 0);
            SK_DEBUG("selector '+': flecha mueve a %d de %d", sel, count);
            return 0;
        }
    } else if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(h, &editProc, idSubclass);
    }
    return DefSubclassProc(h, msg, wParam, lParam);
}

LRESULT CALLBACK pickerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    State* st =
        reinterpret_cast<State*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE: {
            st = reinterpret_cast<State*>(
                GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            HINSTANCE inst = GetModuleHandleW(nullptr);
            st->edit = CreateWindowExA(
                0, "EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 6, 6,
                kWidth - 12, kEditH, hwnd, reinterpret_cast<HMENU>(1), inst,
                nullptr);
            SendMessageA(st->edit, WM_SETFONT,
                         reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
            SetWindowSubclass(st->edit, &editProc, 1,
                              reinterpret_cast<DWORD_PTR>(st));
            st->list = CreateWindowExA(
                0, "LISTBOX", "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 6,
                6 + kEditH + 6, kWidth - 12, kHeight - kEditH - 24, hwnd,
                reinterpret_cast<HMENU>(2), inst, nullptr);
            SendMessageA(st->list, WM_SETFONT,
                         reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
            refilter(st);
            SetFocus(st->edit);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id == 1 && code == EN_CHANGE) {
                refilter(st);
                return 0;
            }
            if (id == 2 && code == LBN_DBLCLK) {
                pick(st);
                return 0;
            }
            // Un clic simple sobre la lista tambien elige (antes solo el
            // doble clic). La navegacion por teclado mueve la seleccion de
            // forma programatica (LB_SETCURSEL), que no dispara este aviso,
            // asi que no se elige al navegar con flechas.
            if (id == 2 && code == LBN_SELCHANGE) {
                pick(st);
                return 0;
            }
            break;
        }
        case WM_ACTIVATE:
            // Click fuera: se cierra sin elegir.
            if (LOWORD(wParam) == WA_INACTIVE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (wParam == VK_RETURN) {
                pick(st);
                return 0;
            }
            break;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, theme::surface());
            SetTextColor(hdc, theme::text());
            return reinterpret_cast<LRESULT>(theme::surfaceBrush());
        }
        case WM_NCDESTROY: {
            if (g_picker == hwnd) g_picker = nullptr;
            delete st;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace

void ClassPicker::close() {
    if (g_picker) {
        DestroyWindow(g_picker);
        g_picker = nullptr;
    }
}

void ClassPicker::open(void* ownerHwnd, int x, int y,
                       std::function<void(const std::string&)> onPick) {
    close();

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = pickerProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = theme::backgroundBrush();
        wc.lpszClassName = kPickerClass;
        RegisterClassExA(&wc);
        registered = true;
    }

    auto* st = new State();
    st->onPick = std::move(onPick);

    HWND owner = static_cast<HWND>(ownerHwnd);
    g_picker = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kPickerClass, "Insertar clase",
        WS_POPUP | WS_BORDER, x, y, kWidth, kHeight, owner, nullptr,
        GetModuleHandleW(nullptr), st);
    if (!g_picker) {
        delete st;
        return;
    }
    SK_DEBUG("selector '+': abierto en (%d,%d)", x, y);
    ShowWindow(g_picker, SW_SHOW);
    SetForegroundWindow(g_picker);
}

} // namespace sk
