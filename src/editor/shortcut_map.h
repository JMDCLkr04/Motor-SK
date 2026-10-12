#pragma once

#include <string>
#include <utility>
#include <vector>

namespace sk::editor {

// Codigos de tecla que no son letras ni digitos (los de Windows, sin
// incluir windows.h: este modulo es logica pura).
inline constexpr int kKeyDelete = 0x2E; // VK_DELETE
inline constexpr int kKeyEscape = 0x1B; // VK_ESCAPE
inline constexpr int kKeyF1 = 0x70;     // VK_F1
inline constexpr int kKeyF2 = 0x71;     // VK_F2

// Un atajo: accion logica + etiqueta para el dialogo + tecla. `vk` es el
// codigo de tecla virtual de Windows (las letras/digitos usan su codigo
// ASCII en mayusculas, que coincide con el VK_*).
struct Shortcut {
    const char* action = "";
    const char* label = "";
    int vk = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

// Tabla unica de atajos del editor: la consultan el manejador de teclado
// (matches) y el dialogo "Personalizar atajos" (describe). Es la fuente de
// verdad: cambiar aqui un atajo lo cambia en ambos sitios.
class ShortcutMap {
public:
    // Todos los atajos, en orden de presentacion.
    static const std::vector<Shortcut>& all();

    // Atajo de una accion (nullptr si no existe).
    static const Shortcut* find(const char* action);

    // True si (vk, modificadores) activa `action`. Los modificadores que el
    // atajo no exige se ignoran (p.ej. Ctrl+Shift+G sigue agrupando).
    static bool matches(const char* action, int vk, bool ctrl, bool shift,
                        bool alt);

    // Pares de acciones que comparten tecla y modificadores exactos. La
    // tabla por defecto no tiene colisiones.
    static std::vector<std::pair<std::string, std::string>> collisions();

    // Texto del dialogo "Personalizar atajos" (una linea por atajo).
    static std::string describe();

    // Texto legible de la combinacion de un atajo ("Ctrl+G", "F1", ...).
    static std::string keyText(const Shortcut& shortcut);
};

} // namespace sk::editor
