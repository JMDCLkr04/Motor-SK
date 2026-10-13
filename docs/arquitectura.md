# Arquitectura

Documento de referencia sobre la estructura interna del motor, los flujos
principales (escena -> UI/render) y cómo probarlo. Complementa a
[README.md](../README.md) (uso) y a [roadmap.md](roadmap.md) (fases) y a
[clases.md](clases.md) (catálogo de clases).

## Módulos

El código vive en `src/` agrupado por capas. La regla general es que cada
capa depende solo de las de abajo:

```
src/main.cpp                       (solo construye EditorApp y llama a run)
   └── editor/    (EditorApp + controladores + lógica pura del editor)
         ├── editor_app.*          (estado y bucle principal; main queda en 8 líneas)
         ├── layout.*              (rectángulos de la ventana)
         ├── property_model.*      (filas de Properties: comunes, "mixto", default)
         ├── output_buffer.h       (buffer circular del panel Output)
         ├── shortcut_map.*        (tabla única acción -> tecla)
         ├── selection_controller / object_drag_controller / gizmo_controller
         ├── clipboard_controller / editor_commands
   ├── ui/        (paneles Win32 + kit de widgets propio en ui/kit/)
   │     ├── place_view       (Properties | Output | Explorer)
   │     ├── properties_panel (formulario generado desde ClassRegistry)
   │     ├── output_panel     (log con buffer circular de 5.000 líneas)
   │     ├── docs_view   (Referencia de clases, lee ClassRegistry)
   │     ├── code_view   (organizador de scripts)
   │     ├── projects_panel, play_window ...
   │     └── kit/        (widgets GDI propios: ver más abajo)
   ├── scene/     (World/Instance, commands, ClassRegistry, cámara, picking,
   │               gizmo, snapping, CollectionService, property_value)
   ├── render/    (renderer Vulkan: swapchain, pipeline, rejilla, contorno)
   ├── project/   (proyecto.sk, CRUD de carpetas de scripts)
   ├── platform/  (ventana Win32 e input, capa portable)
   ├── core/      (logging + config de %APPDATA%)
   └── math/      (Vec2/Vec3/Mat4 propias, estilo glm)
```

`src/scene/` y `src/editor/` son capas con **tests unitarios**
(`tests/test_model.cpp`); el resto es UI/render y se prueba con smoke tests
(ver "Cómo probar").

## Disposición de la ventana (`src/editor/layout.*`)

`editor::Layout` es el **único sitio** donde se calculan los rectángulos de
la división PLACE: `topBar` (barra superior), `shortcutStrip` (fila de
atajos dentro de la barra), `tabRow` (fila de pestañas PLACE/CODE/GUI),
`dimensionBar` (tira de dimensiones a ancho completo, bajo la barra),
`leftPanel` (Properties), `rightPanel` (Explorer) y `viewport` (zona 3D). Los
anchos de panel y los altos de banda y tira viven aquí (`kPropertiesWidth`,
`kExplorerWidth`, `kTopBandHeight` —derivado de las filas de
`WorkspaceTabs`— y `kDimensionBarHeight`); ningún otro archivo repite esas
cifras. `Layout::compute(w, h)` recorta paneles y viewport con ventanas
pequeñas para que `izquierda + viewport + derecha` sumen siempre el ancho de
la ventana, `contentTop()` es el borde inferior de la tira de dimensiones
(donde empiezan paneles y viewport) e `inViewport(x, y)` da el test de
picking/marquee. Es lógica pura, sin `windows.h`, y entra en `motorsk_model`.

La fila de pestañas (`WorkspaceTabs`) mide **24 px** (`kNavbarHeight`): sus
filas (título + pestañas PLACE/CODE/GUI + botón **Vista**) alimentan
`kTopBandHeight`, y hay tests que comprueban tanto la altura de fila como que
la suma de bandas cuadra con el alto de la ventana.

### Marco compartido (`PLACE | CODE | GUI`)

Las tres divisiones comparten el **mismo marco**: los paneles laterales
(Explorer a la derecha, Properties/Output a la izquierda) son **únicos**,
creados una sola vez por `PlaceView` y visibles en las tres. Cambiar de
pestaña **no recrea controles** ni parpadea (`--gdi-check` lo vigila); solo
cambia el **área central**:

