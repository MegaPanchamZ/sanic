#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main() {
    // Sky gradient - blue at horizon, lighter blue at zenith
    vec3 rayDir = normalize(gl_WorldRayDirectionEXT);
    float t = 0.5 * (rayDir.y + 1.0);  // Map [-1,1] to [0,1]
    vec3 horizonColor = vec3(0.8, 0.85, 0.95);
    vec3 zenithColor = vec3(0.3, 0.5, 0.9);
    hitValue = mix(horizonColor, zenithColor, t);
}
