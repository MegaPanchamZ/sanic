/**
 * outline.frag
 * 
 * Fragment shader for selection outline rendering.
 */

#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    float outlineWidth;
    float padding[3];
} pc;

void main() {
    outColor = pc.color;
}
