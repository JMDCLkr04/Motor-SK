#pragma once

#include <vector>

#include "../math/math.h"
#include "instance.h"

namespace sk {

class Camera;
class World;

// Seleccion en el viewport 3D de PLACE:
//  - pickObject: rayo desde la camara por un pixel -> objeto mas cercano.
//  - selectInRect: proyecta las cajas de los objetos y devuelve las que
//    tocan el rectangulo de arrastre (marquee), en coordenadas de cliente.
// Devuelven InstanceId (kInvalidId = sin hit).

// Rayo en espacio mundo desde la posicion de un pixel de cliente.
struct Ray {
    Vec3 origin{};
    Vec3 dir{};
};

Ray rayFromCamera(const Camera& camera, float aspect, float fovYDegrees,
                  const Vec2& pixel, int width, int height);

// Id del objeto mas cercano tocado por el rayo (kInvalidId si no hay hit).
InstanceId pickObject(const World& world, const Ray& ray);

// Igual que pickObject ignorando los ids de `ignore`.
InstanceId pickObjectExcept(const World& world, const Ray& ray,
                            const std::vector<InstanceId>& ignore);

// Como pickObjectExcept pero devuelve tambien el punto de impacto.
bool pickObjectHit(const World& world, const Ray& ray,
                   const std::vector<InstanceId>& ignore, InstanceId& idOut,
                   Vec3& pointOut);

// Como pickObjectHit pero devuelve ademas la normal de la cara golpeada.
bool pickObjectFace(const World& world, const Ray& ray,
                    const std::vector<InstanceId>& ignore, InstanceId& idOut,
                    Vec3& pointOut, Vec3& normalOut);

// Impacto del rayo con un unico objeto: punto y normal de la cara
// golpeada en espacio mundo. false si el rayo no lo toca.
bool rayObjectFace(const Instance& instance, const Ray& ray, Vec3& pointOut,
                   Vec3& normalOut);

// Ids cuyas cajas proyectadas intersectan el rectangulo (x0,y0)-(x1,y1)
// en coordenadas de cliente. x0<x1, y0<y1.
std::vector<InstanceId> selectInRect(const World& world, const Mat4& viewProj,
                                     const Vec2& a, const Vec2& b, int width,
                                     int height);

} // namespace sk
