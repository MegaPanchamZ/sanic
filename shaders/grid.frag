/**
 * grid.frag
 * 
 * Fragment shader for infinite ground grid.
 */

#version 450

layout(location = 0) in vec3 nearPoint;
layout(location = 1) in vec3 farPoint;
layout(location = 2) in mat4 fragView;
layout(location = 6) in mat4 fragProj;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
    vec4 gridParams;  // x = gridSize, y = majorLineSpacing, z = fadeStart, w = fadeEnd
} pc;

vec4 grid(vec3 fragPos3D, float scale, bool drawAxis) {
    vec2 coord = fragPos3D.xz * scale;
    vec2 derivative = fwidth(coord);
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line = min(grid.x, grid.y);
    float minimumz = min(derivative.y, 1.0);
    float minimumx = min(derivative.x, 1.0);
    vec4 color = vec4(0.3, 0.3, 0.3, 1.0 - min(line, 1.0));
    
    // Z axis (blue)
    if(fragPos3D.x > -0.1 * minimumx && fragPos3D.x < 0.1 * minimumx && drawAxis) {
        color.rgb = vec3(0.2, 0.4, 1.0);
    }
    // X axis (red)
    if(fragPos3D.z > -0.1 * minimumz && fragPos3D.z < 0.1 * minimumz && drawAxis) {
        color.rgb = vec3(1.0, 0.3, 0.3);
    }
    
    return color;
}

float computeDepth(vec3 pos) {
    vec4 clipSpacePos = fragProj * fragView * vec4(pos.xyz, 1.0);
    return (clipSpacePos.z / clipSpacePos.w);
}

float computeLinearDepth(vec3 pos) {
    float near = 0.01;
    float far = 1000.0;
    vec4 clipSpacePos = fragProj * fragView * vec4(pos.xyz, 1.0);
    float clipSpaceDepth = (clipSpacePos.z / clipSpacePos.w) * 2.0 - 1.0;
    float linearDepth = (2.0 * near * far) / (far + near - clipSpaceDepth * (far - near));
    return linearDepth / far;
}

void main() {
    float t = -nearPoint.y / (farPoint.y - nearPoint.y);
    vec3 fragPos3D = nearPoint + t * (farPoint - nearPoint);
    
    gl_FragDepth = computeDepth(fragPos3D);
    
    float linearDepth = computeLinearDepth(fragPos3D);
    float fading = max(0.0, (0.5 - linearDepth));
    
    // Major grid lines (every 10 units)
    vec4 majorGrid = grid(fragPos3D, 0.1, true);
    majorGrid.a *= 0.8;
    
    // Minor grid lines (every 1 unit)  
    vec4 minorGrid = grid(fragPos3D, 1.0, false);
    minorGrid.a *= 0.4;
    
    // Combine grids
    outColor = majorGrid;
    outColor.rgb = mix(outColor.rgb, minorGrid.rgb, minorGrid.a * (1.0 - majorGrid.a));
    outColor.a = max(majorGrid.a, minorGrid.a);
    
    // Apply distance fading
    outColor.a *= fading;
    
    // Discard if below ground plane
    if (t < 0.0) {
        discard;
    }
}
