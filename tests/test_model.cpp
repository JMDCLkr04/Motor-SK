// Tests del modelo de datos (Instance/World/commands). Sin framework:
// asserts con un contador de fallos. Uso: motorsk_tests
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "editor/layout.h"
#include "editor/output_buffer.h"
#include "editor/property_model.h"
#include "editor/shortcut_map.h"
#include "platform/input_state.h"
#include "scene/commands.h"
#include "scene/property_value.h"
#include "scene/spawn.h"
#include "scene/world.h"
#include "ui/kit/tree_model.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            std::printf("  FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

using namespace sk;

// Los limites de tiempo solo se exigen en compilacion optimizada (Release
// -O2). Bajo ASan o Debug la instrumentacion los multiplica y no son
// representativos; ahi se ejecuta el codigo (para detectar errores de
// memoria) pero sin afirmar el tiempo.
#if defined(__SANITIZE_ADDRESS__) || defined(_DEBUG)
#define SK_PERF_THRESHOLDS 0
#else
#define SK_PERF_THRESHOLDS 1
#endif

InstanceId workspaceOf(World& w) {
    std::vector<InstanceId> dims = w.dimensions();
    if (dims.empty()) return kInvalidId;
    return w.findChild(dims.front(), kWorkspaceName);
}

// --- Nombres unicos entre hermanos --------------------------------------
void testNameUniqueness() {
    std::printf("testNameUniqueness\n");
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);

    const InstanceId a = w.create("Part", ws);
    const InstanceId b = w.create("Part", ws);
    const InstanceId c = w.create("Part", ws);
    CHECK(w.find(a)->name() == "Part");
    CHECK(w.find(b)->name() == "Part1");
    CHECK(w.find(c)->name() == "Part2");

    // Renombrar hacia un nombre ocupado auto-numera.
    CHECK(w.rename(a, "Part1"));
    CHECK(w.find(a)->name() != w.find(b)->name());
    CHECK(w.find(a)->name().rfind("Part1", 0) == 0);
}

// --- Reubicar y ciclos ---------------------------------------------------
void testReparentAndCycles() {
    std::printf("testReparentAndCycles\n");
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);

    const InstanceId model = w.create("Model", ws);
    const InstanceId part = w.create("Part", model);
    CHECK(w.find(part)->parentId() == model);

    // Ciclo: meter el padre dentro de su propio hijo debe fallar.
    CHECK(!w.reparent(model, part));

    // Mover el hijo al Workspace: cambia de padre.
    CHECK(w.reparent(part, ws));
    CHECK(w.find(part)->parentId() == ws);
    CHECK(w.findChild(ws, w.find(part)->name()) == part);
    CHECK(w.findChild(model, w.find(part)->name()) == kInvalidId);

    // Mover a un destino inexistente falla.
    CHECK(!w.reparent(part, 999999));
}

// --- Nodos protegidos ----------------------------------------------------
void testProtected() {
    std::printf("testProtected\n");
    World w;
    w.ensureStructure();
    const InstanceId dim = w.dimensions().front();
    const InstanceId ws = w.findChild(dim, kWorkspaceName);
    const InstanceId ws2 = w.findChild(w.createDimension("dimension02"),
                                       kWorkspaceName);
    CHECK(w.isProtected(ws));
    CHECK(!w.remove(ws));
    CHECK(!w.rename(ws, "Otro"));
    CHECK(!w.reparent(ws, ws2));
    CHECK(w.find(ws) != nullptr);
    CHECK(w.find(ws)->name() == "Workspace");
}

// --- Clonado de subarbol -------------------------------------------------
void testCloneSubtree() {
    std::printf("testCloneSubtree\n");
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);

    const InstanceId model = w.create("Model", ws);
    w.create("Part", model);
    w.create("Part", model);
    const size_t before = w.aliveCount();

    const InstanceId copy = w.clone(model, ws);
    CHECK(copy != kInvalidId);
    CHECK(copy != model);
    CHECK(w.find(copy)->className() == "Model");
    CHECK(w.find(copy)->children().size() == 2);
    CHECK(w.aliveCount() == before + 3); // model + 2 parts

    // Los descendientes tienen ids nuevos.
    CHECK(w.find(copy)->children()[0] != w.find(model)->children()[0]);
}

