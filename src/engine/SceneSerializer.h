/**
 * SceneSerializer.h
 * 
 * Scene serialization for save/load and level editing.
 * 
 * Features:
 * - JSON scene format for human-readable editing
 * - Binary scene format for fast loading
 * - Prefab system for reusable objects
 * - Async loading with progress callbacks
 * - Scene diffing for networking
 */

#pragma once

#include "ECS.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <fstream>
#include <unordered_map>

namespace Sanic {

// ============================================================================
// SCENE FORMAT
// ============================================================================

constexpr uint32_t SCENE_MAGIC = 0x534E5343;  // "SNSC"
constexpr uint32_t SCENE_VERSION = 1;
constexpr uint32_t PREFAB_MAGIC = 0x534E5046;  // "SNPF"

enum class SceneFormat {
    JSON,
    Binary
};

// ============================================================================
// COMPONENT SERIALIZER REGISTRY
// ============================================================================

// Base class for component serialization
class IComponentSerializer {
public:
    virtual ~IComponentSerializer() = default;
    
    virtual std::string getTypeName() const = 0;
    virtual void serialize(const void* component, std::ostream& stream, SceneFormat format) const = 0;
    virtual void deserialize(void* component, std::istream& stream, SceneFormat format) const = 0;
    virtual size_t getComponentSize() const = 0;
    virtual void addToEntity(World& world, Entity entity, std::istream& stream, SceneFormat format) const = 0;
};

template<typename T>
class ComponentSerializer : public IComponentSerializer {
public:
    using SerializeFunc = std::function<void(const T&, std::ostream&, SceneFormat)>;
    using DeserializeFunc = std::function<void(T&, std::istream&, SceneFormat)>;
    
    ComponentSerializer(const std::string& typeName, 
                       SerializeFunc serializeFunc,
                       DeserializeFunc deserializeFunc)
        : typeName_(typeName), serializeFunc_(serializeFunc), deserializeFunc_(deserializeFunc) {}
    
    std::string getTypeName() const override { return typeName_; }
    size_t getComponentSize() const override { return sizeof(T); }
    
    void serialize(const void* component, std::ostream& stream, SceneFormat format) const override {
        serializeFunc_(*static_cast<const T*>(component), stream, format);
    }
    
    void deserialize(void* component, std::istream& stream, SceneFormat format) const override {
        deserializeFunc_(*static_cast<T*>(component), stream, format);
    }
    
    void addToEntity(World& world, Entity entity, std::istream& stream, SceneFormat format) const override {
        T component{};
        deserializeFunc_(component, stream, format);
        world.addComponent<T>(entity, component);
    }
    
private:
    std::string typeName_;
    SerializeFunc serializeFunc_;
    DeserializeFunc deserializeFunc_;
};

class ComponentSerializerRegistry {
public:
    static ComponentSerializerRegistry& getInstance();
    
    template<typename T>
    void registerComponent(const std::string& typeName,
                          typename ComponentSerializer<T>::SerializeFunc serialize,
                          typename ComponentSerializer<T>::DeserializeFunc deserialize) {
        ComponentTypeId typeId = ComponentRegistry::getInstance().getTypeId<T>();
        serializers_[typeId] = std::make_unique<ComponentSerializer<T>>(typeName, serialize, deserialize);
        nameToTypeId_[typeName] = typeId;
    }
    
    IComponentSerializer* getSerializer(ComponentTypeId typeId) const;
    IComponentSerializer* getSerializer(const std::string& typeName) const;
    ComponentTypeId getTypeId(const std::string& typeName) const;
    
private:
    ComponentSerializerRegistry() = default;
    
    std::unordered_map<ComponentTypeId, std::unique_ptr<IComponentSerializer>> serializers_;
    std::unordered_map<std::string, ComponentTypeId> nameToTypeId_;
};

// ============================================================================
// PREFAB
// ============================================================================

struct Prefab {
    std::string name;
    std::string path;
    Entity rootEntity = INVALID_ENTITY;
    
    // Serialized entity data
    std::vector<uint8_t> data;
    
    // Child entity offsets for hierarchy
    std::vector<uint32_t> entityOffsets;
    
    // Override properties
    struct Override {
        std::string componentType;
        std::string propertyPath;
        std::vector<uint8_t> value;
    };
    std::vector<Override> overrides;
};

class PrefabManager {
public:
    static PrefabManager& getInstance();
    
    // Create prefab from entity hierarchy
    std::shared_ptr<Prefab> createPrefab(World& world, Entity root, const std::string& name);
    
    // Save/Load
    bool savePrefab(const Prefab& prefab, const std::string& path);
    std::shared_ptr<Prefab> loadPrefab(const std::string& path);
    
    // Instantiate
    Entity instantiate(World& world, const Prefab& prefab, const glm::vec3& position = glm::vec3(0),
                      const glm::quat& rotation = glm::quat());
    
    // Cache
    std::shared_ptr<Prefab> getPrefab(const std::string& name);
    void clearCache();
    
private:
    PrefabManager() = default;
    
    std::unordered_map<std::string, std::shared_ptr<Prefab>> cache_;
};

// ============================================================================
// SCENE
// ============================================================================

struct SceneMetadata {
    std::string name;
    std::string description;
    std::string author;
    uint64_t createdTime;
    uint64_t modifiedTime;
    
