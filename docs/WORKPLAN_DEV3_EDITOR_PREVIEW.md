# Developer 3: Editor Framework & Preview Systems

## Assigned Tasks
- **Task 16**: Visual Editor / Level Editor - Core Framework
- **Task 17**: Map/Level Building - Scene Tools
- **Task 18**: Preview Systems - Real-time Feedback
- **Task 19**: Asset Browser - Content Management

## Overview
Your responsibility is to build the editor framework including viewport, gizmos, property inspector, scene hierarchy, asset browser, and preview systems. This enables non-programmers to create content.

---

## Task 16: Visual Editor Core Framework

### Target Architecture

```
src/editor/
├── Editor.h                    # Main editor application
├── Editor.cpp
├── EditorWindow.h              # Base window class
├── EditorWindow.cpp
├── EditorLayout.h              # Docking/layout manager
├── EditorLayout.cpp
├── core/
│   ├── Selection.h             # Selection manager
│   ├── Selection.cpp
│   ├── UndoSystem.h            # Undo/redo
│   ├── UndoSystem.cpp
│   ├── Commands.h              # Editor commands
│   ├── Commands.cpp
│   ├── Shortcuts.h             # Keyboard shortcuts
│   └── Shortcuts.cpp
├── viewport/
│   ├── Viewport.h              # 3D viewport
│   ├── Viewport.cpp
│   ├── ViewportCamera.h        # Camera controls
│   ├── ViewportCamera.cpp
│   ├── Gizmo.h                 # Transform gizmos
│   ├── Gizmo.cpp
│   ├── Grid.h                  # Grid rendering
│   ├── Grid.cpp
│   └── SelectionOutline.h      # Selection highlighting
├── panels/
│   ├── HierarchyPanel.h        # Scene tree
│   ├── HierarchyPanel.cpp
│   ├── InspectorPanel.h        # Properties
│   ├── InspectorPanel.cpp
│   ├── AssetBrowserPanel.h     # Content browser
│   ├── AssetBrowserPanel.cpp
│   ├── ConsolePanel.h          # Log output
│   └── ConsolePanel.cpp
└── widgets/
    ├── PropertyWidgets.h       # Property editors
    ├── PropertyWidgets.cpp
    ├── ColorPicker.h
    ├── CurvEditor.h
    └── TransformWidget.h
```

### Step 1: Editor Core

Create `src/editor/Editor.h`:

```cpp
#pragma once
#include "EditorWindow.h"
#include "core/Selection.h"
#include "core/UndoSystem.h"
#include "core/Shortcuts.h"
#include "../engine/ECS.h"
#include "../engine/Renderer.h"
#include <imgui.h>
#include <memory>
#include <vector>
#include <functional>

namespace Sanic::Editor {

// Editor mode
enum class EditorMode {
    Edit,       // Normal editing
    Play,       // Playing in editor
    Paused,     // Paused during play
    Simulate    // Physics simulation without player
};

// Editor configuration
struct EditorConfig {
    std::string layoutPath = "editor_layout.ini";
    std::string recentProjectsPath = "recent_projects.json";
    
    // Viewport
    float gizmoSize = 100.0f;
    float gridSize = 100.0f;
    float gridStep = 1.0f;
    bool snapToGrid = true;
    float snapTranslate = 1.0f;
    float snapRotate = 15.0f;
    float snapScale = 0.1f;
    
    // Colors
    ImVec4 selectionColor = ImVec4(1.0f, 0.6f, 0.1f, 1.0f);
    ImVec4 gridColor = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    ImVec4 xAxisColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
    ImVec4 yAxisColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
    ImVec4 zAxisColor = ImVec4(0.2f, 0.2f, 1.0f, 1.0f);
    
    // Performance
    bool limitEditorFPS = true;
    int editorFPSLimit = 60;
};

class Editor {
public:
    Editor();
    ~Editor();
    
    // Lifecycle
    bool initialize(Renderer* renderer, Sanic::World* world);
    void shutdown();
    
    // Main loop
    void beginFrame();
    void update(float deltaTime);
    void render();
    void endFrame();
    
    // Mode control
    void setMode(EditorMode mode);
    EditorMode getMode() const { return mode_; }
    void play();
    void pause();
    void stop();
    
    // Panel management
    template<typename T>
    T* getPanel() {
        for (auto& panel : panels_) {
            if (auto* p = dynamic_cast<T*>(panel.get())) {
                return p;
            }
        }
        return nullptr;
    }
    
    void openPanel(const std::string& name);
    void closePanel(const std::string& name);
    
    // Core systems access
    Selection& getSelection() { return *selection_; }
    UndoSystem& getUndoSystem() { return *undoSystem_; }
    ShortcutManager& getShortcuts() { return *shortcuts_; }
    
    // Scene access
    Sanic::World* getWorld() { return world_; }
    Renderer* getRenderer() { return renderer_; }
    
    // Configuration
    EditorConfig& getConfig() { return config_; }
    
    // Notifications
    void showNotification(const std::string& message, float duration = 3.0f);
    
    // Static access
    static Editor* getInstance() { return instance_; }
    
private:
    void setupImGuiStyle();
    void setupDocking();
    void drawMainMenuBar();
    void drawToolbar();
    void drawStatusBar();
    void drawNotifications();
    void handleGlobalShortcuts();
    
    void createDefaultPanels();
    void saveLayout();
    void loadLayout();
    
    static Editor* instance_;
    
    Renderer* renderer_ = nullptr;
    Sanic::World* world_ = nullptr;
    
    EditorConfig config_;
    EditorMode mode_ = EditorMode::Edit;
    
    std::unique_ptr<Selection> selection_;
    std::unique_ptr<UndoSystem> undoSystem_;
    std::unique_ptr<ShortcutManager> shortcuts_;
    
    std::vector<std::unique_ptr<EditorWindow>> panels_;
    
    // Notification system
    struct Notification {
        std::string message;
        float timeRemaining;
    };
    std::vector<Notification> notifications_;
    
    bool showDemoWindow_ = false;
};

} // namespace Sanic::Editor
```

