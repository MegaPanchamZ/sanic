/**
 * SceneSerializer.cpp - Scene Serialization Implementation
 */

#include "SceneSerializer.h"
#include <sstream>
#include <algorithm>
#include <chrono>

namespace Sanic {

// ============================================================================
// COMPONENT SERIALIZER REGISTRY
// ============================================================================

ComponentSerializerRegistry& ComponentSerializerRegistry::getInstance() {
    static ComponentSerializerRegistry instance;
    return instance;
}

IComponentSerializer* ComponentSerializerRegistry::getSerializer(ComponentTypeId typeId) const {
    auto it = serializers_.find(typeId);
    return it != serializers_.end() ? it->second.get() : nullptr;
}

IComponentSerializer* ComponentSerializerRegistry::getSerializer(const std::string& typeName) const {
    auto it = nameToTypeId_.find(typeName);
    if (it != nameToTypeId_.end()) {
        return getSerializer(it->second);
    }
    return nullptr;
}

ComponentTypeId ComponentSerializerRegistry::getTypeId(const std::string& typeName) const {
    auto it = nameToTypeId_.find(typeName);
    return it != nameToTypeId_.end() ? it->second : 0;
}

// ============================================================================
// PREFAB MANAGER
// ============================================================================

PrefabManager& PrefabManager::getInstance() {
    static PrefabManager instance;
    return instance;
}

std::shared_ptr<Prefab> PrefabManager::createPrefab(World& /*world*/, Entity root, const std::string& name) {
    auto prefab = std::make_shared<Prefab>();
    prefab->name = name;
    prefab->rootEntity = root;
    
    // TODO: Serialize entity hierarchy to prefab->data
    
    return prefab;
}

bool PrefabManager::savePrefab(const Prefab& prefab, const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    
    // Write magic
    file.write(reinterpret_cast<const char*>(&PREFAB_MAGIC), sizeof(PREFAB_MAGIC));
    
    // Write version
    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    // Write name
    uint32_t nameLen = static_cast<uint32_t>(prefab.name.length());
    file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
    file.write(prefab.name.c_str(), nameLen);
    
    // Write data
    uint32_t dataSize = static_cast<uint32_t>(prefab.data.size());
    file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
    if (dataSize > 0) {
        file.write(reinterpret_cast<const char*>(prefab.data.data()), dataSize);
    }
    
    return file.good();
}

std::shared_ptr<Prefab> PrefabManager::loadPrefab(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return nullptr;
    
    // Read and verify magic
    uint32_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != PREFAB_MAGIC) return nullptr;
    
    // Read version
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    
    auto prefab = std::make_shared<Prefab>();
    prefab->path = path;
    
    // Read name
    uint32_t nameLen;
    file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
    prefab->name.resize(nameLen);
    file.read(&prefab->name[0], nameLen);
    
    // Read data
    uint32_t dataSize;
    file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
    if (dataSize > 0) {
        prefab->data.resize(dataSize);
        file.read(reinterpret_cast<char*>(prefab->data.data()), dataSize);
    }
    
    // Cache it
    cache_[prefab->name] = prefab;
    
    return prefab;
}

Entity PrefabManager::instantiate(World& world, const Prefab& prefab, 
                                  const glm::vec3& /*position*/, const glm::quat& /*rotation*/) {
    if (prefab.data.empty()) {
        return world.createEntity();
    }
    
    // TODO: Deserialize prefab data to create entity hierarchy
    // For now, just create an empty entity
    Entity entity = world.createEntity();
    
    return entity;
}

std::shared_ptr<Prefab> PrefabManager::getPrefab(const std::string& name) {
    auto it = cache_.find(name);
    return it != cache_.end() ? it->second : nullptr;
}

void PrefabManager::clearCache() {
    cache_.clear();
}

// ============================================================================
// SCENE
// ============================================================================

Scene::Scene(const std::string& name) {
    metadata_.name = name;
    metadata_.createdTime = static_cast<uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count());
    metadata_.modifiedTime = metadata_.createdTime;
}

Entity Scene::createEntity(const std::string& /*name*/) {
    Entity entity = world_.createEntity();
    // TODO: Add name component
    markDirty();
    return entity;
}

void Scene::destroyEntity(Entity entity) {
    world_.destroyEntity(entity);
    markDirty();
}

