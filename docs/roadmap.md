# Roadmap

## Fase 0 — Setup ✅

- [x] Compilador MSVC 19.44 + CMake 4.4.4
- [x] Vulkan SDK 1.4.363.0
- [x] ICD de software (lavapipe) para poder ejecutar sin GPU Vulkan
- [x] Repositorio con build base

## Fase 1 — Ventana y cámara (en curso)

Ejecutable `.exe` con:

- [x] Ventana Win32 con swapchain de Vulkan (resize incluido)
- [x] Panel de proyectos (Win32 ListView): listar, crear, abrir, renombrar,
      borrar y lista de recientes. Raíz fija: `Documentos\Motor SK\Proyectos`
- [x] Interfaz modo oscuro: fondo negro, texto blanco, botones redondeados
      (owner-draw), lista y cabeceras oscuras, barra de título oscura
- [x] Aplicación de ventana sin consola (errores fatales en MessageBox)
- [x] Tabla con márgenes, esquinas redondeadas y marco; tipografía Segoe UI
      (12px, cabeceras semibold)
- [x] Abrir un proyecto → vista 3D (Esc vuelve al panel)
- [x] Icono del programa (logo.ico embebido via app.rc): ventana, barra de
       tareas y Alt+Tab
- [x] Banda superior: fila de atajos estilo Roblox con el boton "Part" (anade
        un objeto 3D a la escena) y navbar compacta con
        PLACE / CODE / GUI (botones redondeados, oscuro, texto blanco);
        sin area vacia, la vista 3D ocupa el resto; la banda completa son 116px
        al incluir la fila de menus (ver "Barra de menus" mas abajo)
- [x] Swapchain se recrea en cada resize (WM_SIZE avisa al renderer; sin
       depender de OUT_OF_DATE, que lavapipe no devuelve)
- [x] Division PLACE: paneles Properties (Transform del objeto seleccionado)
       a la izquierda y Explorer (arbol con raiz unica `dimension01` y los
       objetos Part insertados) a la derecha; visibles solo en PLACE,
       ocultos en CODE/GUI
- [x] Division CODE: organizador de scripts (Server/Shared/Player/Character),
       carpetas anidadas, crear script/carpeta, renombrar, borrar con
       confirmacion, raices protegidas, Properties con Nombre/Tipo/Ubicacion
       a la izquierda y Scripts a la derecha; visibles solo en CODE
- [x] Objetos 3D "Part": modelo de escena `World`/`Instance` (id estable,
       jerarquia por id, posicion/rotacion/escala), el boton Part inserta un
       cubo sobre la rejilla, Explorer anade y selecciona el nodo, Properties
       refresca el Transform
- [x] ModelScript: las escenas guardan la asociacion `script` de cada objeto,
        CODE muestra el objeto asociado en su Properties, borrar/renombrar
        scripts remapea (y guarda) las asociaciones y todo persiste con el
        arranque; la edicion desde PLACE se retiro por ahora (fila "Parent"
        solo lectura, sin boton "Cambiar...")
- [x] Seleccion en el viewport de PLACE: click izquierdo selecciona el objeto
       bajo el puntero (rayo inverso contra la caja de cada Part), Ctrl+click
       anade o quita de la seleccion, click en vacio deselecciona todo y
       arrastrar un rectangulo (marquee) selecciona varios objetos a la vez;
       el Explorer marca el objeto activo, Properties pasa a
       "-N objetos seleccionados-" con varios, y los objetos elegidos se
        rodean de un contorno celeste (mas el rectangulo de arrastre mientras
        dura el marquee)
- [x] Barra de herramientas: tarjetas compactas de 56x52 con el icono arriba
        y el nombre en ingles abajo (Select, Move, Scale, Rotate, Part) en la
        banda superior y teclas 1-4; la herramienta activa se registra en el
        log
- [x] Gizmo 3D en el viewport para la seleccion: tres asas de mundo en
        X/Y/Z coloreadas y reordenadas por herramienta (mover desplaza,
        escalar escala, rotar rota), asas en AMBAS direcciones de cada eje
        (mangos + con flecha y mangos - a la inversa, cuadraditos en
        escalar), al escalar la cara opuesta al mango elegido queda anclada
        en su sitio, hover con tinte claro y cursores distintos, arrastre
        que aplica el mismo delta a todos los objetos seleccionados y
        escribe el Transform y la escena al soltar; ESC cancela el arrastre
