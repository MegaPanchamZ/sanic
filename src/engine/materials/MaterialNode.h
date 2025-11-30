/**
 * MaterialNode.h
 * 
 * Base class for material graph nodes.
 * Each node represents an operation or value in the material graph.
 * 
 * Features:
 * - Input/output pin system
 * - Type-safe connections
 * - Code generation interface
 * - Serialization support
 */

#pragma once

#include <string>
#include <vector>
#include <variant>
#include <memory>
#include <optional>
#include <functional>
#include <unordered_map>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Sanic {

// Forward declarations
class MaterialCompiler;
class MaterialGraph;

/**
 * Data types that can flow through material graph connections
 */
enum class MaterialValueType {
    Float,          // Single float value
    Float2,         // vec2
    Float3,         // vec3
    Float4,         // vec4
    Int,            // Integer
    Bool,           // Boolean
    Texture2D,      // 2D texture sampler
    Texture3D,      // 3D texture sampler
    TextureCube,    // Cubemap texture
    Sampler,        // Sampler state
    Matrix3,        // mat3
    Matrix4,        // mat4
};

/**
 * Get GLSL type string for a value type
 */
std::string getGLSLType(MaterialValueType type);

/**
 * Get number of components for a value type
 */
uint32_t getComponentCount(MaterialValueType type);

/**
 * Check if two types are compatible for connection
 */
bool areTypesCompatible(MaterialValueType from, MaterialValueType to);

/**
 * Default value for material pins
 */
using MaterialPinValue = std::variant<
    float,
    glm::vec2,
    glm::vec3,
    glm::vec4,
    int32_t,
    bool,
    std::string  // For texture paths
>;

/**
 * Connection to another node's pin (simple reference)
 */
struct MaterialNodeConnection {
    uint32_t nodeId = 0;       // ID of connected node
    uint32_t pinIndex = 0;     // Index of connected pin
    
    bool isValid() const { return nodeId != 0; }
};

/**
 * Input or output pin on a material node
 */
struct MaterialPin {
    std::string name;
    MaterialValueType type;
    bool isOutput;
    uint32_t id = 0;           // Unique ID within node
    
    // Default value (for unconnected inputs)
    MaterialPinValue defaultValue;
    
    // Connection (only for input pins)
    std::optional<MaterialNodeConnection> connection;
    
    // Whether this pin is hidden in UI
    bool hidden = false;
    
    // Whether this input is optional (can be unconnected)
    bool optional = true;
    
    // Help text for tooltip
    std::string tooltip;
    
    MaterialPin() = default;
    MaterialPin(const std::string& n, MaterialValueType t, bool output)
        : name(n), type(t), isOutput(output) {
        // Set reasonable defaults
        switch (type) {
            case MaterialValueType::Float: defaultValue = 0.0f; break;
            case MaterialValueType::Float2: defaultValue = glm::vec2(0.0f); break;
            case MaterialValueType::Float3: defaultValue = glm::vec3(0.0f); break;
            case MaterialValueType::Float4: defaultValue = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); break;
            case MaterialValueType::Int: defaultValue = 0; break;
            case MaterialValueType::Bool: defaultValue = false; break;
            case MaterialValueType::Texture2D:
            case MaterialValueType::Texture3D:
            case MaterialValueType::TextureCube:
                defaultValue = std::string();
                break;
            default:
                defaultValue = 0.0f;
        }
    }
};

/**
 * Base class for all material nodes
 */
class MaterialNode {
public:
    virtual ~MaterialNode() = default;
    
    // Node identification
    virtual std::string getName() const = 0;
    virtual std::string getCategory() const = 0;
    virtual std::string getDescription() const { return ""; }
    
    // Visual properties for editor
    virtual glm::vec4 getColor() const { return glm::vec4(0.4f, 0.4f, 0.4f, 1.0f); }
    virtual float getWidth() const { return 200.0f; }
    
    // Pin access
    const std::vector<MaterialPin>& getInputs() const { return inputs_; }
    const std::vector<MaterialPin>& getOutputs() const { return outputs_; }
    std::vector<MaterialPin>& getInputsMutable() { return inputs_; }
    
    MaterialPin* getInput(uint32_t index);
    MaterialPin* getOutput(uint32_t index);
    MaterialPin* findInput(const std::string& name);
    MaterialPin* findOutput(const std::string& name);
    
    // Code generation
    virtual std::string generateCode(MaterialCompiler& compiler) const = 0;
    
    // Get variable name for an output pin
    std::string getOutputVar(uint32_t pinIndex) const;
    
    // Validation
    virtual bool validate(std::string& error) const { return true; }
    
    // Preview generation (optional, for editor)
    virtual bool supportsPreview() const { return false; }
    virtual std::string generatePreviewCode() const { return ""; }
    
    // Serialization
    virtual void serialize(class MaterialSerializer& s) const;
    virtual void deserialize(class MaterialSerializer& s);
    
    // Custom properties (subclasses can override)
    virtual void drawProperties() {}  // For ImGui property panel
    
    // Node position in editor
    glm::vec2 position = glm::vec2(0);
    
    // Unique ID (assigned by graph)
    uint32_t id = 0;
    