// --- Undo/redo -----------------------------------------------------------
void testUndoRedo() {
    std::printf("testUndoRedo\n");
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);
    const InstanceId dim = w.dimensions().front();
    UndoStack undo;

    // Alta.
    undo.push(w, makeCreateCommand(w, "Part", ws, Vec3{0, 0.5f, 0}));
    const InstanceId part = w.findChild(ws, "Part");
    CHECK(part != kInvalidId);
    undo.undo(w);
    CHECK(w.findChild(ws, "Part") == kInvalidId);
    undo.redo(w);
    const InstanceId part2 = w.findChild(ws, "Part");
    CHECK(part2 == part); // rehace con el mismo id

    // Transformacion.
    undo.push(w, makeTransformCommand(w, part, Vec3{5, 1, 2}, Vec3{0, 90, 0},
                                      Vec3{1, 1, 1}));
    CHECK(w.find(part)->position().x == 5.0f);
    undo.undo(w);
    CHECK(w.find(part)->position().x == 0.0f);

    // Renombrar.
    undo.push(w, makeRenameCommand(w, part, "MiParte"));
    CHECK(w.find(part)->name() == "MiParte");
    undo.undo(w);
    CHECK(w.find(part)->name() == "Part");

    // Propiedad.
    undo.push(w, makePropertyCommand(w, part, "Transparency",
                                     PropertyValue{false, 0, 0.75}));
    CHECK(w.find(part)->propFloat("Transparency") == 0.75);
    undo.undo(w);
    CHECK(w.find(part)->propFloat("Transparency") == 0.0);

    // Reubicar.
    const InstanceId model = w.create("Model", ws);
    undo.push(w, makeReparentCommand(w, part, model));
    CHECK(w.find(part)->parentId() == model);
    undo.undo(w);
    CHECK(w.find(part)->parentId() == ws);

    // Borrado de subarbol y restauracion con los mismos ids.
    const InstanceId inner = w.create("Part", model);
    undo.push(w, makeRemoveCommand(w, model));
    CHECK(w.find(model) == nullptr);
    CHECK(w.find(inner) == nullptr);
    undo.undo(w);
    CHECK(w.find(model) != nullptr);
    CHECK(w.find(inner) != nullptr);
    CHECK(w.find(inner)->parentId() == model);

    (void)dim;
}

// --- Crear dimension como comando ---------------------------------------
void testDimensionCommand() {
    std::printf("testDimensionCommand\n");
    World w;
    w.ensureStructure();
    const size_t before = w.dimensions().size();
    UndoStack undo;

    InstanceId created = kInvalidId;
    undo.push(w, makeCreateDimensionCommand(w, "Nivel1", &created));
    CHECK(created != kInvalidId);
    CHECK(w.dimensions().size() == before + 1);
    CHECK(w.find(created) != nullptr);
    CHECK(w.find(created)->className() == "Dimension");
    CHECK(w.find(created)->name() == "Nivel1");

    // La dimension llega completa: Workspace, Camera, Lighting (con efectos)
    // y Scripts.
    CHECK(w.findChild(created, kWorkspaceName) != kInvalidId);
    CHECK(w.findChild(created, kCameraName) != kInvalidId);
    const InstanceId li = w.findChild(created, kLightingName);
    CHECK(li != kInvalidId);
    CHECK(w.findChild(li, "Sky") != kInvalidId);
    CHECK(w.findChild(created, kScriptsName) != kInvalidId);

    // Undo la quita entera; redo la recrea con el mismo id y estructura.
    undo.undo(w);
    CHECK(w.dimensions().size() == before);
    CHECK(w.find(created) == nullptr);
    undo.redo(w);
    CHECK(w.dimensions().size() == before + 1);
    CHECK(w.find(created) != nullptr);
    CHECK(w.findChild(created, kWorkspaceName) != kInvalidId);
    CHECK(w.findChild(created, kCameraName) != kInvalidId);

    // El nombre no choca con el existente: se uniciza como Nivel1 (1).
    InstanceId second = kInvalidId;
    undo.push(w, makeCreateDimensionCommand(w, "Nivel1", &second));
    CHECK(second != kInvalidId);
    CHECK(w.find(second)->name() != w.find(created)->name());
}

// --- Tags + CollectionService -------------------------------------------
void testTags() {
    std::printf("testTags\n");
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);
    const InstanceId part = w.create("Part", ws);

    CHECK(w.addTag(part, "Rojo"));
    CHECK(w.find(part)->hasTag("Rojo"));
    std::vector<InstanceId> tagged = w.collection().getTagged("Rojo");
    CHECK(tagged.size() == 1 && tagged[0] == part);

    w.removeTag(part, "Rojo");
    CHECK(!w.find(part)->hasTag("Rojo"));
    CHECK(w.collection().getTagged("Rojo").empty());
}

// --- Crear Dimension -----------------------------------------------------
void testCreateDimension() {
    std::printf("testCreateDimension\n");
    World w;
    const InstanceId dim = w.createDimension("dimension02");
    CHECK(dim != kInvalidId);
    const InstanceId ws = w.findChild(dim, kWorkspaceName);
    const InstanceId cam = w.findChild(dim, kCameraName);
    const InstanceId li = w.findChild(dim, kLightingName);
    const InstanceId sc = w.findChild(dim, kScriptsName);
    CHECK(ws != kInvalidId);
    CHECK(cam != kInvalidId);
    CHECK(li != kInvalidId);
    CHECK(sc != kInvalidId);
    CHECK(w.find(li)->children().size() == 9);
}

// --- Migracion v1 -> v2 --------------------------------------------------
void testMigrateV1() {
    std::printf("testMigrateV1\n");
    const std::filesystem::path p =
        std::filesystem::temp_directory_path() / "motorsk_v1_test.scene";
    {
        std::ofstream f(p);
        f << "version: 1\n";
        f << "objeto: dimension01\n";
        f << "clase: Dimension\n";
        f << "protegido: true\n";
        f << "posicion: 0 0 0\n";
        f << "objeto: Workspace\n";
        f << "clase: Workspace\n";
        f << "protegido: true\n";
        f << "parent: dimension01\n";
        f << "objeto: Part\n";
        f << "clase: Part\n";
        f << "parent: Workspace\n";
        f << "posicion: 1 0.5 2\n";
        f << "forma: esfera\n";
        f << "anchored: true\n";
        f << "objeto: Model\n";
        f << "clase: Model\n";
        f << "parent: Part\n";
    }

    World w;
    CHECK(w.loadFromFile(p.string()));

    // La estructura existe y no se duplica.
    std::vector<InstanceId> dims = w.dimensions();
    CHECK(dims.size() == 1);
    const InstanceId ws = w.findChild(dims.front(), kWorkspaceName);
    CHECK(ws != kInvalidId);

    // Los objetos de contenido viven bajo Workspace.
    const InstanceId part = w.findChild(ws, "Part");
    CHECK(part != kInvalidId);
    CHECK(w.find(part)->className() == "Part");
    CHECK(w.find(part)->shape() == Shape::Sphere);
    CHECK(w.find(part)->propBool("Anchored"));
    CHECK(w.find(part)->position().x == 1.0f);

    // La jerarquia entre objetos de contenido se conserva.
    const InstanceId model = w.findChild(part, "Model");
    CHECK(model != kInvalidId);
    CHECK(w.find(model)->className() == "Model");

    std::filesystem::remove(p);
}

