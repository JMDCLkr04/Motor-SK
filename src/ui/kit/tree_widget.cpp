#include "tree_widget.h"

#include <cstring>
#include <algorithm>

#include "../theme.h"
#include "disclosure.h"
#include "icon_atlas.h"

namespace sk {
namespace ui {

TreeWidget::TreeWidget() {
    vbar_.setOnScroll([this](int p){ scrollY_ = p; invalidate(); });
}

void TreeWidget::create(HWND parent, int x, int y, int w, int h) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSA wc{};
    if (!GetClassInfoA(inst, "MOTORSK_TREEWIDGET", &wc)) {
        wc.style = CS_VREDRAW | CS_HREDRAW;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hbrBackground = theme::listBackgroundBrush();
        wc.lpszClassName = "MOTORSK_TREEWIDGET";
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        RegisterClassA(&wc);
    }
    hwnd_ = CreateWindowA("MOTORSK_TREEWIDGET", "", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                          x, y, w, h, parent, nullptr, inst, this);
    vbar_.attach(hwnd_, 100, true);
    invalidate();
}

void TreeWidget::destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
}

void TreeWidget::setWorldRoot(void* rootId) { rootId_ = rootId; }

void TreeWidget::clear() {
    items_.clear();
    byId_.clear();
    sel_.clear();
    primary_ = nullptr;
    buildList();
    vbar_.setRange(0, std::max<int>(0, (int)visible_.size() * 22 - 200));
    vbar_.update();
}

void TreeWidget::setNodes(const std::vector<TreeNode>& flat) {
    items_.clear();
    byId_.clear();
    for (auto& f : flat) {
        Item it{};
        it.id = f.id;
        it.parentId = f.parentId;
        strncpy_s(it.text, f.text, _TRUNCATE);
        it.hasChildren = f.hasChildren;
        it.expanded = f.expanded;
        it.protectedNode = f.protectedNode;
        it.level = 0;
        items_.push_back(it);
    }
    buildList();
    RECT r; GetClientRect(hwnd_, &r);
    vbar_.setPage(r.bottom);
    vbar_.setRange(0, std::max<int>(0, (int)visible_.size() * 22 - r.bottom));
    vbar_.update();
    invalidate();
}

void TreeWidget::buildList() {
    visible_.clear();
    for (size_t i = 0; i < items_.size(); ++i) {
        // simple flat pass
        visible_.push_back((int)i);
    }
}

void TreeWidget::select(const std::vector<void*>& ids, void* primary) {
    sel_ = ids;
    primary_ = primary;
    if (onSel_) onSel_(sel_);
    invalidate();
}

std::vector<void*> TreeWidget::selection() const { return sel_; }
void* TreeWidget::primary() const { return primary_; }

void TreeWidget::invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

LRESULT CALLBACK TreeWidget::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TreeWidget* self = reinterpret_cast<TreeWidget*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<TreeWidget*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcA(hwnd, msg, wParam, lParam);
    switch (msg) {
        case WM_SIZE: {
            RECT r; GetClientRect(hwnd, &r);
            self->vbar_.setRect({r.right - 12, 0, r.right, r.bottom});
            self->vbar_.setPage(r.bottom);
            self->vbar_.setRange(0, std::max<int>(0, (int)self->visible_.size() * 22 - r.bottom));
            self->vbar_.update();
            break;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            if (self->pb_.beginPaint(hwnd, &ps)) {
                self->paint(self->pb_.dc());
                self->pb_.endPaint();
            } else { BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); }
            break;
        }
        case WM_MOUSEWHEEL: {
            self->vbar_.onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            self->scrollY_ = self->vbar_.pos();
            self->invalidate();
            break;
        }
        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            int row = self->hitRow(y + self->scrollY_);
            if (row >= 0 && row < (int)self->visible_.size()) {
                int idx = self->visible_[row];
                self->select({self->items_[idx].id}, self->items_[idx].id);
            }
            break;
        }
        default: return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void TreeWidget::paint(HDC hdc) {
    RECT r; GetClientRect(hwnd_, &r);
    FillRect(hdc, &r, theme::listBackgroundBrush());
    int y = -scrollY_;
    for (size_t i = 0; i < visible_.size() && y < r.bottom; ++i) {
        int idx = visible_[i];
        if (y + 22 > 0) {
            RECT tr{0, y, r.right - 12, y + 22};
            // highlight selection
            for (auto& sid : sel_) {
                if (sid == items_[idx].id) {
                    HBRUSH hb = CreateSolidBrush(RGB(64, 92, 140));
                    FillRect(hdc, &tr, hb);
                    DeleteObject(hb);
                    break;
                }
            }
            int dx = items_[idx].level * 14 + 4;
            if (items_[idx].hasChildren) {
                RECT dr{dx, y + 7, dx + 10, y + 15};
                drawDisclosure(hdc, dr, items_[idx].expanded);
            }
            drawIcon(hdc, IconId::Part, dx + 12, y + 3, 16, theme::text());
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, theme::text());
            HFONT of = (HFONT)SelectObject(hdc, theme::uiFont());
            TextOutA(hdc, dx + 32, y + 4, items_[idx].text, (int)strlen(items_[idx].text));
            SelectObject(hdc, of);
            if (items_[idx].protectedNode) {
                drawIcon(hdc, IconId::UiStop, r.right - 24, y + 3, 12, RGB(160, 160, 180));
            }
        }
        y += 22;
    }
    vbar_.paint(hdc);
}

int TreeWidget::hitRow(int y) { return y / 22; }

void TreeWidget::updateNode(void* id, const TreeNode& node) {}
void TreeWidget::removeNode(void* id) {}
void TreeWidget::expandTo(void* id) {}
void TreeWidget::scrollTo(void* id) {}

} // namespace ui
} // namespace sk
