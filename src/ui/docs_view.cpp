#include "docs_view.h"

#include <windows.h>

#include <string>
#include <vector>

#include "../core/log.h"
#include "../editor/layout.h"
#include "../scene/class_registry.h"
#include "kit/kit.h"
#include "theme.h"
#include "workspace_tabs.h"

namespace sk {

namespace {

const char* kDocsClass = "MotorSKDocsView";

enum ControlId {
    kIdDivider = 3,
};

// Las categorias y clases viven en ClassRegistry (fuente unica de datos
// del motor). La Referencia de clases consume ese registro directamente,
// igual que el buscador "+" y el panel Properties.
const ClassRegistry& reg() { return ClassRegistry::get(); }

// Color del texto de las etiquetas (gris acorde al tema oscuro).
COLORREF nameColor() { return RGB(214, 214, 220); }

} // namespace

long long __stdcall DocsView::wndProc(void* hwndPtr, unsigned int msg,
                                      unsigned long long wParam, long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    DocsView* self =
        reinterpret_cast<DocsView*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<DocsView*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self) {
        switch (msg) {
            case WM_DRAWITEM: {
                // Linea divisoria vertical (STATIC owner-draw).
                auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                if (dis->CtlID == kIdDivider) {
                    FillRect(dis->hDC, &dis->rcItem,
                             theme::boxBackgroundBrush());
                    return TRUE;
                }
                break;
            }
            case WM_CTLCOLORSTATIC: {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetTextColor(hdc, nameColor());
                SetBkColor(hdc, theme::listBackground());
                return reinterpret_cast<long long>(theme::listBackgroundBrush());
            }
            default:
                break;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

bool DocsView::create(void* parentHwnd) {
    parent_ = parentHwnd;

    static bool registered = false;
    if (!registered) {
        HINSTANCE inst = GetModuleHandleW(nullptr);

        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&DocsView::wndProc);
        wc.hInstance = inst;
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = theme::listBackgroundBrush();
        wc.lpszClassName = kDocsClass;
        RegisterClassExA(&wc);

        registered = true;
    }

    HINSTANCE inst = GetModuleHandleW(nullptr);
    RECT rc{};
    GetClientRect(static_cast<HWND>(parent_), &rc);

    hwnd_ = CreateWindowExA(0, kDocsClass, "Documentacion de clases",
                            WS_CHILD | WS_CLIPCHILDREN, 0, 0, rc.right,
                            rc.bottom, static_cast<HWND>(parent_), nullptr,
                            inst, this);
    if (!hwnd_) {
        SK_ERROR("DocsView: CreateWindowEx fallo (error %lu)", GetLastError());
        return false;
    }
    HWND root = static_cast<HWND>(hwnd_);

    // Lista de clases (izquierda): icono + nombre + descripcion corta, con
    // el ListWidget del kit (cabeceras redondeadas, fondo oscuro; nada de
    // blanco nativo del ListView).
    classList_.create(root, 0, 0, kListWidth, 100);
    classList_.setRowHeight(24);
    classList_.setColumns({"", "Clase", "Descripcion"},
                          {kIconColWidth, kNameColWidth, 160});
    classList_.setOnClick([this](int row) {
        sel_ = row;
        updateDetails();
    });

    // Linea divisoria vertical entre la lista y el detalle.
    divider_ = CreateWindowExA(0, "STATIC", "",
                               WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, kListWidth,
                               0, 1, 100, root,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDivider)),
                               inst, nullptr);

    // Detalle (derecha): nombre, categoria, descripcion y tabla de
    // propiedades.
    title_ = CreateWindowExA(0, "STATIC", "Selecciona una clase",
                             WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 26,
                             root, nullptr, inst, nullptr);
    category_ = CreateWindowExA(0, "STATIC", "",
                                WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 20,
                                root, nullptr, inst, nullptr);
    desc_ = CreateWindowExA(0, "EDIT", "",
                            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                                ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                            0, 0, 100, 100, root, nullptr, inst, nullptr);
    propsLabel_ = CreateWindowExA(0, "STATIC", "Propiedades",
                                  WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100,
                                  20, root, nullptr, inst, nullptr);
    propsList_.create(root, 0, 0, 100, 100);
    propsList_.setRowHeight(22);
    propsList_.setColumns({"Propiedad", "Valor por defecto"}, {180, 200});

