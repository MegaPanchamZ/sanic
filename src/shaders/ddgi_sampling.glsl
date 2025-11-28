// ============================================================================
// DDGI SAMPLING FUNCTIONS
// Include this in composition shaders to sample the DDGI probe grid
// ============================================================================

#ifndef DDGI_SAMPLING_GLSL
#define DDGI_SAMPLING_GLSL

// DDGI Uniforms structure (must match DDGISystem.h)
struct DDGIParams {
    ivec4 probeCount;           // xyz = count, w = total probes
    vec4 probeSpacing;          // xyz = spacing, w = 1/maxDistance
    vec4 gridOrigin;            // xyz = origin, w = hysteresis
    ivec4 irradianceTextureSize; // xy = texture size, zw = probe size
    ivec4 depthTextureSize;      // xy = texture size, zw = probe size
    vec4 rayParams;             // x = raysPerProbe, y = maxDistance, z = normalBias, w = viewBias
};

// ============================================================================
// OCTAHEDRAL MAPPING
// ============================================================================

// Convert 3D direction to octahedral UV [0,1]^2
vec2 octEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    
    if (n.y < 0.0) {
        vec2 wrapped = (1.0 - abs(n.zx)) * sign(n.xz);
        n.x = wrapped.x;
        n.z = wrapped.y;
    }
    
    return n.xz * 0.5 + 0.5;
}

// Convert octahedral UV [0,1]^2 to 3D direction
vec3 octDecode(vec2 uv) {
    vec2 f = uv * 2.0 - 1.0;
    vec3 n = vec3(f.x, 1.0 - abs(f.x) - abs(f.y), f.y);
    float t = max(-n.y, 0.0);
    n.x += (n.x >= 0.0) ? -t : t;
    n.z += (n.z >= 0.0) ? -t : t;
    return normalize(n);
}

// ============================================================================
// PROBE GRID UTILITIES
// ============================================================================

// Get grid-space coordinates from world position
vec3 worldToGrid(vec3 worldPos, DDGIParams params) {
    return (worldPos - params.gridOrigin.xyz) / params.probeSpacing.xyz;
}

// Clamp grid coordinates to valid probe range
ivec3 clampProbeCoord(ivec3 coord, DDGIParams params) {
    return clamp(coord, ivec3(0), params.probeCount.xyz - 1);
}

// Get probe index from grid coordinates
int getProbeIndex(ivec3 coord, DDGIParams params) {
    return coord.y * (params.probeCount.x * params.probeCount.z) + 
           coord.z * params.probeCount.x + 
           coord.x;
}

// Get world position of a probe
vec3 getProbeWorldPos(ivec3 coord, DDGIParams params) {
    return params.gridOrigin.xyz + vec3(coord) * params.probeSpacing.xyz;
}

// ============================================================================
// TEXTURE COORDINATE CALCULATION
// ============================================================================

// Get texture coordinates for sampling a probe's irradiance
vec2 getProbeIrradianceUV(ivec3 probeCoord, vec3 direction, DDGIParams params) {
    // Calculate probe's position in the 2D atlas
    int probesPerRow = params.probeCount.x * params.probeCount.z;
    int probeX = probeCoord.x + probeCoord.z * params.probeCount.x;
    int probeY = probeCoord.y;
    
    // Get octahedral UV for the direction
    vec2 octUV = octEncode(direction);
    
    // Map to texel coordinates within probe (accounting for 1-pixel border)
    int probeSize = params.irradianceTextureSize.z;
    int innerSize = probeSize - 2;
    
    vec2 texelCoord = vec2(1.0) + octUV * float(innerSize);
    
    // Calculate final UV in the atlas
    vec2 probeCorner = vec2(probeX * probeSize, probeY * probeSize);
    vec2 atlasSize = vec2(params.irradianceTextureSize.xy);
    
    return (probeCorner + texelCoord) / atlasSize;
}