- [x] Foco del teclado: al escribir en Properties las teclas de camara y
        herramientas quedan bloqueadas, y un click en la vista las libera
        (clase EDIT detectada sin distinguir mayusculas)
- [x] Properties Transform editable: los nueve campos de posicion, rotacion y
        escala se editan tecleando (acepta coma decimal), el cambio se aplica
        al perder el foco, se guarda la escena y los campos se reescriben
        normalizados; si el valor no es valido o ya no hay seleccion se
        restaura el anterior
- [x] Properties en cascada: "Transform" se pliega con su triangulito y
        dentro Position/Rotation/Scale muestran un resumen "x, y, z"
        editable (separador coma, punto decimal) y los tres ejes X/Y/Z al
        desplegarlas; cada cascada tiene su triangulo y arranca con
        Transform desplegado y Position/Rotation/Scale plegadas
- [x] Proyectos nuevos arrancan con un `basepart` (losa de 20x1x20 en el
        suelo: escala `20 1 20`, posicion `0 -0.5 0`) en vez de la escena
        vacia
- [x] Rectangulo de arrastre del marquee alineado con el puntero (origen y
        tamano corregidos en el render, clamado al viewport)
- [x] Botones de herramientas con el texto pintado una sola vez
- [x] Abrir/importar un archivo de escena `.scene`: menú Archivo > Importar /
        Importar como (con prefijo de nombre), incorpora los objetos a la
        escena actual
- [x] Cámara libre: WASD para moverse, click derecho para mirar, scroll para
       acercar/alejar
- [x] Rejilla de referencia en el suelo (para percibir el movimiento)
- [x] Arrastre del cuerpo de un objeto: pulsar sobre un Part y arrastrarlo
        solo lo mueve mientras el puntero siga sobre OTRO objeto de la
        escena (en el vacio se congela), con apilado tipo Roblox: el objeto
        arranca SOBRE la cara superior del destino, centrado en el punto de
        golpe y limitado a su huella; con log del arrastre, cancelacion
        con ESC durante el movimiento y toggle con Ctrl; el marquee solo
        arranca desde el vacio
- [x] Mediciones de la banda: tarjetas de herramientas de 56x52 con el
        icono al 133%, triangulos de cascada agrandados (zona de click amplia)
        y fuente de la interfaz a 12px (banda superior de 116px)
- [x] Properties en orden Nombre (EDIT editable: renombra el objeto en la
        escena y el Explorer, guarda y refresca; raiz `dimension01`
        bloqueada) -> Parent (solo lectura, muestra el padre real; la raiz
        `dimension01` si el objeto no esta anidado, valor atenuado en los
        campos estaticos) -> Transform
- [x] Deshacer/rehacer (Ctrl+Z para deshacer, Ctrl+Y o Ctrl+Shift+Z para
        rehacer): snapshot de la escena (objetos + seleccion) antes de
        cada mutacion de PLACE (anadir Part, campos de Properties,
        renombrar, borrar, cortar/pegar, clonar, rotar y los arrastres de
        objeto y de gizmo), pila limitada a 50 pasos y historia nueva por
        proyecto; deshacer/rehacer reconstruye el Explorer y guarda
- [x] Renombrar con F12: dialogo de texto sobre la parte activa, con la
        misma validacion que el campo Nombre de Properties (no vacio, no
        duplicado)
- [x] Foco de camara (F): enfoca la seleccion (centro y tamano de todo el
        grupo) reorientando la vista y ajustando la distancia
- [x] Barra de menus: boton "Archivo" desplegable en la banda (Cerrar,
        Guardar, Guardar como, Importar .scene, Importar como, Salir;
        Configuracion del editor / Personalizar atajos / Abrir guardados
        automaticos como dialogos informativos); la banda pasa a 116px con
        la fila de menus encima de la barra de herramientas
- [x] Explorer jerarquico: triangulitos de cascada propios (mismo estilo que
        Properties) para plegar/desplegar los objetos anidados, y arrastrar
        un objeto sobre otro lo anida como hijo (soltarlo sobre `dimension01`
        lo devuelve a la raiz); la escena persiste `parent:` de cada objeto y
        borrar/cortar/renombrar/pegar/clonar/importar/deshacer respetan la
        jerarquia (sin ciclos; el render todavia usa posiciones absolutas)
- [x] Catalogo unico de clases (`scene/class_catalog`): 19 categorias y ~145
        clases con descripcion, propiedades y valores por defecto; fuente
        unica de la Referencia de clases, las Properties, el buscador del `+`
        y los iconos
