/**
 * ViewMode.h
 * 
 * View mode system for rendering debug and visualization modes.
 * Inspired by Unreal Engine's ShowFlags and ViewModeIndex system.
 * 
 * This provides:
 * - EViewMode: High-level view mode presets (Lit, Unlit, Wireframe, etc.)
 * - ShowFlags: Granular boolean flags for individual rendering features
 * - Helper functions to apply view modes to show flags
 */

#pragma once

#include <cstdint>
#include <string>

namespace Sanic {

/**
 * View mode presets - high-level rendering modes
 * These configure ShowFlags for common visualization scenarios
 */
enum class EViewMode : uint8_t {
    // Standard rendering modes
    Lit = 0,                    // Full lit rendering (default)
    Unlit,                      // Unlit - base color/emissive only
    Wireframe,                  // Wireframe overlay
    LitWireframe,               // Lit with wireframe overlay
    
    // Lighting debug modes
    LightingOnly,               // Lighting without materials (white diffuse)
    DetailLighting,             // Lighting with enhanced detail
    LightComplexity,            // Visualize light count per pixel
    
    // G-Buffer visualization
    BaseColor,                  // View base color/albedo
    Metallic,                   // View metallic values
    Roughness,                  // View roughness values
    Specular,                   // View specular values
    Normal,                     // View world-space normals
    WorldNormal,                // View world-space normals (alternative)
    AmbientOcclusion,           // View AO values
    CustomDepth,                // View custom depth
    SceneDepth,                 // View linearized depth
    
    // Material debug
    Reflections,                // View reflections only
    ReflectionOverride,         // All surfaces as perfect mirror
    MaterialAO,                 // View material ambient occlusion
    
    // Geometry visualization
    MeshUVs,                    // UV density / coordinates
    VertexColors,               // Vertex colors
    LODColoration,              // Color by LOD level
    TriangleDensity,            // Triangle density visualization
    
    // Advanced visualization
    Nanite,                     // Nanite cluster/triangle visualization
    VirtualShadowMap,           // VSM visualization
    Lumen,                      // Lumen GI visualization
    DDGI,                       // DDGI probe visualization
    SSR,                        // Screen-space reflections debug
    MotionVectors,              // Motion vector visualization
    
    // Geometry inspection modes  
    Clay,                       // Gray clay material for form evaluation
    FrontBackFace,              // Front (green) / Back (red) face visualization
    
    // Path tracing
    PathTracing,                // Full path tracing mode
    RayTracingDebug,            // Ray tracing debug visualization
    
    // Overdraw / Performance
    ShaderComplexity,           // Shader instruction count
    QuadOverdraw,               // Quad overdraw visualization
    
    Count
};

/**
 * Show flags - granular control over individual rendering features
 * These can be combined to create custom rendering configurations
 */
struct ShowFlags {
    // ==========================================
    // Lighting
    // ==========================================
    bool lighting = true;               // Master lighting toggle
    bool directLighting = true;         // Direct light contribution
    bool globalIllumination = true;     // GI (DDGI, Lumen, etc.)
    bool ambientOcclusion = true;       // Screen-space AO
    bool shadows = true;                // Shadow mapping
    bool reflections = true;            // SSR and reflection probes
    
    // ==========================================
    // Light types
    // ==========================================
    bool directionalLights = true;
    bool pointLights = true;
    bool spotLights = true;
    bool rectLights = true;
    bool skyLight = true;
    
    // ==========================================
    // Lighting features
    // ==========================================
    bool ddgi = true;                   // Dynamic Diffuse GI
    bool lumenGI = true;                // Lumen GI
    bool lumenReflections = true;       // Lumen reflections
    bool screenSpaceReflections = true; // SSR
    bool contactShadows = true;         // Screen-space contact shadows
    bool volumetricFog = true;          // Volumetric fog/lighting
    bool volumetricClouds = true;       // Volumetric clouds
    bool virtualShadowMaps = true;      // VSM
    
    // ==========================================
    // Material / Shading
    // ==========================================
    bool materials = true;              // Use full materials vs debug
    bool normalMaps = true;             // Normal map sampling
    bool materialAO = true;             // Material ambient occlusion
    bool subsurfaceScattering = true;   // SSS
    bool decals = true;                 // Decal rendering
    
    // ==========================================
    // Post Processing
    // ==========================================
    bool postProcessing = true;         // Master post-process toggle
    bool bloom = true;
    bool depthOfField = true;
    bool motionBlur = true;
    bool tonemapping = true;
    bool exposure = true;
    bool colorGrading = true;
    bool vignette = true;
    bool chromaticAberration = true;
    bool filmGrain = true;
    bool antiAliasing = true;
    