// --- Round-trip v2 -------------------------------------------------------
void testSaveLoadRoundTrip() {
    std::printf("testSaveLoadRoundTrip\n");
    const std::filesystem::path p =
        std::filesystem::temp_directory_path() / "motorsk_v2_test.scene";

    InstanceId partId = kInvalidId;
    InstanceId modelId = kInvalidId;
    {
        World w;
        w.ensureStructure();
        const InstanceId ws = workspaceOf(w);
        modelId = w.create("Model", ws);
        partId = w.create("Part", modelId);
        w.setVec3(partId, "Position", Vec3{3, 1, 4});
        w.setFloat(partId, "Transparency", 0.5);
        w.setString(partId, "Shape", "Cylinder");
        w.setTags(partId, {"A", "B"});
        w.addTag(modelId, "Grupo");
        Attribute a;
        a.name = "Vida";
        a.type = "number";
        a.value = "42";
        w.setAttribute(partId, a);
        CHECK(w.saveToFile(p.string()));
    }

    {
        World w;
        CHECK(w.loadFromFile(p.string()));
        const InstanceId ws = workspaceOf(w);
        const InstanceId model = w.findChild(ws, "Model");
        CHECK(model != kInvalidId);
        const InstanceId part = w.findChild(model, "Part");
        CHECK(part != kInvalidId);
        CHECK(part == partId); // ids preservados
        CHECK(model == modelId);
        CHECK(w.find(part)->position().x == 3.0f);
        CHECK(w.find(part)->position().z == 4.0f);
        CHECK(w.find(part)->propFloat("Transparency") == 0.5);
        CHECK(w.find(part)->shape() == Shape::Cylinder);
        CHECK(w.find(part)->hasTag("A") && w.find(part)->hasTag("B"));
        CHECK(w.find(model)->hasTag("Grupo"));
        CHECK(w.find(part)->getAttribute("Vida").value == "42");
        CHECK(w.collection().getTagged("A").size() == 1);
    }

    std::filesystem::remove(p);
}

// --- Rendimiento: nombres unicos O(1) amortizado ------------------------
void testNameIndexPerformance() {
    std::printf("testNameIndexPerformance\n");

    // 1) Crear 5000 Parts en el mismo padre (< 150 ms en Release -O2).
    {
        World w;
        w.ensureStructure();
        const InstanceId ws = workspaceOf(w);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5000; ++i) {
            const InstanceId id = w.create("Part", ws);
            (void)id;
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        std::printf("  5000 altas en el mismo padre: %lld ms\n",
                    static_cast<long long>(ms));
#if SK_PERF_THRESHOLDS
        CHECK(ms < 150);
#endif
        CHECK(w.find(ws)->children().size() == 5000);
        // Los nombres son unicos y correlativos.
        CHECK(w.findChild(ws, "Part") != kInvalidId);
        CHECK(w.findChild(ws, "Part4999") != kInvalidId);
        CHECK(w.findChild(ws, "Part5000") == kInvalidId);
    }

    // 2) Clonar un subarbol de 2000 nodos (< 100 ms).
    {
        World w;
        w.ensureStructure();
        const InstanceId ws = workspaceOf(w);
        const InstanceId model = w.create("Model", ws);
        for (int i = 0; i < 1999; ++i) w.create("Part", model);

        const auto t0 = std::chrono::steady_clock::now();
        InstanceId root = kInvalidId;
        auto cmd = makeCloneCommand(w, model, ws, Vec3{1, 0, 0}, &root);
        cmd->apply(w);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        std::printf("  clonar 2000 nodos: %lld ms\n",
                    static_cast<long long>(ms));
#if SK_PERF_THRESHOLDS
        CHECK(ms < 100);
#endif
        CHECK(root != kInvalidId);
        CHECK(w.subtree(root).size() == 2000);
    }

    // 3) Insertar un subarbol capturado de 2000 nodos (< 100 ms).
    {
        World w;
        w.ensureStructure();
        const InstanceId ws = workspaceOf(w);
        const InstanceId model = w.create("Model", ws);
        for (int i = 0; i < 1999; ++i) w.create("Part", model);

        std::vector<Instance> nodes;
        for (InstanceId sid : w.subtree(model)) {
            const Instance* inst = w.find(sid);
            if (inst) nodes.push_back(*inst);
        }
        std::vector<InstanceId> newIds;
        newIds.reserve(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i) {
            newIds.push_back(w.allocateId());
        }

        const auto t0 = std::chrono::steady_clock::now();
        auto cmd = makeInsertSubtreeCommand(w, nodes, newIds, ws, Vec3{0, 0, 1},
                                            "Copia_", nullptr);
        cmd->apply(w);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        std::printf("  insertar 2000 nodos capturados: %lld ms\n",
                    static_cast<long long>(ms));
#if SK_PERF_THRESHOLDS
        CHECK(ms < 100);
#endif
        CHECK(w.findChild(ws, "Copia_Model") != kInvalidId);
    }
}