- PLACE: viewport 3D (con `PlaceView` visible).
- CODE: panel central de código (`ui/code_view.*`, clase `MotorSKCodePanel`).
  De momento muestra la cabecera "Código" y el aviso "Editor de código:
  próximamente"; el editor multilínea llega en un paso posterior.
- GUI: por definir.

`EditorApp::viewportVisible()` centraliza la decisión de "¿hay viewport 3D?"
(`view3d_ && !code_.visible() && !docs_.visible()`): el picking, los atajos de
encuadre y el `logProjection_` la consultan en lugar de mirar cada panel.

El botón **Vista** de la pestañera abre un menú emergente (`WorkspaceTabs`)
con **Rejilla / Sombras / Wireframe / Estadísticas**; emite
`onViewOption(id, on)` y `EditorApp::setViewOption` lo aplica (la rejilla ya
se respeta en `renderer.drawFrame(..., showGrid)`; sombras y wireframe quedan
pendientes del renderer).

## Tira de dimensiones (`src/ui/dimensions_bar.*`)

Bajo la barra superior, a ancho completo y sobre los paneles, vive la **tira
de dimensiones**: una pestaña por cada `Dimension` del `World` (reutiliza el
`TabStrip` del kit, `ui/kit/tab_strip.*`). Permite:

- **Seleccionar** la dimensión activa (clic en la pestaña). Solo la dimensión
  activa se dibuja y se puede elegir: `EditorApp` filtra `World::renderParts()`
  y pasa el resto de dimensiones como `ignore` a `pickObjectExcept`, además de
  filtrar el marquee. Las partes nuevas se crean bajo el `Workspace` de la
  dimensión activa (`defaultParent()`).
- **Crear** una dimensión con el botón `+` (`makeCreateDimensionCommand`):
  construye la estructura completa (Workspace, Camera, Lighting con sus
  efectos y Scripts) en un `World` temporal y la inserta como subárbol, así
  que undo/redo la quita y la recrea con ids estables.
- **Cerrar** con la `x` (pide confirmación y no deja cerrar la última) y
  **renombrar** con doble clic (edición en línea). Todo pasa por comandos con
  undo y guardado diferido.

La dimensión activa es estado de interfaz (no se serializa en la escena).

## Panel Properties y panel Output (`src/ui/`)

El panel izquierdo de PLACE tiene dos pestañas, **Properties** y **Output**,
ambas widgets propios dibujados a mano (sin un `HWND` por campo):

- **`PropertiesPanel`** (`properties_panel.*`) genera las filas a partir del
  `ClassRegistry`: para la selección actual pide las filas a
  `property_model.*` (`buildPropertyRows`), las agrupa en cajitas por sección
  (orden canónico en `propertySectionOrder`), y pinta etiqueta, valor, resalte
  de "mixto", botón de restaurar (solo si el valor difiere del default) y
  tooltip con la descripción del registro. Solo la fila en edición es un
  `EDIT` superpuesto; los `Bool` llevan su propia casilla y los `Vec3` se
  pueden desplegar por eje. Los cambios salen por un único
  `onPropertyEdited(id, nombre, PropertyValue)`.
- **`OutputPanel`** (`output_panel.*`) es una lista virtualizada alimentada
  por `editor/output_buffer.h` (`OutputBuffer`, tope de 5.000 líneas
  circulares), con color por nivel (info/aviso/error), autoscroll que se
  pausa al subir y botón "Limpiar". `EditorApp` conecta el sink de
  `SK_INFO/WARN/ERROR` a `PlaceView::appendOutput`.

La lógica de valores (serialización, parseo, default y "mixto") vive en
`scene/property_value.*`, compartida con `World`; así el panel y el guardado
de escena usan exactamente el mismo formato. Añadir una propiedad a una clase
del registro basta para que aparezca en Properties, en la Referencia de clases
(`docs_view`, que también lee `ClassRegistry`) y en el archivo de escena
(`World` serializa iterando `valueCount`/`valueProp`).

