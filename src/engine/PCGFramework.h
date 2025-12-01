#pragma once

/**
 * PCGFramework.h
 * 
 * Procedural Content Generation framework.
 * Based on UE5's PCG system architecture.
 * 
 * Features:
 * - Graph-based procedural generation
 * - Multiple node types (samplers, filters, spawners)
 * - Deterministic generation from seeds
 * - Runtime and editor-time generation
 * - Hierarchical generation (subgraphs)
 * - Parameter overrides and variation
 */

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <random>
#include <variant>

namespace Kinetic {

// Forward declarations
class VulkanRenderer;
class LandscapeSystem;
class FoliageSystem;

//------------------------------------------------------------------------------
// PCG Data Types
//------------------------------------------------------------------------------

/**
 * Point data for PCG operations
 */
struct PCGPoint {
    glm::vec3 position;
    glm::vec3 normal = glm::vec3(0, 1, 0);
    glm::vec3 scale = glm::vec3(1);
    glm::quat rotation = glm::quat(1, 0, 0, 0);
    glm::vec4 color = glm::vec4(1);
    float density = 1.0f;
    int32_t seed = 0;
    
    // Custom attributes
    std::unordered_map<std::string, float> attributes;
};

/**
 * Spatial data collection
 */
struct PCGSpatialData {
    std::vector<PCGPoint> points;
    
    // Bounds
    glm::vec3 boundsMin = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    
    void updateBounds();
    void clear();
    void append(const PCGSpatialData& other);
    
    // Spatial acceleration (built on demand)
    mutable bool spatialIndexDirty = true;
    mutable std::vector<uint32_t> spatialGrid;
    mutable glm::ivec3 gridDimensions;
    mutable float gridCellSize = 0.0f;
    
    void buildSpatialIndex(float cellSize) const;
    std::vector<uint32_t> queryRadius(const glm::vec3& center, float radius) const;
    std::vector<uint32_t> queryBox(const glm::vec3& min, const glm::vec3& max) const;
};

/**
 * Landscape data for PCG queries
 */
struct PCGLandscapeData {
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    
    std::function<float(const glm::vec2&)> heightQuery;
    std::function<glm::vec3(const glm::vec2&)> normalQuery;
    std::function<float(const glm::vec2&, uint32_t)> layerWeightQuery;  // Layer weight at position
};

/**
 * Spline data for PCG
 */
struct PCGSplineData {
    std::vector<glm::vec3> points;
    std::vector<glm::vec3> tangents;
    std::vector<float> widths;
    bool isClosed = false;
    float length = 0.0f;
};

/**
 * PCG data variants
 */
using PCGData = std::variant<
    PCGSpatialData,
    PCGLandscapeData,
    PCGSplineData,
    std::vector<PCGData>  // Collection
>;

//------------------------------------------------------------------------------
// PCG Node Base
//------------------------------------------------------------------------------

/**
 * PCG node execution context
 */
struct PCGContext {
    int32_t seed = 0;
    glm::vec3 worldBoundsMin;
    glm::vec3 worldBoundsMax;
    
    // References
    LandscapeSystem* landscape = nullptr;
    FoliageSystem* foliage = nullptr;
    
    // Random generator
    std::mt19937 rng;
    
    // Hierarchical seed generation
    int32_t getChildSeed(int32_t index) const {
        return seed ^ (index * 2654435761);
    }
    
    void seedRNG(int32_t nodeSeed) {
        rng.seed(static_cast<uint32_t>(seed ^ nodeSeed));
    }
    
    float randomFloat(float min = 0.0f, float max = 1.0f) {
        std::uniform_real_distribution<float> dist(min, max);
        return dist(rng);
    }
    
    int32_t randomInt(int32_t min, int32_t max) {
        std::uniform_int_distribution<int32_t> dist(min, max);
        return dist(rng);
    }
};

/**
 * Pin type for node connections
 */
enum class PCGPinType {
    Spatial,        // Point cloud
    Landscape,      // Landscape reference
    Spline,         // Spline data
    Param,          // Scalar/vector parameter
    Any             // Accepts any type
};

/**
 * Node input/output pin
 */
struct PCGPin {
    std::string name;
    PCGPinType type;
    bool isOptional = false;
    PCGData defaultValue;
};

/**
 * Base class for PCG nodes
 */
class PCGNode {
public:
    virtual ~PCGNode() = default;
    
