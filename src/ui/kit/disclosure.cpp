#include "disclosure.h"

#include "../theme.h"

namespace sk {
namespace ui {

void drawDisclosure(HDC hdc, const RECT& rc, bool expanded) {
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    const int cx = (rc.left + rc.right) / 2;
    const int cy = (rc.top + rc.bottom) / 2;
    int hw = (rc.right - rc.left) / 3;
    int hh = (rc.bottom - rc.top) / 3;
    if (hw < 4) hw = 4;
    if (hh < 3) hh = 3;
    if (hw > 5) hw = 5;
    if (hh > 4) hh = 4;
    POINT poly[3];
    if (expanded) {
        poly[0] = {cx - hw, cy - hh / 2};
        poly[1] = {cx + hw, cy - hh / 2};
        poly[2] = {cx, cy + hh / 2 + 1};
    } else {
        poly[0] = {cx - hw / 2, cy - hh};
        poly[1] = {cx - hw / 2, cy + hh};
        poly[2] = {cx + hw / 2 + 1, cy};
    }
    HBRUSH brush = CreateSolidBrush(theme::text());
    HPEN pen = CreatePen(PS_SOLID, 1, theme::text());
    HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
    Polygon(hdc, poly, 3);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(pen);
    DeleteObject(brush);
}

} // namespace ui
} // namespace sk