// --- Fuzz: invariantes de nombres y jerarquia ---------------------------
void checkSiblings(World& w, InstanceId parent,
                   const std::vector<InstanceId>& children) {
    std::unordered_set<std::string> names;
    for (InstanceId c : children) {
        const Instance* inst = w.find(c);
        CHECK(inst != nullptr);
        if (!inst) continue;
        CHECK(w.find(c)->parentId() == parent);
        CHECK(names.insert(inst->name()).second); // sin repetidos
    }
}

void checkTreeConsistency(World& w, const std::vector<InstanceId>& created) {
    std::vector<InstanceId> alive;
    for (InstanceId id : created) {
        if (w.exists(id)) alive.push_back(id);
    }

    checkSiblings(w, kInvalidId, w.roots());
    for (InstanceId id : alive) {
        const Instance* inst = w.find(id);
        const InstanceId p = inst->parentId();
        if (p == kInvalidId) {
            const std::vector<InstanceId>& r = w.roots();
            CHECK(std::find(r.begin(), r.end(), id) != r.end());
        } else {
            const Instance* par = w.find(p);
            CHECK(par != nullptr);
            if (par) {
                const std::vector<InstanceId>& c = par->children();
                CHECK(std::find(c.begin(), c.end(), id) != c.end());
            }
        }
        checkSiblings(w, id, inst->children());
    }
}

void testNameFuzz() {
    std::printf("testNameFuzz\n");
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);

    std::vector<InstanceId> created{ws};
    std::vector<InstanceId> alive;

    uint64_t state = 0x9e3779b97f4a7c15ull;
    auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    auto rnd = [&](uint64_t n) { return n ? next() % n : 0; };

    const char* classes[] = {"Part", "Model", "Folder", "StringValue"};

    for (int op = 0; op < 4000; ++op) {
        alive.clear();
        for (InstanceId id : created) {
            if (w.exists(id)) alive.push_back(id);
        }
        if (alive.empty()) alive.push_back(ws);

        switch (rnd(8)) {
            case 0:
            case 1:
            case 2:
            case 3: { // crear bajo un padre existente (o la raiz)
                InstanceId parent = alive[rnd(alive.size())];
                if (rnd(8) == 0) parent = ws;
                const InstanceId id = w.create(classes[rnd(4)], parent);
                if (id != kInvalidId) created.push_back(id);
                break;
            }
            case 4:
            case 5: { // reparentar
                const InstanceId id = alive[rnd(alive.size())];
                const InstanceId np = alive[rnd(alive.size())];
                w.reparent(id, np);
                break;
            }
            case 6: { // borrar
                const InstanceId id = alive[rnd(alive.size())];
                w.remove(id);
                break;
            }
            default: { // renombrar (fuerza colisiones con pocos nombres base)
                const InstanceId id = alive[rnd(alive.size())];
                const std::string base = "N" + std::to_string(rnd(5));
                w.rename(id, base);
                break;
            }
        }
    }

    checkTreeConsistency(w, created);
    std::printf("  %zu nodos tras 4000 operaciones\n", w.aliveCount());
}

// --- Arbol del Explorer (TreeModel) --------------------------------------
using sk::ui::DropDecision;
using sk::ui::DropZone;
using sk::ui::IconId;
using sk::ui::NodeId;
using sk::ui::TreeModel;
using sk::ui::TreeNode;

// Comprueba que cada fila aparece despues de su padre (preorden) y que el
// nivel del hijo es el del padre + 1.
bool isPreorder(const std::vector<TreeNode>& rows) {
    std::unordered_map<NodeId, int> pos;
    for (int i = 0; i < (int)rows.size(); ++i) pos[rows[i].id] = i;
    for (int i = 0; i < (int)rows.size(); ++i) {
        const NodeId p = rows[i].parentId;
        if (p == 0 || p == sk::ui::kTreeRootId) continue; // nivel superior
        auto it = pos.find(p);
        if (it == pos.end()) return false;
        if (it->second >= i) return false;
        if (rows[i].level != rows[it->second].level + 1) return false;
    }
    return true;
}

void testTreeFlatten5Levels() {
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);
    const InstanceId a = w.create("Part", ws);
    const InstanceId b = w.create("Part", a);
    const InstanceId c = w.create("Part", b);
    const InstanceId d = w.create("Part", c);
    const InstanceId e = w.create("Part", d);

    TreeModel m(&w);
    m.expandAll();
    const std::vector<TreeNode>& rows = m.rows();
    CHECK(isPreorder(rows));
    const int iws = m.indexOf(ws);
    const int ie = m.indexOf(e);
    CHECK(iws >= 0);
    CHECK(ie >= 0);
    CHECK(iws < ie);
    CHECK(rows[ie].level == rows[iws].level + 5);
    CHECK(rows[ie].id == e);
    std::printf("  5 niveles: fila Workspace=%d, fila E=%d (nivel %d)\n", iws,
                ie, rows[ie].level);
}

