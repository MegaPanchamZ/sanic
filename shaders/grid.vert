/**
 * grid.vert
 * 
 * Vertex shader for infinite ground grid.
 */

#version 450

layout(location = 0) out vec3 fragNearPoint;
layout(location = 1) out vec3 fragFarPoint;
layout(location = 2) out mat4 fragView;
layout(location = 6) out mat4 fragProj;

layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
} pc;

// Grid plane vertices (fullscreen quad)
vec3 gridPlane[6] = vec3[](
    vec3( 1,  1, 0),
    vec3(-1, -1, 0),
    vec3(-1,  1, 0),
    vec3(-1, -1, 0),
    vec3( 1,  1, 0),
    vec3( 1, -1, 0)
);

vec3 unprojectPoint(float x, float y, float z, mat4 view, mat4 proj) {
    mat4 viewInv = inverse(view);
    mat4 projInv = inverse(proj);
    vec4 unprojectedPoint = viewInv * projInv * vec4(x, y, z, 1.0);
    return unprojectedPoint.xyz / unprojectedPoint.w;
}

void main() {
    vec3 p = gridPlane[gl_VertexIndex];
    
    fragNearPoint = unprojectPoint(p.x, p.y, 0.0, pc.view, pc.proj);
    fragFarPoint = unprojectPoint(p.x, p.y, 1.0, pc.view, pc.proj);
    fragView = pc.view;
    fragProj = pc.proj;
    
    gl_Position = vec4(p, 1.0);
}
