/**
 * MaterialCompiler.cpp
 * 
 * Implementation of material graph to GLSL compilation.
 */

#include "MaterialCompiler.h"
#include <algorithm>
#include <regex>

namespace Sanic {

MaterialCompiler::MaterialCompiler() {
    m_ShaderCompiler = std::make_unique<ShaderCompilerEnhanced>();
    m_ShaderCompiler->initialize();
}

CompiledMaterial MaterialCompiler::compile(const MaterialGraph& graph) {
    CompiledMaterial result;
    
    // Validate graph first
    if (!graph.isValid()) {
        auto diagnostics = graph.validate();
        result.success = false;
        result.errorMessage = "Material graph validation failed:\n";
        for (const auto& diag : diagnostics) {
            if (diag.severity == MaterialGraphDiagnostic::Severity::Error) {
                result.errorMessage += "- " + diag.message + "\n";
            }
        }
        return result;
    }
    
    // Reset state
    m_CodeStream.str("");
    m_CodeStream.clear();
    m_VarCounter = 0;
    m_NodeOutputs.clear();
    m_Textures.clear();
    m_RequiredUniforms.clear();
    m_VertexOffset.clear();
    m_HasVertexOffset = false;
    m_CurrentGraph = &graph;
    
    // Generate shader sources
    try {
        result.fragmentShaderSource = generateFragmentShader(graph);
        result.vertexShaderSource = generateVertexShader(graph);
        
        // Copy material properties
        result.blendMode = graph.blendMode;
        result.twoSided = graph.twoSided;
        result.hasVertexOffset = m_HasVertexOffset;
        result.vertexOffsetExpression = m_VertexOffset;
        
        // Copy texture info
        for (const auto& tex : m_Textures) {
            result.textureNames.push_back(tex.path);
            result.textureSlots.push_back(tex.slot);
        }
        
        // Copy required uniforms
        for (const auto& [name, type] : m_RequiredUniforms) {
            result.requiredUniforms.insert(name);
        }
        
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = "Shader generation failed: " + std::string(e.what());
        return result;
    }
    
    // Compile to SPIR-V
    ShaderCompileOptions vertOptions;
    vertOptions.stage = ShaderStage::Vertex;
    vertOptions.sourceName = graph.name + ".vert";
    vertOptions.generateDebugInfo = m_DebugInfo;
    vertOptions.optimization = (m_OptimizationLevel > 0) ? ShaderOptLevel::Performance : ShaderOptLevel::None;
    
    // Compile vertex shader
    auto vertResult = m_ShaderCompiler->compile(
        result.vertexShaderSource,
        vertOptions
    );
    
    if (!vertResult.success) {
        result.success = false;
        result.errorMessage = "Vertex shader compilation failed:\n" + vertResult.errors;
        return result;
    }
    result.vertexSpirv = vertResult.spirv;
    
    // Compile fragment shader
    ShaderCompileOptions fragOptions;
    fragOptions.stage = ShaderStage::Fragment;
    fragOptions.sourceName = graph.name + ".frag";
    fragOptions.generateDebugInfo = m_DebugInfo;
    fragOptions.optimization = (m_OptimizationLevel > 0) ? ShaderOptLevel::Performance : ShaderOptLevel::None;
    
    auto fragResult = m_ShaderCompiler->compile(
        result.fragmentShaderSource,
        fragOptions
    );
    
    if (!fragResult.success) {
        result.success = false;
        result.errorMessage = "Fragment shader compilation failed:\n" + fragResult.errors;
        return result;
    }
    result.fragmentSpirv = fragResult.spirv;
    
    result.success = true;
    m_CurrentGraph = nullptr;
    
    return result;
}

// ============================================================================
// Code Generation Helpers
// ============================================================================

std::string MaterialCompiler::generateUniqueVar(const std::string& prefix) {
    return prefix + "_" + std::to_string(m_VarCounter++);
}

void MaterialCompiler::addLine(const std::string& code) {
    for (int i = 0; i < m_IndentLevel; i++) {
        m_CodeStream << "    ";
    }
    m_CodeStream << code << "\n";
}

void MaterialCompiler::registerOutput(uint64_t nodeId, const std::string& pinName, 
                                        const std::string& expression) {
    m_NodeOutputs[nodeId][pinName] = expression;
}

std::string MaterialCompiler::getInputValue(const MaterialNode* node, const std::string& pinName,
                                             const std::string& defaultValue) {
    if (!m_CurrentGraph) return defaultValue;
    
    // Find the input pin index
    const auto& inputs = node->getInputs();
    uint32_t pinIndex = 0;
    bool found = false;
    
    for (uint32_t i = 0; i < inputs.size(); i++) {
        if (inputs[i].name == pinName) {
            pinIndex = i;
            found = true;
            break;
        }
    }
    
    if (!found) return defaultValue;
    
    // Check if there's a connection to this input
    auto connection = m_CurrentGraph->getInputConnection(node->id, pinIndex);
    if (!connection) {
        return defaultValue;
    }
    
    // Get the source node and pin
    MaterialNode* sourceNode = m_CurrentGraph->getNode(connection->sourceNodeId);
    if (!sourceNode) return defaultValue;
    
    const auto& outputs = sourceNode->getOutputs();
    if (connection->sourcePin >= outputs.size()) return defaultValue;
    
    const std::string& sourcePinName = outputs[connection->sourcePin].name;
    
    // Look up the registered output
    auto nodeIt = m_NodeOutputs.find(connection->sourceNodeId);
    if (nodeIt == m_NodeOutputs.end()) {
        // Source node hasn't been processed yet - shouldn't happen with topological sort
        return defaultValue;
    }
    
    auto pinIt = nodeIt->second.find(sourcePinName);
    if (pinIt == nodeIt->second.end()) {
        // Try empty pin name (default output)
        pinIt = nodeIt->second.find("");
        if (pinIt == nodeIt->second.end()) {
            return defaultValue;
        }
    }
    
    return pinIt->second;
}

std::string MaterialCompiler::registerTexture(const std::string& path, uint32_t slot, bool srgb) {
    // Check if already registered
    for (const auto& tex : m_Textures) {
        if (tex.path == path && tex.slot == slot) {
            return tex.samplerName;
        }
    }
    
    TextureInfo tex;
    tex.path = path;
    tex.slot = slot;
    tex.srgb = srgb;
    tex.samplerName = "u_Texture" + std::to_string(m_Textures.size());
    
    m_Textures.push_back(tex);
    return tex.samplerName;
}

void MaterialCompiler::requireUniform(const std::string& name, const std::string& type) {
    m_RequiredUniforms[name] = type;
}

void MaterialCompiler::setVertexOffset(const std::string& expression) {
    if (expression != "vec3(0.0)" && expression != "vec3(0.0, 0.0, 0.0)") {
        m_VertexOffset = expression;
        m_HasVertexOffset = true;
    }
}

std::string MaterialCompiler::getValueType(const std::string& expression) const {
    // Try to infer type from expression
    if (expression.find("vec4") != std::string::npos) return "vec4";
    if (expression.find("vec3") != std::string::npos) return "vec3";
    if (expression.find("vec2") != std::string::npos) return "vec2";
    
    // Check for swizzles
    std::regex swizzleRegex(R"(\.(rgba|rgb|rg|r|xyzw|xyz|xy|x)+$)");
    std::smatch match;
    if (std::regex_search(expression, match, swizzleRegex)) {
        std::string swizzle = match[1].str();
        if (swizzle.length() == 1) return "float";
        if (swizzle.length() == 2) return "vec2";
        if (swizzle.length() == 3) return "vec3";
        if (swizzle.length() == 4) return "vec4";
    }
    
    // Check for known variables
    if (expression.find("v_WorldPos") != std::string::npos) return "vec3";
    if (expression.find("v_WorldNormal") != std::string::npos) return "vec3";
    if (expression.find("v_TexCoord") != std::string::npos) return "vec2";
    if (expression.find("v_Color") != std::string::npos) return "vec4";
    if (expression.find("gl_FragCoord") != std::string::npos) return "vec4";
    
    // Default to float
    return "float";
}

std::string MaterialCompiler::inferResultType(const std::string& a, const std::string& b) const {
    std::string typeA = getValueType(a);
    std::string typeB = getValueType(b);
    
    // Pick the larger type
    auto typeRank = [](const std::string& t) -> int {
        if (t == "vec4") return 4;
        if (t == "vec3") return 3;
        if (t == "vec2") return 2;
        return 1;
    };
    
    return (typeRank(typeA) >= typeRank(typeB)) ? typeA : typeB;
}

// ============================================================================
// Shader Generation
// ============================================================================

std::string MaterialCompiler::generateVertexShader(const MaterialGraph& graph) {
    std::stringstream ss;
    
    ss << "#version 460 core\n\n";
    
    ss << "// Vertex inputs\n";
    ss << getVertexInputs();
    ss << "\n";
    
    ss << "// Vertex outputs (to fragment shader)\n";
    ss << "layout(location = 0) out vec3 v_WorldPos;\n";
    ss << "layout(location = 1) out vec3 v_WorldNormal;\n";
    ss << "layout(location = 2) out vec2 v_TexCoord;\n";
    ss << "layout(location = 3) out vec4 v_Color;\n";
    ss << "layout(location = 4) out vec3 v_Tangent;\n";
    ss << "layout(location = 5) out vec3 v_Bitangent;\n";
    ss << "layout(location = 6) out vec3 v_ViewDir;\n";
    ss << "\n";
    
    ss << "// Uniforms\n";
    ss << "layout(set = 0, binding = 0) uniform CameraData {\n";
    ss << "    mat4 viewProjection;\n";
    ss << "    mat4 view;\n";
    ss << "    mat4 projection;\n";
    ss << "    vec3 cameraPos;\n";
    ss << "    float time;\n";
    ss << "} u_Camera;\n";
    ss << "\n";
    
    ss << "layout(push_constant) uniform PushConstants {\n";
    ss << "    mat4 model;\n";
    ss << "    mat4 normalMatrix;\n";
    ss << "} u_Push;\n";
    ss << "\n";
    
    ss << "void main() {\n";
    ss << "    vec4 worldPos = u_Push.model * vec4(a_Position, 1.0);\n";
    
    // Apply vertex offset if any
    if (m_HasVertexOffset) {
        ss << "    worldPos.xyz += " << m_VertexOffset << ";\n";
    }
    
    ss << "    v_WorldPos = worldPos.xyz;\n";
    ss << "    v_WorldNormal = normalize(mat3(u_Push.normalMatrix) * a_Normal);\n";
    ss << "    v_TexCoord = a_TexCoord;\n";
    ss << "    v_Color = a_Color;\n";
    ss << "    v_Tangent = normalize(mat3(u_Push.normalMatrix) * a_Tangent);\n";
    ss << "    v_Bitangent = cross(v_WorldNormal, v_Tangent);\n";
    ss << "    v_ViewDir = normalize(u_Camera.cameraPos - worldPos.xyz);\n";
    ss << "    \n";
    ss << "    gl_Position = u_Camera.viewProjection * worldPos;\n";
    ss << "}\n";
    
    return ss.str();
}

std::string MaterialCompiler::generateFragmentShader(const MaterialGraph& graph) {
    std::stringstream ss;
    
    ss << "#version 460 core\n\n";
    
    // Fragment inputs
    ss << "// Fragment inputs (from vertex shader)\n";
    ss << getFragmentInputs();
    ss << "\n";
    
    // GBuffer outputs
    ss << "// GBuffer outputs\n";
    ss << getGBufferOutputs();
    ss << "\n";
    
    // Uniforms
    ss << "// Uniforms\n";
    ss << getUniformBlock();
    ss << "\n";
    
    // Texture bindings
    if (!m_Textures.empty()) {
        ss << "// Textures\n";
        ss << getTextureBindings();
        ss << "\n";
    }
    
    // Utility functions
    ss << "// Utility functions\n";
    ss << getUtilityFunctions();
    ss << "\n";
    
    // Main function
    ss << "void main() {\n";
    m_IndentLevel = 1;
    
    // Generate code for all nodes in topological order
    m_CodeStream.str("");
    m_CodeStream.clear();
    generateNodeCode(graph);
    
    ss << m_CodeStream.str();
    
    m_IndentLevel = 0;
    ss << "}\n";
    
    return ss.str();
}

void MaterialCompiler::generateNodeCode(const MaterialGraph& graph) {
    // Get nodes in topological order
    std::vector<MaterialNode*> sortedNodes = graph.topologicalSort();
    
    // Generate code for each node
    for (MaterialNode* node : sortedNodes) {
        // Skip output node for now - it will be processed last
        if (node == graph.getOutputNode()) {
            continue;
        }
        
        // Generate node code
        std::string result = node->generateCode(*this);
        
        // Register the main output
        if (!result.empty()) {
            const auto& outputs = node->getOutputs();
            if (!outputs.empty()) {
                // Register first output pin as default
                m_NodeOutputs[node->id][outputs[0].name] = result;
                // Also register with empty name for compatibility
                m_NodeOutputs[node->id][""] = result;
            }
        }
    }
    
    // Generate output node code last
    MaterialNode* outputNode = graph.getOutputNode();
    if (outputNode) {
        outputNode->generateCode(*this);
    }
}

// ============================================================================
// Template Sections
// ============================================================================

std::string MaterialCompiler::getVertexInputs() const {
    std::stringstream ss;
    ss << "layout(location = 0) in vec3 a_Position;\n";
    ss << "layout(location = 1) in vec3 a_Normal;\n";
    ss << "layout(location = 2) in vec2 a_TexCoord;\n";
    ss << "layout(location = 3) in vec4 a_Color;\n";
    ss << "layout(location = 4) in vec3 a_Tangent;\n";
    return ss.str();
}

std::string MaterialCompiler::getFragmentInputs() const {
    std::stringstream ss;
    ss << "layout(location = 0) in vec3 v_WorldPos;\n";
    ss << "layout(location = 1) in vec3 v_WorldNormal;\n";
    ss << "layout(location = 2) in vec2 v_TexCoord;\n";
    ss << "layout(location = 3) in vec4 v_Color;\n";
    ss << "layout(location = 4) in vec3 v_Tangent;\n";
    ss << "layout(location = 5) in vec3 v_Bitangent;\n";
    ss << "layout(location = 6) in vec3 v_ViewDir;\n";
    return ss.str();
}

std::string MaterialCompiler::getUniformBlock() const {
    std::stringstream ss;
    
    ss << "layout(set = 0, binding = 0) uniform CameraData {\n";
    ss << "    mat4 viewProjection;\n";
    ss << "    mat4 view;\n";
    ss << "    mat4 projection;\n";
    ss << "    vec3 cameraPos;\n";
    ss << "    float time;\n";
    ss << "} u_Camera;\n";
    ss << "\n";
    
    ss << "// Aliases for common uniforms\n";
    ss << "#define u_Time u_Camera.time\n";
    ss << "#define u_CameraPos u_Camera.cameraPos\n";
    ss << "#define u_DeltaTime 0.016 // TODO: Pass actual delta time\n";
    
    return ss.str();
}

std::string MaterialCompiler::getTextureBindings() const {
    std::stringstream ss;
    
    for (size_t i = 0; i < m_Textures.size(); i++) {
        ss << "layout(set = 1, binding = " << i << ") uniform sampler2D " 
           << m_Textures[i].samplerName << ";\n";
    }
    
    return ss.str();
}

std::string MaterialCompiler::getUtilityFunctions() const {
    std::stringstream ss;
    
    // Normal encoding for GBuffer
    ss << "vec2 encodeNormal(vec3 n) {\n";
    ss << "    // Octahedron normal encoding\n";
    ss << "    n /= (abs(n.x) + abs(n.y) + abs(n.z));\n";
    ss << "    if (n.z < 0.0) {\n";
    ss << "        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);\n";
    ss << "    }\n";
    ss << "    return n.xy * 0.5 + 0.5;\n";
    ss << "}\n";
    ss << "\n";
    
    // Tangent space normal mapping
    ss << "vec3 applyNormalMap(vec3 normalMapSample, mat3 TBN) {\n";
    ss << "    vec3 tangentNormal = normalMapSample * 2.0 - 1.0;\n";
    ss << "    return normalize(TBN * tangentNormal);\n";
    ss << "}\n";
    ss << "\n";
    
    // sRGB conversion
    ss << "vec3 linearToSRGB(vec3 color) {\n";
    ss << "    return pow(color, vec3(1.0 / 2.2));\n";
    ss << "}\n";
    ss << "\n";
    ss << "vec3 sRGBToLinear(vec3 color) {\n";
    ss << "    return pow(color, vec3(2.2));\n";
    ss << "}\n";
    
    return ss.str();
}

std::string MaterialCompiler::getGBufferOutputs() const {
    std::stringstream ss;
    
    ss << "layout(location = 0) out vec4 out_GBuffer0; // RGB: BaseColor, A: Metallic\n";
    ss << "layout(location = 1) out vec4 out_GBuffer1; // RG: Normal (encoded), B: Roughness, A: AO\n";
    ss << "layout(location = 2) out vec4 out_GBuffer2; // RGB: Emissive, A: MaterialID\n";
    ss << "layout(location = 3) out float out_Alpha;   // For transparency\n";
    
    return ss.str();
}

} // namespace Sanic

