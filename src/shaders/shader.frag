#version 450

// ============================================================================
// UNIFORMS AND SAMPLERS
// ============================================================================
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 lightPos;
    vec4 viewPos;
    vec4 lightColor;
    mat4 lightSpaceMatrix;
    // CSM cascade data (4 cascades)
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;      // Far plane distances for each cascade
    vec4 shadowParams;       // x = map size, y = pcf radius, z = bias, w = unused
} ubo;

// Material textures
layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2D metallicRoughnessMap;  // R=unused, G=Roughness, B=Metallic (glTF standard)
layout(binding = 3) uniform sampler2D normalMap;
layout(binding = 4) uniform sampler2D shadowMap;
layout(binding = 5) uniform samplerCube environmentMap;  // Skybox for IBL

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec4 fragPosLightSpace;

layout(location = 0) out vec4 outColor;

// ============================================================================
// CONSTANTS
// ============================================================================
const float PI = 3.14159265359;
const float SHADOW_MAP_SIZE = 2048.0;
const int NUM_LIGHTS = 4;

// Additional point lights (hardcoded for demo - would be in UBO for production)
const vec3 pointLights[NUM_LIGHTS] = vec3[](
    vec3(10.0, 15.0, 10.0),   // Main sun (from UBO)
    vec3(-8.0, 3.0, 5.0),     // Fill light 1
    vec3(5.0, 2.0, -8.0),     // Fill light 2
    vec3(0.0, 8.0, 0.0)       // Top light
);

const vec3 pointLightColors[NUM_LIGHTS] = vec3[](
    vec3(1.0, 0.95, 0.9),     // Warm sunlight
    vec3(0.3, 0.4, 0.6),      // Cool fill
    vec3(0.5, 0.4, 0.3),      // Warm fill
    vec3(0.2, 0.2, 0.3)       // Ambient top
);

const float pointLightIntensities[NUM_LIGHTS] = float[](
    3.0,   // Main sun
    0.5,   // Fill 1
    0.3,   // Fill 2
    0.4    // Top
);

// ============================================================================
// PBR FUNCTIONS - Cook-Torrance BRDF
// ============================================================================

// Normal Distribution Function (GGX/Trowbridge-Reitz)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / max(denom, 0.0001);
}

// Geometry Function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / max(denom, 0.0001);
}

// Smith's method
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Fresnel equation (Schlick approximation)
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel with roughness for IBL
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// TONE MAPPING
// ============================================================================

// ACES Filmic Tone Mapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// ============================================================================
// NORMAL MAPPING
// ============================================================================
mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv) {
    // Get edge vectors of the pixel triangle
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    // Solve the linear system
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // Construct a scale-invariant frame
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}

vec3 perturbNormal(vec3 N, vec3 V, vec2 texcoord) {
    vec3 mapNormal = texture(normalMap, texcoord).rgb;
    
    // Check if normal map is valid (not just flat blue)
    if(abs(mapNormal.z - 1.0) < 0.01 && length(mapNormal.xy) < 0.01) {
        return N;  // No normal map, use geometry normal
    }
    
    mapNormal = mapNormal * 2.0 - 1.0;
    mat3 TBN = cotangent_frame(N, fragPos, texcoord);
    return normalize(TBN * mapNormal);
}

// ============================================================================
// SHADOW CALCULATION (with soft shadows)
// ============================================================================
float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * 0.5 + 0.5;
    
    if(projCoords.z > 1.0) {
        return 0.0;
    }
    
    // Clamp to shadow map bounds
    if(projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    
    float currentDepth = projCoords.z;
    
    // Minimal shader bias - hardware bias handles most of it
    float cosTheta = max(dot(normal, lightDir), 0.0);
    float bias = max(0.0005 * (1.0 - cosTheta), 0.0001);
    
    // Rotated Poisson disk sampling for smoother shadows
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(SHADOW_MAP_SIZE);
    
    // Rotation based on screen position for noise
    float angle = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.28318;
    float s = sin(angle);
    float c = cos(angle);
    mat2 rotation = mat2(c, -s, s, c);
    
    const vec2 poissonDisk[16] = vec2[](
        vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
        vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760),
        vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
        vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
        vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),
        vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
        vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
        vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790)
    );
    
    float spreadFactor = 1.5;
    for(int i = 0; i < 16; ++i) {
        vec2 offset = rotation * poissonDisk[i] * texelSize * spreadFactor;
        float pcfDepth = texture(shadowMap, projCoords.xy + offset).r;
        shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
    }
    shadow /= 16.0;
    
    // Fade shadow at edges of shadow map
    vec2 fade = smoothstep(vec2(0.0), vec2(0.1), projCoords.xy) * 
                (1.0 - smoothstep(vec2(0.9), vec2(1.0), projCoords.xy));
    shadow *= fade.x * fade.y;
    
    return shadow;
}

