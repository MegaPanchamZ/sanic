/**
 * substrate_eval.glsl
 * 
 * Substrate material evaluation library.
 * Multi-layer BSDF with energy conservation.
 * 
 * Include this in deferred lighting or forward shaders.
 * 
 * Based on Unreal Engine 5 Substrate/SubstrateMaterial.usf
 */

#ifndef SUBSTRATE_EVAL_GLSL
#define SUBSTRATE_EVAL_GLSL

// ============================================================================
// CONSTANTS
// ============================================================================

const uint SUBSTRATE_SLAB_STANDARD = 0;
const uint SUBSTRATE_SLAB_CLEARCOAT = 1;
const uint SUBSTRATE_SLAB_TRANSMISSION = 2;
const uint SUBSTRATE_SLAB_SUBSURFACE = 3;
const uint SUBSTRATE_SLAB_CLOTH = 4;
const uint SUBSTRATE_SLAB_HAIR = 5;
const uint SUBSTRATE_SLAB_EYE = 6;
const uint SUBSTRATE_SLAB_THINFILM = 7;

const uint MAX_SUBSTRATE_SLABS = 8;
const float PI = 3.14159265359;
const float INV_PI = 0.31830988618;

// ============================================================================
// SLAB DATA STRUCTURE (128 bytes per slab)
// ============================================================================

struct SubstrateSlab {
    vec4 typeBlendWeightThickness;      // x=type, y=blendMode, z=weight, w=thickness
    vec4 baseColorOpacity;              // xyz=color, w=opacity
    vec4 metallicRoughnessSpecularAniso;// x=metallic, y=roughness, z=specular, w=aniso
    vec4 normalClearCoatIOR;            // x=normalStr, y=ccRoughness, z=ccIOR, w=transIOR
    vec4 absorptionSubsurface;          // xyz=absorption, w=sssRadius
    vec4 subsurfaceColorPhase;          // xyz=sssColor, w=sssPhase
    vec4 sheenColorRoughness;           // xyz=sheen, w=sheenRough
    vec4 thinFilmHair;                  // x=filmThick, y=filmIOR, z=hairScatter, w=hairShift
};

// ============================================================================
// MATERIAL DATA STRUCTURE (256 bytes per material)
// ============================================================================

struct SubstrateMaterial {
    SubstrateSlab slabs[MAX_SUBSTRATE_SLABS];
    uvec4 flagsAndCounts;   // x=slabCount, y=flags, z=texMask, w=reserved
    ivec4 textureIndices0;  // baseColor, normal, metallicRoughness, emissive
    ivec4 textureIndices1;  // clearCoat, subsurface, reserved, reserved
};

// ============================================================================
// FRESNEL FUNCTIONS
// ============================================================================

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float fresnelSchlickScalar(float cosTheta, float F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Dielectric Fresnel (IOR based)
float fresnelDielectric(float cosI, float ior) {
    float sinT2 = (1.0 - cosI * cosI) / (ior * ior);
    if (sinT2 > 1.0) return 1.0; // TIR
    
    float cosT = sqrt(1.0 - sinT2);
    float rs = (cosI - ior * cosT) / (cosI + ior * cosT);
    float rp = (ior * cosI - cosT) / (ior * cosI + cosT);
    return 0.5 * (rs * rs + rp * rp);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// DISTRIBUTION FUNCTIONS
// ============================================================================

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return nom / max(denom, 0.0001);
}

// Anisotropic GGX
float distributionGGXAniso(vec3 N, vec3 H, vec3 T, vec3 B, float ax, float ay) {
    float NdotH = max(dot(N, H), 0.0);
    float TdotH = dot(T, H);
    float BdotH = dot(B, H);
    
    float d = (TdotH * TdotH) / (ax * ax) + (BdotH * BdotH) / (ay * ay) + NdotH * NdotH;
    return 1.0 / (PI * ax * ay * d * d);
}

// ============================================================================
// GEOMETRY FUNCTIONS
// ============================================================================

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// ============================================================================
// SLAB EVALUATION FUNCTIONS
// ============================================================================

/**
 * Evaluate standard PBR slab
 */
vec3 evaluateStandardSlab(SubstrateSlab slab, vec3 V, vec3 L, vec3 N, out float throughput) {
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0001);
    float NdotL = max(dot(N, L), 0.0001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    
    vec3 baseColor = slab.baseColorOpacity.rgb;
    float metallic = slab.metallicRoughnessSpecularAniso.x;
    float roughness = max(slab.metallicRoughnessSpecularAniso.y, 0.04);
    float specular = slab.metallicRoughnessSpecularAniso.z;
    
    vec3 F0 = mix(vec3(0.04 * specular), baseColor, metallic);
    
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(VdotH, F0);
    
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    
    vec3 diffuse = kD * baseColor * INV_PI;
    vec3 specularBRDF = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);
    
    throughput = 1.0 - max(max(F.r, F.g), F.b);
    
    return (diffuse + specularBRDF) * NdotL * slab.typeBlendWeightThickness.z;
}

