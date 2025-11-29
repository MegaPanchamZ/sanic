/**
 * ECS.h
 * 
 * Entity Component System for gameplay logic.
 * 
 * Features:
 * - Archetype-based storage for cache efficiency
 * - Type-safe component access
 * - System scheduling with dependencies
 * - Event/message passing
 * - Prefab support for instantiation
 * 
 * Usage:
 *   World world;
 *   Entity player = world.createEntity();
 *   world.addComponent<Transform>(player, {glm::vec3(0), glm::quat(), glm::vec3(1)});
 *   world.addComponent<Health>(player, {100.0f, 100.0f});
 *   
 *   world.registerSystem<MovementSystem>();
 *   world.update(deltaTime);
 */

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <bitset>
#include <queue>
#include <string>
#include <any>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Sanic {

// ============================================================================
// ENTITY
// ============================================================================

// Entity is just an ID
using Entity = uint32_t;
constexpr Entity INVALID_ENTITY = UINT32_MAX;

// Generation counter to detect stale entity references
struct EntityHandle {
    uint32_t index;
    uint32_t generation;
    
    bool operator==(const EntityHandle& other) const {
        return index == other.index && generation == other.generation;
    }
    
    bool isValid() const { return index != UINT32_MAX; }
};

// ============================================================================
// COMPONENT TYPE REGISTRATION
// ============================================================================

using ComponentTypeId = uint32_t;
constexpr uint32_t MAX_COMPONENTS = 64;

class ComponentRegistry {
public:
    static ComponentRegistry& getInstance() {
        static ComponentRegistry instance;
        return instance;
    }
    
    template<typename T>
    ComponentTypeId getTypeId() {
        std::type_index typeIdx(typeid(T));
        auto it = typeToId_.find(typeIdx);
        if (it != typeToId_.end()) {
            return it->second;
        }
        
        ComponentTypeId id = nextId_++;
        typeToId_[typeIdx] = id;
        idToSize_[id] = sizeof(T);
        return id;
    }
    
    size_t getComponentSize(ComponentTypeId id) const {
        auto it = idToSize_.find(id);
        return it != idToSize_.end() ? it->second : 0;
    }
    
private:
    ComponentRegistry() = default;
    std::unordered_map<std::type_index, ComponentTypeId> typeToId_;
    std::unordered_map<ComponentTypeId, size_t> idToSize_;
    ComponentTypeId nextId_ = 0;
};

// Component signature (bitset of which components an entity has)
using ComponentSignature = std::bitset<MAX_COMPONENTS>;

// ============================================================================
// COMPONENT STORAGE
// ============================================================================

// Base class for type-erased component arrays
class IComponentArray {
public:
    virtual ~IComponentArray() = default;
    virtual void entityDestroyed(Entity entity) = 0;
    virtual void* getDataPtr(Entity entity) = 0;
    virtual void copyComponent(Entity src, Entity dst) = 0;
};

// Typed component storage
template<typename T>
class ComponentArray : public IComponentArray {
public:
    void insertData(Entity entity, T component) {
        size_t newIndex = size_;
        entityToIndex_[entity] = newIndex;
        indexToEntity_[newIndex] = entity;
        
        if (newIndex >= components_.size()) {
            components_.resize(newIndex + 1);
        }
        components_[newIndex] = component;
        ++size_;
    }
    
    void removeData(Entity entity) {
        auto it = entityToIndex_.find(entity);
        if (it == entityToIndex_.end()) return;
        
        size_t indexOfRemoved = it->second;
        size_t indexOfLast = size_ - 1;
        
        // Move last element to removed position
        components_[indexOfRemoved] = components_[indexOfLast];
        
        Entity lastEntity = indexToEntity_[indexOfLast];
        entityToIndex_[lastEntity] = indexOfRemoved;
        indexToEntity_[indexOfRemoved] = lastEntity;
        
        entityToIndex_.erase(entity);
        indexToEntity_.erase(indexOfLast);
        --size_;
    }
    
    T& getData(Entity entity) {
        return components_[entityToIndex_.at(entity)];
    }
    
    T* tryGetData(Entity entity) {
        auto it = entityToIndex_.find(entity);
        if (it == entityToIndex_.end()) return nullptr;
        return &components_[it->second];
    }
    
    bool hasData(Entity entity) const {
        return entityToIndex_.find(entity) != entityToIndex_.end();
    }
    
