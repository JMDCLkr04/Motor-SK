#include "tree_widget.h"

#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <unordered_map>

#include "../theme.h"
#include "disclosure.h"
#include "icon_atlas.h"

namespace sk {
namespace ui {

namespace {

constexpr int kRowH = 22;     // alto de fila (virtualizada)
constexpr int kIndent = 16;   // sangria por nivel
constexpr int kIconSize = 16;
constexpr int kScrollW = 12;

COLORREF rowSelected() { return RGB(56, 88, 150); }
COLORREF rowHover() { return RGB(40, 40, 48); }
COLORREF dropValidColor() { return RGB(96, 190, 110); }
COLORREF dropInvalidColor() { return RGB(214, 74, 74); }

// Boton circular "+" que solo se dibuja en la fila bajo el raton.
void drawPlus(HDC hdc, const RECT& r) {
    HBRUSH fill = CreateSolidBrush(RGB(48, 48, 58));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(170, 170, 182));
    HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, fill));
    HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
    Ellipse(hdc, r.left, r.top, r.right, r.bottom);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(fill);
    DeleteObject(pen);

    const int cx = (r.left + r.right) / 2;
    const int cy = (r.top + r.bottom) / 2;
    const int h = 4;
    HPEN cross = CreatePen(PS_SOLID, 1, theme::text());
    HPEN oc = static_cast<HPEN>(SelectObject(hdc, cross));
    MoveToEx(hdc, cx - h, cy, nullptr);
    LineTo(hdc, cx + h + 1, cy);
    MoveToEx(hdc, cx, cy - h, nullptr);
    LineTo(hdc, cx, cy + h + 1);
    SelectObject(hdc, oc);
    DeleteObject(cross);
}

// Candado discreto de los nodos protegidos: arco + cuerpo.
void drawLock(HDC hdc, int x, int y) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(176, 176, 190));
    HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
    HBRUSH nullBrush = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, nullBrush));
    Ellipse(hdc, x + 1, y, x + 8, y + 7);  // arco
    SelectObject(hdc, ob);
    HBRUSH body = CreateSolidBrush(RGB(120, 120, 136));
    HBRUSH ob2 = static_cast<HBRUSH>(SelectObject(hdc, body));
    Rectangle(hdc, x, y + 4, x + 9, y + 10);
    SelectObject(hdc, ob2);
    SelectObject(hdc, op);
    DeleteObject(body);
    DeleteObject(pen);
}

}  // namespace

TreeWidget::TreeWidget() {
    vbar_.setOnScroll([this](int p) {
        if (edit_) commitRename(true);
        scrollY_ = p;
        invalidate();
    });
}

void TreeWidget::create(HWND parent, int x, int y, int w, int h) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(inst, L"MOTORSK_TREEWIDGET", &wc)) {
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = theme::listBackgroundBrush();
        wc.lpszClassName = L"MOTORSK_TREEWIDGET";
        RegisterClassExW(&wc);
    }
    hwnd_ = CreateWindowExW(0, L"MOTORSK_TREEWIDGET", L"",
                            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN |
                                WS_TABSTOP,
                            x, y, w, h, parent, nullptr, inst, this);
    vbar_.attach(hwnd_, 100, true);
    updateScroll();
    invalidate();
}

void TreeWidget::destroy() {
    if (edit_) {
        RemoveWindowSubclass(edit_, editProc, 1);
        DestroyWindow(edit_);
        edit_ = nullptr;
    }
    editing_ = 0;
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void TreeWidget::buildList() {
    rows_.clear();
    rowIndex_.clear();
    if (model_) rows_ = model_->rows();

#ifndef NDEBUG
    // El aplanado del TreeModel debe venir en preorden (el padre antes que
    // sus hijos y con nivel = nivel del padre + 1).
    std::unordered_map<NodeId, int> pos;
    bool ok = true;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        pos[rows_[i].id] = i;
        const NodeId p = rows_[i].parentId;
        if (p == 0 || p == kTreeRootId) continue;
        auto it = pos.find(p);
        if (it == pos.end() || it->second >= i ||
            rows_[i].level != rows_[it->second].level + 1) {
            ok = false;
        }
    }
    assert(ok && "el aplanado del TreeModel debe venir en preorden");
#endif

    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        rowIndex_[rows_[i].id] = i;
    }
    updateScroll();
}