/**
 * Evaluate clear coat slab
 */
vec3 evaluateClearCoatSlab(SubstrateSlab slab, vec3 V, vec3 L, vec3 N, out float throughput) {
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0001);
    float NdotL = max(dot(N, L), 0.0001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    
    float ccRoughness = max(slab.normalClearCoatIOR.y, 0.01);
    float ccIOR = slab.normalClearCoatIOR.z;
    
    float F = fresnelDielectric(VdotH, ccIOR);
    float D = distributionGGX(N, H, ccRoughness);
    float G = geometrySmith(N, V, L, ccRoughness);
    
    float specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);
    
    throughput = 1.0 - F;
    
    // Apply absorption through clear coat
    float thickness = slab.typeBlendWeightThickness.w;
    if (thickness > 0.0) {
        vec3 absorption = slab.absorptionSubsurface.rgb;
        throughput *= exp(-dot(absorption, vec3(1.0)) * thickness);
    }
    
    return vec3(specular * NdotL) * slab.typeBlendWeightThickness.z;
}

/**
 * Evaluate transmission slab
 */
vec3 evaluateTransmissionSlab(SubstrateSlab slab, vec3 V, vec3 L, vec3 N, out float throughput) {
    vec3 H = normalize(V + L);
    float VdotH = max(dot(V, H), 0.0);
    
    float transIOR = slab.normalClearCoatIOR.w;
    float F = fresnelDielectric(VdotH, transIOR);
    
    vec3 baseColor = slab.baseColorOpacity.rgb;
    vec3 absorption = slab.absorptionSubsurface.rgb;
    float thickness = slab.typeBlendWeightThickness.w;
    
    // Beer-Lambert absorption
    vec3 transmittedColor = baseColor * exp(-absorption * thickness);
    
    throughput = 1.0 - F;
    
    return transmittedColor * (1.0 - F) * slab.typeBlendWeightThickness.z;
}

/**
 * Evaluate subsurface scattering slab
 */
vec3 evaluateSubsurfaceSlab(SubstrateSlab slab, vec3 V, vec3 L, vec3 N, out float throughput) {
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0001);
    float NdotL = max(dot(N, L), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    
    vec3 baseColor = slab.baseColorOpacity.rgb;
    vec3 sssColor = slab.subsurfaceColorPhase.rgb;
    float sssRadius = slab.absorptionSubsurface.w;
    float sssPhase = slab.subsurfaceColorPhase.w;
    
    vec3 F = fresnelSchlick(VdotH, vec3(0.04));
    
    // Wrapped diffuse
    float wrap = 0.5;
    float diffuseWrap = max(0.0, (NdotL + wrap) / (1.0 + wrap));
    
    // Back-scatter approximation
    float backScatter = max(0.0, dot(V, -L));
    vec3 sss = sssColor * backScatter * sssRadius * 0.25;
    
    // Forward scatter (phase function approximation)
    float forwardScatter = pow(max(0.0, dot(L, V)), 8.0 * (1.0 - sssPhase));
    sss += sssColor * forwardScatter * sssRadius * 0.25;
    
    throughput = 1.0 - max(max(F.r, F.g), F.b);
    
    return ((1.0 - F) * baseColor * diffuseWrap + sss) * slab.typeBlendWeightThickness.z;
}

/**
 * Evaluate cloth slab (Ashikhmin model)
 */
vec3 evaluateClothSlab(SubstrateSlab slab, vec3 V, vec3 L, vec3 N, out float throughput) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0001);
    float VdotH = max(dot(V, H), 0.0);
    
    vec3 baseColor = slab.baseColorOpacity.rgb;
    vec3 sheenColor = slab.sheenColorRoughness.rgb;
    float sheenRoughness = slab.sheenColorRoughness.w;
    
    // Diffuse
    vec3 diffuse = baseColor * NdotL * INV_PI;
    
    // Sheen (Charlie distribution approximation)
    float sheenDist = pow(1.0 - VdotH, 5.0) / (6.0 * sheenRoughness + 0.001);
    vec3 sheen = sheenColor * sheenDist;
    
    throughput = 0.9; // Cloth absorbs some light
    
    return (diffuse + sheen * NdotL) * slab.typeBlendWeightThickness.z;
}

/**
 * Evaluate thin-film interference slab
 */
