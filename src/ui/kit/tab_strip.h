#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

#include "icons.h"

namespace sk {
namespace ui {

struct TabStripItem {
    std::string text; // UTF-8 interno
    bool closeable = false;
};

enum class TabHit { None, Tab, Close, Plus };

struct TabHitResult {
    TabHit kind = TabHit::None;
    int index = -1;
    RECT close{}; // rect del boton de cierre (hover)
};

class TabStrip {
public:
    TabStrip();

    void attach(HWND parent, int id, bool showPlus = false);
    void setItems(const std::vector<TabStripItem>& items);
    void addItem(const TabStripItem& item);
    void setSelected(int index);
    int selected() const;
    void setRect(const RECT& r);
    const RECT& rect() const { return rc_; }

    void setOnSelected(std::function<void(int)> cb) { onSelected_ = std::move(cb); }
    void setOnClose(std::function<void(int)> cb) { onClose_ = std::move(cb); }
    void setOnPlus(std::function<void()> cb) { onPlus_ = std::move(cb); }

    void paint(HDC hdc);
    TabHitResult hitTest(int x, int y);
    // Rectangulo de la pestana "index" (para colocar un editor en linea).
    RECT tabRect(int index) const;
    // La ventana anfitriona reenvia el raton.
    void onMouseMove(int x, int y);
    void onMouseLeave();
    bool onMouseDown(int x, int y);
    void onMouseWheel(int delta);

private:
    void recomputeLayout() const;

    HWND parent_ = nullptr;
    int id_ = 0;
    bool showPlus_ = false;
    std::vector<TabStripItem> items_;
    int selected_ = -1;
    RECT rc_ = {};
    mutable int offset_ = 0; // scroll horizontal (rueda) con muchas pestanas
    int hover_ = -1; // pestana bajo el raton
    bool hoverClose_ = false;
    bool hoverPlus_ = false;
    mutable std::vector<int> xs_; // x inicial de cada pestana (offset aplicado)
    mutable std::vector<int> ws_; // ancho de cada pestana
    mutable int plusX_ = 0;
    std::function<void(int)> onSelected_;
    std::function<void(int)> onClose_;
    std::function<void()> onPlus_;
};

} // namespace ui
} // namespace sk