#include "properties_panel.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include "theme.h"
#include "kit/kit.h"
#include "scene/property_value.h"
#include "scene/world.h"

namespace sk {
namespace ui {

namespace {

const wchar_t* kPropsClass = L"MOTORSK_PROPERTIES";

constexpr int kSearchId = 100;
constexpr int kCellId = 101;

const char* const kAttrTypes[] = {"text", "number", "bool", "vector3", "color"};
constexpr int kAttrTypeCount =
    static_cast<int>(sizeof(kAttrTypes) / sizeof(kAttrTypes[0]));

std::string trimCopy(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

bool enumContains(const std::string& options, const std::string& value) {
    size_t start = 0;
    while (start <= options.size()) {
        size_t comma = options.find(',', start);
        if (comma == std::string::npos) comma = options.size();
        if (trimCopy(options.substr(start, comma - start)) == value) return true;
        if (comma == options.size()) break;
        start = comma + 1;
    }
    return false;
}

std::string nextAttrType(const std::string& type) {
    for (int i = 0; i < kAttrTypeCount; ++i) {
        if (type == kAttrTypes[i]) return kAttrTypes[(i + 1) % kAttrTypeCount];
    }
    return kAttrTypes[0];
}

} // namespace

PropertiesPanel::PropertiesPanel(const World* world) : world_(world) {
    vbar_.setOnScroll([this](int p) {
        scrollY_ = p;
        invalidate();
    });
}

PropertiesPanel::~PropertiesPanel() { destroy(); }

void PropertiesPanel::create(HWND parent, int id) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(inst, kPropsClass, &wc)) {
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.hbrBackground = theme::listBackgroundBrush();
        wc.lpszClassName = kPropsClass;
        RegisterClassExW(&wc);
    }
    hwnd_ = CreateWindowExW(0, kPropsClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0,
                            0, 100, 100, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            inst, this);
    if (!hwnd_) return;

    vbar_.attach(hwnd_, 1, true);

    searchEdit_ = CreateWindowExW(
        0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchId)), inst,
        nullptr);
    SendMessageW(searchEdit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);

    cellEdit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0,
                                0, 10, 10, hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCellId)),
                                inst, nullptr);
    SendMessageW(cellEdit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    SetWindowSubclass(cellEdit_, editProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));

    tooltip_ = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr,
                               WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0,
                               0, hwnd_, nullptr, inst, nullptr);
    if (tooltip_) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        ti.hwnd = hwnd_;
        ti.uId = 1;
        ti.lpszText = tipText_;
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0,
                     reinterpret_cast<LPARAM>(&ti));
        SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, 320);
    }

    layoutItems();
    invalidate();
}

void PropertiesPanel::destroy() {
    if (tooltip_) {
        DestroyWindow(tooltip_);
        tooltip_ = nullptr;
    }
    if (hwnd_) {
        vbar_.attach(nullptr, 0, true);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        searchEdit_ = nullptr;
        cellEdit_ = nullptr;
    }
}

