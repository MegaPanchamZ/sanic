/**
 * foliage.vert
 * 
 * Foliage vertex shader with wind animation.
 * Supports Pivot Painter style vertex animation.
 * 
 * Features:
 * - Per-vertex wind displacement
 * - Branch and leaf wind layers
 * - Wind direction and strength from uniforms
 * - Interaction with player/objects
 * - LOD fade support
 */

#version 450
#extension GL_GOOGLE_include_directive : enable

// Per-vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;         // xyz: tangent, w: bitangent sign
layout(location = 4) in vec4 inColor;           // R: wind weight, G: phase offset, B: branch mask, A: unused

// Per-instance attributes (from visible instance buffer)
layout(location = 5) in vec4 instancePosScale;  // xyz: position, w: scale
layout(location = 6) in vec4 instanceRotLOD;    // xyz: rotation (euler), w: LOD + fade
layout(location = 7) in uint instanceTypeId;

// Outputs
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec3 outBitangent;
layout(location = 5) out float outFade;
layout(location = 6) out flat uint outTypeId;

// Uniforms
layout(set = 0, binding = 0) uniform FoliageUniforms {
    mat4 viewProjection;
    vec3 cameraPos;
    float time;
    vec3 windDirection;
    float windStrength;
    vec3 windGustDirection;
    float windGustStrength;
    float windFrequency;
    float windTurbulence;
    float interactionRadius;
    float interactionStrength;
    vec4 interactionPoints[8];  // xyz: position, w: strength
    uint numInteractionPoints;
    vec3 padding;
} ubo;

// Type-specific wind parameters
layout(set = 0, binding = 1) readonly buffer TypeParams {
    vec4 windParams[];  // x: strength mult, y: speed mult, z: frequency mult, w: phase offset
};

// Constants
const float PI = 3.14159265359;
const float TAU = 6.28318530718;

// Build rotation matrix from euler angles
mat3 rotationFromEuler(vec3 euler) {
    float cx = cos(euler.x);
    float sx = sin(euler.x);
    float cy = cos(euler.y);
    float sy = sin(euler.y);
    float cz = cos(euler.z);
    float sz = sin(euler.z);
    
    mat3 rx = mat3(
        1.0, 0.0, 0.0,
        0.0, cx, -sx,
        0.0, sx, cx
    );
    
    mat3 ry = mat3(
        cy, 0.0, sy,
        0.0, 1.0, 0.0,
        -sy, 0.0, cy
    );
    
    mat3 rz = mat3(
        cz, -sz, 0.0,
        sz, cz, 0.0,
        0.0, 0.0, 1.0
    );
    
    return rz * ry * rx;
}

// Simplex noise for wind variation
vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 1.0) * x); }

float snoise(vec3 v) {
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);
    
    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);
    
    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);
    
    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;
    
    i = mod289(i);
    vec4 p = permute(permute(permute(
        i.z + vec4(0.0, i1.z, i2.z, 1.0))
        + i.y + vec4(0.0, i1.y, i2.y, 1.0))
        + i.x + vec4(0.0, i1.x, i2.x, 1.0));
    
    float n_ = 0.142857142857;
    vec3 ns = n_ * D.wyz - D.xzx;
    
    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
    
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);
    
    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);
    
    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);
    
    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    
    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;
    
    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);
    
    vec4 norm = inversesqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;
    
    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

