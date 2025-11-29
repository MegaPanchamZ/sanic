/**
 * vsm_render.frag
 * 
 * Fragment shader for rendering shadow depth.
 * 
 * Turn 37-39: Virtual Shadow Maps
 */

#version 460

layout(location = 0) in float inDepth;

// No output - just depth write

void main() {
    // Depth is written automatically from gl_FragDepth or gl_Position.z
    // We could output to a color attachment for VSM (variance shadow maps)
    // but for standard shadow maps, just let the depth buffer handle it
}
