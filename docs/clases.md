# Catalogo de clases y propiedades

Este documento describe el **catalogo de clases** del motor y el **valor por
defecto de cada propiedad**. La fuente unica de datos es el codigo:

- `src/scene/class_catalog.h` / `src/scene/class_catalog.cpp`

Cualquier cambio en las clases (nombre, categoria, icono, descripcion,
propiedades o valores por defecto) se hace **solo** ahi, y se refleja
automaticamente en:

- la **Referencia de clases** (F1),
- las descripciones y la tabla de propiedades de **Properties**,
- el buscador del boton **`+`** del Explorer,
- las etiquetas de cada nodo del Explorer.

## Categorias

El catalogo agrupa las clases en 19 categorias (el icono es provisional):

1. Proyecto y organizacion
2. Iluminacion y ambiente
3. Fuentes de luz
4. Fisica y articulaciones
5. Particulas y efectos
6. Interaccion
7. Animaciones
8. Audio
9. Controles de entrada
10. Comunicacion
11. Scripts
12. Tiempo
13. Gestion de recursos
14. Diagnostico
15. Persistencia
16. Edicion en ejecucion
17. Interfaz (GUI)
18. Interpolacion
19. Objetos de valor

## Clases abstractas y no insertables

- Las clases **abstractas** (`GuiObject`, `SoundEffect`, `UIComponent`,
  `UILayout`, `UIGridStyleLayout`, ...) no se pueden insertar con el `+`.
- Los **servicios** del sistema (terminan en `Service`, mas `ErrorReporter`,
  `Stats` y `DebugSettings`) tampoco aparecen en el buscador.
- Los valores de `Light.Type` (`Point`, `Spot`, `Surface`) no son clases: se
  eligen en Properties sobre una clase `Light`.

## Jerarquia por defecto de una Dimension

```
Dimensions
└── dimension01
    ├── Workspace
    ├── Camera
    ├── Lighting
    │   ├── Sky
    │   ├── Atmosphere
    │   ├── Clouds
    │   ├── BloomEffect
    │   ├── BlurEffect
    │   ├── ColorCorrectionEffect
    │   ├── ColorGradingEffect
    │   ├── DepthOfFieldEffect
    │   └── SunRaysEffect
    └── Scripts
Scripts               (global del proyecto: Server/Shared/Player/Character)
Assets                (Models/Meshes/Images/Textures/Audio/Animations/ModuleScripts)
```

`Workspace`, `Camera`, `Lighting` y `Scripts` de cada dimension son **nodos
protegidos**: no se renombran ni se borran. El nombre de la **Dimension** si es
editable, y los **efectos** de `Lighting` se pueden borrar y reinsertar.

## Propiedades por defecto

Cada propiedad tiene `nombre`, `tipo` (`text`, `number`, `bool`, `Vector3`,
`color`, `enum`, `object`), `valor por defecto` y `descripcion`. Estas son las
fichas completas ya definidas (el resto se completara igual, en
`class_catalog.cpp`):

### Part

| Propiedad | Tipo | Defecto | Descripcion |
| --- | --- | --- | --- |
| Name | text | Part | Nombre visible del objeto. |
| Parent | object | Workspace | Objeto que lo contiene. |
| Position | Vector3 | 0, 0, 0 | Posicion en el mundo. |
| Size | Vector3 | 4, 1.2, 2 | Tamano en los tres ejes. |
| Orientation | Vector3 | 0, 0, 0 | Rotacion en grados (X/Y/Z). |
| Shape | enum | Cube | Forma geometrica del objeto. |
| Anchored | bool | false | No le afecta la fisica. |
| CanCollide | bool | true | Colisiona con otros objetos. |
| CastShadow | bool | true | Proyecta sombra. |
| Locked | bool | false | No se puede mover en el editor. |
| Reflectance | number | 0 | Reflejo de la superficie (0 a 1). |
| Transparency | number | 0 | Opacidad inversa (0 a 1). |

### Workspace / Camera / Lighting / Sky / Atmosphere / Light

- **Workspace**: Gravity = 196.2.
- **Camera**: FieldOfView = 70, Position = 0, 20, 0.
- **Lighting**: Ambient = 0.1, 0.1, 0.1; Brightness = 2; ClockTime = 14.
- **Sky**: StarCount = 3000.
- **Atmosphere**: Density = 0.3; Haze = 1.5.
- **Light**: Type = Point; Brightness = 1; Range = 8; Color = 1, 1, 1.

### Model / Folder / Humanoid / SpawnLocation / Sound

- **Model**: PrimaryPart = "" (ninguna).
- **Folder**: (sin propiedades propias).
- **Humanoid**: Health = 100; MaxHealth = 100; WalkSpeed = 16; JumpPower = 50.
- **SpawnLocation**: Duration = 0; Neutral = false.
- **Sound**: Volume = 0.5; Looped = false; Playing = false.

## Attributes y Tags

Todas las clases tienen, ademas, dos secciones comunes en Properties:

- **Attributes**: pares `nombre` + `tipo` (`text`, `number`, `bool`, `vector3`,
  `color`) + `valor`.
- **Tags**: etiquetas de texto (una por linea). `CollectionService` permite
  buscar objetos por etiqueta.
