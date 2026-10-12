#pragma once

#include <string>

namespace sk {

// Forma geometrica de un Part (mismos nombres que en Roblox salvo el
// rombo, que es un extra low-poly). El cubo es el indice 0 para que las
// escenas antiguas sin "forma" carguen tal cual.
enum class Shape : int {
    Cube = 0,      // cubo
    Rhombus,       // rombo (octaedro: 8 caras)
    Sphere,        // esfera low-poly (8x4 = 48 triangulos)
    Cylinder,      // cilindro con el eje en X (como en Roblox)
    Wedge,         // cuna: prisma triangular con la pendiente en +Z
    CornerWedge,   // cuna de esquina: tetraedro de 3 tri. rectos
    Capsule,       // capsula: cilindro con casquetes semiesfericos
    Count,
};

// Nombre canonico de la forma para el archivo de escena ("forma: ...").
const char* shapeName(Shape shape);
Shape shapeFromName(const std::string& name);

} // namespace sk
