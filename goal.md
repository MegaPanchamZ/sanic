# Sanic Engine: Production Readiness & Feature Roadmap
**Project Target:** *Kinetic Edge* (High-Speed Open World Action RPG)
**Reference Architecture:** Unreal Engine 5.7 (Source - November 2025)

---

## Executive Summary
This document outlines the engineering roadmap required to elevate **Sanic Engine** from a high-fidelity renderer to a production-ready Game Engine capable of shipping a AAA title. The focus is on bridging the gap between raw rendering power (Nanite/Lumen) and gameplay utility (Physics, Animation, World Building), ensuring C# scripting can drive high-performance native systems.

**Reference Update:** With UE 5.7 releasing November 12, 2025, we now have access to cutting-edge features including **MegaLights**, **Procedural Vegetation Editor**, **GPU-accelerated PCG**, and **production-ready Substrate materials**. These should inform our roadmap priorities.

### Game Vision: *Kinetic Edge*
A Sonic Frontiers-inspired open world action RPG featuring:
- **Diverse Character Abilities:** Ziplining, super jumping, boosting, flamethrowers, sword slashing
- **Hybrid World Structure:** Open world exploration zones + linear high-speed stages
- **RPG Progression:** Investigation, leveling, mini-bosses, and major boss encounters
- **Extreme Speed:** 700+ mph traversal with physics that *feel* right

---

## Part 0: Current Engine Strengths (What We Have)
*Before addressing gaps, acknowledge the solid foundation.*

| Subsystem | Status | Notes |
|-----------|--------|-------|
| **Nanite-Style Geometry** | ✅ Implemented | Cluster culling, visibility buffer, GPU-driven pipeline |
| **Lumen-Style GI** | ✅ Implemented | Screen probes, radiance cache, SDF tracing |
| **Virtual Shadow Maps** | ✅ Implemented | 16K virtual resolution, clipmaps, page streaming |
| **Ray Tracing** | ✅ Implemented | RT shadows, reflections, DDGI |
| **Material System** | ✅ Implemented | PBR, bindless textures, multiple shading models |
| **ECS Architecture** | ✅ Implemented | Archetype storage, queries, events |
| **Physics (Jolt)** | ✅ Integrated | Rigid bodies, collision detection |
| **C# Scripting** | ✅ Implemented | .NET 8, hot reload, native interop |
| **Skeletal Animation** | ✅ Implemented | Blend trees, state machines, GPU skinning |
| **Level Streaming** | ✅ Implemented | World partition style, HLOD, async loading |
| **Audio System** | ✅ Implemented | 3D spatial, HRTF, reverb zones |

---

## Part 0.5: UE 5.7 Features to Adopt
*New in November 2025 - prioritize these for competitive parity.*

### 🔴 HIGH PRIORITY (Significant Competitive Advantage)

#### MegaLights ("Nanite for Lights")
*   **What It Is:** Allows hundreds of dynamic shadow-casting lights with minimal performance cost.
*   **Why We Need It:** Open world + linear stages need rich lighting without baking.
*   **UE5.7 Reference:** `Engine/Source/Runtime/Renderer/Private/MegaLights/`
*   **Implementation:**
    *   Virtual shadow map tiling per light
    *   Light clustering and culling (similar to cluster culling pipeline)
    *   Importance sampling for shadow rays
    *   Now supports directional lights, translucency, and hair

#### Substrate Materials (Production Ready)
*   **What It Is:** Layered material system replacing classic shading models.
*   **Why We Need It:** Complex materials (dust over metal, wet surfaces, carbon fiber) with physical accuracy.
*   **UE5.7 Reference:** `Engine/Source/Runtime/Engine/Private/Materials/Substrate/`
*   **Implementation:**
    *   Material "slabs" that stack (base + clear coat + dust + scratches)
    *   Per-layer roughness, IOR, absorption
    *   Energy-conserving multi-layer BSDF
    *   Modular artist workflow

#### Nanite Skinned Foliage
*   **What It Is:** Nanite geometry with vertex animation support.
*   **Why We Need It:** Trees/foliage with wind animation while keeping billions of polygons.
*   **UE5.7 Reference:** `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteSkinning.cpp`
*   **Implementation:**
    *   Bone-driven deformation in cluster space
    *   Wind animation via world position offset
    *   "Pixel Programmable Distance" for LOD optimization

### 🟡 MEDIUM PRIORITY (Valuable for World Building)

#### Procedural Vegetation Editor (PVE)
*   **What It Is:** Graph-based tool for authoring Nanite-optimized trees/bushes in-engine.
*   **Why We Need It:** Eliminates SpeedTree dependency for vegetation.
*   **UE5.7 Reference:** `Engine/Source/Editor/ProceduralVegetation/`
*   **Implementation:**
    *   L-System graph nodes for trunk/branch generation
    *   Leaf density and distribution rules
    *   Auto-LOD and Nanite cluster generation
    *   Physics collision mesh generation

#### PCG GPU Compute
*   **What It Is:** Procedural Content Generation running on GPU.
*   **Why We Need It:** Real-time dense biome generation for open world.
*   **UE5.7 Reference:** `Engine/Source/Runtime/PCG/Private/GPU/`
*   **Implementation:**
    *   Compute shader scatter/placement
    *   GPU-side rule evaluation
    *   Streaming-friendly chunk generation

#### Heterogeneous Volumes (Improved)
*   **What It Is:** Better smoke/fire/fluid rendering integrated with Lumen.
*   **Why We Need It:** Speed effects, explosions, boss attacks need volumetric VFX.
*   **UE5.7 Reference:** `Engine/Source/Runtime/Renderer/Private/HeterogeneousVolumes/`
*   **Implementation:**
    *   Volumetric shadows from/onto volumes
    *   Lumen GI contribution from emissive volumes
    *   Temporal reprojection for stability

### 🟢 NICE TO HAVE (Polish Features)

#### Motion Matching + Choosers
*   **What It Is:** Logic selector for animation with deep Motion Matching integration.
*   **Why We Need It:** Fluid character movement based on input trajectory.
*   **UE5.7 Reference:** `Engine/Source/Runtime/AnimGraphRuntime/Private/AnimNodes/AnimNode_MotionMatching.cpp`

#### Skeletal Mesh Sculpting
*   **What It Is:** Sculpt blend shapes directly on skeletal meshes in-editor.
*   **Why We Need It:** Fix deformation issues without Maya/Blender round-trip.
*   **UE5.7 Reference:** `Engine/Source/Editor/SkeletalMeshEditor/`

