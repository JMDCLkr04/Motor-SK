#include "world.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sk {

namespace {

// --- Utilidades de texto -------------------------------------------------
std::string trim(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' ||
                     s[a] == '\n')) {
        ++a;
    }
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' ||
                     s[b - 1] == '\r' || s[b - 1] == '\n')) {
        --b;
    }
    return s.substr(a, b - a);
}

bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string trimNumber(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

bool parseVec3Text(const std::string& text, Vec3& out) {
    std::string t = text;
    for (char& c : t) {
        if (c == ',') c = ' ';
    }
    std::istringstream ss(t);
    float x = 0, y = 0, z = 0;
    if (!(ss >> x)) return false;
    ss >> y >> z;
    out = {x, y, z};
    return true;
}

// --- Valores por defecto / serializacion ---------------------------------
long long defaultInt(const char* def) { return std::atoll(def ? def : "0"); }
double defaultFloat(const char* def) { return std::atof(def ? def : "0"); }
bool defaultBool(const char* def) {
    return def && (std::strcmp(def, "true") == 0 || std::strcmp(def, "1") == 0);
}

std::string serializeVec3(const Vec3& v) {
    return trimNumber(v.x) + " " + trimNumber(v.y) + " " + trimNumber(v.z);
}

std::string serializeValue(const PropertyValue& val, PropType type) {
    switch (type) {
        case PropType::Bool:   return val.b ? "true" : "false";
        case PropType::Int:    return std::to_string(val.i);
        case PropType::Float:  return trimNumber(val.f);
        case PropType::String: return val.s;
        case PropType::Vec3:   return serializeVec3(val.v);
        case PropType::Color:  return serializeVec3(val.v);
        case PropType::Enum:   return val.s;
        case PropType::RefObject:
            return val.ref == kInvalidId ? std::string()
                                         : std::to_string(val.ref);
    }
    return {};
}

PropertyValue defaultValue(const PropertyDef& def) {
    PropertyValue v;
    switch (def.type) {
        case PropType::Bool:  v.b = defaultBool(def.def); break;
        case PropType::Int:   v.i = defaultInt(def.def); break;
        case PropType::Float: v.f = defaultFloat(def.def); break;
        case PropType::String: v.s = def.def ? def.def : ""; break;
        case PropType::Vec3:
        case PropType::Color: parseVec3Text(def.def ? def.def : "0 0 0", v.v); break;
        case PropType::Enum:  v.s = def.def ? def.def : ""; break;
        case PropType::RefObject: v.ref = kInvalidId; break;
    }
    return v;
}

bool parseValue(const std::string& text, PropType type, PropertyValue& out) {
    switch (type) {
        case PropType::Bool:
            out.b = (text == "true" || text == "1");
            break;
        case PropType::Int:
            out.i = std::atoll(text.c_str());
            break;
        case PropType::Float:
            out.f = std::atof(text.c_str());
            break;
        case PropType::String:
            out.s = text;
            break;
        case PropType::Vec3:
        case PropType::Color:
            parseVec3Text(text, out.v);
            break;
        case PropType::Enum:
            out.s = text;
            break;
        case PropType::RefObject:
            out.ref = text.empty() ? kInvalidId
                                   : static_cast<InstanceId>(
                                         std::strtoul(text.c_str(), nullptr, 10));
            break;
    }
    return true;
}

// true si el valor difiere del default (para no escribir lo redundante en
// el archivo v2).
bool valueDiffersFromDefault(const PropertyValue& val, const PropertyDef& def) {
    const PropertyValue defVal = defaultValue(def);
    switch (def.type) {
        case PropType::Bool:  return val.b != defVal.b;
        case PropType::Int:   return val.i != defVal.i;
        case PropType::Float: return std::abs(val.f - defVal.f) > 1e-6;
        case PropType::String: return val.s != defVal.s;
        case PropType::Vec3:
        case PropType::Color:
            return std::abs(val.v.x - defVal.v.x) > 1e-6 ||
                   std::abs(val.v.y - defVal.v.y) > 1e-6 ||
                   std::abs(val.v.z - defVal.v.z) > 1e-6;
        case PropType::Enum:  return val.s != defVal.s;
        case PropType::RefObject: return val.ref != defVal.ref;
    }
    return false;
}

} // namespace

// =========================================================================
// Ciclo de vida y estructura
// =========================================================================
void World::clear() {
    pool_.clear();
    roots_.clear();
    childNames_.clear();
    nameCounters_.clear();
    nextId_ = 1;
    globalScripts_ = kInvalidId;
    assetsRoot_ = kInvalidId;
    collection_.clear();
    renderDirty_ = true;
}

