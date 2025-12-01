/**
 * terrain_clipmap.frag
 * 
 * Terrain fragment shader with multi-layer material blending.
 * Based on UE5 Landscape material approach.
 * 
 * Features:
 * - Up to 16 material layers via splatmaps
 * - Height-based blending for natural transitions
 * - Triplanar mapping option for cliffs
 * - Detail normal mapping per layer
 * - Macro color variation
 */

#version 450
#extension GL_GOOGLE_include_directive : enable

// Input from vertex shader
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in float inMorphFactor;
layout(location = 5) in flat uint inLOD;

// GBuffer outputs
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outPBR;       // R: roughness, G: metallic, B: AO, A: emission

// Terrain uniforms
layout(set = 0, binding = 0) uniform TerrainUniforms {
    mat4 viewProjection;
    vec3 cameraPos;
    float time;
    vec3 worldOrigin;
    float pad0;
    vec2 worldSize;
    vec2 texelSize;
    ivec4 neighborLODs;
    float morphRange;
    uint numLevels;
    vec2 pad1;
} ubo;

// Splatmaps (4 layers per splatmap, RGBA weights)
layout(set = 1, binding = 0) uniform sampler2D splatmap0;   // Layers 0-3
layout(set = 1, binding = 1) uniform sampler2D splatmap1;   // Layers 4-7
layout(set = 1, binding = 2) uniform sampler2D splatmap2;   // Layers 8-11
layout(set = 1, binding = 3) uniform sampler2D splatmap3;   // Layers 12-15

// Material layer textures (array texture: each layer has albedo, normal, PBR)
layout(set = 2, binding = 0) uniform sampler2DArray layerAlbedo;
layout(set = 2, binding = 1) uniform sampler2DArray layerNormal;
layout(set = 2, binding = 2) uniform sampler2DArray layerPBR;       // R: roughness, G: metallic, B: AO
layout(set = 2, binding = 3) uniform sampler2DArray layerHeight;    // For height-based blending

// Layer configuration
layout(set = 3, binding = 0) uniform LayerConfig {
    vec4 uvScales[16];          // UV scale per layer
    vec4 heightBlendFactors[4]; // 4 layers per vec4
    vec4 normalStrengths[4];
    uint numLayers;
    float detailScale;
    float heightBlendSharpness;
    float pad;
} layerConfig;

// Macro color variation
layout(set = 3, binding = 1) uniform sampler2D macroVariation;

const float PI = 3.14159265359;

// Sample layer textures
struct LayerSample {
    vec3 albedo;
    vec3 normal;
    float roughness;
    float metallic;
    float ao;
    float height;
};

LayerSample sampleLayer(uint layerIndex, vec2 worldUV) {
    LayerSample result;
    
    float uvScale = layerConfig.uvScales[layerIndex / 4][layerIndex % 4];
    vec2 layerUV = worldUV * uvScale;
    
    // Sample textures
    result.albedo = texture(layerAlbedo, vec3(layerUV, float(layerIndex))).rgb;
    result.normal = texture(layerNormal, vec3(layerUV, float(layerIndex))).rgb * 2.0 - 1.0;
    vec3 pbr = texture(layerPBR, vec3(layerUV, float(layerIndex))).rgb;
    result.height = texture(layerHeight, vec3(layerUV, float(layerIndex))).r;
    
    result.roughness = pbr.r;
    result.metallic = pbr.g;
    result.ao = pbr.b;
    
    // Apply normal strength
    float normalStrength = layerConfig.normalStrengths[layerIndex / 4][layerIndex % 4];
    result.normal.xy *= normalStrength;
    result.normal = normalize(result.normal);
    
    return result;
}

// Height-based blending weights
vec4 heightBlend(vec4 heights, vec4 weights, float sharpness) {
    // Find max height adjusted by weight
    vec4 adjustedHeights = heights * weights;
    float maxHeight = max(max(adjustedHeights.x, adjustedHeights.y), 
                          max(adjustedHeights.z, adjustedHeights.w));
    
    // Calculate blend weights based on height difference from max
    vec4 diffs = maxHeight - adjustedHeights;
    vec4 blendWeights = max(vec4(0.0), weights - diffs * sharpness);
    
    // Normalize
    float total = blendWeights.x + blendWeights.y + blendWeights.z + blendWeights.w;
    return blendWeights / max(total, 0.001);
}

// Blend 4 layers
void blend4Layers(uint startLayer, vec4 weights, vec2 worldUV, 
                  inout vec3 albedo, inout vec3 normal, 
                  inout float roughness, inout float metallic, inout float ao) {
    if (dot(weights, weights) < 0.001) return;
    
    // Sample all 4 layers
    LayerSample samples[4];
    vec4 heights;
    
    for (uint i = 0; i < 4; i++) {
        uint layerIdx = startLayer + i;
        if (weights[i] > 0.001 && layerIdx < layerConfig.numLayers) {
            samples[i] = sampleLayer(layerIdx, worldUV);
            heights[i] = samples[i].height;
        } else {
            heights[i] = 0.0;
        }
    }
    
    // Apply height-based blending
    vec4 blendWeights = heightBlend(heights, weights, layerConfig.heightBlendSharpness);
    
    // Blend results
    for (uint i = 0; i < 4; i++) {
        float w = blendWeights[i];
        if (w > 0.001) {
            albedo += w * samples[i].albedo;
            normal += w * samples[i].normal;
            roughness += w * samples[i].roughness;
            metallic += w * samples[i].metallic;
            ao += w * samples[i].ao;
        }
    }
}

