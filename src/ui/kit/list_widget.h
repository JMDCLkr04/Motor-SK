#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

#include "paint_buffer.h"
#include "scroll_bar.h"

namespace sk {
namespace ui {

// Lista estilo ListView con fila de cabecera (fondo gris oscuro, esquinas
// redondeadas y separadores), varias columnas con ancho configurable, todas
// las celdas pintadas, hover de fila, seleccion, callbacks de seleccion y
// doble clic y scrollbar del kit. Todo en fondo oscuro; el texto interno es
// UTF-8 y se dibuja con las APIs W (nada de blanco por defecto).
class ListWidget {
public:
    ListWidget();

    void create(HWND parent, int x, int y, int w, int h);
    void destroy();
    HWND hwnd() const { return hwnd_; }

    void setColumns(const std::vector<std::string>& names,
                    const std::vector<int>& widths);
    void setColumnWidths(const std::vector<int>& w);
    void setItems(const std::vector<std::vector<std::string>>& rows);
    void setRowHeight(int h);

    int rowCount() const { return (int)rows_.size(); }
    int selectedRow() const { return selected_; }
    void setSelectedRow(int row);
    void ensureVisible(int row);

    void setOnClick(std::function<void(int)> cb) { onClick_ = std::move(cb); }
    void setOnDoubleClick(std::function<void(int)> cb) {
        onDoubleClick_ = std::move(cb);
    }

    void invalidate();
    void invalidateRow(int row);

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    void paint(HDC);
    void layoutVBar();
    int hitRow(int y) const;
    void trackLeave();

    HWND hwnd_ = nullptr;
    PaintBuffer pb_;
    ScrollBar vbar_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<std::string> header_;
    std::vector<int> colw_;
    int rowh_ = 22;
    int headerH_ = 24;
    int scrollY_ = 0;
    int hoverRow_ = -1;
    int selected_ = -1;
    bool trackingLeave_ = false;
    std::function<void(int)> onClick_;
    std::function<void(int)> onDoubleClick_;
};

} // namespace ui
} // namespace sk