## Scripts en el Explorer (`src/ui/place_view.cpp`, `src/project/scripts.*`)

El Explorer compartido muestra los scripts del proyecto como un árbol de
disco. `PlaceView` implementa la interfaz `ui::ScriptsTree` con
`DiskScriptsTree`, una vista del sistema de archivos apoyada en
`scripts::list` (nodos `scripts::Node` con `rel/name/isDir`). Los nodos de
disco se distinguen del `World` por su id (`>= ui::kScriptNodeBase`) y viven
bajo la rama global `Scripts` (las cuatro raíces Server/Shared/Player/
Character). `PlaceView::setProject` (llamado por `EditorApp::openProject`)
fija el proyecto y reconstruye ese árbol; `refreshScripts()` lo relee tras
cada cambio.

Las operaciones salen por callbacks de `EditorApp` (`onScriptCreate`,
`onScriptRename`, `onScriptDelete`, `onScriptSelected`):

- **Crear**: nombre por defecto único (`NuevoScript`, `NuevoScript2`, …) y
  `scripts::createScript`.
- **Renombrar / borrar**: se aplican en disco (`scripts::renameNode` /
  `scripts::removeNode`) y **se reescriben las asociaciones** de la escena:
  los `Attribute` `"Script"` de los nodos que apuntaban a la ruta afectada se
  actualizan o se eliminan (y la escena se guarda). Los nodos protegidos
  (`scripts::isProtected`) no se tocan.
- **Seleccionar**: limpia la selección de escena; las Properties de un script
  llegarán en un paso posterior.

## Input por eventos (`src/platform/input_state.*`)

`InputState` concentra el estado del teclado y del foco. `Window` lo
actualiza al recibir los mensajes (`WM_KEYDOWN`/`WM_KEYUP`, `WM_ACTIVATE`,
`WM_SETFOCUS`/`WM_KILLFOCUS`) y el resto del código solo lee:
`down(vk)` (tecla mantenida), `pressed(vk)` (flánco del frame),
`ctrl()/shift()/alt()` (modificadores) y `active()`/`textFocus()` (primer
plano y campo de texto). Ya no se consultan las API Win32 de teclado ni el
foco en cada frame. `newFrame()` limpia los fláncos al drenar la cola en
`pumpEvents()`.


## Controladores e `EditorCommands` (`src/editor/`)

La lógica del editor se reparte en clases con dependencias explícitas (no
conocen `EditorApp`, reciben `World`/selección por parámetro), de modo que
cada una se prueba o se reutiliza por separado:

- **`SelectionController`**: click, Ctrl+click (multi) y marquee. Guarda la
  pulsación sin resolver (`pending`), decide el umbral de arrastre y clama el
  rectángulo al `Layout::viewport`.
- **`ObjectDragController`**: arrastre del grupo apoyándolo en la cara de lo
  que tenga debajo (`surface snapping`) o en el suelo; guarda las poses
  iniciales para el undo y para revertir con Esc.
- **`GizmoController`**: ciclo de vida del `GizmoDrag` (begin/update/commit/
  cancel) y el resaltado de eje bajo el puntero. No dibuja.
- **`ClipboardController`**: portapapeles interno de subárboles (Ctrl+C/X/V);
  guarda una copia por valor, de modo que cortar/borrar después no lo
  invalida, y recuerda el padre común para el pegado.
- **`EditorCommands`** (`struct` de métodos estáticos): Delete, Duplicate,
  Group, Ungroup, Lock, Anchor, Rename y `bounds` (foco de cámara). Es la
  **única implementación** de cada acción: la llaman por igual el atajo de
  teclado, el botón y el menú.

`ClipboardController` y `EditorCommands` solo dependen de la escena, así que
viven en `motorsk_model`; los tres controladores de puntero enlazan con
`buildGizmo`/`pickObject`/`computeSurfaceDrag` y van con el ejecutable.

## `EditorApp` y `ShortcutMap`