InstanceId World::createDimension(const std::string& name) {
    InstanceId dim = createNamed("Dimension", name, kInvalidId);

    InstanceId ws = createNamed("Workspace", kWorkspaceName, dim, true);
    createNamed("Camera", kCameraName, dim, true);
    InstanceId lighting = createNamed("Lighting", kLightingName, dim, true);
    createNamed("Scripts", kScriptsName, dim, true);

    createNamed("Sky", "Sky", lighting);
    createNamed("Atmosphere", "Atmosphere", lighting);
    createNamed("Clouds", "Clouds", lighting);
    createNamed("BloomEffect", "BloomEffect", lighting);
    createNamed("BlurEffect", "BlurEffect", lighting);
    createNamed("ColorCorrectionEffect", "ColorCorrectionEffect", lighting);
    createNamed("ColorGradingEffect", "ColorGradingEffect", lighting);
    createNamed("DepthOfFieldEffect", "DepthOfFieldEffect", lighting);
    createNamed("SunRaysEffect", "SunRaysEffect", lighting);

    (void)ws;
    return dim;
}

void World::ensureStructure() {
    if (dimensions().empty()) {
        createDimension(kDimName);
    }
    if (!exists(globalScripts_)) {
        globalScripts_ = createNamed("Scripts", kScriptsName, kInvalidId, true);
        createNamed("Folder", "Server", globalScripts_, true);
        createNamed("Folder", "Shared", globalScripts_, true);
        createNamed("Folder", "Player", globalScripts_, true);
        createNamed("Folder", "Character", globalScripts_, true);
    }
    if (!exists(assetsRoot_)) {
        assetsRoot_ = createNamed("Folder", kAssetsName, kInvalidId, true);
        createNamed("Folder", "Models", assetsRoot_, true);
        createNamed("Folder", "Meshes", assetsRoot_, true);
        createNamed("Folder", "Images", assetsRoot_, true);
        createNamed("Folder", "Textures", assetsRoot_, true);
        createNamed("Folder", "Audio", assetsRoot_, true);
        createNamed("Folder", "Animations", assetsRoot_, true);
        createNamed("Folder", "ModuleScripts", assetsRoot_, true);
    }
}

// =========================================================================
// Consultas
// =========================================================================
Instance* World::find(InstanceId id) {
    auto it = pool_.find(id);
    return it == pool_.end() ? nullptr : &it->second;
}

const Instance* World::find(InstanceId id) const {
    auto it = pool_.find(id);
    return it == pool_.end() ? nullptr : &it->second;
}

InstanceId World::findChild(InstanceId parent,
                            const std::string& name) const {
    const Instance* p = find(parent);
    if (!p) return kInvalidId;
    auto it = childNames_.find(parent);
    if (it == childNames_.end()) return kInvalidId;
    auto nit = it->second.find(name);
    return nit == it->second.end() ? kInvalidId : nit->second;
}

void World::indexChild(InstanceId parent, const std::string& name,
                       InstanceId id) {
    childNames_[parent][name] = id;
}

void World::unindexChild(InstanceId parent, const std::string& name) {
    auto it = childNames_.find(parent);
    if (it == childNames_.end()) return;
    it->second.erase(name);
    if (it->second.empty()) childNames_.erase(it);
}

void World::rebuildNameIndex() {
    childNames_.clear();
    nameCounters_.clear();
    for (const auto& entry : pool_) {
        const Instance& inst = entry.second;
        indexChild(inst.parentId(), inst.name(), entry.first);
    }
}

void World::setNameIndexed(InstanceId id, const std::string& name) {
    Instance* inst = find(id);
    if (!inst) return;
    const InstanceId parent = inst->parentId();
    unindexChild(parent, inst->name());
    inst->setName(name);
    indexChild(parent, name, id);
}

std::vector<InstanceId> World::dimensions() const {
    std::vector<InstanceId> out;
    for (InstanceId id : roots_) {
        const Instance* inst = find(id);
        if (inst && inst->className() == "Dimension") out.push_back(id);
    }
    return out;
}

size_t World::aliveCount() const { return pool_.size(); }

bool World::isProtected(InstanceId id) const {
    const Instance* inst = find(id);
    return inst && inst->isProtected();
}

