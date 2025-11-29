# Sanic Engine - Nanite/Lumen Implementation Progress

## Recent Fixes (November 2025)

### SSR Hi-Z Implementation Fix
**Problem**: The `ssr.comp` shader claimed to use "hierarchical ray marching" but was actually using linear O(N) ray marching with fixed step sizes.

**Solution**: Complete rewrite of `ssr.comp` with true Hierarchical Z-Buffer (Hi-Z) traversal:
- Uses depth pyramid (`depth_downsample.comp`) for O(log N) ray marching
- Starts at coarsest mip level, drops to finer mips on potential intersections
- Skips empty space by checking conservative depth at each mip level
- Added `hizBuffer` and `velocityBuffer` bindings for temporal stability

### Motion Vector / Velocity Buffer
**Problem**: No motion vector output for temporal effects (TAA, SSR temporal filtering).

**Solution**: Updated G-Buffer pass to output velocity:
- `gbuffer.vert`: Added `prevModel`, `prevView`, `prevProj` uniforms for previous frame transform
- `gbuffer.vert`: Outputs `currentClipPos` and `prevClipPos` to fragment shader
- `gbuffer.frag`: New output `outVelocity` (RG16F) with screen-space motion vectors
- `gbuffer.frag`: Jitter correction for TAA compatibility

### SSRSystem Updates
- Added Hi-Z pyramid binding (binding 9)
- Added velocity buffer binding (binding 10)  
- Added hit UV output for temporal filtering (binding 11)
- New uniforms: `prevViewProj`, `hizMipLevels`, `jitter`, `temporalWeight`
- Backward-compatible legacy `update()` function preserved

### Legacy Shader Cleanup
**Problem**: `shader.frag` was a legacy forward rendering PBR shader redundant in the deferred architecture.

**Solution**: Renamed to `legacy_forward.frag` for archive purposes. Use `gbuffer.frag` + `composition.frag` for deferred rendering.

---

## Phase 1: Core Geometry Pipeline (Turns 1-12) ✅ COMPLETE

### Turn 1-2: Cluster Hierarchy & LOD System
- **ClusterHierarchy.h/cpp**: Nanite-style mesh cluster management
  - `ClusterGeometry`: Per-cluster vertex/index offsets, material ID
  - `ClusterNode`: DAG node with bounds, error metrics, children
  - `LODData`: Level-of-detail with parent/sibling links
  - `ClusterHierarchyBuilder`: meshoptimizer-based clustering
  - GPU-uploadable buffers for cluster data

- **cluster_common.glsl**: Shared GPU structures
  - `ClusterGeometry`: meshletOffset, vertexOffset, triangleOffset, triangleCount
  - `ClusterNode`: boundingSphere, error metrics, child links
  - `InstanceData`: transform, clusterId

### Turn 3: GPU Cluster Culling
- **ClusterCullingPipeline.h/cpp**: Frustum + occlusion culling
  - Visibility buffer output
  - Persistent threads for work distribution
  - Error metric-based LOD selection

- **cluster_cull.comp**: 
  - Frustum culling (6-plane test)
  - Screen-space error evaluation
  - Parent cut traversal for continuous LOD

### Turn 4: Instance Culling & HZB
- **HZBPipeline.h/cpp**: Hierarchical Z-Buffer management
  - Mip-chain generation
  - Occlusion query support

- **instance_cull.comp**: Per-instance frustum + HZB culling
- **hzb_generate.comp**: Depth pyramid generation (min reduction)
- **hzb_cull.comp**: HZB-based occlusion test

### Turn 5-6: Indirect Draw & Mesh Shaders
- **IndirectDrawPipeline.h/cpp**: Indirect rendering setup
  - VkDrawMeshTasksIndirectCommandEXT generation
  - Meshlet-level granularity

