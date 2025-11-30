/**
 * outline.vert
 * 
 * Vertex shader for selection outline rendering.
 * Slightly scales mesh outward along normals for outline effect.
 */

#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    float outlineWidth;
    float padding[3];
} pc;

void main() {
    // Offset position along normal for outline
    vec3 offsetPos = inPosition + inNormal * pc.outlineWidth * 0.01;
    gl_Position = pc.mvp * vec4(offsetPos, 1.0);
}
