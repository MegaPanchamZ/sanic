/**
 * vsm_render.vert
 * 
 * Vertex shader for rendering shadow pages.
 * 
 * Turn 37-39: Virtual Shadow Maps
 */

#version 460
#extension GL_EXT_buffer_reference : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out float outDepth;

layout(push_constant) uniform PushConstants {
    mat4 lightViewProj;
    vec4 pageOffset;    // xy = offset, zw = scale
    float depthBias;
    float normalBias;
    uint pageIndex;
    uint pad;
} pc;

void main() {
    // Apply normal bias to reduce shadow acne
    vec3 biasedPos = inPosition + inNormal * pc.normalBias;
    
    vec4 lightSpace = pc.lightViewProj * vec4(biasedPos, 1.0);
    
    // Apply depth bias
    lightSpace.z += pc.depthBias;
    
    gl_Position = lightSpace;
    outDepth = lightSpace.z / lightSpace.w;
}
