cmake_minimum_required(VERSION 3.20)

project(MotorSK
    VERSION 0.1.0
    DESCRIPTION "Motor de juegos 3D en Vulkan"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Runtime estatico (/MT, /MTd en Debug): el .exe no depende de las DLL
# de Visual C++ (vcruntime140.dll, msvcp140.dll), asi que arranca en
# equipos sin el redistributable. Debe fijarse antes de crear el target.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Recursos de Windows (app.rc): icono del .exe. Solo con MSVC, que trae
# rc.exe; CMake compila los .rc listados en las fuentes del target.
if(MSVC)
    enable_language(RC)
endif()

# El Vulkan SDK de LunarG define VULKAN_SDK al instalar; si la sesion no la
# tiene (terminales nuevas), se usa la ruta por defecto como hint.
if(NOT DEFINED ENV{VULKAN_SDK} AND EXISTS "C:/VulkanSDK")
    set(ENV{VULKAN_SDK} "C:/VulkanSDK")
endif()

find_package(Vulkan REQUIRED)

# --- Modelo de datos (Instance/World/commands/ClassRegistry): se comparte
# entre el ejecutable y los tests.
add_library(motorsk_model STATIC
    src/scene/shape.cpp
    src/scene/class_registry.cpp
    src/scene/instance.cpp
    src/scene/collection_service.cpp
    src/scene/commands.cpp
    src/scene/world.cpp
)
target_include_directories(motorsk_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(motorsk_model PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(motorsk_model PRIVATE /W4)
endif()

add_executable(${PROJECT_NAME}
    app.rc
    src/main.cpp
    src/core/config.cpp
    src/platform/window.cpp
    src/project/project.cpp
    src/project/scripts.cpp
    src/scene/class_catalog.cpp
    src/scene/picking.cpp
    src/scene/gizmo.cpp
    src/scene/snapping.cpp
    src/ui/code_view.cpp
    src/render/renderer.cpp
    src/render/meshes.cpp
    src/ui/projects_panel.cpp
    src/ui/dark_button.cpp
    src/ui/prompt.cpp
    src/ui/script_picker.cpp
    src/ui/place_view.cpp
    src/ui/class_picker.cpp
    src/ui/play_window.cpp
    src/ui/workspace_tabs.cpp
    src/ui/docs_view.cpp
    src/ui/kit/paint_buffer.cpp
    src/ui/kit/place_popup.cpp
    src/ui/kit/icon_atlas.cpp
    src/ui/kit/scroll_bar.cpp
    src/ui/kit/disclosure.cpp
    src/ui/kit/tab_strip.cpp
    src/ui/kit/tree_widget.cpp
    src/ui/kit/list_widget.cpp
    src/ui/kit/demo_window.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE motorsk_model)

# Para que rc.exe encuentre logo.ico junto al app.rc.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)

# comctl32: ListView; shell32: SHGetFolderPathA (Documentos);
# gdi32: pintado de botones; dwmapi: barra de titulo oscura.
target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan comctl32 shell32 gdi32 dwmapi shlwapi windowscodecs msimg32)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    # Release: optimizacion plena (/O2) + optimizacion global (/GL) con
    # LTCG en el enlace. Sin /arch:AVX (los equipos de 2011 no lo tienen;
    # el default SSE2 funciona en todas las CPUs x64).
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/O2 /GL>)
    # Aplicacion de ventana: sin consola cmd detras (los logs salen solo
    # si se redirige la salida: MotorSK.exe > log.txt).
    target_link_options(${PROJECT_NAME} PRIVATE
        /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
        $<$<CONFIG:Release>:/LTCG>)
endif()

# --- Tests del modelo (asserts, sin framework) --------------------------
option(MOTORSK_BUILD_TESTS "Construir los tests del modelo" ON)
if(MOTORSK_BUILD_TESTS)
    enable_testing()
    add_executable(motorsk_tests tests/test_model.cpp)
    target_link_libraries(motorsk_tests PRIVATE motorsk_model)
    set_target_properties(motorsk_tests PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(motorsk_tests PRIVATE /W4)
    endif()
    add_test(NAME modelo COMMAND motorsk_tests)
endif()

# --- Shaders: glslc (del Vulkan SDK) compila GLSL a SPIR-V en cada build ---
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin")
if(NOT GLSLC)
    message(FATAL_ERROR "glslc no encontrado; instala el Vulkan SDK o define VULKAN_SDK")
endif()

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

add_custom_command(
    OUTPUT
        ${SHADER_BIN_DIR}/grid.spv
        ${SHADER_BIN_DIR}/grid.frag.spv
    COMMAND ${GLSLC} -O ${SHADER_SRC_DIR}/grid.vert -o ${SHADER_BIN_DIR}/grid.spv
    COMMAND ${GLSLC} -O ${SHADER_SRC_DIR}/grid.frag -o ${SHADER_BIN_DIR}/grid.frag.spv
    DEPENDS ${SHADER_SRC_DIR}/grid.vert ${SHADER_SRC_DIR}/grid.frag
    COMMENT "Compilando shaders SPIR-V"
)

add_custom_target(compile_shaders
    DEPENDS ${SHADER_BIN_DIR}/grid.spv ${SHADER_BIN_DIR}/grid.frag.spv
)
add_dependencies(${PROJECT_NAME} compile_shaders)

# Los shaders se cargan junto al .exe en tiempo de ejecucion.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SHADER_BIN_DIR}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
    COMMENT "Copiando shaders al directorio del ejecutable"
)