void testTreeExpandCollapse() {
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);
    const InstanceId a = w.create("Part", ws);
    const InstanceId b = w.create("Part", a);

    TreeModel m(&w);
    m.collapseAll();
    // Sin expandir: solo los 3 nodos de nivel superior (Dimensions,
    // Scripts y Assets).
    CHECK(m.rows().size() == 3);
    CHECK(m.indexOf(a) < 0);
    CHECK(m.indexOf(b) < 0);

    m.expandTo(b);
    CHECK(m.isExpanded(a));
    CHECK(m.indexOf(a) >= 0);
    CHECK(m.indexOf(b) >= 0);

    m.setExpanded(a, false);
    CHECK(m.indexOf(a) >= 0);
    CHECK(m.indexOf(b) < 0);

    m.setExpanded(a, true);
    CHECK(m.indexOf(b) >= 0);
    CHECK(isPreorder(m.rows()));
}

void testTreeRemovalDepth5() {
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);
    InstanceId parent = ws;
    for (int i = 0; i < 5; ++i) parent = w.create("Part", parent);
    const InstanceId deep = parent;
    const InstanceId leaf = w.create("Part", deep);

    TreeModel m(&w);
    m.expandAll();
    CHECK(m.indexOf(deep) >= 0);
    CHECK(m.indexOf(leaf) >= 0);
    const size_t before = m.rows().size();

    CHECK(w.remove(deep));
    m.onRemoved(deep);
    CHECK(m.indexOf(deep) < 0);
    CHECK(m.indexOf(leaf) < 0); // los descendientes tambien desaparecen
    CHECK(m.rows().size() < before);
    CHECK(isPreorder(m.rows()));
    std::printf("  borrado a profundidad 5: %zu -> %zu filas\n", before,
                m.rows().size());
}

void testTree200Levels() {
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);
    InstanceId parent = ws;
    InstanceId deepest = ws;
    for (int i = 0; i < 200; ++i) {
        parent = w.create("Part", parent);
        deepest = parent;
    }
    TreeModel m(&w);
    m.expandAll();
    const std::vector<TreeNode>& rows = m.rows();
    CHECK(isPreorder(rows));
    const int iws = m.indexOf(ws);
    const int ideep = m.indexOf(deepest);
    CHECK(iws >= 0);
    CHECK(ideep >= 0);
    CHECK(rows[ideep].level == rows[iws].level + 200);
    std::printf("  200 niveles: %zu filas, nivel mas profundo=%d\n",
                rows.size(), rows[ideep].level);
}

void testTreeReparent() {
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);
    const InstanceId a = w.create("Part", ws);
    const InstanceId b = w.create("Part", ws);

    TreeModel m(&w);
    m.expandAll();
    CHECK(m.parentOf(a) == ws);
    const DropDecision d = m.dropTarget(a, b, DropZone::Inside);
    CHECK(d.valid);
    CHECK(d.newParent == b);

    CHECK(w.reparent(a, b));
    m.onReparented(a, ws, b);
    CHECK(m.parentOf(a) == b);
    CHECK(isPreorder(m.rows()));
}

void testTreeInvalidTargets() {
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);
    const InstanceId a = w.create("Part", ws);
    const InstanceId b = w.create("Part", a);

    TreeModel m(&w);
    m.expandAll();

    const DropDecision self = m.dropTarget(a, a, DropZone::Inside);
    CHECK(!self.valid);
    CHECK(std::string(self.reason) == "el mismo");

    // b ya esta dentro de a: mover a dentro de b formaria un ciclo.
    const DropDecision cyc = m.dropTarget(a, b, DropZone::Inside);
    CHECK(!cyc.valid);
    CHECK(std::string(cyc.reason) == "ciclo");

    // Workspace es protegido: no se puede arrastrar.
    const DropDecision prot = m.dropTarget(ws, b, DropZone::Inside);
    CHECK(!prot.valid);
    CHECK(std::string(prot.reason) == "protegido");

    const DropDecision missing = m.dropTarget(999999, b, DropZone::Inside);
    CHECK(!missing.valid);
    CHECK(std::string(missing.reason) == "inexistente");
}

void testTreePreorderUnorderedEvents() {
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);
    const InstanceId a = w.create("Part", ws);
    const InstanceId b = w.create("Part", a);
    const InstanceId c = w.create("Part", b);

    TreeModel m(&w);
    // Eventos en desorden (y alguno redundante): el modelo lee del World,
    // asi que el orden no debe alterar el preorden.
    m.onAdded(c, b);
    m.onAdded(a, ws);
    m.onAdded(b, a);
    m.onReparented(c, b, b);
    m.onRenamed(a);
    m.expandAll();
    CHECK(isPreorder(m.rows()));
    CHECK(m.indexOf(a) >= 0);
    CHECK(m.indexOf(b) >= 0);
    CHECK(m.indexOf(c) >= 0);
}

void testTreeIcons() {
    using sk::ui::iconForClass;
    const IconId part = iconForClass("Part");
    const IconId model = iconForClass("Model");
    const IconId folder = iconForClass("Folder");
    const IconId workspace = iconForClass("Workspace");
    const IconId camera = iconForClass("Camera");
    const IconId script = iconForClass("Script");
    CHECK(part == IconId::Part);
    CHECK(model == IconId::Model);
    CHECK(folder == IconId::Folder);
    CHECK(workspace == IconId::Workspace);
    CHECK(camera == IconId::Camera);
    CHECK(script == IconId::Script);
    std::printf(
        "  iconos: Part=%d Model=%d Folder=%d Workspace=%d Camera=%d "
        "Script=%d\n",
        (int)part, (int)model, (int)folder, (int)workspace, (int)camera,
        (int)script);
}

