#include "dimensions_bar.h"

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>

#include "../core/log.h"
#include "../scene/world.h"
#include "theme.h"

namespace sk {
namespace ui {

namespace {

const char* kBarClass = "MotorSKDimensionsBar";
constexpr int kEditId = 1;

} // namespace

bool DimensionsBar::create(void* parentHwnd) {
    parent_ = parentHwnd;

    static bool registered = false;
    if (!registered) {
        HINSTANCE inst = GetModuleHandleW(nullptr);
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&DimensionsBar::wndProc);
        wc.hInstance = inst;
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = theme::headerBackgroundBrush();
        wc.lpszClassName = kBarClass;
        wc.style = CS_DBLCLKS;
        RegisterClassExA(&wc);

        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);
        registered = true;
    }

    HINSTANCE inst = GetModuleHandleW(nullptr);
    hwnd_ = CreateWindowExA(0, kBarClass, "Dimensions",
                            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 100, 28,
                            static_cast<HWND>(parent_), nullptr, inst, this);
    if (!hwnd_) {
        SK_ERROR("DimensionsBar: CreateWindowEx fallo (error %lu)", GetLastError());
        return false;
    }

    // Editor en linea para renombrar (oculto hasta el doble clic).
    renameEdit_ = CreateWindowExA(
        0, "EDIT", "", WS_CHILD | ES_AUTOHSCROLL,
        0, 0, 10, 10, static_cast<HWND>(hwnd_),
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)), inst, nullptr);
    if (renameEdit_) {
        SendMessageA(static_cast<HWND>(renameEdit_), WM_SETFONT,
                     reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
        SetWindowSubclass(static_cast<HWND>(renameEdit_),
                          &DimensionsBar::editProc, kEditId,
                          reinterpret_cast<DWORD_PTR>(this));
    }

    tabs_.attach(static_cast<HWND>(hwnd_), 1, /*showPlus=*/true);
    tabs_.setOnSelected([this](int index) {
        if (suppressSelect_) return;
        if (index >= 0 && index < static_cast<int>(ids_.size())) {
            active_ = ids_[index];
            if (onSelected_) onSelected_(active_);
        }
    });
    tabs_.setOnClose([this](int index) {
        if (index >= 0 && index < static_cast<int>(ids_.size()) &&
            onCloseRequested_) {
            onCloseRequested_(ids_[index]);
        }
    });
    tabs_.setOnPlus([this]() {
        if (onPlus_) onPlus_();
    });
    return true;
}

void DimensionsBar::destroy() {
    if (renameEdit_) {
        RemoveWindowSubclass(static_cast<HWND>(renameEdit_),
                             &DimensionsBar::editProc, kEditId);
        renameEdit_ = nullptr;
    }
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
}

void DimensionsBar::setWorld(const World* world) {
    world_ = world;
    refresh();
}

void DimensionsBar::setActive(InstanceId id) {
    active_ = id;
    suppressSelect_ = true;
    for (size_t i = 0; i < ids_.size(); ++i) {
        if (ids_[i] == id) {
            tabs_.setSelected(static_cast<int>(i));
            break;
        }
    }
    suppressSelect_ = false;
}

void DimensionsBar::refresh() {
    ids_.clear();
    std::vector<TabStripItem> items;
    if (world_) {
        for (InstanceId id : world_->dimensions()) {
            const Instance* inst = world_->find(id);
            TabStripItem it;
            it.text = inst ? inst->name() : std::string("Dimension");
            it.closeable = true;
            items.push_back(it);
            ids_.push_back(id);
        }
    }
    tabs_.setItems(items);
    // Selecciona la activa si sigue existiendo; si no, la primera.
    int sel = 0;
    bool found = false;
    for (size_t i = 0; i < ids_.size(); ++i) {
        if (ids_[i] == active_) {
            sel = static_cast<int>(i);
            found = true;
            break;
        }
    }
    if (!found) active_ = ids_.empty() ? kInvalidId : ids_.front();
    suppressSelect_ = true;
    tabs_.setSelected(sel);
    suppressSelect_ = false;
    invalidate();
}

void DimensionsBar::resize(int x, int y, int width, int height) {
    if (!hwnd_) return;
    MoveWindow(static_cast<HWND>(hwnd_), x, y, width, height, TRUE);
    tabs_.setRect(RECT{0, 0, width, height});
    invalidate();
}

