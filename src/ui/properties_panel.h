#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor/property_model.h"
#include "kit/paint_buffer.h"
#include "kit/scroll_bar.h"
#include "scene/class_registry.h"
#include "scene/instance.h"

namespace sk {
class World;

namespace ui {

// Panel Properties dibujado a mano (PaintBuffer + ScrollBar del kit). Las
// filas se generan desde el ClassRegistry para la seleccion actual: nombre
// y padre fijos, luego las secciones de la clase (plegables), y al final
// Attributes y Tags (estilo Roblox). Solo la celda en edicion es un EDIT
// real superpuesto; el resto se pinta. Todas las ediciones salen por
// callbacks; el dueno las convierte en comandos (undo + guardado diferido).
class PropertiesPanel {
public:
    using PropertyEdited =
        std::function<void(InstanceId, const std::string&, const PropertyValue&)>;
    using NameEdited = std::function<void(InstanceId, const std::string&)>;
    using AttributeSet = std::function<void(InstanceId, const Attribute&)>;
    using AttributeRemoved =
        std::function<void(InstanceId, const std::string&)>;
    using TagAdded = std::function<void(InstanceId, const std::string&)>;
    using TagRemoved = std::function<void(InstanceId, const std::string&)>;

    explicit PropertiesPanel(const World* world = nullptr);
    ~PropertiesPanel();

    void create(HWND parent, int id);
    void destroy();
    HWND hwnd() const { return hwnd_; }
    void setRect(const RECT& r);
    void setWorld(const World* world) { world_ = world; }

    void setSelection(const std::vector<InstanceId>& ids);
    void clearSelection() { setSelection({}); }
    // Reconstruye filas/atributos/tags (tras undo, renombrado, ...).
    void refresh();

    void setOnPropertyEdited(PropertyEdited cb) {
        onPropertyEdited_ = std::move(cb);
    }
    void setNameEdited(NameEdited cb) { onNameEdited_ = std::move(cb); }
    void setOnAttributeSet(AttributeSet cb) {
        onAttributeSet_ = std::move(cb);
    }
    void setOnAttributeRemoved(AttributeRemoved cb) {
        onAttributeRemoved_ = std::move(cb);
    }
    void setOnTagAdded(TagAdded cb) { onTagAdded_ = std::move(cb); }
    void setOnTagRemoved(TagRemoved cb) { onTagRemoved_ = std::move(cb); }

private:
    // Fila del listado visible (propiedades + secciones + atributos/tags).
    struct Item {
        enum class Kind {
            Name,
            Parent,
            Section,
            Property,
            Axis,
            AttrHeader,
            Attribute,
            AddAttribute,
            TagHeader,
            TagInput,
            TagChip
        };
        Kind kind = Kind::Property;
        std::string section;
        int rowIndex = -1;   // Property/Axis: indice en rows_
        int axis = -1;       // Axis: 0..2
        std::string attrName;
        std::string tag;
        int y = 0;
        int h = 24;
    };

    struct EditTarget {
        enum class Kind { None, Property, Axis, Name, AttrName, AttrValue, Tag };
        Kind kind = Kind::None;
        int rowIndex = -1;
        int axis = -1;
        std::string key;  // nombre del atributo (AttrName/AttrValue)
    };

    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK editProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR,
                                     DWORD_PTR);

    void rebuild();
    void buildItems();
    void layoutItems();
    void invalidate();
    void trackLeave();

    const Instance* primary() const;
    void itemRects(const Item& it, RECT& label, RECT& value,
                   RECT& restore) const;
    const Item* itemAt(int y) const;
    RECT searchRect() const;
    int bodyRight() const;

    void onLButtonDown(int x, int y);
    void onEnterOrCommit();
    void toggleBool(const Item& it);
    void openEditor(const Item& it);
    void openFreeEditor(EditTarget target, const RECT& cell, const std::string& text);
    void commitEditor(bool cancel);
    void applyPropertyText(int rowIndex, int axis, const std::string& text);
    void restoreDefault(const Item& it);
    void updateTooltip(int x, int y);

    // Politica de secciones plegadas y Vec3 desplegados.
    bool sectionCollapsed(const std::string& s) const;
    bool vecExpanded(const std::string& name) const;

    void paint(HDC hdc);  // en properties_panel_paint.cpp

    const World* world_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND searchEdit_ = nullptr;
    HWND cellEdit_ = nullptr;
    HWND tooltip_ = nullptr;
    PaintBuffer pb_;
    ScrollBar vbar_;

    std::vector<InstanceId> selection_;
    std::string search_;
    std::vector<PropertyRow> rows_;
    std::vector<Item> items_;
    std::unordered_map<std::string, bool> collapsed_;
    std::unordered_map<std::string, bool> vecOpen_;

    int scrollY_ = 0;
    int contentH_ = 0;
    int hoverItem_ = -1;
    bool trackingLeave_ = false;
    EditTarget edit_;
    wchar_t tipText_[512]{};

    PropertyEdited onPropertyEdited_;
    NameEdited onNameEdited_;
    AttributeSet onAttributeSet_;
    AttributeRemoved onAttributeRemoved_;
    TagAdded onTagAdded_;
    TagRemoved onTagRemoved_;

    static constexpr int kSearchH = 30;
    static constexpr int kRowH = 22;
    static constexpr int kHeaderH = 24;
    static constexpr int kBarW = 12;
    static constexpr int kLabelW = 132;
};

} // namespace ui
} // namespace sk