void TreeWidget::updateScroll() {
    if (!hwnd_) return;
    RECT r{};
    GetClientRect(hwnd_, &r);
    clientW_ = r.right;
    clientH_ = r.bottom;
    const int page = r.bottom;
    const int content = static_cast<int>(rows_.size()) * kRowH;
    const int maxScroll = (std::max)(0, content - page);
    if (scrollY_ > maxScroll) scrollY_ = maxScroll;
    if (scrollY_ < 0) scrollY_ = 0;
    vbar_.setRect({r.right - kScrollW, 0, r.right, r.bottom});
    vbar_.setPage(page);
    vbar_.setRange(0, maxScroll);
    vbar_.setPos(scrollY_);
    vbar_.update();
}

void TreeWidget::refresh() {
    buildList();
    if (editing_ && rowOf(editing_) < 0) commitRename(false);
    invalidate();
}

void TreeWidget::invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void TreeWidget::invalidateRow(NodeId id) {
    const int row = rowOf(id);
    if (row < 0) {
        invalidate();
        return;
    }
    RECT r = rowRect(row);
    if (hwnd_) InvalidateRect(hwnd_, &r, FALSE);
}

int TreeWidget::hitRow(int y) const {
    const int row = (y + scrollY_) / kRowH;
    if (row < 0 || row >= static_cast<int>(rows_.size())) return -1;
    return row;
}

RECT TreeWidget::rowRect(int row) const {
    RECT r{};
    r.left = 0;
    r.right = clientW_ - kScrollW;
    r.top = row * kRowH - scrollY_;
    r.bottom = r.top + kRowH;
    return r;
}

RECT TreeWidget::disclosureRect(int row) const {
    const RECT rr = rowRect(row);
    const int dx = 6 + rows_[row].level * kIndent;
    const int cy = (rr.top + rr.bottom) / 2;
    RECT d{};
    d.left = dx;
    d.right = dx + 10;
    d.top = cy - 4;
    d.bottom = cy + 4;
    return d;
}

RECT TreeWidget::plusRect(int row) const {
    const RECT rr = rowRect(row);
    RECT p{};
    p.right = rr.right - 6;
    p.left = p.right - 14;
    p.top = rr.top + 4;
    p.bottom = p.top + 14;
    return p;
}

NodeId TreeWidget::rowIdAt(int row) const {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return 0;
    return rows_[row].id;
}

int TreeWidget::rowOf(NodeId id) const {
    auto it = rowIndex_.find(id);
    return it == rowIndex_.end() ? -1 : it->second;
}

void TreeWidget::select(const std::vector<NodeId>& ids, NodeId primary) {
    sel_ = ids;
    primary_ = primary;
    if (primary != 0) anchor_ = primary;
    notifySelection();
    invalidate();
}

void TreeWidget::notifySelection() {
    if (onSel_) onSel_(sel_);
}

void TreeWidget::expandTo(NodeId id) {
    if (!model_) return;
    model_->expandTo(id);
    refresh();
}

void TreeWidget::scrollTo(NodeId id) {
    expandTo(id);
    const int row = rowOf(id);
    if (row < 0) return;
    const int top = row * kRowH;
    const int bot = top + kRowH;
    if (scrollY_ > top) {
        vbar_.setPos(top);
    } else if (scrollY_ + clientH_ < bot) {
        vbar_.setPos(bot - clientH_);
    }
    scrollY_ = vbar_.pos();
    invalidate();
}

void TreeWidget::setHover(NodeId id) {
    if (id == hover_) return;
    const int old = hover_ ? rowOf(hover_) : -1;
    hover_ = id;
    const int now = id ? rowOf(id) : -1;
    if (old >= 0) {
        RECT r = rowRect(old);
        InvalidateRect(hwnd_, &r, FALSE);
    }
    if (now >= 0) {
        RECT r = rowRect(now);
        InvalidateRect(hwnd_, &r, FALSE);
    }
}

void TreeWidget::clearHover() { setHover(0); }