    // Custom data (for editor use)
    void* userData = nullptr;
    
protected:
    // Helper methods for subclasses
    uint32_t addInput(const std::string& name, MaterialValueType type);
    uint32_t addOutput(const std::string& name, MaterialValueType type);
    
    // Convenience aliases for addInput/addOutput (optional parameter is currently ignored)
    uint32_t addInputPin(const std::string& name, MaterialValueType type, bool optional = false) {
        (void)optional; // Currently unused, for future extensibility
        return addInput(name, type);
    }
    uint32_t addOutputPin(const std::string& name, MaterialValueType type) {
        return addOutput(name, type);
    }
    
    void setInputDefault(uint32_t index, float value);
    void setInputDefault(uint32_t index, const glm::vec2& value);
    void setInputDefault(uint32_t index, const glm::vec3& value);
    void setInputDefault(uint32_t index, const glm::vec4& value);
    void setInputDefault(uint32_t index, int32_t value);
    void setInputDefault(uint32_t index, bool value);
    void setInputDefault(uint32_t index, const std::string& value);
    
    void setInputTooltip(uint32_t index, const std::string& tooltip);
    void setInputHidden(uint32_t index, bool hidden);
    
    std::vector<MaterialPin> inputs_;
    std::vector<MaterialPin> outputs_;
    
private:
    uint32_t nextPinId_ = 1;
};

/**
 * Node factory for creating nodes by type name
 */
class MaterialNodeFactory {
public:
    using CreateFunc = std::function<std::unique_ptr<MaterialNode>()>;
    
    static MaterialNodeFactory& getInstance();
    
    /**
     * Register a node type
     */
    void registerNode(const std::string& typeName, 
                     const std::string& category,
                     CreateFunc creator);
    
    /**
     * Create a node by type name
     */
    std::unique_ptr<MaterialNode> create(const std::string& typeName);
    
    /**
     * Get all registered node type names
     */
    std::vector<std::string> getNodeTypes() const;
    
    /**
     * Get node types in a specific category
     */
    std::vector<std::string> getNodeTypesInCategory(const std::string& category) const;
    
    /**
     * Get all categories
     */
    std::vector<std::string> getCategories() const;
    
    /**
     * Check if a type is registered
     */
    bool hasType(const std::string& typeName) const;
    
private:
    MaterialNodeFactory() = default;
    
    struct NodeTypeInfo {
        std::string category;
        CreateFunc creator;
    };
    std::unordered_map<std::string, NodeTypeInfo> creators_;
};

/**
 * Macro for easy node registration (with explicit category)
 */
#define REGISTER_MATERIAL_NODE(TypeName, Category) \
    namespace { \
        static bool _reg_##TypeName = []() { \
            MaterialNodeFactory::getInstance().registerNode( \
                #TypeName, Category, \
                []() { return std::make_unique<TypeName>(); } \
            ); \
            return true; \
        }(); \
    }

/**
 * Macro for easy node registration (uses node's getCategory() method)
 * Usage: REGISTER_MATERIAL_NODE_AUTO(MyNode) - category derived from getCategory()
 */
#define REGISTER_MATERIAL_NODE_AUTO(TypeName) \
    namespace { \
        static bool _reg_##TypeName = []() { \
            auto temp = std::make_unique<TypeName>(); \
            std::string category = temp->getCategory(); \
            MaterialNodeFactory::getInstance().registerNode( \
                #TypeName, category, \
                []() { return std::make_unique<TypeName>(); } \
            ); \
            return true; \
        }(); \
    }

/**
 * Serializer for material nodes and graphs
 */
class MaterialSerializer {
public:
    MaterialSerializer() = default;
    
    // Serialization mode
    bool isWriting() const { return writing_; }
    bool isReading() const { return !writing_; }
    
    // Value serialization
    void serialize(const std::string& name, float& value);
    void serialize(const std::string& name, glm::vec2& value);
    void serialize(const std::string& name, glm::vec3& value);
    void serialize(const std::string& name, glm::vec4& value);
    void serialize(const std::string& name, int32_t& value);
    void serialize(const std::string& name, uint32_t& value);
    void serialize(const std::string& name, bool& value);
    void serialize(const std::string& name, std::string& value);
    
    // Begin/end object
    void beginObject(const std::string& name);
    void endObject();
    
    // Begin/end array
    void beginArray(const std::string& name);
    size_t getArraySize() const;
    void endArray();
    
    // File I/O
    bool saveToFile(const std::string& path);
    bool loadFromFile(const std::string& path);
    
    // String I/O
    std::string toString() const;
    bool fromString(const std::string& data);
    
    // Static node serialization (JSON-based)
    static nlohmann::json serializeNode(const MaterialNode* node);
    static std::unique_ptr<MaterialNode> deserializeNode(const nlohmann::json& json);
    
private:
    bool writing_ = true;
    std::string buffer_;
    size_t readPos_ = 0;
    
    // Simple JSON-like format
    std::unordered_map<std::string, std::string> currentObject_;
    std::vector<std::string> currentArray_;
    std::vector<void*> stack_;
};

} // namespace Sanic