std::vector<InstanceId> World::subtree(InstanceId id) const {
    std::vector<InstanceId> out;
    if (!exists(id)) return out;
    std::vector<InstanceId> stack{id};
    while (!stack.empty()) {
        InstanceId cur = stack.back();
        stack.pop_back();
        out.push_back(cur);
        const Instance* inst = find(cur);
        if (inst) {
            for (InstanceId c : inst->children()) stack.push_back(c);
        }
    }
    return out;
}

// =========================================================================
// Nombres unicos
// =========================================================================
std::string World::uniqueName(InstanceId parent, const std::string& base) const {
    std::string root = base.empty() ? "Object" : base;
    auto namesIt = childNames_.find(parent);
    if (namesIt == childNames_.end() ||
        namesIt->second.find(root) == namesIt->second.end()) {
        return root;
    }
    const auto& names = namesIt->second;
    int& counter = nameCounters_[parent][root];
    if (counter < 1) counter = 1;
    for (;;) {
        const std::string cand = root + std::to_string(counter);
        if (names.find(cand) == names.end()) {
            ++counter;
            return cand;
        }
        ++counter;
    }
}

// =========================================================================
// Alta / baja / reubicacion / clonado
// =========================================================================
InstanceId World::create(const std::string& className, InstanceId parent) {
    const ClassDef* cls = registry().find(className);
    if (!cls) return kInvalidId;
    if (parent != kInvalidId && !exists(parent)) return kInvalidId;

    InstanceId id = allocateId();
    std::string name = uniqueName(parent, className);
    Instance inst(id, cls, name);

    // Valor por defecto: los Parts aparecen un poco elevados.
    if (isRenderableClass(className)) inst.setPosition(Vec3{0.0f, 0.5f, 0.0f});

    pool_.emplace(id, std::move(inst));
    adoptChild(parent, id, parent == kInvalidId ? roots_.size()
                                                : find(parent)->children().size());
    emitAdded(id, parent);
    invalidateRender();
    return id;
}

InstanceId World::createNamed(const std::string& className,
                              const std::string& name, InstanceId parent,
                              bool protectedNode) {
    const ClassDef* cls = registry().find(className);
    if (!cls) return kInvalidId;
    InstanceId id = allocateId();
    Instance inst(id, cls, name);
    inst.setProtected(protectedNode);
    pool_.emplace(id, std::move(inst));
    adoptChild(parent, id, parent == kInvalidId ? roots_.size()
                                                : find(parent)->children().size());
    emitAdded(id, parent);
    invalidateRender();
    return id;
}

void World::adoptChild(InstanceId parent, InstanceId child, size_t index) {
    Instance* c = find(child);
    if (!c) return;
    unindexChild(c->parentId(), c->name());
    c->setParentId(parent);
    if (parent == kInvalidId) {
        if (index > roots_.size()) index = roots_.size();
        roots_.insert(roots_.begin() + static_cast<long long>(index), child);
    } else {
        Instance* p = find(parent);
        if (p) p->insertChild(index, child);
    }
    indexChild(parent, c->name(), child);
}

void World::detachChild(InstanceId parent, InstanceId child) {
    Instance* c = find(child);
    if (c) {
        unindexChild(parent, c->name());
        c->setParentId(kInvalidId);
    }
    if (parent == kInvalidId) {
        roots_.erase(std::remove(roots_.begin(), roots_.end(), child),
                     roots_.end());
    } else {
        Instance* p = find(parent);
        if (p) p->removeChild(child);
    }
}

void World::removeRoot(InstanceId id) {
    roots_.erase(std::remove(roots_.begin(), roots_.end(), id), roots_.end());
}

bool World::remove(InstanceId id) {
    Instance* inst = find(id);
    if (!inst) return false;
    if (inst->isProtected()) return false;

    std::vector<InstanceId> all = subtree(id);
    const InstanceId parent = inst->parentId();

    detachChild(parent, id);
    for (InstanceId sub : all) {
        const Instance* s = find(sub);
        if (s) unindexChild(s->parentId(), s->name());
        collection_.clearTags(sub);
        pool_.erase(sub);
        emitRemoved(sub);
    }
    invalidateRender();
    return true;
}

bool World::reparent(InstanceId id, InstanceId newParent) {
    Instance* inst = find(id);
    if (!inst) return false;
    if (inst->isProtected()) return false;
    if (newParent != kInvalidId && !exists(newParent)) return false;
    if (newParent == id) return false;

    // Ciclo: el destino no puede estar dentro del subarbol de id.
    if (newParent != kInvalidId) {
        std::vector<InstanceId> sub = subtree(id);
        if (std::find(sub.begin(), sub.end(), newParent) != sub.end()) {
            return false;
        }
    }

    const InstanceId oldParent = inst->parentId();
    if (oldParent == newParent) return false;

    detachChild(oldParent, id);
    std::string newName = uniqueName(newParent, inst->name());
    inst = find(id);
    inst->setName(newName);
    adoptChild(newParent, id, newParent == kInvalidId
                                 ? roots_.size()
                                 : find(newParent)->children().size());
    emitReparented(id, oldParent, newParent);
    invalidateRender();
    return true;
}