Tras el refactor, `src/main.cpp` tiene 8 líneas: construye `editor::EditorApp`
y llama a `run(argc, argv)`. `EditorApp` es el **único dueño** del estado
(ventana, renderer, `Config`, proyecto activo, `World`, `UndoStack`, paneles,
selección por ids, objeto primario, cámara, controladores, cachés y flags) y
del bucle principal. Se reparte en varios ficheros por responsabilidad:

- `editor_app.cpp`: ciclo de vida (`run`/`initialize`/`shutdown`), parseo de
  argumentos y helpers de proyecto/escena.
- `editor_app_ui.cpp`: `wireUi()` (conexión de los `setOn…` de los paneles) y
  los callbacks (`onAddPart`, `onFileCommand`, `onInsertClass`,
  `onReparent`, ediciones de Properties, etc.).
- `editor_app_frame.cpp`: el frame, los atajos dependientes del encuadre y el
  dibujo de cámara. Un `FrameCtx` reúne lo que comparten
  atajos/interacción/dibujo (`InputState`, viewProj, viewport, gizmo…).
- `editor_app_interaction.cpp`: puntero (hover/press del gizmo, arrastre,
  cursor, umbral y suelta) y el volcado de cachés/marquee/gizmo a pantalla.

**`ShortcutMap`** (`shortcut_map.h/.cpp`, lógica pura) es la tabla única
`acción -> tecla + modificadores`. El manejador de teclado y el diálogo
"Personalizar atajos" (`Archivo > Atajos`) leen de aquí, así que no hay dos
listas que se desincronicen. `matches()` acepta modificadores de más (para no
romper combinaciones), `collisions()` detecta duplicados exactos y
`describe()` da el texto del diálogo. `InputState` es puro (reedeclara las
constantes `VK_*`, sin `windows.h`), por lo que el grep de aceptación de
`GetAsyncKeyState`/`GetKeyState` da 0.


## Modelo de datos

- **`Instance`** (`instance.h`): un nodo con `id` estable (los ids no se
  reutilizan), `parentId`, posición dentro de la lista de hermanos, nombre,
  `className`, propiedades tipadas, Attributes y Tags.
- **`World`** (`world.h/.cpp`): árbol de instancias. Estructura por defecto
  (idempotente, `ensureStructure()`): una `dimension01` con `Workspace`,
  `Camera`, `Lighting` (con sus efectos) y `Scripts`, más el árbol global
  `Scripts` (Server/Shared/Player/Character) y `Assets`. Los nodos
  estructurales están **protegidos** (no se renombran, no se borran, no se
  mueven).
- **Índice de nombres**: `World` mantiene `childNames_[(padre, nombre)] -> id`
  para que `findChild` y `uniqueName` sean O(1) amortizado (antes eran O(n)
  con un bucle por alta, O(n³) al poblar una escena grande). El índice se
  actualiza en todas las mutaciones (`create`, `adoptChild`, `detachChild`,
  `remove`, `rename`, `reparent`, `restoreInstances`) y se reconstruye al
  cargar. `nameCounters_` guarda el siguiente sufijo a probar por
  (padre, base), evitando recontar desde 1.
- **Eventos**: `onAdded`, `onRemoved`, `onChanged`, `onReparented` (callbacks
  `std::function` registrados por la UI). Las mutaciones de alto nivel los
  emiten; las de bajo nivel (`insertInstance`, `adoptChild`,
  `setNameIndexed`, `eraseInstance`, ...) no, porque son para undo/redo y
  deserialización, que emiten al final.
- **Undo/redo**: `commands.h/.cpp` (patrón comando con `UndoStack` y
  `CmdPtr`). Cada operación es un comando con `apply()/undo()`; el undo
  captura y restaura subárboles mediante `captureSubtree`.
- **ClassRegistry** (`class_registry.h/.cpp`): catálogo único de clases y
  categorías (sustituyó a `class_catalog.{h,cpp}`, eliminado). Lo usan la
  Referencia (F1), el botón `+` del Explorer y el picker de inserción, de
  modo que todos muestran exactamente la misma lista.
- **CollectionService** (`collection_service.h/.cpp`): índice por clase y por
  tags de todos los nodos; se mantiene con la estructura y se reconstruye con
  `rebuildCollection()`.

## Flujo escena -> UI/render

