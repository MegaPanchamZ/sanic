#version 460
#extension GL_EXT_ray_tracing : require

// Shadow payload - 1.0 means the light is visible (not shadowed)
layout(location = 1) rayPayloadInEXT float shadowPayload;

void main() {
    // Ray missed all geometry - the point is lit (not in shadow)
    shadowPayload = 1.0;
}