    void* labels[] = {title_, category_, desc_, propsLabel_};
    for (void* h : labels) {
        SendMessageA(static_cast<HWND>(h), WM_SETFONT,
                     reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    }

    buildClassList();
    layout(rc.right, rc.bottom);
    SK_INFO("Vista de documentacion de clases lista (%d clases)",
            ClassRegistry::get().classCount());
    return true;
}

void DocsView::destroy() {
    classList_.destroy();
    propsList_.destroy();
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
    divider_ = nullptr;
    title_ = nullptr;
    category_ = nullptr;
    desc_ = nullptr;
    propsLabel_ = nullptr;
}

void DocsView::setVisible(bool visible) {
    visible_ = visible;
    if (hwnd_) ShowWindow(static_cast<HWND>(hwnd_), visible ? SW_SHOW : SW_HIDE);
}

void DocsView::resize(int width, int height) { layout(width, height); }

void DocsView::showClass(const std::string& className) {
    if (!classList_.hwnd()) return;
    const ClassRegistry& r = reg();
    for (int i = 0; i < r.classCount(); ++i) {
        if (className == r.at(i).name) {
            sel_ = i;
            classList_.setSelectedRow(i);  // selecciona y hace visible
            updateDetails();
            return;
        }
    }
}

void DocsView::buildClassList() {
    const ClassRegistry& r = reg();
    std::vector<std::vector<std::string>> rows;
    rows.reserve(static_cast<size_t>(r.classCount()));
    for (int i = 0; i < r.classCount(); ++i) {
        const ClassDef& c = r.at(i);
        // El icono es un emoji provisional; si la clase no fija uno se usa
        // el de su categoria. Se pasa como UTF-8 al ListWidget del kit.
        const wchar_t* ic = c.icon;
        if (!ic && c.category >= 0 && c.category < r.categoryCount()) {
            ic = r.categories()[c.category].icon;
        }
        const std::string icon = ic ? ui::narrow(std::wstring(ic)) : "";
        rows.push_back({icon, c.name, c.shortDesc});
    }
    classList_.setItems(std::move(rows));
    sel_ = 0;
    classList_.setSelectedRow(0);  // onClick_ -> updateDetails
    updateDetails();
}

void DocsView::updateDetails() {
    const ClassRegistry& r = reg();
    if (sel_ < 0 || sel_ >= r.classCount()) return;
    const ClassDef& c = r.at(sel_);
    if (title_) SetWindowTextA(static_cast<HWND>(title_), c.name);
    if (category_) {
        SetWindowTextA(static_cast<HWND>(category_),
                       r.categories()[c.category].name);
    }
    if (desc_) SetWindowTextA(static_cast<HWND>(desc_), c.shortDesc);
    // Tabla "Propiedades": propiedades efectivas del registro (misma fuente
    // que usa el boton "+" y el panel Properties).
    std::vector<std::vector<std::string>> rows;
    rows.reserve(c.effective.size());
    for (const PropertyDef& p : c.effective) {
        rows.push_back({p.name, p.def ? p.def : ""});
    }
    propsList_.setItems(std::move(rows));
}

void DocsView::layout(int width, int height) {
    if (!hwnd_) return;
    const int y = editor::Layout::topBandHeight();
    int h = height - y;
    if (h < 0) h = 0;
    MoveWindow(static_cast<HWND>(hwnd_), 0, y, width, h, TRUE);

    if (h <= 0) return;

    const int detailX = kListWidth + 14;
    const int detailW = width - detailX - 14;
    const int descH = 140;

    if (classList_.hwnd()) {
        MoveWindow(classList_.hwnd(), 0, 0, kListWidth, h, TRUE);
        // La columna "Descripcion" se estira con el ancho de la lista.
        int descW = kListWidth - kIconColWidth - kNameColWidth - 24;
        if (descW < 60) descW = 60;
        classList_.setColumnWidths({kIconColWidth, kNameColWidth, descW});
    }
    if (divider_) {
        MoveWindow(static_cast<HWND>(divider_), kListWidth, 0, 1, h, TRUE);
    }
    if (title_) {
        MoveWindow(static_cast<HWND>(title_), detailX, 10, detailW, 26, TRUE);
    }
    if (category_) {
        MoveWindow(static_cast<HWND>(category_), detailX, 38, detailW, 20, TRUE);
    }
    if (desc_) {
        MoveWindow(static_cast<HWND>(desc_), detailX, 66, detailW, descH, TRUE);
    }
    if (propsLabel_) {
        MoveWindow(static_cast<HWND>(propsLabel_), detailX, 66 + descH + 12,
                   detailW, 20, TRUE);
    }
    if (propsList_.hwnd()) {
        const int propsY = 66 + descH + 36;
        int propsH = h - propsY - 12;
        if (propsH < 40) propsH = 40;
        MoveWindow(propsList_.hwnd(), detailX, propsY, detailW, propsH, TRUE);
    }
}

} // namespace sk
