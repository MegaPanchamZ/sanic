#pragma once
// ============================================================================
// SANIC ENGINE - Asset System
// ============================================================================
// Handles asset discovery, loading, and management.
// Similar to Unreal's FPaths, AssetRegistry, and Package system.
//
// File formats:
//   .sproj  - Sanic Project descriptor (JSON)
//   .sasset - Sanic Asset (binary serialized)
//   .sworld - Sanic World/Level file
//   .smesh  - Mesh data
//   .smat   - Material definition
//
// Directory structure expected:
//   MyProject/
//   ├── MyProject.sproj
//   ├── Config/
//   │   └── Settings.json
//   ├── Content/
//   │   ├── Levels/
//   │   ├── Meshes/
//   │   ├── Materials/
//   │   ├── Textures/
//   │   └── Audio/
//   ├── Shaders/
//   └── Saved/
// ============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Sanic {

// Forward declarations
class Asset;
class World;
class Mesh;
class Material;
class Texture;

// ============================================================================
// ASSET TYPES
// ============================================================================
enum class AssetType : uint32_t {
    Unknown = 0,
    World,          // .sworld - Level/map data
    Mesh,           // .smesh - 3D geometry
    Material,       // .smat - Material definition
    Texture,        // .stex - Image data
    Audio,          // .saudio - Sound data
    Blueprint,      // .sbp - Visual scripting
    Animation,      // .sanim - Animation data
    Physics,        // .sphys - Physics asset
    Terrain,        // .sterrain - Terrain heightmap
    Spline,         // .sspline - Spline/rail data
    Prefab,         // .sprefab - GameObject template
    Config,         // .json - Configuration
    Shader,         // .glsl/.spv - Shader code
    
    MAX_TYPES
};

// ============================================================================
// ASSET HEADER - Common header for all binary assets
// ============================================================================
struct AssetHeader {
    static constexpr uint32_t MAGIC = 0x534E4943; // "SNIC" in hex
    static constexpr uint32_t CURRENT_VERSION = 1;
    
    uint32_t magic = MAGIC;
    uint32_t version = CURRENT_VERSION;
    AssetType type = AssetType::Unknown;
    uint64_t guid[2] = {0, 0};      // 128-bit unique ID
    uint64_t dataOffset = 0;
    uint64_t dataSize = 0;
    uint64_t importTableOffset = 0; // Dependencies
    uint32_t importCount = 0;
    uint32_t flags = 0;
    char name[256] = {0};
    
    bool isValid() const { return magic == MAGIC && version <= CURRENT_VERSION; }
};

// ============================================================================
// ASSET METADATA - Lightweight info for registry (no loading required)
// ============================================================================
struct AssetMetadata {
    std::string path;           // Virtual path: /Content/Meshes/Rock.smesh
    std::string diskPath;       // Absolute disk path
    std::string name;           // Asset name without extension
    AssetType type = AssetType::Unknown;
    uint64_t guid[2] = {0, 0};
    uint64_t fileSize = 0;
    uint64_t lastModified = 0;
    std::vector<std::string> dependencies;
    std::unordered_map<std::string, std::string> tags; // Searchable metadata
    
    bool isLoaded = false;
    std::weak_ptr<Asset> cachedAsset;
};

// ============================================================================
// PROJECT DESCRIPTOR (.sproj)
// ============================================================================
struct ProjectDescriptor {
    std::string name;
    std::string description;
    std::string engineVersion;
    std::string defaultWorld;       // Starting level
    std::vector<std::string> modules;
    std::vector<std::string> plugins;
    std::unordered_map<std::string, std::string> settings;
    
    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;
};

// ============================================================================
// SANIC PATHS - Path resolution system (like UE's FPaths)
// ============================================================================
class SanicPaths {
public:
    static SanicPaths& get();
    
    // Initialize with project root
    void initialize(const std::string& projectPath);
    void initializeEngineOnly(const std::string& enginePath);
    
    // Engine paths
    std::string engineDir() const { return m_engineDir; }
    std::string engineContentDir() const { return m_engineDir + "/Content"; }
    std::string engineShadersDir() const { return m_engineDir + "/Shaders"; }
    std::string engineConfigDir() const { return m_engineDir + "/Config"; }
    
    // Project paths
    std::string projectDir() const { return m_projectDir; }
    std::string projectContentDir() const { return m_projectDir + "/Content"; }
    std::string projectConfigDir() const { return m_projectDir + "/Config"; }
    std::string projectSavedDir() const { return m_projectDir + "/Saved"; }
    std::string projectShadersDir() const { return m_projectDir + "/Shaders"; }
    
    // Path utilities
    std::string normalize(const std::string& path) const;
    std::string makeAbsolute(const std::string& relativePath) const;
    std::string makeRelative(const std::string& absolutePath, const std::string& basePath) const;
    std::string combine(const std::string& base, const std::string& path) const;
    std::string getExtension(const std::string& path) const;
    std::string getFilename(const std::string& path) const;
    std::string getDirectory(const std::string& path) const;
    