    // ==========================================
    // Geometry types
    // ==========================================
    bool staticMeshes = true;
    bool skeletalMeshes = true;
    bool landscape = true;
    bool foliage = true;
    bool particles = true;
    bool translucency = true;
    bool nanite = true;
    
    // ==========================================
    // Debug / Visualization
    // ==========================================
    bool wireframe = false;             // Wireframe overlay
    bool bounds = false;                // Bounding boxes
    bool collision = false;             // Collision geometry
    bool grid = true;                   // Editor grid
    bool gizmos = true;                 // Transform gizmos
    bool icons = true;                  // Billboard icons
    bool selection = true;              // Selection outlines
    
    // ==========================================
    // Visualization overrides
    // ==========================================
    bool overrideBaseColor = false;     // Override with solid color
    bool overrideLighting = false;      // Override lighting mode
    bool overrideRoughness = false;     // Override roughness
    bool overrideMetallic = false;      // Override metallic
    
    // ==========================================
    // Buffer visualization
    // ==========================================
    bool visualizeBuffer = false;       // Show G-Buffer
    uint8_t bufferVisualization = 0;    // Which buffer to show (0=none)
    
    // ==========================================
    // Performance / Debug
    // ==========================================
    bool showStats = false;             // Performance stats overlay
    bool freezeCulling = false;         // Freeze culling for debugging
    bool disableOcclusionCulling = false;
    
    /**
     * Reset all flags to default lit mode
     */
    void reset() {
        *this = ShowFlags{};
    }
    
    /**
     * Disable all advanced features for unlit mode
     */
    void setUnlit() {
        lighting = false;
        directLighting = false;
        globalIllumination = false;
        shadows = false;
        reflections = false;
        ddgi = false;
        lumenGI = false;
        lumenReflections = false;
        screenSpaceReflections = false;
        contactShadows = false;
        ambientOcclusion = false;
    }
    
