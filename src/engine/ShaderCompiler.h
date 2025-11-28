#pragma once

#include <string>
#include <vector>

// Enum to match shaderc_shader_kind roughly, or just use our own
enum class ShaderKind {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEvaluation,
    Task,
    Mesh
};

class ShaderCompiler {
public:
    ShaderCompiler();
    ~ShaderCompiler();

    /**
     * Compiles GLSL source code to SPIR-V binary using glslc.
     * @param source The GLSL source code string.
     * @param kind The shader kind.
     * @param sourceName The name of the source file (for error reporting).
     * @return A vector of uint32_t containing the SPIR-V binary.
     */
    std::vector<uint32_t> compileShader(const std::string& source, ShaderKind kind, const std::string& sourceName);

private:
    std::string getTempFileName(const std::string& suffix);
};