Entity Scene::findEntity(const std::string& /*name*/) {
    // TODO: Search by name component
    return INVALID_ENTITY;
}

std::vector<Entity> Scene::findEntitiesWithTag(const std::string& /*tag*/) {
    // TODO: Search by tag component
    return {};
}

// ============================================================================
// SCENE SERIALIZER - JSON
// ============================================================================

// Simple JSON writer helper
class JsonWriter {
public:
    JsonWriter(std::ostream& stream) : stream_(stream), indent_(0), needsComma_(false) {}
    
    void beginObject() {
        maybeComma();
        stream_ << "{\n";
        indent_++;
        needsComma_ = false;
    }
    
    void endObject() {
        stream_ << "\n";
        indent_--;
        writeIndent();
        stream_ << "}";
        needsComma_ = true;
    }
    
    void beginArray() {
        maybeComma();
        stream_ << "[\n";
        indent_++;
        needsComma_ = false;
    }
    
    void endArray() {
        stream_ << "\n";
        indent_--;
        writeIndent();
        stream_ << "]";
        needsComma_ = true;
    }
    
    void key(const std::string& k) {
        maybeComma();
        writeIndent();
        stream_ << "\"" << k << "\": ";
        needsComma_ = false;
    }
    
    void value(const std::string& v) {
        maybeComma();
        stream_ << "\"" << escapeString(v) << "\"";
        needsComma_ = true;
    }
    
    void value(int v) {
        maybeComma();
        stream_ << v;
        needsComma_ = true;
    }
    
    void value(uint64_t v) {
        maybeComma();
        stream_ << v;
        needsComma_ = true;
    }
    
    void value(float v) {
        maybeComma();
        stream_ << v;
        needsComma_ = true;
    }
    
    void value(bool v) {
        maybeComma();
        stream_ << (v ? "true" : "false");
        needsComma_ = true;
    }
    
    void nullValue() {
        maybeComma();
        stream_ << "null";
        needsComma_ = true;
    }
    
private:
    void writeIndent() {
        for (int i = 0; i < indent_; i++) stream_ << "  ";
    }
    
    void maybeComma() {
        if (needsComma_) {
            stream_ << ",\n";
        }
        if (!needsComma_ && indent_ > 0) {
            writeIndent();
        }
    }
    
    std::string escapeString(const std::string& s) {
        std::string result;
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c;
            }
        }
        return result;
    }
    
    std::ostream& stream_;
    int indent_;
    bool needsComma_;
};

void SceneSerializer::writeJSON(std::ostream& stream, const Scene& scene) {
    JsonWriter writer(stream);
    
    writer.beginObject();
    
    // Magic and version
    writer.key("magic"); writer.value("SNSC");
    writer.key("version"); writer.value(static_cast<int>(SCENE_VERSION));
    
    // Metadata
    writer.key("metadata");
    writer.beginObject();
    writer.key("name"); writer.value(scene.getMetadata().name);
    writer.key("description"); writer.value(scene.getMetadata().description);
    writer.key("author"); writer.value(scene.getMetadata().author);
    writer.key("createdTime"); writer.value(scene.getMetadata().createdTime);
    writer.key("modifiedTime"); writer.value(scene.getMetadata().modifiedTime);
    writer.key("ambientColor");
    writer.beginArray();
    writer.value(scene.getMetadata().ambientColor.x);
    writer.value(scene.getMetadata().ambientColor.y);
    writer.value(scene.getMetadata().ambientColor.z);
    writer.endArray();
    writer.key("skyboxPath"); writer.value(scene.getMetadata().skyboxPath);
    writer.key("environmentMapPath"); writer.value(scene.getMetadata().environmentMapPath);
    writer.endObject();
    
    // Entities
    writer.key("entities");
    writer.beginArray();
    
    for (Entity root : scene.getRootEntities()) {
        serializeEntityHierarchy(stream, const_cast<World&>(scene.getWorld()), root, SceneFormat::JSON);
    }
    
    writer.endArray();
    
    writer.endObject();
}

std::unique_ptr<Scene> SceneSerializer::readJSON(std::istream& /*stream*/) {
    // TODO: Implement JSON parsing
    auto scene = std::make_unique<Scene>();
    return scene;
}

// ============================================================================
// SCENE SERIALIZER - BINARY
// ============================================================================