// --- Layout ---------------------------------------------------------------
using sk::editor::Layout;
using sk::editor::Rect;

bool overlap(const Rect& a, const Rect& b) {
    return !(a.right() <= b.x || b.right() <= a.x || a.bottom() <= b.y ||
             b.bottom() <= a.y);
}

void checkLayout(int w, int h) {
    const Layout l = Layout::compute(w, h);
    CHECK(l.width == w);
    CHECK(l.height == h);

    // La barra empieza en 0 y ocupa todo el ancho.
    CHECK(l.topBar.x == 0 && l.topBar.y == 0 && l.topBar.w == w);
    CHECK(l.topBar.h <= Layout::kTopBandHeight);

    // Las sub-bandas viven dentro de la barra y no se solapan entre si.
    CHECK(l.shortcutStrip.x == 0 && l.shortcutStrip.w == w);
    CHECK(l.tabRow.x == 0 && l.tabRow.w == w);
    CHECK(l.shortcutStrip.y >= l.topBar.y);
    CHECK(l.shortcutStrip.bottom() <= l.topBar.bottom());
    CHECK(l.tabRow.bottom() <= l.topBar.bottom());
    CHECK(!overlap(l.shortcutStrip, l.tabRow));

    // La tira de dimensiones ocupa todo el ancho justo bajo la barra y por
    // encima del contenido.
    CHECK(l.dimensionBar.x == 0 && l.dimensionBar.w == w);
    CHECK(l.dimensionBar.y >= l.topBar.bottom());
    CHECK(l.dimensionBar.h <= Layout::kDimensionBarHeight);
    CHECK(l.dimensionBar.bottom() == l.contentTop());

    // Paneles y viewport: debajo de la tira, sin solaparse y sumando el
    // ancho de la ventana.
    CHECK(l.leftPanel.y == l.contentTop());
    CHECK(l.rightPanel.y == l.contentTop());
    CHECK(l.viewport.y == l.contentTop());
    CHECK(!overlap(l.leftPanel, l.rightPanel));
    CHECK(!overlap(l.leftPanel, l.viewport));
    CHECK(!overlap(l.rightPanel, l.viewport));
    CHECK(l.leftPanel.w + l.viewport.w + l.rightPanel.w == w);
    // Viewport/paneles y las bandas superiores no se pisan.
    CHECK(!overlap(l.viewport, l.topBar));
    CHECK(!overlap(l.leftPanel, l.topBar));
    CHECK(!overlap(l.viewport, l.dimensionBar));
    CHECK(!overlap(l.leftPanel, l.dimensionBar));

    // El viewport empieza justo tras el panel izquierdo.
    CHECK(l.viewport.x == l.leftPanel.right());
    CHECK(l.rightPanel.right() == w);

    // inViewport coincide con los limites del viewport.
    if (l.viewport.w > 0 && l.viewport.h > 0) {
        CHECK(l.inViewport(l.viewport.x, l.viewport.y));
        CHECK(l.inViewport(l.viewport.right(), l.viewport.bottom() - 1));
        CHECK(!l.inViewport(l.leftPanel.x, l.viewport.y));
        CHECK(!l.inViewport(l.viewport.x, l.topBar.y));
    }
}

void testLayout() {
    std::printf("testLayout\n");
    // Ventana grande y ventana pequena (mas estrecha que los paneles).
    const int sizes[][2] = {{1280, 720}, {1920, 1080}, {640, 480}, {200, 120},
                            {300, 116}, {100, 40}};
    for (const int* s : sizes) {
        checkLayout(s[0], s[1]);
    }
    // En la ventana estandar los paneles conservan su ancho nominal.
    const Layout big = Layout::compute(1280, 720);
    CHECK(big.leftPanel.w == Layout::kPropertiesWidth);
    CHECK(big.rightPanel.w == Layout::kExplorerWidth);
    CHECK(big.viewport.w ==
          1280 - Layout::kPropertiesWidth - Layout::kExplorerWidth);
    CHECK(big.viewport.h ==
          720 - Layout::kTopBandHeight - Layout::kDimensionBarHeight);
    std::printf("  banda=%d tira=%d props=%d explorer=%d viewport=%dx%d\n",
                Layout::kTopBandHeight, Layout::kDimensionBarHeight,
                big.leftPanel.w, big.rightPanel.w, big.viewport.w,
                big.viewport.h);
}

