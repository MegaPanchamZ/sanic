/**
 * MaterialGraph.h
 * 
 * Node graph data structure for visual material editing.
 * Manages nodes, connections, and graph traversal for shader generation.
 */

#pragma once

#include "MaterialNode.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <optional>

namespace Sanic {

/**
 * Represents a connection between two material nodes
 */
struct MaterialConnection {
    uint64_t id = 0;
    
    // Source (output pin)
    uint64_t sourceNodeId = 0;
    uint32_t sourcePin = 0;
    
    // Target (input pin)
    uint64_t targetNodeId = 0;
    uint32_t targetPin = 0;
    
    bool operator==(const MaterialConnection& other) const {
        return sourceNodeId == other.sourceNodeId &&
               sourcePin == other.sourcePin &&
               targetNodeId == other.targetNodeId &&
               targetPin == other.targetPin;
    }
};

/**
 * Validation error/warning
 */
struct MaterialGraphDiagnostic {
    enum class Severity {
        Info,
        Warning,
        Error
    };
    
    Severity severity = Severity::Error;
    uint64_t nodeId = 0;          // 0 for graph-level issues
    std::string pinName;          // Empty for node-level issues
    std::string message;
};

/**
 * Material domain - affects what inputs are available
 */
enum class MaterialDomain {
    Surface,          // Standard 3D surfaces
    PostProcess,      // Post-process effects
    UI,               // UI elements
    Decal,            // Decal projections
    LightFunction,    // Light functions
    VolumetricFog,    // Volumetric effects
    Sky               // Sky rendering
};

/**
 * Material blend mode
 */
enum class MaterialBlendMode {
    Opaque,
    Masked,
    Translucent,
    Additive,
    Modulate
};

/**
 * Material shading model
 */
enum class MaterialShadingModel {
    Unlit,
    DefaultLit,         // Standard PBR
    Subsurface,
    ClearCoat,
    Cloth,
    Eye,
    Hair,
    ThinTranslucent
};

/**
 * Material graph - contains all nodes and connections
 */
class MaterialGraph {
public:
    MaterialGraph();
    ~MaterialGraph() = default;
    
    // Graph info
    std::string name = "New Material";
    std::string description;
    MaterialDomain domain = MaterialDomain::Surface;
    MaterialBlendMode blendMode = MaterialBlendMode::Opaque;
    MaterialShadingModel shadingModel = MaterialShadingModel::DefaultLit;
    bool twoSided = false;
    bool wireframe = false;
    
    // --------------------------------------------------------------------------
    // Node Management
    // --------------------------------------------------------------------------
    
    /**
     * Add a node to the graph
     * @return Pointer to the added node
     */
    MaterialNode* addNode(std::unique_ptr<MaterialNode> node);
    
    /**
     * Create and add a node by type name
     * @param typeName Registered node type name
     * @return Pointer to created node, or nullptr if type not found
     */
    MaterialNode* createNode(const std::string& typeName);
    
    /**
     * Remove a node and all its connections
     * @param nodeId Node to remove
     * @return True if node was found and removed
     */
    bool removeNode(uint64_t nodeId);
    
    /**
     * Get a node by ID
     * @return Node pointer or nullptr
     */
    MaterialNode* getNode(uint64_t nodeId) const;
    
    /**
     * Get all nodes
     */
    const std::unordered_map<uint64_t, std::unique_ptr<MaterialNode>>& getNodes() const { 
        return m_Nodes; 
    }
    
    /**
     * Get the output node (there should always be exactly one)
     */
    MaterialNode* getOutputNode() const;
    
    /**
     * Get nodes by category
     */
    std::vector<MaterialNode*> getNodesByCategory(const std::string& category) const;
    
    // --------------------------------------------------------------------------
    // Connection Management
    // --------------------------------------------------------------------------
    
    /**
     * Connect two nodes
     * @param sourceNodeId Source node (has output pin)
     * @param sourcePinIndex Output pin index
     * @param targetNodeId Target node (has input pin)
     * @param targetPinIndex Input pin index
     * @return Connection ID, or 0 if connection failed
     */
    uint64_t connect(uint64_t sourceNodeId, uint32_t sourcePinIndex,
                     uint64_t targetNodeId, uint32_t targetPinIndex);
    
    /**
     * Disconnect a connection by ID
     */
    bool disconnect(uint64_t connectionId);
    
    /**
     * Disconnect all connections to/from a specific pin
     */
    void disconnectPin(uint64_t nodeId, uint32_t pinIndex, bool isInput);
    
    /**
     * Get connection by ID
     */
    const MaterialConnection* getConnection(uint64_t connectionId) const;
    
    /**
     * Get all connections
     */
    const std::unordered_map<uint64_t, MaterialConnection>& getConnections() const { 
        return m_Connections; 
    }
    
    /**
     * Get connections to a specific input pin
     */
    std::optional<MaterialConnection> getInputConnection(uint64_t nodeId, uint32_t pinIndex) const;
    
    /**
     * Get all connections from an output pin
     */
    std::vector<MaterialConnection> getOutputConnections(uint64_t nodeId, uint32_t pinIndex) const;
    
