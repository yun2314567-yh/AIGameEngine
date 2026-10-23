#include "TinyEngine/ECS.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace TinyEngine {

Entity Registry::create() {
    if (m_NextId == 0 || m_NextId == std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("Entity ID space exhausted");
    }
    const Entity entity{m_NextId++};
    m_Alive.emplace(entity, m_MutationDepth == 0);
    m_UsedIds.emplace(entity.id);
    if (m_MutationDepth == 0) ++m_AliveCount;
    return entity;
}

Entity Registry::create(std::uint32_t requestedId) {
    if (requestedId == 0 || requestedId == std::numeric_limits<std::uint32_t>::max() || m_UsedIds.contains(requestedId)) {
        throw std::invalid_argument("Requested entity ID is invalid or has already been used");
    }
    m_NextId = requestedId >= m_NextId ? requestedId + 1 : m_NextId;
    const Entity entity{requestedId};
    m_Alive.emplace(entity, m_MutationDepth == 0);
    m_UsedIds.emplace(entity.id);
    if (m_MutationDepth == 0) ++m_AliveCount;
    return entity;
}

bool Registry::destroy(Entity entity) {
    if (!valid(entity)) return false;
    if (m_MutationDepth != 0) return m_PendingDestruction.insert(entity).second;
    return destroyImmediate(entity);
}

bool Registry::destroyImmediate(Entity entity) {
    const auto iterator = m_Alive.find(entity);
    if (iterator == m_Alive.end()) return false;
    if (iterator->second) --m_AliveCount;
    m_Alive.erase(entity);
    for (auto& [type, storage] : m_Storages) storage->remove(entity);
    return true;
}

void Registry::commitMutations() {
    if (--m_MutationDepth != 0) return;
    for (const auto entity : m_PendingDestruction) destroyImmediate(entity);
    m_PendingDestruction.clear();
    for (auto& [entity, published] : m_Alive) {
        if (!published) { published = true; ++m_AliveCount; }
    }
}

bool Registry::valid(Entity entity) const { return entity && m_Alive.contains(entity); }

std::vector<Entity> Registry::entities() const {
    std::vector<Entity> result;
    result.reserve(m_Alive.size());
    for (const auto& entry : m_Alive) if (entry.second) result.push_back(entry.first);
    std::sort(result.begin(), result.end(), [](Entity lhs, Entity rhs) { return lhs.id < rhs.id; });
    return result;
}

void Registry::clear() {
    m_Alive.clear();
    m_AliveCount = 0;
    m_Storages.clear();
    m_PendingDestruction.clear();
}

} // namespace TinyEngine