// Convert tangent-space normal to world space
vec3 tangentToWorld(vec3 tangentNormal, vec3 worldNormal, vec3 worldTangent) {
    vec3 bitangent = cross(worldNormal, worldTangent);
    mat3 TBN = mat3(worldTangent, bitangent, worldNormal);
    return normalize(TBN * tangentNormal);
}

// Triplanar mapping for steep slopes
LayerSample triplanarSample(uint layerIndex, vec3 worldPos, vec3 normal) {
    vec3 absNormal = abs(normal);
    float sum = absNormal.x + absNormal.y + absNormal.z;
    vec3 blendWeights = absNormal / sum;
    
    // Power for sharper transitions
    blendWeights = pow(blendWeights, vec3(4.0));
    blendWeights /= (blendWeights.x + blendWeights.y + blendWeights.z);
    
    LayerSample result;
    result.albedo = vec3(0.0);
    result.normal = vec3(0.0);
    result.roughness = 0.0;
    result.metallic = 0.0;
    result.ao = 0.0;
    result.height = 0.0;
    
    float uvScale = layerConfig.uvScales[layerIndex / 4][layerIndex % 4];
    
    // Sample from each projection plane
    if (blendWeights.x > 0.01) {
        vec2 uv = worldPos.zy * uvScale;
        LayerSample s = sampleLayer(layerIndex, uv);
        result.albedo += blendWeights.x * s.albedo;
        result.normal += blendWeights.x * s.normal;
        result.roughness += blendWeights.x * s.roughness;
        result.metallic += blendWeights.x * s.metallic;
        result.ao += blendWeights.x * s.ao;
    }
    
    if (blendWeights.y > 0.01) {
        vec2 uv = worldPos.xz * uvScale;
        LayerSample s = sampleLayer(layerIndex, uv);
        result.albedo += blendWeights.y * s.albedo;
        result.normal += blendWeights.y * s.normal;
        result.roughness += blendWeights.y * s.roughness;
        result.metallic += blendWeights.y * s.metallic;
        result.ao += blendWeights.y * s.ao;
    }
    
    if (blendWeights.z > 0.01) {
        vec2 uv = worldPos.xy * uvScale;
        LayerSample s = sampleLayer(layerIndex, uv);
        result.albedo += blendWeights.z * s.albedo;
        result.normal += blendWeights.z * s.normal;
        result.roughness += blendWeights.z * s.roughness;
        result.metallic += blendWeights.z * s.metallic;
        result.ao += blendWeights.z * s.ao;
    }
    
    result.normal = normalize(result.normal);
    return result;
}

void main() {
    // World-space UV for detail texturing
    vec2 worldUV = inWorldPos.xz * layerConfig.detailScale;
    
    // Sample splatmaps
    vec4 weights0 = texture(splatmap0, inUV);
    vec4 weights1 = texture(splatmap1, inUV);
    vec4 weights2 = texture(splatmap2, inUV);
    vec4 weights3 = texture(splatmap3, inUV);
    
    // Normalize all weights
    float totalWeight = dot(weights0, vec4(1.0)) + dot(weights1, vec4(1.0)) + 
                        dot(weights2, vec4(1.0)) + dot(weights3, vec4(1.0));
    if (totalWeight > 0.0) {
        float invTotal = 1.0 / totalWeight;
        weights0 *= invTotal;
        weights1 *= invTotal;
        weights2 *= invTotal;
        weights3 *= invTotal;
    }
    
    // Accumulate blended material
    vec3 albedo = vec3(0.0);
    vec3 normalTS = vec3(0.0);
    float roughness = 0.0;
    float metallic = 0.0;
    float ao = 0.0;
    
    // Blend each group of 4 layers
    blend4Layers(0, weights0, worldUV, albedo, normalTS, roughness, metallic, ao);
    blend4Layers(4, weights1, worldUV, albedo, normalTS, roughness, metallic, ao);
    blend4Layers(8, weights2, worldUV, albedo, normalTS, roughness, metallic, ao);
    blend4Layers(12, weights3, worldUV, albedo, normalTS, roughness, metallic, ao);
    
    // Normalize blended normal
    normalTS = normalize(normalTS);
    
    // Convert to world space
    vec3 worldNormal = tangentToWorld(normalTS, inNormal, inTangent);
    
    // Apply macro color variation
    vec3 macroColor = texture(macroVariation, inUV * 0.1).rgb;
    albedo *= mix(vec3(0.8), vec3(1.2), macroColor);
    
    // Distance-based detail fade
    float distToCamera = length(ubo.cameraPos - inWorldPos);
    float detailFade = 1.0 - smoothstep(100.0, 500.0, distToCamera);
    
    // Blend toward base normal at distance
    worldNormal = mix(inNormal, worldNormal, detailFade);
    
    // Output to GBuffer
    outAlbedo = vec4(albedo, 1.0);
    outNormal = vec4(worldNormal * 0.5 + 0.5, 1.0);
    outPBR = vec4(roughness, metallic, ao, 0.0);
}