#### Control Rig Dependency View
*   **What It Is:** Graph visualization of rig data flow for debugging.
*   **Why We Need It:** Debug complex IK setups faster.
*   **UE5.7 Reference:** `Engine/Source/Editor/ControlRigEditor/Private/Graph/`

---

## Part 1: Core Architecture & RHI (The Foundation)
*Goal: Eliminate frame stutters and ensure stability on varied hardware.*

### 1.1 Render Dependency Graph (RDG)
*   **The Gap:** Current manual barrier management is error-prone and scales poorly.
*   **Priority:** 🔴 CRITICAL
*   **Implementation:**
    *   Build a DAG (Directed Acyclic Graph) for render passes.
    *   **Automatic Barrier Insertion:** The graph analyzes resource usage (Read/Write) and injects Vulkan/DX12 barriers automatically.
    *   **Memory Aliasing:** Reuse transient memory (e.g., Depth Buffer memory reused for Post-Process temp buffers) to lower VRAM usage.
    *   **UE5 Reference:** `FRDGBuilder` (RenderGraphBuilder.h).

### 1.2 Pipeline State Object (PSO) Caching
*   **The Gap:** High-speed games cannot tolerate shader compilation stutter.
*   **Priority:** 🔴 CRITICAL
*   **Implementation:**
    *   **Offline Cache:** Serialize `VkPipeline` / `ID3D12PipelineState` binaries to disk.
    *   **Runtime Pre-warm:** During the game loading screen, pre-compile all generic shaders.
    *   **Background Compilation:** Use a dedicated thread to compile shaders that are missed by the cache, substituting a default material until ready (preventing hitching).
    *   **UE5 Reference:** `FShaderPipelineCache`.

### 1.3 Asynchronous Compute Scheduling
*   **The Gap:** Sequential queue submission underutilizes the GPU.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   Move **Lumen GI**, **Physics Simulation**, and **Particle Updates** to the Async Compute Queue.
    *   Synchronize with Graphics Queue only when drawing begins.
    *   Implement `FRHIAsyncComputeBudget` equivalent for GPU overlap management.

### 1.4 Scalability & Quality Settings
*   **The Gap:** No system to automatically adjust settings for hardware capability.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Quality Presets:** Low/Medium/High/Ultra/Cinematic profiles.
    *   **Dynamic Resolution:** Scale render resolution to maintain target framerate.
    *   **Feature Toggles:** Disable ray tracing, reduce GI quality, LOD bias on lower hardware.
    *   **Auto-Detect:** Query GPU capabilities and select appropriate preset.

---

## Part 2: High-Speed Physics & Traversal (The "Sonic" Feel)
*Goal: Enable gameplay at 700mph without physics breaking.*