// Main wind calculation
vec3 calculateWind(vec3 worldPos, float windWeight, float phaseOffset, float branchMask) {
    if (windWeight < 0.01) {
        return vec3(0.0);
    }
    
    // Get type-specific parameters
    vec4 typeParams = windParams[instanceTypeId];
    float strengthMult = typeParams.x;
    float speedMult = typeParams.y;
    float freqMult = typeParams.z;
    float typePhase = typeParams.w;
    
    float time = ubo.time * speedMult;
    float totalPhase = phaseOffset + typePhase;
    
    // Primary wave (trunk/branch sway)
    float primaryFreq = ubo.windFrequency * freqMult;
    float primaryWave = sin(time * primaryFreq + totalPhase * TAU) * 0.5 + 0.5;
    primaryWave += sin(time * primaryFreq * 0.7 + totalPhase * TAU * 1.3) * 0.3;
    
    // Secondary wave (smaller oscillations)
    float secondaryWave = sin(time * primaryFreq * 2.3 + totalPhase * TAU * 0.8) * 0.25;
    secondaryWave += sin(time * primaryFreq * 3.1 + totalPhase * TAU * 1.1) * 0.15;
    
    // Gust noise
    vec3 noisePos = worldPos * 0.02 + vec3(time * 0.1, 0.0, time * 0.1);
    float gustNoise = snoise(noisePos) * 0.5 + 0.5;
    gustNoise = pow(gustNoise, 2.0);  // Make gusts more distinct
    
    // Turbulence noise
    vec3 turbNoisePos = worldPos * 0.1 + vec3(time * 0.3, time * 0.2, time * 0.3);
    float turbulence = snoise(turbNoisePos) * ubo.windTurbulence;
    
    // Combine wind forces
    vec3 windDir = normalize(ubo.windDirection);
    vec3 gustDir = normalize(ubo.windGustDirection);
    
    // Primary displacement (bends from base)
    float bendAmount = windWeight * windWeight;  // Quadratic falloff from base
    vec3 primaryDisp = windDir * primaryWave * bendAmount * ubo.windStrength * strengthMult;
    
    // Add gusts
    primaryDisp += gustDir * gustNoise * bendAmount * ubo.windGustStrength * strengthMult;
    
    // Secondary displacement (leaf flutter)
    float leafAmount = branchMask * windWeight;
    vec3 secondaryDisp = vec3(secondaryWave, secondaryWave * 0.5, secondaryWave * 0.8);
    secondaryDisp *= leafAmount * ubo.windStrength * 0.3 * strengthMult;
    
    // Add turbulence
    vec3 turbDisp = vec3(turbulence, turbulence * 0.5, turbulence * 0.7);
    turbDisp *= windWeight * strengthMult * 0.2;
    
    return primaryDisp + secondaryDisp + turbDisp;
}

// Calculate interaction displacement (player pushing through foliage)
vec3 calculateInteraction(vec3 worldPos) {
    vec3 totalDisp = vec3(0.0);
    
    for (uint i = 0; i < ubo.numInteractionPoints; i++) {
        vec3 interactPos = ubo.interactionPoints[i].xyz;
        float strength = ubo.interactionPoints[i].w;
        
        vec3 toVertex = worldPos - interactPos;
        float dist = length(toVertex);
        
        if (dist < ubo.interactionRadius && dist > 0.01) {
            float falloff = 1.0 - (dist / ubo.interactionRadius);
            falloff = falloff * falloff;  // Quadratic falloff
            
            vec3 pushDir = normalize(toVertex);
            totalDisp += pushDir * falloff * strength * ubo.interactionStrength;
        }
    }
    
    return totalDisp;
}

void main() {
    // Extract instance data
    vec3 instancePos = instancePosScale.xyz;
    float instanceScale = instancePosScale.w;
    vec3 instanceRot = instanceRotLOD.xyz;
    float lodFade = fract(instanceRotLOD.w * 100.0);  // Unpack fade
    
    // Build instance transform
    mat3 rotMatrix = rotationFromEuler(instanceRot);
    
    // Transform to local space
    vec3 localPos = inPosition * instanceScale;
    
    // Calculate wind displacement
    float windWeight = inColor.r;
    float phaseOffset = inColor.g;
    float branchMask = inColor.b;
    
    vec3 preWindWorldPos = rotMatrix * localPos + instancePos;
    vec3 windDisp = calculateWind(preWindWorldPos, windWeight, phaseOffset, branchMask);
    
    // Apply wind
    localPos += windDisp;
    
    // Transform to world space
    vec3 worldPos = rotMatrix * localPos + instancePos;
    
    // Calculate interaction displacement
    vec3 interactDisp = calculateInteraction(worldPos) * windWeight;
    worldPos += interactDisp;
    
    // Transform normal and tangent
    vec3 worldNormal = normalize(rotMatrix * inNormal);
    vec3 worldTangent = normalize(rotMatrix * inTangent.xyz);
    vec3 worldBitangent = cross(worldNormal, worldTangent) * inTangent.w;
    
    // Output
    outWorldPos = worldPos;
    outNormal = worldNormal;
    outTexCoord = inTexCoord;
    outTangent = worldTangent;
    outBitangent = worldBitangent;
    outFade = lodFade;
    outTypeId = instanceTypeId;
    
    gl_Position = ubo.viewProjection * vec4(worldPos, 1.0);
}
