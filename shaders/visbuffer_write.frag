#version 460
#extension GL_GOOGLE_include_directive : enable

/**
 * Visibility Buffer Fragment Shader
 * ==================================
 * Outputs visibility buffer data for deferred material shading.
 * 
 * The visibility buffer stores:
 * - Triangle ID (cluster + meshlet + triangle)
 * - Instance ID
 * - Barycentrics are reconstructed from screen position
 * 
 * This allows deferred shading with minimal bandwidth:
 * - No need to store full G-buffer
 * - Material shading happens per-pixel from visibility data
 */

layout(location = 0) in VertexInput {
    vec3 worldPosition;
    vec3 worldNormal;
    vec2 texCoord;
    flat uint triangleId;
    flat uint instanceId;
    flat uint clusterId;
} fragIn;

// Output: Visibility buffer (64-bit)
// Format: instanceId (16 bits) | clusterId (16 bits) | triangleId (32 bits)
layout(location = 0) out uvec2 outVisibility;

// Optional: Output depth derivatives for mip selection
layout(location = 1) out vec2 outDepthDerivatives;

void main() {
    // Pack visibility data
    // High 32 bits: instanceId (16) | clusterId (16)
    // Low 32 bits: triangleId (32)
    uint high = (fragIn.instanceId << 16) | (fragIn.clusterId & 0xFFFF);
    uint low = fragIn.triangleId;
    
    outVisibility = uvec2(low, high);
    
    // Compute screen-space depth derivatives for texture mip selection
    // during material resolve pass
    float depth = gl_FragCoord.z;
    outDepthDerivatives = vec2(dFdx(depth), dFdy(depth));
}
