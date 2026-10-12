#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

#include "kit.h"
#include "paint_buffer.h"
#include "scroll_bar.h"

namespace sk {
namespace ui {

struct TreeNode {
    NodeId id = 0;          // 0 = invalido
    NodeId parentId = 0;
    std::string text;       // UTF-8 interno
    bool hasChildren = false;
    bool expanded = false;
    bool protectedNode = false;
};

class TreeWidget {
public:
    TreeWidget();

    void create(HWND parent, int x, int y, int w, int h);
    void destroy();
    HWND hwnd() const { return hwnd_; }
    void setWorldRoot(NodeId rootId);

    void setNodes(const std::vector<TreeNode>& flat);
    void clear();

    void setOnSelectionChanged(std::function<void(const std::vector<NodeId>&)> cb) { onSel_ = std::move(cb); }
    void setOnExpandChanged(std::function<void(NodeId, bool)> cb) { onExp_ = std::move(cb); }
    void setOnAddRequested(std::function<void(NodeId, const RECT&)> cb) { onAdd_ = std::move(cb); }
    void setOnRenameRequested(std::function<void(NodeId, const std::string&)> cb) { onRename_ = std::move(cb); }
    void setOnDeleteRequested(std::function<void(const std::vector<NodeId>&)> cb) { onDel_ = std::move(cb); }
    void setOnReparentRequested(std::function<void(NodeId, NodeId, int)> cb) { onReparent_ = std::move(cb); }

    void select(const std::vector<NodeId>& ids, NodeId primary);
    std::vector<NodeId> selection() const;
    NodeId primary() const;
    void updateNode(NodeId id, const TreeNode& node);
    void removeNode(NodeId id);
    void expandTo(NodeId id);
    void scrollTo(NodeId id);

    void invalidate();

private:
    struct Item {
        NodeId id;
        NodeId parentId;
        std::string text;
        bool hasChildren;
        bool expanded;
        bool protectedNode;
        int level;
    };

    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    void buildList();
    void ensureVisible(int idx);
    int hitRow(int y);
    void paint(HDC);

    HWND hwnd_ = nullptr;
    PaintBuffer pb_;
    ScrollBar vbar_;
    std::vector<Item> items_;   // orden plano (padres antes que hijos)
    std::vector<int> visible_;  // indices de items_ visibles
    std::unordered_map<NodeId, int> byId_;
    std::vector<NodeId> sel_;
    NodeId primary_ = 0;
    NodeId hoverId_ = 0;
    NodeId rootId_ = 0;
    int scrollY_ = 0;
    std::function<void(const std::vector<NodeId>&)> onSel_;
    std::function<void(NodeId, bool)> onExp_;
    std::function<void(NodeId, const RECT&)> onAdd_;
    std::function<void(NodeId, const std::string&)> onRename_;
    std::function<void(const std::vector<NodeId>&)> onDel_;
    std::function<void(NodeId, NodeId, int)> onReparent_;
};

} // namespace ui
} // namespace sk