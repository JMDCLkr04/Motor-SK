#include "tree_widget.h"

#include <windowsx.h>

#include <algorithm>
#include <unordered_set>

#include "../theme.h"
#include "disclosure.h"
#include "icon_atlas.h"

namespace sk {
namespace ui {

TreeWidget::TreeWidget() {
    vbar_.setOnScroll([this](int p) { scrollY_ = p; invalidate(); });
}

void TreeWidget::create(HWND parent, int x, int y, int w, int h) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(inst, L"MOTORSK_TREEWIDGET", &wc)) {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.hbrBackground = theme::listBackgroundBrush();
        wc.lpszClassName = L"MOTORSK_TREEWIDGET";
        RegisterClassExW(&wc);
    }
    hwnd_ = CreateWindowExW(0, L"MOTORSK_TREEWIDGET", L"",
                            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, x, y, w, h,
                            parent, nullptr, inst, this);
    vbar_.attach(hwnd_, 100, true);
    invalidate();
}

void TreeWidget::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void TreeWidget::setWorldRoot(NodeId rootId) { rootId_ = rootId; }

void TreeWidget::clear() {
    items_.clear();
    byId_.clear();
    sel_.clear();
    primary_ = 0;
    buildList();
    vbar_.setRange(0, std::max<int>(0, (int)visible_.size() * 22 - 200));
    vbar_.update();
    invalidate();
}

void TreeWidget::setNodes(const std::vector<TreeNode>& flat) {
    items_.clear();
    byId_.clear();
    for (const TreeNode& f : flat) {
        Item it{};
        it.id = f.id;
        it.parentId = f.parentId;
        it.text = f.text;
        it.hasChildren = f.hasChildren;
        it.expanded = f.expanded;
        it.protectedNode = f.protectedNode;
        it.level = 0;
        byId_[it.id] = (int)items_.size();
        items_.push_back(std::move(it));
    }
    buildList();
    RECT r;
    if (hwnd_) GetClientRect(hwnd_, &r);
    const int page = hwnd_ ? r.bottom : 200;
    vbar_.setPage(page);
    vbar_.setRange(0, std::max<int>(0, (int)visible_.size() * 22 - page));
    vbar_.update();
    invalidate();
}

void TreeWidget::buildList() {
    visible_.clear();
    std::unordered_map<NodeId, bool> vmap;
    for (size_t i = 0; i < items_.size(); ++i) {
        Item& it = items_[i];
        bool vis = true;
        int level = 0;
        if (it.parentId != 0) {
            auto f = byId_.find(it.parentId);
            if (f != byId_.end()) {
                const Item& par = items_[f->second];
                vis = vmap[par.id] ? par.expanded : false;
                level = par.level + 1;
            }
        }
        it.level = level;
        vmap[it.id] = vis;
        if (vis) visible_.push_back((int)i);
    }
}

void TreeWidget::select(const std::vector<NodeId>& ids, NodeId primary) {
    sel_ = ids;
    primary_ = primary;
    if (onSel_) onSel_(sel_);
    invalidate();
}

std::vector<NodeId> TreeWidget::selection() const { return sel_; }
NodeId TreeWidget::primary() const { return primary_; }

void TreeWidget::updateNode(NodeId id, const TreeNode& node) {
    auto f = byId_.find(id);
    if (f == byId_.end()) return;
    Item& it = items_[f->second];
    it.parentId = node.parentId;
    it.text = node.text;
    it.hasChildren = node.hasChildren;
    it.expanded = node.expanded;
    it.protectedNode = node.protectedNode;
    buildList();
    invalidate();
}

void TreeWidget::removeNode(NodeId id) {
    auto f = byId_.find(id);
    if (f == byId_.end()) return;
    // Borra tambien los descendientes (subiendo por la cadena de padres).
    std::unordered_set<NodeId> doomed{id};
    for (const Item& it : items_) {
        NodeId p = it.parentId;
        while (p != 0) {
            if (doomed.count(p)) {
                doomed.insert(it.id);
                break;
            }
            auto pf = byId_.find(p);
            if (pf == byId_.end()) break;
            p = items_[pf->second].parentId;
        }
    }
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [&](const Item& it) {
                                    return doomed.count(it.id) != 0;
                                }),
                 items_.end());
    byId_.clear();
    for (size_t i = 0; i < items_.size(); ++i) byId_[items_[i].id] = (int)i;
    auto it = std::remove_if(sel_.begin(), sel_.end(),
                             [&](NodeId sid) { return doomed.count(sid) != 0; });
    sel_.erase(it, sel_.end());
    if (doomed.count(primary_)) primary_ = 0;
    buildList();
    invalidate();
}

