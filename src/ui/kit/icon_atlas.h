#pragma once

#include <windows.h>

#include "icons.h"

namespace sk {
namespace ui {

void initIconAtlas();
void shutdownIconAtlas();

// Dibuja un icono del atlas en (x,y) con tamaño size (16/20/24/32);
// color es el COLORREF (se tiñe el blanco) o RGB(255,255,255) para sin tinte.
void drawIcon(HDC hdc, IconId id, int x, int y, int size, COLORREF color);

} // namespace ui
} // namespace sk