### Step 2: Selection System

Create `src/editor/core/Selection.h`:

```cpp
#pragma once
#include "../../engine/ECS.h"
#include <vector>
#include <functional>
#include <unordered_set>

namespace Sanic::Editor {

class Selection {
public:
    using SelectionChangedCallback = std::function<void()>;
    
    Selection() = default;
    
    // Selection operations
    void select(Entity entity);
    void addToSelection(Entity entity);
    void removeFromSelection(Entity entity);
    void toggleSelection(Entity entity);
    void selectAll(World& world);
    void clearSelection();
    
    // Multi-select with box
    void selectInRect(World& world, const glm::vec2& min, const glm::vec2& max,
                     const glm::mat4& viewProj, bool additive = false);
    
    // Query
    bool isSelected(Entity entity) const;
    bool hasSelection() const { return !selected_.empty(); }
    size_t getSelectionCount() const { return selected_.size(); }
    
    // Iteration
    const std::unordered_set<Entity>& getSelection() const { return selected_; }
    Entity getFirstSelected() const;
    Entity getLastSelected() const;
    
    // Focus (primary selection for inspector)
    Entity getFocused() const { return focused_; }
    void setFocused(Entity entity);
    
    // Callbacks
    void onSelectionChanged(SelectionChangedCallback callback);
    
    // Transform helpers for multi-selection
    glm::vec3 getSelectionCenter(World& world) const;
    glm::vec3 getSelectionBoundsMin(World& world) const;
    glm::vec3 getSelectionBoundsMax(World& world) const;
    
private:
    void notifyChanged();
    
    std::unordered_set<Entity> selected_;
    Entity focused_ = INVALID_ENTITY;
    std::vector<SelectionChangedCallback> callbacks_;
};

} // namespace Sanic::Editor
```

### Step 3: Undo System

Create `src/editor/core/UndoSystem.h`:

