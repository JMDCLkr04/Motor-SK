// GENERADO por tools/make_icons.py. NO EDITAR A MANO.
// Iconos de Lucide (ISC); atlas embebido como RCDATA.
#pragma once

namespace sk {
namespace ui {

// Identificador de icono (indice de celda en el atlas).
enum class IconId : int {
    CatOrganization,
    CatLighting,
    CatLightSources,
    CatPhysics,
    CatParticles,
    CatInteraction,
    CatAnimations,
    CatAudio,
    CatInput,
    CatCommunication,
    CatScripts,
    CatTime,
    CatResources,
    CatDiagnostics,
    CatPersistence,
    CatRuntime,
    CatGui,
    CatInterpolation,
    CatValues,
    Part,
    Model,
    Folder,
    Workspace,
    Camera,
    Lighting,
    Script,
    LocalScript,
    ModuleScript,
    Light,
    Sound,
    Dimension,
    Attachment,
    Decal,
    ParticleEmitter,
    UiBook,
    UiPlus,
    UiPlay,
    UiStop,
    Count,
};

inline constexpr int kIconCount = static_cast<int>(IconId::Count);

// Escalas (px de celda) presentes en el atlas, banda a banda.
inline constexpr int kIconScales[] = {16, 20, 24, 32};
inline constexpr int kIconScaleCount = 4;

// Nombre del SVG de Lucide de cada icono (para regenerar).
inline constexpr const char* kIconNames[] = {
    "folder",
    "sun",
    "lightbulb",
    "cog",
    "sparkles",
    "mouse-pointer",
    "clapperboard",
    "volume-2",
    "gamepad-2",
    "radio",
    "file-code",
    "alarm-clock",
    "package",
    "bug",
    "database",
    "wrench",
    "image",
    "activity",
    "hash",
    "box",
    "boxes",
    "folder",
    "layout-dashboard",
    "camera",
    "sun-medium",
    "file-code",
    "file-terminal",
    "file-cog",
    "lightbulb",
    "volume-2",
    "layers",
    "paperclip",
    "image",
    "sparkles",
    "book-open",
    "plus",
    "play",
    "square",
};

// Id del recurso RCDATA del atlas (ver app.rc).
inline constexpr int kIconAtlasResId = 101;

} // namespace ui
} // namespace sk