    // Node metadata
    virtual std::string getName() const = 0;
    virtual std::string getCategory() const = 0;
    virtual std::string getDescription() const { return ""; }
    
    // Pin definitions
    virtual std::vector<PCGPin> getInputPins() const = 0;
    virtual std::vector<PCGPin> getOutputPins() const = 0;
    
    // Execution
    virtual bool execute(PCGContext& context,
                        const std::vector<PCGData>& inputs,
                        std::vector<PCGData>& outputs) = 0;
    
    // Settings
    struct Setting {
        std::string name;
        std::variant<bool, int32_t, float, glm::vec2, glm::vec3, std::string> value;
    };
    
    void setSetting(const std::string& name, const Setting::value_type& value);
    const Setting* getSetting(const std::string& name) const;
    virtual std::vector<Setting> getDefaultSettings() const { return {}; }
    
    // Unique ID
    uint32_t nodeId = 0;
    
protected:
    std::unordered_map<std::string, Setting> settings_;
};

//------------------------------------------------------------------------------
// Sampler Nodes
//------------------------------------------------------------------------------

/**
 * Surface sampler - generates points on landscape
 */
class PCGSurfaceSamplerNode : public PCGNode {
public:
    std::string getName() const override { return "Surface Sampler"; }
    std::string getCategory() const override { return "Samplers"; }
    
    std::vector<PCGPin> getInputPins() const override;
    std::vector<PCGPin> getOutputPins() const override;
    
    bool execute(PCGContext& context,
                const std::vector<PCGData>& inputs,
                std::vector<PCGData>& outputs) override;
    
    std::vector<Setting> getDefaultSettings() const override;
    
private:
    float pointsPerSquareMeter_ = 1.0f;
    float minHeight_ = -10000.0f;
    float maxHeight_ = 10000.0f;
    float minSlope_ = 0.0f;
    float maxSlope_ = 90.0f;
    bool alignToNormal_ = false;
};

/**
 * Spline sampler - generates points along spline
 */
class PCGSplineSamplerNode : public PCGNode {
public:
    std::string getName() const override { return "Spline Sampler"; }
    std::string getCategory() const override { return "Samplers"; }
    
    std::vector<PCGPin> getInputPins() const override;
    std::vector<PCGPin> getOutputPins() const override;
    
    bool execute(PCGContext& context,
                const std::vector<PCGData>& inputs,
                std::vector<PCGData>& outputs) override;
    
    std::vector<Setting> getDefaultSettings() const override;
    
private:
    float spacing_ = 10.0f;
    bool projectToSurface_ = true;
    float offsetFromSpline_ = 0.0f;
};

/**
 * Volume sampler - generates points in 3D volume
 */
class PCGVolumeSamplerNode : public PCGNode {
public:
    std::string getName() const override { return "Volume Sampler"; }
    std::string getCategory() const override { return "Samplers"; }
    
    std::vector<PCGPin> getInputPins() const override;
    std::vector<PCGPin> getOutputPins() const override;
    
    bool execute(PCGContext& context,
                const std::vector<PCGData>& inputs,
                std::vector<PCGData>& outputs) override;
    
    std::vector<Setting> getDefaultSettings() const override;
    
private:
    float density_ = 0.1f;
    bool usePoissonDisk_ = true;
};

//------------------------------------------------------------------------------
// Filter Nodes
//------------------------------------------------------------------------------

/**
 * Density filter - removes points based on density/noise
 */
class PCGDensityFilterNode : public PCGNode {
public:
    std::string getName() const override { return "Density Filter"; }
    std::string getCategory() const override { return "Filters"; }
    
    std::vector<PCGPin> getInputPins() const override;
    std::vector<PCGPin> getOutputPins() const override;
    
    bool execute(PCGContext& context,
                const std::vector<PCGData>& inputs,
                std::vector<PCGData>& outputs) override;
    
    std::vector<Setting> getDefaultSettings() const override;
    
private:
    float densityMin_ = 0.0f;
    float densityMax_ = 1.0f;
    bool invertDensity_ = false;
    float noiseScale_ = 100.0f;
    int32_t noiseOctaves_ = 4;
};

/**
 * Distance filter - removes points too close together
 */
class PCGDistanceFilterNode : public PCGNode {
public:
    std::string getName() const override { return "Distance Filter"; }
    std::string getCategory() const override { return "Filters"; }
    
    std::vector<PCGPin> getInputPins() const override;
    std::vector<PCGPin> getOutputPins() const override;
    