    // Environment settings
    glm::vec3 ambientColor = glm::vec3(0.1f);
    std::string skyboxPath;
    std::string environmentMapPath;
    
    // Navigation
    std::string navMeshPath;
    
    // Audio
    std::string ambienceClip;
    float ambienceVolume = 1.0f;
};

class Scene {
public:
    Scene() = default;
    Scene(const std::string& name);
    
    const std::string& getName() const { return metadata_.name; }
    void setName(const std::string& name) { metadata_.name = name; }
    
    SceneMetadata& getMetadata() { return metadata_; }
    const SceneMetadata& getMetadata() const { return metadata_; }
    
    World& getWorld() { return world_; }
    const World& getWorld() const { return world_; }
    
    // Entity management (wrapper around World)
    Entity createEntity(const std::string& name = "Entity");
    void destroyEntity(Entity entity);
    
    // Find entities
    Entity findEntity(const std::string& name);
    std::vector<Entity> findEntitiesWithTag(const std::string& tag);
    
    // Scene hierarchy root entities
    const std::vector<Entity>& getRootEntities() const { return rootEntities_; }
    void setRootEntities(const std::vector<Entity>& roots) { rootEntities_ = roots; }
    
    // Dirty tracking for auto-save
    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    void clearDirty() { dirty_ = false; }
    
private:
    SceneMetadata metadata_;
    World world_;
    std::vector<Entity> rootEntities_;
    bool dirty_ = false;
};

// ============================================================================
// SCENE SERIALIZER
// ============================================================================

class SceneSerializer {
public:
    SceneSerializer() = default;
    
    // Save scene
    bool saveScene(const Scene& scene, const std::string& path, SceneFormat format = SceneFormat::JSON);
    
    // Load scene
    std::unique_ptr<Scene> loadScene(const std::string& path);
    
    // Async loading
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;
    void loadSceneAsync(const std::string& path, 
                       std::function<void(std::unique_ptr<Scene>)> onComplete,
                       ProgressCallback onProgress = nullptr);
    
    // Partial serialization (for networking/undo)
    std::vector<uint8_t> serializeEntity(World& world, Entity entity);
    Entity deserializeEntity(World& world, const std::vector<uint8_t>& data);
    
    // Diff for networking
    struct EntityDiff {
        Entity entity;
        std::vector<ComponentTypeId> addedComponents;
        std::vector<ComponentTypeId> removedComponents;
        std::vector<ComponentTypeId> modifiedComponents;
        std::vector<std::pair<ComponentTypeId, std::vector<uint8_t>>> componentData;
    };
    
    EntityDiff diffEntity(World& world, Entity entity, const std::vector<uint8_t>& previousState);
    void applyDiff(World& world, const EntityDiff& diff);
    
private:
    // JSON serialization helpers
    void writeJSON(std::ostream& stream, const Scene& scene);
    std::unique_ptr<Scene> readJSON(std::istream& stream);
    
    // Binary serialization helpers
    void writeBinary(std::ostream& stream, const Scene& scene);
    std::unique_ptr<Scene> readBinary(std::istream& stream);
    
    void serializeEntityHierarchy(std::ostream& stream, World& world, Entity entity, SceneFormat format);
};

// ============================================================================
// UNDO/REDO SYSTEM
// ============================================================================

class UndoAction {
public:
    virtual ~UndoAction() = default;
    virtual void undo(World& world) = 0;
    virtual void redo(World& world) = 0;
    virtual std::string getDescription() const = 0;
};

class UndoStack {
public:
    void push(std::unique_ptr<UndoAction> action);
    void undo(World& world);
    void redo(World& world);
    
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    
    std::string getUndoDescription() const;
    std::string getRedoDescription() const;
    
    void clear();
    void setMaxSize(size_t maxSize) { maxSize_ = maxSize; }
    
private:
    std::vector<std::unique_ptr<UndoAction>> undoStack_;
    std::vector<std::unique_ptr<UndoAction>> redoStack_;
    size_t maxSize_ = 100;
};

// Common undo actions
class CreateEntityAction : public UndoAction {
public:
    CreateEntityAction(Entity entity, const std::vector<uint8_t>& serializedData);
    void undo(World& world) override;
    void redo(World& world) override;
    std::string getDescription() const override { return "Create Entity"; }
    
private:
    Entity entity_;
    std::vector<uint8_t> data_;
};

class DeleteEntityAction : public UndoAction {
public:
    DeleteEntityAction(Entity entity, const std::vector<uint8_t>& serializedData);
    void undo(World& world) override;
    void redo(World& world) override;
    std::string getDescription() const override { return "Delete Entity"; }
    
private:
    Entity entity_;
    std::vector<uint8_t> data_;
};

class ModifyComponentAction : public UndoAction {
public:
    ModifyComponentAction(Entity entity, ComponentTypeId typeId,
                         const std::vector<uint8_t>& oldData,
                         const std::vector<uint8_t>& newData);
    void undo(World& world) override;
    void redo(World& world) override;
    std::string getDescription() const override { return "Modify Component"; }
    
private:
    Entity entity_;
    ComponentTypeId typeId_;
    std::vector<uint8_t> oldData_;
    std::vector<uint8_t> newData_;
};

// ============================================================================
// REGISTER BUILT-IN COMPONENTS
// ============================================================================

void registerBuiltInSerializers();

} // namespace Sanic
