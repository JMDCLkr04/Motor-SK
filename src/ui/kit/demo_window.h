#pragma once

namespace sk {
namespace ui {

// Ventana de demostracion del kit: acentos (UTF-8 -> W), repintado parcial
// por hover, listas con cabecera multi-columna, tabs con cierre/"+", etc.
bool runUIDemo();

// Repinta un widget 1.000 veces y compara GetGuiResources(GR_GDIOBJECTS)
// antes y despues; falla si crece mas de 5 objetos GDI. Imprime ambos
// contadores por stdout.
bool runGdiCheck();

} // namespace ui
} // namespace sk