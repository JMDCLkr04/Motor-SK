#pragma once

namespace sk {

struct Project;

// Division CODE: ocupa solo el area central (entre los paneles laterales
// compartidos, que gestiona PlaceView). El organizador de scripts vive
// ahora en la rama Scripts del Explorer; aqui queda el sitio del editor
// de codigo del script seleccionado, que llega en un paso posterior. De
// momento el panel muestra un aviso ("Editor de codigo: proximamente").
class CodeView {
public:
    bool create(void* parentHwnd);
    void destroy();

    // Reservado para cuando el editor multiplinea cargue el script. Hoy no
    // hace nada salvo guardar la referencia.
    void open(const Project& project);
    void close();

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // width/height son los de la ventana principal: el panel se coloca en
    // el rectangulo central (Layout::viewport), no a pantalla completa.
    void resize(int width, int height);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);

    void* parent_ = nullptr;
    void* hwnd_ = nullptr;
    const Project* project_ = nullptr;
    bool visible_ = false;
};

} // namespace sk
