#version 450

// ============================================================================
// COMPOSITION FRAGMENT SHADER - Deferred Lighting Pass
// Reads G-Buffer via input attachments and applies full PBR lighting
// Now with DDGI (Dynamic Diffuse Global Illumination) support!
// ============================================================================

// G-Buffer Input Attachments (from geometry subpass)
layout(input_attachment_index = 0, binding = 0) uniform subpassInput inputPosition;
layout(input_attachment_index = 1, binding = 1) uniform subpassInput inputNormal;
layout(input_attachment_index = 2, binding = 2) uniform subpassInput inputAlbedo;
layout(input_attachment_index = 3, binding = 3) uniform subpassInput inputPBR;

// External resources
layout(binding = 4) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 lightPos;
    vec4 viewPos;
    vec4 lightColor;
    mat4 lightSpaceMatrix;
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;
    vec4 shadowParams;
} ubo;

layout(binding = 5) uniform sampler2DArray shadowMapArray;  // CSM cascade array
layout(binding = 6) uniform samplerCube environmentMap;

// ============================================================================
// DDGI Resources - Using dummy samplers to avoid validation errors
// The actual DDGI/SSR code paths are disabled via const bools below
// ============================================================================

// Dummy UBO for DDGI (always present to satisfy shader structure)
layout(binding = 7) uniform DDGIUniformBlock {
    ivec4 probeCount;           // xyz = count, w = total probes
    vec4 probeSpacing;          // xyz = spacing, w = 1/maxDistance
    vec4 gridOrigin;            // xyz = origin, w = hysteresis
    ivec4 irradianceTextureSize; // xy = texture size, zw = probe size
    ivec4 depthTextureSize;      // xy = texture size, zw = probe size
    vec4 rayParams;             // x = raysPerProbe, y = maxDistance, z = normalBias, w = viewBias
    mat4 randomRotation;
} ddgi;

// DDGI textures - bound to environment map as placeholder when not used
layout(binding = 8) uniform sampler2D ddgiIrradiance;
layout(binding = 9) uniform sampler2D ddgiDepth;

// SSR Reflections texture - bound to a 1x1 black texture when not used
layout(binding = 10) uniform sampler2D ssrReflections;

// Feature enable flags - DISABLED for now until descriptor bindings are fully implemented
// Set these to true once the descriptor sets are properly bound in C++
const bool DDGI_ENABLED = false;
const bool SSR_ENABLED = false;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// ============================================================================
// CONSTANTS
// ============================================================================
const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;
const float INV_PI = 0.31830988618;
const float MAX_REFLECTION_LOD = 4.0;
const vec3 DIELECTRIC_F0 = vec3(0.04);
const float MULTI_SCATTER_COMPENSATION = 1.0;

// ============================================================================
// DDGI SAMPLING FUNCTIONS (Octahedral Mapping)
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

// Get grid-space coordinates from world position
vec3 worldToGrid(vec3 worldPos) {
    return (worldPos - ddgi.gridOrigin.xyz) / ddgi.probeSpacing.xyz;
}

// Clamp grid coordinates to valid probe range
ivec3 clampProbeCoord(ivec3 coord) {
    return clamp(coord, ivec3(0), ddgi.probeCount.xyz - 1);
}

// Get world position of a probe
vec3 getProbeWorldPos(ivec3 coord) {
    return ddgi.gridOrigin.xyz + vec3(coord) * ddgi.probeSpacing.xyz;
}

// Get texture coordinates for sampling a probe's irradiance
vec2 getProbeIrradianceUV(ivec3 probeCoord, vec3 direction) {
    int probeX = probeCoord.x + probeCoord.z * ddgi.probeCount.x;
    int probeY = probeCoord.y;
    
    vec2 octUV = octEncode(direction);
    
    int probeSize = ddgi.irradianceTextureSize.z;
    int innerSize = probeSize - 2;
    
    vec2 texelCoord = vec2(1.0) + octUV * float(innerSize);
    vec2 probeCorner = vec2(probeX * probeSize, probeY * probeSize);
    vec2 atlasSize = vec2(ddgi.irradianceTextureSize.xy);
    
    return (probeCorner + texelCoord) / atlasSize;
}

// Get texture coordinates for sampling a probe's depth
vec2 getProbeDepthUV(ivec3 probeCoord, vec3 direction) {
    int probeX = probeCoord.x + probeCoord.z * ddgi.probeCount.x;
    int probeY = probeCoord.y;
    
    vec2 octUV = octEncode(direction);
    
    int probeSize = ddgi.depthTextureSize.z;
    int innerSize = probeSize - 2;
    
    vec2 texelCoord = vec2(1.0) + octUV * float(innerSize);
    vec2 probeCorner = vec2(probeX * probeSize, probeY * probeSize);
    vec2 atlasSize = vec2(ddgi.depthTextureSize.xy);
    
    return (probeCorner + texelCoord) / atlasSize;
}