void TreeWidget::expandTo(NodeId id) {
    NodeId p = id;
    for (size_t n = 0; n < items_.size() + 1; ++n) {
        auto f = byId_.find(p);
        if (f == byId_.end()) break;
        items_[f->second].expanded = true;
        p = items_[f->second].parentId;
        if (p == 0) break;
    }
    buildList();
    invalidate();
}

void TreeWidget::scrollTo(NodeId id) {
    auto f = byId_.find(id);
    if (f == byId_.end()) return;
    const int idx = f->second;
    auto v = std::find(visible_.begin(), visible_.end(), idx);
    if (v == visible_.end()) {
        expandTo(id);
        v = std::find(visible_.begin(), visible_.end(), idx);
        if (v == visible_.end()) return;
    }
    const int row = (int)(v - visible_.begin());
    ensureVisible(row);
}

void TreeWidget::invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void TreeWidget::ensureVisible(int idx) {
    RECT r;
    if (hwnd_) GetClientRect(hwnd_, &r);
    const int view = hwnd_ ? r.bottom : 200;
    const int top = idx * 22;
    const int bot = (idx + 1) * 22;
    if (scrollY_ > top) {
        vbar_.setPos(top);
    } else if (scrollY_ + view < bot) {
        vbar_.setPos(bot - view);
    }
}

int TreeWidget::hitRow(int y) { return (y + scrollY_) / 22; }

LRESULT CALLBACK TreeWidget::wndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {
    TreeWidget* self =
        reinterpret_cast<TreeWidget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<TreeWidget*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
        case WM_SIZE: {
            RECT r;
            GetClientRect(hwnd, &r);
            self->vbar_.setRect({r.right - 12, 0, r.right, r.bottom});
            self->vbar_.setPage(r.bottom);
            self->vbar_.setRange(
                0, std::max<int>(0, (int)self->visible_.size() * 22 - r.bottom));
            self->vbar_.update();
            break;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            if (self->pb_.beginPaint(hwnd, &ps)) {
                self->paint(self->pb_.dc());
                self->pb_.endPaint();
            } else {
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            self->vbar_.onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            self->scrollY_ = self->vbar_.pos();
            self->invalidate();
            break;
        }
        case WM_CAPTURECHANGED:
            self->vbar_.onCaptureChanged();
            break;
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const int row = self->hitRow(y);
            if (row < 0 || row >= (int)self->visible_.size()) break;
            const int idx = self->visible_[row];
            Item& it = self->items_[idx];
            const int dx = it.level * 14 + 4;
            if (it.hasChildren && x >= dx && x <= dx + 14) {
                it.expanded = !it.expanded;
                if (self->onExp_) self->onExp_(it.id, it.expanded);
                self->buildList();
                self->invalidate();
            } else {
                self->select({it.id}, it.id);
            }
            break;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void TreeWidget::paint(HDC hdc) {
    RECT r;
    GetClientRect(hwnd_, &r);
    FillRect(hdc, &r, theme::listBackgroundBrush());
    int y = -scrollY_;
    for (int i = 0; i < (int)visible_.size() && y < r.bottom; ++i) {
        const int idx = visible_[i];
        if (y + 22 > 0) {
            RECT tr{0, y, r.right - 12, y + 22};
            const bool isSel =
                std::find(sel_.begin(), sel_.end(), items_[idx].id) != sel_.end();
            if (isSel) {
                HBRUSH hb = CreateSolidBrush(RGB(64, 92, 140));
                FillRect(hdc, &tr, hb);
                DeleteObject(hb);
            }
            const int dx = items_[idx].level * 14 + 4;
            if (items_[idx].hasChildren) {
                RECT dr{dx, y + 7, dx + 10, y + 15};
                drawDisclosure(hdc, dr, items_[idx].expanded);
            }
            drawIcon(hdc, IconId::Part, dx + 12, y + 3, 16, theme::text());
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, theme::text());
            HFONT of = static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
            std::wstring t = widen(items_[idx].text);
            ExtTextOutW(hdc, dx + 32, y + 4, ETO_CLIPPED, &tr, t.c_str(),
                        (int)t.size(), nullptr);
            SelectObject(hdc, of);
            if (items_[idx].protectedNode) {
                drawIcon(hdc, IconId::UiStop, r.right - 24, y + 3, 12,
                         RGB(160, 160, 180));
            }
        }
        y += 22;
    }
    vbar_.paint(hdc);
}

} // namespace ui
} // namespace sk