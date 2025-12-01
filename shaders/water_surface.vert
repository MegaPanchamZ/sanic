/**
 * water_surface.vert
 * 
 * Water surface vertex shader with Gerstner wave animation.
 * 
 * Features:
 * - Multiple Gerstner wave layers
 * - Normal calculation from wave derivatives
 * - Foam calculation from wave steepness
 * - Flow map support for rivers
 */

#version 450
#extension GL_GOOGLE_include_directive : enable

// Per-vertex input (water mesh grid)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

// Output
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outTangent;
layout(location = 3) out vec3 outBitangent;
layout(location = 4) out vec2 outTexCoord;
layout(location = 5) out vec4 outClipPos;
layout(location = 6) out vec4 outPrevClipPos;
layout(location = 7) out float outFoam;

// Uniforms
layout(set = 0, binding = 0) uniform WaterUniforms {
    mat4 viewProjection;
    mat4 prevViewProjection;
    mat4 model;
    vec3 cameraPos;
    float time;
    float waterLevel;
    float waveAmplitude;
    float waveFrequency;
    float waveSteepness;
} ubo;

// Wave parameters (up to 8 waves)
struct GerstnerWave {
    vec2 direction;
    float amplitude;
    float frequency;
    float phase;
    float steepness;
    vec2 padding;
};

layout(set = 0, binding = 1) readonly buffer Waves {
    GerstnerWave waves[8];
    uint waveCount;
};

// Flow map for rivers
layout(set = 0, binding = 2) uniform sampler2D flowMap;

const float PI = 3.14159265359;

// Single Gerstner wave
vec3 gerstnerWave(vec2 xz, GerstnerWave wave, out vec3 tangent, out vec3 bitangent) {
    float k = 2.0 * PI * wave.frequency;
    float c = sqrt(9.8 / k);  // Phase velocity (deep water approximation)
    float a = wave.amplitude;
    float q = wave.steepness;
    
    vec2 d = normalize(wave.direction);
    float dotDP = dot(d, xz);
    float theta = k * dotDP - c * wave.phase * ubo.time;
    
    float sinTheta = sin(theta);
    float cosTheta = cos(theta);
    
    // Displacement
    vec3 offset;
    offset.x = q * a * d.x * cosTheta;
    offset.z = q * a * d.y * cosTheta;
    offset.y = a * sinTheta;
    
    // Tangent (wave direction derivative)
    tangent.x = 1.0 - q * k * d.x * d.x * sinTheta;
    tangent.y = q * k * d.x * a * cosTheta;
    tangent.z = -q * k * d.x * d.y * sinTheta;
    
    // Bitangent (perpendicular direction derivative)
    bitangent.x = -q * k * d.x * d.y * sinTheta;
    bitangent.y = q * k * d.y * a * cosTheta;
    bitangent.z = 1.0 - q * k * d.y * d.y * sinTheta;
    
    return offset;
}

void main() {
    vec3 localPos = inPosition;
    vec2 xz = localPos.xz;
    
    // Optional: Apply flow map offset for rivers
    vec2 flowMapUV = inTexCoord;
    vec2 flowDir = texture(flowMap, flowMapUV).rg * 2.0 - 1.0;
    float flowSpeed = length(flowDir);
    
    // Apply time-based flow offset
    if (flowSpeed > 0.01) {
        xz += flowDir * ubo.time * 0.5;
    }
    
    // Accumulate Gerstner waves
    vec3 totalOffset = vec3(0.0);
    vec3 totalTangent = vec3(1.0, 0.0, 0.0);
    vec3 totalBitangent = vec3(0.0, 0.0, 1.0);
    float totalSteepness = 0.0;
    
    for (uint i = 0; i < waveCount; i++) {
        vec3 waveTangent, waveBitangent;
        vec3 offset = gerstnerWave(xz, waves[i], waveTangent, waveBitangent);
        
        totalOffset += offset;
        totalTangent += waveTangent - vec3(1.0, 0.0, 0.0);
        totalBitangent += waveBitangent - vec3(0.0, 0.0, 1.0);
        totalSteepness += waves[i].steepness * waves[i].amplitude;
    }
    
    // Apply displacement
    vec3 displacedPos = localPos + totalOffset;
    displacedPos.y += ubo.waterLevel;
    
    // Transform to world space
    vec4 worldPos4 = ubo.model * vec4(displacedPos, 1.0);
    vec3 worldPos = worldPos4.xyz;
    
    // Normalize tangent/bitangent and compute normal
    vec3 tangent = normalize(totalTangent);
    vec3 bitangent = normalize(totalBitangent);
    vec3 normal = normalize(cross(bitangent, tangent));
    
    // Transform directions to world space
    mat3 normalMatrix = mat3(transpose(inverse(ubo.model)));
    vec3 worldNormal = normalize(normalMatrix * normal);
    vec3 worldTangent = normalize(normalMatrix * tangent);
    vec3 worldBitangent = normalize(normalMatrix * bitangent);
    
    // Calculate foam from wave steepness
    float foam = 0.0;
    
    // Foam at wave crests (high steepness areas)
    foam = smoothstep(0.3, 0.8, totalSteepness * 2.0);
    
    // Additional foam from vertical displacement
    float heightFoam = smoothstep(ubo.waveAmplitude * 0.3, ubo.waveAmplitude, totalOffset.y);
    foam = max(foam, heightFoam * 0.5);
    
    // Outputs
    outWorldPos = worldPos;
    outNormal = worldNormal;
    outTangent = worldTangent;
    outBitangent = worldBitangent;
    outTexCoord = inTexCoord;
    outFoam = foam;
    
    // Current and previous frame clip positions for motion vectors
    outClipPos = ubo.viewProjection * worldPos4;
    
    // Previous frame position (approximate - assumes no mesh animation change)
    vec4 prevWorldPos = ubo.model * vec4(localPos + totalOffset, 1.0);
    prevWorldPos.y += ubo.waterLevel;
    outPrevClipPos = ubo.prevViewProjection * prevWorldPos;
    
    gl_Position = outClipPos;
}