// Calculate visibility weight for a probe based on depth (Chebyshev)
float getProbeVisibility(vec3 worldPos, ivec3 probeCoord) {
    vec3 probePos = getProbeWorldPos(probeCoord);
    vec3 toProbe = probePos - worldPos;
    float distToProbe = length(toProbe);
    vec3 dirToProbe = toProbe / max(distToProbe, 0.001);
    
    vec2 depthUV = getProbeDepthUV(probeCoord, -dirToProbe);
    vec2 depthData = texture(ddgiDepth, depthUV).rg;
    float meanDepth = depthData.r;
    float variance = depthData.g;
    
    float chebyshev = 1.0;
    if (distToProbe > meanDepth) {
        float diff = distToProbe - meanDepth;
        chebyshev = variance / (variance + diff * diff);
        chebyshev = max(chebyshev * chebyshev * chebyshev, 0.0);
    }
    
    return chebyshev;
}

// Main DDGI sampling function - trilinear interpolation over 8 probes
vec3 sampleDDGI(vec3 worldPos, vec3 normal) {
    float normalBias = ddgi.rayParams.z;
    vec3 biasedPos = worldPos + normal * normalBias;
    
    vec3 gridPos = worldToGrid(biasedPos);
    ivec3 baseProbe = ivec3(floor(gridPos));
    vec3 alpha = fract(gridPos);
    
    vec3 irradiance = vec3(0.0);
    float weightSum = 0.0;
    
    // Trilinear interpolation over 8 surrounding probes
    for (int i = 0; i < 8; i++) {
        ivec3 offset = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        ivec3 probeCoord = clampProbeCoord(baseProbe + offset);
        
        vec3 triWeight = mix(1.0 - alpha, alpha, vec3(offset));
        float weight = triWeight.x * triWeight.y * triWeight.z;
        
        vec3 probePos = getProbeWorldPos(probeCoord);
        vec3 toProbe = probePos - worldPos;
        vec3 dirToProbe = normalize(toProbe);
        
        // Backface weight reduction
        float normalWeight = max(0.0001, dot(dirToProbe, normal) * 0.5 + 0.5);
        normalWeight = normalWeight * normalWeight;
        
        float visibility = getProbeVisibility(worldPos, probeCoord);
        float combinedWeight = weight * normalWeight * visibility;
        
        if (combinedWeight > 0.0001) {
            vec2 irradianceUV = getProbeIrradianceUV(probeCoord, normal);
            vec3 probeIrradiance = texture(ddgiIrradiance, irradianceUV).rgb;
            
            irradiance += probeIrradiance * combinedWeight;
            weightSum += combinedWeight;
        }
    }
    
    if (weightSum > 0.0001) {
        irradiance /= weightSum;
    }
    
    return irradiance;
}

