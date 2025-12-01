/**
 * sky_atmosphere.frag
 * 
 * Physically-based sky atmosphere rendering.
 * Samples pre-computed LUTs for efficient real-time rendering.
 * 
 * Features:
 * - Rayleigh and Mie scattering
 * - Multi-scattering approximation
 * - Sun disk with limb darkening
 * - Aerial perspective (for distant objects)
 * - Time-of-day support
 */

#version 450
#extension GL_GOOGLE_include_directive : enable

// Input
layout(location = 0) in vec2 inUV;

// Output
layout(location = 0) out vec4 outColor;

// Pre-computed LUTs
layout(set = 0, binding = 0) uniform sampler2D transmittanceLUT;
layout(set = 0, binding = 1) uniform sampler3D scatteringLUT;
layout(set = 0, binding = 2) uniform sampler2D multiScatteringLUT;

// Atmosphere parameters
layout(set = 0, binding = 3) uniform AtmosphereParams {
    vec3 rayleighScattering;
    float rayleighScaleHeight;
    vec3 mieScattering;
    float mieScaleHeight;
    vec3 mieAbsorption;
    float mieAnisotropy;
    vec3 ozoneAbsorption;
    float ozoneLayerHeight;
    float ozoneLayerWidth;
    float earthRadius;
    float atmosphereHeight;
    float sunAngularRadius;
    vec3 groundAlbedo;
    float padding;
} atmo;

// Scene uniforms
layout(set = 0, binding = 4) uniform SceneUniforms {
    mat4 invViewProjection;
    vec3 cameraPosition;
    float cameraAltitude;
    vec3 sunDirection;
    float sunIntensity;
    vec3 sunColor;
    float exposure;
    float time;
    vec3 padding;
} scene;

const float PI = 3.14159265359;

// Reconstruct view ray from screen UV
vec3 getViewRay(vec2 uv) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 worldPos = scene.invViewProjection * clipPos;
    return normalize(worldPos.xyz / worldPos.w - scene.cameraPosition);
}

// Sample transmittance LUT
vec3 sampleTransmittance(float altitude, float cosZenith) {
    float u = (sign(cosZenith) * sqrt(abs(cosZenith)) + 1.0) * 0.5;
    float v = clamp(altitude / atmo.atmosphereHeight, 0.0, 1.0);
    return texture(transmittanceLUT, vec2(u, v)).rgb;
}

// Sample scattering LUT
vec4 sampleScattering(vec3 viewDir, vec3 sunDir) {
    // Convert view and sun directions to LUT coordinates
    float viewZenith = acos(clamp(viewDir.y, -1.0, 1.0));
    float viewAzimuth = atan(viewDir.z, viewDir.x);
    if (viewAzimuth < 0.0) viewAzimuth += 2.0 * PI;
    
    float cosSunZenith = sunDir.y;
    
    vec3 uvw;
    uvw.x = viewZenith / PI;
    uvw.y = viewAzimuth / (2.0 * PI);
    uvw.z = (cosSunZenith + 1.0) * 0.5;
    
    return texture(scatteringLUT, uvw);
}

// Sample multi-scattering LUT
vec3 sampleMultiScattering(float altitude, float cosSunZenith) {
    float u = (cosSunZenith + 1.0) * 0.5;
    float v = clamp(altitude / atmo.atmosphereHeight, 0.0, 1.0);
    return texture(multiScatteringLUT, vec2(u, v)).rgb;
}

// Rayleigh phase function
float rayleighPhase(float cosTheta) {
    return 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
}

// Mie phase function (Henyey-Greenstein)
float miePhase(float cosTheta, float g) {
    float g2 = g * g;
    float num = (1.0 - g2);
    float denom = 4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / denom;
}

// Ray-sphere intersection
float raySphereIntersect(vec3 origin, vec3 dir, float radius) {
    float a = dot(dir, dir);
    float b = 2.0 * dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - 4.0 * a * c;
    
    if (discriminant < 0.0) {
        return -1.0;
    }
    
    return (-b + sqrt(discriminant)) / (2.0 * a);
}

// Get density at height
float getDensity(float altitude, float scaleHeight) {
    return exp(-altitude / scaleHeight);
}

