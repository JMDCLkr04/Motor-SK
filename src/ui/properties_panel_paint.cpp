#include "properties_panel.h"

#include <windowsx.h>

#include <cstdio>
#include <string>

#include "theme.h"
#include "kit/kit.h"
#include "scene/world.h"

namespace sk {
namespace ui {

namespace {

const wchar_t* attrTypeLabel(const std::string& type) {
    if (type == "number") return L"número";
    if (type == "bool") return L"booleano";
    if (type == "vector3") return L"Vector3";
    if (type == "color") return L"color";
    return L"texto";
}

COLORREF labelColor() { return RGB(150, 150, 160); }
COLORREF valueColor() { return RGB(232, 232, 238); }
COLORREF mixedColor() { return RGB(214, 168, 62); }
COLORREF sectionColor() { return RGB(200, 200, 210); }
COLORREF iconColor() { return RGB(150, 150, 162); }

void drawTextLine(HDC hdc, const RECT& rc, const std::wstring& text,
                  COLORREF color, UINT flags = 0) {
    SetTextColor(hdc, color);
    RECT r = rc;
    DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &r,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX | flags);
}

void drawTriangle(HDC hdc, int left, int midY, bool open) {
    POINT pts[3];
    if (open) {
        pts[0] = POINT{left, midY - 2};
        pts[1] = POINT{left + 8, midY - 2};
        pts[2] = POINT{left + 4, midY + 3};
    } else {
        pts[0] = POINT{left + 1, midY - 4};
        pts[1] = POINT{left + 1, midY + 4};
        pts[2] = POINT{left + 6, midY};
    }
    HBRUSH b = CreateSolidBrush(sectionColor());
    HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, b));
    HPEN op = static_cast<HPEN>(SelectObject(hdc, GetStockObject(NULL_PEN)));
    Polygon(hdc, pts, 3);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(b);
}

// state: 0 = no, 1 = si, 2 = mixto.
void drawCheckbox(HDC hdc, const RECT& rc, int state) {
    HBRUSH b = CreateSolidBrush(theme::boxBackground());
    HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
    HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, b));
    HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 5, 5);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(pen);
    DeleteObject(b);

    HPEN mark = CreatePen(PS_SOLID, 2, state == 2 ? mixedColor() : theme::accent());
    HPEN om = static_cast<HPEN>(SelectObject(hdc, mark));
    if (state == 1) {
        MoveToEx(hdc, rc.left + 3, rc.top + 7, nullptr);
        LineTo(hdc, rc.left + 6, rc.top + 10);
        LineTo(hdc, rc.right - 3, rc.top + 3);
    } else if (state == 2) {
        MoveToEx(hdc, rc.left + 3, (rc.top + rc.bottom) / 2, nullptr);
        LineTo(hdc, rc.right - 3, (rc.top + rc.bottom) / 2);
    }
    SelectObject(hdc, om);
    DeleteObject(mark);
}

void drawResetIcon(HDC hdc, const RECT& rc) {
    const COLORREF c = iconColor();
    HPEN pen = CreatePen(PS_SOLID, 2, c);
    HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
    HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Ellipse(hdc, rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(pen);
    // Punta de flecha.
    POINT pts[3] = {POINT{rc.right - 5, rc.top - 1},
                    POINT{rc.right - 1, rc.top + 5},
                    POINT{rc.right - 8, rc.top + 5}};
    HBRUSH b = CreateSolidBrush(c);
    HBRUSH ob2 = static_cast<HBRUSH>(SelectObject(hdc, b));
    HPEN op2 = static_cast<HPEN>(SelectObject(hdc, GetStockObject(NULL_PEN)));
    Polygon(hdc, pts, 3);
    SelectObject(hdc, ob2);
    SelectObject(hdc, op2);
    DeleteObject(b);
}

} // namespace

