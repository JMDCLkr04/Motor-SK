#pragma once

#include <string>
#include <vector>

#include "../math/math.h"

namespace sk {

// Forma geometrica de un Part. Mismos nombres que en Roblox (Block,
// Ball, Cylinder, Wedge, CornerWedge) con el rombo (octaedro) como
// extra low-poly. El cubo es el valor por defecto y el indice 0, asi
// que las escenas antiguas sin "forma" cargan tal cual.
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
// Devuelve "cubo" para formas desconocidas (y shapeFromName acepta
// cualquier texto sin romper la carga).
const char* shapeName(Shape shape);
Shape shapeFromName(const std::string& name);

// Nombres de los nodos estructurales que toda Dimension trae por defecto
// y que no se pueden renombrar ni borrar.
inline constexpr const char* kDimensionName = "dimension01";
inline constexpr const char* kWorkspaceName = "Workspace";
inline constexpr const char* kCameraName = "Camera";
inline constexpr const char* kLightingName = "Lighting";
inline constexpr const char* kScriptsName = "Scripts";

// Attribute de un objeto (seccion Attributes de Properties). El tipo es
// uno de: "text", "number", "bool", "vector3", "color"; el valor se
// guarda siempre como texto y cada tipo lo interpreta a su manera.
struct Attribute {
    std::string name;
    std::string type = "text";
    std::string value;
    bool operator==(const Attribute&) const = default;
};

// Objeto de la escena PLACE: nodo con su clase, su transformacion
// (traslacion, rotacion en grados y escala), su forma geometrica, su
// asociacion ModelScript y sus Attributes/Tags. Los nodos estructurales
// (Dimension, Workspace, Camera, Lighting, Scripts) tambien son objetos,
// con protectedNode = true.
struct SceneObject {
    std::string name;   // nombre visible en el Explorer
    std::string className = "Part";  // clase del objeto ("Part", "Model"...)
    // Nombre del padre en la jerarquia del Explorer ("" = raiz: un nodo
    // Dimension). Con esto los objetos pueden anidarse padres/hijos; el
    // render aun usa position absoluto.
    std::string parent;
    bool protectedNode = false;  // no se renombra ni se borra
    Vec3 position{};
    Vec3 rotation{};    // grados en los ejes X/Y/Z
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Shape shape = Shape::Cube;
    std::string script; // rel bajo scripts/ ("Server/Saludo.sk")

    // Appearance
    bool castShadow = true;
    float reflectance = 0.0f;   // 0.00 a 1.00
    float transparency = 0.0f;  // 0.00 a 1.00

    // Data
    bool locked = false;

    // Pivot (origen de la part, independiente de position)
    Vec3 pivotPosition{};
    Vec3 pivotRotation{};  // grados en los ejes X/Y/Z

    // Collision
    bool canCollide = true;
    bool anchored = false;

    // Attributes y Tags: disponibles en todas las clases.
    std::vector<Attribute> attributes;
    std::vector<std::string> tags;

    // T * Rx * Ry * Rz * S (las rotaciones se aplican en este orden).
    Mat4 modelMatrix() const;
};

// true si la clase del objeto se dibuja en el viewport 3D (Part, MeshPart
// y demas piezas con geometria). Los contenedores y servicios no.
bool isRenderableClass(const std::string& className);

// Coleccion de objetos de la division PLACE. Solo la muta la interfaz
// (boton "Part" e insertar clase); el renderer la lee por const
// referencia cada frame.
class Scene {
public:
    // Anade un Part en una fila de la rejilla (Part, Part1, Part2...)
    // con la forma indicada (cubo por defecto), dentro de Workspace.
    // Devuelve una referencia al ultimo elemento; no conservarla tras
    // cualquier otra llamada a addPart/clear.
    SceneObject& addPart(Shape shape = Shape::Cube);

    // Inserta un nodo de una clase arbitraria bajo "parent" (vacio = la
    // dimension por defecto). El nombre se deduce de la clase con un
    // sufijo numerico si hace falta.
    SceneObject& addNode(const std::string& className, const std::string& parent);

    // Crea los nodos estructurales que falten (dimension01 con Workspace,
    // Camera, Lighting + efectos y Scripts). Idempotente. Si "Workspace"
    // de la dimension esta, no vuelve a anadir los efectos de Lighting.
    void ensureStructure();

    // true si el nombre corresponde a un nodo estructural protegido.
    bool isProtectedName(const std::string& name) const;

    void clear();

    const SceneObject* findByName(const std::string& name) const;
    SceneObject* findByName(const std::string& name);

    // Renombra un objeto (transform y script intactos). Requiere que el
    // origen exista, que no sea un nodo protegido y que el destino este
    // vacio: devuelve false sin tocar nada en caso contrario.
    bool renameObject(const std::string& oldName, const std::string& newName);

    // Elimina un objeto por nombre. Devuelve false si no existe o si es
    // un nodo protegido.
    bool removeObject(const std::string& name);

    // Duplica un objeto: copia todos sus campos con un nombre nuevo
    // (Part, Part1...) y su posicion desplazada por "offset". El clon no
    // es protegido. Devuelve una referencia al nuevo objeto.
    SceneObject& addCopy(const SceneObject& src, const Vec3& offset);

    const std::vector<SceneObject>& objects() const { return objects_; }
    std::vector<SceneObject>& objects() { return objects_; }

    // Persistencia en escenas/inicio.scene (texto "clave: valor",
    // versionado). Un archivo ausente o vacio = escena nueva (se le anade
    // la estructura por defecto).
    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;

private:
    std::vector<SceneObject> objects_;
};

} // namespace sk
