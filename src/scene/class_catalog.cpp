#include "class_catalog.h"

#include <cstring>

namespace sk {
namespace catalog {

namespace {

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

constexpr int kCategoryCount =
    static_cast<int>(sizeof(kCategories) / sizeof(kCategories[0]));

// --- Propiedades por defecto de algunas clases ---------------------------
// El documento docs/clases.md detalla el resto; aqui van las que ya tienen
// una ficha completa para la tabla de la Referencia de clases.

const PropDef kPartProps[] = {
    {"Name", "text", "Part", "Nombre visible del objeto."},
    {"Parent", "object", "Workspace", "Objeto que lo contiene."},
    {"Position", "Vector3", "0, 0, 0", "Posicion en el mundo (unidad)."},
    {"Size", "Vector3", "4, 1.2, 2", "Tamano en los tres ejes."},
    {"Orientation", "Vector3", "0, 0, 0", "Rotacion en grados (X/Y/Z)."},
    {"Shape", "enum", "Cube", "Forma geometrica del objeto."},
    {"Anchored", "bool", "false", "No le afecta la fisica."},
    {"CanCollide", "bool", "true", "Colisiona con otros objetos."},
    {"CastShadow", "bool", "true", "Proyecta sombra."},
    {"Locked", "bool", "false", "No se puede mover en el editor."},
    {"Reflectance", "number", "0", "Reflejo de la superficie (0 a 1)."},
    {"Transparency", "number", "0", "Opacidad inversa (0 a 1)."},
};

const PropDef kModelProps[] = {
    {"Name", "text", "Model", "Nombre del grupo."},
    {"Parent", "object", "Workspace", "Objeto que lo contiene."},
    {"PrimaryPart", "object", "", "Parte principal del modelo."},
};

const PropDef kFolderProps[] = {
    {"Name", "text", "Folder", "Nombre de la carpeta."},
    {"Parent", "object", "Workspace", "Objeto que la contiene."},
};

const PropDef kWorkspaceProps[] = {
    {"Name", "text", "Workspace", "Nombre del contenedor."},
    {"Gravity", "number", "196.2", "Aceleracion de la gravedad."},
};

const PropDef kCameraProps[] = {
    {"Name", "text", "Camera", "Nombre de la camara."},
    {"FieldOfView", "number", "70", "Campo de vision en grados."},
    {"Position", "Vector3", "0, 20, 0", "Posicion de la camara."},
};

const PropDef kLightingProps[] = {
    {"Name", "text", "Lighting", "Nombre del contenedor."},
    {"Ambient", "color", "0.1, 0.1, 0.1", "Luz ambiente."},
    {"Brightness", "number", "2", "Brillo general."},
    {"ClockTime", "number", "14", "Hora del dia (0 a 24)."},
};

const PropDef kSkyProps[] = {
    {"Name", "text", "Sky", "Nombre del cielo."},
    {"StarCount", "number", "3000", "Numero de estrellas."},
};

const PropDef kAtmosphereProps[] = {
    {"Name", "text", "Atmosphere", "Nombre de la atmosfera."},
    {"Density", "number", "0.3", "Densidad de la niebla."},
    {"Haze", "number", "1.5", "Brillo del horizonte."},
};

const PropDef kLightProps[] = {
    {"Name", "text", "Light", "Nombre de la fuente de luz."},
    {"Type", "enum", "Point", "Point, Spot o Surface."},
    {"Brightness", "number", "1", "Intensidad de la luz."},
    {"Range", "number", "8", "Alcance de la luz."},
    {"Color", "color", "1, 1, 1", "Color emitido."},
};

const PropDef kHumanoidProps[] = {
    {"Name", "text", "Humanoid", "Nombre del controlador."},
    {"Health", "number", "100", "Vida actual."},
    {"MaxHealth", "number", "100", "Vida maxima."},
    {"WalkSpeed", "number", "16", "Velocidad al caminar."},
    {"JumpPower", "number", "50", "Fuerza del salto."},
};

const PropDef kSpawnProps[] = {
    {"Name", "text", "SpawnLocation", "Nombre del punto de aparicion."},
    {"Duration", "number", "0", "Tiempo de reaparicion."},
    {"Neutral", "bool", "false", "Sirve a cualquier equipo."},
};

const PropDef kSoundProps[] = {
    {"Name", "text", "Sound", "Nombre del sonido."},
    {"Volume", "number", "0.5", "Volumen (0 a 10)."},
    {"Looped", "bool", "false", "Se repite."},
    {"Playing", "bool", "false", "Esta sonando."},
};

#define C(cat, nm, base, abs, desc) \
    {cat, nm, base, abs, desc, desc, nullptr, 0}
#define CP(cat, nm, base, abs, desc, tab) \
    {cat, nm, base, abs, desc, desc, tab, \
     static_cast<int>(sizeof(tab) / sizeof(tab[0]))}

const ClassDef kClasses[] = {
    // 0. Proyecto y organizacion.
    C(0, "Dimension", "", false, "Dimension independiente con sus objetos, config y scripts."),
    C(0, "Workspace", "", false, "Contiene los objetos del mundo de una dimension."),
    CP(0, "Part", "", false, "Objeto 3D basico con forma, transformacion y colision.", kPartProps),
    CP(0, "Model", "", false, "Agrupa objetos para manipularlos como una unidad.", kModelProps),
    CP(0, "Folder", "", false, "Organiza objetos jerarquicamente.", kFolderProps),
    C(0, "MeshPart", "Part", false, "Geometria basada en una malla 3D."),
    C(0, "Decal", "", false, "Aplica una imagen sobre una superficie."),
    C(0, "Texture", "", false, "Repite una imagen en mosaico sobre una superficie."),
    C(0, "Color", "", false, "Color aplicable a un objeto o a parte de su geometria."),
    C(0, "Accessory", "", false, "Accesorio configurable."),
    C(0, "Attachment", "", false, "Punto de referencia para uniones y efectos."),
    CP(0, "Camera", "", false, "Vista de juego de una dimension.", kCameraProps),
    C(0, "CollectionService", "", false, "Busca objetos por etiqueta y avisa de cambios."),

    // 1. Iluminacion y ambiente.
    CP(1, "Lighting", "", false, "Configura la iluminacion y el ambiente de una dimension.", kLightingProps),
    CP(1, "Sky", "", false, "Define la apariencia del cielo.", kSkyProps),
    CP(1, "Atmosphere", "", false, "Dispersion atmosferica y niebla.", kAtmosphereProps),
    C(1, "Clouds", "", false, "Representa y configura las nubes."),
    C(1, "BloomEffect", "PostEffect", false, "Resplandor alrededor de zonas luminosas."),
    C(1, "BlurEffect", "PostEffect", false, "Aplica desenfoque."),
    C(1, "ColorCorrectionEffect", "PostEffect", false, "Modifica color, contraste y saturacion."),
    C(1, "ColorGradingEffect", "PostEffect", false, "Aplica gradacion de color."),
    C(1, "DepthOfFieldEffect", "PostEffect", false, "Desenfoca segun la distancia a la camara."),
    C(1, "SunRaysEffect", "PostEffect", false, "Simula rayos solares."),

    // 2. Fuentes de luz.
    CP(2, "Light", "", false, "Fuente de luz; Type: Point, Spot o Surface.", kLightProps),

    // 3. Fisica, movimiento y articulaciones.
    C(3, "AlignOrientation", "", false, "Controla o alinea la orientacion de un objeto."),
    C(3, "AngularVelocity", "", false, "Controla la velocidad angular."),
    C(3, "LinearVelocity", "", false, "Controla la velocidad lineal."),
    C(3, "VectorForce", "", false, "Aplica una fuerza vectorial."),
    C(3, "BallSocketConstraint", "", false, "Rotacion en varias direcciones entre cuerpos."),
    C(3, "HingeConstraint", "", false, "Permite rotacion alrededor de un eje."),
    C(3, "NoCollisionConstraint", "", false, "Evita la colision entre dos objetos."),
    C(3, "Weld", "", false, "Conecta rigidamente dos objetos."),
    C(3, "WeldConstraint", "", false, "Mantiene dos objetos unidos sin movimiento."),
    C(3, "RopeConstraint", "", false, "Cuerda que limita la separacion entre puntos."),
    C(3, "AirController", "", false, "Gestiona el control del movimiento en el aire."),
    CP(3, "Humanoid", "", false, "Controlador de personaje con vida y movimiento.", kHumanoidProps),
    C(3, "Seat", "", false, "Permite que un personaje se siente."),
    CP(3, "SpawnLocation", "Part", false, "Define un punto de aparicion.", kSpawnProps),
    C(3, "Pathfinding", "", false, "Navegacion y busqueda de rutas."),

    // 4. Particulas y efectos.
    C(4, "ParticleEmitter", "", false, "Genera particulas configurables."),
    C(4, "Trail", "", false, "Rastro visual detras de un objeto en movimiento."),
    C(4, "Smoke", "", false, "Genera humo."),
    C(4, "Fire", "", false, "Efecto visual de fuego."),
    C(4, "Sparkles", "", false, "Genera destellos."),
    C(4, "Explosion", "", false, "Explosion visual con efectos fisicos."),
    C(4, "Highlight", "", false, "Resalta visualmente un objeto."),
    C(4, "Beam", "", false, "Efecto visual entre dos puntos."),

    // 5. Interaccion.
    C(5, "ClickDetector", "", false, "Detecta clics sobre un objeto."),
    C(5, "DragDetector", "", false, "Permite arrastrar objetos."),
    C(5, "ProximityPrompt", "", false, "Interacciones contextuales al acercarse."),
    C(5, "Dialog", "", false, "Dialogo interactivo."),
    C(5, "DialogChoice", "", false, "Opcion seleccionable de un dialogo."),

    // 6. Animaciones.
    C(6, "Animation", "", false, "Recurso de animacion."),
    C(6, "AnimationConstraint", "", false, "Restricciones del sistema de animacion."),
    C(6, "Bone", "", false, "Hueso de una estructura articulada."),
    C(6, "Motor6D", "", false, "Conecta partes articuladas y controla su transformacion."),
    C(6, "Keyframe", "", false, "Fotograma clave."),
    C(6, "KeyframeSequence", "", false, "Secuencia de fotogramas clave."),
    C(6, "EulerRotationCurve", "", false, "Curva de rotacion con angulos de Euler."),
    C(6, "RotationCurve", "", false, "Curva de orientacion."),
    C(6, "Vector3Curve", "", false, "Curva de valores vectoriales."),
    C(6, "Animator", "", false, "Coordina la reproduccion de animaciones."),

    // 7. Audio.
    CP(7, "Sound", "", false, "Reproduce musica, voces y efectos.", kSoundProps),
    C(7, "SoundEffect", "", true, "Clase base abstracta de efectos de audio."),
    C(7, "SoundGroup", "", false, "Agrupa sonidos con configuracion compartida."),
    C(7, "SoundService", "", false, "Configuracion general y reproduccion de audio."),
    C(7, "ChorusSoundEffect", "SoundEffect", false, "Efecto de coro."),
    C(7, "CompressorSoundEffect", "SoundEffect", false, "Comprime el rango dinamico."),
    C(7, "DistortionSoundEffect", "SoundEffect", false, "Anade distorsion o saturacion."),
    C(7, "EchoSoundEffect", "SoundEffect", false, "Genera repeticiones de sonido."),
    C(7, "EqualizerSoundEffect", "SoundEffect", false, "Modifica bandas de frecuencia."),
    C(7, "FlangeSoundEffect", "SoundEffect", false, "Efecto de modulacion tipo flanger."),
    C(7, "PitchShiftSoundEffect", "SoundEffect", false, "Modifica el tono del sonido."),
    C(7, "ReverbSoundEffect", "SoundEffect", false, "Simula la reverberacion de un espacio."),
    C(7, "TremoloSoundEffect", "SoundEffect", false, "Modula periodicamente el volumen."),
    C(7, "Wire", "", false, "Conecta componentes de una cadena de audio."),

    // 8. Controles de entrada.
    C(8, "Mouse", "", false, "Gestiona las entradas del raton."),
    C(8, "KeyboardControl", "", false, "Control configurable asociado a una tecla."),
    C(8, "GamepadControl", "", false, "Control configurable de mando."),
    C(8, "TouchControl", "", false, "Control configurable para pantallas tactiles."),

    // 9. Comunicacion entre componentes.
    C(9, "BindableEvent", "", false, "Eventos para componentes del mismo contexto."),
    C(9, "BindableFunction", "", false, "Llama a una funcion de otro componente."),
    C(9, "RemoteEvent", "", false, "Eventos entre contextos separados."),
    C(9, "RemoteFunction", "", false, "Llamadas con respuesta entre contextos separados."),

    // 10. Scripts y actualizacion.
    C(10, "Script", "", false, "Codigo que se ejecuta segun su ubicacion."),
    C(10, "LocalScript", "", false, "Codigo que se ejecuta del lado del jugador."),
    C(10, "ModuleScript", "", false, "Codigo reutilizable que otros scripts importan."),
    C(10, "Heartbeat", "", false, "Evento de actualizacion para los scripts."),
    C(10, "Stepped", "", false, "Evento de la actualizacion de la simulacion."),

    // 11. Tiempo.
    C(11, "ClockTime", "", false, "Consulta y manipula el tiempo de un ciclo temporal."),

    // 12. Gestion de recursos.
    C(12, "Debris", "", false, "Programa la eliminacion de objetos tras un tiempo."),
    C(12, "CacheService", "", false, "Gestiona recursos almacenados temporalmente."),
    C(12, "AssetService", "", false, "Administra recursos y operaciones con assets."),
    C(12, "ContentProvider", "", false, "Gestiona la precarga de recursos."),

    // 13. Diagnostico y depuracion.
    C(13, "LogService", "", false, "Centraliza registros, avisos y mensajes."),
    C(13, "ErrorReporter", "", false, "Recopila informacion sobre errores."),
    C(13, "Stats", "", false, "Estadisticas de rendimiento y recursos."),
    C(13, "DebugSettings", "", false, "Opciones de configuracion para depuracion."),

    // 14. Persistencia y guardado local.
    C(14, "DataStore", "", false, "Almacena y recupera datos persistentes."),
    C(14, "DataStoreService", "", false, "Administra los almacenes de datos."),
    C(14, "JSONService", "", false, "Convierte datos a JSON y desde JSON."),
    C(14, "SaveService", "", false, "Coordina el guardado y la carga."),

    // 15. Edicion de recursos en ejecucion.
    C(15, "EditableImage", "", false, "Modifica imagenes durante la ejecucion."),
    C(15, "EditableMesh", "", false, "Crea o modifica geometria durante la ejecucion."),

    // 16. Interfaz grafica (GUI).
    C(16, "GuiObject", "", true, "Clase base abstracta de elementos visuales."),
    C(16, "ScreenGui", "GuiObject", false, "Interfaz 2D que se muestra en pantalla."),
    C(16, "Frame", "GuiObject", false, "Contenedor para organizar componentes."),
    C(16, "CanvasGroup", "GuiObject", false, "Agrupa elementos con propiedades compartidas."),
    C(16, "ScrollingFrame", "GuiObject", false, "Contenedor con desplazamiento."),
    C(16, "ImageLabel", "GuiObject", false, "Muestra una imagen."),
    C(16, "ImageButton", "GuiObject", false, "Muestra una imagen y permite interaccion."),
    C(16, "TextLabel", "GuiObject", false, "Muestra texto."),
    C(16, "TextButton", "GuiObject", false, "Boton con texto."),
    C(16, "TextBox", "GuiObject", false, "Campo de texto editable."),
    C(16, "ViewportFrame", "GuiObject", false, "Escena 3D dentro de la interfaz."),
    C(16, "VideoFrame", "GuiObject", false, "Muestra video dentro de la interfaz."),
    C(16, "BillboardGui", "GuiObject", false, "Interfaz 3D que mira hacia la camara."),
    C(16, "SurfaceGui", "GuiObject", false, "Interfaz dibujada sobre una superficie."),
    C(16, "UIComponent", "", true, "Clase base abstracta de componentes de interfaz."),
    C(16, "UICorner", "UIComponent", false, "Configura las esquinas redondeadas."),
    C(16, "UIStroke", "UIComponent", false, "Configura el contorno de un elemento."),
    C(16, "UIGradient", "UIComponent", false, "Aplica un degradado de color."),
    C(16, "UIShadow", "UIComponent", false, "Anade una sombra visual."),
    C(16, "UIPadding", "UIComponent", false, "Define el espacio interior."),
    C(16, "UIScale", "UIComponent", false, "Ajusta la escala de un elemento."),
    C(16, "UIAspectRatioConstraint", "UIComponent", false, "Mantiene una proporcion de aspecto."),
    C(16, "UISizeConstraint", "UIComponent", false, "Limita el tamano minimo y maximo."),
    C(16, "UITextSizeConstraint", "UIComponent", false, "Limita el tamano del texto."),
    C(16, "UIDragDetector", "UIComponent", false, "Permite arrastrar elementos de interfaz."),
    C(16, "UIFlexItem", "UIComponent", false, "Comportamiento flexible en un diseno."),
    C(16, "UILayout", "", true, "Clase base abstracta de distribucion automatica."),
    C(16, "UIListLayout", "UILayout", false, "Organiza elementos en lista."),
    C(16, "UIGridLayout", "UILayout", false, "Organiza elementos en cuadricula."),
    C(16, "UITableLayout", "UILayout", false, "Organiza elementos en filas y columnas."),
    C(16, "UIPageLayout", "UILayout", false, "Organiza elementos como paginas navegables."),
    C(16, "UIGridStyleLayout", "UILayout", true, "Clase base abstracta de rejilla o lista."),

    // 17. Interpolacion.
    C(17, "Tween", "", false, "Interpolacion gradual de propiedades."),
    C(17, "TweenService", "", false, "Coordina la creacion y ejecucion de tweens."),

    // 18. Objetos de valor.
    C(18, "StringValue", "", false, "Almacena un texto."),
    C(18, "IntValue", "", false, "Almacena un numero entero."),
    C(18, "NumberValue", "", false, "Almacena un numero decimal."),
    C(18, "BoolValue", "", false, "Almacena verdadero o falso."),
    C(18, "Vector3Value", "", false, "Almacena un vector tridimensional."),
    C(18, "Color3Value", "", false, "Almacena un color."),
    C(18, "ObjectValue", "", false, "Almacena una referencia a otro objeto."),
};

#undef C
#undef CP

constexpr int kClassCount =
    static_cast<int>(sizeof(kClasses) / sizeof(kClasses[0]));

bool endsWith(const char* s, const char* suffix) {
    const size_t ls = std::strlen(s);
    const size_t lf = std::strlen(suffix);
    return ls >= lf && std::strcmp(s + (ls - lf), suffix) == 0;
}

} // namespace

int categoryCount() { return kCategoryCount; }
const Category* categories() { return kCategories; }

int classCount() { return kClassCount; }
const ClassDef* classes() { return kClasses; }

const ClassDef* findClass(const std::string& name) {
    for (int i = 0; i < kClassCount; ++i) {
        if (name == kClasses[i].name) return &kClasses[i];
    }
    return nullptr;
}

int categoryOf(const std::string& className) {
    const ClassDef* c = findClass(className);
    return c ? c->cat : -1;
}

bool insertable(const ClassDef& c) {
    if (c.abstractCls) return false;
    // Servicios y utilidades del sistema no se insertan a mano.
    if (endsWith(c.name, "Service")) return false;
    if (std::strcmp(c.name, "ErrorReporter") == 0) return false;
    if (std::strcmp(c.name, "Stats") == 0) return false;
    if (std::strcmp(c.name, "DebugSettings") == 0) return false;
    return true;
}

bool isLightType(const std::string& className) {
    return className == "Point" || className == "Spot" ||
           className == "Surface";
}

} // namespace catalog
} // namespace sk
