#version 450

// ============================================================================
// G-BUFFER VERTEX SHADER - Deferred Rendering Geometry Pass
// ============================================================================

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 lightPos;
    vec4 viewPos;
    vec4 lightColor;
    mat4 lightSpaceMatrix;
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;
    vec4 shadowParams;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;
layout(location = 4) out vec4 fragPosLightSpace;
layout(location = 5) out float fragViewDepth;  // For CSM cascade selection

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    vec4 viewPos = ubo.view * worldPos;
    
    gl_Position = ubo.proj * viewPos;
    
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragNormal = mat3(push.normalMatrix) * inNormal;
    fragPos = vec3(worldPos);
    fragPosLightSpace = ubo.lightSpaceMatrix * worldPos;
    fragViewDepth = -viewPos.z;  // Positive depth in view space for CSM
}
