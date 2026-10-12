#include "instance.h"

#include <cstdlib>

namespace sk {

const std::string Instance::empty_;

Instance::Instance(InstanceId id, const ClassDef* cls, std::string name)
    : id_(id), cls_(cls), name_(std::move(name)) {
    if (cls_) {
        className_ = cls_->name;
        values_.resize(static_cast<size_t>(ClassRegistry::get().valueCount(
            ClassRegistry::get().indexOf(cls_->name))));
    }
}

void Instance::insertChild(size_t index, InstanceId child) {
    if (index > children_.size()) index = children_.size();
    children_.insert(children_.begin() + static_cast<long long>(index), child);
}

bool Instance::removeChild(InstanceId child) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (*it == child) {
            children_.erase(it);
            return true;
        }
    }
    return false;
}

int Instance::slotOf(const std::string& prop) const {
    if (!cls_) return -1;
    return ClassRegistry::get().valueIndex(
        ClassRegistry::get().indexOf(cls_->name), prop);
}

bool Instance::propBool(const std::string& prop, bool def) const {
    const int slot = slotOf(prop);
    return slot < 0 ? def : values_[static_cast<size_t>(slot)].b;
}

long long Instance::propInt(const std::string& prop, long long def) const {
    const int slot = slotOf(prop);
    return slot < 0 ? def : values_[static_cast<size_t>(slot)].i;
}

double Instance::propFloat(const std::string& prop, double def) const {
    const int slot = slotOf(prop);
    return slot < 0 ? def : values_[static_cast<size_t>(slot)].f;
}

std::string Instance::propString(const std::string& prop,
                                 const std::string& def) const {
    const int slot = slotOf(prop);
    return slot < 0 ? def : values_[static_cast<size_t>(slot)].s;
}

Vec3 Instance::propVec3(const std::string& prop, const Vec3& def) const {
    const int slot = slotOf(prop);
    return slot < 0 ? def : values_[static_cast<size_t>(slot)].v;
}

InstanceId Instance::propRef(const std::string& prop) const {
    const int slot = slotOf(prop);
    return slot < 0 ? kInvalidId : values_[static_cast<size_t>(slot)].ref;
}

bool Instance::setPropBool(const std::string& prop, bool value) {
    const int slot = slotOf(prop);
    if (slot < 0) return false;
    values_[static_cast<size_t>(slot)].b = value;
    return true;
}

bool Instance::setPropInt(const std::string& prop, long long value) {
    const int slot = slotOf(prop);
    if (slot < 0) return false;
    values_[static_cast<size_t>(slot)].i = value;
    return true;
}

bool Instance::setPropFloat(const std::string& prop, double value) {
    const int slot = slotOf(prop);
    if (slot < 0) return false;
    values_[static_cast<size_t>(slot)].f = value;
    return true;
}

bool Instance::setPropString(const std::string& prop,
                             const std::string& value) {
    const int slot = slotOf(prop);
    if (slot < 0) return false;
    values_[static_cast<size_t>(slot)].s = value;
    return true;
}

bool Instance::setPropVec3(const std::string& prop, const Vec3& value) {
    const int slot = slotOf(prop);
    if (slot < 0) return false;
    values_[static_cast<size_t>(slot)].v = value;
    return true;
}

bool Instance::setPropRef(const std::string& prop, InstanceId value) {
    const int slot = slotOf(prop);
    if (slot < 0) return false;
    values_[static_cast<size_t>(slot)].ref = value;
    return true;
}

Vec3 Instance::position() const { return propVec3("Position"); }
Vec3 Instance::rotation() const { return propVec3("Orientation"); }
Vec3 Instance::scale() const { return propVec3("Size", Vec3{1, 1, 1}); }

void Instance::setPosition(const Vec3& p) { setPropVec3("Position", p); }
void Instance::setRotation(const Vec3& r) { setPropVec3("Orientation", r); }
void Instance::setScale(const Vec3& s) { setPropVec3("Size", s); }

// El enum se guarda como texto con el mismo orden que Shape y que las
// opciones del ClassRegistry ("Cube,Rhombus,...").
static const char* const kShapeOptions[] = {
    "Cube", "Rhombus", "Sphere", "Cylinder", "Wedge", "CornerWedge", "Capsule"};
static constexpr int kShapeOptionCount = 7;

const char* shapeOptionName(Shape shape) {
    const int i = static_cast<int>(shape);
    return (i >= 0 && i < kShapeOptionCount) ? kShapeOptions[i] : "Cube";
}

Shape shapeFromOptionName(const std::string& name) {
    for (int i = 0; i < kShapeOptionCount; ++i) {
        if (name == kShapeOptions[i]) return static_cast<Shape>(i);
    }
    return Shape::Cube;
}

Shape Instance::shape() const {
    return shapeFromOptionName(propString("Shape", "Cube"));
}

void Instance::setShape(Shape shape) {
    setPropString("Shape", shapeOptionName(shape));
}

bool Instance::isRenderable() const {
    return cls_ && isRenderableClass(className_);
}

Mat4 Instance::modelMatrix() const {
    return translate(position()) * rotateX(rotation().x) *
           rotateY(rotation().y) * rotateZ(rotation().z) * sk::scale(scale());
}

bool Instance::hasAttribute(const std::string& name) const {
    for (const Attribute& a : attributes_) {
        if (a.name == name) return true;
    }
    return false;
}

Attribute Instance::getAttribute(const std::string& name) const {
    for (const Attribute& a : attributes_) {
        if (a.name == name) return a;
    }
    return Attribute{};
}

void Instance::setAttribute(const Attribute& attr) {
    for (Attribute& a : attributes_) {
        if (a.name == attr.name) {
            a = attr;
            return;
        }
    }
    attributes_.push_back(attr);
}

void Instance::removeAttribute(const std::string& name) {
    for (auto it = attributes_.begin(); it != attributes_.end(); ++it) {
        if (it->name == name) {
            attributes_.erase(it);
            return;
        }
    }
}

void Instance::addTag(const std::string& tag) {
    if (tag.empty()) return;
    if (hasTag(tag)) return;
    tags_.push_back(tag);
}

void Instance::removeTag(const std::string& tag) {
    for (auto it = tags_.begin(); it != tags_.end(); ++it) {
        if (*it == tag) {
            tags_.erase(it);
            return;
        }
    }
}

bool Instance::hasTag(const std::string& tag) const {
    for (const std::string& t : tags_) {
        if (t == tag) return true;
    }
    return false;
}

bool isRenderableClass(const std::string& className) {
    return className == "Part" || className == "MeshPart" ||
           className == "SpawnLocation" || className == "Seat";
}

} // namespace sk
