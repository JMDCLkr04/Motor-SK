#pragma once

#include "ui/workspace_tabs.h"

namespace sk {
namespace editor {

// Rectangulo en coordenadas de cliente de la ventana principal.
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    int right() const { return x + w; }
    int bottom() const { return y + h; }
};

// Disposicion de la division PLACE, calculada en un unico sitio. Todos los
// consumidores (picking, marquee, render, paneles) leen de aqui en vez de
// repetir anchos/altos: un cambio de interfaz se hace en un solo lugar.
//
//  - topBar:         barra superior completa (menus + atajos + pestañas).
//  - dimensionStrip: fila de atajos/herramientas dentro de la barra.
//  - tabRow:         fila de pestañas (navbar PLACE/CODE/GUI), bajo la
//                    tira de dimensiones.
//  - leftPanel:      panel izquierdo (Properties).
//  - rightPanel:     panel derecho (Explorer).
//  - viewport:       zona 3D, entre ambos paneles y bajo la barra.
//
// Logica pura (sin Win32): se prueba en motorsk_model.
struct Layout {
    static constexpr int kPropertiesWidth = 300;
    static constexpr int kExplorerWidth = 260;
    // Alto de la banda superior: fila de menus + atajos + navbar. Se deriva
    // de las filas de WorkspaceTabs para no duplicar la cifra.
    static constexpr int kTopBandHeight = WorkspaceTabs::kMenuBarHeight +
                                          WorkspaceTabs::kShortcutHeight +
                                          WorkspaceTabs::kNavbarHeight;

    int width = 0;
    int height = 0;
    Rect topBar;
    Rect dimensionStrip;
    Rect tabRow;
    Rect leftPanel;
    Rect rightPanel;
    Rect viewport;

    // Calcula los rectangulos para un tamaño de ventana dado. Con ventanas
    // mas estrechas que la suma de paneles, el Explorer y el viewport se
    // recortan (ancho 0) para que la suma de anchos siga siendo el ancho
    // de la ventana.
    static Layout compute(int width, int height);

    // Accesores de las constantes: el resto de archivos no repite sus
    // nombres (la fuente unica es este struct).
    static int propertiesWidth() { return kPropertiesWidth; }
    static int explorerWidth() { return kExplorerWidth; }
    static int topBandHeight() { return kTopBandHeight; }

    // true si (px, py) cae dentro de la zona 3D. Usa los mismos limites
    // inclusivos que el picking historico para no cambiar el comportamiento.
    bool inViewport(int px, int py) const;
};

} // namespace editor
} // namespace sk
