#include "docs_view.h"

#include <windows.h>
#include <commctrl.h>

#include <cstring>

#include "../core/log.h"
#include "../scene/class_registry.h"
#include "theme.h"
#include "workspace_tabs.h"

namespace sk {

namespace {

const char* kDocsClass = "MotorSKDocsView";

enum ControlId {
    kIdClassList = 1,
    kIdPropsList = 2,
    kIdDivider = 3,
};

// Las categorias y clases viven en ClassRegistry (fuente unica de datos
// del motor). La Referencia de clases consume ese registro directamente,
// igual que el buscador "+" y el panel Properties.
const ClassRegistry& reg() { return ClassRegistry::get(); }

// Colores de la vista (grises acordes al tema oscuro; sin blanco puro).
COLORREF nameColor() { return RGB(214, 214, 220); }
COLORREF descColor() { return RGB(150, 150, 160); }

// Fuente para pintar los emojis (provisional).
HFONT emojiFont() {
    static HFONT font = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Emoji");
    return font;
}

// Copia una cadena ancha a un buffer fijo.
void copyW(wchar_t* dst, int cap, const wchar_t* src) {
    int i = 0;
    while (src[i] != L'\0' && i < cap - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = L'\0';
}

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
                auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                if (dis->CtlID == kIdClassList && dis->itemID != static_cast<UINT>(-1)) {
                    self->paintRow(*dis);
                    return TRUE;
                }
                if (dis->CtlID == kIdDivider) {
                    FillRect(dis->hDC, &dis->rcItem,
                             theme::boxBackgroundBrush());
                    return TRUE;
                }
                break;
            }
            case WM_NOTIFY: {
                auto* nm = reinterpret_cast<NMHDR*>(lParam);
                if (nm->idFrom == kIdClassList && nm->code == LVN_ITEMCHANGED) {
                    auto* lv = reinterpret_cast<NMLISTVIEW*>(lParam);
                    if ((lv->uNewState & LVIS_SELECTED) &&
                        !(lv->uOldState & LVIS_SELECTED)) {
                        self->sel_ = lv->iItem;
                        self->updateDetails();
                    }
                    return 0;
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

        INITCOMMONCONTROLSEX icc{sizeof(icc),
                                 ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);

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

    // Lista de clases (izquierda), owner-draw: icono + nombre + descripcion
    // corta. Las filas se pintan en paintRow.
    HWND list = CreateWindowExA(
        0, WC_LISTVIEWA, "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDRAWFIXED |
            LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER,
        0, 0, kListWidth, 100, root,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdClassList)), inst,
        nullptr);
    list_ = list;
    SendMessageA(list, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    ListView_SetBkColor(list, theme::listBackground());
    ListView_SetTextBkColor(list, theme::listBackground());
    ListView_SetTextColor(list, descColor());
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
    {
        LVCOLUMNA col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<char*>("");
        col.cx = kIconColWidth;
        ListView_InsertColumn(list, 0, &col);
        col.pszText = const_cast<char*>("Clase");
        col.cx = kNameColWidth;
        ListView_InsertColumn(list, 1, &col);
        col.pszText = const_cast<char*>("Descripcion");
        col.cx = 160;
        ListView_InsertColumn(list, 2, &col);
    }

    // Linea divisoria vertical entre la lista y el detalle.
    divider_ = CreateWindowExA(0, "STATIC", "",
                               WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, kListWidth,
                               0, 1, 100, root,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDivider)),
                               inst, nullptr);

    // Detalle (derecha): nombre, categoria, descripcion y tabla de
    // propiedades (vacia de momento).
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
    HWND props = CreateWindowExA(
        0, WC_LISTVIEWA, "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER, 0, 0,
        100, 100, root,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPropsList)), inst,
        nullptr);
    props_ = props;
    ListView_SetBkColor(props, theme::listBackground());
    ListView_SetTextBkColor(props, theme::listBackground());
    ListView_SetTextColor(props, descColor());
    {
        LVCOLUMNA col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<char*>("Propiedad");
        col.cx = 180;
        ListView_InsertColumn(props, 0, &col);
        col.pszText = const_cast<char*>("Valor por defecto");
        col.cx = 200;
        ListView_InsertColumn(props, 1, &col);
    }

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
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
    list_ = nullptr;
    divider_ = nullptr;
    title_ = nullptr;
    category_ = nullptr;
    desc_ = nullptr;
    propsLabel_ = nullptr;
    props_ = nullptr;
}

void DocsView::setVisible(bool visible) {
    visible_ = visible;
    if (hwnd_) ShowWindow(static_cast<HWND>(hwnd_), visible ? SW_SHOW : SW_HIDE);
}

void DocsView::resize(int width, int height) { layout(width, height); }

void DocsView::showClass(const std::string& className) {
    HWND list = static_cast<HWND>(list_);
    if (!list) return;
    const int count = ListView_GetItemCount(list);
    const int n = reg().classCount();
    for (int i = 0; i < count; ++i) {
        if (i < 0 || i >= n) break;
        if (className == reg().at(i).name) {
            sel_ = i;
            ListView_SetItemState(list, i, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list, i, FALSE);
            updateDetails();
            return;
        }
    }
}

