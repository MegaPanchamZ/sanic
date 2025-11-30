#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require

/**
 * Visibility Buffer Fragment Shader - Masked Materials
 * ======================================================
 * 
 * Alpha-tested visibility buffer write for masked materials like
 * foliage, fences, decals, hair, etc.
 * 
 * Unlike the opaque visbuffer.frag, this shader:
 * 1. Samples the albedo texture alpha channel
 * 2. Compares against per-material opacity mask threshold
 * 3. Discards fragments below threshold
 * 4. Writes visibility data only for surviving fragments
 * 
 * This implements Nanite-style masked material support.
 */

// Inputs from mesh shader
layout(location = 0) flat in uint inMeshletID;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) flat in uint inInstanceID;
layout(location = 3) flat in uint inMaterialID;

// Visibility buffer output
layout(location = 0) out uvec2 outVisBuffer;

// Material structure (must match CPU)
struct Material {
    uint albedoTexture;
    uint normalTexture;
    uint roughnessMetallicTexture;
    uint emissiveTexture;
    
    vec4 baseColor;
    
    float roughness;
    float metallic;
    float emissiveStrength;
    uint blendModeAndFlags;
    
    float opacityMaskClipValue;
    float subsurfaceOpacity;
    float clearCoatRoughness;
    float anisotropy;
};

// Material buffer reference
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer MaterialBuffer {
    Material materials[];
};

// Push constants
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
    uint64_t meshletBuffer;        // Buffer addresses
    uint64_t meshletVertices;
    uint64_t meshletTriangles;
    uint64_t vertexBuffer;
    uint meshletCount;
    uint instanceID;
    uint materialID;
    uint pad0;
    MaterialBuffer materialBuffer; // Material buffer for alpha threshold
} push;

// Bindless textures
layout(set = 1, binding = 0) uniform sampler2D textures[];

// Invalid texture marker
const uint INVALID_TEXTURE = 0xFFFFFFFF;

void main() {
    // Load material data
    Material mat = push.materialBuffer.materials[inMaterialID];
    
    // Get blend mode (low 4 bits of blendModeAndFlags)
    uint blendMode = mat.blendModeAndFlags & 0xF;
    
    // Only process if this is a masked material (blend mode 1)
    // Note: Opaque materials (0) should use the optimized visbuffer.frag
    if (blendMode == 1) { // EBlendMode::Masked
        float alpha = mat.baseColor.a;
        
        // Sample albedo texture alpha if available
        if (mat.albedoTexture != INVALID_TEXTURE) {
            vec4 texColor = texture(textures[nonuniformEXT(mat.albedoTexture)], inTexCoord);
            alpha *= texColor.a;
        }
        
        // Alpha test - discard if below threshold
        // Using Nanite-style dithered opacity for smooth transitions
        if (alpha < mat.opacityMaskClipValue) {
            discard;
        }
    }
    
    // 64-bit Visibility Buffer Packing
    // Depth (15 bits) | InstanceID (20 bits) | ClusterID (22 bits) | TriangleID (7 bits)
    
    uint64_t depth = uint64_t(gl_FragCoord.z * 32767.0) & 0x7FFF;
    uint64_t instanceID = uint64_t(inInstanceID) & 0xFFFFF;
    uint64_t clusterID = uint64_t(inMeshletID) & 0x3FFFFF;
    uint64_t triangleID = uint64_t(gl_PrimitiveID) & 0x7F;
    
    // Pack into 64-bit integer
    uint64_t packedVal = (depth << 49) | (instanceID << 29) | (clusterID << 7) | triangleID;
    outVisBuffer = unpackUint2x32(packedVal);
}