bool World::rename(InstanceId id, const std::string& name) {
    Instance* inst = find(id);
    if (!inst) return false;
    if (inst->isProtected()) return false;
    const std::string clean = trim(name);
    if (clean.empty()) return false;
    const std::string unique = uniqueName(inst->parentId(), clean);
    if (unique == inst->name()) return false;
    setNameIndexed(id, unique);
    emitChanged(id);
    return true;
}

InstanceId World::clone(InstanceId id, InstanceId parent) {
    const Instance* src = find(id);
    if (!src) return kInvalidId;
    if (parent != kInvalidId && !exists(parent)) return kInvalidId;

    auto cloneRec = [&](auto&& self, InstanceId sid, InstanceId dstParent,
                        bool isRoot) -> InstanceId {
        const Instance* s = find(sid);
        if (!s) return kInvalidId;
        InstanceId nid = allocateId();
        Instance copy(nid, s->cls(),
                      isRoot ? uniqueName(dstParent, s->name()) : s->name());
        copy.mutableValues() = s->values();
        copy.mutableAttributes() = s->getAttributes();
        copy.setTags(s->getTags());
        copy.setProtected(false);
        pool_.emplace(nid, std::move(copy));
        adoptChild(dstParent, nid,
                   dstParent == kInvalidId ? roots_.size()
                                           : find(dstParent)->children().size());
        emitAdded(nid, dstParent);
        const std::vector<InstanceId> kids = s->children();
        for (InstanceId k : kids) self(self, k, nid, false);
        return nid;
    };

    InstanceId root = cloneRec(cloneRec, id, parent, true);
    for (InstanceId sub : subtree(root)) {
        Instance* c = find(sub);
        if (c) {
            for (const std::string& t : c->getTags()) collection_.addTag(sub, t);
        }
    }
    invalidateRender();
    return root;
}

// =========================================================================
// Propiedades
// =========================================================================
bool World::setBool(InstanceId id, const std::string& prop, bool value) {
    Instance* i = find(id);
    if (!i || !i->setPropBool(prop, value)) return false;
    emitChanged(id);
    return true;
}

bool World::setInt(InstanceId id, const std::string& prop, long long value) {
    Instance* i = find(id);
    if (!i || !i->setPropInt(prop, value)) return false;
    emitChanged(id);
    return true;
}

bool World::setFloat(InstanceId id, const std::string& prop, double value) {
    Instance* i = find(id);
    if (!i || !i->setPropFloat(prop, value)) return false;
    emitChanged(id);
    return true;
}

bool World::setString(InstanceId id, const std::string& prop,
                      const std::string& value) {
    Instance* i = find(id);
    if (!i || !i->setPropString(prop, value)) return false;
    emitChanged(id);
    return true;
}

bool World::setVec3(InstanceId id, const std::string& prop, const Vec3& value) {
    Instance* i = find(id);
    if (!i || !i->setPropVec3(prop, value)) return false;
    emitChanged(id);
    invalidateRender();
    return true;
}

bool World::setRef(InstanceId id, const std::string& prop, InstanceId value) {
    Instance* i = find(id);
    if (!i || !i->setPropRef(prop, value)) return false;
    emitChanged(id);
    return true;
}

bool World::setTransform(InstanceId id, const Vec3& position,
                         const Vec3& rotation, const Vec3& scale) {
    Instance* i = find(id);
    if (!i) return false;
    i->setPosition(position);
    i->setRotation(rotation);
    i->setScale(scale);
    emitChanged(id);
    invalidateRender();
    return true;
}

bool World::setAttribute(InstanceId id, const Attribute& attr) {
    Instance* i = find(id);
    if (!i || attr.name.empty()) return false;
    i->setAttribute(attr);
    emitChanged(id);
    return true;
}

bool World::removeAttribute(InstanceId id, const std::string& name) {
    Instance* i = find(id);
    if (!i || !i->hasAttribute(name)) return false;
    i->removeAttribute(name);
    emitChanged(id);
    return true;
}