```cpp
#pragma once
#include "../../engine/ECS.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace Sanic::Editor {

// Base class for undoable actions
class UndoableAction {
public:
    virtual ~UndoableAction() = default;
    
    virtual void execute() = 0;     // Do/Redo
    virtual void undo() = 0;        // Undo
    
    virtual std::string getDescription() const = 0;
    
    // Can this action be merged with another of same type?
    virtual bool canMerge(const UndoableAction* other) const { return false; }
    virtual void merge(const UndoableAction* other) {}
};

// Transform change action
class TransformAction : public UndoableAction {
public:
    TransformAction(World* world, Entity entity, 
                   const Transform& oldTransform,
                   const Transform& newTransform);
    
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    
    bool canMerge(const UndoableAction* other) const override;
    void merge(const UndoableAction* other) override;
    
private:
    World* world_;
    Entity entity_;
    Transform oldTransform_;
    Transform newTransform_;
};

// Entity creation action
class CreateEntityAction : public UndoableAction {
public:
    CreateEntityAction(World* world, const std::string& name);
    
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    
    Entity getCreatedEntity() const { return entity_; }
    
private:
    World* world_;
    std::string name_;
    Entity entity_ = INVALID_ENTITY;
    std::vector<uint8_t> serializedData_;  // For redo
};

// Entity deletion action
class DeleteEntityAction : public UndoableAction {
public:
    DeleteEntityAction(World* world, Entity entity);
    
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    
private:
    World* world_;
    Entity entity_;
    std::vector<uint8_t> serializedData_;
};

// Component modification action
class ModifyComponentAction : public UndoableAction {
public:
    template<typename T>
    static std::unique_ptr<ModifyComponentAction> create(
        World* world, Entity entity, const T& oldValue, const T& newValue);
    
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    
private:
    World* world_;
    Entity entity_;
    std::string componentName_;
    std::vector<uint8_t> oldData_;
    std::vector<uint8_t> newData_;
    std::function<void(const std::vector<uint8_t>&)> applyFunc_;
};

// Compound action (multiple actions as one)
class CompoundAction : public UndoableAction {
public:
    CompoundAction(const std::string& description);
    
    void addAction(std::unique_ptr<UndoableAction> action);
    
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    
private:
    std::string description_;
    std::vector<std::unique_ptr<UndoableAction>> actions_;
};

// Undo/Redo stack manager
class UndoSystem {
public:
    UndoSystem(size_t maxHistorySize = 100);
    
    // Execute an action and add to history
    void execute(std::unique_ptr<UndoableAction> action);
    
    // Undo/Redo
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    void undo();
    void redo();
    
    // Get descriptions for UI
    std::string getUndoDescription() const;
    std::string getRedoDescription() const;
    std::vector<std::string> getUndoHistory(size_t maxItems = 10) const;
    std::vector<std::string> getRedoHistory(size_t maxItems = 10) const;
    
    // Clear all history
    void clear();
    
    // Mark clean (e.g., after save)
    void markClean();
    bool isDirty() const;
    
    // Begin/end batch (merges actions)
    void beginBatch(const std::string& description);
    void endBatch();
    bool isBatching() const { return currentBatch_ != nullptr; }
    
private:
    void trimHistory();
    
    std::vector<std::unique_ptr<UndoableAction>> undoStack_;
    std::vector<std::unique_ptr<UndoableAction>> redoStack_;
    
    size_t maxHistorySize_;
    size_t cleanIndex_ = 0;  // Index when last saved
    
    std::unique_ptr<CompoundAction> currentBatch_;
};

} // namespace Sanic::Editor
```

### Step 4: Viewport with Gizmos

Create `src/editor/viewport/Viewport.h`:

```cpp
#pragma once
#include "../EditorWindow.h"
#include "ViewportCamera.h"
#include "Gizmo.h"
#include "Grid.h"
#include "../../engine/VulkanContext.h"
#include <imgui.h>

namespace Sanic::Editor {

enum class ViewportTool {
    Select,
    Translate,
    Rotate,
    Scale,
    Universal    // All three
};

enum class TransformSpace {
    World,
    Local
};

enum class TransformPivot {
    Center,      // Center of selection
    Individual,  // Each object's pivot
    Cursor       // 3D cursor position
};

class Viewport : public EditorWindow {
public:
    Viewport();
    ~Viewport() override;
    
    // EditorWindow interface
    void initialize(Editor* editor) override;
    void shutdown() override;
    void update(float deltaTime) override;
    void draw() override;
    const char* getName() const override { return "Viewport"; }
    
    // Camera access
    ViewportCamera& getCamera() { return camera_; }
    
    // Tool settings
    void setTool(ViewportTool tool) { currentTool_ = tool; }
    ViewportTool getTool() const { return currentTool_; }
    
    void setTransformSpace(TransformSpace space) { transformSpace_ = space; }
    TransformSpace getTransformSpace() const { return transformSpace_; }
    
    void setTransformPivot(TransformPivot pivot) { transformPivot_ = pivot; }
    TransformPivot getTransformPivot() const { return transformPivot_; }
    
    // Snapping
    void setSnapEnabled(bool enabled) { snapEnabled_ = enabled; }
    bool isSnapEnabled() const { return snapEnabled_; }
    
    // Focus on selection
    void focusOnSelection();
    
    // Picking
    Entity pickEntity(const glm::vec2& screenPos);
    
    // 3D cursor
    void set3DCursor(const glm::vec3& position) { cursor3D_ = position; }
    glm::vec3 get3DCursor() const { return cursor3D_; }
    
private:
    void handleInput(float deltaTime);
    void handleMousePicking();
    void handleBoxSelection();
    void handleGizmoInteraction();
    
    void renderScene();
    void renderOverlays();
    void renderGrid();
    void renderGizmos();
    void renderSelectionOutlines();
    void renderDebugInfo();
    
    void drawToolbar();
    void drawViewportSettings();
    
    // Screen to world ray
    Ray screenToRay(const glm::vec2& screenPos);
    
    Editor* editor_ = nullptr;
    
    // Viewport state
    ImVec2 viewportPos_;
    ImVec2 viewportSize_;
    bool isFocused_ = false;
    bool isHovered_ = false;
    
    // Camera
    ViewportCamera camera_;
    
    // Tool state
    ViewportTool currentTool_ = ViewportTool::Translate;
    TransformSpace transformSpace_ = TransformSpace::World;
    TransformPivot transformPivot_ = TransformPivot::Center;
    bool snapEnabled_ = true;
    
    // Gizmo
    std::unique_ptr<Gizmo> gizmo_;
    bool gizmoActive_ = false;
    
    // Grid
    std::unique_ptr<Grid> grid_;
    bool showGrid_ = true;
    
    // Selection
    bool boxSelecting_ = false;
    glm::vec2 boxSelectStart_;
    glm::vec2 boxSelectEnd_;
    
    // 3D cursor
    glm::vec3 cursor3D_ = glm::vec3(0);
    bool showCursor3D_ = false;
    
    // Render target for viewport
    VkImage viewportImage_ = VK_NULL_HANDLE;
    VkDeviceMemory viewportMemory_ = VK_NULL_HANDLE;
    VkImageView viewportImageView_ = VK_NULL_HANDLE;
    VkSampler viewportSampler_ = VK_NULL_HANDLE;
    VkDescriptorSet viewportDescSet_ = VK_NULL_HANDLE;
    uint32_t viewportWidth_ = 0;
    uint32_t viewportHeight_ = 0;
    
    void createViewportRenderTarget(uint32_t width, uint32_t height);
    void destroyViewportRenderTarget();
};

} // namespace Sanic::Editor
```