void TreeWidget::movePrimary(NodeId to) {
    select({to}, to);
    const int row = rowOf(to);
    if (row >= 0) {
        const int top = row * kRowH;
        const int bot = top + kRowH;
        if (scrollY_ > top) {
            vbar_.setPos(top);
        } else if (scrollY_ + clientH_ < bot) {
            vbar_.setPos(bot - clientH_);
        }
        scrollY_ = vbar_.pos();
    }
    invalidate();
}

void TreeWidget::cancelDrag() {
    dragging_ = false;
    dragArmed_ = false;
    dropId_ = 0;
    dropValid_ = false;
    dragId_ = 0;
    autoScrollDir_ = 0;
    if (hwnd_) KillTimer(hwnd_, 1);
    if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
    invalidate();
}

void TreeWidget::updateDropTarget(int x, int y) {
    (void)x;
    if (!model_) return;
    const int row = hitRow(y);
    if (row < 0) {
        dropId_ = 0;
        dropValid_ = false;
        return;
    }
    const RECT rr = rowRect(row);
    int zone = 1;
    if (y < rr.top + kRowH / 4) {
        zone = 0;
    } else if (y > rr.bottom - kRowH / 4) {
        zone = 2;
    }
    dropZone_ = zone == 0 ? DropZone::Before
                          : (zone == 2 ? DropZone::After : DropZone::Inside);
    dropId_ = rows_[row].id;
    const DropDecision d = model_->dropTarget(dragId_, dropId_, dropZone_);
    dropValid_ = d.valid;
}

void TreeWidget::beginInlineEdit(NodeId id) {
    if (!model_ || !hwnd_ || model_->isProtected(id)) return;
    commitRename(true);
    const int row = rowOf(id);
    if (row < 0) return;

    const RECT rr = rowRect(row);
    const int dx = 6 + rows_[row].level * kIndent;
    int x = dx + 34;
    int w = rr.right - x - 4;
    if (w < 40) w = 40;
    const std::wstring cur = widen(model_->textOf(id));
    edit_ = CreateWindowExW(0, L"EDIT", cur.c_str(),
                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
                            x, rr.top + 1, w, kRowH - 2, hwnd_, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
    SendMessageW(edit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    SetWindowSubclass(edit_, editProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(edit_, EM_SETSEL, 0, -1);
    editing_ = id;
    SetFocus(edit_);
}

void TreeWidget::commitRename(bool accept) {
    if (!edit_) return;
    HWND e = edit_;
    const NodeId id = editing_;
    edit_ = nullptr;
    editing_ = 0;
    std::string newName;
    if (accept) {
        const int len = GetWindowTextLengthW(e);
        std::vector<wchar_t> buf(len + 1, L'\0');
        GetWindowTextW(e, buf.data(), len + 1);
        newName = narrow(std::wstring(buf.data()));
    }
    RemoveWindowSubclass(e, editProc, 1);
    DestroyWindow(e);
    if (hwnd_) SetFocus(hwnd_);
    if (accept && id != 0 && !newName.empty() && model_ &&
        newName != model_->textOf(id) && onRename_) {
        onRename_(id, newName);
    }
    invalidate();
}

LRESULT CALLBACK TreeWidget::editProc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam, UINT_PTR uId,
                                      DWORD_PTR ref) {
    (void)uId;
    TreeWidget* self = reinterpret_cast<TreeWidget*>(ref);
    if (self) {
        if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
            self->commitRename(true);
            return 0;
        }
        if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
            self->commitRename(false);
            return 0;
        }
        if (msg == WM_KILLFOCUS && self->edit_) {
            self->commitRename(true);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void TreeWidget::paintRow(HDC hdc, int row, const RECT& rr) {
    const TreeNode& n = rows_[row];
    const bool isSel =
        std::find(sel_.begin(), sel_.end(), n.id) != sel_.end();
    const bool isHover = (hover_ == n.id && !dragging_);

    if (isSel) {
        HBRUSH b = CreateSolidBrush(rowSelected());
        FillRect(hdc, &rr, b);
        DeleteObject(b);
    } else if (isHover) {
        HBRUSH b = CreateSolidBrush(rowHover());
        FillRect(hdc, &rr, b);
        DeleteObject(b);
    }
    if (focused_ && isSel && n.id == primary_) {
        RECT bar{rr.left, rr.top, rr.left + 2, rr.bottom};
        HBRUSH b = CreateSolidBrush(theme::accent());
        FillRect(hdc, &bar, b);
        DeleteObject(b);
    }

    const int dx = 6 + n.level * kIndent;
    if (n.hasChildren) {
        drawDisclosure(hdc, disclosureRect(row), n.expanded);
    }
    drawIcon(hdc, n.icon, dx + 14, rr.top + 3, kIconSize, theme::text());

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, theme::text());
    HFONT of = static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
    const std::wstring t = model_ ? widen(model_->textOf(n.id)) : std::wstring();
    RECT tc{dx + 34, rr.top, rr.right - 40, rr.bottom};
    ExtTextOutW(hdc, dx + 34, rr.top + 4, ETO_CLIPPED, &tc, t.c_str(),
                static_cast<int>(t.size()), nullptr);
    SelectObject(hdc, of);

    if (n.protectedNode) drawLock(hdc, rr.right - 40, rr.top + 6);
    if (isHover) drawPlus(hdc, plusRect(row));
}

void TreeWidget::paintGhost(HDC hdc) {
    if (!dragging_ || dragId_ == 0 || !model_) return;
    RECT cr{};
    GetClientRect(hwnd_, &cr);
    const int w = cr.right - kScrollW;
    if (w <= 0) return;

    HDC mem = CreateCompatibleDC(hdc);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -kRowH;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp =
        CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) {
        DeleteDC(mem);
        return;
    }
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, bmp));

    RECT rr{0, 0, w, kRowH};
    HBRUSH bg = CreateSolidBrush(RGB(52, 52, 62));
    FillRect(mem, &rr, bg);
    DeleteObject(bg);
    drawIcon(mem, model_->iconOf(dragId_), 12, 3, kIconSize, theme::text());
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, theme::text());
    HFONT of = static_cast<HFONT>(SelectObject(mem, theme::uiFont()));
    const std::wstring t = widen(model_->textOf(dragId_));
    ExtTextOutW(mem, 30, 4, ETO_CLIPPED, &rr, t.c_str(),
                static_cast<int>(t.size()), nullptr);
    SelectObject(mem, of);

    int y = cursor_.y - kRowH / 2;
    if (y < 0) y = 0;
    if (y + kRowH > cr.bottom) y = cr.bottom - kRowH;
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 150, 0};
    AlphaBlend(hdc, 0, y, w, kRowH, mem, 0, 0, w, kRowH, bf);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