    void entityDestroyed(Entity entity) override {
        if (entityToIndex_.find(entity) != entityToIndex_.end()) {
            removeData(entity);
        }
    }
    
    void* getDataPtr(Entity entity) override {
        auto it = entityToIndex_.find(entity);
        if (it == entityToIndex_.end()) return nullptr;
        return &components_[it->second];
    }
    
    void copyComponent(Entity src, Entity dst) override {
        if (hasData(src)) {
            insertData(dst, getData(src));
        }
    }
    
    // Iteration support
    size_t size() const { return size_; }
    T& operator[](size_t index) { return components_[index]; }
    Entity getEntity(size_t index) const { return indexToEntity_.at(index); }
    
private:
    std::vector<T> components_;
    std::unordered_map<Entity, size_t> entityToIndex_;
    std::unordered_map<size_t, Entity> indexToEntity_;
    size_t size_ = 0;
};

// ============================================================================
// BUILT-IN COMPONENTS
// ============================================================================

struct Transform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    
    Entity parent = INVALID_ENTITY;
    std::vector<Entity> children;
    
    glm::mat4 getLocalMatrix() const {
        glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
        return T * R * S;
    }
    
    glm::vec3 forward() const {
        return rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    }
    
    glm::vec3 right() const {
        return rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    }
    
    glm::vec3 up() const {
        return rotation * glm::vec3(0.0f, 1.0f, 0.0f);
    }
};

struct Name {
    std::string name;
    std::string tag;
};

struct Active {
    bool active = true;
    bool visibleInEditor = true;
};

// ============================================================================
// SYSTEM BASE CLASS
// ============================================================================

class World;

class System {
public:
    virtual ~System() = default;
    
    virtual void init(World& world) {}
    virtual void update(World& world, float deltaTime) = 0;
    virtual void fixedUpdate(World& world, float fixedDeltaTime) {}
    virtual void lateUpdate(World& world, float deltaTime) {}
    virtual void shutdown(World& world) {}
    
    // Component requirements for this system
    ComponentSignature getSignature() const { return signature_; }
    
    // Ordering
    int getPriority() const { return priority_; }
    void setPriority(int priority) { priority_ = priority; }
    
protected:
    template<typename T>
    void requireComponent() {
        ComponentTypeId id = ComponentRegistry::getInstance().getTypeId<T>();
        signature_.set(id);
    }
    
    ComponentSignature signature_;
    int priority_ = 0;
};

// ============================================================================
// EVENTS
// ============================================================================

struct Event {
    std::string name;
    std::any data;
    Entity sender = INVALID_ENTITY;
    Entity target = INVALID_ENTITY;  // INVALID_ENTITY = broadcast
};

using EventCallback = std::function<void(const Event&)>;

class EventBus {
public:
    void subscribe(const std::string& eventName, EventCallback callback) {
        subscribers_[eventName].push_back(callback);
    }
    
    void emit(const Event& event) {
        pendingEvents_.push(event);
    }
    
    void emitImmediate(const Event& event) {
        auto it = subscribers_.find(event.name);
        if (it != subscribers_.end()) {
            for (auto& callback : it->second) {
                callback(event);
            }
        }
    }
    
    void processEvents() {
        while (!pendingEvents_.empty()) {
            Event event = pendingEvents_.front();
            pendingEvents_.pop();
            emitImmediate(event);
        }
    }
    
private:
    std::unordered_map<std::string, std::vector<EventCallback>> subscribers_;
    std::queue<Event> pendingEvents_;
};

// ============================================================================
// QUERY - Iterate entities with specific components
// ============================================================================

template<typename... Components>
class Query {
public:
    Query(World& world);
    
    class Iterator {
    public:
        Iterator(World& world, size_t index, const std::vector<Entity>& entities)
            : world_(world), index_(index), entities_(entities) {}
        
        bool operator!=(const Iterator& other) const { return index_ != other.index_; }
        Iterator& operator++() { ++index_; return *this; }
        
        std::tuple<Entity, Components&...> operator*();
        
    private:
        World& world_;
        size_t index_;
        const std::vector<Entity>& entities_;
    };
    
    Iterator begin() { return Iterator(world_, 0, matchingEntities_); }
    Iterator end() { return Iterator(world_, matchingEntities_.size(), matchingEntities_); }
    
