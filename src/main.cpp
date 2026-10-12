// Punto de entrada del editor. Toda la aplicacion (ventana, renderer,
// World, paneles, controladores y bucle principal) vive en EditorApp; aqui
// solo se construye y se corre.
#include "editor/editor_app.h"

int main(int argc, char** argv) {
    sk::editor::EditorApp app;
    return app.run(argc, argv);
}