void PropertiesPanel::setRect(const RECT& r) {
    if (!hwnd_) return;
    MoveWindow(hwnd_, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    layoutItems();
    invalidate();
}

void PropertiesPanel::invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

const Instance* PropertiesPanel::primary() const {
    if (!world_ || selection_.empty()) return nullptr;
    return world_->find(selection_.front());
}

int PropertiesPanel::bodyRight() const {
    RECT rc{};
    if (hwnd_) GetClientRect(hwnd_, &rc);
    return rc.right - (vbar_.visible() ? kBarW : 0) - 2;
}

RECT PropertiesPanel::searchRect() const {
    RECT rc{};
    if (hwnd_) GetClientRect(hwnd_, &rc);
    RECT out{};
    out.left = 6;
    out.right = rc.right - kBarW - 6;
    out.top = 5;
    out.bottom = 5 + 20;
    return out;
}

bool PropertiesPanel::sectionCollapsed(const std::string& s) const {
    auto it = collapsed_.find(s);
    return it != collapsed_.end() && it->second;
}

bool PropertiesPanel::vecExpanded(const std::string& name) const {
    auto it = vecOpen_.find(name);
    return it != vecOpen_.end() && it->second;
}

void PropertiesPanel::setSelection(const std::vector<InstanceId>& ids) {
    // Al cambiar de seleccion se vuelve arriba; al refrescar la misma
    // seleccion (arrastre de gizmo) se conserva el desplazamiento.
    if (ids != selection_) scrollY_ = 0;
    selection_ = ids;
    rebuild();
}

void PropertiesPanel::refresh() { rebuild(); }

void PropertiesPanel::rebuild() {
    std::vector<const Instance*> sel;
    if (world_) {
        for (InstanceId id : selection_) {
            const Instance* i = world_->find(id);
            if (i) sel.push_back(i);
        }
    }
    rows_ = world_
                ? buildPropertyRows(world_->registry(), sel, search_)
                : std::vector<PropertyRow>{};
    buildItems();
    layoutItems();
    invalidate();
}

void PropertiesPanel::buildItems() {
    items_.clear();
    if (!world_ || selection_.empty()) return;

    Item name;
    name.kind = Item::Kind::Name;
    name.h = kRowH;
    items_.push_back(name);
    Item parent;
    parent.kind = Item::Kind::Parent;
    parent.h = kRowH;
    items_.push_back(parent);

    std::string curSection;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        const PropertyRow& r = rows_[static_cast<size_t>(i)];
        if (r.section != curSection) {
            curSection = r.section;
            Item h;
            h.kind = Item::Kind::Section;
            h.section = curSection;
            h.h = kHeaderH;
            items_.push_back(h);
        }
        if (sectionCollapsed(curSection)) continue;
        Item p;
        p.kind = Item::Kind::Property;
        p.rowIndex = i;
        p.h = kRowH;
        items_.push_back(p);
        if ((r.type == PropType::Vec3 || r.type == PropType::Color) &&
            vecExpanded(r.name)) {
            for (int a = 0; a < 3; ++a) {
                Item ax;
                ax.kind = Item::Kind::Axis;
                ax.rowIndex = i;
                ax.axis = a;
                ax.h = kRowH;
                items_.push_back(ax);
            }
        }
    }

    // Attributes (estilo Roblox): cabecera + filas + "anadir".
    Item ah;
    ah.kind = Item::Kind::AttrHeader;
    ah.h = kHeaderH;
    items_.push_back(ah);
    const Instance* p = primary();
    if (p) {
        for (const Attribute& a : p->getAttributes()) {
            Item it;
            it.kind = Item::Kind::Attribute;
            it.attrName = a.name;
            it.h = kRowH;
            items_.push_back(it);
        }
    }
    Item add;
    add.kind = Item::Kind::AddAttribute;
    add.h = kRowH;
    items_.push_back(add);

    // Tags.
    Item th;
    th.kind = Item::Kind::TagHeader;
    th.h = kHeaderH;
    items_.push_back(th);
    Item tin;
    tin.kind = Item::Kind::TagInput;
    tin.h = kRowH + 2;
    items_.push_back(tin);
    if (p) {
        for (const std::string& t : p->getTags()) {
            Item it;
            it.kind = Item::Kind::TagChip;
            it.tag = t;
            it.h = 24;
            items_.push_back(it);
        }
    }
}

void PropertiesPanel::layoutItems() {
    if (!hwnd_) return;
    int y = 0;
    for (Item& it : items_) {
        it.y = y;
        y += it.h;
    }
    contentH_ = y;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int view = static_cast<int>(rc.bottom) - kSearchH;
    if (view < 0) view = 0;
    const int maxPos = std::max<int>(0, contentH_ - view);
    if (scrollY_ > maxPos) scrollY_ = maxPos;
    if (scrollY_ < 0) scrollY_ = 0;
    vbar_.setRect(RECT{rc.right - kBarW, kSearchH, rc.right, rc.bottom});
    vbar_.setPage(view);
    vbar_.setRange(0, contentH_);
    vbar_.setPos(scrollY_);
    vbar_.update();

    // Recoloca el buscador.
    const RECT sr = searchRect();
    MoveWindow(searchEdit_, sr.left, sr.top, sr.right - sr.left,
               sr.bottom - sr.top, TRUE);
}

