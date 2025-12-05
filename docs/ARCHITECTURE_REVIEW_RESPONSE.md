# Sanic Engine Architecture Review - Action Plan

## Understanding the Feedback

This document breaks down the architectural review into **actionable categories** for the Sanic Engine team. Since we're building a **Sonic-style RPG platformer**, some "violations" are actually **intentional design decisions**, while others are legitimate issues that need fixing.

---

## Quick Reference: What's a Problem vs What's Fine

| Feedback Item | Verdict | Reason |
|--------------|---------|--------|
| Sphere Picking | 🔴 **FIX** | Breaks level editing for all users |
| Rail/Loop Physics in Engine | 🟢 **KEEP** | Core to our game genre |
| Quest/Inventory Systems | 🟡 **MOVE** | Should be in game layer, not engine |
| TODO: File Dialogs | 🔴 **FIX** | Editor is unusable without save/load |
| Hardcoded "Nanite/Lumen" names | 🟡 **RENAME** | Misleading but low priority |
| FBX/GLTF Import Missing | 🟠 **ADD** | Needed for artist workflow |
| Wind/Footstep Audio | 🟢 **KEEP** | Integral to game feel |

---

## Part 1: Critical Fixes (Blocks Development)

### 1.1 Editor Save/Load is Broken

**The Problem:**
```cpp
// In Editor.cpp
void Editor::openScene(const std::string& path) {
    // TODO: Show file dialog  <-- Nothing happens when user clicks Open
    return;
}

void Editor::saveScene() {
    // TODO: Save scene  <-- Work is lost on close
}
```

**Why It Matters:** Developers cannot save their level designs. All work is lost when closing the editor.

**Fix Required:**
- [ ] Integrate a file dialog library (e.g., `nfd` or `tinyfiledialogs`)
- [ ] Connect `SceneSerializer::saveScene()` to the menu action
- [ ] Add auto-save functionality

**Files to Modify:**
- [Editor.cpp](../src/editor/Editor.cpp) - Lines with TODO comments
- Add dependency to CMakeLists.txt for file dialog library

---

### 1.2 Sphere Picking Makes Selection Impossible

**The Problem:**
```cpp
// In Viewport.cpp - pickEntityAtMouse()
float pickRadius = 0.5f * glm::length(transform.scale);
// Checks sphere intersection only
```

**Visual Example:**
```
    ┌─────────────────┐
    │    BIG ROCK     │  ← Rock's bounding sphere
    │   ┌───┐         │
    │   │ F │         │  ← Fence post (inside rock's sphere)
    │   │ E │         │
    │   │ N │         │
    │   │ C │         │
    │   │ E │         │
    │   └───┘         │
    └─────────────────┘
    
    User clicks on fence → Selects rock instead!
```

**Why It Matters:** Level designers cannot select thin objects (fences, poles, rails) near large objects.

**Fix Options (Choose One):**

| Option | Complexity | Accuracy | Performance |
|--------|------------|----------|-------------|
| A. GPU Object ID Readback | Medium | Perfect | Fast |
| B. Ray-Mesh Intersection | High | Perfect | Slower |
| C. Bounding Box (instead of sphere) | Low | Better | Fast |

**Recommended: Option A - GPU Picking**
```cpp
// Concept: Render Object IDs to 1x1 pixel at mouse position
// 1. Create tiny framebuffer (1x1 or 3x3 pixels)
// 2. Render scene with Object ID as color
// 3. Read back pixel value = selected entity
```

**Files to Modify:**
- [Viewport.cpp](../src/editor/viewport/Viewport.cpp) - `pickEntityAtMouse()`
- [EditorRenderer.cpp](../src/editor/EditorRenderer.cpp) - Add ID render pass

---

### 1.3 Hardcoded Scene in Renderer

**The Problem:**
```cpp
// In Renderer.cpp - loadGameObjects()
// Completely ignores AssetSystem and loads specific test assets:
auto rockMesh = AssetLoader::get().load<MeshAsset>("../assets/rock/rock.obj");
// ...
gameObjects.push_back(rock);  // Hardcoded rock placement
```

**Why It Matters:** The editor loads a hardcoded test scene instead of user-created levels. The WorldAsset system is bypassed.

**Fix Required:**
- [ ] Remove hardcoded `loadGameObjects()`
- [ ] Load scene from `ProjectDescriptor::defaultWorld`
- [ ] Connect `WorldAsset::objects` to entity spawning

**Files to Modify:**
- [Renderer.cpp](../src/engine/Renderer.cpp)
- [main.cpp](../src/main.cpp) or wherever initialization occurs

---

## Part 2: Missing Features (Blocks Artists)

### 2.1 FBX/GLTF Import

**Current State:**
```cpp
bool MeshAsset::importFromFBX(const std::string& fbxPath) {
    // TODO: FBX import
    return false;
}
```

**Why It Matters:** 
- Artists use Blender/Maya → Export FBX/GLTF
- Currently only OBJ works
- OBJ lacks: animations, materials, skeleton data

**Fix Required:**
- [ ] Integrate Assimp library OR
- [ ] Use cgltf for GLTF (lighter weight)
- [ ] Parse material references during import

**Priority:** HIGH - Artists are blocked

---

### 2.2 Texture Compression Pipeline

**The Problem:** Raw PNG/JPG textures are loaded directly into GPU memory.

**Why It Matters:**
| Format | Size (1024x1024) | GPU Load Time |
|--------|------------------|---------------|
| PNG (raw) | 4 MB | Slow, needs decode |
| BC7 (compressed) | 1 MB | Fast, native GPU format |

**Fix Required:**
- [ ] Add texture compression during asset import (using `compressonator` or `texconv`)
- [ ] Create `.stex` binary format for engine-ready textures
- [ ] Add compression quality settings

