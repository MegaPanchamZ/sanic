#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main() {
    // Sky gradient based on ray direction
    hitValue = vec3(0.4, 0.6, 1.0) * gl_WorldRayDirectionEXT.y * 0.5 + 0.5;
}
