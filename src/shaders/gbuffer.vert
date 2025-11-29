#version 450

// ============================================================================
// G-BUFFER VERTEX SHADER - Deferred Rendering Geometry Pass
// Outputs current and previous frame positions for motion vector generation
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
    // Motion vector uniforms
    mat4 prevView;          // Previous frame's view matrix
    mat4 prevProj;          // Previous frame's projection matrix
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
    mat4 prevModel;         // Previous frame's model matrix (for animated objects)
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
layout(location = 5) out float fragViewDepth;   // For CSM cascade selection
layout(location = 6) out vec4 currentClipPos;   // Current frame clip position
layout(location = 7) out vec4 prevClipPos;      // Previous frame clip position

void main() {
    // Current frame transform
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    vec4 viewPos = ubo.view * worldPos;
    vec4 clipPos = ubo.proj * viewPos;
    
    gl_Position = clipPos;
    
    // Previous frame transform (for motion vectors)
    vec4 prevWorldPos = push.prevModel * vec4(inPosition, 1.0);
    vec4 prevViewPos = ubo.prevView * prevWorldPos;
    vec4 prevClip = ubo.prevProj * prevViewPos;
    
    // Pass clip positions for motion vector calculation in fragment shader
    currentClipPos = clipPos;
    prevClipPos = prevClip;
    
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragNormal = mat3(push.normalMatrix) * inNormal;
    fragPos = vec3(worldPos);
    fragPosLightSpace = ubo.lightSpaceMatrix * worldPos;
    fragViewDepth = -viewPos.z;  // Positive depth in view space for CSM
}