Create `src/editor/viewport/Gizmo.h`:

```cpp
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "../../engine/ECS.h"

namespace Sanic::Editor {

enum class GizmoAxis {
    None,
    X, Y, Z,
    XY, XZ, YZ,
    XYZ,
    Screen,     // View-aligned plane
    Trackball   // Free rotation
};

enum class GizmoType {
    Translate,
    Rotate,
    Scale,
    Universal
};

struct GizmoResult {
    bool active = false;
    glm::vec3 deltaTranslation = glm::vec3(0);
    glm::quat deltaRotation = glm::quat();
    glm::vec3 deltaScale = glm::vec3(0);
    GizmoAxis axis = GizmoAxis::None;
};

class Gizmo {
public:
    Gizmo();
    
    // Set gizmo type and position
    void setType(GizmoType type) { type_ = type; }
    void setPosition(const glm::vec3& pos) { position_ = pos; }
    void setRotation(const glm::quat& rot) { rotation_ = rot; }
    void setScale(float scale) { size_ = scale; }
    
    // Interaction
    // Returns true if gizmo was clicked
    bool beginInteraction(const glm::vec2& mousePos, 
                          const glm::mat4& view,
                          const glm::mat4& proj,
                          const glm::vec2& viewportSize);
    
    // Update during drag
    GizmoResult updateInteraction(const glm::vec2& mousePos,
                                   const glm::mat4& view,
                                   const glm::mat4& proj,
                                   const glm::vec2& viewportSize);
    
    // End interaction
    void endInteraction();
    
    // Check if gizmo is being used
    bool isActive() const { return active_; }
    
    // Hit testing (for hover highlighting)
    GizmoAxis hitTest(const glm::vec2& mousePos,
                      const glm::mat4& view,
                      const glm::mat4& proj,
                      const glm::vec2& viewportSize);
    
    // Get hovered axis for rendering
    GizmoAxis getHoveredAxis() const { return hoveredAxis_; }
    void setHoveredAxis(GizmoAxis axis) { hoveredAxis_ = axis; }
    
    // Snapping
    void setTranslationSnap(float snap) { translateSnap_ = snap; }
    void setRotationSnap(float snap) { rotationSnap_ = snap; }
    void setScaleSnap(float snap) { scaleSnap_ = snap; }
    
    // Local/World space
    void setLocalSpace(bool local) { localSpace_ = local; }
    bool isLocalSpace() const { return localSpace_; }
    
    // Rendering
    void render(const glm::mat4& view, const glm::mat4& proj);
    
private:
    // Ray-primitive intersection helpers
    bool rayAxisIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                         GizmoAxis axis, float& outT);
    bool rayPlaneIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                          GizmoAxis plane, glm::vec3& outPoint);
    bool rayCircleIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                           GizmoAxis axis, float& outAngle);
    
    glm::vec3 getAxisVector(GizmoAxis axis) const;
    glm::vec3 getPlaneNormal(GizmoAxis plane) const;
    
    // Apply snapping
    glm::vec3 snapTranslation(const glm::vec3& delta);
    float snapRotation(float angle);
    glm::vec3 snapScale(const glm::vec3& delta);
    
    GizmoType type_ = GizmoType::Translate;
    glm::vec3 position_ = glm::vec3(0);
    glm::quat rotation_ = glm::quat();
    float size_ = 100.0f;
    
    bool active_ = false;
    GizmoAxis activeAxis_ = GizmoAxis::None;
    GizmoAxis hoveredAxis_ = GizmoAxis::None;
    
    // Interaction state
    glm::vec3 interactionStart_;
    glm::vec3 interactionPlanePoint_;
    glm::vec3 interactionPlaneNormal_;
    float interactionStartAngle_ = 0.0f;
    
    // Snapping
    float translateSnap_ = 1.0f;
    float rotationSnap_ = 15.0f;
    float scaleSnap_ = 0.1f;
    bool localSpace_ = false;
};

} // namespace Sanic::Editor
```