`EditorApp` registra los callbacks del `World` al abrir un proyecto (en
`wireUi`, `editor_app_ui.cpp`):

```
World.create(className, parent)
   └─ emitAdded(id, parent)
        ├─ ui: TreeModel anota el alta y TreeWidget repinta solo lo afectado
        ├─ ui: Properties refresca el formulario
        └─ World marca renderDirty_ (invalidateRender) y pide repintado
   └─ renderParts() devuelve la lista plana de Part (cadena cacheada)
        └─ renderer dibuja los cubos/contornos la próxima vez que aplica
```

`renderParts()` regenera una lista plana de `RenderPart` (matriz modelo, forma
e id) solo cuando `renderDirty_`; los eventos la invalidan.

## Bucle de render bajo demanda

El bucle de `EditorApp::frame`, uno por iteración de `run`:

1. Duerme en `MsgWaitForMultipleObjects` hasta que llega un mensaje de
   entrada, un `WM_PAINT` o un evento que cambie la escena.
2. Al despertar, dibuja **solo si hace falta** (entrada de cámara, cambio de
   escena, selección, herramienta, arrastre, hover del gizmo, resize).
3. Limita a **60 FPS** en GPU, **30 FPS** si el device es un rasterizador por
   software (lavapipe/llvmpipe).

El guardado de escena es **diferido (500 ms)** tras el último cambio y con
escritura **atómica**: se escribe `<escena>.scene.tmp` y se hace
`std::filesystem::rename` sobre el destino (si al cargar no existe el archivo
principal se promociona el `.tmp`). Esto evita escenas corruptas si la
aplicación se cierra a mitad de escritura.

## Kit de widgets (`ui/kit/`)

Controles de GDI propios para sustituir gradualmente los nativos
(ListView de comctl32) y garantizar el mismo tema oscuro en toda la UI (el
`TreeView` nativo ya no se usa en ninguna parte: el antiguo árbol de scripts
del panel CODE murió al pasar los scripts al Explorer compartido):

- `paint_buffer` (backing store + doble buffer para parpadeo cero),
  `theme.h` (tokens de color/medida), `icon_atlas` + `icons.h`
  (pipeline `tools/make_icons.py` -> PNG/atlas + nombre por icono),
  `scroll_bar`, `tab_strip`, `disclosure` (plegable), `place_popup`
  (menú contextual del `+`), `tree_model`, `tree_widget`, `list_widget`,
  `demo_window` (muestra el kit, se abre con `--ui-demo`).

Estado: el Explorer ya usa `tree_model` + `tree_widget` (filas de 22 px
virtualizadas, sangría real, icono por clase, `+` en la fila bajo el ratón,
selección/teclado/renombrado en línea y arrastrar+soltar con fila fantasma y
tres zonas). La lista de la Referencia de clases (`docs_view`) usa
`list_widget` (cabeceras redondeadas, sin blanco nativo). El resto de
controles nativos se portarán de forma incremental.

`tree_model.{h,cpp}` es **lógica pura** (sin `windows.h`, entra en
`motorsk_model`): lee el `World` por id (no copia texto), aplana en preorden
con pila explícita (aguanta 200+ niveles), guarda el conjunto de expandidos
por id y resuelve `dropTarget(origen, destino, zona)` (ciclo, protegido, él
mismo, inexistente). Se prueba en `tests/test_model.cpp`. 

Además del `World`, el árbol puede mostrar una segunda fuente de nodos
sintéticos a través de la interfaz `ui::ScriptsTree` (`setScriptsTree`): los
nodos con id `>= kScriptNodeBase` los resuelve esa fuente. Así el Explorer
lista los **scripts en disco** (bajo la rama global `Scripts`, en subcarpetas
Server/Shared/Player/Character) como una vista del sistema de archivos, sin
que `tree_model` dependa de `windows.h` ni del proyecto.

## Cómo probar

Tests del modelo (asserts sin framework; salen con un contador de
comprobaciones y de fallos):

```bat
call build.bat
build\Release\motorsk_tests.exe
```