    // Virtual path resolution: /Content/Meshes/Rock -> actual disk path
    std::string resolveVirtualPath(const std::string& virtualPath) const;
    std::string toVirtualPath(const std::string& diskPath) const;
    
    // Mount points for DLC/mods
    void addMountPoint(const std::string& virtualRoot, const std::string& diskPath, int priority = 0);
    void removeMountPoint(const std::string& virtualRoot);
    
    bool hasProject() const { return !m_projectDir.empty(); }
    
private:
    SanicPaths() = default;
    
    std::string m_engineDir;
    std::string m_projectDir;
    
    struct MountPoint {
        std::string virtualRoot;
        std::string diskPath;
        int priority;
    };
    std::vector<MountPoint> m_mountPoints;
};

// ============================================================================
// ASSET REGISTRY - Index of all discoverable assets
// ============================================================================
class AssetRegistry {
public:
    static AssetRegistry& get();
    
    // Scan directories for assets
    void scanDirectory(const std::string& path, bool recursive = true);
    void scanProjectContent();
    void scanEngineContent();
    
    // Asset queries
    const AssetMetadata* findAsset(const std::string& virtualPath) const;
    std::vector<const AssetMetadata*> findAssetsByType(AssetType type) const;
    std::vector<const AssetMetadata*> findAssetsByPath(const std::string& pathPrefix) const;
    std::vector<const AssetMetadata*> findAssetsByTag(const std::string& key, const std::string& value) const;
    std::vector<const AssetMetadata*> getAllAssets() const;
    
    // Asset registration (for runtime-created assets)
    void registerAsset(const AssetMetadata& metadata);
    void unregisterAsset(const std::string& virtualPath);
    
    // Dependency tracking
    std::vector<std::string> getDependencies(const std::string& virtualPath) const;
    std::vector<std::string> getReferencers(const std::string& virtualPath) const;
    
    // Persistence
    void saveRegistryCache(const std::string& path) const;
    bool loadRegistryCache(const std::string& path);
    
    size_t getAssetCount() const { return m_assets.size(); }
    
private:
    AssetRegistry() = default;
    
    void scanFile(const std::filesystem::path& filePath);
    AssetType getAssetTypeFromExtension(const std::string& ext) const;
    
    std::unordered_map<std::string, AssetMetadata> m_assets; // virtual path -> metadata
    std::unordered_map<std::string, std::vector<std::string>> m_referencers;
};

// ============================================================================
// ASSET LOADER - Loads assets from disk
// ============================================================================
class AssetLoader {
public:
    static AssetLoader& get();
    
    // Synchronous loading
    template<typename T>
    std::shared_ptr<T> load(const std::string& virtualPath);
    
    std::shared_ptr<Asset> loadGeneric(const std::string& virtualPath);
    
    // Async loading
    using LoadCallback = std::function<void(std::shared_ptr<Asset>, bool success)>;
    void loadAsync(const std::string& virtualPath, LoadCallback callback);
    
    // Batch loading
    void loadBatch(const std::vector<std::string>& paths, std::function<void(size_t loaded, size_t total)> progress = nullptr);
    
    // Cache management
    void unload(const std::string& virtualPath);
    void unloadUnused();
    void clearCache();
    
    bool isLoaded(const std::string& virtualPath) const;
    
private:
    AssetLoader() = default;
    
    std::shared_ptr<Asset> loadFromDisk(const std::string& diskPath, AssetType type);
    
    std::unordered_map<std::string, std::shared_ptr<Asset>> m_cache;
};

// ============================================================================
// BASE ASSET CLASS
// ============================================================================
class Asset {
public:
    virtual ~Asset() = default;
    
    const std::string& getPath() const { return m_path; }
    const std::string& getName() const { return m_name; }
    AssetType getType() const { return m_type; }
    const uint64_t* getGuid() const { return m_guid; }
    
    virtual bool load(const std::string& diskPath) = 0;
    virtual bool save(const std::string& diskPath) const = 0;
    
    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    
protected:
    std::string m_path;
    std::string m_name;
    AssetType m_type = AssetType::Unknown;
    uint64_t m_guid[2] = {0, 0};
    bool m_dirty = false;
};

// ============================================================================
// WORLD/LEVEL ASSET
// ============================================================================
struct WorldObjectData {
    std::string name;
    std::string prefabPath;     // Reference to prefab asset
    std::string meshPath;       // Direct mesh reference
    std::string materialPath;
    glm::vec3 position{0};
    glm::quat rotation{1, 0, 0, 0};
    glm::vec3 scale{1};
    std::unordered_map<std::string, std::string> properties;
    std::vector<std::string> tags;
    bool isStatic = true;
};

struct StreamingLevelData {
    std::string levelPath;
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    float loadDistance = 1000.0f;
    bool alwaysLoaded = false;
};

class WorldAsset : public Asset {
public:
    WorldAsset() { m_type = AssetType::World; }
    
    bool load(const std::string& diskPath) override;
    bool save(const std::string& diskPath) const override;
    
