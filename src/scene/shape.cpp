#include "shape.h"

namespace sk {

const char* shapeName(Shape shape) {
    switch (shape) {
        case Shape::Rhombus:     return "rombo";
        case Shape::Sphere:      return "esfera";
        case Shape::Cylinder:    return "cilindro";
        case Shape::Wedge:       return "cuna";
        case Shape::CornerWedge: return "cuna-esquina";
        case Shape::Capsule:     return "capsula";
        case Shape::Cube:
        default:                 return "cubo";
    }
}

Shape shapeFromName(const std::string& name) {
    if (name == "rombo") return Shape::Rhombus;
    if (name == "esfera") return Shape::Sphere;
    if (name == "cilindro") return Shape::Cylinder;
    if (name == "cuna") return Shape::Wedge;
    if (name == "cuna-esquina") return Shape::CornerWedge;
    if (name == "capsula") return Shape::Capsule;
    return Shape::Cube;
}

} // namespace sk