// ============================================================================
// PBR LIGHTING CALCULATION
// ============================================================================
vec3 calculatePBRLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, float lightIntensity,
                       vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 H = normalize(V + L);
    
    // Distance attenuation
    float NdotL = max(dot(N, L), 0.0);
    
    // Calculate Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    
    // Specular BRDF
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    vec3 specular = numerator / denominator;
    
    // Energy conservation
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    // Combine
    vec3 radiance = lightColor * lightIntensity;
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// ============================================================================
// IMAGE-BASED LIGHTING (Real Skybox Reflections)
// ============================================================================
vec3 calculateIBL(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0) {
    // Reflection vector in world space
    vec3 R = reflect(-V, N);
    
    float NdotV = max(dot(N, V), 0.0);
    
    // For rough materials, almost no specular reflection
    float MAX_REFLECTION_LOD = 4.0;
    float mipLevel = roughness * MAX_REFLECTION_LOD;
    
    // Sample environment in reflection direction
    vec3 prefilteredColor = textureLod(environmentMap, R, mipLevel).rgb;
    
    // Calculate reflection strength based on material type
    vec3 specularIBL;
    if(metallic > 0.5) {
        // MIRROR/METAL: Strong, consistent reflection from all angles
        // No Fresnel variation - mirrors reflect the same from all angles
        float mirrorStrength = (1.0 - roughness) * 0.8;  // 80% max for smooth mirror
        specularIBL = prefilteredColor * albedo * mirrorStrength;  // Tinted by albedo for metals
    } else {
        // DIELECTRIC (non-metal): Very subtle reflection, mostly at grazing angles
        vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
        float roughness4 = roughness * roughness * roughness * roughness;
        float specularStrength = (1.0 - roughness4) * 0.02;  // Max 2% reflection
        specularIBL = prefilteredColor * F * specularStrength;
    }
    
    // Diffuse IBL - subtle ambient light from environment
    vec3 irradiance = textureLod(environmentMap, N, MAX_REFLECTION_LOD).rgb;
    float kD = (1.0 - metallic);  // Metals have no diffuse
    vec3 diffuseIBL = kD * albedo * irradiance * 0.12;
    
    return diffuseIBL + specularIBL;
}

// ============================================================================
// MAIN
// ============================================================================
void main() {
    // ========================================================================
    // 1. SAMPLE MATERIAL TEXTURES
    // ========================================================================
    vec3 albedo = pow(texture(albedoMap, fragTexCoord).rgb, vec3(2.2));
    
    // Sample metallic-roughness map (glTF format: G=Roughness, B=Metallic)
    vec4 mrSample = texture(metallicRoughnessMap, fragTexCoord);
    
    // Interpret legacy specular maps OR proper metallic-roughness
    float roughness, metallic;
    float luminance = dot(mrSample.rgb, vec3(0.299, 0.587, 0.114));
    float albedoLuminance = dot(albedo, vec3(0.299, 0.587, 0.114));
    
    // Detect mirror material: very bright albedo AND very bright specular
    bool isMirror = (albedoLuminance > 0.7 && luminance > 0.7);
    
    if(isMirror) {
        // Mirror: smooth and metallic for perfect reflections
        roughness = 0.05;  // Nearly perfect mirror
        metallic = 1.0;    // Fully metallic = colored reflections
    } else {
        // For legacy specular maps, these are NOT PBR materials
        // Force very high roughness (matte look) and no metallic
        roughness = 0.9;  // Almost completely matte
        metallic = 0.0;   // Non-metallic
    }
    
    // ========================================================================
    // 2. NORMAL MAPPING
    // ========================================================================
    vec3 V = normalize(ubo.viewPos.xyz - fragPos);
    vec3 geomNormal = normalize(fragNormal);
    vec3 N = perturbNormal(geomNormal, V, fragTexCoord);
    
    // ========================================================================
    // 3. CALCULATE F0 (Surface Reflection at Normal Incidence)
    // ========================================================================
    vec3 F0 = vec3(0.04);  // Dielectric base
    F0 = mix(F0, albedo, metallic);  // Metals use albedo as F0
    
    // ========================================================================
    // 4. DIRECT LIGHTING (Single Main Sun - Directional)
    // ========================================================================
    vec3 Lo = vec3(0.0);
    
    // Main directional sun light
    // ubo.lightPos.w == 0 means directional, use as direction vector
    vec3 mainLightDir;
    if(ubo.lightPos.w < 0.5) {
        // Directional light - lightPos IS the direction (normalized)
        mainLightDir = normalize(ubo.lightPos.xyz);
    } else {
        // Point light - compute direction from position
        mainLightDir = normalize(ubo.lightPos.xyz - fragPos);
    }
    
    Lo += calculatePBRLight(N, V, mainLightDir, ubo.lightColor.rgb, 2.0,
                            albedo, metallic, roughness, F0);
    
    // ========================================================================
    // 5. IMAGE-BASED LIGHTING (Real Skybox Reflections)
    // ========================================================================
    vec3 ambient = calculateIBL(N, V, albedo, metallic, roughness, F0);
    
    // ========================================================================
    // 6. SHADOWS (Main Light Only)
    // ========================================================================
    float shadow = ShadowCalculation(fragPosLightSpace, geomNormal, mainLightDir);
    
    // Apply shadow - simply darken the direct lighting
    Lo *= (1.0 - shadow * 0.85);  // Keep 15% light in shadows
    
    // Slight ambient reduction in shadows
    ambient *= (1.0 - shadow * 0.3);
    
    // ========================================================================
    // 7. FINAL COMPOSITION
    // ========================================================================
    vec3 color = ambient + Lo;
    
    // Exposure (simulated)
    color *= 1.0;  // Adjust for scene brightness
    
    // HDR Tone Mapping (ACES)
    color = ACESFilm(color);
    
    // Gamma Correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}