void TreeWidget::paint(HDC hdc) {
    RECT cr{};
    GetClientRect(hwnd_, &cr);
    FillRect(hdc, &cr, theme::listBackgroundBrush());

    const int first = (std::max)(0, scrollY_ / kRowH);
    const int last = (std::min)(
        static_cast<int>(rows_.size()),
        static_cast<int>((scrollY_ + cr.bottom) / kRowH + 2));
    for (int i = first; i < last; ++i) {
        paintRow(hdc, i, rowRect(i));
    }

    // Feedback del arrastre: linea (antes/despues) o marco (dentro).
    if (dragging_ && dropId_ != 0) {
        const int row = rowOf(dropId_);
        if (row >= 0) {
            const RECT rr = rowRect(row);
            const COLORREF c = dropValid_ ? dropValidColor() : dropInvalidColor();
            HPEN pen = CreatePen(PS_SOLID, 2, c);
            HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
            HBRUSH ob = static_cast<HBRUSH>(SelectObject(
                hdc, GetStockObject(NULL_BRUSH)));
            if (dropZone_ == DropZone::Inside) {
                Rectangle(hdc, rr.left + 1, rr.top + 1, rr.right - 1,
                          rr.bottom - 1);
            } else {
                const int ly = dropZone_ == DropZone::Before ? rr.top + 1
                                                             : rr.bottom - 2;
                MoveToEx(hdc, rr.left + 1, ly, nullptr);
                LineTo(hdc, rr.right - 1, ly);
            }
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(pen);
        }
    }

    paintGhost(hdc);
    vbar_.paint(hdc);
}