void SceneSerializer::writeBinary(std::ostream& stream, const Scene& scene) {
    // Write magic
    stream.write(reinterpret_cast<const char*>(&SCENE_MAGIC), sizeof(SCENE_MAGIC));
    
    // Write version
    stream.write(reinterpret_cast<const char*>(&SCENE_VERSION), sizeof(SCENE_VERSION));
    
    // Write metadata
    const auto& meta = scene.getMetadata();
    
    auto writeString = [&stream](const std::string& s) {
        uint32_t len = static_cast<uint32_t>(s.length());
        stream.write(reinterpret_cast<const char*>(&len), sizeof(len));
        stream.write(s.c_str(), len);
    };
    
    writeString(meta.name);
    writeString(meta.description);
    writeString(meta.author);
    stream.write(reinterpret_cast<const char*>(&meta.createdTime), sizeof(meta.createdTime));
    stream.write(reinterpret_cast<const char*>(&meta.modifiedTime), sizeof(meta.modifiedTime));
    stream.write(reinterpret_cast<const char*>(&meta.ambientColor), sizeof(meta.ambientColor));
    writeString(meta.skyboxPath);
    writeString(meta.environmentMapPath);
    writeString(meta.navMeshPath);
    writeString(meta.ambienceClip);
    stream.write(reinterpret_cast<const char*>(&meta.ambienceVolume), sizeof(meta.ambienceVolume));
    
    // Write entity count
    uint32_t entityCount = static_cast<uint32_t>(scene.getRootEntities().size());
    stream.write(reinterpret_cast<const char*>(&entityCount), sizeof(entityCount));
    
    // Write entities
    for (Entity root : scene.getRootEntities()) {
        serializeEntityHierarchy(stream, const_cast<World&>(scene.getWorld()), root, SceneFormat::Binary);
    }
}

std::unique_ptr<Scene> SceneSerializer::readBinary(std::istream& stream) {
    // Read and verify magic
    uint32_t magic;
    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != SCENE_MAGIC) return nullptr;
    
    // Read and verify version
    uint32_t version;
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version > SCENE_VERSION) return nullptr;
    
    auto scene = std::make_unique<Scene>();
    auto& meta = scene->getMetadata();
    
    auto readString = [&stream]() -> std::string {
        uint32_t len;
        stream.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string s(len, '\0');
        stream.read(&s[0], len);
        return s;
    };
    
    meta.name = readString();
    meta.description = readString();
    meta.author = readString();
    stream.read(reinterpret_cast<char*>(&meta.createdTime), sizeof(meta.createdTime));
    stream.read(reinterpret_cast<char*>(&meta.modifiedTime), sizeof(meta.modifiedTime));
    stream.read(reinterpret_cast<char*>(&meta.ambientColor), sizeof(meta.ambientColor));
    meta.skyboxPath = readString();
    meta.environmentMapPath = readString();
    meta.navMeshPath = readString();
    meta.ambienceClip = readString();
    stream.read(reinterpret_cast<char*>(&meta.ambienceVolume), sizeof(meta.ambienceVolume));
    
    // Read entities
    uint32_t entityCount;
    stream.read(reinterpret_cast<char*>(&entityCount), sizeof(entityCount));
    
    // TODO: Deserialize entity hierarchy
    
    return scene;
}

void SceneSerializer::serializeEntityHierarchy(std::ostream& /*stream*/, World& /*world*/, 
                                               Entity /*entity*/, SceneFormat /*format*/) {
    // TODO: Write entity and all its components
    // Then recursively write children
}

// ============================================================================
// SCENE SERIALIZER - PUBLIC API
// ============================================================================

bool SceneSerializer::saveScene(const Scene& scene, const std::string& path, SceneFormat format) {
    std::ofstream file(path, format == SceneFormat::Binary ? std::ios::binary : std::ios::out);
    if (!file) return false;
    
    if (format == SceneFormat::JSON) {
        writeJSON(file, scene);
    } else {
        writeBinary(file, scene);
    }
    
    return file.good();
}

std::unique_ptr<Scene> SceneSerializer::loadScene(const std::string& path) {
    // Try binary first
    std::ifstream file(path, std::ios::binary);
    if (!file) return nullptr;
    
    uint32_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.seekg(0);
    
    if (magic == SCENE_MAGIC) {
        return readBinary(file);
    } else {
        // Try JSON
        file.close();
        std::ifstream jsonFile(path);
        return readJSON(jsonFile);
    }
}