// ============================================================================
// PBR FUNCTIONS
// ============================================================================

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / max(denom, 0.00001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + 0.00001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 MultiScatterCompensation(vec3 F0, float roughness, float NdotV) {
    float Ess = 1.0 - roughness * 0.3;
    vec3 Favg = F0 + (1.0 - F0) * 0.047619;
    vec3 Fms = (1.0 - Ess) * F0 * Favg / (1.0 - Favg * (1.0 - Ess) + 0.0001);
    return Fms * MULTI_SCATTER_COMPENSATION;
}

// BRDF LUT approximation
vec2 IntegrateBRDF_Approx(float NdotV, float roughness) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// ============================================================================
// CASCADED SHADOW MAPPING
// ============================================================================

int getCascadeIndex(float viewDepth) {
    int cascade = 0;
    for (int i = 0; i < 4; i++) {
        if (viewDepth < ubo.cascadeSplits[i]) {
            cascade = i;
            break;
        }
        cascade = i;
    }
    return cascade;
}

float VogelDiskSample(int sampleIndex, int sampleCount, float phi, out vec2 offset) {
    float goldenAngle = 2.4;
    float r = sqrt(float(sampleIndex) + 0.5) / sqrt(float(sampleCount));
    float theta = float(sampleIndex) * goldenAngle + phi;
    offset = vec2(cos(theta), sin(theta)) * r;
    return r;
}

float InterleavedGradientNoise(vec2 screenPos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

float ShadowCalculationCSM(vec3 worldPos, vec3 normal, vec3 lightDir, float viewDepth) {
    int cascade = getCascadeIndex(viewDepth);
    
    // Transform to light space for this cascade
    vec4 lightSpacePos = ubo.cascadeViewProj[cascade] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * 0.5 + 0.5;
    
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    
    float currentDepth = projCoords.z;
    
    // Slope-scale bias
    float cosTheta = max(dot(normal, lightDir), 0.0);
    float slopeScale = sqrt(1.0 - cosTheta * cosTheta) / max(cosTheta, 0.001);
    float bias = max(0.002 * slopeScale, 0.0005) * (1.0 + float(cascade) * 0.5);
    
    // PCF with Vogel disk
    float shadow = 0.0;
    float shadowMapSize = ubo.shadowParams.x;
    vec2 texelSize = 1.0 / vec2(shadowMapSize);
    float rotation = InterleavedGradientNoise(gl_FragCoord.xy) * TWO_PI;
    
    const int SAMPLE_COUNT = 16;
    float filterRadius = 2.0 * (1.0 + float(cascade) * 0.5);
    
    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        vec2 offset;
        VogelDiskSample(i, SAMPLE_COUNT, rotation, offset);
        offset *= filterRadius * texelSize;
        
        float pcfDepth = texture(shadowMapArray, vec3(projCoords.xy + offset, float(cascade))).r;
        shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
    }
    
    shadow /= float(SAMPLE_COUNT);
    
    // Cascade edge fade
    vec2 fade = smoothstep(vec2(0.0), vec2(0.05), projCoords.xy) * 
                (1.0 - smoothstep(vec2(0.95), vec2(1.0), projCoords.xy));
    shadow *= fade.x * fade.y;
    
    return shadow;
}

// ============================================================================
// LIGHTING CALCULATIONS
// ============================================================================

vec3 calculatePBRLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, float lightIntensity,
                       vec3 albedo, float metallic, float roughness, vec3 F0, float ao) {
    vec3 H = normalize(V + L);
    
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    
    if (NdotL <= 0.0) return vec3(0.0);
    
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(VdotH, F0);
    
    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular = numerator / denominator;
    
    vec3 FmsEms = MultiScatterCompensation(F0, roughness, NdotV);
    specular += FmsEms * (1.0 - F) * metallic;
    
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    
    vec3 diffuse = kD * albedo * INV_PI;
    
    vec3 radiance = lightColor * lightIntensity;
    return (diffuse + specular) * radiance * NdotL;
}

vec3 calculateIBL(vec3 N, vec3 V, vec3 R, vec3 albedo, float metallic, float roughness, vec3 F0, float ao) {
    float NdotV = max(dot(N, V), 0.0);
    
    vec3 prefilteredColor = textureLod(environmentMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 envBRDF = IntegrateBRDF_Approx(NdotV, roughness);
    vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    
    vec3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);
    vec3 FmsEms = MultiScatterCompensation(F0, roughness, NdotV);
    specular += prefilteredColor * FmsEms;
    
    vec3 irradiance = textureLod(environmentMap, N, MAX_REFLECTION_LOD).rgb;
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 diffuse = kD * irradiance * albedo;
    
    return (diffuse + specular) * ao;
}

// ============================================================================
// TONE MAPPING
// ============================================================================

vec3 ACESFitted(vec3 color) {
    mat3 inputMatrix = mat3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    );
    mat3 outputMatrix = mat3(
        1.60475, -0.53108, -0.07367,
        -0.10208, 1.10813, -0.00605,
        -0.00327, -0.07276, 1.07602
    );
    color = inputMatrix * color;
    vec3 a = color * (color + 0.0245786) - 0.000090537;
    vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    color = a / b;
    return clamp(outputMatrix * color, 0.0, 1.0);
}

