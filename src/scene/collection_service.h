#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "instance.h"

namespace sk {

// Servicio de colecciones por etiqueta (tag). Mantiene un indice
// etiqueta -> instancias y avisa de altas/bajas. API minimalista tipo
// Roblox para consultar y para engancharse a cambios.
class CollectionService {
public:
    using TagEvent = std::function<void(InstanceId id, const std::string& tag)>;

    // Instancias etiquetadas con `tag` (vacio si ninguna).
    std::vector<InstanceId> getTagged(const std::string& tag) const;
    std::vector<std::string> getTags() const;
    bool hasTag(InstanceId id) const;

    // Alta/baja (idempotentes). Emiten el evento correspondiente solo si
    // el estado cambia.
    void addTag(InstanceId id, const std::string& tag);
    void removeTag(InstanceId id, const std::string& tag);
    // Quita todas las etiquetas de una instancia (al borrarla).
    void clearTags(InstanceId id);
    void clear();

    void onTagAdded(TagEvent cb) { onAdded_ = std::move(cb); }
    void onTagRemoved(TagEvent cb) { onRemoved_ = std::move(cb); }

private:
    std::unordered_map<std::string, std::unordered_set<InstanceId>> byTag_;
    std::unordered_map<InstanceId, std::unordered_set<std::string>> byId_;
    TagEvent onAdded_;
    TagEvent onRemoved_;
};

} // namespace sk