LRESULT CALLBACK TreeWidget::wndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {
    TreeWidget* self =
        reinterpret_cast<TreeWidget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<TreeWidget*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_SIZE:
            self->updateScroll();
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
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
        case WM_SETFOCUS:
            self->focused_ = true;
            self->invalidate();
            break;
        case WM_KILLFOCUS:
            // Si el foco pasa al EDIT de renombrado en linea, no se cierra:
            // el editor es hijo del arbol y acaba de recibir el foco.
            if (self->edit_ && GetFocus() == self->edit_) break;
            self->focused_ = false;
            self->commitRename(true);
            self->invalidate();
            break;
        case WM_MOUSEWHEEL: {
            if (self->edit_) self->commitRename(true);
            self->vbar_.onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            self->scrollY_ = self->vbar_.pos();
            self->invalidate();
            break;
        }
        case WM_CAPTURECHANGED:
            self->vbar_.onCaptureChanged();
            if (self->dragging_ || self->dragArmed_) {
                self->dragging_ = false;
                self->dragArmed_ = false;
                self->dropId_ = 0;
                self->dropValid_ = false;
                self->dragId_ = 0;
                self->autoScrollDir_ = 0;
                KillTimer(hwnd, 1);
                self->invalidate();
            }
            break;
        case WM_TIMER:
            if (wParam == 1 && self->dragging_ && self->autoScrollDir_ != 0) {
                self->vbar_.setPos(self->scrollY_ +
                                   self->autoScrollDir_ * kRowH);
                self->scrollY_ = self->vbar_.pos();
                self->updateDropTarget(self->cursor_.x, self->cursor_.y);
                self->invalidate();
            }
            break;
        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (self->vbar_.dragging()) {
                self->vbar_.onMouseMove(x, y);
                self->scrollY_ = self->vbar_.pos();
                self->invalidate();
                return 0;
            }
            if (self->dragging_) {
                self->cursor_ = {x, y};
                self->lastMouseY_ = y;
                self->updateDropTarget(x, y);
                RECT cr{};
                GetClientRect(hwnd, &cr);
                int dir = 0;
                if (y < kRowH) {
                    dir = -1;
                } else if (y > cr.bottom - kRowH) {
                    dir = 1;
                }
                if (dir != self->autoScrollDir_) {
                    self->autoScrollDir_ = dir;
                    if (dir != 0) {
                        SetTimer(hwnd, 1, 40, nullptr);
                    } else {
                        KillTimer(hwnd, 1);
                    }
                }
                self->invalidate();
                return 0;
            }
            if (self->dragArmed_ &&
                (std::abs(x - self->dragStart_.x) > 4 ||
                 std::abs(y - self->dragStart_.y) > 4)) {
                if (self->dragId_ != 0 && self->model_ &&
                    !self->model_->isProtected(self->dragId_)) {
                    self->dragging_ = true;
                    self->clearHover();
                    SetCapture(hwnd);
                    self->cursor_ = {x, y};
                    self->updateDropTarget(x, y);
                    self->invalidate();
                    return 0;
                }
                self->dragArmed_ = false;
            }
            const int row = self->hitRow(y);
            self->setHover(self->rowIdAt(row));
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (!self->dragging_) self->clearHover();
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (self->vbar_.hitTest(x, y)) {
                self->vbar_.onMouseDown(x, y);
                return 0;
            }
            const int row = self->hitRow(y);
            if (row < 0) {
                self->select({}, 0);
                return 0;
            }
            const TreeNode n = self->rows_[row];
            if (n.hasChildren) {
                const RECT d = self->disclosureRect(row);
                POINT p{x, y};
                if (PtInRect(&d, p) && self->model_) {
                    self->model_->toggleExpanded(n.id);
                    if (self->onExp_) {
                        self->onExp_(n.id, self->model_->isExpanded(n.id));
                    }
                    self->refresh();
                    return 0;
                }
            }
            if (self->hover_ == n.id) {
                RECT pr = self->plusRect(row);
                POINT p{x, y};
                if (PtInRect(&pr, p)) {
                    POINT pts[2] = {{pr.left, pr.top},
                                    {pr.right, pr.bottom}};
                    MapWindowPoints(hwnd, nullptr, pts, 2);
                    RECT anchor{pts[0].x, pts[0].y, pts[1].x, pts[1].y};
                    if (self->onAdd_) self->onAdd_(n.id, anchor);
                    return 0;
                }
            }
            const bool ctrl = (wParam & MK_CONTROL) != 0;
            const bool shift = (wParam & MK_SHIFT) != 0;
            if (shift && self->anchor_ != 0) {
                int a = self->rowOf(self->anchor_);
                int b = row;
                if (a > b) std::swap(a, b);
                std::vector<NodeId> set;
                for (int i = a; i <= b; ++i) set.push_back(self->rows_[i].id);
                self->select(set, n.id);
            } else if (ctrl) {
                std::vector<NodeId> set = self->sel_;
                auto it = std::find(set.begin(), set.end(), n.id);
                if (it != set.end()) {
                    set.erase(it);
                } else {
                    set.push_back(n.id);
                }
                self->select(set, n.id);
            } else {
                self->select({n.id}, n.id);
            }
            self->dragArmed_ = true;
            self->dragStart_ = {x, y};
            self->dragId_ = n.id;
            SetCapture(hwnd);
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            const int y = GET_Y_LPARAM(lParam);
            const int row = self->hitRow(y);
            if (row >= 0) self->beginInlineEdit(self->rows_[row].id);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (self->vbar_.dragging()) {
                self->vbar_.onMouseUp();
                return 0;
            }
            if (self->dragging_) {
                const NodeId src = self->dragId_;
                const NodeId target = self->dropId_;
                const DropZone zone = self->dropZone_;
                const bool valid = self->dropValid_;
                self->cancelDrag();
                if (valid && self->onReparent_ && self->model_) {
                    const DropDecision d =
                        self->model_->dropTarget(src, target, zone);
                    if (d.valid) self->onReparent_(src, d.newParent, d.index);
                }
                return 0;
            }
            if (self->dragArmed_) {
                self->dragArmed_ = false;
            }
            // Suelta el raton en cualquier caso (arrastre cancelado, nodo
            // protegido o simple clic) para no dejar el capture colgado.
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        case WM_KEYDOWN: {
            if (!self->model_) break;
            const NodeId p = self->primary_;
            const int row = p ? self->rowOf(p) : -1;
            switch (wParam) {
                case VK_DOWN: {
                    const int r = row < 0 ? 0 : row + 1;
                    if (r < static_cast<int>(self->rows_.size())) {
                        self->movePrimary(self->rows_[r].id);
                    }
                    return 0;
                }
                case VK_UP: {
                    const int r = row <= 0 ? 0 : row - 1;
                    if (r < static_cast<int>(self->rows_.size())) {
                        self->movePrimary(self->rows_[r].id);
                    }
                    return 0;
                }
                case VK_LEFT: {
                    if (row >= 0) {
                        const TreeNode n = self->rows_[row];
                        if (n.hasChildren && n.expanded) {
                            self->model_->setExpanded(n.id, false);
                            if (self->onExp_) self->onExp_(n.id, false);
                            self->refresh();
                        } else {
                            const NodeId par = self->model_->parentOf(n.id);
                            if (par != kInvalidId && par != kTreeRootId) {
                                self->movePrimary(par);
                            }
                        }
                    }
                    return 0;
                }
                case VK_RIGHT: {
                    if (row >= 0) {
                        const TreeNode n = self->rows_[row];
                        if (n.hasChildren && !n.expanded) {
                            self->model_->setExpanded(n.id, true);
                            if (self->onExp_) self->onExp_(n.id, true);
                            self->refresh();
                        } else if (n.hasChildren) {
                            const std::vector<NodeId> kids =
                                self->model_->childrenOf(n.id);
                            if (!kids.empty()) self->movePrimary(kids.front());
                        }
                    }
                    return 0;
                }
                case VK_F2:
                    if (p != 0) self->beginInlineEdit(p);
                    return 0;
                case VK_DELETE: {
                    std::vector<NodeId> victims;
                    for (NodeId id : self->sel_) {
                        if (!self->model_->isProtected(id)) {
                            victims.push_back(id);
                        }
                    }
                    if (!victims.empty() && self->onDel_) self->onDel_(victims);
                    return 0;
                }
                default:
                    break;
            }
            break;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

}  // namespace ui
}  // namespace sk