    /**
     * Check if connecting two pins would create a cycle
     */
    bool wouldCreateCycle(uint64_t sourceNodeId, uint64_t targetNodeId) const;
    
    /**
     * Check if two pin types are compatible for connection
     */
    bool areTypesCompatible(MaterialValueType sourceType, MaterialValueType targetType) const;
    
    // --------------------------------------------------------------------------
    // Graph Analysis
    // --------------------------------------------------------------------------
    
    /**
     * Perform topological sort of nodes (for code generation order)
     * @return Nodes in dependency order (sources first)
     */
    std::vector<MaterialNode*> topologicalSort() const;
    
    /**
     * Get all nodes that the given node depends on (directly or indirectly)
     */
    std::unordered_set<uint64_t> getDependencies(uint64_t nodeId) const;
    
    /**
     * Get all nodes that depend on the given node
     */
    std::unordered_set<uint64_t> getDependents(uint64_t nodeId) const;
    
    /**
     * Find unconnected required inputs
     */
    std::vector<std::pair<MaterialNode*, uint32_t>> getUnconnectedRequiredInputs() const;
    
    /**
     * Find nodes with no outputs connected (excluding output node)
     */
    std::vector<MaterialNode*> getOrphanedNodes() const;
    
    // --------------------------------------------------------------------------
    // Validation
    // --------------------------------------------------------------------------
    
    /**
     * Validate the entire graph
     * @return List of diagnostics (empty if valid)
     */
    std::vector<MaterialGraphDiagnostic> validate() const;
    
    /**
     * Check if graph is valid for compilation
     */
    bool isValid() const;
    
    // --------------------------------------------------------------------------
    // Serialization
    // --------------------------------------------------------------------------
    
    /**
     * Serialize graph to JSON
     */
    nlohmann::json serialize() const;
    
    /**
     * Deserialize graph from JSON
     */
    static std::unique_ptr<MaterialGraph> deserialize(const nlohmann::json& json);
    
    /**
     * Save graph to file
     */
    bool saveToFile(const std::string& path) const;
    
    /**
     * Load graph from file
     */
    static std::unique_ptr<MaterialGraph> loadFromFile(const std::string& path);
    
    // --------------------------------------------------------------------------
    // Editing Helpers
    // --------------------------------------------------------------------------
    
    /**
     * Duplicate selected nodes
     */
    std::vector<MaterialNode*> duplicateNodes(const std::vector<uint64_t>& nodeIds);
    
    /**
     * Group selected nodes into a subgraph (for organization)
     */
    // MaterialNodeGroup* groupNodes(const std::vector<uint64_t>& nodeIds, const std::string& name);
    
    /**
     * Clear the entire graph (except output node)
     */
    void clear();
    
    /**
     * Reset to default state
     */
    void reset();
    
    // --------------------------------------------------------------------------
    // Callbacks
    // --------------------------------------------------------------------------
    
    using NodeCallback = std::function<void(MaterialNode*)>;
    using ConnectionCallback = std::function<void(const MaterialConnection&)>;
    
    void setOnNodeAdded(NodeCallback callback) { m_OnNodeAdded = callback; }
    void setOnNodeRemoved(NodeCallback callback) { m_OnNodeRemoved = callback; }
    void setOnConnectionAdded(ConnectionCallback callback) { m_OnConnectionAdded = callback; }
    void setOnConnectionRemoved(ConnectionCallback callback) { m_OnConnectionRemoved = callback; }
    
    // Mark graph as dirty (needs recompilation)
    void markDirty() { m_IsDirty = true; }
    bool isDirty() const { return m_IsDirty; }
    void clearDirty() { m_IsDirty = false; }
    
private:
    // ID generation
    uint64_t generateNodeId();
    uint64_t generateConnectionId();
    
    // Cycle detection helper
    bool hasCycleFrom(uint64_t nodeId, std::unordered_set<uint64_t>& visited,
                      std::unordered_set<uint64_t>& recursionStack) const;
    
    // Nodes and connections
    std::unordered_map<uint64_t, std::unique_ptr<MaterialNode>> m_Nodes;
    std::unordered_map<uint64_t, MaterialConnection> m_Connections;
    
    // Quick lookup: input pin -> connection
    std::unordered_map<uint64_t, std::unordered_map<uint32_t, uint64_t>> m_InputConnections;
    
    // Quick lookup: output pin -> connections
    std::unordered_map<uint64_t, std::unordered_map<uint32_t, std::vector<uint64_t>>> m_OutputConnections;
    
    // ID counters
    uint64_t m_NextNodeId = 1;
    uint64_t m_NextConnectionId = 1;
    
    // Output node (always present)
    uint64_t m_OutputNodeId = 0;
    
    // State
    bool m_IsDirty = true;
    
    // Callbacks
    NodeCallback m_OnNodeAdded;
    NodeCallback m_OnNodeRemoved;
    ConnectionCallback m_OnConnectionAdded;
    ConnectionCallback m_OnConnectionRemoved;
};

} // namespace Sanic
