#version 450

// ============================================================================
// COMPOSITION FRAGMENT SHADER - Deferred Lighting Pass
// Reads G-Buffer via input attachments and applies full PBR lighting
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
    // IMAGE-BASED LIGHTING
    // ========================================================================
    vec3 ambient = calculateIBL(N, V, R, albedo, metallic, roughness, F0, ao);
    ambient *= 0.5;
    
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