    bool execute(PCGContext& context,
                const std::vector<PCGData>& inputs,
                std::vector<PCGData>& outputs) override;
    
    std::vector<Setting> getDefaultSettings() const override;
    
private:
    float minDistance_ = 1.0f;
    enum class Mode { Random, Priority, Ordered } mode_ = Mode::Random;
};

/**
 * Bounds filter - keeps only points within bounds
 */
class PCGBoundsFilterNode : public PCGNode {
public:
    std::string getName() const override { return "Bounds Filter"; }
    std::string getCategory() const override { return "Filters"; }
    
    std::vector<PCGPin> getInputPins() const override;
    std::vector<PCGPin> getOutputPins() const override;
    
    bool execute(PCGContext& context,
                const std::vector<PCGData>& inputs,
                std::vector<PCGData>& outputs) override;
    
    std::vector<Setting> getDefaultSettings() const override;
    
private:
    glm::vec3 boundsMin_;
    glm::vec3 boundsMax_;
    bool invert_ = false;
};

/**
 * Layer filter - filters based on landscape layer weights
 */
class PCGLayerFilterNode : public PCGNode {
public:
    std::string getName() const override { return "Layer Filter"; }
    std::string getCategory() const override { return "Filters"; }
    
    std::vector<PCGPin> getInputPins() const override;
    std::vector<PCGPin> getOutputPins() const override;
    
    bool execute(PCGContext& context,
                const std::vector<PCGData>& inputs,
                std::vector<PCGData>& outputs) override;
    
    std::vector<Setting> getDefaultSettings() const override;
    
private:
    uint32_t layerIndex_ = 0;
    float minWeight_ = 0.5f;
    float maxWeight_ = 1.0f;
};

//------------------------------------------------------------------------------
// Transform Nodes
//------------------------------------------------------------------------------

/**
 * Transform points - apply position, rotation, scale
 */
class PCGTransformNode : public PCGNode {
public:
    std::string getName() const override { return "Transform"; }
    std::string getCategory() const override { return "Transform"; }
    
    std::vector<PCGPin> getInputPins() const override;
    std::vector<PCGPin> getOutputPins() const override;
    
    bool execute(PCGContext& context,
                const std::vector<PCGData>& inputs,
                std::vector<PCGData>& outputs) override;
    
    std::vector<Setting> getDefaultSettings() const override;
    
private:
    glm::vec3 offsetMin_ = glm::vec3(0);
    glm::vec3 offsetMax_ = glm::vec3(0);
    glm::vec3 rotationMin_ = glm::vec3(0);
    glm::vec3 rotationMax_ = glm::vec3(360, 0, 0);  // Random Y rotation by default
    glm::vec3 scaleMin_ = glm::vec3(1);
    glm::vec3 scaleMax_ = glm::vec3(1);
    bool uniformScale_ = true;
};

/**
 * Project to surface - projects points onto landscape
 */
class PCGProjectToSurfaceNode : public PCGNode {
public:
    std::string getName() const override { return "Project To Surface"; }
    std::string getCategory() const override { return "Transform"; }
    
    std::vector<PCGPin> getInputPins() const override;
    std::vector<PCGPin> getOutputPins() const override;
    
    bool execute(PCGContext& context,
                const std::vector<PCGData>& inputs,
                std::vector<PCGData>& outputs) override;
    
    std::vector<Setting> getDefaultSettings() const override;
    
private:
    float verticalOffset_ = 0.0f;
    bool alignToNormal_ = false;
};

//------------------------------------------------------------------------------
// Spawner Nodes
//------------------------------------------------------------------------------

/**
 * Static mesh spawner - spawns meshes at points
 */
class PCGStaticMeshSpawnerNode : public PCGNode {
public:
    std::string getName() const override { return "Static Mesh Spawner"; }
    std::string getCategory() const override { return "Spawners"; }
    
    std::vector<PCGPin> getInputPins() const override;
    std::vector<PCGPin> getOutputPins() const override;
    
    bool execute(PCGContext& context,
                const std::vector<PCGData>& inputs,
                std::vector<PCGData>& outputs) override;
    
    std::vector<Setting> getDefaultSettings() const override;
    
    void setMeshAssets(const std::vector<std::string>& meshPaths);
    
private:
    std::vector<std::string> meshPaths_;
    std::vector<float> meshWeights_;
    bool useInstancedRendering_ = true;
    float cullDistance_ = 10000.0f;
};

/**
 * Foliage spawner - spawns foliage instances
 */
class PCGFoliageSpawnerNode : public PCGNode {
public:
    std::string getName() const override { return "Foliage Spawner"; }
    std::string getCategory() const override { return "Spawners"; }
    
