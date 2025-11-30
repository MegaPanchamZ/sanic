#version 460

// Motion Vector Generation Shader
// Calculates per-pixel velocity for temporal effects (TAA, temporal SSR)

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inPrevWorldPos;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec2 outVelocity;

layout(set = 0, binding = 0) uniform MotionUniforms {
    mat4 currentViewProj;
    mat4 prevViewProj;
    mat4 currentView;
    mat4 prevView;
    vec2 screenSize;
    vec2 jitter;         // Sub-pixel jitter for TAA
    vec2 prevJitter;
} ubo;

void main() {
    // Project current position to screen space
    vec4 currentClip = ubo.currentViewProj * vec4(inWorldPos, 1.0);
    vec2 currentScreen = (currentClip.xy / currentClip.w) * 0.5 + 0.5;
    
    // Project previous position to screen space
    vec4 prevClip = ubo.prevViewProj * vec4(inPrevWorldPos, 1.0);
    vec2 prevScreen = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
    
    // Apply jitter correction
    currentScreen -= ubo.jitter / ubo.screenSize;
    prevScreen -= ubo.prevJitter / ubo.screenSize;
    
    // Velocity = current - previous (for reprojection, use negative)
    outVelocity = currentScreen - prevScreen;
}