    size_t count() const { return matchingEntities_.size(); }
    
private:
    World& world_;
    std::vector<Entity> matchingEntities_;
};

// ============================================================================
// WORLD - Main ECS container
// ============================================================================

class World {
public:
    World() = default;
    ~World();
    
    // Entity management
    Entity createEntity();
    Entity createEntity(const std::string& name);
    void destroyEntity(Entity entity);
    bool isValid(Entity entity) const;
    
    // Component management
    template<typename T>
    T& addComponent(Entity entity, T component = T{}) {
        ComponentTypeId typeId = ComponentRegistry::getInstance().getTypeId<T>();
        
        // Create array if needed
        if (componentArrays_.find(typeId) == componentArrays_.end()) {
            componentArrays_[typeId] = std::make_shared<ComponentArray<T>>();
        }
        
        auto& arr = getComponentArray<T>();
        arr.insertData(entity, component);
        
        signatures_[entity].set(typeId);
        
        return arr.getData(entity);
    }
    
    template<typename T>
    void removeComponent(Entity entity) {
        ComponentTypeId typeId = ComponentRegistry::getInstance().getTypeId<T>();
        getComponentArray<T>().removeData(entity);
        signatures_[entity].reset(typeId);
    }
    
    template<typename T>
    T& getComponent(Entity entity) {
        return getComponentArray<T>().getData(entity);
    }
    
    template<typename T>
    T* tryGetComponent(Entity entity) {
        return getComponentArray<T>().tryGetData(entity);
    }
    
    template<typename T>
    bool hasComponent(Entity entity) const {
        ComponentTypeId typeId = ComponentRegistry::getInstance().getTypeId<T>();
        auto it = signatures_.find(entity);
        return it != signatures_.end() && it->second.test(typeId);
    }
    
    ComponentSignature getSignature(Entity entity) const {
        auto it = signatures_.find(entity);
        return it != signatures_.end() ? it->second : ComponentSignature{};
    }
    
    // System management
    template<typename T, typename... Args>
    T& registerSystem(Args&&... args) {
        auto system = std::make_shared<T>(std::forward<Args>(args)...);
        systems_.push_back(system);
        system->init(*this);
        
        // Sort by priority
        std::sort(systems_.begin(), systems_.end(),
            [](const auto& a, const auto& b) { return a->getPriority() < b->getPriority(); });
        
        return *system;
    }
    
    template<typename T>
    T* getSystem() {
        for (auto& sys : systems_) {
            T* typed = dynamic_cast<T*>(sys.get());
            if (typed) return typed;
        }
        return nullptr;
    }
    
    // Update
    void update(float deltaTime);
    void fixedUpdate(float fixedDeltaTime);
    void lateUpdate(float deltaTime);
    
    // Entity queries
    std::vector<Entity> getEntitiesWithSignature(ComponentSignature signature) const;
    
    template<typename... Components>
    Query<Components...> query() {
        return Query<Components...>(*this);
    }
    
    Entity findEntity(const std::string& name) const;
    std::vector<Entity> findEntitiesWithTag(const std::string& tag) const;
    
    // Events
    EventBus& getEventBus() { return eventBus_; }
    
    // Prefabs
    Entity instantiate(Entity prefab);
    Entity instantiate(Entity prefab, const glm::vec3& position, const glm::quat& rotation = glm::quat());
    
    // Scene management
    void clear();
    
    // Debug
    size_t getEntityCount() const { return livingEntities_.size(); }
    size_t getSystemCount() const { return systems_.size(); }
    
private:
    template<typename T>
    ComponentArray<T>& getComponentArray() {
        ComponentTypeId typeId = ComponentRegistry::getInstance().getTypeId<T>();
        return *static_cast<ComponentArray<T>*>(componentArrays_[typeId].get());
    }
    
    // Entity storage
    std::queue<Entity> availableEntities_;
    std::unordered_set<Entity> livingEntities_;
    uint32_t nextEntityId_ = 0;
    
    // Component storage
    std::unordered_map<ComponentTypeId, std::shared_ptr<IComponentArray>> componentArrays_;
    std::unordered_map<Entity, ComponentSignature> signatures_;
    
    // Systems
    std::vector<std::shared_ptr<System>> systems_;
    
    // Events
    EventBus eventBus_;
    
    // Pending destruction
    std::vector<Entity> pendingDestruction_;
};

