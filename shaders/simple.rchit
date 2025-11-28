#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec2 attribs;

void main() {
    // Simple normal visualization
    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    
    // Visualize geometric normal
    vec3 normal = normalize(cross(
        gl_WorldRayDirectionEXT,
        vec3(barycentrics.x, barycentrics.y, barycentrics.z)
    ));
    
    // Abs normal as color
    hitValue = abs(normal);
}
