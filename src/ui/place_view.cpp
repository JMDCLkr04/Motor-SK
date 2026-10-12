#include "place_view.h"

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>

#include "../core/log.h"
#include "../editor/layout.h"
#include "class_picker.h"
#include "theme.h"

namespace sk {

namespace {

const char* kPanelClass = "MotorSKPlacePanel";

constexpr int kHeaderHeight = 36;

// Rectangulo de la pestana "index" de "count" repartidas en el ancho.
RECT tabRect(const RECT& rc, int index, int count) {
    RECT r{};
    r.left = index * rc.right / count;
    r.right = (index + 1) * rc.right / count;
    r.top = 0;
    r.bottom = kHeaderHeight;
    return r;
}

// Barra de pestanas del panel: la activa se funde con el fondo del
// contenido (cuadrado inferior) y las demas quedan atenuadas.
void paintTabs(HDC hdc, const RECT& rc, const char* const* labels, int count,
               int active) {
    RECT header{0, 0, rc.right, kHeaderHeight};
    FillRect(hdc, &header, theme::headerBackgroundBrush());

    HFONT oldFont =
        static_cast<HFONT>(SelectObject(hdc, theme::uiHeaderFont()));
    SetBkMode(hdc, TRANSPARENT);
    for (int i = 0; i < count; ++i) {
        RECT tr = tabRect(rc, i, count);
        if (i == active) {
            RECT body{tr.left, 5, tr.right, kHeaderHeight};
            FillRect(hdc, &body, theme::backgroundBrush());
        }
        SetTextColor(hdc, i == active ? theme::text() : theme::textDisabled());
        DrawTextA(hdc, labels[i], -1, &tr,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hdc, oldFont);

    HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    MoveToEx(hdc, 0, kHeaderHeight, nullptr);
    LineTo(hdc, rc.right, kHeaderHeight);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

} // namespace

bool PlaceView::create(void* parentHwnd) {
    parent_ = parentHwnd;

    static bool registered = false;
    if (!registered) {
        HINSTANCE inst = GetModuleHandleW(nullptr);

        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&PlaceView::wndProc);
        wc.hInstance = inst;
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = theme::backgroundBrush();
        wc.lpszClassName = kPanelClass;
        RegisterClassExA(&wc);

        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);

        registered = true;
    }

    HWND parent = static_cast<HWND>(parent_);
    RECT rc{};
    GetClientRect(parent, &rc);
    HINSTANCE inst = GetModuleHandleW(nullptr);

    explorer_ = CreateWindowExA(0, kPanelClass, "Explorer",
                                WS_CHILD | WS_CLIPCHILDREN,
                                0, 0, editor::Layout::explorerWidth(), 100, parent, nullptr,
                                inst, this);
    properties_ = CreateWindowExA(0, kPanelClass, "Properties",
                                  WS_CHILD | WS_CLIPCHILDREN,
                                  0, 0, editor::Layout::propertiesWidth(), 100, parent, nullptr,
                                  inst, this);
    if (!explorer_ || !properties_) {
        SK_ERROR("PlaceView: CreateWindowEx fallo (error %lu)", GetLastError());
        return false;
    }

    // Explorer: TreeWidget dibujado a mano, alimentado por el TreeModel
    // (filas de 22 px virtualizadas, sangria real, icono por clase, boton
    // "+" solo en la fila bajo el raton, renombrado en linea y arrastrar y
    // soltar con fila fantasma y tres zonas).
    treeWidget_.create(static_cast<HWND>(explorer_), 6, kHeaderHeight + 6,
                       editor::Layout::explorerWidth() - 12, 100);
    treeWidget_.setModel(&treeModel_);
    treeWidget_.setOnSelectionChanged(
        [this](const std::vector<ui::NodeId>&) { notifySelection(); });
    treeWidget_.setOnAddRequested([this](ui::NodeId id, const RECT& anchor) {
        POINT pt{anchor.left, anchor.bottom};
        openClassPicker(static_cast<InstanceId>(id), pt);
    });
    treeWidget_.setOnRenameRequested([this](ui::NodeId id,
                                            const std::string& name) {
        if (onNameEdited_) onNameEdited_(static_cast<InstanceId>(id), name);
    });
    treeWidget_.setOnDeleteRequested(
        [this](const std::vector<ui::NodeId>& ids) {
            if (!onDelete_) return;
            std::vector<InstanceId> out;
            out.reserve(ids.size());
            for (ui::NodeId n : ids) {
                out.push_back(static_cast<InstanceId>(n));
            }
            onDelete_(out);
        });
    treeWidget_.setOnReparentRequested(
        [this](ui::NodeId child, ui::NodeId parent, int index) {
            if (onReparent_) {
                onReparent_(static_cast<InstanceId>(child),
                            static_cast<InstanceId>(parent), index);
            }
        });