// ============================================================================
// MAIN - Deferred Lighting Composition
// ============================================================================
void main() {
    // Read G-Buffer
    vec4 posData = subpassLoad(inputPosition);
    vec4 normalData = subpassLoad(inputNormal);
    vec4 albedoData = subpassLoad(inputAlbedo);
    vec4 pbrData = subpassLoad(inputPBR);
    
    // Check for sky pixels (no geometry rendered)
    float viewDepth = posData.a;
    if (viewDepth <= 0.0) {
        discard; // Let skybox pass handle this
    }
    
    // Reconstruct material properties
    vec3 fragPos = posData.rgb;
    vec3 N = normalize(normalData.rgb * 2.0 - 1.0);  // Unpack from [0,1] to [-1,1]
    vec3 albedo = albedoData.rgb;
    float metallic = albedoData.a;
    float roughness = pbrData.r;
    float ao = pbrData.g;
    
    // Calculate view and reflection vectors
    vec3 V = normalize(ubo.viewPos.xyz - fragPos);
    vec3 R = reflect(-V, N);
    
    // Calculate F0
    vec3 F0 = DIELECTRIC_F0;
    F0 = mix(F0, albedo, metallic);
    
    // ========================================================================
    // DIRECT LIGHTING
    // ========================================================================
    vec3 Lo = vec3(0.0);
    
    vec3 mainLightDir;
    if (ubo.lightPos.w < 0.5) {
        mainLightDir = normalize(ubo.lightPos.xyz);
    } else {
        mainLightDir = normalize(ubo.lightPos.xyz - fragPos);
    }
    
    Lo += calculatePBRLight(N, V, mainLightDir, ubo.lightColor.rgb, 3.0,
                            albedo, metallic, roughness, F0, ao);
    
    // ========================================================================
    // GLOBAL ILLUMINATION (DDGI or fallback to IBL)
    // ========================================================================
    vec3 ambient = vec3(0.0);
    vec3 indirectDiffuse = vec3(0.0);
    vec3 indirectSpecular = vec3(0.0);
    
    if (DDGI_ENABLED && ddgi.probeCount.w > 0) {
        // Sample DDGI for indirect diffuse
        indirectDiffuse = sampleDDGI(fragPos, N);
        
        // Apply diffuse BRDF (energy conservation)
        float NdotV = max(dot(N, V), 0.0);
        vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        indirectDiffuse *= kD * albedo;
        
        // === SSR for specular reflections ===
        if (SSR_ENABLED) {
            // Sample SSR result
            vec4 ssrData = texture(ssrReflections, fragTexCoord);
            float ssrConfidence = ssrData.a;
            
            // Blend SSR with environment map fallback
            vec3 prefilteredColor = textureLod(environmentMap, R, roughness * MAX_REFLECTION_LOD).rgb;
            vec2 envBRDF = IntegrateBRDF_Approx(NdotV, roughness);
            vec3 envSpecular = prefilteredColor * (F * envBRDF.x + envBRDF.y);
            
            // SSR provides better local reflections, blend based on confidence
            vec3 ssrSpecular = ssrData.rgb;
            indirectSpecular = mix(envSpecular, ssrSpecular, ssrConfidence);
        } else {
            // Fallback to pure IBL for specular
            vec3 prefilteredColor = textureLod(environmentMap, R, roughness * MAX_REFLECTION_LOD).rgb;
            vec2 envBRDF = IntegrateBRDF_Approx(NdotV, roughness);
            indirectSpecular = prefilteredColor * (F * envBRDF.x + envBRDF.y);
        }
        
        ambient = (indirectDiffuse + indirectSpecular) * ao;
    } else {
        // Fallback to pure IBL (with optional SSR)
        float NdotV = max(dot(N, V), 0.0);
        vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
        
        if (SSR_ENABLED) {
            vec4 ssrData = texture(ssrReflections, fragTexCoord);
            float ssrConfidence = ssrData.a;
            
            vec3 prefilteredColor = textureLod(environmentMap, R, roughness * MAX_REFLECTION_LOD).rgb;
            vec2 envBRDF = IntegrateBRDF_Approx(NdotV, roughness);
            vec3 envSpecular = prefilteredColor * (F * envBRDF.x + envBRDF.y);
            
            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
            vec3 irradiance = textureLod(environmentMap, N, MAX_REFLECTION_LOD).rgb;
            indirectDiffuse = kD * irradiance * albedo;
            
            vec3 ssrSpecular = ssrData.rgb;
            indirectSpecular = mix(envSpecular, ssrSpecular, ssrConfidence);
            
            ambient = (indirectDiffuse + indirectSpecular) * ao * 0.5;
        } else {
            ambient = calculateIBL(N, V, R, albedo, metallic, roughness, F0, ao);
            ambient *= 0.5;
        }
    }
    
    // ========================================================================
    // CASCADED SHADOW MAPPING
    // ========================================================================
    float shadow = ShadowCalculationCSM(fragPos, N, mainLightDir, viewDepth);
    
    Lo *= (1.0 - shadow * 0.9);
    ambient *= (1.0 - shadow * 0.2);
    
    // ========================================================================
    // FINAL COMPOSITION
    // ========================================================================
    vec3 color = ambient + Lo;
    
    // Exposure
    color *= 1.2;
    
    // Tone mapping
    color = ACESFitted(color);
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
    
    // Debug: Output raw albedo
    // outColor = vec4(albedo, 1.0);
}