---

## Part 3: Architecture Decisions (Keep vs Move)

### 3.1 KEEP: Kinetic Character Controller

The reviewer called this "hard-coded gameplay logic" but for our Sonic-style game, this IS the engine:

```cpp
// KineticCharacterController.cpp
void updateSplineLock();      // Rail grinding - CORE FEATURE
void updateSurfaceAdhesion(); // Loop running - CORE FEATURE
void updateCoyoteTime();      // Jump forgiveness - CORE FEATURE
```

**Decision: KEEP in Engine**

These are not "specific game logic" - they define the character controller that our entire game is built around. They should:
- [ ] Be configurable via exposed parameters (not just constants)
- [ ] Have enable/disable flags for different character types
- [ ] Stay in `src/engine` as our core movement system

---

### 3.2 KEEP: Procedural Audio Systems

The reviewer criticized `WindSynthesizer` and `FootstepSynthesizer` as "too specific."

**Decision: KEEP but Generalize**

For a high-speed Sonic game:
- Wind audio feedback is essential for speed feel
- Footsteps on different surfaces add immersion

**Action Items:**
- [ ] Make surface types data-driven (load from asset file, not enum)
- [ ] Allow disabling systems via config for scenes that don't need them
- [ ] Keep the synthesis code but parameterize frequencies/envelopes

---

### 3.3 MOVE: Quest/Inventory/Combat Systems

The reviewer is correct here. These should NOT be in `src/engine`:

```
src/engine/
├── QuestSystem.cpp      ← MOVE to src/game/
├── InventorySystem.cpp  ← MOVE to src/game/
├── CombatSystem.cpp     ← MOVE to src/game/
└── ...
```

**Why Move:**
- Quest content ("A Rat Problem") is game-specific
- Item stats (minDamage, armorValue) are RPG-specific
- Combat stamina rules are game design, not engine

**Refactor Plan:**
```
src/
├── engine/           ← Core systems (rendering, physics, audio, ECS)
├── game/             ← NEW: Game-specific systems
│   ├── QuestSystem.cpp
│   ├── InventorySystem.cpp
│   └── CombatSystem.cpp
└── editor/           ← Editor UI
```

**Benefits:**
- Cleaner architecture
- Engine can be reused for different games
- Game logic can be hot-reloaded via C# if moved to scripts

---

### 3.4 RENAME: View Mode Names

**Current:** "Nanite", "Lumen", "VirtualShadowMap"

**Problem:** These are Epic Games trademarks. Our implementation is inspired by but not identical to Unreal's.

**Rename Map:**
| Current | Rename To |
|---------|-----------|
| Nanite | Virtual Geometry / Cluster LOD |
| Lumen | Dynamic Global Illumination |
| VirtualShadowMap | Cached Shadow Maps |

**Files to Modify:**
- [ViewMode.h](../src/engine/core/ViewMode.h) - Enum names and display strings

---

## Part 4: Lower Priority Fixes

### 4.1 Asset Scanning Performance

**The Problem:** `scanProjectContent()` does a full recursive file scan every startup.

**Impact:** With 50,000 assets, startup takes minutes.

**Fix:**
- [ ] Create `.sanic_cache` file storing asset metadata
- [ ] Only rescan folders with changed modification times
- [ ] Add progress bar during initial scan

---

### 4.2 Async Loading is Fake

```cpp
void AssetLoader::loadAsync(const std::string& path, LoadCallback callback) {
    // TODO: Proper async loading with job system
    auto asset = loadGeneric(path);  // ← Blocks main thread!
    callback(asset, asset != nullptr);
}
```

**Fix:**
- [ ] Implement job queue for background loading
- [ ] Add loading screen support
- [ ] Priority: LOW (affects loading screens, not core functionality)

---

### 4.3 Physics Integration Stubs

Several physics functions are placeholders:

```cpp
void DestructionSystem::createPieceBody(/* ... */) {
    // In a full implementation, you'd create the Jolt body here
}
```

**Fix:**
- [ ] Complete Jolt Physics integration for destruction debris
- [ ] Add ragdoll support for enemies
- [ ] Priority: MEDIUM (affects destruction visual quality)

---

## Implementation Roadmap

### Sprint 1: Editor Basics (1-2 weeks)
- [ ] Fix save/load file dialogs
- [ ] Implement GPU picking for selection
- [ ] Remove hardcoded scene, load from WorldAsset

### Sprint 2: Artist Pipeline (2-3 weeks)
- [ ] Add FBX/GLTF import
- [ ] Add texture compression
- [ ] Create asset import wizard

### Sprint 3: Architecture Cleanup (1 week)
- [ ] Move Quest/Inventory/Combat to `src/game/`
- [ ] Rename misleading view mode names
- [ ] Add config files for Kinetic constants

### Sprint 4: Polish (Ongoing)
- [ ] Asset database caching
- [ ] True async loading
- [ ] Complete physics stubs

---

## Summary

| Category | Action | Priority |
|----------|--------|----------|
| Editor Save/Load | FIX immediately | 🔴 CRITICAL |
| Sphere Picking | Replace with GPU picking | 🔴 CRITICAL |
| Hardcoded Scene | Connect to AssetSystem | 🔴 CRITICAL |
| FBX/GLTF Import | Add Assimp/cgltf | 🟠 HIGH |
| Quest/Inventory/Combat | Move to `src/game/` | 🟡 MEDIUM |
| View Mode Names | Rename | 🟢 LOW |
| Kinetic Controller | Keep, parameterize | ✅ DONE (intentional) |
| Procedural Audio | Keep, data-drive surfaces | ✅ DONE (intentional) |

The engine is correctly specialized for our Sonic-style RPG platformer. The critical fixes are about **editor usability**, not architectural purity.
