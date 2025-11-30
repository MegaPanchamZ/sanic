/**
 * SceneSerializerAdvanced.h
 * 
 * Enhanced scene serialization with reflection system integration.
 * Provides automatic component serialization using SPROPERTY metadata.
 */

#pragma once

#include "SceneSerializer.h"
#include "Reflection.h"
#include <sstream>
#include <iomanip>

namespace Sanic {

// ============================================================================
// REFLECTION-BASED SERIALIZER
// ============================================================================

/**
 * Automatic component serializer using reflection
 */
class ReflectionComponentSerializer : public IComponentSerializer {
public:
    ReflectionComponentSerializer(TypeDescriptor* typeDesc);
    
    std::string getTypeName() const override { return typeDesc_->name; }
    size_t getComponentSize() const override { return typeDesc_->size; }
    
    void serialize(const void* component, std::ostream& stream, SceneFormat format) const override;
    void deserialize(void* component, std::istream& stream, SceneFormat format) const override;
    void addToEntity(World& world, Entity entity, std::istream& stream, SceneFormat format) const override;
    
private:
    TypeDescriptor* typeDesc_;
    
    void serializePropertyJSON(const PropertyMeta& prop, const void* data, std::ostream& stream) const;
    void deserializePropertyJSON(const PropertyMeta& prop, void* data, std::istream& stream) const;
    void serializePropertyBinary(const PropertyMeta& prop, const void* data, std::ostream& stream) const;
    void deserializePropertyBinary(const PropertyMeta& prop, void* data, std::istream& stream) const;
};

/**
 * JSON helper utilities
 */
class JSONWriter {
public:
    JSONWriter(std::ostream& stream, bool pretty = true);
    
    void beginObject();
    void endObject();
    void beginArray();
    void endArray();
    
    void key(const std::string& name);
    void value(bool v);
    void value(int32_t v);
    void value(uint32_t v);
    void value(int64_t v);
    void value(uint64_t v);
    void value(float v);
    void value(double v);
    void value(const std::string& v);
    void value(const char* v);
    void nullValue();
    
    // Compound types
    void writeVec2(const glm::vec2& v);
    void writeVec3(const glm::vec3& v);
    void writeVec4(const glm::vec4& v);
    void writeQuat(const glm::quat& v);
    void writeMat4(const glm::mat4& m);
    
private:
    std::ostream& stream_;
    bool pretty_;
    int indent_ = 0;
    bool needsComma_ = false;
    bool inKey_ = false;
    
    void writeIndent();
    void comma();
    void escapeString(const std::string& s);
};

class JSONReader {
public:
    JSONReader(std::istream& stream);
    
    enum class Token {
        ObjectStart,
        ObjectEnd,
        ArrayStart,
        ArrayEnd,
        String,
        Number,
        True,
        False,
        Null,
        Colon,
        Comma,
        EndOfFile,
        Error
    };
    
    Token nextToken();
    Token peekToken();
    
    bool readBool();
    int32_t readInt();
    int64_t readInt64();
    float readFloat();
    double readDouble();
    std::string readString();
    
    glm::vec2 readVec2();
    glm::vec3 readVec3();
    glm::vec4 readVec4();
    glm::quat readQuat();
    glm::mat4 readMat4();
    
    void expectToken(Token expected);
    bool skipValue();
    
    std::string getCurrentKey() const { return currentKey_; }
    
private:
    std::istream& stream_;
    Token currentToken_ = Token::EndOfFile;
    std::string currentKey_;
    std::string currentString_;
    double currentNumber_ = 0.0;
    
    void skipWhitespace();
    Token parseToken();
};

// ============================================================================
// ENHANCED SCENE SERIALIZER
// ============================================================================

class EnhancedSceneSerializer {
public:
    EnhancedSceneSerializer();
    
    /**
     * Register all reflected types for serialization
     */
    void registerReflectedTypes();
    
    /**
     * Serialize scene to JSON with full reflection support
     */
    std::string serializeToJSON(const Scene& scene, bool pretty = true);
    
    /**
     * Deserialize scene from JSON
     */
    std::unique_ptr<Scene> deserializeFromJSON(const std::string& json);
    
    /**
     * Serialize scene to binary with reflection
     */
    std::vector<uint8_t> serializeToBinary(const Scene& scene);
    
    /**
     * Deserialize scene from binary
     */
    std::unique_ptr<Scene> deserializeFromBinary(const std::vector<uint8_t>& data);
    
    /**
     * Serialize single entity for copy/paste or networking
     */
    std::string serializeEntityToJSON(World& world, Entity entity);
    Entity deserializeEntityFromJSON(World& world, const std::string& json);
    
    /**
     * Batch serialization for networking
     */
    struct EntitySnapshot {
        Entity entity;
        uint64_t version;
        std::vector<uint8_t> data;
    };
    
    EntitySnapshot createSnapshot(World& world, Entity entity);
    void applySnapshot(World& world, const EntitySnapshot& snapshot);
    
    /**
     * Delta compression for networking
     */
    std::vector<uint8_t> createDelta(const EntitySnapshot& from, const EntitySnapshot& to);
    void applyDelta(World& world, Entity entity, const std::vector<uint8_t>& delta);
    
private:
    std::unordered_map<std::string, TypeDescriptor*> typeDescriptors_;
    
    void serializeMetadataJSON(JSONWriter& writer, const SceneMetadata& metadata);
    void deserializeMetadataJSON(JSONReader& reader, SceneMetadata& metadata);
    