### 2.1 Kinetic Character Controller (Custom Jolt Integration)
*   **The Gap:** Standard RigidBody physics cannot handle loops or wall-running reliably.
*   **Priority:** 🔴 CRITICAL
*   **Implementation:**
    *   **Gravity Override:** Implement a `SetGravityVector()` method exposed to C#.
    *   **Surface Adhesion:** Raycast downwards (-LocalUp). If surface detected within threshold, apply heavy "Snap Force" and align character Up vector to Surface Normal.
    *   **Velocity Projection:** When running onto a slope, project the velocity vector onto the new plane to maintain momentum (don't bounce off).
    *   **Speed-Based Adhesion:** Adhesion force scales with velocity (faster = stickier to surfaces).
    *   **UE5 Reference:** `UCharacterMovementComponent::PhysWalking`.

```cpp
// Kinetic Character Controller Pseudocode
class KineticCharacterController {
    void Update(float dt) {
        // 1. Find ground relative to LOCAL down (not world down)
        SurfaceHit hit = SphereCast(position, -transform.up, GROUND_CHECK_DIST);
        
        // 2. Surface adhesion at high speed
        if (hit.valid && speed > ADHESION_THRESHOLD) {
            // Align character up to surface normal
            transform.up = Lerp(transform.up, hit.normal, ADHESION_RATE * dt);
            // Apply snap force
            velocity += -hit.normal * SNAP_FORCE * dt;
        }
        
        // 3. Variable gravity
        glm::vec3 effectiveGravity = GetGravityAtPosition(position);
        velocity += effectiveGravity * dt;
        
        // 4. CCD sub-stepping for high speeds
        int substeps = Max(1, (int)(velocity.length() * dt / MAX_STEP_SIZE));
        for (int i = 0; i < substeps; i++) {
            StepPhysics(dt / substeps);
        }
    }
};
```

### 2.2 Variable Gravity Volumes
*   **The Gap:** Global gravity only. Cannot do loops, planetoids, or twisted tubes.
*   **Priority:** 🔴 CRITICAL
*   **Implementation:**
    *   **Directional Volumes:** Box/Sphere with constant gravity direction.
    *   **Spherical Gravity:** Gravity points toward volume center (planetoids).
    *   **Spline Gravity:** Gravity perpendicular to spline tangent (tubes/loops).
    *   **Blending:** Smooth transition between gravity zones.

```cpp
struct GravityVolume {
    enum Type { Directional, Spherical, SplineBased };
    Type type;
    glm::vec3 direction;      // For Directional
    glm::vec3 center;         // For Spherical
    uint32_t splineId;        // For SplineBased
    float strength = 9.81f;
    float blendRadius = 5.0f; // Transition zone
};
```

### 2.3 The Spline System (Rails, Ziplines & 2.5D)
*   **The Gap:** No way to constrain player movement to paths.
*   **Priority:** 🔴 CRITICAL
*   **Implementation:**
    *   **`SplineComponent`:** Native ECS component holding Bezier/Catmull-Rom control points.
    *   **Math Library:** Fast lookups for `GetPositionAtDistance(t)`, `GetTangentAtDistance(t)`, `GetClosestPoint(worldPos)`.
    *   **Physics Constraint:** C# method `LockToSpline(SplineID)` that maps input to 1D movement along curve.
    *   **Banking:** Auto-calculate roll based on curvature for visual lean.
    *   **UE5 Reference:** `USplineComponent`.

**Use Cases:**
| Use Case | Constraint Type | Notes |
|----------|-----------------|-------|
| Grind Rails | Full lock | Player snaps to spline, input = speed control |
| Ziplines | Full lock + hang offset | Player hangs below spline |
| 2.5D Sections | Lateral constraint | Player locked to plane defined by spline |
| Boost Rings | Velocity injection | Launch player along spline tangent |
| Camera Rails | Reference only | Camera follows spline, player free |

### 2.4 Continuous Collision Detection (CCD)
*   **The Gap:** Tunneling through walls at high speeds.
*   **Priority:** 🔴 CRITICAL
*   **Implementation:**
    *   Enable **MotionQuality::LinearCast** (Swept Volume) in Jolt for the Player Capsule.
    *   **Sub-stepping:** If velocity > Threshold, run physics simulation at 120hz internally while game runs at 60hz.
    *   **Speculative Contacts:** Predict collisions before they happen.

### 2.5 Ability System Framework
*   **The Gap:** No structured system for diverse character abilities.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Ability Base Class:** Cooldowns, resource costs, activation conditions.
    *   **Ability Types:**
        - **Boost:** Velocity burst with invincibility frames
        - **Super Jump:** Variable height based on charge time
        - **Zipline Attach:** Detect nearby splines, snap to closest
        - **Sword Slash:** Melee hitbox with combo system
        - **Flamethrower:** Particle stream with damage volumes
    *   **Input Buffering:** Queue inputs during animations for responsiveness.

```cpp
class Ability {
    float cooldown;
    float resourceCost;
    AbilityState state; // Ready, Active, Cooldown
    
    virtual bool CanActivate();
    virtual void OnActivate();
    virtual void OnTick(float dt);
    virtual void OnDeactivate();
};

class BoostAbility : public Ability {
    void OnActivate() override {
        controller->velocity += controller->forward * BOOST_FORCE;
        controller->SetInvincible(BOOST_DURATION);
        PlayVFX("SpeedLines");
        PlaySFX("BoostWhoosh");
    }
};
```

---

## Part 3: Large Scale World Building (The Environment)
*Goal: Create "The Reclaimed Megastructure" with organic blending.*

### 3.1 Landscape & Terrain System
*   **The Gap:** We currently only support Static Meshes. We need a heightfield.
*   **Priority:** 🔴 CRITICAL (Open World requires this)
*   **Implementation:**
    *   **Clipmap Renderer:** Render terrain in concentric rings around the player (Highest detail center, lower detail far).
    *   **CDLOD (Continuous Distance-Dependent LOD):** Morph vertices between LOD levels to prevent "popping."
    *   **Heightfield Physics:** Jolt supports Heightfield shapes natively; hook this into the terrain data.
    *   **Terrain Layers:** 4-16 material layers with splatmap blending.
    *   **Sculpting Tools:** Raise, lower, smooth, flatten, erosion brushes.
    *   **UE5 Reference:** `ALandscape`, `Runtime/Landscape/Private`.

### 3.2 Hierarchical Instanced Static Meshes (HISM)
*   **The Gap:** Rendering forests individually draws too many calls.
*   **Priority:** 🔴 CRITICAL (Foliage density for visual quality)
*   **Implementation:**
    *   **Instance Buffers:** Store transforms of 100,000+ instances in a single GPU buffer.
    *   **GPU Culling:** Use a Compute Shader to cull instances against the Frustum and HZB *before* the Vertex Shader runs.
    *   **LOD System:** Distance-based mesh LOD with crossfade dithering.
    *   **Wind Animation:** Pivot Painter-style vertex animation for foliage sway.
    *   **Procedural Placement:** Density maps, slope constraints, exclusion zones.
    *   **UE5 Reference:** `UHierarchicalInstancedStaticMeshComponent`.

### 3.3 Runtime Virtual Texturing (RVT)
*   **The Gap:** Objects clip into the ground. We need "Vine Merging."
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Capture Pass:** Render the Landscape Albedo and Normal into a giant Virtual Texture Atlas.
    *   **Material Node:** In the Vine/Rock shader, sample this Virtual Texture based on World Position.
    *   **Blending:** Lerp between the Mesh Texture and the Virtual Texture based on the Z-distance to the ground.
    *   **Alternative:** Use existing SDF/Distance Field infrastructure for mesh-to-terrain blending.
    *   **UE5 Reference:** `URuntimeVirtualTexture`.

### 3.4 Sky & Atmosphere
*   **The Gap:** Basic Skybox only.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Sky Atmosphere:** Implement physically based Rayleigh/Mie scattering shader.
    *   **Volumetric Clouds:** Ray-march a 3D noise texture. Optimization: Render at quarter resolution and temporal upscale.
    *   **Time of Day:** Sun position driving atmosphere color, shadow direction, GI tint.
    *   **Weather System:** Rain, fog, dust storms affecting visibility and gameplay.
    *   **UE5 Reference:** `ASkyAtmosphere`, `AVolumetricCloud`.

### 3.5 Water & Fluid System
*   **The Gap:** No water rendering or physics.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Water Surface:** Planar reflections OR SSR + refraction.
    *   **Wave Simulation:** Gerstner waves or FFT ocean for large bodies.
    *   **Buoyancy Physics:** Objects float based on submerged volume.
    *   **Underwater Post-Process:** Color grading, caustics, fog.
    *   **Interaction:** Splash particles, ripples from player/objects.

### 3.6 Spline-Based Level Streaming
*   **The Gap:** Current radial streaming doesn't account for player velocity/direction.
*   **Priority:** 🔴 CRITICAL (High-speed traversal)
*   **Implementation:**
    *   **Path Prediction:** Load cells ahead along the spline path, not in a radius.
    *   **Velocity-Based Lookahead:** At 700mph, load 2-3km ahead.
    *   **Aggressive Unloading:** Unload cells quickly once passed (no backtracking in linear stages).
    *   **Hybrid Mode:** Radial for open world, linear for stages.

```cpp
void LevelStreaming::UpdatePredictive(const SplineComponent& path, float currentDist, float speed) {
    // Calculate lookahead based on speed
    float lookaheadTime = 5.0f; // seconds
    float lookaheadDist = speed * lookaheadTime;
    
    // Load cells along predicted path
    for (float d = currentDist; d < currentDist + lookaheadDist; d += CELL_SIZE) {
        glm::vec3 futurePos = path.GetPositionAtDistance(d);
        RequestCellLoad(WorldToCell(futurePos), Priority::High);
    }
    
    // Aggressive unload behind (only 2 seconds of backtrack)
    float behindDist = speed * 2.0f;
    for (auto& cell : loadedCells) {
        float cellDist = GetDistanceAlongPath(cell.center);
        if (cellDist < currentDist - behindDist) {
            RequestCellUnload(cell, Priority::Low);
        }
    }
}
```

---

## Part 4: Animation & Simulation (The Polish)
*Goal: Make the character connect with the world and feel responsive.*

### 4.1 Procedural Animation (Control Rig Lite)
*   **The Gap:** Feet sliding, legs not stretching, no lean into turns.
*   **Priority:** 🔴 CRITICAL (Core to game feel)
*   **Implementation:**
    *   **Foot IK:** Raycast from hips to ground. If ground is higher than foot, lift foot and rotate ankle to match normal.
    *   **Stride Warping:** Scale the hip-movement vs. animation-playback-rate so foot speed equals ground speed exactly.
    *   **Banking:** Rotate the Pelvis bone based on Angular Velocity (turning left leans body left).
    *   **Speed-Based Blending:** Blend between walk/jog/run/sprint based on actual velocity.
    *   **Surface Alignment:** Tilt full body to match surface normal when running on walls/ceilings.
    *   **UE5 Reference:** `Control Rig`, `AnimNode_LegIK`, `AnimNode_OrientationWarping`.

```cpp
// Procedural Animation Layer
struct ProceduralAnimData {
    // Foot IK
    glm::vec3 leftFootTarget;
    glm::vec3 rightFootTarget;
    glm::quat leftAnkleRotation;
    glm::quat rightAnkleRotation;
    
    // Body lean
    float bankAngle;        // Side lean based on turn rate
    float pitchAngle;       // Forward lean based on acceleration
    
    // Speed matching
    float playbackRate;     // Animation speed to match ground speed
    float strideScale;      // Stretch legs for longer strides
};

void UpdateProceduralAnim(Character& character, float dt) {
    // Foot placement
    RaycastHit leftHit = Raycast(character.leftHip, -character.up, LEG_LENGTH * 1.5f);
    RaycastHit rightHit = Raycast(character.rightHip, -character.up, LEG_LENGTH * 1.5f);
    
    if (leftHit.valid) {
        data.leftFootTarget = leftHit.position;
        data.leftAnkleRotation = AlignToNormal(leftHit.normal);
    }
    
    // Banking based on angular velocity
    data.bankAngle = Clamp(character.angularVelocity.y * BANK_FACTOR, -MAX_BANK, MAX_BANK);
    
    // Stride warping
    float animSpeed = GetCurrentAnimationSpeed();
    float groundSpeed = Length(character.velocity);
    data.playbackRate = groundSpeed / animSpeed;
}
```

### 4.2 Inertial Blending
*   **The Gap:** Animation transitions feel "floaty" with standard interpolation.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Physics-Based Blending:** Use velocity/acceleration to drive blend weights.
    *   **Motion Extrapolation:** Predict where bones should be based on momentum.
    *   **Instant Response:** Cancel into new animations without waiting for blend.

### 4.3 Motion Matching (Stretch Goal)
*   **The Gap:** Complex state machines for locomotion.
*   **Priority:** 🟢 NICE TO HAVE
*   **Implementation:**
    *   **Pose Database:** Store all animation poses with trajectory metadata.
    *   **Runtime Search:** Find best matching pose based on input trajectory.
    *   **Seamless Transitions:** No explicit transitions needed.
    *   **UE5 Reference:** `Motion Matching` plugin.

### 4.4 Destruction System
*   **The Gap:** Static meshes need to break when hit at high speed.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Voronoi Tool:** In-Editor tool to pre-fracture a mesh into chunks.
    *   **Geometry Collection:** A file format storing the chunks and their connection graph.
    *   **Runtime Switch:** When damage > threshold, hide Static Mesh, spawn Geometry Collection as active Jolt Physics bodies.
    *   **Debris Pooling:** Reuse physics bodies to avoid allocation spikes.
    *   **LOD for Debris:** Distant chunks merge or despawn quickly.
    *   **UE5 Reference:** `Chaos Destruction`.

### 4.5 Cloth Simulation
*   **The Gap:** No cloth physics for capes, scarves, character details.
*   **Priority:** 🟡 HIGH (Visual polish for high-speed movement cues)
*   **Implementation:**
    *   **Verlet Integration:** Simple particle-based cloth.
    *   **Wind Response:** Cloth reacts to movement speed (creates "speed lines" effect).
    *   **Collision:** Simple sphere/capsule colliders for body avoidance.
    *   **GPU Simulation:** Compute shader for performance.

---

## Part 5: Combat & Gameplay Systems
*Goal: Support diverse character abilities and RPG mechanics.*

### 5.1 Melee Combat System
*   **The Gap:** No combat framework.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Hitbox System:** Animated hitbox volumes attached to bones.
    *   **Combo System:** Input buffering, combo chains, cancels.
    *   **Hit Reactions:** Enemy stagger, knockback, juggling.
    *   **Parry/Dodge:** Timing-based defensive mechanics with i-frames.
    *   **Lock-On:** Target tracking for melee engagement.

### 5.2 Projectile & Effects System
*   **The Gap:** No projectile physics or damage volumes.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Projectile Types:** Hitscan, physical, homing.
    *   **Damage Volumes:** Sphere, capsule, cone (for flamethrower).
    *   **Pooling:** Object pools for frequently spawned projectiles.
    *   **Trails:** GPU particle trails for visual feedback.

### 5.3 RPG Progression
*   **The Gap:** No stats, leveling, or inventory.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Stats System:** Health, stamina, attack, defense, speed.
    *   **Level/XP:** Experience gain, level thresholds.
    *   **Skill Trees:** Ability unlocks and upgrades.
    *   **Inventory:** Collectibles, equipment, consumables.
    *   **Save System:** Serialize player progress to disk.

### 5.4 AI & Enemies
*   **The Gap:** No navigation or enemy behavior.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **NavMesh Generation:** Recast/Detour integration for pathfinding.
    *   **Behavior Trees:** Visual graph for enemy decision making.
    *   **Perception:** Sight, hearing, damage awareness.
    *   **Boss Patterns:** Phase-based behavior with attack sequences.
    *   **Spawning:** Encounter triggers, wave management.

---

## Part 6: Audio & Feedback
*Goal: Audio that responds to extreme speed and enhances game feel.*

### 6.1 Procedural Audio (MetaSounds-Lite)
*   **The Gap:** Sample-based audio only. No DSP synthesis.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Wind Synthesis:** Generate wind noise based on player speed.
    *   **Doppler Effect:** Pitch shift for passing objects.
    *   **Speed Layers:** Crossfade between audio layers based on velocity.
    *   **Impact Synthesis:** Procedural hit sounds based on material/force.

### 6.2 Dynamic Music System
*   **The Gap:** Static music playback.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Stem Mixing:** Separate instrument tracks that mix based on gameplay state.
    *   **Intensity Layers:** Combat, exploration, boss phases.
    *   **Transition System:** Musical crossfades on state changes.
    *   **Stingers:** One-shot musical accents for events (ring collect, enemy defeat).

### 6.3 Haptic Feedback (Controller)
*   **The Gap:** No controller rumble integration.
*   **Priority:** 🟢 NICE TO HAVE
*   **Implementation:**
    *   **Speed Rumble:** Low rumble increases with velocity.
    *   **Impact Feedback:** Sharp pulses for hits, landings.
    *   **Trigger Resistance:** (PS5) Adaptive triggers for boost/brake.

---

## Part 7: Tooling & Workflow (The Editor)
*Goal: Allow artists and designers to build without coding.*

### 7.1 Node-Based Material Editor
*   **The Gap:** Hardcoding shaders is too slow for art iteration.
*   **Priority:** 🔴 CRITICAL
*   **Implementation:**
    *   **UI:** ImGui Node Graph editor (imnodes library).
    *   **Backend:** Graph compiler that traverses nodes and generates HLSL/GLSL code string.
    *   **Compilation:** Calls DXC/glslangValidator to compile to SPIR-V/DXIL.
    *   **Live Preview:** Recompile and hot-reload materials in viewport.
    *   **Core Nodes:** TextureSample, Panner, Multiply, Lerp, Add, Subtract, Normalize, Dot, Cross, WorldPosition, VertexNormal, SceneDepth, Time, DistanceFieldGradient, Fresnel, Noise.

### 7.2 Spline Mesh Editor Tool
*   **The Gap:** Building rails/vines by hand is impossible.
*   **Priority:** 🔴 CRITICAL
*   **Implementation:**
    *   **Component:** `SplineMeshComponent` (C# & C++).
    *   **Logic:** User drags spline point -> Engine instantiates Mesh -> Vertex Shader bends mesh based on Spline Tangent.
    *   **Optimization:** Auto-merge static spline meshes into larger buffers for rendering.
    *   **Presets:** Rail, Vine, Pipe, Bridge with auto-configured physics.

### 7.3 Streaming Debugger
*   **The Gap:** We need to visualize the "Linear Prediction" loading.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   Draw the "Loading Window" box along the spline path in the editor.
    *   Color code cells: Green (Loaded), Yellow (Loading), Red (Unloaded).
    *   Show memory usage per cell, streaming priority queue.

### 7.4 Content Browser
*   **The Gap:** No asset management system.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Asset Thumbnails:** Preview images for meshes, materials, textures.
    *   **Search/Filter:** Find assets by name, type, tag.
    *   **Dependency Tracking:** Show what uses/is used by an asset.
    *   **Import Pipeline:** Drag-drop FBX/glTF/PNG with auto-conversion.

### 7.5 Sequencer (Cutscene Editor)
*   **The Gap:** No cinematic tooling.
*   **Priority:** 🟢 NICE TO HAVE (Can ship without)
*   **Implementation:**
    *   **Timeline UI:** Tracks for camera, actors, audio, events.
    *   **Keyframe Animation:** Animate transforms, properties over time.
    *   **Camera Cuts:** Switch between cameras on the timeline.
    *   **Event Triggers:** Fire C# events at specific frames.

### 7.6 Level Editor Enhancements
*   **The Gap:** Basic entity placement only.
*   **Priority:** 🟡 HIGH
*   **Implementation:**
    *   **Multi-Select:** Select and transform multiple entities.
    *   **Snap Tools:** Grid snap, surface snap, vertex snap.
    *   **Prefabs:** Save/load entity groups as reusable templates.
    *   **Undo/Redo:** Full edit history with Ctrl+Z/Y.
    *   **Copy/Paste:** Duplicate entities with transforms.

---

## Part 8: Comparison with Unreal Engine 5
*Reference table for feature parity tracking.*

### Rendering & Graphics
| Feature | UE5.7 | Sanic | Gap |
|---------|-------|-------|-----|
| Nanite Geometry | ✅ | ✅ | ✅ Parity |
| Nanite Skinned Foliage | ✅ NEW | ❌ | 🔴 Need (wind animation) |
| Lumen GI | ✅ Improved | ✅ | ⚠️ Check translucency |
| MegaLights | ✅ NEW | ❌ | 🔴 Need (many dynamic lights) |
| Virtual Shadow Maps | ✅ Improved | ✅ | ⚠️ Update defaults |
| Hardware Ray Tracing | ✅ | ✅ | ✅ Parity |
| Temporal Super Resolution | ✅ | ⚠️ TAA only | 🟡 Need TSR |
| Substrate Materials | ✅ Production | ❌ | 🔴 Need (layered materials) |
| Heterogeneous Volumes | ✅ Improved | ❌ | 🟡 Need (smoke/fire) |
| Volumetric Clouds | ✅ | ❌ | 🔴 Need |
| Sky Atmosphere | ✅ | ❌ | 🔴 Need |

### World Building
| Feature | UE5.7 | Sanic | Gap |
|---------|-------|-------|-----|
| World Partition | ✅ Improved | ✅ | ✅ Parity |
| Landscape Terrain | ✅ | ❌ | 🔴 Need |
| Procedural Vegetation Editor | ✅ NEW | ❌ | 🟡 Need (in-engine trees) |
| PCG GPU Compute | ✅ NEW | ❌ | 🟡 Need (fast biome gen) |
| Foliage System (HISM) | ✅ | ⚠️ Basic | 🔴 Need GPU culling |
| Water System | ✅ | ❌ | 🟡 Need |
| Runtime Virtual Texturing | ✅ | ❌ | 🟡 Need |

### Physics & Movement
| Feature | UE5.7 | Sanic | Gap |
|---------|-------|-------|-----|
| Character Movement | ✅ | ⚠️ Basic Jolt | 🔴 Need custom controller |
| Spline Components | ✅ | ❌ | 🔴 Need |
| Variable Gravity | ⚠️ Manual | ❌ | 🔴 Need |
| CCD | ✅ | ⚠️ Jolt supports | Need enforcement |
| Chaos Destruction | ✅ | ⚠️ Basic | 🟡 Need |
| Chaos Cloth | ✅ | ❌ | 🟡 Need |

### Animation
| Feature | UE5.7 | Sanic | Gap |
|---------|-------|-------|-----|
| Skeletal Animation | ✅ | ✅ | ✅ Parity |
| Blend Trees | ✅ | ✅ | ✅ Parity |
| Control Rig (IK) | ✅ + Dependency View | ⚠️ Basic IK | 🔴 Need stride warp/bank |
| Motion Matching + Choosers | ✅ NEW | ❌ | 🟢 Nice to have |
| Skeletal Sculpting | ✅ NEW | ❌ | 🟢 Nice to have |
| Animation Compression | ✅ ACL | ❌ | 🟡 Need for memory |

### Audio
| Feature | UE5.7 | Sanic | Gap |
|---------|-------|-------|-----|
| 3D Spatial Audio | ✅ | ✅ | ✅ Parity |
| HRTF | ✅ | ✅ | ✅ Parity |
| MetaSounds DSP | ✅ Improved | ❌ | 🟡 Need |
| Middleware (Wwise/FMOD) | ✅ | ❌ | 🟡 Optional |

### Core Systems
| Feature | UE5.7 | Sanic | Gap |
|---------|-------|-------|-----|
| Render Graph (RDG) | ✅ | ❌ | 🔴 Need |
| PSO Caching | ✅ | ❌ | 🔴 Need |
| Async Compute | ✅ | ⚠️ Basic | 🟡 Need |
| Reflection/Serialization | ✅ UHT | ❌ | 🟡 Need |
| Networking | ✅ | ❌ | ❓ TBD |

### Tooling
| Feature | UE5.7 | Sanic | Gap |
|---------|-------|-------|-----|
| Material Editor | ✅ + Substrate | ❌ | 🔴 Need |
| Content Browser | ✅ | ❌ | 🟡 Need |
| Sequencer | ✅ | ❌ | 🟢 Nice to have |
| Behavior Trees | ✅ | ❌ | 🟡 Need |
| NavMesh | ✅ Recast | ❌ | 🟡 Need |
| Procedural Vegetation Editor | ✅ NEW | ❌ | 🟡 Need |

---

## Part 9: Implementation Roadmap

### Phase 1: Movement Core (6-8 weeks)
*The game cannot exist without this.*

| Task | Priority | Estimate |
|------|----------|----------|
| Spline Component System | 🔴 | 2 weeks |
| Kinetic Character Controller | 🔴 | 2 weeks |
| Variable Gravity Volumes | 🔴 | 1 week |
| CCD Enforcement | 🔴 | 0.5 weeks |
| Spline Mesh Generation | 🔴 | 1.5 weeks |
| Basic Ability Framework | 🔴 | 1 week |

### Phase 2: World Foundation (8-10 weeks)
*Required for open world zones.*

| Task | Priority | Estimate |
|------|----------|----------|
| Clipmap Terrain System | 🔴 | 3 weeks |
| Terrain Physics (Heightfield) | 🔴 | 1 week |
| HISM Foliage + GPU Culling | 🔴 | 2 weeks |
| Sky Atmosphere | 🟡 | 1.5 weeks |
| Volumetric Clouds | 🟡 | 1.5 weeks |
| Spline-Based Streaming | 🔴 | 1 week |

### Phase 3: Polish & Feel (6-8 weeks)
*What separates good from great.*

| Task | Priority | Estimate |
|------|----------|----------|
| Control Rig (Foot IK, Banking) | 🔴 | 2 weeks |
| Stride Warping | 🔴 | 1 week |
| Per-Object Motion Blur | 🟡 | 1 week |
| Destruction System | 🟡 | 2 weeks |
| Cloth Simulation | 🟡 | 1.5 weeks |

### Phase 4: Core Systems (4-6 weeks)
*Stability and performance.*

| Task | Priority | Estimate |
|------|----------|----------|
| Render Graph (RDG) | 🔴 | 2 weeks |
| PSO Caching | 🔴 | 1 week |
| Async Compute Scheduling | 🟡 | 1 week |
| Quality Scalability | 🟡 | 1 week |

### Phase 5: Combat & RPG (6-8 weeks)
*Gameplay depth.*

| Task | Priority | Estimate |
|------|----------|----------|
| Melee Combat System | 🟡 | 2 weeks |
| Projectile System | 🟡 | 1 week |
| AI NavMesh (Recast) | 🟡 | 2 weeks |
| Behavior Trees | 🟡 | 1.5 weeks |
| RPG Stats/Inventory | 🟡 | 1.5 weeks |

### Phase 6: Tooling (Ongoing)
*Parallel with other work.*

| Task | Priority | Estimate |
|------|----------|----------|
| Node Material Editor | 🔴 | 3 weeks |
| Spline Editor Tool | 🔴 | 1 week |
| Content Browser | 🟡 | 2 weeks |
| Streaming Debugger | 🟡 | 0.5 weeks |
| Level Editor Polish | 🟡 | 2 weeks |

---

## Part 10: Technical Debt & Risk Areas

### Known Technical Debt
1. **Manual Vulkan Barriers:** Risk of GPU hangs as complexity grows.
2. **No PSO Cache:** First-run shader stutter unacceptable for fast gameplay.
3. **Single-Threaded Rendering:** RHI command recording should be parallel.

### Risk Mitigation
| Risk | Impact | Mitigation |
|------|--------|------------|
| Spline physics feels wrong | 🔴 High | Prototype early, iterate on feel |
| Terrain performance issues | 🟡 Medium | Profile on target hardware weekly |
| Memory pressure (open world) | 🟡 Medium | Implement streaming debugger early |
| Shader stutter | 🔴 High | PSO cache is critical path |
| Animation jank at high speed | 🟡 Medium | Control rig must scale with velocity |

---

## Part 11: AI-First Development Strategy
*The only viable path for solo-dev AAA production.*

**Philosophy:** The Engine is the "Factory." AI is the "Labor." You are the "Architect."

Building a 40+ hour AAA-scale RPG as a solo developer (even with AI agents) is an immense challenge. The only way to make this plausible is to **invert the standard workflow**. Instead of hand-crafting content, you must **hand-craft the *systems* that generate content**, and use Generative AI to fill the data pipelines.

---

### 11.1 Procedural World Generation (The "PCG" Pipeline)
*Constraint: You cannot hand-place 7 zones worth of assets.*
*Solution: Build a rule-based PCG framework in Sanic Engine.*

#### A. The "Biome Painter" Tool (Editor Feature)
*   **Concept:** Paint a heatmap on a massive landscape (8km x 8km). Red = Industrial, Green = Jungle, Blue = Water.
*   **The AI Agent:**
    *   Feed the AI a "Style Guide" (e.g., "Industrial Zone: Pipes, Rust, Vents").
    *   **Sanic Engine Tech:** Implement **Wave Function Collapse (WFC)** solver or **Poisson Disk Sampling**.
    *   **Workflow:**
        1.  Paint "Industrial" on a cliff.
        2.  Engine automatically scatters `Rust_Pipe_01`, `Vent_Large_04` based on slope and connectivity rules.
        3.  Engine automatically merges meshes (HISM) for performance.

#### B. Procedural Spline Generation (The "Track" Maker)
*   **Concept:** Sonic levels are splines. Hand-placing 500km of rails is impossible.
*   **The AI Agent:**
    *   **Pathfinding AI:** Set a "Start Point" and "End Point."
    *   **Algorithm:** An A* Agent "runs" through terrain.
    *   **Generation:** Records path → Engine converts to smooth Spline → Auto-generates rail mesh with banking → Places coin/ring trails along trajectory.

```cpp
// Procedural Track Generation
class TrackGenerator {
public:
    Spline GenerateTrack(glm::vec3 start, glm::vec3 end, TrackStyle style) {
        // A* pathfinding with terrain awareness
        std::vector<glm::vec3> waypoints = PathfindThroughTerrain(start, end);
        
        // Smooth into spline
        Spline track = CatmullRomFit(waypoints, style.smoothness);
        
        // Auto-banking based on curvature
        for (float t = 0; t < track.length; t += SEGMENT_SIZE) {
            float curvature = track.GetCurvatureAt(t);
            float bankAngle = curvature * style.bankFactor;
            track.SetBankAt(t, bankAngle);
        }
        
        // Place collectibles along path
        PlaceCollectibles(track, style.ringDensity);
        
        return track;
    }
};
```

---

### 11.2 AI Asset Generation (Visuals)
*Constraint: You cannot model/texture 5,000 assets.*

#### A. AI Texturing (Materials)
*   **Tool:** Stable Diffusion / Midjourney → Materialize (AI-to-PBR).
*   **Pipeline:**
    1.  Prompt: *"Rusted white sci-fi metal with moss leaks, 4k texture."*
    2.  AI generates Diffuse map.
    3.  AI generates Normal, Roughness, Height, AO from Diffuse.
    4.  **Sanic Engine:** Auto-imports into "Master Material" instance. Swap texture sets, never tweak nodes.

| AI Tool | Output | Engine Integration |
|---------|--------|-------------------|
| Stable Diffusion | Diffuse/Albedo | Hot-folder import |
| Materialize AI | PBR map set | Auto-material creation |
| Shap-E / Point-E | Simple meshes | Greeble library |
| Meshy.ai | 3D from image | Kitbash pieces |

#### B. 3D Mesh "Kitbashing"
*   **Strategy:** Don't generate complex meshes. Generate **"Greebles"** (vents, panels, bolts, trees).
*   **The Engine:** PCG tool glues simple AI parts into complex structures.
*   **Example:** A "Building" = simple cube + 50 AI-generated "Vents" procedurally attached.

---

### 11.3 AI Animation & Motion (The "Living" Character)
*Constraint: Mocap is expensive. Keyframing is slow.*

#### A. AI Motion Synthesis
*   **Pipeline:**
    1.  Record yourself doing a clumsy jump OR find parkour video.
    2.  **AI Tool (Move.ai / Cascadeur):** Extracts 3D root motion and skeletal data.
    3.  **Sanic Engine:** Import raw animation.
    4.  **Motion Matching System:** Hides the "jank" by selecting best frame for current velocity. 500 messy jump variations → engine picks the smooth one *right now*.

#### B. Procedural Lip Sync (Audio-to-Face)
*   **Tool:** Nvidia Audio2Face / Oculus Lipsync.
*   **Pipeline:**
    1.  AI generates Voice Audio (ElevenLabs).
    2.  Engine analyzes waveform at runtime (FFT).
    3.  Engine drives Facial Morph Targets automatically. No hand-animating faces.

```cpp
// Runtime Lip Sync from AI Audio
class LipSyncDriver {
    void ProcessAudioFrame(const float* samples, int count) {
        // FFT analysis
        std::array<float, 5> phonemes = AnalyzePhonemes(samples, count);
        
        // Drive morph targets
        mesh->SetMorphWeight("MouthOpen", phonemes[0]);
        mesh->SetMorphWeight("MouthWide", phonemes[1]);
        mesh->SetMorphWeight("MouthPucker", phonemes[2]);
        mesh->SetMorphWeight("JawOpen", phonemes[3]);
        mesh->SetMorphWeight("TongueUp", phonemes[4]);
    }
};
```

---

### 11.4 AI Audio & Narrative (The "Soul")
*Constraint: Writing 40 hours of dialogue and composing 50 tracks.*

#### A. Generative Music System (Dynamic Soundtrack)
*   **Tool:** Suno AI / Udio (for stems) + FMOD/Wwise integration.
*   **Strategy:** Don't generate full songs. Generate **Stems** (Drum loop, Bassline, Synth pad).
*   **Sanic Engine Audio System:**

| Game State | Active Stems | Transition |
|------------|--------------|------------|
| `Idle` | Synth Pad only | Instant |
| `Walking` | + Soft Bass | 2s fade |
| `Running` | + Hi-Hats, Full Bass | 1s fade |
| `Combat` | + Aggressive Drums, Guitar | 0.5s fade |
| `Boss` | Full Orchestra + Choir | Beat-synced |

*Result: Music perfectly syncs to gameplay speed without composing complex transitions.*

#### B. AI Voice Acting & Dialogue
*   **Tool:** ElevenLabs (for Vex/NPCs).
*   **Pipeline:**
    *   Write script (aided by ChatGPT/Claude).
    *   Batch generate thousands of lines.
    *   **Runtime Variation:** Generate 50 "Bark" variations each (*"Reloading!", "Watch out!"*) → Never repetitive.

---

### 11.5 Engine Features Required for AI-First Workflow

To support this solo-dev workflow, **Sanic Engine** needs these specific interfaces:

#### Phase A: The "Importer" Pipeline (Hot-Folder System)
*   **Feature:** Background `AssetProcessor` with folder watcher.
*   **Why:** When AI agent generates texture/sound, Engine *instantly* imports, compresses, creates Asset ID without manual "Import" click.
*   **Tech:** `FileSystemWatcher` + async asset pipeline.

```cpp
class HotFolderImporter {
    void WatchFolder(const std::string& path) {
        watcher.OnFileCreated = [this](const std::string& file) {
            AssetType type = DetectAssetType(file);
            switch (type) {
                case AssetType::Texture:
                    TextureImporter::ImportAsync(file, TextureSettings::Default());
                    break;
                case AssetType::Audio:
                    AudioImporter::ImportAsync(file, AudioSettings::Default());
                    break;
                case AssetType::Mesh:
                    MeshImporter::ImportAsync(file, MeshSettings::Default());
                    break;
            }
        };
    }
};
```

#### Phase B: The PCG Framework (C# Scripting API)
*   **Feature:** Expose `SpawnActor`, `Raycast`, `MeshMerge`, `HeightmapQuery` to C#.
*   **Why:** Write C# scripts: *"For every pixel in Heightmap > 0.5, spawn a Tree."*
*   **Result:** Build worlds via code rules, not hand placement.

```csharp
// C# PCG Script Example
public class BiomeScatterer : PCGScript
{
    public void GenerateZone(Heightmap map, BiomeRules rules)
    {
        foreach (var pixel in map.Pixels)
        {
            if (pixel.value > rules.TreeThreshold && pixel.slope < rules.MaxTreeSlope)
            {
                var tree = rules.TreePrefabs.Random();
                var pos = map.PixelToWorld(pixel);
                var rot = Quaternion.FromAxisAngle(Vector3.Up, Random.Range(0, 360));
                var scale = Random.Range(rules.MinScale, rules.MaxScale);
                
                PCG.SpawnInstance(tree, pos, rot, scale);
            }
        }
        
        // Merge into HISM for performance
        PCG.MergeInstances();
    }
}
```

#### Phase C: Motion Matching Database
*   **Feature:** `PoseSearch` database with trajectory matching.
*   **Why:** Utilize messy AI animations effectively. Throw 1,000 AI animations into bucket → engine connects them mathematically.

#### Phase D: Runtime Audio Analysis
*   **Feature:** FFT analysis in Audio Engine.
*   **Why:** Drive gameplay elements (lights pulsing) and facial animation from AI-generated audio streams in real-time.

---

### 11.6 The Daily Solo-Dev Workflow

| Time | Activity | Tools |
|------|----------|-------|
| **Morning** | Define zone rules: *"Red Rock + Neon Grass"* | Editor + Style Guide |
| **Background** | Agent 1: 100 texture variations | Stable Diffusion |
| **Background** | Agent 2: 50 debris mesh kits | Shap-E / Meshy |
| **Background** | Agent 3: Audio stems | Suno AI |
| **Auto** | Engine imports all assets | Hot-Folder Importer |
| **Afternoon** | Run PCG Script | C# + Biome Painter |
| **Auto** | Generate terrain, scatter rocks, place debris, paint grass | PCG Framework |
| **Auto** | Generate rail grind splines via pathfinder | Track Generator |
| **Evening** | Playtest. Tweak gravity variable in C#. | Game Client |

**Result:** Content creation bottleneck removed. You focus on **Game Design** and **Engine Engineering**—your strengths.

---

### 11.7 AI Tool Stack Summary

| Category | Tool | Output | Integration |
|----------|------|--------|-------------|
| **Textures** | Stable Diffusion | Diffuse | Hot-folder |
| **PBR Maps** | Materialize / TexGen | Normal/Rough/AO | Auto-material |
| **3D Meshes** | Shap-E, Meshy, Tripo | Greebles | Kitbash library |
| **Animation** | Move.ai, Cascadeur | Skeletal clips | Motion Matching |
| **Voice** | ElevenLabs | Dialogue WAV | Lip Sync driver |
| **Music** | Suno AI, Udio | Stem loops | Dynamic mixer |
| **SFX** | ElevenLabs SFX | Impact/Whoosh | Event system |
| **Dialogue** | Claude, GPT-4 | Script text | Batch TTS |
| **Level Design** | Claude Code | PCG rules | C# scripts |

---

## Appendix A: C# API Design (Scripting Interface)

### Character Controller API
```csharp
public class KineticController : Component
{
    public Vector3 Velocity { get; set; }
    public Vector3 GravityDirection { get; set; }
    public float GravityStrength { get; set; }
    
    public void SetGravityVolume(GravityVolume volume);
    public void LockToSpline(Spline spline, float startDistance);
    public void UnlockFromSpline();
    
    public void ApplyForce(Vector3 force, ForceMode mode);
    public void Boost(float power, float duration);
    public void SuperJump(float height);
}
```

### Spline API
```csharp
public class Spline : Component
{
    public Vector3 GetPositionAtDistance(float distance);
    public Quaternion GetRotationAtDistance(float distance);
    public Vector3 GetTangentAtDistance(float distance);
    public float GetClosestDistance(Vector3 worldPosition);
    public float TotalLength { get; }
    
    public void AddControlPoint(Vector3 position, int index = -1);
    public void RemoveControlPoint(int index);
}
```

### Ability API
```csharp
public abstract class Ability : Component
{
    public float Cooldown { get; protected set; }
    public float ResourceCost { get; protected set; }
    public bool IsReady => cooldownRemaining <= 0;
    
    public abstract bool CanActivate();
    public abstract void OnActivate();
    public virtual void OnTick(float deltaTime) { }
    public virtual void OnDeactivate() { }
}
```

---

## Appendix B: Shader Requirements

### New Shaders Needed
| Shader | Purpose |
|--------|---------|
| `terrain_clipmap.vert/frag` | Terrain with LOD morphing |
| `atmosphere_scatter.comp` | Sky atmosphere LUT generation |
| `volumetric_clouds.comp` | Ray-marched cloud rendering |
| `water_surface.vert/frag` | Water with Gerstner waves |
| `foliage_instance.vert` | Wind animation for instances |
| `cloth_simulate.comp` | GPU cloth physics |
| `spline_mesh.vert` | Bend mesh along spline |
| `velocity_buffer.frag` | Per-object motion vectors |

---

*Document Version: 2.0*
*Last Updated: 2025-11-30*
*Target Completion: Q2 2026 (Alpha)*