Incluyen perfiles de rendimiento del índice de nombres (5000 altas en el
mismo padre < 150 ms, clonar/insertar 2000 nodos < 100 ms) y un **fuzz** de
4000 operaciones aleatorias (create/reparent/remove/rename) que comprueba los
invariantes (padre<->hijos consistente, nombres únicos entre hermanos). Hay
además pruebas del `TreeModel`: aplanado en preorden a 5 y a 200 niveles,
expandir/plegar, borrado a profundidad 5, reubicación, destinos inválidos de
arrastre (ciclo/protegido/él mismo/inexistente) y preorden estable aunque los
eventos del `World` lleguen desordenados. Los
límites de tiempo solo se exigen en compilación optimizada (`Release` /O2);
bajo `_DEBUG` o ASan la instrumentación los hace no representativos y se mide
sin afirmar. También hay pruebas de la disposición de la ventana
(`Layout`: en ventana pequeña y grande, los rectángulos no se solapan y
`izquierda + viewport + derecha` suman el ancho) y de la tabla de atajos
(`ShortcutMap`: sin colisiones, `find`/`matches` de cada combinación).

Smoke test del editor:

```bat
build\Release\MotorSK.exe --frames 30 > log.txt 2>&1
build\Release\MotorSK.exe --stats   # cada 5 s: frames y media del frame
build\Release\MotorSK.exe --proyecto <ruta> --play
build\Release\MotorSK.exe --ui-demo  # abre la demostración del kit de UI
build\Release\MotorSK.exe --gdi-check # PASS si cambiar de vista no recrea controles
```

Con `--debug`, al abrir un proyecto se registra cuántos scripts en disco ve el
Explorer (`Explorer: N scripts en disco bajo <ruta>`), útil para comprobar el
árbol de scripts sin depender de capturas.

Build con sanitizadores (AddressSanitizer del MSVC) para detectar errores de
memoria en el modelo; se construye en `build-asan` (runtime dinámico, sin
optimizar):

```bat
cmake -S . -B build-asan -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL ^
      -DCMAKE_CXX_FLAGS="/fsanitize=address /Zi" -DCMAKE_EXE_LINKER_FLAGS="/DEBUG"
cmake --build build-asan --config Debug --target motorsk_tests
build-asan\Debug\motorsk_tests.exe
```

`build-asan/` está en `.gitignore`; los artefactos de build nunca se
versionan.

## Entrada de teclado y Play/Run

- **Teclado a nivel de hilo.** `Window::pumpEvents` alimenta el `InputState`
  con los `WM_KEYDOWN/WM_KEYUP` (y variantes `SYS`) *antes* de
  `TranslateMessage`/`DispatchMessage`, para TODA la ventana del hilo. Así
  WASD, Supr y los atajos funcionan aunque el foco esté en el Explorer, en
  Properties o en un cajetín, que son ventanas hijas distintas. Los controles
  (EDIT, LISTBOX) siguen recibiendo su mensaje; los atajos del editor se
  gatean con `InputState::textFocus()` para que escribir no mueva la cámara.
  `onKeyDown/onKeyUp` marcan flanco solo en la transición, así que el
  manejador de la propia ventana no cuenta doble.
- **Selector de clase del `+`.** El foco vive en el EDIT del filtro; una
  subclase (`editProc`) intercepta Esc (cierra), Enter (elige) y las flechas
  (mueven la selección sin sacar el foco). Un clic simple en la lista también
  elige (`LBN_SELCHANGE`), además del doble clic.
- **Cámara de Play/Run.** `PlayWindow` tiene su propia `Camera` y arranca en
  `playSpawnPosition(world)` (`src/scene/spawn.h`): la posición del primer
  `SpawnLocation`, o `(0,20,0)` si no hay. La lógica es pura y se prueba en
  `testPlaySpawnPosition`. La ventana de Play abre con su propio renderer y
  dibuja la escena cada frame desde el bucle principal.

Con `--debug` el editor registra foco/capture de las ventanas, los clics
(press/release y si caen en el viewport), los recuentos de render
(`render: partes/visibles/dimActiva`), las llamadas a `drawFrame` y la
apertura/elección del selector de clase, para diagnosticar fallos de entrada
o de objetos invisibles sin depender de capturas de pantalla.