vec3 evaluateThinFilmSlab(SubstrateSlab slab, vec3 V, vec3 L, vec3 N, out float throughput) {
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0001);
    float NdotL = max(dot(N, L), 0.0001);
    float VdotH = max(dot(V, H), 0.0);
    
    float filmThickness = slab.thinFilmHair.x;
    float filmIOR = slab.thinFilmHair.y;
    
    // Thin-film interference calculation
    // Path length through film
    float cosT = sqrt(1.0 - (1.0 - VdotH * VdotH) / (filmIOR * filmIOR));
    float pathLength = 2.0 * filmThickness * cosT;
    
    // Interference for RGB wavelengths (approximate)
    vec3 wavelengths = vec3(650.0, 550.0, 450.0); // nm
    vec3 phase = 2.0 * PI * filmIOR * pathLength / wavelengths;
    vec3 interference = 0.5 + 0.5 * cos(phase);
    
    // Fresnel at interfaces
    float F1 = fresnelDielectric(VdotH, filmIOR);
    float F2 = fresnelDielectric(cosT, 1.0 / filmIOR);
    
    vec3 reflectance = mix(vec3(F1), interference, F1 * (1.0 - F2));
    
    throughput = 1.0 - max(max(reflectance.r, reflectance.g), reflectance.b);
    
    float roughness = max(slab.metallicRoughnessSpecularAniso.y, 0.04);
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    
    return reflectance * D * G / max(4.0 * NdotV * NdotL, 0.0001) * NdotL * slab.typeBlendWeightThickness.z;
}

// ============================================================================
// MAIN EVALUATION FUNCTION
// ============================================================================

/**
 * Evaluate a single slab based on its type
 */
vec3 evaluateSlab(SubstrateSlab slab, vec3 V, vec3 L, vec3 N, out float throughput) {
    uint slabType = uint(slab.typeBlendWeightThickness.x);
    
    switch (slabType) {
        case SUBSTRATE_SLAB_STANDARD:
            return evaluateStandardSlab(slab, V, L, N, throughput);
        case SUBSTRATE_SLAB_CLEARCOAT:
            return evaluateClearCoatSlab(slab, V, L, N, throughput);
        case SUBSTRATE_SLAB_TRANSMISSION:
            return evaluateTransmissionSlab(slab, V, L, N, throughput);
        case SUBSTRATE_SLAB_SUBSURFACE:
            return evaluateSubsurfaceSlab(slab, V, L, N, throughput);
        case SUBSTRATE_SLAB_CLOTH:
            return evaluateClothSlab(slab, V, L, N, throughput);
        case SUBSTRATE_SLAB_THINFILM:
            return evaluateThinFilmSlab(slab, V, L, N, throughput);
        default:
            throughput = 1.0;
            return evaluateStandardSlab(slab, V, L, N, throughput);
    }
}

/**
 * Evaluate complete multi-layer substrate material
 * Evaluates from top to bottom with energy conservation
 */
vec3 evaluateSubstrateMaterial(SubstrateMaterial material, vec3 V, vec3 L, vec3 N) {
    uint slabCount = material.flagsAndCounts.x;
    
    vec3 totalBSDF = vec3(0.0);
    float accumulatedThroughput = 1.0;
    
    // Evaluate from top layer down
    for (int i = int(slabCount) - 1; i >= 0; i--) {
        SubstrateSlab slab = material.slabs[i];
        
        float slabThroughput;
        vec3 slabBSDF = evaluateSlab(slab, V, L, N, slabThroughput);
        
        // Accumulate with current throughput
        totalBSDF += accumulatedThroughput * slabBSDF;
        
        // Update throughput for lower layers
        accumulatedThroughput *= slabThroughput;
        
        // Apply absorption through thickness
        float thickness = slab.typeBlendWeightThickness.w;
        if (thickness > 0.0) {
            vec3 absorption = slab.absorptionSubsurface.rgb;
            if (dot(absorption, absorption) > 0.0) {
                accumulatedThroughput *= dot(exp(-absorption * thickness), vec3(1.0 / 3.0));
            }
        }
        
        // Early out if no light reaches lower layers
        if (accumulatedThroughput < 0.001) break;
    }
    
    return totalBSDF;
}

/**
 * Sample environment/indirect lighting for substrate material
 */
vec3 evaluateSubstrateEnvironment(SubstrateMaterial material, vec3 V, vec3 N, 
                                   vec3 diffuseGI, vec3 specularGI) {
    uint slabCount = material.flagsAndCounts.x;
    
    vec3 totalIndirect = vec3(0.0);
    float accumulatedThroughput = 1.0;
    
    for (int i = int(slabCount) - 1; i >= 0; i--) {
        SubstrateSlab slab = material.slabs[i];
        
        vec3 baseColor = slab.baseColorOpacity.rgb;
        float metallic = slab.metallicRoughnessSpecularAniso.x;
        float roughness = slab.metallicRoughnessSpecularAniso.y;
        float specular = slab.metallicRoughnessSpecularAniso.z;
        
        float NdotV = max(dot(N, V), 0.0001);
        
        vec3 F0 = mix(vec3(0.04 * specular), baseColor, metallic);
        vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
        
        vec3 kS = F;
        vec3 kD = (1.0 - kS) * (1.0 - metallic);
        
        vec3 diffuse = kD * baseColor * diffuseGI;
        vec3 specularIndirect = specularGI * F;
        
        totalIndirect += accumulatedThroughput * (diffuse + specularIndirect) * slab.typeBlendWeightThickness.z;
        
        accumulatedThroughput *= 1.0 - max(max(F.r, F.g), F.b);
        
        if (accumulatedThroughput < 0.001) break;
    }
    
    return totalIndirect;
}

#endif // SUBSTRATE_EVAL_GLSL