bool World::setTags(InstanceId id, const std::vector<std::string>& tags) {
    Instance* i = find(id);
    if (!i) return false;
    std::vector<std::string> old = i->getTags();
    for (const std::string& t : old) {
        if (std::find(tags.begin(), tags.end(), t) == tags.end()) {
            collection_.removeTag(id, t);
        }
    }
    i->setTags(tags);
    for (const std::string& t : tags) collection_.addTag(id, t);
    emitChanged(id);
    return true;
}

bool World::addTag(InstanceId id, const std::string& tag) {
    Instance* i = find(id);
    if (!i || tag.empty() || i->hasTag(tag)) return false;
    i->addTag(tag);
    collection_.addTag(id, tag);
    emitChanged(id);
    return true;
}

bool World::removeTag(InstanceId id, const std::string& tag) {
    Instance* i = find(id);
    if (!i || !i->hasTag(tag)) return false;
    i->removeTag(tag);
    collection_.removeTag(id, tag);
    emitChanged(id);
    return true;
}

// =========================================================================
// Eventos y cache de render
// =========================================================================
void World::emitAdded(InstanceId id, InstanceId parent) {
    if (!suppressEvents_ && onAdded_) onAdded_(id, parent);
}

void World::emitRemoved(InstanceId id) {
    if (!suppressEvents_ && onRemoved_) onRemoved_(id);
}

void World::emitChanged(InstanceId id) {
    if (!suppressEvents_ && onChanged_) onChanged_(id);
    invalidateRender();
}

void World::emitReparented(InstanceId id, InstanceId oldP, InstanceId newP) {
    if (!suppressEvents_ && onReparented_) onReparented_(id, oldP, newP);
}

const std::vector<RenderPart>& World::renderParts() const {
    if (renderDirty_) refreshRenderCache();
    return renderCache_;
}

void World::refreshRenderCache() const {
    renderCache_.clear();
    auto walk = [&](auto&& self, InstanceId id) -> void {
        const Instance* inst = find(id);
        if (!inst) return;
        if (inst->isRenderable()) {
            RenderPart rp;
            rp.model = inst->modelMatrix();
            rp.shape = static_cast<int>(inst->shape());
            rp.id = id;
            renderCache_.push_back(rp);
        }
        for (InstanceId c : inst->children()) self(self, c);
    };
    for (InstanceId r : roots_) walk(walk, r);
    renderDirty_ = false;
}

Instance& World::insertInstance(Instance inst) {
    InstanceId id = inst.id();
    auto res = pool_.emplace(id, std::move(inst));
    if (id >= nextId_) nextId_ = id + 1;
    renderDirty_ = true;
    // Indice provisional; adoptChild/rebuildNameIndex lo reasignan al padre
    // definitivo cuando corresponda.
    indexChild(res.first->second.parentId(), res.first->second.name(), id);
    return res.first->second;
}

bool World::setValue(InstanceId id, int slot, const PropertyValue& value) {
    Instance* i = find(id);
    if (!i) return false;
    const int count = registry().valueCount(registry().indexOf(i->className()));
    if (slot < 0 || slot >= count) return false;
    i->mutableValues()[static_cast<size_t>(slot)] = value;
    emitChanged(id);
    return true;
}

void World::eraseInstance(InstanceId id) {
    pool_.erase(id);
    invalidateRender();
}

void World::restoreInstances(const std::vector<Instance>& nodes,
                             InstanceId parent, size_t index) {
    for (const Instance& n : nodes) insertInstance(n);
    if (!nodes.empty()) {
        adoptChild(parent, nodes.front().id(), index);
    }
    // Los descendientes conservan su enlace interno; registra todos sus
    // nombres en el indice.
    for (const Instance& n : nodes) {
        const Instance* inst = find(n.id());
        if (inst) indexChild(inst->parentId(), inst->name(), inst->id());
    }
    invalidateRender();
}

void World::rebuildCollection() {
    collection_.clear();
    for (const auto& entry : pool_) {
        for (const std::string& t : entry.second.getTags()) {
            collection_.addTag(entry.first, t);
        }
    }
}

