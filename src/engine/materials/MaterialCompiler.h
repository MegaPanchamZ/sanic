/**
 * MaterialCompiler.h
 * 
 * Converts material node graphs into GLSL shader code.
 * Generates both vertex and fragment shaders for PBR rendering.
 */

#pragma once

#include "MaterialGraph.h"
#include "MaterialNode.h"
#include "../shaders/ShaderCompilerNew.h"
#include <sstream>
#include <unordered_map>
#include <set>

namespace Sanic {

/**
 * Compiled material output
 */
struct CompiledMaterial {
    // Shader source code
    std::string vertexShaderSource;
    std::string fragmentShaderSource;
    
    // Compiled SPIR-V
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    
    // Resource requirements
    std::vector<std::string> textureNames;
    std::vector<uint32_t> textureSlots;
    std::set<std::string> requiredUniforms;
    
    // Vertex shader needs world position offset
    bool hasVertexOffset = false;
    std::string vertexOffsetExpression;
    
    // Compilation success
    bool success = false;
    std::string errorMessage;
    
    // Material properties
    MaterialBlendMode blendMode = MaterialBlendMode::Opaque;
    bool twoSided = false;
};

/**
 * Material compiler - converts node graphs to GLSL shaders
 */
class MaterialCompiler {
public:
    MaterialCompiler();
    ~MaterialCompiler() = default;
    
    /**
     * Compile a material graph
     * @param graph The material graph to compile
     * @return Compiled material with shader sources and SPIR-V
     */
    CompiledMaterial compile(const MaterialGraph& graph);
    
    // --------------------------------------------------------------------------
    // Code Generation Helpers (called by nodes)
    // --------------------------------------------------------------------------
    
    /**
     * Generate a unique variable name
     */
    std::string generateUniqueVar(const std::string& prefix);
    
    /**
     * Add a line of code to the current shader
     */
    void addLine(const std::string& code);
    
    /**
     * Register an output value for a node pin
     */
    void registerOutput(uint64_t nodeId, const std::string& pinName, const std::string& expression);
    
    /**
     * Get the value of an input pin (resolves connections)
     */
    std::string getInputValue(const MaterialNode* node, const std::string& pinName, 
                               const std::string& defaultValue);
    
    /**
     * Register a texture for binding
     * @return Sampler variable name
     */
    std::string registerTexture(const std::string& path, uint32_t slot, bool srgb);
    
    /**
     * Request a uniform be available
     */
    void requireUniform(const std::string& name, const std::string& type);
    
    /**
     * Set vertex offset expression (from output node)
     */
    void setVertexOffset(const std::string& expression);
    
    /**
     * Infer GLSL type from variable/expression
     */
    std::string getValueType(const std::string& expression) const;
    
    /**
     * Infer result type when combining two expressions
     */
    std::string inferResultType(const std::string& a, const std::string& b) const;
    
    // --------------------------------------------------------------------------
    // Settings
    // --------------------------------------------------------------------------
    
    void setOptimizationLevel(int level) { m_OptimizationLevel = level; }
    void setDebugInfo(bool enable) { m_DebugInfo = enable; }
    
private:
    // Shader generation phases
    std::string generateVertexShader(const MaterialGraph& graph);
    std::string generateFragmentShader(const MaterialGraph& graph);
    
    // Generate code for all nodes in order
    void generateNodeCode(const MaterialGraph& graph);
    
    // Template sections
    std::string getVertexInputs() const;
    std::string getFragmentInputs() const;
    std::string getUniformBlock() const;
    std::string getTextureBindings() const;
    std::string getUtilityFunctions() const;
    std::string getGBufferOutputs() const;
    
    // Code buffers
    std::stringstream m_CodeStream;
    int m_IndentLevel = 0;
    
    // Variable counter for unique names
    int m_VarCounter = 0;
    
    // Node output values: nodeId -> (pinName -> expression)
    std::unordered_map<uint64_t, std::unordered_map<std::string, std::string>> m_NodeOutputs;
    
    // Texture tracking
    struct TextureInfo {
        std::string path;
        uint32_t slot;
        bool srgb;
        std::string samplerName;
    };
    std::vector<TextureInfo> m_Textures;
    
    // Required uniforms: name -> type
    std::unordered_map<std::string, std::string> m_RequiredUniforms;
    
    // Vertex offset
    std::string m_VertexOffset;
    bool m_HasVertexOffset = false;
    
    // Current graph reference
    const MaterialGraph* m_CurrentGraph = nullptr;
    
    // Settings
    int m_OptimizationLevel = 1;
    bool m_DebugInfo = false;
    
    // Shader compiler for SPIR-V generation
    std::unique_ptr<ShaderCompiler> m_ShaderCompiler;
};

} // namespace Sanic