const PropertiesPanel::Item* PropertiesPanel::itemAt(int y) const {
    if (y < kSearchH) return nullptr;
    const int contentY = y - kSearchH + scrollY_;
    for (const Item& it : items_) {
        if (contentY >= it.y && contentY < it.y + it.h) return &it;
    }
    return nullptr;
}

void PropertiesPanel::itemRects(const Item& it, RECT& label, RECT& value,
                                RECT& restore) const {
    const int top = kSearchH + it.y - scrollY_;
    int left = 10;
    if (it.kind == Item::Kind::Axis) {
        left = 30;
    } else if (it.kind == Item::Kind::Property &&
               it.rowIndex < static_cast<int>(rows_.size())) {
        const PropType t = rows_[static_cast<size_t>(it.rowIndex)].type;
        if (t == PropType::Vec3 || t == PropType::Color) left = 26;
    }
    label = RECT{left, top, kLabelW, top + it.h};
    value = RECT{kLabelW, top, bodyRight() - 24, top + it.h};
    restore = RECT{bodyRight() - 22, top, bodyRight() - 4, top + it.h};
}

void PropertiesPanel::trackLeave() {
    if (hwnd_ && !trackingLeave_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        trackingLeave_ = true;
    }
}

// --- Edicion -------------------------------------------------------------

void PropertiesPanel::openFreeEditor(EditTarget target, const RECT& cell,
                                     const std::string& text) {
    if (!cellEdit_) return;
    edit_ = target;
    MoveWindow(cellEdit_, cell.left, cell.top + 1, cell.right - cell.left,
               cell.bottom - cell.top - 2, TRUE);
    SetWindowTextA(cellEdit_, text.c_str());
    ShowWindow(cellEdit_, SW_SHOW);
    SetFocus(cellEdit_);
    SendMessageW(cellEdit_, EM_SETSEL, 0, -1);
}

void PropertiesPanel::openEditor(const Item& it) {
    if (it.kind == Item::Kind::Name) {
        if (selection_.size() != 1) return;
        const Instance* p = primary();
        if (!p) return;
        RECT label{}, value{}, restore{};
        itemRects(it, label, value, restore);
        EditTarget t;
        t.kind = EditTarget::Kind::Name;
        openFreeEditor(t, value, p->name());
        return;
    }
    if (it.kind == Item::Kind::Axis) {
        const Instance* p = primary();
        if (!p || it.rowIndex < 0 ||
            it.rowIndex >= static_cast<int>(rows_.size()))
            return;
        const PropertyRow& row = rows_[static_cast<size_t>(it.rowIndex)];
        const int slot = p->slotOf(row.name);
        if (slot < 0) return;
        const Vec3 v = p->valueBySlot(slot).v;
        const float comp[3] = {v.x, v.y, v.z};
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6g", comp[it.axis]);
        RECT label{}, value{}, restore{};
        itemRects(it, label, value, restore);
        EditTarget t;
        t.kind = EditTarget::Kind::Axis;
        t.rowIndex = it.rowIndex;
        t.axis = it.axis;
        openFreeEditor(t, value, buf);
        return;
    }
    if (it.kind != Item::Kind::Property) return;
    if (it.rowIndex < 0 || it.rowIndex >= static_cast<int>(rows_.size())) return;
    const PropertyRow& row = rows_[static_cast<size_t>(it.rowIndex)];
    if (row.readOnly || row.type == PropType::Bool ||
        row.type == PropType::RefObject)
        return;
    RECT label{}, value{}, restore{};
    itemRects(it, label, value, restore);
    EditTarget t;
    t.kind = EditTarget::Kind::Property;
    t.rowIndex = it.rowIndex;
    openFreeEditor(t, value, row.mixed ? std::string() : row.value);
}

