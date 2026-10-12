#pragma once

#include <windows.h>

namespace sk {
namespace ui {

// Muestra un popup WS_POPUP cerca de anchorRect, ajustado al area
// del monitor de la ventana para que nunca quede fuera de pantalla.
// Devuelve el HWND del popup (creado pero no mostrado) o nullptr; el
// llamante es responsable de ShowWindow y de cerrar el popup.
HWND placePopup(HWND parent, const RECT& anchorRect, int width, int height,
                const WCHAR* wndClass, const WCHAR* title, WNDPROC proc,
                void* userData = nullptr);

} // namespace ui
} // namespace sk