    /**
     * Disable all material features for lighting-only mode
     */
    void setLightingOnly() {
        materials = false;
        normalMaps = false;
        materialAO = false;
    }
};

/**
 * Apply a view mode preset to show flags
 */
inline void ApplyViewMode(EViewMode viewMode, ShowFlags& flags) {
    // Start with defaults
    flags.reset();
    
    switch (viewMode) {
        case EViewMode::Lit:
            // Default - all features enabled
            break;
            
        case EViewMode::Unlit:
            flags.setUnlit();
            break;
            
        case EViewMode::Wireframe:
            flags.setUnlit();
            flags.wireframe = true;
            flags.materials = false;
            break;
            
        case EViewMode::LitWireframe:
            flags.wireframe = true;
            break;
            
        case EViewMode::LightingOnly:
            flags.setLightingOnly();
            break;
            
        case EViewMode::DetailLighting:
            flags.setLightingOnly();
            flags.overrideLighting = true;
            break;
            
        case EViewMode::LightComplexity:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 10; // Light complexity
            break;
            
        case EViewMode::BaseColor:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 1;
            break;
            
        case EViewMode::Metallic:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 2;
            break;
            
        case EViewMode::Roughness:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 3;
            break;
            
        case EViewMode::Specular:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 4;
            break;
            
        case EViewMode::Normal:
        case EViewMode::WorldNormal:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 5;
            break;
            
        case EViewMode::AmbientOcclusion:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 6;
            break;
            
        case EViewMode::SceneDepth:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 7;
            break;
            
        case EViewMode::Reflections:
            flags.lighting = false;
            flags.directLighting = false;
            flags.globalIllumination = false;
            // Only show reflections
            break;
            
        case EViewMode::ReflectionOverride:
            flags.overrideRoughness = true;
            flags.overrideMetallic = true;
            break;
            
        case EViewMode::VertexColors:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 8;
            break;
            
        case EViewMode::MeshUVs:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 9;
            break;
            
        case EViewMode::LODColoration:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 11;
            break;
            
        case EViewMode::TriangleDensity:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 12;
            break;
            
        case EViewMode::Nanite:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 20;
            break;
            
        case EViewMode::VirtualShadowMap:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 21;
            break;
            
        case EViewMode::Lumen:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 22;
            break;
            
        case EViewMode::DDGI:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 23;
            break;
            
        case EViewMode::SSR:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 24;
            break;
            
        case EViewMode::MotionVectors:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 25;
            break;
            
        case EViewMode::Clay:
            flags.materials = false;
            flags.overrideBaseColor = true;
            flags.normalMaps = false;
            break;
            
        case EViewMode::FrontBackFace:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 30;
            break;
            
        case EViewMode::PathTracing:
            // Path tracing uses separate render path
            flags.postProcessing = false; // PT has its own post-process
            break;
            
        case EViewMode::RayTracingDebug:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 40;
            break;
            
        case EViewMode::ShaderComplexity:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 50;
            break;
            
        case EViewMode::QuadOverdraw:
            flags.setUnlit();
            flags.visualizeBuffer = true;
            flags.bufferVisualization = 51;
            break;
            
        default:
            break;
    }
}

/**
 * Get display name for a view mode
 */
inline const char* GetViewModeName(EViewMode viewMode) {
    switch (viewMode) {
        case EViewMode::Lit:                return "Lit";
        case EViewMode::Unlit:              return "Unlit";
        case EViewMode::Wireframe:          return "Wireframe";
        case EViewMode::LitWireframe:       return "Lit Wireframe";
        case EViewMode::LightingOnly:       return "Lighting Only";
        case EViewMode::DetailLighting:     return "Detail Lighting";
        case EViewMode::LightComplexity:    return "Light Complexity";
        case EViewMode::BaseColor:          return "Base Color";
        case EViewMode::Metallic:           return "Metallic";
        case EViewMode::Roughness:          return "Roughness";
        case EViewMode::Specular:           return "Specular";
        case EViewMode::Normal:             return "Normals";
        case EViewMode::WorldNormal:        return "World Normals";
        case EViewMode::AmbientOcclusion:   return "Ambient Occlusion";
        case EViewMode::CustomDepth:        return "Custom Depth";
        case EViewMode::SceneDepth:         return "Scene Depth";
        case EViewMode::Reflections:        return "Reflections";
        case EViewMode::ReflectionOverride: return "Reflection Override";
        case EViewMode::MaterialAO:         return "Material AO";
        case EViewMode::MeshUVs:            return "Mesh UVs";
        case EViewMode::VertexColors:       return "Vertex Colors";
        case EViewMode::LODColoration:      return "LOD Coloration";
        case EViewMode::TriangleDensity:    return "Triangle Density";
        case EViewMode::Nanite:             return "Nanite Visualization";
        case EViewMode::VirtualShadowMap:   return "Virtual Shadow Map";
        case EViewMode::Lumen:              return "Lumen";
        case EViewMode::DDGI:               return "DDGI";
        case EViewMode::SSR:                return "Screen Space Reflections";
        case EViewMode::MotionVectors:      return "Motion Vectors";
        case EViewMode::Clay:               return "Clay";
        case EViewMode::FrontBackFace:      return "Front/Back Face";
        case EViewMode::PathTracing:        return "Path Tracing";
        case EViewMode::RayTracingDebug:    return "Ray Tracing Debug";
        case EViewMode::ShaderComplexity:   return "Shader Complexity";
        case EViewMode::QuadOverdraw:       return "Quad Overdraw";
        default:                            return "Unknown";
    }
}

/**
 * Get category for a view mode (for menu organization)
 */
inline const char* GetViewModeCategory(EViewMode viewMode) {
    switch (viewMode) {
        case EViewMode::Lit:
        case EViewMode::Unlit:
        case EViewMode::Wireframe:
        case EViewMode::LitWireframe:
            return "Standard";
            
        case EViewMode::LightingOnly:
        case EViewMode::DetailLighting:
        case EViewMode::LightComplexity:
            return "Lighting";
            
        case EViewMode::BaseColor:
        case EViewMode::Metallic:
        case EViewMode::Roughness:
        case EViewMode::Specular:
        case EViewMode::Normal:
        case EViewMode::WorldNormal:
        case EViewMode::AmbientOcclusion:
        case EViewMode::SceneDepth:
        case EViewMode::CustomDepth:
            return "Buffer Visualization";
            
        case EViewMode::Reflections:
        case EViewMode::ReflectionOverride:
        case EViewMode::MaterialAO:
            return "Material";
            
        case EViewMode::MeshUVs:
        case EViewMode::VertexColors:
        case EViewMode::LODColoration:
        case EViewMode::TriangleDensity:
            return "Mesh";
            
        case EViewMode::Nanite:
        case EViewMode::VirtualShadowMap:
        case EViewMode::Lumen:
        case EViewMode::DDGI:
        case EViewMode::SSR:
        case EViewMode::MotionVectors:
            return "Advanced";
            
        case EViewMode::Clay:
        case EViewMode::FrontBackFace:
            return "Geometry Inspection";
            
        case EViewMode::PathTracing:
        case EViewMode::RayTracingDebug:
            return "Ray Tracing";
            
        case EViewMode::ShaderComplexity:
        case EViewMode::QuadOverdraw:
            return "Performance";
            
        default:
            return "Other";
    }
}

} // namespace Sanic
