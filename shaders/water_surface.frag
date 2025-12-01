/**
 * water_surface.frag
 * 
 * Water surface fragment shader with physically-based rendering.
 * 
 * Features:
 * - Screen-space reflections/planar reflections
 * - Refraction with chromatic aberration
 * - Water fog/absorption
 * - Caustics
 * - Foam rendering
 * - Subsurface scattering approximation
 */

#version 450
#extension GL_GOOGLE_include_directive : enable

// Input
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec2 inTexCoord;
layout(location = 5) in vec4 inClipPos;
layout(location = 6) in vec4 inPrevClipPos;
layout(location = 7) in float inFoam;

// Output
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outMotionVector;

// Textures
layout(set = 0, binding = 0) uniform sampler2D sceneColor;       // Background scene
layout(set = 0, binding = 1) uniform sampler2D sceneDepth;       // Depth buffer
layout(set = 0, binding = 2) uniform sampler2D normalMap;        // Detail normal map
layout(set = 0, binding = 3) uniform samplerCube envCubemap;     // Environment reflection
layout(set = 0, binding = 4) uniform sampler2D foamTexture;      // Foam texture
layout(set = 0, binding = 5) uniform sampler2D causticsTexture;  // Caustics pattern

// Uniforms
layout(set = 1, binding = 0) uniform WaterParams {
    vec3 cameraPos;
    float time;
    
    vec3 shallowColor;           // Water color in shallow areas
    float shallowDepth;
    
    vec3 deepColor;              // Water color in deep areas
    float deepDepth;
    
    vec3 scatterColor;           // Subsurface scatter color
    float scatterStrength;
    
    float refractionStrength;
    float fresnelPower;
    float normalStrength;
    float foamScale;
    
    vec3 sunDirection;
    float sunIntensity;
    
    vec3 sunColor;
    float specularPower;
    
    vec2 screenSize;
    float causticsStrength;
    float causticsScale;
    
    float absorptionR;           // Color absorption coefficients
    float absorptionG;
    float absorptionB;
    float maxDepth;
} water;

const float PI = 3.14159265359;

// Linearize depth
float linearizeDepth(float depth, float near, float far) {
    return near * far / (far - depth * (far - near));
}

// Fresnel (Schlick approximation)
float fresnel(float cosTheta, float f0) {
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, water.fresnelPower);
}

// Sample detail normal map with animation
vec3 sampleNormalMap(vec2 uv) {
    // Two scrolling normal map layers
    vec2 uv1 = uv * 4.0 + vec2(water.time * 0.02, water.time * 0.01);
    vec2 uv2 = uv * 2.0 - vec2(water.time * 0.015, water.time * 0.025);
    
    vec3 n1 = texture(normalMap, uv1).rgb * 2.0 - 1.0;
    vec3 n2 = texture(normalMap, uv2).rgb * 2.0 - 1.0;
    
    // Blend normals (Reoriented Normal Mapping)
    vec3 t = n1 + vec3(0, 0, 1);
    vec3 u = n2 * vec3(-1, -1, 1);
    vec3 n = t * dot(t, u) / t.z - u;
    
    return normalize(n);
}

// Screen-space refraction
vec3 getRefraction(vec2 screenUV, vec3 normal, float waterDepth) {
    // Offset UV based on normal
    vec2 offset = normal.xz * water.refractionStrength / max(waterDepth, 1.0);
    
    // Chromatic aberration
    vec3 refraction;
    refraction.r = texture(sceneColor, screenUV + offset * 1.0).r;
    refraction.g = texture(sceneColor, screenUV + offset * 1.1).g;
    refraction.b = texture(sceneColor, screenUV + offset * 1.2).b;
    
    return refraction;
}

// Water fog (absorption)
vec3 applyWaterFog(vec3 color, float depth) {
    // Beer-Lambert absorption
    vec3 absorption = vec3(water.absorptionR, water.absorptionG, water.absorptionB);
    vec3 transmittance = exp(-absorption * depth);
    
    // Depth-based color
    float depthFactor = clamp(depth / water.deepDepth, 0.0, 1.0);
    vec3 waterColor = mix(water.shallowColor, water.deepColor, depthFactor);
    
    return mix(waterColor, color * transmittance, transmittance);
}

