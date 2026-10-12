#include "editor/layout.h"

namespace sk {
namespace editor {

Layout Layout::compute(int width, int height) {
    Layout l;
    l.width = width;
    l.height = height;

    const int menuH = WorkspaceTabs::kMenuBarHeight;
    const int shortcutH = WorkspaceTabs::kShortcutHeight;
    const int navbarH = WorkspaceTabs::kNavbarHeight;

    // Barra superior: se recorta si la ventana es mas baja que la banda.
    const int band = height < kTopBandHeight ? height : kTopBandHeight;
    l.topBar = {0, 0, width, band};

    // Sub-bandas de la barra. Si la ventana es muy baja se recortan a 0
    // para no asomar por debajo de topBar (rectangulos vacios al ras).
    auto subBand = [width, band](int y, int h) -> Rect {
        if (y > band) y = band;
        if (y + h > band) h = band - y;
        if (h < 0) h = 0;
        return {0, y, width, h};
    };
    l.dimensionStrip = subBand(menuH, shortcutH);
    l.tabRow = subBand(menuH + shortcutH, navbarH);

    // Paneles laterales y viewport debajo de la barra.
    const int panelY = kTopBandHeight;
    int panelH = height - panelY;
    if (panelH < 0) panelH = 0;

    const int propsW = width < kPropertiesWidth ? width : kPropertiesWidth;
    const int remaining = width - propsW;
    const int explorerW = remaining < kExplorerWidth ? remaining : kExplorerWidth;
    const int viewportW = width - propsW - explorerW;

    l.leftPanel = {0, panelY, propsW, panelH};
    l.rightPanel = {width - explorerW, panelY, explorerW, panelH};
    l.viewport = {propsW, panelY, viewportW, panelH};
    return l;
}

bool Layout::inViewport(int px, int py) const {
    return px >= viewport.x && px <= rightPanel.x && py >= topBar.bottom() &&
           py <= height;
}

} // namespace editor
} // namespace sk