### Step 5: Hierarchy Panel

Create `src/editor/panels/HierarchyPanel.h`:

```cpp
#pragma once
#include "../EditorWindow.h"
#include "../../engine/ECS.h"
#include <string>

namespace Sanic::Editor {

class HierarchyPanel : public EditorWindow {
public:
    void initialize(Editor* editor) override;
    void update(float deltaTime) override;
    void draw() override;
    const char* getName() const override { return "Hierarchy"; }
    
private:
    void drawEntityNode(Entity entity, World& world);
    void handleDragDrop(Entity entity, World& world);
    void handleContextMenu(Entity entity, World& world);
    
    void createEntity();
    void duplicateEntity(Entity entity);
    void deleteEntity(Entity entity);
    
    Editor* editor_ = nullptr;
    
    // Search
    char searchBuffer_[256] = "";
    bool showInactive_ = true;
    
    // Drag state
    Entity draggedEntity_ = INVALID_ENTITY;
    Entity dropTarget_ = INVALID_ENTITY;
    enum class DropPosition { Above, Below, Inside };
    DropPosition dropPosition_ = DropPosition::Inside;
    
    // Rename state
    Entity renamingEntity_ = INVALID_ENTITY;
    char renameBuffer_[256] = "";
};

} // namespace Sanic::Editor
```

### Step 6: Inspector Panel

Create `src/editor/panels/InspectorPanel.h`:

```cpp
#pragma once
#include "../EditorWindow.h"
#include "../widgets/PropertyWidgets.h"
#include "../../engine/ECS.h"
#include <functional>
#include <unordered_map>
#include <typeindex>

namespace Sanic::Editor {

// Component editor interface
class IComponentEditor {
public:
    virtual ~IComponentEditor() = default;
    virtual void draw(Entity entity, World& world, UndoSystem& undo) = 0;
    virtual const char* getComponentName() const = 0;
    virtual const char* getIcon() const { return nullptr; }
    virtual bool canRemove() const { return true; }
};

// Typed component editor base
template<typename T>
class ComponentEditor : public IComponentEditor {
public:
    void draw(Entity entity, World& world, UndoSystem& undo) override {
        if (world.hasComponent<T>(entity)) {
            T& component = world.getComponent<T>(entity);
            drawComponent(entity, component, undo);
        }
    }
    
protected:
    virtual void drawComponent(Entity entity, T& component, UndoSystem& undo) = 0;
};

// Built-in component editors
class TransformEditor : public ComponentEditor<Transform> {
public:
    const char* getComponentName() const override { return "Transform"; }
    const char* getIcon() const override { return ICON_TRANSFORM; }
    bool canRemove() const override { return false; }
    
protected:
    void drawComponent(Entity entity, Transform& t, UndoSystem& undo) override;
};

class NameEditor : public ComponentEditor<Name> {
public:
    const char* getComponentName() const override { return "Name"; }
    bool canRemove() const override { return false; }
    
protected:
    void drawComponent(Entity entity, Name& n, UndoSystem& undo) override;
};

class MeshRendererEditor : public ComponentEditor<MeshRenderer> {
public:
    const char* getComponentName() const override { return "Mesh Renderer"; }
    
protected:
    void drawComponent(Entity entity, MeshRenderer& m, UndoSystem& undo) override;
};

class LightEditor : public ComponentEditor<Light> {
public:
    const char* getComponentName() const override { return "Light"; }
    
protected:
    void drawComponent(Entity entity, Light& l, UndoSystem& undo) override;
};

// Inspector panel
class InspectorPanel : public EditorWindow {
public:
    void initialize(Editor* editor) override;
    void update(float deltaTime) override;
    void draw() override;
    const char* getName() const override { return "Inspector"; }
    
    // Register custom component editors
    template<typename T>
    void registerEditor(std::unique_ptr<IComponentEditor> editor) {
        ComponentTypeId id = ComponentRegistry::getInstance().getTypeId<T>();
        editors_[id] = std::move(editor);
    }
    
private:
    void drawEntityInspector(Entity entity);
    void drawMultiEntityInspector(const std::unordered_set<Entity>& entities);
    void drawAddComponentButton(Entity entity);
    
    Editor* editor_ = nullptr;
    std::unordered_map<ComponentTypeId, std::unique_ptr<IComponentEditor>> editors_;
    
    // Add component popup state
    bool showAddComponentPopup_ = false;
    char addComponentFilter_[128] = "";
};

} // namespace Sanic::Editor
```