void PropertiesPanel::paint(HDC hdc) {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    FillRect(hdc, &rc, theme::listBackgroundBrush());

    const int body = bodyRight();
    const int save = SaveDC(hdc);
    IntersectClipRect(hdc, 0, kSearchH, rc.right, rc.bottom);

    HFONT of = static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
    SetBkMode(hdc, TRANSPARENT);

    if (selection_.empty()) {
        SetTextColor(hdc, theme::textDisabled());
        RECT er{0, kSearchH + 6, rc.right, kSearchH + 30};
        DrawTextW(hdc, L"Sin selección.", -1, &er,
                  DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, of);
        RestoreDC(hdc, save);
        vbar_.paint(hdc);
        return;
    }

    const Instance* p = primary();

    for (const Item& it : items_) {
        const int top = kSearchH + it.y - scrollY_;
        if (top + it.h <= kSearchH) continue;
        if (top >= rc.bottom) break;
        const int midY = top + it.h / 2;

        if (it.kind == Item::Kind::Section) {
            SelectObject(hdc, theme::uiHeaderFont());
            drawTriangle(hdc, 8, midY, !sectionCollapsed(it.section));
            drawTextLine(hdc, RECT{24, top, body, top + it.h},
                         widen(it.section), sectionColor());
            SelectObject(hdc, theme::uiFont());
        } else if (it.kind == Item::Kind::Name) {
            drawTextLine(hdc, RECT{10, top, kLabelW, top + it.h}, L"Nombre",
                         labelColor());
            std::wstring v;
            COLORREF c = valueColor();
            if (selection_.size() == 1 && p) {
                v = widen(p->name());
            } else {
                v = std::to_wstring(selection_.size()) + L" objetos";
                c = mixedColor();
            }
            drawTextLine(hdc, RECT{kLabelW, top, body - 22, top + it.h}, v, c);
        } else if (it.kind == Item::Kind::Parent) {
            drawTextLine(hdc, RECT{10, top, kLabelW, top + it.h}, L"Padre",
                         labelColor());
            std::wstring v = L"—";
            if (p && p->parentId() != kInvalidId && world_) {
                const Instance* par = world_->find(p->parentId());
                if (par) v = widen(par->name());
            }
            drawTextLine(hdc, RECT{kLabelW, top, body - 22, top + it.h}, v,
                         valueColor());
        } else if (it.kind == Item::Kind::Property && it.rowIndex >= 0 &&
                   it.rowIndex < static_cast<int>(rows_.size())) {
            const PropertyRow& row = rows_[static_cast<size_t>(it.rowIndex)];
            const bool vec =
                row.type == PropType::Vec3 || row.type == PropType::Color;
            if (vec) drawTriangle(hdc, 10, midY, vecExpanded(row.name));
            drawTextLine(hdc, RECT{vec ? 26 : 10, top, kLabelW, top + it.h},
                         widen(row.name), labelColor());

            if (row.type == PropType::Bool) {
                const int st = row.mixed ? 2 : (row.value == "true" ? 1 : 0);
                drawCheckbox(hdc, RECT{kLabelW, top + 3, kLabelW + 14,
                                       top + 17},
                             st);
            } else {
                const std::wstring v =
                    row.mixed ? L"mixto" : widen(row.value);
                drawTextLine(hdc, RECT{kLabelW, top, body - 24, top + it.h}, v,
                             row.mixed ? mixedColor() : valueColor());
            }
            if (!row.isDefault) {
                drawResetIcon(hdc,
                              RECT{body - 20, top + 3, body - 4, top + 17});
            }
        } else if (it.kind == Item::Kind::Axis && it.rowIndex >= 0 &&
                   it.rowIndex < static_cast<int>(rows_.size())) {
            const PropertyRow& row = rows_[static_cast<size_t>(it.rowIndex)];
            const wchar_t* axisName = it.axis == 0 ? L"X"
                                      : it.axis == 1 ? L"Y"
                                                     : L"Z";
            drawTextLine(hdc, RECT{34, top, kLabelW, top + it.h}, axisName,
                         labelColor());
            std::wstring v;
            if (p) {
                const int slot = p->slotOf(row.name);
                if (slot >= 0) {
                    const Vec3 vec = p->valueBySlot(slot).v;
                    const float comp[3] = {vec.x, vec.y, vec.z};
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.6g", comp[it.axis]);
                    v = widen(buf);
                }
            }
            drawTextLine(hdc, RECT{kLabelW, top, body - 24, top + it.h}, v,
                         valueColor());
            if (!row.isDefault) {
                drawResetIcon(hdc,
                              RECT{body - 20, top + 3, body - 4, top + 17});
            }
        } else if (it.kind == Item::Kind::AttrHeader) {
            SelectObject(hdc, theme::uiHeaderFont());
            drawTextLine(hdc, RECT{10, top, body, top + it.h}, L"Attributes",
                         sectionColor());
            SelectObject(hdc, theme::uiFont());
        } else if (it.kind == Item::Kind::Attribute && p) {
            const Attribute* a = nullptr;
            for (const Attribute& at : p->getAttributes()) {
                if (at.name == it.attrName) {
                    a = &at;
                    break;
                }
            }
            if (!a) continue;
            drawTextLine(hdc, RECT{10, top, kLabelW - 4, top + it.h},
                         widen(a->name), valueColor());
            // Pastilla de tipo.
            RECT pill{kLabelW, top + 3, kLabelW + 56, top + it.h - 3};
            HBRUSH pb = CreateSolidBrush(theme::boxBackground());
            HPEN pp = CreatePen(PS_SOLID, 1, theme::border());
            HBRUSH opb = static_cast<HBRUSH>(SelectObject(hdc, pb));
            HPEN opp = static_cast<HPEN>(SelectObject(hdc, pp));
            RoundRect(hdc, pill.left, pill.top, pill.right, pill.bottom, 8, 8);
            SelectObject(hdc, opb);
            SelectObject(hdc, opp);
            DeleteObject(pp);
            DeleteObject(pb);
            drawTextLine(hdc, pill, attrTypeLabel(a->type), labelColor(),
                         DT_CENTER);
            drawTextLine(hdc, RECT{kLabelW + 62, top, body - 22, top + it.h},
                         widen(a->value), valueColor());
            drawTextLine(hdc, RECT{body - 18, top, body - 2, top + it.h}, L"✕",
                         iconColor(), DT_CENTER);
        } else if (it.kind == Item::Kind::AddAttribute) {
            drawTextLine(hdc, RECT{24, top, body, top + it.h},
                         L"+ Añadir atributo", theme::accent());
        } else if (it.kind == Item::Kind::TagHeader) {
            SelectObject(hdc, theme::uiHeaderFont());
            drawTextLine(hdc, RECT{10, top, body, top + it.h}, L"Tags",
                         sectionColor());
            SelectObject(hdc, theme::uiFont());
        } else if (it.kind == Item::Kind::TagInput) {
            RECT box{10, top + 2, body - 10, top + it.h - 3};
            HBRUSH b = CreateSolidBrush(theme::boxBackground());
            HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
            HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, b));
            HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
            RoundRect(hdc, box.left, box.top, box.right, box.bottom, 8, 8);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(pen);
            DeleteObject(b);
            drawTextLine(hdc, RECT{box.left + 8, box.top, box.right, box.bottom},
                         L"Añadir etiqueta…", theme::textDisabled());
        } else if (it.kind == Item::Kind::TagChip) {
            const std::wstring w = widen(it.tag);
            SIZE sz{};
            GetTextExtentPoint32W(hdc, w.c_str(), static_cast<int>(w.size()),
                                  &sz);
            RECT chip{10, top + 2, 10 + sz.cx + 34, top + it.h - 2};
            if (chip.right > body - 6) chip.right = body - 6;
            HBRUSH b = CreateSolidBrush(theme::boxBackgroundAlt());
            HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
            HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, b));
            HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
            RoundRect(hdc, chip.left, chip.top, chip.right, chip.bottom, 9, 9);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(pen);
            DeleteObject(b);
            drawTextLine(hdc, RECT{chip.left + 8, chip.top, chip.right - 18,
                                   chip.bottom},
                         w, valueColor());
            drawTextLine(hdc, RECT{chip.right - 18, chip.top, chip.right - 4,
                                   chip.bottom},
                         L"✕", iconColor(), DT_CENTER);
        }
    }

    SelectObject(hdc, of);
    RestoreDC(hdc, save);
    vbar_.paint(hdc);
}

} // namespace ui
} // namespace sk