- [x] Referencia de clases (F1): lista owner-draw con icono + nombre +
        descripcion y tabla de propiedades por defecto tomada del catalogo
- [x] Boton `+` del Explorer: selector emergente con buscador que inserta
        cualquier clase insertable del catalogo bajo el nodo elegido
- [x] Pestañas de panel: Properties | Output (log del motor) a la izquierda
        y Explorer | Assets (biblioteca del proyecto) a la derecha
- [x] Attributes y Tags en Properties (multilinea `nombre|tipo|valor` y una
        etiqueta por linea), persistidos en la escena
- [x] Atajos de edicion ampliados: Ctrl+G agrupar, Ctrl+U desagrupar,
        Ctrl+L bloquear, Alt+A anclar y F2 renombrar
- [x] Botones Play/Run/Stop: abren una ventana aparte con su propio
        renderer Vulkan que dibuja la escena con la camara del editor; Play
        simula y Run ejecutara los ModelScripts cuando exista el interprete;
        Stop (o cerrar la ventana) la cierra sin afectar al editor
- [x] Rendimiento: bucle de dibujo **bajo demanda** (solo `drawFrame` cuando
        cambia algo), espera en `MsgWaitForMultipleObjects` (0 % CPU en
        reposo), limite 60/30 FPS segun dispositivo (software), caches de
        `models`/contornos/gizmo por version de contenido, mapeo persistente
        del buffer del gizmo, guardado de escena **diferido (500 ms) y
        atomico** (temporal + rename), build Release `/O2 /GL` con LTCG y
        runtime estatico `/MT` (sin redistributable), y flag `--stats`

## Fase 1.5 — Modelo de datos Instance/World ✅

- [x] `Instance` con id estable (`InstanceId`, nunca se reutilizan), pool en
      `World` (sustituye `Scene`/`SceneObject`, que se eliminaron), nombres
      unicos entre hermanos, Attributes y Tags estilo Roblox, y eventos
      (`onAdded`/`onRemoved`/`onChanged`/`onReparented`)
- [x] `World`: dimensiones + arbol global Scripts (Server/Shared/Player/
      Character) + Assets (Models/Meshes/Images/Textures/Audio/Animations/
      ModuleScripts); protegidos Workspace/Camera/Lighting/Scripts y las raices
      de Scripts/Assets
- [x] `ClassRegistry` unico (categorias, clases, propiedades efectivas, valores
      por defecto, insertables); `class_catalog` pasa a ser su adaptador
- [x] `CollectionService`: indice tag -> instancias con eventos
- [x] Comandos con deltas y `UndoStack` (pila de 100, compuestos): crear,
      renombrar, mover, borrar/restaurar subarbol, clonar, agrupar/desagrupar,
      insertar subarbol (pegar/importar), propiedades, atributos, tags y
      transformaciones; un arrastre = un comando
- [x] Render por lista plana cacheada `World::renderParts()`
- [x] Escena `version: 2` (`instancia:`/`parent:` por id, solo propiedades no
      por defecto) con **migracion automatica de v1** (los Part antiguos pasan
      al Workspace de `dimension01`; no se pierde nada)
- [x] Tests del modelo (`tests/test_model.cpp`, `motorsk_tests` + CTest): 9
      pruebas, 76 comprobaciones, 0 fallos

## Fase 2 — Render básico

- [ ] Pipelines: shader vert/frag con `glslc` en build time
- [x] Mallas: cubo de 36 vertices con color por cara (pipeline de
       triangulos propio, mismos shaders y push constant que la rejilla)
- [x] Depth buffer

## Fase 3 — Escenas y recursos

- [x] Formato de escena propio (texto, versionado): `escenas/inicio.scene`
       con `instancia/parent/clase/nombre/prop/atributo/tag` por id,
       **version: 2** (solo propiedades distintas de su valor por defecto),
       tolerante a claves desconocidas; se carga al abrir el proyecto y se
       guarda de forma diferida (500 ms) y atomica al editar, y al
       cerrar/salir/Guardar. Las escenas `version: 1` se migran solas al
       abrirlas
- [ ] Carga de recursos (mallas, texturas)

## Futuro

- [ ] Backend Linux (X11/Wayland) trasladando la capa `platform/`
- [ ] Android via NDK (clang) reutilizando la capa `platform/` y el renderer