void PropertiesPanel::commitEditor(bool cancel) {
    if (edit_.kind == EditTarget::Kind::None) return;
    const EditTarget t = edit_;
    edit_.kind = EditTarget::Kind::None;
    char buf[512]{};
    if (cellEdit_) GetWindowTextA(cellEdit_, buf, sizeof(buf));
    if (cellEdit_) ShowWindow(cellEdit_, SW_HIDE);
    if (hwnd_) SetFocus(hwnd_);
    if (cancel) return;
    const std::string text = buf;

    switch (t.kind) {
        case EditTarget::Kind::Property:
            applyPropertyText(t.rowIndex, -1, text);
            break;
        case EditTarget::Kind::Axis:
            applyPropertyText(t.rowIndex, t.axis, text);
            break;
        case EditTarget::Kind::Name:
            if (onNameEdited_ && selection_.size() == 1 && !text.empty()) {
                onNameEdited_(selection_.front(), text);
            }
            break;
        case EditTarget::Kind::AttrName: {
            const Instance* p = primary();
            if (!p || text.empty()) break;
            for (const Attribute& a : p->getAttributes()) {
                if (a.name != t.key) continue;
                if (a.name != text && onAttributeRemoved_) {
                    onAttributeRemoved_(selection_.front(), a.name);
                }
                Attribute na = a;
                na.name = text;
                if (onAttributeSet_) onAttributeSet_(selection_.front(), na);
                break;
            }
            break;
        }
        case EditTarget::Kind::AttrValue: {
            const Instance* p = primary();
            if (!p) break;
            for (const Attribute& a : p->getAttributes()) {
                if (a.name != t.key) continue;
                Attribute na = a;
                na.value = text;
                if (onAttributeSet_) onAttributeSet_(selection_.front(), na);
                break;
            }
            break;
        }
        case EditTarget::Kind::Tag:
            if (onTagAdded_ && !text.empty()) {
                onTagAdded_(selection_.front(), text);
            }
            break;
        default:
            break;
    }
    // El World ya refleja el cambio (el dueno aplico el comando); se
    // relee para mostrar el valor canonico.
    refresh();
}

void PropertiesPanel::applyPropertyText(int rowIndex, int axis,
                                        const std::string& text) {
    if (rowIndex < 0 || rowIndex >= static_cast<int>(rows_.size())) return;
    const PropertyRow& row = rows_[static_cast<size_t>(rowIndex)];
    if (row.mixed && text.empty()) return;  // no cambiar un valor mixto a vacio

    PropertyValue pv;
    if (row.type == PropType::Vec3 || row.type == PropType::Color) {
        Vec3 v{};
        if (axis >= 0) {
            const Instance* p = primary();
            if (p) {
                const int slot = p->slotOf(row.name);
                if (slot >= 0) v = p->valueBySlot(slot).v;
            }
            const float f = static_cast<float>(std::atof(text.c_str()));
            if (axis == 0) v.x = f;
            else if (axis == 1) v.y = f;
            else v.z = f;
        } else if (!parseVec3Text(text, v)) {
            return;
        }
        pv.v = v;
    } else if (row.type == PropType::Enum) {
        if (!row.enumOptions.empty() && !enumContains(row.enumOptions, text)) {
            return;
        }
        pv.s = text;
    } else if (!parsePropertyValue(text, row.type, pv)) {
        return;
    }

    if (!onPropertyEdited_) return;
    for (InstanceId id : selection_) onPropertyEdited_(id, row.name, pv);
}

void PropertiesPanel::toggleBool(const Item& it) {
    if (it.rowIndex < 0 || it.rowIndex >= static_cast<int>(rows_.size())) return;
    const PropertyRow& row = rows_[static_cast<size_t>(it.rowIndex)];
    PropertyValue pv;
    const bool cur = !row.mixed && row.value == "true";
    pv.b = !cur;
    if (!onPropertyEdited_) return;
    for (InstanceId id : selection_) onPropertyEdited_(id, row.name, pv);
}

void PropertiesPanel::restoreDefault(const Item& it) {
    if (it.rowIndex < 0 || it.rowIndex >= static_cast<int>(rows_.size())) return;
    const PropertyRow& row = rows_[static_cast<size_t>(it.rowIndex)];
    PropertyValue pv;
    if (!parsePropertyValue(row.def, row.type, pv)) return;
    if (!onPropertyEdited_) return;
    for (InstanceId id : selection_) onPropertyEdited_(id, row.name, pv);
}

// --- Interaccion ---------------------------------------------------------