---

## Task 17: Asset Browser

Create `src/editor/panels/AssetBrowserPanel.h`:

```cpp
#pragma once
#include "../EditorWindow.h"
#include <filesystem>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Sanic::Editor {

enum class AssetType {
    Unknown,
    Folder,
    Mesh,
    Texture,
    Material,
    Audio,
    Scene,
    Prefab,
    Script,
    Shader
};

struct AssetEntry {
    std::filesystem::path path;
    std::string name;
    AssetType type;
    uint64_t size;
    std::filesystem::file_time_type lastModified;
    
    // Thumbnail
    VkDescriptorSet thumbnail = VK_NULL_HANDLE;
    bool thumbnailLoaded = false;
};

class AssetBrowserPanel : public EditorWindow {
public:
    void initialize(Editor* editor) override;
    void shutdown() override;
    void update(float deltaTime) override;
    void draw() override;
    const char* getName() const override { return "Assets"; }
    
    // Navigation
    void setRootPath(const std::filesystem::path& path);
    void navigateTo(const std::filesystem::path& path);
    void navigateUp();
    void refresh();
    
    // Selection
    const AssetEntry* getSelectedAsset() const;
    std::vector<const AssetEntry*> getSelectedAssets() const;
    
    // Import
    void importAsset(const std::filesystem::path& sourcePath);
    void importAssets(const std::vector<std::filesystem::path>& sourcePaths);
    
private:
    void drawNavigationBar();
    void drawFolderTree();
    void drawContentView();
    void drawAssetGrid();
    void drawAssetList();
    void drawAssetContextMenu(const AssetEntry& entry);
    void drawImportDialog();
    
    void scanDirectory(const std::filesystem::path& path);
    void loadThumbnails();
    AssetType detectAssetType(const std::filesystem::path& path);
    VkDescriptorSet generateThumbnail(const AssetEntry& entry);
    
    void handleDragDrop(const AssetEntry& entry);
    void handleDoubleClick(const AssetEntry& entry);
    
    Editor* editor_ = nullptr;
    
    // Navigation
    std::filesystem::path rootPath_;
    std::filesystem::path currentPath_;
    std::vector<std::filesystem::path> pathHistory_;
    int historyIndex_ = 0;
    
    // Content
    std::vector<AssetEntry> entries_;
    std::unordered_set<std::string> selectedPaths_;
    
    // View settings
    enum class ViewMode { Grid, List };
    ViewMode viewMode_ = ViewMode::Grid;
    float thumbnailSize_ = 80.0f;
    char searchFilter_[256] = "";
    
    // Import
    bool showImportDialog_ = false;
    std::vector<std::filesystem::path> pendingImports_;
    
    // Thumbnails
    std::queue<const AssetEntry*> thumbnailQueue_;
    std::unordered_map<std::string, VkDescriptorSet> thumbnailCache_;
    
    // Default icons
    VkDescriptorSet folderIcon_ = VK_NULL_HANDLE;
    VkDescriptorSet meshIcon_ = VK_NULL_HANDLE;
    VkDescriptorSet textureIcon_ = VK_NULL_HANDLE;
    VkDescriptorSet materialIcon_ = VK_NULL_HANDLE;
    VkDescriptorSet unknownIcon_ = VK_NULL_HANDLE;
};

} // namespace Sanic::Editor
```

---

## Task 18: Preview Systems

Create `src/editor/preview/PreviewRenderer.h`:

