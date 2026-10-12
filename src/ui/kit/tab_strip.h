#pragma once

#include <functional>
#include <vector>
#include <windows.h>

#include "icons.h"

namespace sk {
namespace ui {

struct TabStripItem {
    const char* text;
    bool closeable = false;
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
    int hitTest(int x, int y);

private:
    HWND parent_ = nullptr;
    int id_ = 0;
    bool showPlus_ = false;
    std::vector<TabStripItem> items_;
    int selected_ = -1;
    RECT rc_ = {};
    std::function<void(int)> onSelected_;
    std::function<void(int)> onClose_;
    std::function<void()> onPlus_;
};

} // namespace ui
} // namespace sk
