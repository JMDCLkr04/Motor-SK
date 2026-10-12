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
#include <unordered_set>
#include <vector>

#include "scene/commands.h"
#include "scene/world.h"

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

} // namespace

int main() {
    testNameUniqueness();
    testReparentAndCycles();
    testProtected();
    testCloneSubtree();
    testUndoRedo();
    testTags();
    testCreateDimension();
    testMigrateV1();
    testSaveLoadRoundTrip();
    testNameIndexPerformance();
    testNameFuzz();

    std::printf("\n%d comprobaciones, %d fallos\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
