#include "class_registry.h"

#include <cstring>
#include <unordered_map>

namespace sk {

const char* propTypeName(PropType type) {
    switch (type) {
        case PropType::Bool:      return "bool";
        case PropType::Int:       return "int";
        case PropType::Float:     return "number";
        case PropType::String:    return "text";
        case PropType::Vec3:      return "Vector3";
        case PropType::Color:     return "color";
        case PropType::Enum:      return "enum";
        case PropType::RefObject: return "object";
    }
    return "text";
}

namespace {

// --- Categorias (19) ----------------------------------------------------
const Category kCategories[] = {
    {L"\U0001F5C2", "Proyecto y organizacion"},
    {L"\U0001F4A1", "Iluminacion y ambiente"},
    {L"\U0001F505", "Fuentes de luz"},
    {L"\u2699", "Fisica y articulaciones"},
    {L"\u2728", "Particulas y efectos"},
    {L"\U0001F5B1", "Interaccion"},
    {L"\U0001F3AC", "Animaciones"},
    {L"\U0001F50A", "Audio"},
    {L"\U0001F3AE", "Controles de entrada"},
    {L"\U0001F4E1", "Comunicacion"},
    {L"\U0001F4DC", "Scripts"},
    {L"\u23F0", "Tiempo"},
    {L"\U0001F4E6", "Gestion de recursos"},
    {L"\U0001F41E", "Diagnostico"},
    {L"\U0001F4BE", "Persistencia"},
    {L"\U0001F6E0", "Edicion en ejecucion"},
    {L"\U0001F5BC", "Interfaz (GUI)"},
    {L"\U0001F300", "Interpolacion"},
    {L"\U0001F522", "Objetos de valor"},
};

#define P(nm, ty, df, sec, ds) \
    {nm, ty, df, sec, ds, false, nullptr}
#define PE(nm, ty, df, sec, ds, opts) \
    {nm, ty, df, sec, ds, false, opts}

// --- Propiedades propias de las clases clave ----------------------------
const PropertyDef kPartProps[] = {
    P("Position", PropType::Vec3, "0, 0, 0", "Transform",
      "Posicion en el mundo (unidad)."),
    P("Size", PropType::Vec3, "4, 1.2, 2", "Transform",
      "Tamano en los tres ejes."),
    P("Orientation", PropType::Vec3, "0, 0, 0", "Transform",
      "Rotacion en grados (X/Y/Z)."),
    PE("Shape", PropType::Enum, "Cube", "Transform",
       "Forma geometrica del objeto.",
       "Cube,Rhombus,Sphere,Cylinder,Wedge,CornerWedge,Capsule"),
    P("Anchored", PropType::Bool, "false", "Collision",
      "No le afecta la fisica."),
    P("CanCollide", PropType::Bool, "true", "Collision",
      "Colisiona con otros objetos."),
    P("CastShadow", PropType::Bool, "true", "Appearance", "Proyecta sombra."),
    P("Locked", PropType::Bool, "false", "Data",
      "No se puede mover en el editor."),
    P("Reflectance", PropType::Float, "0", "Appearance",
      "Reflejo de la superficie (0 a 1)."),
    P("Transparency", PropType::Float, "0", "Appearance",
      "Opacidad inversa (0 a 1)."),
    P("PivotPosition", PropType::Vec3, "0, 0, 0", "Pivot",
      "Origen del pivote (independiente de Position)."),
    P("PivotRotation", PropType::Vec3, "0, 0, 0", "Pivot",
      "Rotacion del pivote en grados."),
};

const PropertyDef kModelProps[] = {
    P("Position", PropType::Vec3, "0, 0, 0", "Transform",
      "Pivote del modelo en el mundo."),
    P("Orientation", PropType::Vec3, "0, 0, 0", "Transform",
      "Rotacion del modelo en grados."),
    P("PrimaryPart", PropType::RefObject, "", "Model",
      "Parte principal del modelo."),
};

const PropertyDef kWorkspaceProps[] = {
    P("Gravity", PropType::Float, "196.2", "Workspace",
      "Aceleracion de la gravedad."),
};

const PropertyDef kCameraProps[] = {
    P("Position", PropType::Vec3, "0, 20, 0", "Transform",
      "Posicion de la camara."),
    P("FieldOfView", PropType::Float, "70", "Camera",
      "Campo de vision en grados."),
};

const PropertyDef kLightingProps[] = {
    P("Ambient", PropType::Color, "0.1, 0.1, 0.1", "Lighting",
      "Luz ambiente."),
    P("Brightness", PropType::Float, "2", "Lighting", "Brillo general."),
    P("ClockTime", PropType::Float, "14", "Lighting", "Hora del dia (0 a 24)."),
};

const PropertyDef kSkyProps[] = {
    P("StarCount", PropType::Int, "3000", "Sky", "Numero de estrellas."),
};

const PropertyDef kAtmosphereProps[] = {
    P("Density", PropType::Float, "0.3", "Atmosphere", "Densidad de la niebla."),
    P("Haze", PropType::Float, "1.5", "Atmosphere", "Brillo del horizonte."),
};

const PropertyDef kLightProps[] = {
    PE("Type", PropType::Enum, "Point", "Light", "Point, Spot o Surface.",
       "Point,Spot,Surface"),
    P("Brightness", PropType::Float, "1", "Light", "Intensidad de la luz."),
    P("Range", PropType::Float, "8", "Light", "Alcance de la luz."),
    P("Color", PropType::Color, "1, 1, 1", "Light", "Color emitido."),
};

const PropertyDef kHumanoidProps[] = {
    P("Health", PropType::Float, "100", "Humanoid", "Vida actual."),
    P("MaxHealth", PropType::Float, "100", "Humanoid", "Vida maxima."),
    P("WalkSpeed", PropType::Float, "16", "Humanoid", "Velocidad al caminar."),
    P("JumpPower", PropType::Float, "50", "Humanoid", "Fuerza del salto."),
};

const PropertyDef kSpawnProps[] = {
    P("Duration", PropType::Float, "0", "Spawn", "Tiempo de reaparicion."),
    P("Neutral", PropType::Bool, "false", "Spawn", "Sirve a cualquier equipo."),
};

const PropertyDef kSoundProps[] = {
    P("Volume", PropType::Float, "0.5", "Sound", "Volumen (0 a 10)."),
    P("Looped", PropType::Bool, "false", "Sound", "Se repite."),
    P("Playing", PropType::Bool, "false", "Sound", "Esta sonando."),
};

// --- Hijos sugeridos (son sugerencias: no restringen) -------------------
const char* const kSuggPart[] = {"Attachment", "Decal", "Texture", "Light",
                                 "ParticleEmitter", "ClickDetector", "Sound"};
const char* const kSuggModel[] = {"Part", "MeshPart", "Folder"};
const char* const kSuggFolder[] = {"Part", "Model", "Folder", "Script"};
const char* const kSuggWorkspace[] = {"Part", "Model", "Folder",
                                      "SpawnLocation", "Camera"};
const char* const kSuggLighting[] = {
    "Sky", "Atmosphere", "Clouds", "BloomEffect", "BlurEffect",
    "ColorCorrectionEffect", "ColorGradingEffect", "DepthOfFieldEffect",
    "SunRaysEffect"};
const char* const kSuggScripts[] = {"Script", "LocalScript", "ModuleScript",
                                    "Folder"};
const char* const kSuggDimension[] = {"Workspace", "Camera", "Lighting",
                                      "Scripts"};

// --- Clases -------------------------------------------------------------
// C0: sin props ni sugerencias. CP: con props. CG: con sugerencias.
// CPG: con props y sugerencias. El icono nullptr = el de la categoria.
#define C0(cat, nm, base, abs, ds) \
    {nm, cat, base, abs, ds, ds, nullptr, nullptr, 0, nullptr, 0}
#define CP(cat, nm, base, abs, ds, tab) \
    {nm, cat, base, abs, ds, ds, nullptr, tab, \
     static_cast<int>(sizeof(tab) / sizeof((tab)[0])), nullptr, 0}
#define CG(cat, nm, base, abs, ds, sug) \
    {nm, cat, base, abs, ds, ds, nullptr, nullptr, 0, sug, \
     static_cast<int>(sizeof(sug) / sizeof((sug)[0]))}
#define CPG(cat, nm, base, abs, ds, tab, sug) \
    {nm, cat, base, abs, ds, ds, nullptr, tab, \
     static_cast<int>(sizeof(tab) / sizeof((tab)[0])), sug, \
     static_cast<int>(sizeof(sug) / sizeof((sug)[0]))}

const ClassDef kRawClasses[] = {
    // 0. Proyecto y organizacion.
    CG(0, "Dimension", "", false, "Dimension independiente con sus objetos, config y scripts.", kSuggDimension),
    CG(0, "Workspace", "", false, "Contiene los objetos del mundo de una dimension.", kSuggWorkspace),
    CPG(0, "Part", "", false, "Objeto 3D basico con forma, transformacion y colision.", kPartProps, kSuggPart),
    CPG(0, "Model", "", false, "Agrupa objetos para manipularlos como una unidad.", kModelProps, kSuggModel),
    CG(0, "Folder", "", false, "Organiza objetos jerarquicamente.", kSuggFolder),
    C0(0, "MeshPart", "Part", false, "Geometria basada en una malla 3D."),
    C0(0, "Decal", "", false, "Aplica una imagen sobre una superficie."),
    C0(0, "Texture", "", false, "Repite una imagen en mosaico sobre una superficie."),
    C0(0, "Color", "", false, "Color aplicable a un objeto o a parte de su geometria."),
    C0(0, "Accessory", "", false, "Accesorio configurable."),
    C0(0, "Attachment", "", false, "Punto de referencia para uniones y efectos."),
    CP(0, "Camera", "", false, "Vista de juego de una dimension.", kCameraProps),
    C0(0, "CollectionService", "", false, "Busca objetos por etiqueta y avisa de cambios."),

    // 1. Iluminacion y ambiente.
    CP(1, "Lighting", "", false, "Configura la iluminacion y el ambiente de una dimension.", kLightingProps),
    CP(1, "Sky", "", false, "Define la apariencia del cielo.", kSkyProps),
    CP(1, "Atmosphere", "", false, "Dispersion atmosferica y niebla.", kAtmosphereProps),
    C0(1, "Clouds", "", false, "Representa y configura las nubes."),
    C0(1, "PostEffect", "", true, "Clase base abstracta de efectos de posproceso."),
    C0(1, "BloomEffect", "PostEffect", false, "Resplandor alrededor de zonas luminosas."),
    C0(1, "BlurEffect", "PostEffect", false, "Aplica desenfoque."),
    C0(1, "ColorCorrectionEffect", "PostEffect", false, "Modifica color, contraste y saturacion."),
    C0(1, "ColorGradingEffect", "PostEffect", false, "Aplica gradacion de color."),
    C0(1, "DepthOfFieldEffect", "PostEffect", false, "Desenfoca segun la distancia a la camara."),
    C0(1, "SunRaysEffect", "PostEffect", false, "Simula rayos solares."),

    // 2. Fuentes de luz.
    CP(2, "Light", "", false, "Fuente de luz; Type: Point, Spot o Surface.", kLightProps),

    // 3. Fisica, movimiento y articulaciones.
    C0(3, "AlignOrientation", "", false, "Controla o alinea la orientacion de un objeto."),
    C0(3, "AngularVelocity", "", false, "Controla la velocidad angular."),
    C0(3, "LinearVelocity", "", false, "Controla la velocidad lineal."),
    C0(3, "VectorForce", "", false, "Aplica una fuerza vectorial."),
    C0(3, "BallSocketConstraint", "", false, "Rotacion en varias direcciones entre cuerpos."),
    C0(3, "HingeConstraint", "", false, "Permite rotacion alrededor de un eje."),
    C0(3, "NoCollisionConstraint", "", false, "Evita la colision entre dos objetos."),
    C0(3, "Weld", "", false, "Conecta rigidamente dos objetos."),
    C0(3, "WeldConstraint", "", false, "Mantiene dos objetos unidos sin movimiento."),
    C0(3, "RopeConstraint", "", false, "Cuerda que limita la separacion entre puntos."),
    C0(3, "AirController", "", false, "Gestiona el control del movimiento en el aire."),
    CP(3, "Humanoid", "", false, "Controlador de personaje con vida y movimiento.", kHumanoidProps),
    C0(3, "Seat", "", false, "Permite que un personaje se siente."),
    CP(3, "SpawnLocation", "Part", false, "Define un punto de aparicion.", kSpawnProps),
    C0(3, "Pathfinding", "", false, "Navegacion y busqueda de rutas."),

    // 4. Particulas y efectos.
    C0(4, "ParticleEmitter", "", false, "Genera particulas configurables."),
    C0(4, "Trail", "", false, "Rastro visual detras de un objeto en movimiento."),
    C0(4, "Smoke", "", false, "Genera humo."),
    C0(4, "Fire", "", false, "Efecto visual de fuego."),
    C0(4, "Sparkles", "", false, "Genera destellos."),
    C0(4, "Explosion", "", false, "Explosion visual con efectos fisicos."),
    C0(4, "Highlight", "", false, "Resalta visualmente un objeto."),
    C0(4, "Beam", "", false, "Efecto visual entre dos puntos."),

    // 5. Interaccion.
    C0(5, "ClickDetector", "", false, "Detecta clics sobre un objeto."),
    C0(5, "DragDetector", "", false, "Permite arrastrar objetos."),
    C0(5, "ProximityPrompt", "", false, "Interacciones contextuales al acercarse."),
    C0(5, "Dialog", "", false, "Dialogo interactivo."),
    C0(5, "DialogChoice", "", false, "Opcion seleccionable de un dialogo."),

    // 6. Animaciones.
    C0(6, "Animation", "", false, "Recurso de animacion."),
    C0(6, "AnimationConstraint", "", false, "Restricciones del sistema de animacion."),
    C0(6, "Bone", "", false, "Hueso de una estructura articulada."),
    C0(6, "Motor6D", "", false, "Conecta partes articuladas y controla su transformacion."),
    C0(6, "Keyframe", "", false, "Fotograma clave."),
    C0(6, "KeyframeSequence", "", false, "Secuencia de fotogramas clave."),
    C0(6, "EulerRotationCurve", "", false, "Curva de rotacion con angulos de Euler."),
    C0(6, "RotationCurve", "", false, "Curva de orientacion."),
    C0(6, "Vector3Curve", "", false, "Curva de valores vectoriales."),
    C0(6, "Animator", "", false, "Coordina la reproduccion de animaciones."),

    // 7. Audio.
    CP(7, "Sound", "", false, "Reproduce musica, voces y efectos.", kSoundProps),
    C0(7, "SoundEffect", "", true, "Clase base abstracta de efectos de audio."),
    C0(7, "SoundGroup", "", false, "Agrupa sonidos con configuracion compartida."),
    C0(7, "SoundService", "", false, "Configuracion general y reproduccion de audio."),
    C0(7, "ChorusSoundEffect", "SoundEffect", false, "Efecto de coro."),
    C0(7, "CompressorSoundEffect", "SoundEffect", false, "Comprime el rango dinamico."),
    C0(7, "DistortionSoundEffect", "SoundEffect", false, "Anade distorsion o saturacion."),
    C0(7, "EchoSoundEffect", "SoundEffect", false, "Genera repeticiones de sonido."),
    C0(7, "EqualizerSoundEffect", "SoundEffect", false, "Modifica bandas de frecuencia."),
    C0(7, "FlangeSoundEffect", "SoundEffect", false, "Efecto de modulacion tipo flanger."),
    C0(7, "PitchShiftSoundEffect", "SoundEffect", false, "Modifica el tono del sonido."),
    C0(7, "ReverbSoundEffect", "SoundEffect", false, "Simula la reverberacion de un espacio."),
    C0(7, "TremoloSoundEffect", "SoundEffect", false, "Modula periodicamente el volumen."),
    C0(7, "Wire", "", false, "Conecta componentes de una cadena de audio."),

    // 8. Controles de entrada.
    C0(8, "Mouse", "", false, "Gestiona las entradas del raton."),
    C0(8, "KeyboardControl", "", false, "Control configurable asociado a una tecla."),
    C0(8, "GamepadControl", "", false, "Control configurable de mando."),
    C0(8, "TouchControl", "", false, "Control configurable para pantallas tactiles."),

    // 9. Comunicacion entre componentes.
    C0(9, "BindableEvent", "", false, "Eventos para componentes del mismo contexto."),
    C0(9, "BindableFunction", "", false, "Llama a una funcion de otro componente."),
    C0(9, "RemoteEvent", "", false, "Eventos entre contextos separados."),
    C0(9, "RemoteFunction", "", false, "Llamadas con respuesta entre contextos separados."),

    // 10. Scripts y actualizacion.
    CG(10, "Scripts", "", false, "Arbol global de scripts del proyecto.", kSuggScripts),
    C0(10, "Script", "", false, "Codigo que se ejecuta segun su ubicacion."),
    C0(10, "LocalScript", "", false, "Codigo que se ejecuta del lado del jugador."),
    C0(10, "ModuleScript", "", false, "Codigo reutilizable que otros scripts importan."),
    C0(10, "Heartbeat", "", false, "Evento de actualizacion para los scripts."),
    C0(10, "Stepped", "", false, "Evento de la actualizacion de la simulacion."),

    // 11. Tiempo.
    C0(11, "ClockTime", "", false, "Consulta y manipula el tiempo de un ciclo temporal."),

    // 12. Gestion de recursos.
    C0(12, "Debris", "", false, "Programa la eliminacion de objetos tras un tiempo."),
    C0(12, "CacheService", "", false, "Gestiona recursos almacenados temporalmente."),
    C0(12, "AssetService", "", false, "Administra recursos y operaciones con assets."),
    C0(12, "ContentProvider", "", false, "Gestiona la precarga de recursos."),

    // 13. Diagnostico y depuracion.
    C0(13, "LogService", "", false, "Centraliza registros, avisos y mensajes."),
    C0(13, "ErrorReporter", "", false, "Recopila informacion sobre errores."),
    C0(13, "Stats", "", false, "Estadisticas de rendimiento y recursos."),
    C0(13, "DebugSettings", "", false, "Opciones de configuracion para depuracion."),

    // 14. Persistencia y guardado local.
    C0(14, "DataStore", "", false, "Almacena y recupera datos persistentes."),
    C0(14, "DataStoreService", "", false, "Administra los almacenes de datos."),
    C0(14, "JSONService", "", false, "Convierte datos a JSON y desde JSON."),
    C0(14, "SaveService", "", false, "Coordina el guardado y la carga."),

    // 15. Edicion de recursos en ejecucion.
    C0(15, "EditableImage", "", false, "Modifica imagenes durante la ejecucion."),
    C0(15, "EditableMesh", "", false, "Crea o modifica geometria durante la ejecucion."),

    // 16. Interfaz grafica (GUI).
    C0(16, "GuiObject", "", true, "Clase base abstracta de elementos visuales."),
    C0(16, "ScreenGui", "GuiObject", false, "Interfaz 2D que se muestra en pantalla."),
    C0(16, "Frame", "GuiObject", false, "Contenedor para organizar componentes."),
    C0(16, "CanvasGroup", "GuiObject", false, "Agrupa elementos con propiedades compartidas."),
    C0(16, "ScrollingFrame", "GuiObject", false, "Contenedor con desplazamiento."),
    C0(16, "ImageLabel", "GuiObject", false, "Muestra una imagen."),
    C0(16, "ImageButton", "GuiObject", false, "Muestra una imagen y permite interaccion."),
    C0(16, "TextLabel", "GuiObject", false, "Muestra texto."),
    C0(16, "TextButton", "GuiObject", false, "Boton con texto."),
    C0(16, "TextBox", "GuiObject", false, "Campo de texto editable."),
    C0(16, "ViewportFrame", "GuiObject", false, "Escena 3D dentro de la interfaz."),
    C0(16, "VideoFrame", "GuiObject", false, "Muestra video dentro de la interfaz."),
    C0(16, "BillboardGui", "GuiObject", false, "Interfaz 3D que mira hacia la camara."),
    C0(16, "SurfaceGui", "GuiObject", false, "Interfaz dibujada sobre una superficie."),
    C0(16, "UIComponent", "", true, "Clase base abstracta de componentes de interfaz."),
    C0(16, "UICorner", "UIComponent", false, "Configura las esquinas redondeadas."),
    C0(16, "UIStroke", "UIComponent", false, "Configura el contorno de un elemento."),
    C0(16, "UIGradient", "UIComponent", false, "Aplica un degradado de color."),
    C0(16, "UIShadow", "UIComponent", false, "Anade una sombra visual."),
    C0(16, "UIPadding", "UIComponent", false, "Define el espacio interior."),
    C0(16, "UIScale", "UIComponent", false, "Ajusta la escala de un elemento."),
    C0(16, "UIAspectRatioConstraint", "UIComponent", false, "Mantiene una proporcion de aspecto."),
    C0(16, "UISizeConstraint", "UIComponent", false, "Limita el tamano minimo y maximo."),
    C0(16, "UITextSizeConstraint", "UIComponent", false, "Limita el tamano del texto."),
    C0(16, "UIDragDetector", "UIComponent", false, "Permite arrastrar elementos de interfaz."),
    C0(16, "UIFlexItem", "UIComponent", false, "Comportamiento flexible en un diseno."),
    C0(16, "UILayout", "", true, "Clase base abstracta de distribucion automatica."),
    C0(16, "UIListLayout", "UILayout", false, "Organiza elementos en lista."),
    C0(16, "UIGridLayout", "UILayout", false, "Organiza elementos en cuadricula."),
    C0(16, "UITableLayout", "UILayout", false, "Organiza elementos en filas y columnas."),
    C0(16, "UIPageLayout", "UILayout", false, "Organiza elementos como paginas navegables."),
    C0(16, "UIGridStyleLayout", "UILayout", true, "Clase base abstracta de rejilla o lista."),

    // 17. Interpolacion.
    C0(17, "Tween", "", false, "Interpolacion gradual de propiedades."),
    C0(17, "TweenService", "", false, "Coordina la creacion y ejecucion de tweens."),

    // 18. Objetos de valor.
    C0(18, "StringValue", "", false, "Almacena un texto."),
    C0(18, "IntValue", "", false, "Almacena un numero entero."),
    C0(18, "NumberValue", "", false, "Almacena un numero decimal."),
    C0(18, "BoolValue", "", false, "Almacena verdadero o falso."),
    C0(18, "Vector3Value", "", false, "Almacena un vector tridimensional."),
    C0(18, "Color3Value", "", false, "Almacena un color."),
    C0(18, "ObjectValue", "", false, "Almacena una referencia a otro objeto."),
};

#undef C0
#undef CP
#undef CG
#undef CPG

constexpr int kRawClassCount =
    static_cast<int>(sizeof(kRawClasses) / sizeof(kRawClasses[0]));

bool endsWith(const char* s, const char* suffix) {
    const size_t ls = std::strlen(s);
    const size_t lf = std::strlen(suffix);
    return ls >= lf && std::strcmp(s + (ls - lf), suffix) == 0;
}

bool isSystemService(const char* name) {
    if (endsWith(name, "Service")) return true;
    return std::strcmp(name, "ErrorReporter") == 0 ||
           std::strcmp(name, "Stats") == 0 ||
           std::strcmp(name, "DebugSettings") == 0 ||
           std::strcmp(name, "Scripts") == 0;
}

bool isLightTypeName(const char* name) {
    return std::strcmp(name, "Point") == 0 ||
           std::strcmp(name, "Spot") == 0 ||
           std::strcmp(name, "Surface") == 0;
}

const PropertyDef kNameProp = {"Name", PropType::String, "", "Data",
                               "Nombre visible del objeto.", false, nullptr};
const PropertyDef kParentProp = {"Parent", PropType::RefObject, "", "Data",
                                 "Objeto que lo contiene.", false, nullptr};

} // namespace

const ClassRegistry& ClassRegistry::get() {
    static const ClassRegistry instance;
    return instance;
}

ClassRegistry::ClassRegistry() {
    for (const Category& c : kCategories) categories_.push_back(c);

    classes_.reserve(kRawClassCount);
    for (int i = 0; i < kRawClassCount; ++i) {
        ClassDef def = kRawClasses[i];
        if (!def.icon) def.icon = categories_[def.category].icon;
        def.insertable = !def.abstractCls && !isSystemService(def.name) &&
                         !isLightTypeName(def.name);
        classes_.push_back(std::move(def));
    }

    // Propiedades efectivas: Name/Parent inherentes + bases + propias.
    std::unordered_map<std::string, int> index;
    for (int i = 0; i < static_cast<int>(classes_.size()); ++i) {
        index[classes_[i].name] = i;
    }
    auto resolve = [&](auto&& self, int i) -> void {
        ClassDef& def = classes_[i];
        if (!def.effective.empty()) return; // ya resuelta (o en curso)
        std::vector<PropertyDef> eff;
        if (def.base && def.base[0] != '\0') {
            auto it = index.find(def.base);
            if (it != index.end()) {
                self(self, it->second);
                eff = classes_[it->second].effective;
            }
        }
        if (eff.empty()) { // raiz: anade Name/Parent inherentes
            eff.push_back(kNameProp);
            eff.push_back(kParentProp);
        }
        for (int p = 0; p < def.propCount; ++p) eff.push_back(def.props[p]);
        def.effective = std::move(eff);
    };
    for (int i = 0; i < static_cast<int>(classes_.size()); ++i) resolve(resolve, i);
}

const ClassDef* ClassRegistry::find(const std::string& name) const {
    for (const ClassDef& c : classes_) {
        if (name == c.name) return &c;
    }
    return nullptr;
}

int ClassRegistry::indexOf(const std::string& name) const {
    for (int i = 0; i < static_cast<int>(classes_.size()); ++i) {
        if (name == classes_[i].name) return i;
    }
    return -1;
}

bool ClassRegistry::isInsertable(const std::string& name) const {
    const ClassDef* c = find(name);
    return c && c->insertable;
}

bool ClassRegistry::isLightType(const std::string& name) const {
    return isLightTypeName(name.c_str());
}

int ClassRegistry::categoryOf(const std::string& className) const {
    const ClassDef* c = find(className);
    return c ? c->category : -1;
}

int ClassRegistry::valueCount(int classIndex) const {
    if (classIndex < 0 || classIndex >= classCount()) return 0;
    int n = 0;
    for (const PropertyDef& p : classes_[classIndex].effective) {
        if (std::strcmp(p.name, "Name") != 0 &&
            std::strcmp(p.name, "Parent") != 0) {
            ++n;
        }
    }
    return n;
}

const PropertyDef& ClassRegistry::valueProp(int classIndex,
                                            int valueIndex) const {
    int n = 0;
    for (const PropertyDef& p : classes_[classIndex].effective) {
        if (std::strcmp(p.name, "Name") == 0 ||
            std::strcmp(p.name, "Parent") == 0) {
            continue;
        }
        if (n == valueIndex) return p;
        ++n;
    }
    return classes_[classIndex].effective.front();
}

int ClassRegistry::valueIndex(int classIndex, const std::string& prop) const {
    if (classIndex < 0 || classIndex >= classCount()) return -1;
    int n = 0;
    for (const PropertyDef& p : classes_[classIndex].effective) {
        if (std::strcmp(p.name, "Name") == 0 ||
            std::strcmp(p.name, "Parent") == 0) {
            continue;
        }
        if (prop == p.name) return n;
        ++n;
    }
    return -1;
}

const PropertyDef* ClassRegistry::findProp(int classIndex,
                                           const std::string& prop) const {
    if (classIndex < 0 || classIndex >= classCount()) return nullptr;
    for (const PropertyDef& p : classes_[classIndex].effective) {
        if (prop == p.name) return &p;
    }
    return nullptr;
}

} // namespace sk
