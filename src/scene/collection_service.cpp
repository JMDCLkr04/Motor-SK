#include "collection_service.h"

#include <algorithm>

namespace sk {

std::vector<InstanceId> CollectionService::getTagged(
    const std::string& tag) const {
    std::vector<InstanceId> out;
    auto it = byTag_.find(tag);
    if (it == byTag_.end()) return out;
    out.assign(it->second.begin(), it->second.end());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> CollectionService::getTags() const {
    std::vector<std::string> out;
    out.reserve(byTag_.size());
    for (const auto& entry : byTag_) {
        if (!entry.second.empty()) out.push_back(entry.first);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool CollectionService::hasTag(InstanceId id) const {
    auto it = byId_.find(id);
    return it != byId_.end() && !it->second.empty();
}

void CollectionService::addTag(InstanceId id, const std::string& tag) {
    if (id == kInvalidId || tag.empty()) return;
    auto& ids = byId_[id];
    if (ids.count(tag)) return;
    ids.insert(tag);
    byTag_[tag].insert(id);
    if (onAdded_) onAdded_(id, tag);
}

void CollectionService::removeTag(InstanceId id, const std::string& tag) {
    auto it = byId_.find(id);
    if (it == byId_.end() || !it->second.count(tag)) return;
    it->second.erase(tag);
    if (it->second.empty()) byId_.erase(it);
    auto bt = byTag_.find(tag);
    if (bt != byTag_.end()) {
        bt->second.erase(id);
        if (bt->second.empty()) byTag_.erase(bt);
    }
    if (onRemoved_) onRemoved_(id, tag);
}

void CollectionService::clearTags(InstanceId id) {
    auto it = byId_.find(id);
    if (it == byId_.end()) return;
    std::vector<std::string> tags(it->second.begin(), it->second.end());
    for (const std::string& tag : tags) removeTag(id, tag);
}

void CollectionService::clear() {
    byTag_.clear();
    byId_.clear();
}

} // namespace sk