void PropertiesPanel::onLButtonDown(int x, int y) {
    SetFocus(hwnd_);
    commitEditor(false);
    const Item* it = itemAt(y);
    if (!it) return;

    switch (it->kind) {
        case Item::Kind::Section: {
            collapsed_[it->section] = !sectionCollapsed(it->section);
            buildItems();
            layoutItems();
            invalidate();
            break;
        }
        case Item::Kind::Property: {
            if (it->rowIndex < 0 ||
                it->rowIndex >= static_cast<int>(rows_.size()))
                break;
            const PropertyRow& row = rows_[static_cast<size_t>(it->rowIndex)];
            RECT label{}, value{}, restore{};
            itemRects(*it, label, value, restore);
            if (!row.isDefault && x >= restore.left && x < restore.right) {
                restoreDefault(*it);
                break;
            }
            if (row.type == PropType::Bool) {
                toggleBool(*it);
                break;
            }
            if ((row.type == PropType::Vec3 || row.type == PropType::Color) &&
                x < 24) {
                vecOpen_[row.name] = !vecExpanded(row.name);
                buildItems();
                layoutItems();
                invalidate();
                break;
            }
            openEditor(*it);
            break;
        }
        case Item::Kind::Axis:
        case Item::Kind::Name:
            openEditor(*it);
            break;
        case Item::Kind::AttrHeader:
            break;
        case Item::Kind::Attribute: {
            const Instance* p = primary();
            if (!p) break;
            const Attribute* found = nullptr;
            for (const Attribute& a : p->getAttributes()) {
                if (a.name == it->attrName) {
                    found = &a;
                    break;
                }
            }
            if (!found) break;
            const int right = bodyRight();
            if (x > right - 18) {  // borrar
                if (onAttributeRemoved_) {
                    onAttributeRemoved_(selection_.front(), found->name);
                }
            } else if (x > kLabelW + 60) {  // valor
                RECT cell{kLabelW + 66, kSearchH + it->y - scrollY_,
                          right - 22, kSearchH + it->y - scrollY_ + it->h};
                EditTarget t;
                t.kind = EditTarget::Kind::AttrValue;
                t.key = found->name;
                openFreeEditor(t, cell, found->value);
            } else if (x > kLabelW + 4) {  // tipo
                Attribute na = *found;
                na.type = nextAttrType(na.type);
                if (onAttributeSet_) onAttributeSet_(selection_.front(), na);
            } else {  // nombre
                RECT cell{10, kSearchH + it->y - scrollY_, kLabelW - 4,
                          kSearchH + it->y - scrollY_ + it->h};
                EditTarget t;
                t.kind = EditTarget::Kind::AttrName;
                t.key = found->name;
                openFreeEditor(t, cell, found->name);
            }
            break;
        }
        case Item::Kind::AddAttribute: {
            const Instance* p = primary();
            if (!p || !onAttributeSet_) break;
            std::string base = "Atributo";
            std::string name = base;
            int n = 1;
            auto taken = [&](const std::string& nm) {
                for (const Attribute& a : p->getAttributes()) {
                    if (a.name == nm) return true;
                }
                return false;
            };
            while (taken(name)) name = base + std::to_string(n++);
            Attribute a;
            a.name = name;
            a.type = "text";
            a.value = "";
            onAttributeSet_(selection_.front(), a);
            break;
        }
        case Item::Kind::TagInput: {
            RECT cell{10, kSearchH + it->y - scrollY_, bodyRight() - 10,
                      kSearchH + it->y - scrollY_ + kRowH - 2};
            EditTarget t;
            t.kind = EditTarget::Kind::Tag;
            openFreeEditor(t, cell, std::string());
            break;
        }
        case Item::Kind::TagChip: {
            const int right = bodyRight();
            if (x > right - 22) {
                if (onTagRemoved_) {
                    onTagRemoved_(selection_.front(), it->tag);
                }
            }
            break;
        }
        default:
            break;
    }
    // Relee del World: los comandos se aplican de forma sincrona en los
    // callbacks, asi que las filas ya reflejan los valores nuevos.
    refresh();
}