// =========================================================================
// Persistencia
// =========================================================================
bool World::saveToFile(const std::string& path) const {
    std::ostringstream out;
    out << "version: 2\n";

    const ClassRegistry& reg = registry();
    std::function<void(InstanceId)> write = [&](InstanceId id) {
        const Instance* inst = find(id);
        if (!inst) return;
        out << "\n";
        out << "instancia: " << inst->id() << "\n";
        out << "clase: " << inst->className() << "\n";
        out << "nombre: " << inst->name() << "\n";
        if (inst->parentId() != kInvalidId) {
            out << "parent: " << inst->parentId() << "\n";
        }
        if (inst->isProtected()) out << "protegido: true\n";

        const int ci = reg.indexOf(inst->className());
        const int count = reg.valueCount(ci);
        for (int v = 0; v < count; ++v) {
            const PropertyDef& pd = reg.valueProp(ci, v);
            const PropertyValue& val = inst->valueBySlot(v);
            if (!valueDiffersFromDefault(val, pd)) continue;
            out << "prop: " << pd.name << "|" << serializeValue(val, pd.type)
                << "\n";
        }
        for (const Attribute& a : inst->getAttributes()) {
            out << "atributo: " << a.name << "|" << a.type << "|" << a.value
                << "\n";
        }
        for (const std::string& t : inst->getTags()) {
            out << "tag: " << t << "\n";
        }
        for (InstanceId c : inst->children()) write(c);
    };
    for (InstanceId r : roots_) write(r);

    const std::string text = out.str();
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!f) return false;
    }
    // Reemplazo atomico: renombrar sobre el destino (en Windows y POSIX
    // std::filesystem::rename sustituye el existente). Si falla, se
    // conserva el .tmp para el siguiente intento.
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

bool World::loadFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        // Recuperacion: si el guardado anterior se interrumpio tras
        // escribir el .tmp, se promociona a archivo definitivo y se carga.
        const std::string tmp = path + ".tmp";
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) return false;
        f.open(path, std::ios::binary);
        if (!f) return false;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(line);

    int version = 1;
    for (const std::string& l : lines) {
        if (startsWith(l, "version:")) {
            version = std::atoi(trim(l.substr(8)).c_str());
            break;
        }
    }

    clear();
    suppressEvents_ = true;
    const bool ok = (version >= 2) ? loadV2(lines) : loadV1(lines);
    suppressEvents_ = false;
    rebuildNameIndex();
    rebuildCollection();
    invalidateRender();
    return ok;
}

bool World::loadV2(const std::vector<std::string>& lines) {
    struct Rec {
        InstanceId id;
        std::string cls;
        std::string name;
        InstanceId parent;
        bool protectedNode;
        std::vector<std::pair<std::string, std::string>> props;
        std::vector<Attribute> attrs;
        std::vector<std::string> tags;
    };
    std::vector<Rec> recs;
    Rec cur;
    bool inRec = false;
    auto flush = [&]() {
        if (inRec) recs.push_back(cur);
        cur = Rec{};
        inRec = false;
    };

    for (const std::string& raw : lines) {
        const std::string l = trim(raw);
        if (l.empty()) continue;
        if (startsWith(l, "instancia:")) {
            flush();
            cur.id = static_cast<InstanceId>(
                std::strtoul(trim(l.substr(10)).c_str(), nullptr, 10));
            cur.parent = kInvalidId;
            cur.protectedNode = false;
            inRec = true;
        } else if (!inRec) {
            continue;
        } else if (startsWith(l, "clase:")) {
            cur.cls = trim(l.substr(6));
        } else if (startsWith(l, "nombre:")) {
            cur.name = trim(l.substr(7));
        } else if (startsWith(l, "parent:")) {
            cur.parent = static_cast<InstanceId>(
                std::strtoul(trim(l.substr(7)).c_str(), nullptr, 10));
        } else if (startsWith(l, "protegido:")) {
            cur.protectedNode = trim(l.substr(10)) == "true";
        } else if (startsWith(l, "prop:")) {
            const std::string body = trim(l.substr(5));
            const size_t bar = body.find('|');
            if (bar != std::string::npos) {
                cur.props.emplace_back(body.substr(0, bar),
                                       body.substr(bar + 1));
            }
        } else if (startsWith(l, "atributo:")) {
            const std::string body = trim(l.substr(9));
            const size_t b1 = body.find('|');
            const size_t b2 = (b1 == std::string::npos)
                                  ? std::string::npos
                                  : body.find('|', b1 + 1);
            Attribute a;
            if (b2 == std::string::npos) {
                a.name = body;
                a.type = "text";
            } else {
                a.name = body.substr(0, b1);
                a.type = body.substr(b1 + 1, b2 - b1 - 1);
                a.value = body.substr(b2 + 1);
            }
            cur.attrs.push_back(std::move(a));
        } else if (startsWith(l, "tag:")) {
            cur.tags.push_back(trim(l.substr(4)));
        }
    }
    flush();

    if (recs.empty()) {
        ensureStructure();
        return true;
    }

    const ClassRegistry& reg = registry();
    for (const Rec& r : recs) {
        const ClassDef* cls = reg.find(r.cls);
        if (!cls) continue;
        Instance inst(r.id, cls, r.name);
        inst.setProtected(r.protectedNode);
        const int ci = reg.indexOf(r.cls);
        for (const auto& kv : r.props) {
            const int slot = reg.valueIndex(ci, kv.first);
            const PropertyDef* pd = reg.findProp(ci, kv.first);
            if (slot < 0 || !pd || pd->readOnly) continue;
            parseValue(kv.second, pd->type, inst.mutableValues()[slot]);
        }
        inst.mutableAttributes() = r.attrs;
        inst.setTags(r.tags);
        insertInstance(std::move(inst));
    }

    // Adopta la jerarquia; los huerfanos van a la raiz.
    for (const Rec& r : recs) {
        Instance* inst = find(r.id);
        if (!inst) continue;
        if (r.parent != kInvalidId && exists(r.parent)) {
            Instance* p = find(r.parent);
            p->insertChild(p->children().size(), r.id);
            inst->setParentId(r.parent);
        } else {
            roots_.push_back(r.id);
            inst->setParentId(kInvalidId);
        }
    }

    // Detecta los nodos globales por rol.
    for (const Rec& r : recs) {
        const Instance* inst = find(r.id);
        if (!inst || inst->parentId() != kInvalidId) continue;
        if (inst->className() == "Scripts") globalScripts_ = r.id;
        if (inst->name() == kAssetsName) assetsRoot_ = r.id;
    }
    return true;
}