// Get texture coordinates for sampling a probe's depth
vec2 getProbeDepthUV(ivec3 probeCoord, vec3 direction, DDGIParams params) {
    int probesPerRow = params.probeCount.x * params.probeCount.z;
    int probeX = probeCoord.x + probeCoord.z * params.probeCount.x;
    int probeY = probeCoord.y;
    
    vec2 octUV = octEncode(direction);
    
    int probeSize = params.depthTextureSize.z;
    int innerSize = probeSize - 2;
    
    vec2 texelCoord = vec2(1.0) + octUV * float(innerSize);
    
    vec2 probeCorner = vec2(probeX * probeSize, probeY * probeSize);
    vec2 atlasSize = vec2(params.depthTextureSize.xy);
    
    return (probeCorner + texelCoord) / atlasSize;
}

// ============================================================================
// VISIBILITY / WEIGHT CALCULATION
// ============================================================================

// Calculate visibility weight for a probe based on depth
float getProbeVisibility(
    vec3 worldPos,
    vec3 normal,
    ivec3 probeCoord,
    DDGIParams params,
    sampler2D depthTexture
) {
    vec3 probePos = getProbeWorldPos(probeCoord, params);
    vec3 toProbe = probePos - worldPos;
    float distToProbe = length(toProbe);
    vec3 dirToProbe = toProbe / max(distToProbe, 0.001);
    
    // Sample probe depth in the direction from probe toward surface
    vec2 depthUV = getProbeDepthUV(probeCoord, -dirToProbe, params);
    vec2 depthData = texture(depthTexture, depthUV).rg;
    float meanDepth = depthData.r;
    float variance = depthData.g;
    
    // Chebyshev visibility test
    float chebyshev = 1.0;
    if (distToProbe > meanDepth) {
        float diff = distToProbe - meanDepth;
        chebyshev = variance / (variance + diff * diff);
        chebyshev = max(chebyshev * chebyshev * chebyshev, 0.0);  // Sharpen
    }
    
    return chebyshev;
}

// ============================================================================
// MAIN SAMPLING FUNCTION
// ============================================================================

// Sample irradiance from the DDGI probe grid
vec3 sampleDDGI(
    vec3 worldPos,
    vec3 normal,
    DDGIParams params,
    sampler2D irradianceTexture,
    sampler2D depthTexture
) {
    // Apply bias to avoid self-shadowing
    float normalBias = params.rayParams.z;
    vec3 biasedPos = worldPos + normal * normalBias;
    
    // Get grid coordinates
    vec3 gridPos = worldToGrid(biasedPos, params);
    ivec3 baseProbe = ivec3(floor(gridPos));
    vec3 alpha = fract(gridPos);
    
    vec3 irradiance = vec3(0.0);
    float weightSum = 0.0;
    
    // Trilinear interpolation over 8 surrounding probes
    for (int i = 0; i < 8; i++) {
        ivec3 offset = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        ivec3 probeCoord = clampProbeCoord(baseProbe + offset, params);
        
        // Calculate trilinear weight
        vec3 triWeight = mix(1.0 - alpha, alpha, vec3(offset));
        float weight = triWeight.x * triWeight.y * triWeight.z;
        
        // Get probe position
        vec3 probePos = getProbeWorldPos(probeCoord, params);
        vec3 toProbe = probePos - worldPos;
        vec3 dirToProbe = normalize(toProbe);
        
        // Weight by normal alignment (backface weight reduction)
        float normalWeight = max(0.0001, dot(dirToProbe, normal) * 0.5 + 0.5);
        normalWeight = normalWeight * normalWeight;
        
        // Visibility weight from depth
        float visibility = getProbeVisibility(worldPos, normal, probeCoord, params, depthTexture);
        
        // Combined weight
        float combinedWeight = weight * normalWeight * visibility;
        
        if (combinedWeight > 0.0001) {
            // Sample irradiance from probe in direction of normal
            vec2 irradianceUV = getProbeIrradianceUV(probeCoord, normal, params);
            vec3 probeIrradiance = texture(irradianceTexture, irradianceUV).rgb;
            
            irradiance += probeIrradiance * combinedWeight;
            weightSum += combinedWeight;
        }
    }
    
    if (weightSum > 0.0001) {
        irradiance /= weightSum;
    }
    
    return irradiance;
}

#endif // DDGI_SAMPLING_GLSL