void PropertiesPanel::updateTooltip(int x, int y) {
    if (!tooltip_) return;
    const Item* it = itemAt(y);
    const PropertyRow* row = nullptr;
    if (it && it->kind == Item::Kind::Property && it->rowIndex >= 0 &&
        it->rowIndex < static_cast<int>(rows_.size())) {
        row = &rows_[static_cast<size_t>(it->rowIndex)];
    }
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.hwnd = hwnd_;
    ti.uId = 1;
    if (row && !row->desc.empty()) {
        std::wstring w = widen(row->desc);
        wcsncpy_s(tipText_, w.c_str(), _TRUNCATE);
        ti.lpszText = tipText_;
        SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0,
                     reinterpret_cast<LPARAM>(&ti));
        POINT p{x, y};
        ClientToScreen(hwnd_, &p);
        p.x += 16;
        p.y += 20;
        SendMessageW(tooltip_, TTM_TRACKPOSITION, 0,
                     MAKELPARAM(p.x, p.y));
        SendMessageW(tooltip_, TTM_TRACKACTIVATE, TRUE,
                     reinterpret_cast<LPARAM>(&ti));
    } else {
        SendMessageW(tooltip_, TTM_TRACKACTIVATE, FALSE,
                     reinterpret_cast<LPARAM>(&ti));
    }
}

LRESULT CALLBACK PropertiesPanel::editProc(HWND h, UINT msg, WPARAM wParam,
                                           LPARAM lParam, UINT_PTR,
                                           DWORD_PTR ref) {
    auto* self = reinterpret_cast<PropertiesPanel*>(ref);
    switch (msg) {
        case WM_KEYDOWN:
            if (wParam == VK_RETURN) {
                self->commitEditor(false);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                self->commitEditor(true);
                return 0;
            }
            break;
        case WM_KILLFOCUS:
            if (self->edit_.kind != EditTarget::Kind::None) {
                self->commitEditor(false);
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(h, editProc, 1);
            break;
        default:
            break;
    }
    return DefSubclassProc(h, msg, wParam, lParam);
}

LRESULT CALLBACK PropertiesPanel::wndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                          LPARAM lParam) {
    PropertiesPanel* self = reinterpret_cast<PropertiesPanel*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<PropertiesPanel*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_SIZE:
            self->layoutItems();
            self->invalidate();
            break;
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
        case WM_COMMAND:
            if (LOWORD(wParam) == kSearchId &&
                HIWORD(wParam) == EN_CHANGE) {
                char buf[256]{};
                GetWindowTextA(self->searchEdit_, buf, sizeof(buf));
                self->search_ = buf;
                self->refresh();
            }
            break;
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, theme::text());
            SetBkColor(dc, theme::boxBackground());
            return reinterpret_cast<LRESULT>(theme::boxBackgroundBrush());
        }
        case WM_MOUSEWHEEL:
            self->vbar_.onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            break;
        case WM_MOUSEMOVE:
            self->trackLeave();
            self->vbar_.onMouseMove(GET_X_LPARAM(lParam),
                                    GET_Y_LPARAM(lParam));
            self->updateTooltip(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        case WM_MOUSELEAVE:
            self->trackingLeave_ = false;
            if (self->tooltip_) {
                TOOLINFOW ti{};
                ti.cbSize = sizeof(ti);
                ti.hwnd = hwnd;
                ti.uId = 1;
                SendMessageW(self->tooltip_, TTM_TRACKACTIVATE, FALSE,
                             reinterpret_cast<LPARAM>(&ti));
            }
            break;
        case WM_RBUTTONDOWN:
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (self->vbar_.hitTest(x, y)) {
                if (msg == WM_LBUTTONDOWN) self->vbar_.onMouseDown(x, y);
            } else if (msg == WM_LBUTTONDOWN) {
                self->onLButtonDown(x, y);
            }
            break;
        }
        case WM_LBUTTONUP:
            self->vbar_.onMouseUp();
            break;
        case WM_CAPTURECHANGED:
            self->vbar_.onCaptureChanged();
            break;
        case WM_KEYDOWN:
            self->vbar_.onKey(static_cast<int>(wParam));
            break;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

} // namespace ui
} // namespace sk