bool World::loadV1(const std::vector<std::string>& lines) {
    // Estructura antigua: bloques de objeto (nombre/clase/padre/protegido/
    // posicion/.../forma/atributo/tag). Se migra a v2: todo el contenido va
    // bajo Workspace de dimension01, conservando jerarquia entre objetos.
    struct Rec {
        std::string name;
        std::string cls;
        std::string parent;
        bool protectedNode = false;
        Vec3 position{}, rotation{}, scale{1, 1, 1};
        bool hasScale = false;
        std::string shape;      // "cubo", ...
        std::string script;
        std::vector<Attribute> attrs;
        std::vector<std::string> tags;
        bool hasBoolProps = false;
        bool anchored = false, canCollide = true, castShadow = true,
             locked = false;
        double reflectance = 0.0, transparency = 0.0;
    };
    std::vector<Rec> recs;
    Rec cur;
    bool inRec = false;
    auto flush = [&]() {
        if (inRec) recs.push_back(cur);
        cur = Rec{};
        inRec = false;
    };

    for (const std::string& raw : lines) {
        const std::string l = trim(raw);
        if (l.empty()) continue;
        if (startsWith(l, "objeto:")) {
            flush();
            cur.name = trim(l.substr(7));
            inRec = true;
        } else if (!inRec) {
            continue;
        } else if (startsWith(l, "clase:")) {
            cur.cls = trim(l.substr(6));
        } else if (startsWith(l, "parent:")) {
            cur.parent = trim(l.substr(7));
        } else if (startsWith(l, "protegido:")) {
            cur.protectedNode = trim(l.substr(10)) == "true" ||
                                trim(l.substr(10)) == "1";
        } else if (startsWith(l, "posicion:")) {
            parseVec3Text(trim(l.substr(9)), cur.position);
        } else if (startsWith(l, "rotacion:")) {
            parseVec3Text(trim(l.substr(9)), cur.rotation);
        } else if (startsWith(l, "escala:")) {
            parseVec3Text(trim(l.substr(7)), cur.scale);
            cur.hasScale = true;
        } else if (startsWith(l, "forma:")) {
            cur.shape = trim(l.substr(6));
        } else if (startsWith(l, "script:")) {
            cur.script = trim(l.substr(7));
        } else if (startsWith(l, "anchored:")) {
            cur.anchored = trim(l.substr(9)) == "true";
            cur.hasBoolProps = true;
        } else if (startsWith(l, "cancollide:")) {
            cur.canCollide = trim(l.substr(11)) == "true";
            cur.hasBoolProps = true;
        } else if (startsWith(l, "castshadow:")) {
            cur.castShadow = trim(l.substr(11)) == "true";
            cur.hasBoolProps = true;
        } else if (startsWith(l, "locked:")) {
            cur.locked = trim(l.substr(7)) == "true";
            cur.hasBoolProps = true;
        } else if (startsWith(l, "reflectance:")) {
            cur.reflectance = std::atof(trim(l.substr(12)).c_str());
            cur.hasBoolProps = true;
        } else if (startsWith(l, "transparency:")) {
            cur.transparency = std::atof(trim(l.substr(13)).c_str());
            cur.hasBoolProps = true;
        } else if (startsWith(l, "atributo:")) {
            const std::string body = trim(l.substr(9));
            const size_t b1 = body.find('|');
            const size_t b2 = (b1 == std::string::npos)
                                  ? std::string::npos
                                  : body.find('|', b1 + 1);
            Attribute a;
            if (b2 == std::string::npos) {
                a.name = body;
                a.type = "text";
            } else {
                a.name = body.substr(0, b1);
                a.type = body.substr(b1 + 1, b2 - b1 - 1);
                a.value = body.substr(b2 + 1);
            }
            cur.attrs.push_back(std::move(a));
        } else if (startsWith(l, "tag:")) {
            cur.tags.push_back(trim(l.substr(4)));
        }
    }
    flush();

    // Estructura nueva y mapa viejo (nombre) -> id nuevo.
    ensureStructure();
    const InstanceId dimension = dimensions().empty() ? kInvalidId
                                                      : dimensions().front();
    const InstanceId workspace = findChild(dimension, kWorkspaceName);
    const InstanceId lighting = findChild(dimension, kLightingName);

    std::unordered_map<std::string, InstanceId> byName;
    if (dimension != kInvalidId) byName[kDimName] = dimension;
    if (workspace != kInvalidId) byName[kWorkspaceName] = workspace;
    byName[kCameraName] = findChild(dimension, kCameraName);
    byName[kLightingName] = lighting;
    byName[kScriptsName] = findChild(dimension, kScriptsName);

    auto isStructural = [&](const Rec& r) {
        return r.cls == "Dimension" || r.cls == "Workspace" ||
               r.cls == "Camera" || r.cls == "Lighting" ||
               r.cls == "Scripts" || r.name == kDimName ||
               r.name == kWorkspaceName || r.name == kCameraName ||
               r.name == kLightingName || r.name == kScriptsName ||
               r.name == "Sky" || r.name == "Atmosphere" ||
               r.name == "Clouds" || r.cls == "PostEffect" ||
               startsWith(r.cls, "Bloom") || startsWith(r.cls, "Blur") ||
               startsWith(r.cls, "ColorCorrection") ||
               startsWith(r.cls, "ColorGrading") ||
               startsWith(r.cls, "DepthOfField") ||
               startsWith(r.cls, "SunRays");
    };

    for (const Rec& r : recs) {
        if (isStructural(r)) continue;
        const ClassDef* cls = registry().find(r.cls);
        if (!cls || r.cls == "Part" || isRenderableClass(r.cls)) {
            if (!cls) cls = registry().find("Part");
        }
        if (!cls) continue;

        // Destino: el Workspace (o el padre migrado si es contenido).
        InstanceId parent = workspace;
        if (!r.parent.empty()) {
            auto it = byName.find(r.parent);
            if (it != byName.end() && it->second != kInvalidId) {
                const Instance* p = find(it->second);
                if (p && p->className() != "Lighting" &&
                    p->className() != "Camera" && p->className() != "Scripts" &&
                    p->className() != "Dimension") {
                    parent = it->second;
                }
            }
        }

        InstanceId id = createNamed(r.cls, r.name, parent);
        if (id == kInvalidId) continue;
        byName[r.name] = id;

        Instance* inst = find(id);
        if (isRenderableClass(r.cls)) {
            inst->setPosition(r.position);
            inst->setRotation(r.rotation);
            inst->setScale(r.hasScale ? r.scale : Vec3{4.0f, 1.2f, 2.0f});
            if (!r.shape.empty()) {
                inst->setShape(shapeFromName(r.shape));
            }
            if (r.hasBoolProps) {
                inst->setPropBool("Anchored", r.anchored);
                inst->setPropBool("CanCollide", r.canCollide);
                inst->setPropBool("CastShadow", r.castShadow);
                inst->setPropBool("Locked", r.locked);
                inst->setPropFloat("Reflectance", r.reflectance);
                inst->setPropFloat("Transparency", r.transparency);
            }
        }
        inst->mutableAttributes() = r.attrs;
        if (!r.script.empty()) {
            Attribute a;
            a.name = "Script";
            a.type = "text";
            a.value = r.script;
            inst->setAttribute(a);
        }
        inst->setTags(r.tags);
    }
    return true;
}

} // namespace sk