```cpp
#pragma once
#include "../../engine/VulkanContext.h"
#include "../../engine/Camera.h"
#include <glm/glm.hpp>
#include <memory>

namespace Sanic::Editor {

class PreviewRenderer {
public:
    PreviewRenderer();
    ~PreviewRenderer();
    
    bool initialize(VulkanContext* context, uint32_t width, uint32_t height);
    void shutdown();
    void resize(uint32_t width, uint32_t height);
    
    // Render to internal target
    void beginRender();
    void endRender();
    
    // Get result for ImGui display
    VkDescriptorSet getOutputDescriptor() const { return outputDescriptor_; }
    
    // Camera control
    void setCamera(const glm::vec3& position, const glm::vec3& target);
    void orbit(float deltaX, float deltaY);
    void zoom(float delta);
    void pan(float deltaX, float deltaY);
    
    // Lighting
    void setEnvironmentMap(const std::string& path);
    void setLightDirection(const glm::vec3& dir);
    void setLightColor(const glm::vec3& color);
    
protected:
    VulkanContext* context_ = nullptr;
    
    // Render target
    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
    VkImageView colorView_ = VK_NULL_HANDLE;
    
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSet outputDescriptor_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    
    uint32_t width_ = 256;
    uint32_t height_ = 256;
    
    // Camera
    glm::vec3 cameraPosition_ = glm::vec3(0, 0, 3);
    glm::vec3 cameraTarget_ = glm::vec3(0);
    float cameraDistance_ = 3.0f;
    float cameraYaw_ = 0.0f;
    float cameraPitch_ = 0.0f;
    
    // Lighting
    glm::vec3 lightDirection_ = glm::normalize(glm::vec3(1, 1, 1));
    glm::vec3 lightColor_ = glm::vec3(1);
};

// Mesh preview
class MeshPreview : public PreviewRenderer {
public:
    void setMesh(class Mesh* mesh);
    void setMaterial(class Material* material);
    void render();
    
private:
    Mesh* mesh_ = nullptr;
    Material* material_ = nullptr;
    VkDescriptorSet meshDescriptor_ = VK_NULL_HANDLE;
};

// Material preview
class MaterialPreview : public PreviewRenderer {
public:
    enum class PreviewShape { Sphere, Cube, Plane, Cylinder };
    
    void setMaterial(class Material* material);
    void setShape(PreviewShape shape);
    void render();
    
private:
    Material* material_ = nullptr;
    PreviewShape shape_ = PreviewShape::Sphere;
    std::unique_ptr<Mesh> sphereMesh_;
    std::unique_ptr<Mesh> cubeMesh_;
    std::unique_ptr<Mesh> planeMesh_;
    std::unique_ptr<Mesh> cylinderMesh_;
};

// Texture preview
class TexturePreview {
public:
    void setTexture(VkImageView imageView);
    void draw(float width, float height);
    
    // View settings
    void setChannel(int channel);  // 0=RGB, 1=R, 2=G, 3=B, 4=A
    void setMipLevel(int level);
    void setExposure(float exposure);
    
private:
    VkImageView textureView_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_ = VK_NULL_HANDLE;
    int channel_ = 0;
    int mipLevel_ = 0;
    float exposure_ = 1.0f;
};

} // namespace Sanic::Editor
```

---

## Unreal Engine Reference Files

### Editor Framework
```
Engine/Source/Editor/UnrealEd/
├── Private/
│   ├── Editor.cpp                      # Main editor class
│   ├── EditorViewportClient.cpp        # Viewport handling
│   ├── EditorModeManager.cpp           # Tool modes
│   └── LevelEditorViewport.cpp         # Level viewport
└── Public/
    ├── Editor.h
    ├── EditorViewportClient.h
    └── EditorModeManager.h
```

**Key files to study**:

1. **`EditorViewportClient.cpp`**
   - Camera controls implementation
   - Mouse input handling
   - View matrix computation

2. **`LevelEditorViewport.cpp`**
   - Viewport widget setup
   - Scene rendering to viewport
   - Overlay rendering

### Selection System
```
Engine/Source/Editor/UnrealEd/
├── Private/
│   ├── EditorActorFolders.cpp
│   └── Selection.cpp
└── Public/
    └── Selection.h
```

### Transform Gizmos
```
Engine/Source/Editor/UnrealEd/
├── Private/
│   ├── UnrealEdGizmos.cpp              # Gizmo rendering
│   ├── EditorModeManager.cpp           # Mode handling
│   └── FEditorViewportClient.cpp       # Hit testing
└── Public/
    └── UnrealWidget.h                  # FWidget (gizmo base)
```

**Key patterns**:

1. **Gizmo Rendering** - `UnrealEdGizmos.cpp`
   - Axis arrows/cubes/circles
   - Plane squares
   - Hover highlighting

2. **Hit Testing** - `FEditorViewportClient::InputWidgetDelta()`
   - Ray-axis intersection
   - Plane intersection for translation
   - Arc intersection for rotation

### Property Editor
```
Engine/Source/Editor/PropertyEditor/
├── Private/
│   ├── PropertyEditorModule.cpp
│   ├── SDetailsView.cpp                # Main property panel
│   ├── SPropertyEditor*.cpp            # Per-type editors
│   └── PropertyCustomization*.cpp      # Custom layouts
└── Public/
    ├── IPropertyTable.h
    ├── PropertyCustomization*.h
    └── DetailCategoryBuilder.h
```

**Key patterns**:

1. **`SDetailsView.cpp`** - Property panel
   - Category organization
   - Property row widgets
   - Edit condition handling