- **indirect_build.comp**: Compact visible clusters → draw commands
- **visbuffer.task**: Task shader - workgroup distribution
- **visbuffer.mesh**: Mesh shader - vertex transform, primitive output
- **visbuffer.frag**: Visibility buffer write (clusterID | triangleID | depth)

### Turn 7-9: Software Rasterization & G-Buffer
- **SoftwareRasterizerPipeline.h/cpp**: Hybrid SW/HW rasterization
  - Small triangle threshold (default 32px²)
  - 64-bit atomic depth testing

- **triangle_bin.comp**: Triangle size classification (SW vs HW)
- **sw_rasterize.comp**: Edge-function software rasterizer
  - `GL_EXT_shader_atomic_int64` for atomic depth test
- **visbuffer_resolve.comp**: Visibility → G-Buffer conversion
  - Barycentric interpolation
  - World position, normal, UV computation

### Turn 10: Material System
- **MaterialSystem.h/cpp**: Unified material & deferred shading
  - `GPUMaterial`: 96-byte PBR material struct
  - Bindless texture support (MAX_TEXTURES = 4096)
  - Material binning for coherent evaluation

- **material_bin.comp**: Pixel-to-material binning
- **material_eval.comp**: PBR material evaluation
  - Vertex-based TBN computation (no dFdx in compute)
  - textureGrad for anisotropic filtering
  - `GL_EXT_nonuniform_qualifier` for bindless access

- **deferred_lighting.comp**: PBR deferred lighting
  - Point, directional, spot lights
  - GGX BRDF, Smith geometry, Schlick Fresnel
  - Environment map IBL support

### Turn 11-12: Temporal Stability
- **TemporalSystem.h/cpp**: TAA & motion vector system
  - Halton sequence jitter generation
  - Double-buffered history frames
  - Catmull-Rom bicubic history sampling

- **motion_vectors.comp**: Per-pixel velocity from visibility buffer
  - Current/previous transform interpolation
  - Screen-space motion output

- **temporal_aa.comp**: Temporal anti-aliasing
  - YCoCg color space for better clipping
  - Variance-based neighborhood clamping
  - Velocity-adaptive feedback (0.88-0.97)
  - Optional sharpening pass

---

## Phase 2: Advanced Rendering (Turns 13-24) - NEXT

### Turn 13-15: Screen-Space Reflections (SSR)
- [ ] Hi-Z traced reflections
- [ ] Temporal filtering for SSR
- [ ] Roughness-based blur
- [ ] Reflection resolve with fallback

### Turn 16-18: Ambient Occlusion
- [ ] GTAO (Ground Truth AO) implementation
- [ ] Spatial denoising
- [ ] Temporal accumulation
- [ ] Bent normals for GI integration

### Turn 19-21: Volumetric Effects
- [ ] Volumetric fog/lighting
- [ ] Froxel-based light scattering
- [ ] Temporal reprojection for volumes
- [ ] Cloud raymarching (optional)

### Turn 22-24: Post-Processing
- [ ] Bloom (physically-based)
- [ ] Tone mapping (ACES/Neutral)
- [ ] Chromatic aberration
- [ ] Film grain, vignette

---

## Phase 3: Global Illumination (Turns 25-36)

### Turn 25-27: DDGI (Dynamic Diffuse GI)
- [ ] Probe grid management
- [ ] Ray tracing for probe updates
- [ ] Octahedral encoding for irradiance
- [ ] Temporal blending

### Turn 28-30: Screen-Space GI
- [ ] SSGI raymarching
- [ ] Half-res tracing
- [ ] Denoising & upscaling
- [ ] Blending with DDGI

### Turn 31-33: Lumen-style Features
- [ ] Surface cache for radiance
- [ ] Mesh card generation
- [ ] Software ray tracing fallback
- [ ] Infinite bounces via probes

### Turn 34-36: GI Integration
- [ ] Final gather compositing
- [ ] Sky/environment contribution
- [ ] Emissive material support
- [ ] Debug visualization

---

## Phase 4: Shadows & Polish (Turns 37-50)

