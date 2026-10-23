#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace TinyEngine {

struct Entity {
    std::uint32_t id = 0;
    std::uint32_t generation = 1;

    constexpr bool operator==(const Entity&) const = default;
    constexpr explicit operator bool() const { return id != 0; }
};

struct EntityHash {
    std::size_t operator()(Entity entity) const noexcept {
        return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(entity.generation) << 32u) | entity.id);
    }
};

class Registry {
public:
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) noexcept = default;
    Registry& operator=(Registry&&) noexcept = default;

    Entity create();
    Entity create(std::uint32_t requestedId);
    bool destroy(Entity entity);
    [[nodiscard]] bool valid(Entity entity) const;
    [[nodiscard]] std::size_t size() const { return m_AliveCount; }
    [[nodiscard]] std::vector<Entity> entities() const;
    // Single-threaded update transaction. New entities can be initialized by
    // handle immediately, but join iteration only on the outermost scope exit.
    // Destruction retains components until that same commit point.
    class MutationScope {
    public:
        explicit MutationScope(Registry& registry) : m_Registry(registry) { ++m_Registry.m_MutationDepth; }
        ~MutationScope() { m_Registry.commitMutations(); }
        MutationScope(const MutationScope&) = delete;
        MutationScope& operator=(const MutationScope&) = delete;
    private:
        Registry& m_Registry;
    };
    [[nodiscard]] bool pendingDestruction(Entity entity) const { return m_PendingDestruction.contains(entity); }

    template <typename Component, typename... Args>
    Component& emplace(Entity entity, Args&&... args) {
        if (!valid(entity)) {
            throw std::invalid_argument("Cannot add a component to an invalid entity");
        }
        auto& storage = getStorage<Component>().data;
        auto [iterator, inserted] = storage.emplace(entity, Component(std::forward<Args>(args)...));
        return iterator->second;
    }

    template <typename Component>
    Component* tryGet(Entity entity) {
        if (!valid(entity)) return nullptr;
        auto& storage = getStorage<Component>().data;
        const auto iterator = storage.find(entity);
        return iterator == storage.end() ? nullptr : &iterator->second;
    }

    template <typename Component>
    const Component* tryGet(Entity entity) const {
        if (!valid(entity)) return nullptr;
        const auto* storage = tryGetStorage<Component>();
        if (!storage) return nullptr;
        const auto iterator = storage->data.find(entity);
        return iterator == storage->data.end() ? nullptr : &iterator->second;
    }

    template <typename Component>
    Component& get(Entity entity) {
        auto* component = tryGet<Component>(entity);
        if (!component) throw std::out_of_range("Entity does not have the requested component");
        return *component;
    }

    template <typename Component>
    bool has(Entity entity) const { return tryGet<Component>(entity) != nullptr; }

    template <typename Component>
    bool remove(Entity entity) {
        if (auto* storage = tryGetStorage<Component>()) return storage->data.erase(entity) != 0;
        return false;
    }

    template <typename Component, typename Function>
    void each(Function&& function) {
        auto* storage = tryGetStorage<Component>();
        if (!storage) return;
        std::vector<Entity> snapshot;
        snapshot.reserve(storage->data.size());
        for (const auto& [entity, component] : storage->data) {
            const auto alive = m_Alive.find(entity);
            if (alive != m_Alive.end() && alive->second) snapshot.push_back(entity);
        }
        for (const Entity entity : snapshot) {
            if (auto* component = tryGet<Component>(entity)) function(entity, *component);
        }
    }

    void clear();

private:
    void commitMutations();
    bool destroyImmediate(Entity entity);
    struct StorageBase {
        virtual ~StorageBase() = default;
        virtual void remove(Entity entity) = 0;
    };
    template <typename Component>
    struct Storage final : StorageBase {
        std::unordered_map<Entity, Component, EntityHash> data;
        void remove(Entity entity) override { data.erase(entity); }
    };

    template <typename Component>
    Storage<Component>& getStorage() {
        const auto type = std::type_index(typeid(Component));
        auto iterator = m_Storages.find(type);
        if (iterator == m_Storages.end()) {
            auto storage = std::make_unique<Storage<Component>>();
            auto* pointer = storage.get();
            m_Storages.emplace(type, std::move(storage));
            return *pointer;
        }
        return *static_cast<Storage<Component>*>(iterator->second.get());
    }
    template <typename Component>
    const Storage<Component>* tryGetStorage() const {
        const auto iterator = m_Storages.find(std::type_index(typeid(Component)));
        return iterator == m_Storages.end() ? nullptr : static_cast<const Storage<Component>*>(iterator->second.get());
    }
    template <typename Component>
    Storage<Component>* tryGetStorage() {
        const auto iterator = m_Storages.find(std::type_index(typeid(Component)));
        return iterator == m_Storages.end() ? nullptr : static_cast<Storage<Component>*>(iterator->second.get());
    }

    std::uint32_t m_NextId = 1;
    std::size_t m_AliveCount = 0;
    std::unordered_map<Entity, bool, EntityHash> m_Alive;
    std::unordered_set<std::uint32_t> m_UsedIds;
    std::unordered_set<Entity, EntityHash> m_PendingDestruction;
    std::size_t m_MutationDepth = 0;
    std::unordered_map<std::type_index, std::unique_ptr<StorageBase>> m_Storages;
};

} // namespace TinyEngine