void DimensionsBar::setVisible(bool visible) {
    if (!hwnd_) return;
    ShowWindow(static_cast<HWND>(hwnd_), visible ? SW_SHOW : SW_HIDE);
}

void DimensionsBar::invalidate() {
    if (hwnd_) InvalidateRect(static_cast<HWND>(hwnd_), nullptr, TRUE);
}

void DimensionsBar::layoutEdit() {
    if (!renameEdit_ || renameIndex_ < 0) return;
    RECT tr = tabs_.tabRect(renameIndex_);
    if (tr.right <= tr.left) return;
    MoveWindow(static_cast<HWND>(renameEdit_), tr.left + 4, tr.top + 3,
               tr.right - tr.left - 8, tr.bottom - tr.top - 6, TRUE);
}

void DimensionsBar::beginRename(int index) {
    if (!renameEdit_ || index < 0 || index >= static_cast<int>(ids_.size())) {
        return;
    }
    const Instance* inst = world_ ? world_->find(ids_[index]) : nullptr;
    renameIndex_ = index;
    std::string name = inst ? inst->name() : std::string();
    SetWindowTextA(static_cast<HWND>(renameEdit_), name.c_str());
    layoutEdit();
    ShowWindow(static_cast<HWND>(renameEdit_), SW_SHOW);
    SetFocus(static_cast<HWND>(renameEdit_));
    SendMessageA(static_cast<HWND>(renameEdit_), EM_SETSEL, 0, -1);
}

void DimensionsBar::commitRename(bool accept) {
    if (renameBusy_ || renameIndex_ < 0) return;
    renameBusy_ = true;
    const int index = renameIndex_;
    renameIndex_ = -1;
    char buf[256]{};
    GetWindowTextA(static_cast<HWND>(renameEdit_), buf, sizeof(buf));
    ShowWindow(static_cast<HWND>(renameEdit_), SW_HIDE);
    if (accept && index < static_cast<int>(ids_.size()) && buf[0] != '\0') {
        const Instance* inst = world_ ? world_->find(ids_[index]) : nullptr;
        if (inst && inst->name() != buf && onRenamed_) {
            onRenamed_(ids_[index], buf);
        }
    }
    SetFocus(static_cast<HWND>(hwnd_));
    renameBusy_ = false;
}

LRESULT CALLBACK DimensionsBar::editProc(HWND h, UINT msg, WPARAM wParam,
                                         LPARAM lParam, UINT_PTR idSubclass,
                                         DWORD_PTR refData) {
    DimensionsBar* self = reinterpret_cast<DimensionsBar*>(refData);
    (void)idSubclass;
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            self->commitRename(true);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            self->commitRename(false);
            return 0;
        }
    } else if (msg == WM_KILLFOCUS) {
        self->commitRename(true);
    }
    return DefSubclassProc(h, msg, wParam, lParam);
}

long long __stdcall DimensionsBar::wndProc(void* hwndPtr, unsigned int msg,
                                           unsigned long long wParam,
                                           long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    DimensionsBar* self = reinterpret_cast<DimensionsBar*>(
        GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<DimensionsBar*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, theme::headerBackgroundBrush());
            if (self) self->tabs_.paint(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!self) break;
            if (!self->trackingLeave_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                self->trackingLeave_ = true;
            }
            self->tabs_.onMouseMove(static_cast<short>(LOWORD(lParam)),
                                    static_cast<short>(HIWORD(lParam)));
            return 0;
        }
        case WM_MOUSELEAVE: {
            if (!self) break;
            self->trackingLeave_ = false;
            self->tabs_.onMouseLeave();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (!self) break;
            self->tabs_.onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!self) break;
            const int x = static_cast<short>(LOWORD(lParam));
            const int y = static_cast<short>(HIWORD(lParam));
            if (!self->tabs_.onMouseDown(x, y)) {
                SetFocus(hwnd);
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            if (!self) break;
            const int x = static_cast<short>(LOWORD(lParam));
            const int y = static_cast<short>(HIWORD(lParam));
            TabHitResult h = self->tabs_.hitTest(x, y);
            if (h.kind == TabHit::Tab) self->beginRename(h.index);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace ui
} // namespace sk