    // World data
    std::string displayName;
    glm::vec3 sunDirection{0, -1, 0};
    glm::vec3 sunColor{1, 0.95f, 0.9f};
    float sunIntensity = 1.0f;
    glm::vec3 ambientColor{0.1f, 0.12f, 0.15f};
    glm::vec3 fogColor{0.5f, 0.6f, 0.7f};
    float fogDensity = 0.001f;
    
    // Objects in this level
    std::vector<WorldObjectData> objects;
    
    // Streaming sub-levels
    std::vector<StreamingLevelData> streamingLevels;
    
    // Terrain reference
    std::string terrainPath;
    
    // Splines (for rails, rivers, roads)
    std::vector<std::string> splinePaths;
    
    // NavMesh reference
    std::string navMeshPath;
};

// ============================================================================
// MESH ASSET
// ============================================================================
struct MeshLOD {
    std::vector<float> vertices;    // Interleaved: pos, normal, uv, tangent
    std::vector<uint32_t> indices;
    float screenSizeThreshold = 0.0f;
};

class MeshAsset : public Asset {
public:
    MeshAsset() { m_type = AssetType::Mesh; }
    
    bool load(const std::string& diskPath) override;
    bool save(const std::string& diskPath) const override;
    
    // Import from common formats
    bool importFromOBJ(const std::string& objPath);
    bool importFromFBX(const std::string& fbxPath);
    bool importFromGLTF(const std::string& gltfPath);
    
    std::vector<MeshLOD> lods;
    glm::vec3 boundsMin{0};
    glm::vec3 boundsMax{0};
    std::string defaultMaterialPath;
    
    // Physics
    bool hasCollision = true;
    bool useComplexAsSimple = false;
    std::vector<float> convexHullVertices;
};

// ============================================================================
// MATERIAL ASSET
// ============================================================================
class MaterialAsset : public Asset {
public:
    MaterialAsset() { m_type = AssetType::Material; }
    
    bool load(const std::string& diskPath) override;
    bool save(const std::string& diskPath) const override;
    
    // PBR parameters
    glm::vec4 baseColor{1, 1, 1, 1};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float specular = 0.5f;
    glm::vec3 emissive{0};
    float emissiveStrength = 0.0f;
    
    // Texture paths
    std::string albedoTexture;
    std::string normalTexture;
    std::string metallicRoughnessTexture;
    std::string aoTexture;
    std::string emissiveTexture;
    
    // Shader override
    std::string customShaderPath;
    std::unordered_map<std::string, float> shaderParams;
    
    // Rendering flags
    bool doubleSided = false;
    bool transparent = false;
    float opacity = 1.0f;
};

// ============================================================================
// TERRAIN ASSET
// ============================================================================
class TerrainAsset : public Asset {
public:
    TerrainAsset() { m_type = AssetType::Terrain; }
    
    bool load(const std::string& diskPath) override;
    bool save(const std::string& diskPath) const override;
    
    uint32_t resolution = 1024;     // Heightmap resolution
    float worldSizeX = 1000.0f;
    float worldSizeZ = 1000.0f;
    float heightScale = 100.0f;
    
    std::vector<float> heightmap;
    
    // Layers for splatmap painting
    struct TerrainLayer {
        std::string materialPath;
        float tileScale = 1.0f;
    };
    std::vector<TerrainLayer> layers;
    std::vector<uint8_t> splatmap;  // RGBA per texel = weight per layer
    
    // Foliage
    struct FoliageInstance {
        glm::vec3 position;
        float rotation;
        float scale;
    };
    std::unordered_map<std::string, std::vector<FoliageInstance>> foliage;
};

// ============================================================================
// SPLINE ASSET (for rails, roads, rivers)
// ============================================================================
class SplineAsset : public Asset {
public:
    SplineAsset() { m_type = AssetType::Spline; }
    
    bool load(const std::string& diskPath) override;
    bool save(const std::string& diskPath) const override;
    
    struct ControlPoint {
        glm::vec3 position;
        glm::vec3 tangentIn;
        glm::vec3 tangentOut;
        float width = 1.0f;
        float bankAngle = 0.0f;     // For banked turns
        float speedModifier = 1.0f; // Speed boost/penalty
    };
    
    std::vector<ControlPoint> points;
    bool closed = false;
    
    // Spline type
    enum class SplineType { Linear, Bezier, CatmullRom, Hermite };
    SplineType type = SplineType::CatmullRom;
    
    // Usage hints
    bool isRail = false;
    bool isRoad = false;
    bool isRiver = false;
    std::string meshPath;           // Mesh to extrude along spline
    std::string materialPath;
};

// ============================================================================
// PREFAB ASSET (reusable object templates)
// ============================================================================
class PrefabAsset : public Asset {
public:
    PrefabAsset() { m_type = AssetType::Prefab; }
    
    bool load(const std::string& diskPath) override;
    bool save(const std::string& diskPath) const override;
    
    // Root object data
    WorldObjectData rootObject;
    
    // Child objects (relative transforms)
    std::vector<WorldObjectData> children;
    
    // Components
    struct ComponentData {
        std::string type;
        std::unordered_map<std::string, std::string> properties;
    };
    std::vector<ComponentData> components;
};

} // namespace Sanic
