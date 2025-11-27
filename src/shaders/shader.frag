#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 lightPos;
    vec4 viewPos;
    vec4 lightColor;
    mat4 lightSpaceMatrix;
} ubo;

// Texture bindings - using existing workflow, interpreted for PBR
// diffuseSampler = Albedo (base color)
// specularSampler = interpreted as Roughness (inverted) + Metallic hint
// normalSampler = Normal map
layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2D roughnessMap;  // Will invert specular for roughness
layout(binding = 3) uniform sampler2D normalMap;
layout(binding = 4) uniform sampler2D shadowMap;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec4 fragPosLightSpace;

layout(location = 0) out vec4 outColor;

// ============================================================================
// PBR CONSTANTS
// ============================================================================
const float PI = 3.14159265359;
const float SHADOW_MAP_SIZE = 2048.0;

// ============================================================================
// PBR FUNCTIONS - Cook-Torrance BRDF
// ============================================================================

// Normal Distribution Function (GGX/Trowbridge-Reitz)
// Describes the statistical distribution of microfacet orientations
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
// Describes self-shadowing of microfacets
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / max(denom, 0.0001);
}

// Smith's method for combining geometry shadowing and masking
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Fresnel equation (Schlick approximation)
// Describes how light reflects at different angles
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel with roughness for IBL
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// TONE MAPPING - ACES Filmic
// ============================================================================
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
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}

// ============================================================================
// SHADOW CALCULATION
// ============================================================================
float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * 0.5 + 0.5;
    
    if(projCoords.z > 1.0) {
        return 0.0;
    }
    
    float currentDepth = projCoords.z;
    
    float cosTheta = max(dot(normal, lightDir), 0.0);
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float baseBias = 0.0005;
    float slopeBias = 0.002 * sinTheta;
    float bias = baseBias + slopeBias;
    
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(SHADOW_MAP_SIZE);
    
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
        vec2 offset = poissonDisk[i] * texelSize * spreadFactor;
        float pcfDepth = texture(shadowMap, projCoords.xy + offset).r;
        shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
    }
    shadow /= 16.0;
    
    return shadow;
}

// ============================================================================
// MAIN
// ============================================================================
void main() {
    // ========================================================================
    // 1. SAMPLE TEXTURES (Linear Space)
    // ========================================================================
    vec3 albedo = pow(texture(albedoMap, fragTexCoord).rgb, vec3(2.2));
    
    // Interpret existing specular map for PBR:
    // - Bright specular = smooth surface = low roughness
    // - Dark specular = rough surface = high roughness
    vec3 specularSample = texture(roughnessMap, fragTexCoord).rgb;
    float specularLuminance = dot(specularSample, vec3(0.299, 0.587, 0.114));
    float roughness = clamp(1.0 - specularLuminance, 0.05, 1.0);
    
    // Metallic: assume non-metallic for most surfaces
    // High specular + low albedo saturation = possibly metallic
    float metallic = 0.0;
    float albedoLuminance = dot(albedo, vec3(0.299, 0.587, 0.114));
    if(specularLuminance > 0.5 && albedoLuminance < 0.3) {
        metallic = 0.8; // Likely metallic surface
    }
    
    // ========================================================================
    // 2. NORMAL MAPPING
    // ========================================================================
    vec3 V = normalize(ubo.viewPos.xyz - fragPos);
    vec3 N = normalize(fragNormal);
    mat3 TBN = cotangent_frame(N, -V, fragTexCoord);
    vec3 mapNormal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
    N = normalize(TBN * mapNormal);
    
    // ========================================================================
    // 3. LIGHT CALCULATION
    // ========================================================================
    vec3 L = normalize(ubo.lightPos.xyz - fragPos);
    vec3 H = normalize(V + L);
    
    // Distance attenuation (for point light behavior)
    float distance = length(ubo.lightPos.xyz - fragPos);
    float attenuation = 1.0 / (1.0 + 0.007 * distance + 0.0002 * distance * distance);
    
    // Light radiance
    vec3 radiance = ubo.lightColor.rgb * attenuation * 3.0; // Boost light intensity for HDR
    
    // ========================================================================
    // 4. PBR - COOK-TORRANCE BRDF
    // ========================================================================
    
    // F0 = surface reflection at zero incidence (looking straight at surface)
    // Dielectrics (non-metals): ~0.04
    // Metals: use albedo color as F0
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Calculate Cook-Torrance BRDF components
    float NDF = DistributionGGX(N, H, roughness);        // Normal Distribution
    float G = GeometrySmith(N, V, L, roughness);          // Geometry
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);     // Fresnel
    
    // Specular BRDF (Cook-Torrance)
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    
    // Energy conservation: specular + diffuse must not exceed 1.0
    vec3 kS = F;                    // Specular contribution
    vec3 kD = vec3(1.0) - kS;       // Diffuse contribution
    kD *= 1.0 - metallic;           // Metals have no diffuse
    
    // Lambertian diffuse
    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;
    
    // ========================================================================
    // 5. AMBIENT LIGHTING (Simple IBL approximation)
    // ========================================================================
    vec3 F_ambient = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kS_ambient = F_ambient;
    vec3 kD_ambient = 1.0 - kS_ambient;
    kD_ambient *= 1.0 - metallic;
    
    // Fake ambient from hemisphere (sky above, ground below)
    vec3 skyColor = vec3(0.5, 0.7, 1.0) * 0.3;
    vec3 groundColor = vec3(0.2, 0.15, 0.1) * 0.1;
    float skyBlend = N.y * 0.5 + 0.5;
    vec3 ambientLight = mix(groundColor, skyColor, skyBlend);
    
    vec3 diffuseAmbient = kD_ambient * albedo * ambientLight;
    vec3 specularAmbient = F_ambient * ambientLight * (1.0 - roughness) * 0.3;
    vec3 ambient = diffuseAmbient + specularAmbient;
    
    // ========================================================================
    // 6. SHADOWS
    // ========================================================================
    vec3 geomNormal = normalize(fragNormal);
    float shadow = ShadowCalculation(fragPosLightSpace, geomNormal, L);
    
    // Contact shadow darkening for ambient
    float contactDarkening = mix(1.0, 0.5, shadow * 0.6);
    ambient *= contactDarkening;
    
    // ========================================================================
    // 7. FINAL COLOR COMPOSITION
    // ========================================================================
    vec3 color = ambient + (1.0 - shadow) * Lo;
    
    // HDR Tone Mapping (ACES Filmic)
    color = ACESFilm(color);
    
    // Gamma correction (linear to sRGB)
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}