// La tabla unica de atajos (ShortcutMap) que comparten el manejador de
// teclado y el dialogo "Personalizar atajos".
void testShortcutMap() {
    std::printf("testShortcutMap\n");
    using sk::editor::ShortcutMap;

    // La tabla por defecto no tiene dos atajos con la misma tecla + mods.
    CHECK(ShortcutMap::collisions().empty());

    // Cada accion documentada esta en la tabla; una desconocida no.
    const char* actions[] = {
        "tool1",   "tool2",    "tool3",  "tool4",   "undo",     "redo",
        "rename",  "focus",    "copy",   "cut",     "paste",    "duplicate",
        "rotateY", "rotateX",  "delete", "group",   "ungroup",  "lock",
        "anchor",  "classes",  "escape"};
    for (const char* a : actions) {
        CHECK(ShortcutMap::find(a) != nullptr);
    }
    CHECK(ShortcutMap::find("no-existe") == nullptr);

    // Las combinaciones del enunciado activan su accion.
    CHECK(ShortcutMap::matches("copy", 'C', true, false, false));
    CHECK(ShortcutMap::matches("group", 'G', true, false, false));
    CHECK(ShortcutMap::matches("ungroup", 'U', true, false, false));
    CHECK(ShortcutMap::matches("lock", 'L', true, false, false));
    CHECK(ShortcutMap::matches("anchor", 'A', false, false, true));
    CHECK(ShortcutMap::matches("classes", sk::editor::kKeyF1, false, false,
                               false));
    CHECK(ShortcutMap::matches("rename", sk::editor::kKeyF2, false, false,
                               false));
    CHECK(ShortcutMap::matches("delete", sk::editor::kKeyDelete, false, false,
                               false));
    CHECK(ShortcutMap::matches("undo", 'Z', true, false, false));
    CHECK(ShortcutMap::matches("redo", 'Y', true, false, false));
    CHECK(ShortcutMap::matches("tool2", '2', false, false, false));

    // Los modificadores de mas no impiden el atajo (Ctrl+Shift+G agrupa).
    CHECK(ShortcutMap::matches("group", 'G', true, true, true));
    // Los que faltan si: G sola no agrupa; A con Ctrl no ancla.
    CHECK(!ShortcutMap::matches("group", 'G', false, false, false));
    CHECK(!ShortcutMap::matches("anchor", 'A', true, false, false));
    // Otra tecla no coincide.
    CHECK(!ShortcutMap::matches("group", 'H', true, false, false));

    // Texto del dialogo: contiene las etiquetas y sus combinaciones.
    const std::string text = ShortcutMap::describe();
    CHECK(text.find("Agrupar") != std::string::npos);
    CHECK(text.find("Ctrl+G") != std::string::npos);
    CHECK(text.find("F1") != std::string::npos);
    CHECK(text.find("Supr") != std::string::npos);
}

// Modelo puro de Properties: comunes a la seleccion, "mixto", "por
// defecto" y filtro del buscador.
void testPropertyModel() {
    std::printf("testPropertyModel\n");
    World w;
    w.ensureStructure();
    const InstanceId ws = workspaceOf(w);
    const InstanceId a = w.create("Part", ws);
    const InstanceId b = w.create("Part", ws);
    const ClassRegistry& reg = w.registry();

    auto findRow = [](const std::vector<PropertyRow>& rows,
                      const char* n) -> const PropertyRow* {
        for (const PropertyRow& r : rows) {
            if (r.name == n) return &r;
        }
        return nullptr;
    };

    std::vector<PropertyRow> rows =
        buildPropertyRows(reg, {w.find(a), w.find(b)});

    // Name/Parent no son filas normales (el panel los pinta aparte).
    CHECK(findRow(rows, "Name") == nullptr);
    CHECK(findRow(rows, "Parent") == nullptr);

    // Size identico en ambas (los valores arrancan a cero, como siempre).
    const PropertyRow* size = findRow(rows, "Size");
    CHECK(size != nullptr);
    CHECK(size && !size->mixed);
    CHECK(size && size->value == "0 0 0");
    CHECK(size && size->section == "Transform");
    CHECK(size && size->type == PropType::Vec3);
    // El default del registro (4, 1.2, 2) difiere del valor actual: el
    // panel mostraria "restaurar".
    CHECK(size && !size->isDefault);

    // Orientation arranca a cero, que SI coincide con su default.
    const PropertyRow* orient = findRow(rows, "Orientation");
    CHECK(orient != nullptr);
    CHECK(orient && !orient->mixed);
    CHECK(orient && orient->isDefault);

    // Position se eleva al crear: comun pero distinta del default.
    const PropertyRow* pos = findRow(rows, "Position");
    CHECK(pos != nullptr);
    CHECK(pos && !pos->mixed);
    CHECK(pos && !pos->isDefault);

    // Secciones presentes.
    std::vector<std::string> sections = propertyRowSections(rows);
    CHECK(std::find(sections.begin(), sections.end(), "Transform") !=
          sections.end());
    CHECK(std::find(sections.begin(), sections.end(), "Appearance") !=
          sections.end());
    CHECK(std::find(sections.begin(), sections.end(), "Collision") !=
          sections.end());
    CHECK(std::find(sections.begin(), sections.end(), "Data") !=
          sections.end());

    // Solo en una Part cambia el tamano -> "mixto", sin valor y no default.
    PropertyValue pv;
    pv.v = Vec3{2.0f, 2.0f, 2.0f};
    CHECK(w.setProperty(a, "Size", pv));
    rows = buildPropertyRows(reg, {w.find(a), w.find(b)});
    size = findRow(rows, "Size");
    CHECK(size != nullptr);
    CHECK(size && size->mixed);
    CHECK(size && !size->isDefault);
    CHECK(size && size->value.empty());

    // Buscador: solo "Size" contiene "size".
    std::vector<PropertyRow> filtered =
        buildPropertyRows(reg, {w.find(a)}, "size");
    CHECK(filtered.size() == 1);
    if (filtered.size() == 1) CHECK(filtered[0].name == "Size");

    // Restaurar: el valor por defecto del registro no difiere de si mismo.
    const int ci = reg.indexOf("Part");
    const PropertyDef* def = reg.findProp(ci, "Size");
    CHECK(def != nullptr);
    if (def) {
        CHECK(!propertyValueDiffersFromDefault(defaultPropertyValue(*def),
                                               *def));
    }

    // Serializacion / lectura del texto canonico.
    PropertyValue parsed;
    CHECK(parsePropertyValue("1, 2, 3", PropType::Vec3, parsed));
    CHECK(parsed.v.x == 1.0f && parsed.v.y == 2.0f && parsed.v.z == 3.0f);
    CHECK(serializePropertyValue(parsed, PropType::Vec3) == "1 2 3");
    CHECK(!parsePropertyValue("no-numero", PropType::Vec3, parsed));
    CHECK(serializePropertyValue(defaultPropertyValue(*def), PropType::Vec3) ==
          "4 1.2 2");
}

