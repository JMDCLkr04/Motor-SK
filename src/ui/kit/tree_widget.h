#pragma once

#include <functional>
#include <unordered_map>
#include <vector>

#include "paint_buffer.h"
#include "scroll_bar.h"

namespace sk {
namespace ui {

struct TreeNode {
    void* id; // InstanceId cast to void*
    void* parentId;
    const char* text;
    bool hasChildren;
    bool expanded;
    bool protectedNode;
};

class TreeWidget {
public:
    TreeWidget();

    void create(HWND parent, int x, int y, int w, int h);
    void destroy();
    HWND hwnd() const { return hwnd_; }
    void setWorldRoot(void* rootId);

    void setNodes(const std::vector<TreeNode>& flat);
    void clear();

    void setOnSelectionChanged(std::function<void(const std::vector<void*>&)> cb) { onSel_ = std::move(cb); }
    void setOnExpandChanged(std::function<void(void*, bool)> cb) { onExp_ = std::move(cb); }
    void setOnAddRequested(std::function<void(void*, const RECT&)> cb) { onAdd_ = std::move(cb); }
    void setOnRenameRequested(std::function<void(void*, const char*)> cb) { onRename_ = std::move(cb); }
    void setOnDeleteRequested(std::function<void(const std::vector<void*>&)> cb) { onDel_ = std::move(cb); }
    void setOnReparentRequested(std::function<void(void*, void*, int)> cb) { onReparent_ = std::move(cb); }

    void select(const std::vector<void*>& ids, void* primary);
    std::vector<void*> selection() const;
    void* primary() const;
    void updateNode(void* id, const TreeNode& node);
    void removeNode(void* id);
    void expandTo(void* id);
    void scrollTo(void* id);

    void invalidate();

private:
    struct Item {
        void* id;
        void* parentId;
        char text[256];
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
    void startDrag();
    void cancelDrag();
    void handleDrop(int targetIdx, int zone);

    HWND hwnd_ = nullptr;
    PaintBuffer pb_;
    ScrollBar vbar_;
    std::vector<Item> items_;
    std::vector<int> visible_;
    std::unordered_map<void*, int> byId_;
    std::vector<void*> sel_;
    void* primary_ = nullptr;
    void* hoverId_ = nullptr;
    bool showAdd_ = false;
    RECT hoverRect_{};
    int editIdx_ = -1;
    HWND editHwnd_ = nullptr;
    bool dragging_ = false;
    POINT dragStart_{};
    int dragIdx_ = -1;
    void* dragId_ = nullptr;
    int dropTarget_ = -1;
    int dropZone_ = 0; // 0 before, 1 inside, 2 after
    void* rootId_ = nullptr;
    int scrollY_ = 0;
    std::function<void(const std::vector<void*>&)> onSel_;
    std::function<void(void*, bool)> onExp_;
    std::function<void(void*, const RECT&)> onAdd_;
    std::function<void(void*, const char*)> onRename_;
    std::function<void(const std::vector<void*>&)> onDel_;
    std::function<void(void*, void*, int)> onReparent_;
};

} // namespace ui
} // namespace sk