void SceneSerializer::loadSceneAsync(const std::string& path,
                                     std::function<void(std::unique_ptr<Scene>)> onComplete,
                                     ProgressCallback onProgress) {
    // For now, just load synchronously
    // In a real implementation, this would use std::async or a job system
    if (onProgress) {
        onProgress(0.0f, "Loading scene...");
    }
    
    auto scene = loadScene(path);
    
    if (onProgress) {
        onProgress(1.0f, "Complete");
    }
    
    if (onComplete) {
        onComplete(std::move(scene));
    }
}

std::vector<uint8_t> SceneSerializer::serializeEntity(World& /*world*/, Entity /*entity*/) {
    std::vector<uint8_t> data;
    // TODO: Serialize entity to binary
    return data;
}

Entity SceneSerializer::deserializeEntity(World& world, const std::vector<uint8_t>& /*data*/) {
    Entity entity = world.createEntity();
    // TODO: Deserialize from binary
    return entity;
}

SceneSerializer::EntityDiff SceneSerializer::diffEntity(World& /*world*/, Entity entity, 
                                                        const std::vector<uint8_t>& /*previousState*/) {
    EntityDiff diff;
    diff.entity = entity;
    // TODO: Compare current state with previous
    return diff;
}

void SceneSerializer::applyDiff(World& /*world*/, const EntityDiff& /*diff*/) {
    // TODO: Apply diff to entity
}

// ============================================================================
// UNDO STACK
// ============================================================================

void UndoStack::push(std::unique_ptr<UndoAction> action) {
    undoStack_.push_back(std::move(action));
    redoStack_.clear();
    
    // Enforce max size
    while (undoStack_.size() > maxSize_) {
        undoStack_.erase(undoStack_.begin());
    }
}

void UndoStack::undo(World& world) {
    if (undoStack_.empty()) return;
    
    auto action = std::move(undoStack_.back());
    undoStack_.pop_back();
    
    action->undo(world);
    redoStack_.push_back(std::move(action));
}

void UndoStack::redo(World& world) {
    if (redoStack_.empty()) return;
    
    auto action = std::move(redoStack_.back());
    redoStack_.pop_back();
    
    action->redo(world);
    undoStack_.push_back(std::move(action));
}

std::string UndoStack::getUndoDescription() const {
    return undoStack_.empty() ? "" : undoStack_.back()->getDescription();
}

std::string UndoStack::getRedoDescription() const {
    return redoStack_.empty() ? "" : redoStack_.back()->getDescription();
}

void UndoStack::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

// ============================================================================
// UNDO ACTIONS
// ============================================================================

CreateEntityAction::CreateEntityAction(Entity entity, const std::vector<uint8_t>& serializedData)
    : entity_(entity), data_(serializedData) {}

void CreateEntityAction::undo(World& world) {
    world.destroyEntity(entity_);
}

void CreateEntityAction::redo(World& world) {
    // Re-create entity and deserialize
    entity_ = world.createEntity();
    // TODO: Deserialize components from data_
}

DeleteEntityAction::DeleteEntityAction(Entity entity, const std::vector<uint8_t>& serializedData)
    : entity_(entity), data_(serializedData) {}

void DeleteEntityAction::undo(World& world) {
    // Re-create entity and deserialize
    entity_ = world.createEntity();
    // TODO: Deserialize components from data_
}

void DeleteEntityAction::redo(World& world) {
    world.destroyEntity(entity_);
}

ModifyComponentAction::ModifyComponentAction(Entity entity, ComponentTypeId typeId,
                                             const std::vector<uint8_t>& oldData,
                                             const std::vector<uint8_t>& newData)
    : entity_(entity), typeId_(typeId), oldData_(oldData), newData_(newData) {}

void ModifyComponentAction::undo(World& /*world*/) {
    // TODO: Deserialize oldData_ to component
}

void ModifyComponentAction::redo(World& /*world*/) {
    // TODO: Deserialize newData_ to component
}

// ============================================================================
// REGISTER BUILT-IN SERIALIZERS
// ============================================================================

void registerBuiltInSerializers() {
    // TODO: Register serializers for built-in component types
    // auto& registry = ComponentSerializerRegistry::getInstance();
    // registry.registerComponent<Transform>("Transform", ...);
}

} // namespace Sanic