// Sun disk rendering with limb darkening
vec3 getSunDisk(vec3 viewDir, vec3 sunDir) {
    float cosAngle = dot(viewDir, sunDir);
    float angle = acos(cosAngle);
    
    if (angle > atmo.sunAngularRadius * 1.1) {
        return vec3(0.0);
    }
    
    // Limb darkening (simple approximation)
    float t = angle / atmo.sunAngularRadius;
    float limbDarkening = 1.0 - 0.6 * (1.0 - sqrt(max(0.0, 1.0 - t * t)));
    
    // Smooth edge
    float edge = 1.0 - smoothstep(0.9, 1.1, t);
    
    // Get transmittance toward sun
    vec3 transmittance = sampleTransmittance(scene.cameraAltitude, sunDir.y);
    
    return scene.sunColor * scene.sunIntensity * limbDarkening * edge * transmittance;
}

// Main sky color computation
vec3 computeSkyColor(vec3 viewDir) {
    vec3 sunDir = normalize(scene.sunDirection);
    
    // Camera position relative to Earth center
    vec3 earthCenter = vec3(0.0, -atmo.earthRadius, 0.0);
    vec3 rayOrigin = scene.cameraPosition - earthCenter;
    
    float atmosphereRadius = atmo.earthRadius + atmo.atmosphereHeight;
    
    // Check if we hit the atmosphere
    float tAtmo = raySphereIntersect(rayOrigin, viewDir, atmosphereRadius);
    if (tAtmo < 0.0) {
        // Outside atmosphere, just return sun disk
        return getSunDisk(viewDir, sunDir);
    }
    
    // Check if we hit the ground
    float tGround = raySphereIntersect(rayOrigin, viewDir, atmo.earthRadius);
    bool hitGround = tGround > 0.0;
    
    // Sample from LUTs
    vec4 scattering = sampleScattering(viewDir, sunDir);
    
    // Add multi-scattering contribution
    float cosSunZenith = sunDir.y;
    vec3 multiScatter = sampleMultiScattering(scene.cameraAltitude, cosSunZenith);
    
    // Phase functions
    float cosViewSun = dot(viewDir, sunDir);
    float rayleighP = rayleighPhase(cosViewSun);
    float mieP = miePhase(cosViewSun, atmo.mieAnisotropy);
    
    // Combine scattering
    vec3 skyColor = scattering.rgb;
    skyColor += multiScatter * 0.2;  // Multi-scattering contribution
    
    // Apply sun color and intensity
    skyColor *= scene.sunColor * scene.sunIntensity;
    
    // Add sun disk
    vec3 sunDisk = getSunDisk(viewDir, sunDir);
    skyColor += sunDisk;
    
    // Ground contribution (if we hit the ground)
    if (hitGround && viewDir.y < 0.0) {
        vec3 groundPos = rayOrigin + viewDir * tGround;
        vec3 groundNormal = normalize(groundPos);
        float groundNdotL = max(0.0, dot(groundNormal, sunDir));
        
        // Ground transmittance
        vec3 groundTransmittance = sampleTransmittance(0.0, sunDir.y);
        
        // Simple ground color
        vec3 groundColor = atmo.groundAlbedo * groundNdotL * groundTransmittance;
        groundColor *= scene.sunColor * scene.sunIntensity;
        
        // Add aerial perspective
        vec3 pathTransmittance = sampleTransmittance(scene.cameraAltitude, viewDir.y);
        skyColor = mix(groundColor * pathTransmittance, skyColor, 1.0 - pathTransmittance);
    }
    
    return skyColor;
}

// Tone mapping (ACES approximation)
vec3 toneMapACES(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main() {
    // Get view ray direction
    vec3 viewDir = getViewRay(inUV);
    
    // Compute sky color
    vec3 skyColor = computeSkyColor(viewDir);
    
    // Apply exposure
    skyColor *= scene.exposure;
    
    // Tone mapping
    skyColor = toneMapACES(skyColor);
    
    // Gamma correction
    skyColor = pow(skyColor, vec3(1.0 / 2.2));
    
    outColor = vec4(skyColor, 1.0);
}
