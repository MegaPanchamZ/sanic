#version 450

// ============================================================================
// STANDARD PBR FRAGMENT SHADER - SanicEngine
// Simplified PBR with basic lighting for compatibility
// ============================================================================

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 lightPos;
    vec4 viewPos;
    vec4 lightColor;
    mat4 lightSpaceMatrix;
} ubo;

layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2D metallicRoughnessMap;
layout(binding = 3) uniform sampler2D normalMap;
layout(binding = 4) uniform sampler2D shadowMap;
layout(binding = 5) uniform samplerCube environmentMap;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec4 fragPosLightSpace;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;
const float INV_PI = 0.31830988618;
const vec3 DIELECTRIC_F0 = vec3(0.04);

// GGX/Trowbridge-Reitz NDF
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return a2 / max(denom, 0.00001);
}

// Schlick-GGX Geometry
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Normal mapping
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

vec3 perturbNormal(vec3 N, vec3 V, vec2 texcoord) {
    vec3 mapNormal = texture(normalMap, texcoord).rgb;
    if (abs(mapNormal.z - 1.0) < 0.01 && length(mapNormal.xy) < 0.01) {
        return N;
    }
    mapNormal = mapNormal * 2.0 - 1.0;
    mat3 TBN = cotangent_frame(N, fragPos, texcoord);
    return normalize(TBN * mapNormal);
}

// Shadow calculation
float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
    if (projCoords.z > 1.0) return 0.0;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;
    
    float currentDepth = projCoords.z;
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    
    // PCF
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

// ACES tone mapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    // Sample textures
    vec4 albedoSample = texture(albedoMap, fragTexCoord);
    vec3 albedo = pow(albedoSample.rgb, vec3(2.2));
    
    vec4 mrSample = texture(metallicRoughnessMap, fragTexCoord);
    float ao = mrSample.r;
    float roughness = clamp(mrSample.g, 0.04, 1.0);
    float metallic = mrSample.b;
    
    // Handle legacy materials
    float luminance = dot(mrSample.rgb, vec3(0.299, 0.587, 0.114));
    bool isLegacy = (abs(mrSample.r - mrSample.g) < 0.1 && abs(mrSample.g - mrSample.b) < 0.1);
    if (isLegacy) {
        roughness = 1.0 - luminance * 0.8;
        metallic = luminance > 0.9 ? 0.5 : 0.0;
        ao = 1.0;
    }
    if (ao < 0.01) ao = 1.0;
    
    // Normal mapping
    vec3 V = normalize(ubo.viewPos.xyz - fragPos);
    vec3 N = perturbNormal(normalize(fragNormal), V, fragTexCoord);
    vec3 R = reflect(-V, N);
    
    // F0
    vec3 F0 = mix(DIELECTRIC_F0, albedo, metallic);
    
    // Direct lighting
    vec3 L = normalize(ubo.lightPos.w < 0.5 ? ubo.lightPos.xyz : ubo.lightPos.xyz - fragPos);
    vec3 H = normalize(V + L);
    
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    
    // Cook-Torrance BRDF
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(VdotH, F0);
    
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo * INV_PI;
    
    vec3 Lo = (diffuse + specular) * ubo.lightColor.rgb * 3.0 * NdotL;
    
    // IBL approximation
    vec3 irradiance = textureLod(environmentMap, N, 4.0).rgb;
    vec3 prefilteredColor = textureLod(environmentMap, R, roughness * 4.0).rgb;
    vec3 Fibl = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kDibl = (vec3(1.0) - Fibl) * (1.0 - metallic);
    vec3 ambient = (kDibl * irradiance * albedo + prefilteredColor * Fibl) * ao * 0.5;
    
    // Shadows
    float shadow = ShadowCalculation(fragPosLightSpace, normalize(fragNormal), L);
    Lo *= (1.0 - shadow * 0.9);
    ambient *= (1.0 - shadow * 0.2);
    
    // Final color
    vec3 color = ambient + Lo;
    color *= 1.2; // exposure
    color = ACESFilm(color);
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, albedoSample.a);
}
