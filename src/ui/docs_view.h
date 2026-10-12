#pragma once

#include <windows.h>

#include <string>

#include "kit/list_widget.h"

namespace sk {

// Vista "Documentacion de clases": reemplaza al viewport 3D. A la izquierda
// la lista completa de clases (icono + nombre + descripcion corta); a la
// derecha, separado por una linea, la categoria, la descripcion detallada
// y la tabla "Propiedades" con los valores por defecto.
//
// Las dos listas usan el ListWidget del kit (fondo oscuro, cabeceras con
// esquinas redondeadas, hover y seleccion), de modo que no asome el blanco
// nativo del ListView. Los emojis son provisionales; mas adelante se
// sustituyen por PNG.
class DocsView {
public:
    static constexpr int kListWidth = 360;
    static constexpr int kIconColWidth = 34;
    static constexpr int kNameColWidth = 168;

    bool create(void* parentHwnd);
    void destroy();

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // width/height son los de la ventana principal.
    void resize(int width, int height);

    // Selecciona la clase con ese nombre (para F1 desde un objeto); si no
    // existe, deja la seleccion como esta.
    void showClass(const std::string& className);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);

    void buildClassList();
    void layout(int width, int height);
    void updateDetails();

    void* parent_ = nullptr;
    void* hwnd_ = nullptr;
    ui::ListWidget classList_;    // lista de clases (izquierda)
    void* divider_ = nullptr;     // linea divisoria vertical
    void* title_ = nullptr;       // nombre de la clase seleccionada
    void* category_ = nullptr;    // categoria de la clase
    void* desc_ = nullptr;        // descripcion detallada (multilinea)
    void* propsLabel_ = nullptr;  // rotulo "Propiedades"
    ui::ListWidget propsList_;    // tabla de propiedades (derecha)
    int sel_ = -1;
    bool visible_ = false;
};

} // namespace sk