### Turn 37-39: Virtual Shadow Maps
- [ ] Clipmap-based VSM
- [ ] Page table management
- [ ] Shadow caching
- [ ] Dynamic/static separation

### Turn 40-42: Ray-Traced Shadows
- [ ] Hardware RT shadows
- [ ] Denoising (SVGF-style)
- [ ] Contact hardening
- [ ] Multi-light support

### Turn 43-45: Performance
- [ ] Async compute optimization
- [ ] Memory aliasing
- [ ] Shader occupancy tuning
- [ ] GPU profiling integration

### Turn 46-48: Integration
- [ ] Render graph implementation
- [ ] Resource barriers optimization
- [ ] Debug overlays
- [ ] Console variables

### Turn 49-50: Final Polish
- [ ] Quality presets
- [ ] Fallback paths
- [ ] Documentation
- [ ] Demo scene

---

## Shader Inventory

| Shader | Type | Purpose |
|--------|------|---------|
| cluster_common.glsl | Include | Shared GPU structures |
| cluster_cull.comp | Compute | Frustum+LOD culling |
| instance_cull.comp | Compute | Per-instance culling |
| hzb_generate.comp | Compute | Depth pyramid |
| hzb_cull.comp | Compute | Occlusion culling |
| indirect_build.comp | Compute | Draw command generation |
| visbuffer.task | Task | Workgroup distribution |
| visbuffer.mesh | Mesh | Vertex transform |
| visbuffer.frag | Fragment | Visibility buffer write |
| triangle_bin.comp | Compute | SW/HW triangle classification |
| sw_rasterize.comp | Compute | Software rasterizer |
| visbuffer_resolve.comp | Compute | G-Buffer generation |
| material_bin.comp | Compute | Pixel-material binning |
| material_eval.comp | Compute | PBR material eval |
| deferred_lighting.comp | Compute | PBR lighting |
| motion_vectors.comp | Compute | Velocity generation |
| temporal_aa.comp | Compute | TAA resolve |
| depth_downsample.comp | Compute | Hi-Z pyramid generation (min reduction) |
| ssr.comp | Compute | Hi-Z screen-space reflections |
| ssr_hierarchical.comp | Compute | Alternative Hi-Z SSR implementation |
| gbuffer.vert | Vertex | G-Buffer geometry pass + velocity |
| gbuffer.frag | Fragment | G-Buffer MRT output + velocity |
| legacy_forward.frag | Fragment | (Archive) Legacy forward PBR shader |

---

## C++ Class Inventory

| Class | Purpose |
|-------|---------|
| ClusterHierarchy | Mesh cluster DAG |
| ClusterCullingPipeline | GPU culling management |
| HZBPipeline | Hierarchical Z-buffer |
| IndirectDrawPipeline | Mesh shader dispatch |
| SoftwareRasterizerPipeline | Hybrid rasterization |
| MaterialSystem | PBR materials & lighting |
| TemporalSystem | TAA & motion vectors |
| SSRSystem | Hi-Z screen-space reflections |

---

## Required Vulkan Extensions

```cpp
// Core
VK_KHR_buffer_device_address
VK_KHR_synchronization2

// Mesh Shaders
VK_EXT_mesh_shader

// Ray Tracing (for future phases)
VK_KHR_acceleration_structure
VK_KHR_ray_tracing_pipeline
VK_KHR_deferred_host_operations

// Memory
VK_EXT_memory_budget

// Shader Features
GL_EXT_shader_atomic_int64
GL_EXT_nonuniform_qualifier
GL_EXT_buffer_reference
GL_EXT_shader_explicit_arithmetic_types_int64
```

---

## Build Status

✅ All shaders compile with `glslangValidator -V --target-env vulkan1.3`
✅ All C++ classes build successfully with Ninja/CMake
✅ Engine links against: Vulkan, GLFW, GLM, Jolt Physics, meshoptimizer