    std::vector<PCGPin> getInputPins() const override;
    std::vector<PCGPin> getOutputPins() const override;
    
    bool execute(PCGContext& context,
                const std::vector<PCGData>& inputs,
                std::vector<PCGData>& outputs) override;
    
    std::vector<Setting> getDefaultSettings() const override;
    
    void setFoliageType(uint32_t foliageTypeId);
    
private:
    uint32_t foliageTypeId_ = 0;
};

//------------------------------------------------------------------------------
// PCG Graph
//------------------------------------------------------------------------------

/**
 * Connection between nodes
 */
struct PCGConnection {
    uint32_t sourceNode;
    uint32_t sourcePin;
    uint32_t targetNode;
    uint32_t targetPin;
};

/**
 * PCG graph containing nodes and connections
 */
class PCGGraph {
public:
    PCGGraph();
    ~PCGGraph();
    
    // Graph building
    uint32_t addNode(std::unique_ptr<PCGNode> node);
    void removeNode(uint32_t nodeId);
    PCGNode* getNode(uint32_t nodeId);
    
    bool connect(uint32_t sourceNode, uint32_t sourcePin,
                 uint32_t targetNode, uint32_t targetPin);
    void disconnect(uint32_t targetNode, uint32_t targetPin);
    
    // Execution
    bool execute(PCGContext& context);
    bool executePartial(PCGContext& context, const std::vector<uint32_t>& nodeIds);
    
    // Serialization
    bool save(const std::string& path) const;
    bool load(const std::string& path);
    
    // Validation
    bool validate() const;
    std::vector<std::string> getValidationErrors() const;
    
    // Graph info
    const std::vector<std::unique_ptr<PCGNode>>& getNodes() const { return nodes_; }
    const std::vector<PCGConnection>& getConnections() const { return connections_; }
    
private:
    std::vector<std::unique_ptr<PCGNode>> nodes_;
    std::vector<PCGConnection> connections_;
    uint32_t nextNodeId_ = 1;
    
    // Execution order (topologically sorted)
    std::vector<uint32_t> executionOrder_;
    bool orderDirty_ = true;
    
    void updateExecutionOrder();
};

//------------------------------------------------------------------------------
// PCG Framework
//------------------------------------------------------------------------------

/**
 * PCG Framework managing procedural generation
 */
class PCGFramework {
public:
    PCGFramework();
    ~PCGFramework();
    
    // Initialization
    bool initialize(VulkanRenderer* renderer);
    void shutdown();
    
    // Landscape/foliage integration
    void setLandscapeSystem(LandscapeSystem* landscape);
    void setFoliageSystem(FoliageSystem* foliage);
    
    // Graph management
    uint32_t createGraph(const std::string& name);
    void destroyGraph(uint32_t graphId);
    PCGGraph* getGraph(uint32_t graphId);
    
    // Execution
    bool executeGraph(uint32_t graphId, const PCGContext& baseContext);
    bool executeGraphInBounds(uint32_t graphId, const glm::vec3& boundsMin, 
                               const glm::vec3& boundsMax, int32_t seed);
    
    // Generation presets
    void generateForest(const glm::vec3& boundsMin, const glm::vec3& boundsMax,
                        int32_t seed, float density);
    void generateRocks(const glm::vec3& boundsMin, const glm::vec3& boundsMax,
                       int32_t seed, float density);
    void populateSpline(const PCGSplineData& spline, int32_t seed);
    
    // Node factory
    std::unique_ptr<PCGNode> createNode(const std::string& typeName);
    std::vector<std::string> getAvailableNodeTypes() const;
    
    // Debug
    void drawDebugUI();
    
private:
    VulkanRenderer* m_renderer = nullptr;
    LandscapeSystem* m_landscape = nullptr;
    FoliageSystem* m_foliage = nullptr;
    
    std::unordered_map<uint32_t, std::unique_ptr<PCGGraph>> m_graphs;
    uint32_t m_nextGraphId = 1;
    
    // Node type registry
    using NodeFactory = std::function<std::unique_ptr<PCGNode>()>;
    std::unordered_map<std::string, NodeFactory> m_nodeFactories;
    
    void registerDefaultNodes();
};

} // namespace Kinetic