    // Pestana izquierda: Properties (formulario generado desde el
    // ClassRegistry) y Output (log con buffer circular). Ambas son
    // widgets dibujados a mano; switchLeftTab muestra la activa.
    propsPanel_.create(static_cast<HWND>(properties_), 1);
    outputPanel_.create(static_cast<HWND>(properties_), 2);
    propsPanel_.setWorld(world_);
    propsPanel_.setOnPropertyEdited(
        [this](InstanceId id, const std::string& prop,
               const PropertyValue& value) {
            if (onPropertyEdited_) onPropertyEdited_(id, prop, value);
        });
    propsPanel_.setNameEdited([this](InstanceId id, const std::string& name) {
        if (onNameEdited_) onNameEdited_(id, name);
    });
    propsPanel_.setOnAttributeSet([this](InstanceId id, const Attribute& a) {
        if (onAttributeSet_) onAttributeSet_(id, a);
    });
    propsPanel_.setOnAttributeRemoved(
        [this](InstanceId id, const std::string& name) {
            if (onAttributeRemoved_) onAttributeRemoved_(id, name);
        });
    propsPanel_.setOnTagAdded([this](InstanceId id, const std::string& tag) {
        if (onTagAdded_) onTagAdded_(id, tag);
    });
    propsPanel_.setOnTagRemoved([this](InstanceId id, const std::string& tag) {
        if (onTagRemoved_) onTagRemoved_(id, tag);
    });

