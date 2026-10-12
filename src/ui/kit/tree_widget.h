// Arbol dibujado a mano (filas de 22 px virtualizadas) alimentado por un
// TreeModel: sangria real por nivel, triangulo de cascada, icono por clase,
// boton "+" solo en la fila bajo el raton, seleccion simple/Ctrl/Shift,
// teclado (flechas, plegar/desplegar, F2, Supr), renombrado en linea (EDIT
// superpuesto) y arrastrar y soltar con fila fantasma y tres zonas
// (antes/dentro/despues). Pinta solo las filas visibles.
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

#include "kit.h"
#include "paint_buffer.h"
#include "scroll_bar.h"
#include "tree_model.h"

namespace sk {
namespace ui {

class TreeWidget {
public:
    TreeWidget();

    void create(HWND parent, int x, int y, int w, int h);
    void destroy();
    HWND hwnd() const { return hwnd_; }

    void setModel(TreeModel* model) {
        model_ = model;
        refresh();
    }

    // Reconstruye la lista plana desde el modelo y ajusta la barra.
    void refresh();
    void invalidate();
    void invalidateRow(NodeId id);

    void setOnSelectionChanged(
        std::function<void(const std::vector<NodeId>&)> cb) {
        onSel_ = std::move(cb);
    }
    void setOnExpandChanged(std::function<void(NodeId, bool)> cb) {
        onExp_ = std::move(cb);
    }
    void setOnAddRequested(std::function<void(NodeId, const RECT&)> cb) {
        onAdd_ = std::move(cb);
    }
    void setOnRenameRequested(
        std::function<void(NodeId, const std::string&)> cb) {
        onRename_ = std::move(cb);
    }
    void setOnDeleteRequested(
        std::function<void(const std::vector<NodeId>&)> cb) {
        onDel_ = std::move(cb);
    }
    void setOnReparentRequested(std::function<void(NodeId, NodeId, int)> cb) {
        onReparent_ = std::move(cb);
    }

    void select(const std::vector<NodeId>& ids, NodeId primary);
    std::vector<NodeId> selection() const { return sel_; }
    NodeId primary() const { return primary_; }

    void expandTo(NodeId id);
    void scrollTo(NodeId id);
    void beginRename(NodeId id);

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK editProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR,
                                     DWORD_PTR);

    void buildList();
    void updateScroll();
    int hitRow(int y) const;  // indice de fila visible, -1 si fuera
    RECT rowRect(int row) const;
    RECT plusRect(int row) const;
    RECT disclosureRect(int row) const;
    NodeId rowIdAt(int row) const;
    int rowOf(NodeId id) const;
    void paint(HDC);
    void paintRow(HDC hdc, int row, const RECT& clip);
    void paintGhost(HDC hdc);
    void setHover(NodeId id);
    void clearHover();
    void notifySelection();
    void beginInlineEdit(NodeId id);
    void commitRename(bool accept);
    void cancelDrag();
    void updateDropTarget(int x, int y);
    void movePrimary(NodeId to);

    HWND hwnd_ = nullptr;
    PaintBuffer pb_;
    ScrollBar vbar_;
    TreeModel* model_ = nullptr;
    std::vector<TreeNode> rows_;
    std::unordered_map<NodeId, int> rowIndex_;
    std::vector<NodeId> sel_;
    NodeId primary_ = 0;
    NodeId anchor_ = 0;
    NodeId hover_ = 0;
    int scrollY_ = 0;
    int clientW_ = 0;
    int clientH_ = 0;
    bool focused_ = false;

    HWND edit_ = nullptr;
    NodeId editing_ = 0;

    bool dragArmed_ = false;
    bool dragging_ = false;
    POINT dragStart_{};
    NodeId dragId_ = 0;
    NodeId dropId_ = 0;
    DropZone dropZone_ = DropZone::Inside;
    bool dropValid_ = false;
    POINT cursor_{};
    int lastMouseY_ = 0;
    int autoScrollDir_ = 0;

    std::function<void(const std::vector<NodeId>&)> onSel_;
    std::function<void(NodeId, bool)> onExp_;
    std::function<void(NodeId, const RECT&)> onAdd_;
    std::function<void(NodeId, const std::string&)> onRename_;
    std::function<void(const std::vector<NodeId>&)> onDel_;
    std::function<void(NodeId, NodeId, int)> onReparent_;
};

}  // namespace ui
}  // namespace sk
