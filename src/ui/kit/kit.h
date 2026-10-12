#pragma once

#include <cstdint>
#include <string>

namespace sk {
namespace ui {

// Identificador de nodo/columna del kit (nada de punteros).
using NodeId = uint32_t;

// El texto interno del editor es UTF-8; las APIs del kit son W. widen()
// convierte UTF-8 -> UTF-16 y narrow() el camino inverso.
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);

} // namespace ui
} // namespace sk