// Buffer circular del panel Output (limite 5.000 lineas).
void testOutputBuffer() {
    std::printf("testOutputBuffer\n");
    OutputBuffer buf;
    CHECK(buf.capacity() == OutputBuffer::kDefaultCapacity);
    CHECK(buf.capacity() == 5000);

    for (int i = 0; i < 5001; ++i) {
        buf.add(i % 3, "l" + std::to_string(i));
    }
    CHECK(buf.size() == 5000);
    CHECK(buf.dropped() == 1);
    CHECK(buf.at(0).text == "l1");
    CHECK(buf.at(4999).text == "l5000");

    // Los tres niveles se conservan.
    buf.clear();
    CHECK(buf.size() == 0);
    CHECK(buf.dropped() == 0);
    buf.add(0, "info");
    buf.add(1, "aviso");
    buf.add(2, "error");
    CHECK(buf.size() == 3);
    CHECK(buf.at(0).level == 0);
    CHECK(buf.at(1).level == 1);
    CHECK(buf.at(2).level == 2);

    // Capacidad pequena: se descarta lo mas antiguo.
    OutputBuffer small(3);
    for (int i = 0; i < 5; ++i) small.add(0, std::to_string(i));
    CHECK(small.size() == 3);
    CHECK(small.dropped() == 2);
    CHECK(small.at(0).text == "2");
    CHECK(small.at(2).text == "4");
}

// Posicion inicial de Play/Run: SpawnLocation si existe, (0,20,0) si no.
void testPlaySpawnPosition() {
    std::printf("testPlaySpawnPosition\n");
    World w;
    w.ensureStructure();

    // Sin SpawnLocation: respaldo (0,20,0).
    const Vec3 fallback = playSpawnPosition(w);
    CHECK(fallback.x == 0.0f && fallback.y == 20.0f && fallback.z == 0.0f);

    // Con SpawnLocation: usa su posicion.
    const InstanceId ws = workspaceOf(w);
    const InstanceId spawn = w.create("SpawnLocation", ws);
    CHECK(spawn != kInvalidId);
    w.setTransform(spawn, Vec3{3.0f, 4.0f, 5.0f}, Vec3{0.0f, 0.0f, 0.0f},
                   Vec3{1.0f, 1.0f, 1.0f});
    const Vec3 at = playSpawnPosition(w);
    CHECK(at.x == 3.0f && at.y == 4.0f && at.z == 5.0f);
}

// Semantica de InputState en la que se apoya el teclado a nivel de hilo:
// onKeyDown marca flanco solo en la transicion (no cuenta doble si lo ve
// tanto el bucle como la ventana) y onKeyUp/clearKeys lo sueltan.
void testInputStateEdges() {
    std::printf("testInputStateEdges\n");
    InputState in;
    in.newFrame();
    CHECK(!in.down('W') && !in.pressed('W'));

    in.onKeyDown('W');
    CHECK(in.down('W') && in.pressed('W'));

    in.newFrame();
    CHECK(in.down('W'));        // sigue pulsada
    CHECK(!in.pressed('W'));    // el flanco dura un solo frame

    in.onKeyDown('W');          // auto-repeat / doble alimentacion
    CHECK(!in.pressed('W'));    // no vuelve a marcar flanco
    CHECK(in.down('W'));

    in.onKeyUp('W');
    CHECK(!in.down('W'));

    in.onKeyDown(InputState::kShift);
    in.onKeyDown(InputState::kControl);
    CHECK(in.shift() && in.ctrl());

    in.clearKeys();
    CHECK(!in.shift() && !in.ctrl() && !in.down('W') && !in.pressed('W'));
}

} // namespace

int main() {
    testNameUniqueness();
    testLayout();
    testShortcutMap();
    testPropertyModel();
    testOutputBuffer();
    testReparentAndCycles();
    testProtected();
    testCloneSubtree();
    testUndoRedo();
    testDimensionCommand();
    testTags();
    testCreateDimension();
    testMigrateV1();
    testSaveLoadRoundTrip();
    testNameIndexPerformance();
    testNameFuzz();
    testTreeFlatten5Levels();
    testTreeExpandCollapse();
    testTreeRemovalDepth5();
    testTree200Levels();
    testTreeReparent();
    testTreeInvalidTargets();
    testTreePreorderUnorderedEvents();
    testTreeIcons();
    testPlaySpawnPosition();
    testInputStateEdges();

    std::printf("\n%d comprobaciones, %d fallos\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