2. **`PropertyCustomization*.cpp`** - Custom editors
   - Vector3 editor
   - Color picker
   - Asset reference picker

### Asset Browser (Content Browser)
```
Engine/Source/Editor/ContentBrowser/
├── Private/
│   ├── ContentBrowserModule.cpp
│   ├── SContentBrowser.cpp             # Main widget
│   ├── SAssetView.cpp                  # Asset grid/list
│   ├── SPathView.cpp                   # Folder tree
│   └── AssetContextMenu.cpp            # Right-click menu
└── Public/
    └── ContentBrowserModule.h
```

**Key patterns**:

1. **`SContentBrowser.cpp`**
   - Folder navigation
   - Search/filter
   - Drag-drop handling

2. **`SAssetView.cpp`**
   - Grid/list view modes
   - Thumbnail loading
   - Selection handling

### Undo System
```
Engine/Source/Runtime/Core/
├── Private/
│   └── Misc/
│       └── TransactionHistory.cpp
└── Public/
    └── Misc/
        ├── ITransaction.h
        └── TransactionContext.h
```

---

## Implementation Checklist

### Week 1-2: Core Framework
- [ ] Set up ImGui with docking
- [ ] Implement EditorWindow base class
- [ ] Implement Editor main class
- [ ] Implement basic layout saving/loading
- [ ] Add main menu bar

### Week 3-4: Viewport Basics
- [ ] Create viewport render target
- [ ] Implement ViewportCamera (orbit, FPS, pan)
- [ ] Implement Grid rendering
- [ ] Display scene in viewport
- [ ] Handle viewport resize

### Week 5-6: Selection & Gizmos
- [ ] Implement Selection system
- [ ] Implement mouse picking
- [ ] Implement box selection
- [ ] Implement Translate gizmo
- [ ] Implement Rotate gizmo
- [ ] Implement Scale gizmo
- [ ] Add snapping support

### Week 7-8: Panels
- [ ] Implement HierarchyPanel
- [ ] Implement InspectorPanel
- [ ] Add component editors
- [ ] Implement UndoSystem
- [ ] Connect undo to transform changes

### Week 9-10: Asset Browser
- [ ] Implement folder navigation
- [ ] Implement grid/list views
- [ ] Implement thumbnail generation
- [ ] Add drag-drop to viewport
- [ ] Add import workflow

### Week 11-12: Preview Systems
- [ ] Implement PreviewRenderer base
- [ ] Implement MeshPreview
- [ ] Implement MaterialPreview
- [ ] Implement TexturePreview
- [ ] Add preview to Inspector

---

## Dependencies

### Required Libraries
```cmake
# ImGui with docking
# Use docking branch: https://github.com/ocornut/imgui/tree/docking
add_subdirectory(external/imgui)

# ImGui Vulkan backend
# Part of ImGui examples

# ImGuizmo for transform gizmos (optional, or implement custom)
# https://github.com/CedricGuillemet/ImGuizmo
add_subdirectory(external/ImGuizmo)

# File dialogs
# https://github.com/btzy/nativefiledialog-extended
find_package(nfd CONFIG REQUIRED)

# Icon font
# https://github.com/juliettef/IconFontCppHeaders
# Download FontAwesome or MaterialDesignIcons
```

### Integration Points
- Coordinate with **Developer 1** on RHI for viewport rendering
- Coordinate with **Developer 2** on Material Editor integration
- Use existing ECS from engine
- Use existing Scene Serialization

---

## Testing Strategy

1. **Viewport**: Navigate around test scene, verify FPS
2. **Gizmos**: Transform objects, verify accuracy
3. **Selection**: Pick objects, box select, verify correct entities
4. **Undo**: Perform actions, undo/redo, verify state restoration
5. **Hierarchy**: Create/delete/reparent entities
6. **Inspector**: Modify properties, verify changes
7. **Asset Browser**: Import assets, drag to scene
8. **Previews**: Verify mesh/material/texture display

---

## Resources

### ImGui
- [ImGui Docking Branch](https://github.com/ocornut/imgui/tree/docking)
- [ImGui Wiki](https://github.com/ocornut/imgui/wiki)
- [ImGui Demo](https://github.com/ocornut/imgui/blob/master/imgui_demo.cpp)

### Gizmos
- [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) - Reference implementation
- [tinygizmo](https://github.com/ddiakopoulos/tinygizmo) - Simpler alternative

### Editor Design
- [Unity Editor Architecture](https://docs.unity3d.com/Manual/ExtendingTheEditor.html)
- [Godot Editor](https://github.com/godotengine/godot/tree/master/editor)

### Icons
- [Font Awesome](https://fontawesome.com/) - Icon set
- [Material Design Icons](https://materialdesignicons.com/) - Alternative

