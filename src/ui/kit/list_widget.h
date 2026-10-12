#pragma once

#include <functional>
#include <string>
#include <vector>

#include "paint_buffer.h"
#include "scroll_bar.h"

namespace sk {
namespace ui {

class ListWidget {
public:
    ListWidget();

    void create(HWND parent, int x, int y, int w, int h, int cols);
    void destroy();
    HWND hwnd() const { return hwnd_; }

    void setItems(const std::vector<std::vector<const char*>>& rows);
    void setColumnWidths(const std::vector<int>& w);
    void setOnClick(std::function<void(int)> cb) { onClick_ = std::move(cb); }

    void invalidate();

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    void paint(HDC);
    int hitRow(int y);

    HWND hwnd_ = nullptr;
    PaintBuffer pb_;
    ScrollBar vbar_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<int> colw_;
    int cols_ = 0;
    int rowh_ = 22;
    int scrollY_ = 0;
    std::function<void(int)> onClick_;
};

} // namespace ui
} // namespace sk