// Sample caustics
vec3 sampleCaustics(vec3 worldPos, float depth) {
    // Project caustics pattern
    vec2 causticsUV = worldPos.xz * water.causticsScale;
    
    // Animate with time
    vec2 uv1 = causticsUV + vec2(water.time * 0.05, water.time * 0.03);
    vec2 uv2 = causticsUV * 0.8 - vec2(water.time * 0.04, water.time * 0.02);
    
    float c1 = texture(causticsTexture, uv1).r;
    float c2 = texture(causticsTexture, uv2).r;
    
    // Multiply patterns for more interesting result
    float caustics = min(c1, c2);
    caustics = pow(caustics, 2.0);
    
    // Fade with depth
    float causticsFade = exp(-depth * 0.1);
    
    return vec3(caustics * water.causticsStrength * causticsFade);
}

void main() {
    // View direction
    vec3 viewDir = normalize(water.cameraPos - inWorldPos);
    vec3 sunDir = normalize(water.sunDirection);
    
    // Build TBN matrix
    mat3 TBN = mat3(inTangent, inBitangent, inNormal);
    
    // Sample and apply detail normal
    vec3 detailNormal = sampleNormalMap(inTexCoord);
    detailNormal.xy *= water.normalStrength;
    vec3 worldNormal = normalize(TBN * detailNormal);
    
    // Screen UV
    vec2 screenUV = (inClipPos.xy / inClipPos.w) * 0.5 + 0.5;
    
    // Sample scene depth
    float sceneDepthSample = texture(sceneDepth, screenUV).r;
    float sceneZ = linearizeDepth(sceneDepthSample, 0.1, 10000.0);
    float waterZ = linearizeDepth(gl_FragCoord.z, 0.1, 10000.0);
    float waterDepth = max(0.0, sceneZ - waterZ);
    
    // Fresnel
    float NdotV = max(0.0, dot(worldNormal, viewDir));
    float fresnelValue = fresnel(NdotV, 0.02);  // Water F0 ≈ 0.02
    
    // Reflection
    vec3 reflectDir = reflect(-viewDir, worldNormal);
    vec3 reflection = texture(envCubemap, reflectDir).rgb;
    
    // Sun specular highlight
    vec3 halfDir = normalize(viewDir + sunDir);
    float NdotH = max(0.0, dot(worldNormal, halfDir));
    float specular = pow(NdotH, water.specularPower);
    vec3 sunSpec = water.sunColor * water.sunIntensity * specular;
    
    // Refraction
    vec3 refraction = getRefraction(screenUV, worldNormal, waterDepth);
    
    // Apply water fog to refraction
    refraction = applyWaterFog(refraction, waterDepth);
    
    // Add caustics to refraction
    vec3 caustics = sampleCaustics(inWorldPos, waterDepth);
    refraction += caustics * water.sunColor;
    
    // Subsurface scattering approximation
    float sss = pow(max(0.0, dot(-viewDir, sunDir)), 4.0) * water.scatterStrength;
    vec3 scatter = water.scatterColor * sss;
    
    // Combine reflection and refraction
    vec3 color = mix(refraction + scatter, reflection, fresnelValue);
    
    // Add sun specular
    color += sunSpec;
    
    // Foam
    if (inFoam > 0.01) {
        vec2 foamUV = inTexCoord * water.foamScale;
        vec2 foamUV2 = foamUV + vec2(water.time * 0.1, 0.0);
        
        float foam1 = texture(foamTexture, foamUV).r;
        float foam2 = texture(foamTexture, foamUV2 * 0.8).r;
        float foam = max(foam1, foam2);
        
        // Edge foam from depth
        float edgeFoam = 1.0 - smoothstep(0.0, 0.5, waterDepth);
        foam = max(foam * inFoam, edgeFoam * 0.5);
        
        // Blend foam
        vec3 foamColor = vec3(1.0);
        color = mix(color, foamColor, foam * 0.8);
    }
    
    // Distance fog fade (for very distant water)
    float dist = length(water.cameraPos - inWorldPos);
    float fogFactor = 1.0 - exp(-dist * 0.0001);
    
    outColor = vec4(color, 1.0);
    
    // Motion vectors
    vec2 currentScreenPos = screenUV;
    vec2 prevScreenPos = (inPrevClipPos.xy / inPrevClipPos.w) * 0.5 + 0.5;
    outMotionVector = currentScreenPos - prevScreenPos;
}
