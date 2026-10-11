#pragma once

#include <functional>
#include <string>

namespace sk {

// Selector de clase del boton "+" del Explorer: ventana emergente con un
// buscador y la lista de clases insertables del catalogo (ordenadas por
// categoria). Solo una instancia a la vez; abrir otra cierra la anterior.
class ClassPicker {
public:
    // Muestra el popup junto a la posicion (x, y) de pantalla. onPick
    // recibe el nombre de la clase elegida (nunca vacio).
    static void open(void* ownerHwnd, int x, int y,
                     std::function<void(const std::string&)> onPick);

    // Cierra el popup si esta abierto (p. ej. al destruir el dueno).
    static void close();
};

} // namespace sk