void DocsView::buildClassList() {
    HWND list = static_cast<HWND>(list_);
    if (!list) return;
    ListView_DeleteAllItems(list);

    const ClassRegistry& r = reg();
    for (int i = 0; i < r.classCount(); ++i) {
        const ClassDef& c = r.at(i);

        // El texto del item es el nombre (columna "Clase"); el icono y el
        // resto se pintan en paintRow. La primera columna tambien lleva el
        // nombre para que el filtrado/orden siga siendo util.
        LVITEMA item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<char*>(c.name);
        ListView_InsertItem(list, &item);
    }
    sel_ = 0;
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    updateDetails();
}

void DocsView::updateDetails() {
    const ClassRegistry& r = reg();
    if (sel_ < 0 || sel_ >= r.classCount()) return;
    const ClassDef& c = r.at(sel_);
    if (title_) {
        SetWindowTextA(static_cast<HWND>(title_), c.name);
    }
    if (category_) {
        SetWindowTextA(static_cast<HWND>(category_),
                       r.categories()[c.category].name);
    }
    if (desc_) {
        SetWindowTextA(static_cast<HWND>(desc_), c.shortDesc);
    }
    // Tabla "Propiedades": propiedades efectivas del registro (misma fuente
    // que usa el boton "+" y el panel Properties).
    if (props_) {
        HWND list = static_cast<HWND>(props_);
        ListView_DeleteAllItems(list);
        int row = 0;
        for (const PropertyDef& p : c.effective) {
            LVITEMA item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            item.pszText = const_cast<char*>(p.name);
            ListView_InsertItem(list, &item);
            char value[160]{};
            lstrcpynA(value, p.def ? p.def : "", 160);
            ListView_SetItemText(list, row, 1, value);
            ++row;
        }
    }
}

void DocsView::paintRow(const DRAWITEMSTRUCT& dis) const {
    const ClassRegistry& r = reg();
    const int idx = static_cast<int>(dis.itemID);
    if (idx < 0 || idx >= r.classCount()) return;
    const ClassDef& c = r.at(idx);

    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;
    HWND list = dis.hwndItem;

    // Fondo de la fila.
    FillRect(hdc, &rc, theme::listBackgroundBrush());

    const bool selected = (dis.itemState & ODS_SELECTED) != 0;
    const bool hot = (dis.itemState & ODS_HOTLIGHT) != 0;
    if (selected || hot) {
        COLORREF fill = selected ? theme::boxBackgroundAlt() : theme::surfacePressed();
        HBRUSH b = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, selected ? theme::accent() : theme::border());
        HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, b));
        HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
        RECT r = rc;
        r.left += 3;
        r.right -= 3;
        r.top += 1;
        r.bottom -= 1;
        RoundRect(hdc, r.left, r.top, r.right, r.bottom, 8, 8);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(pen);
        DeleteObject(b);
    }

    const int w0 = ListView_GetColumnWidth(list, 0);
    const int w1 = ListView_GetColumnWidth(list, 1);

    SetBkMode(hdc, TRANSPARENT);

    // Icono (emoji) centrado en la primera columna.
    RECT iconRc = rc;
    iconRc.right = rc.left + w0;
    wchar_t icon[8]{};
    copyW(icon, 8, c.icon);
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, emojiFont()));
    SetTextColor(hdc, nameColor());
    DrawTextW(hdc, icon, -1, &iconRc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Nombre de la clase (columna "Clase").
    SelectObject(hdc, theme::uiFont());
    RECT nameRc = rc;
    nameRc.left = rc.left + w0 + 6;
    nameRc.right = rc.left + w0 + w1;
    SetTextColor(hdc, nameColor());
    DrawTextA(hdc, c.name, -1, &nameRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);

    // Descripcion corta (columna "Descripcion").
    RECT descRc = rc;
    descRc.left = rc.left + w0 + w1 + 6;
    descRc.right = rc.right - 6;
    SetTextColor(hdc, descColor());
    DrawTextA(hdc, c.shortDesc, -1, &descRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);

    SelectObject(hdc, oldFont);
}

void DocsView::layout(int width, int height) {
    if (!hwnd_) return;
    const int y = WorkspaceTabs::kTopBandHeight;
    int h = height - y;
    if (h < 0) h = 0;
    MoveWindow(static_cast<HWND>(hwnd_), 0, y, width, h, TRUE);

    if (h <= 0) return;

    const int detailX = kListWidth + 14;
    const int detailW = width - detailX - 14;
    const int descH = 140;

    if (list_) {
        MoveWindow(static_cast<HWND>(list_), 0, 0, kListWidth, h, TRUE);
        // La columna "Descripcion" se estira con el ancho de la lista.
        int descW = kListWidth - kIconColWidth - kNameColWidth - 24;
        if (descW < 60) descW = 60;
        ListView_SetColumnWidth(static_cast<HWND>(list_), 2, descW);
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
    if (props_) {
        const int propsY = 66 + descH + 36;
        int propsH = h - propsY - 12;
        if (propsH < 40) propsH = 40;
        MoveWindow(static_cast<HWND>(props_), detailX, propsY, detailW, propsH,
                   TRUE);
    }
}

} // namespace sk
