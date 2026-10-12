#pragma once

#include <functional>
#include <string>
#include <vector>

#include <windows.h>

#include "../scene/instance.h"
#include "kit/tab_strip.h"

namespace sk {
class World;
namespace ui {

// Fila de dimensiones (TabStrip) que vive entre la fila PLACE/CODE/GUI y la
// zona de contenido. Una pestana por Dimension del World, con "+" para crear
// una nueva, "x" para cerrarla (el dueno confirma) y doble clic para
// renombrarla en linea. La seleccion, el alta, el cierre y el renombrado se
// delegan en el dueno (que aplica comandos con undo y guardado).
class DimensionsBar {
public:
    bool create(void* parentHwnd);
    void destroy();

    void setWorld(const World* world);
    const World* world() const { return world_; }

    // Dimension activa (cambia la pestana seleccionada sin rellenar los
    // items: eso lo hace refresh()).
    void setActive(InstanceId id);
    InstanceId active() const { return active_; }

    // Reconstruye las pestanas a partir de world.dimensions().
    void refresh();

    // width/height son del rectangulo del Layout.
    void resize(int x, int y, int width, int height);
    void setVisible(bool visible);
    void* hwnd() const { return hwnd_; }

    // La dimension "index" se selecciono.
    void setOnSelected(std::function<void(InstanceId)> cb) {
        onSelected_ = std::move(cb);
    }
    // Se pulso la "x" de la dimension (el dueno confirma y borra).
    void setOnCloseRequested(std::function<void(InstanceId)> cb) {
        onCloseRequested_ = std::move(cb);
    }
    // Se pulso el "+".
    void setOnPlus(std::function<void()> cb) { onPlus_ = std::move(cb); }
    // Doble clic y Enter sobre una pestana con un nombre nuevo.
    void setOnRenamed(std::function<void(InstanceId, const std::string&)> cb) {
        onRenamed_ = std::move(cb);
    }

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam,
                                       long long lParam);
    static LRESULT CALLBACK editProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam, UINT_PTR idSubclass,
                                     DWORD_PTR refData);

    void layoutEdit();
    void beginRename(int index);
    void commitRename(bool accept);
    void invalidate();

    void* parent_ = nullptr;
    void* hwnd_ = nullptr;
    const World* world_ = nullptr;
    TabStrip tabs_;
    std::vector<InstanceId> ids_;  // index de pestana -> InstanceId
    InstanceId active_ = kInvalidId;

    void* renameEdit_ = nullptr;
    int renameIndex_ = -1;      // pestana en edicion (-1 = ninguna)
    bool renameBusy_ = false;   // evita reentrada en commitRename
    bool trackingLeave_ = false;
    bool suppressSelect_ = false; // no avisar al dueno al reflejar el estado

    std::function<void(InstanceId)> onSelected_;
    std::function<void(InstanceId)> onCloseRequested_;
    std::function<void()> onPlus_;
    std::function<void(InstanceId, const std::string&)> onRenamed_;
};

} // namespace ui
} // namespace sk