// ============================================================================
// QUERY IMPLEMENTATION
// ============================================================================

template<typename... Components>
Query<Components...>::Query(World& world) : world_(world) {
    ComponentSignature required;
    ((required.set(ComponentRegistry::getInstance().getTypeId<Components>())), ...);
    
    matchingEntities_ = world.getEntitiesWithSignature(required);
}

template<typename... Components>
std::tuple<Entity, Components&...> Query<Components...>::Iterator::operator*() {
    Entity entity = entities_[index_];
    return std::tie(entity, world_.getComponent<Components>(entity)...);
}

// ============================================================================
// COMMON GAMEPLAY COMPONENTS
// ============================================================================

struct Velocity {
    glm::vec3 linear = glm::vec3(0.0f);
    glm::vec3 angular = glm::vec3(0.0f);
};

struct Health {
    float current = 100.0f;
    float max = 100.0f;
    bool invulnerable = false;
    
    float getPercent() const { return max > 0 ? current / max : 0.0f; }
    bool isAlive() const { return current > 0.0f; }
    
    void damage(float amount) {
        if (!invulnerable) {
            current = std::max(0.0f, current - amount);
        }
    }
    
    void heal(float amount) {
        current = std::min(max, current + amount);
    }
};

struct Collider {
    enum class Type { Box, Sphere, Capsule, Mesh };
    Type type = Type::Box;
    
    glm::vec3 center = glm::vec3(0.0f);
    glm::vec3 size = glm::vec3(1.0f);  // For box
    float radius = 0.5f;               // For sphere/capsule
    float height = 2.0f;               // For capsule
    
    bool isTrigger = false;
    uint32_t layer = 0;
    uint32_t mask = 0xFFFFFFFF;
};

struct RigidBody {
    enum class Type { Static, Kinematic, Dynamic };
    Type type = Type::Dynamic;
    
    float mass = 1.0f;
    float drag = 0.0f;
    float angularDrag = 0.05f;
    bool useGravity = true;
    bool isKinematic = false;
    
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 angularVelocity = glm::vec3(0.0f);
    
    // Physics body ID (for Jolt integration)
    uint32_t bodyId = UINT32_MAX;
};

struct MeshRenderer {
    uint32_t meshId = UINT32_MAX;
    uint32_t materialId = UINT32_MAX;
    bool castShadows = true;
    bool receiveShadows = true;
    uint32_t layer = 0;
};

struct Light {
    enum class Type { Directional, Point, Spot };
    Type type = Type::Point;
    
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float range = 10.0f;
    
    float innerAngle = 30.0f;  // For spot
    float outerAngle = 45.0f;
    
    bool castShadows = true;
};

struct Camera {
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    bool isOrthographic = false;
    float orthoSize = 5.0f;
    
    int priority = 0;  // Higher = more important
};

struct AudioSource {
    std::string clipPath;
    float volume = 1.0f;
    float pitch = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 100.0f;
    bool loop = false;
    bool playOnStart = false;
    bool is3D = true;
    
    // Runtime
    uint32_t sourceHandle = UINT32_MAX;
};

struct Script {
    std::string scriptPath;
    std::unordered_map<std::string, std::any> properties;
};

struct Animator {
    std::string controllerPath;
    std::unordered_map<std::string, float> floatParams;
    std::unordered_map<std::string, bool> boolParams;
    
    // Runtime
    uint32_t instanceHandle = UINT32_MAX;
};

struct ParticleEmitter {
    std::string effectPath;
    bool playOnStart = true;
    bool loop = true;
    
    // Runtime
    uint32_t systemHandle = UINT32_MAX;
};

// ============================================================================
// COMMON SYSTEMS
// ============================================================================

class TransformSystem : public System {
public:
    TransformSystem() {
        requireComponent<Transform>();
    }
    
    void update(World& world, float deltaTime) override;
    
    glm::mat4 getWorldMatrix(World& world, Entity entity);
    void setParent(World& world, Entity child, Entity parent);
    void removeFromParent(World& world, Entity child);
    
private:
    void updateWorldMatrices(World& world);
    std::unordered_map<Entity, glm::mat4> worldMatrices_;
};

class MovementSystem : public System {
public:
    MovementSystem() {
        requireComponent<Transform>();
        requireComponent<Velocity>();
    }
    
    void update(World& world, float deltaTime) override;
};

} // namespace Sanic
