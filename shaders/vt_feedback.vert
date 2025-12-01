/**
 * vt_feedback.vert
 * 
 * Vertex shader for virtual texture feedback pass.
 * Renders scene geometry to feedback buffer for page requests.
 */

#version 450
#extension GL_GOOGLE_include_directive : enable

// Input
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

// Output
layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out float outLodBias;

// Uniforms
layout(set = 0, binding = 0) uniform VTUniforms {
    mat4 viewProjection;
    mat4 model;
    vec3 cameraPos;
    float lodBias;
} ubo;

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    
    outTexCoord = inTexCoord;
    outWorldPos = worldPos.xyz;
    
    // Distance-based LOD bias
    float dist = length(ubo.cameraPos - worldPos.xyz);
    outLodBias = ubo.lodBias + log2(max(1.0, dist / 100.0)) * 0.5;
    
    gl_Position = ubo.viewProjection * worldPos;
}
