#version 450

// ============================================================================
// G-BUFFER FRAGMENT SHADER - Deferred Rendering Geometry Pass
// Outputs surface properties to Multiple Render Targets (MRT)
// ============================================================================

// Material textures (matching main descriptor layout)
layout(binding = 1) uniform sampler2D albedoMap;        // diffuse texture
layout(binding = 2) uniform sampler2D metallicRoughnessMap;  // specular texture (R=AO, G=Roughness, B=Metallic)
layout(binding = 3) uniform sampler2D normalMap;        // normal map

// Inputs from vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec4 fragPosLightSpace;
layout(location = 5) in float fragViewDepth;

// G-Buffer Outputs (Multiple Render Targets)
layout(location = 0) out vec4 outPosition;   // RGB: World Position, A: View Depth
layout(location = 1) out vec4 outNormal;     // RGB: World Normal (encoded), A: unused
layout(location = 2) out vec4 outAlbedo;     // RGB: Albedo, A: Metallic
layout(location = 3) out vec4 outPBR;        // R: Roughness, G: AO, B: unused, A: unused

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

vec3 perturbNormal(vec3 N, vec3 V, vec2 texcoord) {
    vec3 mapNormal = texture(normalMap, texcoord).rgb;
    
    // Check if normal map is valid (not just flat blue)
    if (abs(mapNormal.z - 1.0) < 0.01 && length(mapNormal.xy) < 0.01) {
        return N;
    }
    
    mapNormal = mapNormal * 2.0 - 1.0;
    mat3 TBN = cotangent_frame(N, fragPos, texcoord);
    return normalize(TBN * mapNormal);
}

// ============================================================================
// MAIN - Write to G-Buffer
// ============================================================================
void main() {
    // Sample albedo with gamma correction (sRGB to linear)
    vec3 albedo = pow(texture(albedoMap, fragTexCoord).rgb, vec3(2.2));
    // vec3 albedo = fragColor;
    // vec3 albedo = vec3(1.0, 0.0, 0.0); // Hardcoded RED
    
    // Sample metallic-roughness map (glTF: R=AO, G=Roughness, B=Metallic)
    vec4 mrSample = texture(metallicRoughnessMap, fragTexCoord);
    
    float ao = mrSample.r;
    float roughness = mrSample.g;
    float metallic = mrSample.b;
    
    // Clamp roughness to avoid issues
    roughness = clamp(roughness, 0.04, 1.0);
    
    // Legacy texture detection
    float luminance = dot(mrSample.rgb, vec3(0.299, 0.587, 0.114));
    float albedoLuminance = dot(albedo, vec3(0.299, 0.587, 0.114));
    bool isLegacySpecular = (abs(mrSample.r - mrSample.g) < 0.1 && abs(mrSample.g - mrSample.b) < 0.1);
    bool isMirror = (albedoLuminance > 0.7 && luminance > 0.7);
    
    if (isMirror) {
        roughness = 0.05;
        metallic = 1.0;
        ao = 1.0;
    } else if (isLegacySpecular) {
        roughness = 1.0 - luminance * 0.8;
        metallic = luminance > 0.9 ? 0.5 : 0.0;
        ao = 1.0;
    }
    
    if (ao < 0.01) ao = 1.0;
    
    // Calculate view direction for normal perturbation
    // Note: We'd need viewPos from UBO, but for G-Buffer we use geometric approach
    vec3 V = normalize(-fragPos); // Approximate - proper would use camera position
    
    // Apply normal mapping
    vec3 geomNormal = normalize(fragNormal);
    vec3 N = perturbNormal(geomNormal, V, fragTexCoord);
    
    // ========================================================================
    // OUTPUT TO G-BUFFER
    // ========================================================================
    
    // Position: World position + view depth for CSM
    outPosition = vec4(fragPos, fragViewDepth);
    
    // Normal: World-space normal (already normalized)
    // Could use octahedron encoding for better precision in 2 channels
    outNormal = vec4(N * 0.5 + 0.5, 1.0);  // Pack to [0,1] for UNORM format
    
    // Albedo + Metallic
    outAlbedo = vec4(albedo, metallic);
    
    // PBR parameters
    outPBR = vec4(roughness, ao, 0.0, 1.0);
}
