/**
 * MaterialGraph.cpp
 * 
 * Implementation of material node graph.
 */

#include "MaterialGraph.h"
#include "MaterialNodes/CommonNodes.h"
#include <algorithm>
#include <queue>
#include <stack>
#include <fstream>

namespace Sanic {

MaterialGraph::MaterialGraph() {
    // Create the output node (always required)
    auto outputNode = std::make_unique<MaterialOutputNode>();
    outputNode->position = glm::vec2(500.0f, 300.0f);
    m_OutputNodeId = generateNodeId();
    outputNode->id = m_OutputNodeId;
    m_Nodes[m_OutputNodeId] = std::move(outputNode);
}

// ============================================================================
// Node Management
// ============================================================================

MaterialNode* MaterialGraph::addNode(std::unique_ptr<MaterialNode> node) {
    if (!node) return nullptr;
    
    uint64_t nodeId = generateNodeId();
    node->id = nodeId;
    
    MaterialNode* nodePtr = node.get();
    m_Nodes[nodeId] = std::move(node);
    
    markDirty();
    
    if (m_OnNodeAdded) {
        m_OnNodeAdded(nodePtr);
    }
    
    return nodePtr;
}

MaterialNode* MaterialGraph::createNode(const std::string& typeName) {
    auto node = MaterialNodeFactory::instance().create(typeName);
    if (!node) {
        return nullptr;
    }
    return addNode(std::move(node));
}

bool MaterialGraph::removeNode(uint64_t nodeId) {
    // Cannot remove the output node
    if (nodeId == m_OutputNodeId) {
        return false;
    }
    
    auto it = m_Nodes.find(nodeId);
    if (it == m_Nodes.end()) {
        return false;
    }
    
    MaterialNode* nodePtr = it->second.get();
    
    // Remove all connections to/from this node
    std::vector<uint64_t> connectionsToRemove;
    for (const auto& [connId, conn] : m_Connections) {
        if (conn.sourceNodeId == nodeId || conn.targetNodeId == nodeId) {
            connectionsToRemove.push_back(connId);
        }
    }
    
    for (uint64_t connId : connectionsToRemove) {
        disconnect(connId);
    }
    
    // Notify before removal
    if (m_OnNodeRemoved) {
        m_OnNodeRemoved(nodePtr);
    }
    
    m_Nodes.erase(it);
    markDirty();
    
    return true;
}

MaterialNode* MaterialGraph::getNode(uint64_t nodeId) const {
    auto it = m_Nodes.find(nodeId);
    return (it != m_Nodes.end()) ? it->second.get() : nullptr;
}

MaterialNode* MaterialGraph::getOutputNode() const {
    return getNode(m_OutputNodeId);
}

std::vector<MaterialNode*> MaterialGraph::getNodesByCategory(const std::string& category) const {
    std::vector<MaterialNode*> result;
    for (const auto& [id, node] : m_Nodes) {
        if (node->getCategory() == category) {
            result.push_back(node.get());
        }
    }
    return result;
}

// ============================================================================
// Connection Management
// ============================================================================

uint64_t MaterialGraph::connect(uint64_t sourceNodeId, uint32_t sourcePinIndex,
                                 uint64_t targetNodeId, uint32_t targetPinIndex) {
    // Validate nodes exist
    MaterialNode* sourceNode = getNode(sourceNodeId);
    MaterialNode* targetNode = getNode(targetNodeId);
    
    if (!sourceNode || !targetNode) {
        return 0;
    }
    
    // Validate pin indices
    const auto& outputs = sourceNode->getOutputPins();
    const auto& inputs = targetNode->getInputPins();
    
    if (sourcePinIndex >= outputs.size() || targetPinIndex >= inputs.size()) {
        return 0;
    }
    
    const MaterialPin& sourcePin = outputs[sourcePinIndex];
    const MaterialPin& targetPin = inputs[targetPinIndex];
    
    // Check type compatibility
    if (!areTypesCompatible(sourcePin.type, targetPin.type)) {
        return 0;
    }
    
    // Check for cycles
    if (wouldCreateCycle(sourceNodeId, targetNodeId)) {
        return 0;
    }
    
    // Remove existing connection to this input (inputs can only have one connection)
    if (m_InputConnections.count(targetNodeId) && 
        m_InputConnections[targetNodeId].count(targetPinIndex)) {
        disconnect(m_InputConnections[targetNodeId][targetPinIndex]);
    }
    
    // Create connection
    MaterialConnection conn;
    conn.id = generateConnectionId();
    conn.sourceNodeId = sourceNodeId;
    conn.sourcePin = sourcePinIndex;
    conn.targetNodeId = targetNodeId;
    conn.targetPin = targetPinIndex;
    
    m_Connections[conn.id] = conn;
    m_InputConnections[targetNodeId][targetPinIndex] = conn.id;
    m_OutputConnections[sourceNodeId][sourcePinIndex].push_back(conn.id);
    
    markDirty();
    
    if (m_OnConnectionAdded) {
        m_OnConnectionAdded(conn);
    }
    
    return conn.id;
}

bool MaterialGraph::disconnect(uint64_t connectionId) {
    auto it = m_Connections.find(connectionId);
    if (it == m_Connections.end()) {
        return false;
    }
    
    const MaterialConnection& conn = it->second;
    
    // Remove from lookup tables
    if (m_InputConnections.count(conn.targetNodeId)) {
        m_InputConnections[conn.targetNodeId].erase(conn.targetPin);
    }
    
    if (m_OutputConnections.count(conn.sourceNodeId)) {
        auto& outputs = m_OutputConnections[conn.sourceNodeId][conn.sourcePin];
        outputs.erase(std::remove(outputs.begin(), outputs.end(), connectionId), outputs.end());
    }
    
    if (m_OnConnectionRemoved) {
        m_OnConnectionRemoved(conn);
    }
    
    m_Connections.erase(it);
    markDirty();
    
    return true;
}

void MaterialGraph::disconnectPin(uint64_t nodeId, uint32_t pinIndex, bool isInput) {
    std::vector<uint64_t> toRemove;
    
    if (isInput) {
        if (m_InputConnections.count(nodeId) && m_InputConnections[nodeId].count(pinIndex)) {
            toRemove.push_back(m_InputConnections[nodeId][pinIndex]);
        }
    } else {
        if (m_OutputConnections.count(nodeId) && m_OutputConnections[nodeId].count(pinIndex)) {
            toRemove = m_OutputConnections[nodeId][pinIndex];
        }
    }
    
    for (uint64_t connId : toRemove) {
        disconnect(connId);
    }
}

const MaterialConnection* MaterialGraph::getConnection(uint64_t connectionId) const {
    auto it = m_Connections.find(connectionId);
    return (it != m_Connections.end()) ? &it->second : nullptr;
}

std::optional<MaterialConnection> MaterialGraph::getInputConnection(uint64_t nodeId, uint32_t pinIndex) const {
    auto nodeIt = m_InputConnections.find(nodeId);
    if (nodeIt == m_InputConnections.end()) {
        return std::nullopt;
    }
    
    auto pinIt = nodeIt->second.find(pinIndex);
    if (pinIt == nodeIt->second.end()) {
        return std::nullopt;
    }
    
    auto connIt = m_Connections.find(pinIt->second);
    if (connIt == m_Connections.end()) {
        return std::nullopt;
    }
    
    return connIt->second;
}

std::vector<MaterialConnection> MaterialGraph::getOutputConnections(uint64_t nodeId, uint32_t pinIndex) const {
    std::vector<MaterialConnection> result;
    
    auto nodeIt = m_OutputConnections.find(nodeId);
    if (nodeIt == m_OutputConnections.end()) {
        return result;
    }
    
    auto pinIt = nodeIt->second.find(pinIndex);
    if (pinIt == nodeIt->second.end()) {
        return result;
    }
    
    for (uint64_t connId : pinIt->second) {
        auto connIt = m_Connections.find(connId);
        if (connIt != m_Connections.end()) {
            result.push_back(connIt->second);
        }
    }
    
    return result;
}

bool MaterialGraph::wouldCreateCycle(uint64_t sourceNodeId, uint64_t targetNodeId) const {
    if (sourceNodeId == targetNodeId) {
        return true;
    }
    
    // BFS from target to see if we can reach source
    std::queue<uint64_t> queue;
    std::unordered_set<uint64_t> visited;
    
    queue.push(targetNodeId);
    visited.insert(targetNodeId);
    
    while (!queue.empty()) {
        uint64_t current = queue.front();
        queue.pop();
        
        // Check all outputs from current node
        auto outputIt = m_OutputConnections.find(current);
        if (outputIt != m_OutputConnections.end()) {
            for (const auto& [pinIndex, connIds] : outputIt->second) {
                for (uint64_t connId : connIds) {
                    auto connIt = m_Connections.find(connId);
                    if (connIt != m_Connections.end()) {
                        uint64_t nextNode = connIt->second.targetNodeId;
                        
                        if (nextNode == sourceNodeId) {
                            return true; // Found cycle!
                        }
                        
                        if (!visited.count(nextNode)) {
                            visited.insert(nextNode);
                            queue.push(nextNode);
                        }
                    }
                }
            }
        }
    }
    
    return false;
}

bool MaterialGraph::areTypesCompatible(MaterialValueType sourceType, MaterialValueType targetType) const {
    // Use the static function from MaterialNode
    return MaterialNode::areTypesCompatible(sourceType, targetType);
}

// ============================================================================
// Graph Analysis
// ============================================================================

std::vector<MaterialNode*> MaterialGraph::topologicalSort() const {
    std::vector<MaterialNode*> result;
    std::unordered_map<uint64_t, int> inDegree;
    
    // Initialize in-degree counts
    for (const auto& [id, node] : m_Nodes) {
        inDegree[id] = 0;
    }
    
    // Count incoming edges for each node
    for (const auto& [connId, conn] : m_Connections) {
        inDegree[conn.targetNodeId]++;
    }
    
    // Start with nodes that have no incoming edges
    std::queue<uint64_t> queue;
    for (const auto& [id, degree] : inDegree) {
        if (degree == 0) {
            queue.push(id);
        }
    }
    
    // Process nodes
    while (!queue.empty()) {
        uint64_t nodeId = queue.front();
        queue.pop();
        
        MaterialNode* node = getNode(nodeId);
        if (node) {
            result.push_back(node);
        }
        
        // Decrease in-degree of connected nodes
        auto outputIt = m_OutputConnections.find(nodeId);
        if (outputIt != m_OutputConnections.end()) {
            for (const auto& [pinIndex, connIds] : outputIt->second) {
                for (uint64_t connId : connIds) {
                    auto connIt = m_Connections.find(connId);
                    if (connIt != m_Connections.end()) {
                        uint64_t targetId = connIt->second.targetNodeId;
                        inDegree[targetId]--;
                        if (inDegree[targetId] == 0) {
                            queue.push(targetId);
                        }
                    }
                }
            }
        }
    }
    
    return result;
}

std::unordered_set<uint64_t> MaterialGraph::getDependencies(uint64_t nodeId) const {
    std::unordered_set<uint64_t> result;
    std::stack<uint64_t> stack;
    stack.push(nodeId);
    
    while (!stack.empty()) {
        uint64_t current = stack.top();
        stack.pop();
        
        // Find all nodes that connect to this node's inputs
        auto inputIt = m_InputConnections.find(current);
        if (inputIt != m_InputConnections.end()) {
            for (const auto& [pinIndex, connId] : inputIt->second) {
                auto connIt = m_Connections.find(connId);
                if (connIt != m_Connections.end()) {
                    uint64_t sourceId = connIt->second.sourceNodeId;
                    if (!result.count(sourceId)) {
                        result.insert(sourceId);
                        stack.push(sourceId);
                    }
                }
            }
        }
    }
    
    return result;
}

std::unordered_set<uint64_t> MaterialGraph::getDependents(uint64_t nodeId) const {
    std::unordered_set<uint64_t> result;
    std::stack<uint64_t> stack;
    stack.push(nodeId);
    
    while (!stack.empty()) {
        uint64_t current = stack.top();
        stack.pop();
        
        // Find all nodes that this node's outputs connect to
        auto outputIt = m_OutputConnections.find(current);
        if (outputIt != m_OutputConnections.end()) {
            for (const auto& [pinIndex, connIds] : outputIt->second) {
                for (uint64_t connId : connIds) {
                    auto connIt = m_Connections.find(connId);
                    if (connIt != m_Connections.end()) {
                        uint64_t targetId = connIt->second.targetNodeId;
                        if (!result.count(targetId)) {
                            result.insert(targetId);
                            stack.push(targetId);
                        }
                    }
                }
            }
        }
    }
    
    return result;
}

std::vector<std::pair<MaterialNode*, uint32_t>> MaterialGraph::getUnconnectedRequiredInputs() const {
    std::vector<std::pair<MaterialNode*, uint32_t>> result;
    
    for (const auto& [nodeId, node] : m_Nodes) {
        const auto& inputs = node->getInputPins();
        for (uint32_t i = 0; i < inputs.size(); i++) {
            const MaterialPin& pin = inputs[i];
            
            // Check if required and not connected
            if (!pin.optional) {
                bool hasConnection = m_InputConnections.count(nodeId) && 
                                     m_InputConnections.at(nodeId).count(i);
                if (!hasConnection) {
                    result.emplace_back(node.get(), i);
                }
            }
        }
    }
    
    return result;
}

std::vector<MaterialNode*> MaterialGraph::getOrphanedNodes() const {
    std::vector<MaterialNode*> result;
    
    // Get all nodes that contribute to the output
    std::unordered_set<uint64_t> connected = getDependencies(m_OutputNodeId);
    connected.insert(m_OutputNodeId);
    
    for (const auto& [nodeId, node] : m_Nodes) {
        if (!connected.count(nodeId)) {
            result.push_back(node.get());
        }
    }
    
    return result;
}

// ============================================================================
// Validation
// ============================================================================

std::vector<MaterialGraphDiagnostic> MaterialGraph::validate() const {
    std::vector<MaterialGraphDiagnostic> diagnostics;
    
    // Check for unconnected required inputs
    for (const auto& [node, pinIndex] : getUnconnectedRequiredInputs()) {
        MaterialGraphDiagnostic diag;
        diag.severity = MaterialGraphDiagnostic::Severity::Error;
        diag.nodeId = node->id;
        diag.pinName = node->getInputPins()[pinIndex].name;
        diag.message = "Required input '" + diag.pinName + "' is not connected";
        diagnostics.push_back(diag);
    }
    
    // Check for orphaned nodes (warning only)
    for (MaterialNode* node : getOrphanedNodes()) {
        MaterialGraphDiagnostic diag;
        diag.severity = MaterialGraphDiagnostic::Severity::Warning;
        diag.nodeId = node->id;
        diag.message = "Node '" + node->getName() + "' is not connected to the output";
        diagnostics.push_back(diag);
    }
    
    // Validate individual nodes
    for (const auto& [nodeId, node] : m_Nodes) {
        std::string error;
        if (!node->validate(error)) {
            MaterialGraphDiagnostic diag;
            diag.severity = MaterialGraphDiagnostic::Severity::Error;
            diag.nodeId = nodeId;
            diag.message = error;
            diagnostics.push_back(diag);
        }
    }
    
    // Check for cycles (should be prevented, but double check)
    std::unordered_set<uint64_t> visited;
    std::unordered_set<uint64_t> recursionStack;
    for (const auto& [nodeId, node] : m_Nodes) {
        if (hasCycleFrom(nodeId, visited, recursionStack)) {
            MaterialGraphDiagnostic diag;
            diag.severity = MaterialGraphDiagnostic::Severity::Error;
            diag.nodeId = nodeId;
            diag.message = "Cycle detected in material graph";
            diagnostics.push_back(diag);
            break;
        }
    }
    
    return diagnostics;
}

bool MaterialGraph::isValid() const {
    auto diagnostics = validate();
    for (const auto& diag : diagnostics) {
        if (diag.severity == MaterialGraphDiagnostic::Severity::Error) {
            return false;
        }
    }
    return true;
}

bool MaterialGraph::hasCycleFrom(uint64_t nodeId, std::unordered_set<uint64_t>& visited,
                                  std::unordered_set<uint64_t>& recursionStack) const {
    if (recursionStack.count(nodeId)) {
        return true;
    }
    if (visited.count(nodeId)) {
        return false;
    }
    
    visited.insert(nodeId);
    recursionStack.insert(nodeId);
    
    auto outputIt = m_OutputConnections.find(nodeId);
    if (outputIt != m_OutputConnections.end()) {
        for (const auto& [pinIndex, connIds] : outputIt->second) {
            for (uint64_t connId : connIds) {
                auto connIt = m_Connections.find(connId);
                if (connIt != m_Connections.end()) {
                    if (hasCycleFrom(connIt->second.targetNodeId, visited, recursionStack)) {
                        return true;
                    }
                }
            }
        }
    }
    
    recursionStack.erase(nodeId);
    return false;
}

// ============================================================================
// Serialization
// ============================================================================

nlohmann::json MaterialGraph::serialize() const {
    nlohmann::json json;
    
    // Graph properties
    json["name"] = name;
    json["description"] = description;
    json["domain"] = static_cast<int>(domain);
    json["blendMode"] = static_cast<int>(blendMode);
    json["shadingModel"] = static_cast<int>(shadingModel);
    json["twoSided"] = twoSided;
    json["wireframe"] = wireframe;
    
    // Nodes
    json["nodes"] = nlohmann::json::array();
    for (const auto& [nodeId, node] : m_Nodes) {
        nlohmann::json nodeJson = MaterialSerializer::serializeNode(node.get());
        nodeJson["id"] = nodeId;
        json["nodes"].push_back(nodeJson);
    }
    
    // Connections
    json["connections"] = nlohmann::json::array();
    for (const auto& [connId, conn] : m_Connections) {
        nlohmann::json connJson;
        connJson["id"] = conn.id;
        connJson["sourceNodeId"] = conn.sourceNodeId;
        connJson["sourcePin"] = conn.sourcePin;
        connJson["targetNodeId"] = conn.targetNodeId;
        connJson["targetPin"] = conn.targetPin;
        json["connections"].push_back(connJson);
    }
    
    json["outputNodeId"] = m_OutputNodeId;
    json["nextNodeId"] = m_NextNodeId;
    json["nextConnectionId"] = m_NextConnectionId;
    
    return json;
}

std::unique_ptr<MaterialGraph> MaterialGraph::deserialize(const nlohmann::json& json) {
    auto graph = std::make_unique<MaterialGraph>();
    
    // Clear default output node (will be loaded from JSON)
    graph->m_Nodes.clear();
    graph->m_Connections.clear();
    graph->m_InputConnections.clear();
    graph->m_OutputConnections.clear();
    
    // Graph properties
    graph->name = json.value("name", "Untitled");
    graph->description = json.value("description", "");
    graph->domain = static_cast<MaterialDomain>(json.value("domain", 0));
    graph->blendMode = static_cast<MaterialBlendMode>(json.value("blendMode", 0));
    graph->shadingModel = static_cast<MaterialShadingModel>(json.value("shadingModel", 1));
    graph->twoSided = json.value("twoSided", false);
    graph->wireframe = json.value("wireframe", false);
    
    // Load nodes
    if (json.contains("nodes")) {
        for (const auto& nodeJson : json["nodes"]) {
            auto node = MaterialSerializer::deserializeNode(nodeJson);
            if (node) {
                uint64_t nodeId = nodeJson.value("id", graph->generateNodeId());
                node->id = nodeId;
                graph->m_Nodes[nodeId] = std::move(node);
            }
        }
    }
    
    // Load connections
    if (json.contains("connections")) {
        for (const auto& connJson : json["connections"]) {
            MaterialConnection conn;
            conn.id = connJson.value("id", graph->generateConnectionId());
            conn.sourceNodeId = connJson["sourceNodeId"];
            conn.sourcePin = connJson["sourcePin"];
            conn.targetNodeId = connJson["targetNodeId"];
            conn.targetPin = connJson["targetPin"];
            
            graph->m_Connections[conn.id] = conn;
            graph->m_InputConnections[conn.targetNodeId][conn.targetPin] = conn.id;
            graph->m_OutputConnections[conn.sourceNodeId][conn.sourcePin].push_back(conn.id);
        }
    }
    
    graph->m_OutputNodeId = json.value("outputNodeId", 1ULL);
    graph->m_NextNodeId = json.value("nextNodeId", 1ULL);
    graph->m_NextConnectionId = json.value("nextConnectionId", 1ULL);
    
    return graph;
}

bool MaterialGraph::saveToFile(const std::string& path) const {
    try {
        nlohmann::json json = serialize();
        
        std::ofstream file(path);
        if (!file.is_open()) {
            return false;
        }
        
        file << json.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

std::unique_ptr<MaterialGraph> MaterialGraph::loadFromFile(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            return nullptr;
        }
        
        nlohmann::json json = nlohmann::json::parse(file);
        return deserialize(json);
    } catch (...) {
        return nullptr;
    }
}

// ============================================================================
// Editing Helpers
// ============================================================================

std::vector<MaterialNode*> MaterialGraph::duplicateNodes(const std::vector<uint64_t>& nodeIds) {
    std::vector<MaterialNode*> result;
    std::unordered_map<uint64_t, uint64_t> idMapping;
    
    // First pass: duplicate nodes
    for (uint64_t oldId : nodeIds) {
        // Can't duplicate output node
        if (oldId == m_OutputNodeId) {
            continue;
        }
        
        MaterialNode* original = getNode(oldId);
        if (!original) continue;
        
        // Serialize and deserialize to create a copy
        nlohmann::json nodeJson = MaterialSerializer::serializeNode(original);
        auto newNode = MaterialSerializer::deserializeNode(nodeJson);
        
        if (newNode) {
            // Offset position
            newNode->position += glm::vec2(50.0f, 50.0f);
            
            MaterialNode* nodePtr = addNode(std::move(newNode));
            if (nodePtr) {
                idMapping[oldId] = nodePtr->id;
                result.push_back(nodePtr);
            }
        }
    }
    
    // Second pass: recreate connections between duplicated nodes
    for (uint64_t oldId : nodeIds) {
        auto outputIt = m_OutputConnections.find(oldId);
        if (outputIt != m_OutputConnections.end()) {
            for (const auto& [pinIndex, connIds] : outputIt->second) {
                for (uint64_t connId : connIds) {
                    auto connIt = m_Connections.find(connId);
                    if (connIt != m_Connections.end()) {
                        const MaterialConnection& conn = connIt->second;
                        
                        // Only recreate if both nodes were duplicated
                        if (idMapping.count(conn.sourceNodeId) && idMapping.count(conn.targetNodeId)) {
                            connect(idMapping[conn.sourceNodeId], conn.sourcePin,
                                   idMapping[conn.targetNodeId], conn.targetPin);
                        }
                    }
                }
            }
        }
    }
    
    return result;
}

void MaterialGraph::clear() {
    // Remove all nodes except output
    std::vector<uint64_t> toRemove;
    for (const auto& [id, node] : m_Nodes) {
        if (id != m_OutputNodeId) {
            toRemove.push_back(id);
        }
    }
    
    for (uint64_t id : toRemove) {
        removeNode(id);
    }
    
    markDirty();
}

void MaterialGraph::reset() {
    clear();
    
    name = "New Material";
    description = "";
    domain = MaterialDomain::Surface;
    blendMode = MaterialBlendMode::Opaque;
    shadingModel = MaterialShadingModel::DefaultLit;
    twoSided = false;
    wireframe = false;
    
    // Reset output node position
    MaterialNode* outputNode = getOutputNode();
    if (outputNode) {
        outputNode->position = glm::vec2(500.0f, 300.0f);
    }
    
    markDirty();
}

// ============================================================================
// ID Generation
// ============================================================================

uint64_t MaterialGraph::generateNodeId() {
    return m_NextNodeId++;
}

uint64_t MaterialGraph::generateConnectionId() {
    return m_NextConnectionId++;
}

} // namespace Sanic