    switchLeftTab(0);
    return true;
}

void PlaceView::destroy() {
    ClassPicker::close();
    // Deja de escuchar el World: los eventos apuntan a este objeto, que
    // esta a punto de desaparecer.
    if (worldEvents_) {
        worldEvents_->onAdded(nullptr);
        worldEvents_->onRemoved(nullptr);
        worldEvents_->onReparented(nullptr);
        worldEvents_ = nullptr;
    }
    propsPanel_.destroy();
    outputPanel_.destroy();
    treeWidget_.destroy();
    if (explorer_) {
        DestroyWindow(static_cast<HWND>(explorer_));
        explorer_ = nullptr;
    }
    if (properties_) {
        DestroyWindow(static_cast<HWND>(properties_));
        properties_ = nullptr;
    }
}

void PlaceView::setWorld(World* world) {
    world_ = world;
    worldEvents_ = world;
    propsPanel_.setWorld(world);
    treeModel_.setWorld(world);
    treeModel_.expandAll();
    if (treeWidget_.hwnd()) treeWidget_.refresh();
    if (!world) return;
    // El Explorer se suscribe a los eventos estructurales del World: alta,
    // baja y reubicacion refrescan solo lo afectado (el renombrado llega
    // por renameTreeItem). Los cambios de propiedades (onChanged) NO se
    // escuchan para no repintar en cada frame de un arrastre de gizmo.
    world->onAdded([this](InstanceId id, InstanceId parent) {
        treeModel_.onAdded(id, parent);
        if (treeWidget_.hwnd()) treeWidget_.refresh();
    });
    world->onRemoved([this](InstanceId id) {
        treeModel_.onRemoved(id);
        if (treeWidget_.hwnd()) treeWidget_.refresh();
    });
    world->onReparented([this](InstanceId id, InstanceId oldParent,
                               InstanceId newParent) {
        treeModel_.onReparented(id, oldParent, newParent);
        if (treeWidget_.hwnd()) treeWidget_.refresh();
    });
}

void PlaceView::setVisible(bool visible) {
    visible_ = visible;
    if (explorer_) ShowWindow(static_cast<HWND>(explorer_), visible ? SW_SHOW : SW_HIDE);
    if (properties_) ShowWindow(static_cast<HWND>(properties_), visible ? SW_SHOW : SW_HIDE);
}

void PlaceView::resize(int width, int height) {
    layoutPanels(width, height);
}

void PlaceView::rebuildTree(const World& world) {
    // La primera vez (o tras cambiar de World) se fija el modelo y se
    // despliega todo. Las siguientes reconstrucciones solo refrescan: los
    // eventos del World ya mantienen el modelo al dia y asi no se pierde
    // el estado de expansion al insertar, deshacer, etc.
    if (treeModel_.world() != &world) {
        treeModel_.setWorld(&world);
        treeModel_.expandAll();
    }
    if (treeWidget_.hwnd()) treeWidget_.refresh();
}

void PlaceView::clearObjects() {
    treeWidget_.select(std::vector<ui::NodeId>{}, 0);
    if (treeWidget_.hwnd()) treeWidget_.refresh();
}

void PlaceView::renameTreeItem(InstanceId id, const std::string&) {
    // El texto se lee del World: basta repintar la fila afectada.
    if (id == kInvalidId) return;
    treeWidget_.invalidateRow(static_cast<ui::NodeId>(id));
}

void PlaceView::removeTreeItem(InstanceId id) {
    if (id == kInvalidId) return;
    if (treeWidget_.hwnd()) treeWidget_.refresh();
}

void PlaceView::showObject(const Instance& instance, bool) {
    propsPanel_.setSelection({instance.id()});
}

void PlaceView::showRoot() { propsPanel_.setSelection({}); }

void PlaceView::showNoSelection() { propsPanel_.setSelection({}); }

void PlaceView::showSelection(const std::vector<InstanceId>& ids) {
    propsPanel_.setSelection(ids);
}

void PlaceView::syncTreeSelection(InstanceId primary) {
    if (!treeWidget_.hwnd()) return;
    if (primary == kInvalidId) {
        suppressNotify_ = true;
        treeWidget_.select(std::vector<ui::NodeId>{}, 0);
        suppressNotify_ = false;
        return;
    }
    // Expande hasta el nodo, lo selecciona y lo lleva a la vista.
    suppressNotify_ = true;
    treeWidget_.scrollTo(static_cast<ui::NodeId>(primary));
    treeWidget_.select(std::vector<ui::NodeId>{static_cast<ui::NodeId>(primary)},
                       static_cast<ui::NodeId>(primary));
    suppressNotify_ = false;
}

void PlaceView::notifySelection() {
    if (suppressNotify_) return;
    if (!onSelectionChanged_) return;
    const ui::NodeId p = treeWidget_.primary();
    InstanceId id = kInvalidId;
    if (p != 0 && p != ui::kTreeRootId && p != ui::kDimGroupId && world_ &&
        world_->exists(static_cast<InstanceId>(p))) {
        id = static_cast<InstanceId>(p);
    }
    onSelectionChanged_(id);
}

void PlaceView::layoutChildren(int) {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    RECT rc{};
    GetClientRect(props, &rc);
    const RECT r{0, kHeaderHeight, rc.right, rc.bottom};
    propsPanel_.setRect(r);
    outputPanel_.setRect(r);
}

void PlaceView::layoutPanels(int width, int height) {
    if (!explorer_ || !properties_) return;
    const int y = editor::Layout::contentTopStatic();
    int panelH = height - y;
    if (panelH < 0) panelH = 0;

    // Properties a la izquierda, Explorer a la derecha.
    MoveWindow(static_cast<HWND>(properties_), 0, y, editor::Layout::propertiesWidth(), panelH, TRUE);
    MoveWindow(static_cast<HWND>(explorer_),
               width - editor::Layout::explorerWidth(), y, editor::Layout::explorerWidth(), panelH, TRUE);

    if (treeWidget_.hwnd()) {
        MoveWindow(treeWidget_.hwnd(), 6, kHeaderHeight + 6,
                   editor::Layout::explorerWidth() - 12, panelH - kHeaderHeight - 12, TRUE);
    }

    layoutChildren(panelH);
}

void PlaceView::switchLeftTab(int tab) {
    leftTab_ = (tab == 1) ? 1 : 0;
    if (propsPanel_.hwnd()) {
        ShowWindow(propsPanel_.hwnd(), leftTab_ == 0 ? SW_SHOW : SW_HIDE);
    }
    if (outputPanel_.hwnd()) {
        ShowWindow(outputPanel_.hwnd(), leftTab_ == 1 ? SW_SHOW : SW_HIDE);
    }
    if (properties_) InvalidateRect(static_cast<HWND>(properties_), nullptr, TRUE);
}

void PlaceView::openClassPicker(InstanceId parentId, POINT screenPoint) {
    HWND owner = GetAncestor(treeWidget_.hwnd(), GA_ROOT);
    ClassPicker::open(owner, screenPoint.x, screenPoint.y,
                      [this, parentId](const std::string& className) {
                          if (onInsertClass_) onInsertClass_(parentId, className);
                      });
}

void PlaceView::appendOutput(int level, const std::string& message) {
    outputPanel_.addLine(level, message);
}

long long __stdcall PlaceView::wndProc(void* hwndPtr, unsigned int msg,
                                        unsigned long long wParam,
                                        long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    PlaceView* self =
        reinterpret_cast<PlaceView*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<PlaceView*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, theme::backgroundBrush());

            // Barra de pestanas: izquierda (Properties|Output) y derecha
            // (Explorer; el grupo Assets vive dentro del propio arbol).
            if (self && hwnd == static_cast<HWND>(self->properties_)) {
                static const char* kLeftTabs[2] = {"Properties", "Output"};
                paintTabs(hdc, rc, kLeftTabs, 2, self->leftTab_);
            } else if (self &&
                       hwnd == static_cast<HWND>(self->explorer_)) {
                static const char* kRightTabs[1] = {"Explorer"};
                paintTabs(hdc, rc, kRightTabs, 1, 0);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            // Click en la barra de pestanas del panel izquierdo.
            if (self && hwnd == static_cast<HWND>(self->properties_)) {
                const int x = static_cast<short>(LOWORD(lParam));
                const int y = static_cast<short>(HIWORD(lParam));
                if (y < kHeaderHeight) {
                    RECT rc{};
                    GetClientRect(hwnd, &rc);
                    for (int i = 0; i < 2; ++i) {
                        RECT tr = tabRect(rc, i, 2);
                        if (x >= tr.left && x < tr.right) {
                            if (self->leftTab_ != i) self->switchLeftTab(i);
                            return 0;
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace sk
