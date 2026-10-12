#pragma once

#include <windows.h>

namespace sk {
namespace ui {

// Triangulo de cascada: abierto si expanded es true. Rect es la zona
// donde dibujarlo (tamaño ~9x8, 25% mas pequeño que el actual).
void drawDisclosure(HDC hdc, const RECT& rc, bool expanded);

// Hit test para esa zona.
inline bool hitDisclosure(const RECT& rc, int x, int y) {
    POINT p{x, y};
    return PtInRect(&rc, p);
}

} // namespace ui
} // namespace sk