    void serializeEntityJSON(JSONWriter& writer, World& world, Entity entity);
    Entity deserializeEntityJSON(JSONReader& reader, World& world);
    
    void serializeComponentJSON(JSONWriter& writer, const std::string& typeName, const void* data);
    void deserializeComponentJSON(JSONReader& reader, World& world, Entity entity);
    
    template<typename T>
    void serializeValueJSON(JSONWriter& writer, const T& value);
    
    template<typename T>
    T deserializeValueJSON(JSONReader& reader);
};

// ============================================================================
// ASSET REFERENCE SERIALIZATION
// ============================================================================

/**
 * Asset reference for serialization
 */
struct AssetRef {
    std::string path;
    std::string type;
    uint64_t guid = 0;
    
    bool isValid() const { return !path.empty() || guid != 0; }
    
    // Resolve to actual asset at runtime
    template<typename T>
    T* resolve() const;
};

/**
 * Serializes asset references with GUIDs for stability
 */
class AssetReferenceSerializer {
public:
    static AssetReferenceSerializer& get();
    
    /**
     * Assign or get GUID for asset path
     */
    uint64_t getOrAssignGUID(const std::string& path);
    std::string resolvePath(uint64_t guid) const;
    
    /**
     * Save/load GUID mapping
     */
    void saveMapping(const std::string& path);
    void loadMapping(const std::string& path);
    
    /**
     * Handle asset renames/moves
     */
    void updatePath(uint64_t guid, const std::string& newPath);
    
private:
    AssetReferenceSerializer() = default;
    
    std::unordered_map<std::string, uint64_t> pathToGuid_;
    std::unordered_map<uint64_t, std::string> guidToPath_;
    uint64_t nextGuid_ = 1;
};

// ============================================================================
// SCENE GRAPH UTILITIES
// ============================================================================

/**
 * Scene graph traversal utilities
 */
class SceneGraph {
public:
    /**
     * Get parent-child relationships
     */
    static Entity getParent(World& world, Entity entity);
    static std::vector<Entity> getChildren(World& world, Entity entity);
    static std::vector<Entity> getAllDescendants(World& world, Entity entity);
    
    /**
     * Modify hierarchy
     */
    static void setParent(World& world, Entity child, Entity parent);
    static void detach(World& world, Entity entity);
    static void destroyHierarchy(World& world, Entity root);
    
    /**
     * Clone with hierarchy
     */
    static Entity cloneHierarchy(World& world, Entity root, bool keepRefs = false);
    
    /**
     * Traverse scene graph
     */
    static void traverse(World& world, Entity root, 
                         std::function<void(Entity, int depth)> visitor);
    static void traverseTopDown(World& world, 
                                 std::function<void(Entity)> visitor);
    
    /**
     * Find in hierarchy
     */
    static Entity findByPath(World& world, Entity root, const std::string& path);
    static std::string getPath(World& world, Entity entity);
};

// ============================================================================
// STREAMING SCENE LOADER
// ============================================================================

/**
 * Streaming scene loader for large scenes
 */
class StreamingSceneLoader {
public:
    struct LoadRequest {
        std::string scenePath;
        glm::vec3 loadOrigin;
        float loadRadius;
        int priority;
    };
    
    /**
     * Queue scene for async loading
     */
    void queueLoad(const LoadRequest& request);
    
    /**
     * Update streaming based on camera position
     */
    void update(const glm::vec3& cameraPosition, float loadDistance, float unloadDistance);
    
    /**
     * Get loading progress
     */
    float getProgress() const;
    bool isLoading() const;
    
    /**
     * Callbacks
     */
    std::function<void(const std::string& scenePath)> onSceneLoaded;
    std::function<void(const std::string& scenePath)> onSceneUnloaded;
    std::function<void(float progress)> onProgress;
    
private:
    struct LoadedChunk {
        std::string path;
        glm::vec3 origin;
        float radius;
        std::unique_ptr<Scene> scene;
    };
    
    std::vector<LoadRequest> loadQueue_;
    std::vector<LoadedChunk> loadedChunks_;
    std::atomic<float> progress_{0.0f};
    std::atomic<bool> loading_{false};
};

// ============================================================================
// SCENE DIFF FOR COLLABORATION
// ============================================================================

/**
 * Scene diff for collaborative editing
 */
class SceneDiff {
public:
    enum class Operation {
        CreateEntity,
        DeleteEntity,
        AddComponent,
        RemoveComponent,
        ModifyComponent,
        ReparentEntity,
        RenameEntity
    };
    
    struct Change {
        Operation op;
        Entity entity;
        std::string componentType;
        std::vector<uint8_t> oldData;
        std::vector<uint8_t> newData;
        std::string metadata;
    };
    
    /**
     * Compute diff between two scene states
     */
    static std::vector<Change> diff(const Scene& from, const Scene& to);
    
    /**
     * Apply diff to scene
     */
    static void apply(Scene& scene, const std::vector<Change>& changes);
    
    /**
     * Merge diffs (for collaborative editing)
     */
    static std::vector<Change> merge(const std::vector<Change>& local, 
                                      const std::vector<Change>& remote);
    
    /**
     * Serialize diff for network transmission
     */
    static std::vector<uint8_t> serialize(const std::vector<Change>& changes);
    static std::vector<Change> deserialize(const std::vector<uint8_t>& data);
};

} // namespace Sanic
