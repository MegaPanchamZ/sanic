/**
 * foliage.frag
 * 
 * Foliage fragment shader with subsurface scattering approximation.
 * 
 * Features:
 * - Two-sided rendering with proper normal handling
 * - Subsurface scattering for leaves
 * - Dithered alpha for smooth LOD transitions
 * - Per-type material parameters
 */

#version 450
#extension GL_GOOGLE_include_directive : enable

// Inputs
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in float inFade;
layout(location = 6) in flat uint inTypeId;

// GBuffer outputs
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outPBR;

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
    vec4 interactionPoints[8];
    uint numInteractionPoints;
    vec3 padding;
} ubo;

// Material textures (array per type)
layout(set = 1, binding = 0) uniform sampler2DArray albedoTextures;
layout(set = 1, binding = 1) uniform sampler2DArray normalTextures;
layout(set = 1, binding = 2) uniform sampler2DArray pbrTextures;  // R: roughness, G: metallic, B: AO

// Type material parameters
layout(set = 1, binding = 3) readonly buffer TypeMaterials {
    vec4 materials[];  // x: subsurface, y: alpha cutoff, z: roughness scale, w: normal strength
};

// Dithering pattern for LOD fade
float dither4x4(vec2 position) {
    int x = int(mod(position.x, 4.0));
    int y = int(mod(position.y, 4.0));
    int index = x + y * 4;
    
    float dither[16] = float[16](
        0.0625, 0.5625, 0.1875, 0.6875,
        0.8125, 0.3125, 0.9375, 0.4375,
        0.25,   0.75,   0.125,  0.625,
        1.0,    0.5,    0.875,  0.375
    );
    
    return dither[index];
}

void main() {
    // Get type material parameters
    vec4 matParams = materials[inTypeId];
    float subsurfaceStrength = matParams.x;
    float alphaCutoff = matParams.y;
    float roughnessScale = matParams.z;
    float normalStrength = matParams.w;
    
    // Sample textures
    vec4 albedoSample = texture(albedoTextures, vec3(inTexCoord, float(inTypeId)));
    vec3 normalSample = texture(normalTextures, vec3(inTexCoord, float(inTypeId))).rgb;
    vec3 pbrSample = texture(pbrTextures, vec3(inTexCoord, float(inTypeId))).rgb;
    
    // Alpha test with dithered LOD fade
    float alpha = albedoSample.a;
    float ditherThreshold = dither4x4(gl_FragCoord.xy);
    
    // Combine alpha cutoff with LOD fade
    float effectiveAlpha = alpha * inFade;
    if (effectiveAlpha < alphaCutoff + ditherThreshold * (1.0 - inFade)) {
        discard;
    }
    
    // Handle two-sided rendering
    vec3 normal = inNormal;
    vec3 tangent = inTangent;
    vec3 bitangent = inBitangent;
    
    if (!gl_FrontFacing) {
        normal = -normal;
        tangent = -tangent;
        bitangent = -bitangent;
    }
    
    // Build TBN matrix
    mat3 TBN = mat3(tangent, bitangent, normal);
    
    // Transform normal from tangent to world space
    vec3 tangentNormal = normalSample * 2.0 - 1.0;
    tangentNormal.xy *= normalStrength;
    tangentNormal = normalize(tangentNormal);
    vec3 worldNormal = normalize(TBN * tangentNormal);
    
    // Apply subsurface scattering approximation
    vec3 albedo = albedoSample.rgb;
    
    if (subsurfaceStrength > 0.0) {
        // View direction for subsurface
        vec3 viewDir = normalize(ubo.cameraPos - inWorldPos);
        
        // Approximate subsurface by adding light transmission
        // This is a simplified model - full SSS would need light direction
        float subsurface = max(0.0, -dot(viewDir, worldNormal)) * subsurfaceStrength;
        
        // Tint transmission toward yellow-green (leaf color)
        vec3 subsurfaceColor = albedo * vec3(1.0, 1.2, 0.5);
        albedo = mix(albedo, subsurfaceColor, subsurface * 0.5);
    }
    
    // PBR parameters
    float roughness = pbrSample.r * roughnessScale;
    float metallic = pbrSample.g;
    float ao = pbrSample.b;
    
    // Output to GBuffer
    outAlbedo = vec4(albedo, 1.0);
    outNormal = vec4(worldNormal * 0.5 + 0.5, 1.0);
    outPBR = vec4(roughness, metallic, ao, subsurfaceStrength);  // Store subsurface for deferred pass
}
