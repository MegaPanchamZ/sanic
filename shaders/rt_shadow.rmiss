/**
 * rt_shadow.rmiss
 * 
 * Miss shader for shadow rays - indicates no shadow.
 * 
 * Turn 37-39: Ray-traced Shadows
 */

#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT float shadowPayload;

void main() {
    // Ray missed all geometry - fully lit
    shadowPayload = 1.0;
}
