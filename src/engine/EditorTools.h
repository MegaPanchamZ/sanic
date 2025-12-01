/**
 * EditorTools.h
 * 
 * Editor Extensions and Debug Tools
 * 
 * Systems included:
 * - Animation Blueprint Editor (visual state machine)
 * - AI Debugger (behavior tree visualization)
 * - Combat Designer (hitbox/combo editor)
 * - Property Inspector
 * - Asset Browser integration
 * 
 * Reference:
 *   Engine/Source/Editor/Persona/
 *   Engine/Source/Editor/BehaviorTreeEditor/
 */

#pragma once

#include "ECS.h"
#include "Animation.h"
#include "BehaviorTree.h"
#include "CombatSystem.h"
#include "NavigationSystem.h"
#include <glm/glm.hpp>
#include <imgui.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace Sanic {
namespace Editor {

// Forward declarations
class EditorWindow;
class PropertyEditor;

// ============================================================================
// NODE GRAPH EDITOR BASE
// ============================================================================

/**
 * Visual connection between nodes
 */
struct NodeConnection {
    uint32_t sourceNodeId;
    uint32_t sourceSlotIndex;
    uint32_t targetNodeId;
    uint32_t targetSlotIndex;
};

/**
 * Node slot (input/output)
 */
struct NodeSlot {
    std::string name;
    std::string type;
    bool isInput;
    glm::vec4 color = glm::vec4(1, 1, 1, 1);
    
    // Connection state
    bool isConnected = false;
    std::vector<NodeConnection*> connections;
};

/**
 * Base visual node
 */
struct VisualNode {
    uint32_t id;
    std::string name;
    std::string type;
    glm::vec2 position;
    glm::vec2 size = glm::vec2(150, 100);
    glm::vec4 color = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
    
    std::vector<NodeSlot> inputs;
    std::vector<NodeSlot> outputs;
    
    // Selection state
    bool isSelected = false;
    bool isHovered = false;
    
    // Custom data
    std::unordered_map<std::string, std::string> properties;
    
    virtual void drawContent() {}
    virtual void onPropertyChanged(const std::string& name) {}
};

/**
 * Base node graph editor
 */
class NodeGraphEditor {
public:
    NodeGraphEditor(const std::string& name);
    virtual ~NodeGraphEditor() = default;
    
    /**
     * Draw the node graph editor
     */
    virtual void draw();
    
    /**
     * Add a node
     */
    void addNode(std::shared_ptr<VisualNode> node);
    
    /**
     * Remove a node
     */
    void removeNode(uint32_t nodeId);
    
    /**
     * Get selected nodes
     */
    std::vector<VisualNode*> getSelectedNodes();
    
    /**
     * Clear selection
     */
    void clearSelection();
    
    /**
     * Connect nodes
     */
    bool connect(uint32_t sourceNode, uint32_t sourceSlot,
                 uint32_t targetNode, uint32_t targetSlot);
    
    /**
     * Disconnect nodes
     */
    void disconnect(uint32_t sourceNode, uint32_t sourceSlot,
                    uint32_t targetNode, uint32_t targetSlot);
    
    // Callbacks
    using NodeCreatedCallback = std::function<void(VisualNode*)>;
    using NodeDeletedCallback = std::function<void(uint32_t)>;
    using ConnectionCallback = std::function<void(const NodeConnection&)>;
    
    void setOnNodeCreated(NodeCreatedCallback cb) { onNodeCreated_ = cb; }
    void setOnNodeDeleted(NodeDeletedCallback cb) { onNodeDeleted_ = cb; }
    void setOnConnected(ConnectionCallback cb) { onConnected_ = cb; }
    void setOnDisconnected(ConnectionCallback cb) { onDisconnected_ = cb; }
    
protected:
    virtual void drawNode(VisualNode* node);
    virtual void drawConnections();
    virtual void drawContextMenu();
    virtual void handleInput();
    
    std::string name_;
    std::unordered_map<uint32_t, std::shared_ptr<VisualNode>> nodes_;
    std::vector<NodeConnection> connections_;
    
    // View state
    glm::vec2 viewOffset_ = glm::vec2(0);
    float viewZoom_ = 1.0f;
    
    // Interaction state
    bool isDragging_ = false;
    bool isConnecting_ = false;
    uint32_t dragNodeId_ = 0;
    uint32_t connectSourceNode_ = 0;
    uint32_t connectSourceSlot_ = 0;
    glm::vec2 connectEndPos_;
    
    // Selection
    std::unordered_set<uint32_t> selectedNodes_;
    glm::vec2 selectionStart_;
    bool isBoxSelecting_ = false;
    
    // ID counter
    uint32_t nextNodeId_ = 1;
    
    // Callbacks
    NodeCreatedCallback onNodeCreated_;
    NodeDeletedCallback onNodeDeleted_;
    ConnectionCallback onConnected_;
    ConnectionCallback onDisconnected_;
};

// ============================================================================
// ANIMATION BLUEPRINT EDITOR
// ============================================================================

/**
 * Animation state node
 */
struct AnimStateNode : public VisualNode {
    std::string animationClip;
    float playbackSpeed = 1.0f;
    bool looping = true;
    
    // Blend space data
    bool isBlendSpace = false;
    std::vector<std::pair<glm::vec2, std::string>> blendPoints;
    
    void drawContent() override;
};

/**
 * Animation transition node
 */
struct AnimTransitionNode : public VisualNode {
    std::string conditionExpression;
    float transitionDuration = 0.2f;
    std::string blendMode = "Linear";
    
    void drawContent() override;
};

/**
 * Animation state machine visual editor
 */
class AnimationBlueprintEditor : public NodeGraphEditor {
public:
    AnimationBlueprintEditor();
    
    void draw() override;
    
    /**
     * Load animation blueprint
     */
    void loadBlueprint(const std::string& path);
    
    /**
     * Save animation blueprint
     */
    void saveBlueprint(const std::string& path);
    
    /**
     * Create new state
     */
    AnimStateNode* createState(const std::string& name, const glm::vec2& position);
    
    /**
     * Create transition
     */
    AnimTransitionNode* createTransition(uint32_t fromState, uint32_t toState);
    
    /**
     * Set preview skeleton
     */
    void setPreviewSkeleton(const std::string& skeletonPath);
    
    /**
     * Preview animation
     */
    void previewState(uint32_t stateId);
    
private:
    void drawToolbar();
    void drawStateList();
    void drawPreviewPanel();
    void drawPropertyPanel();
    void drawContextMenu() override;
    
    // Preview
    std::string previewSkeletonPath_;
    uint32_t previewingState_ = 0;
    float previewTime_ = 0.0f;
    bool isPreviewPlaying_ = false;
    
    // Asset browser integration
    std::vector<std::string> availableAnimations_;
    std::string searchFilter_;
    
    // Current file
    std::string currentFilePath_;
    bool isDirty_ = false;
};

// ============================================================================
// AI DEBUGGER
// ============================================================================

/**
 * Behavior tree debug node visualization
 */
struct BTDebugNode {
    uint32_t id;
    std::string name;
    std::string type;
    BTNodeStatus lastStatus = BTNodeStatus::Ready;
    float lastExecutionTime = 0.0f;
    int executionCount = 0;
    
    glm::vec2 position;
    glm::vec2 size;
    bool isExpanded = true;
    
    std::vector<uint32_t> childIds;
    uint32_t parentId = 0;
};

/**
 * Blackboard variable display
 */
struct BlackboardVariable {
    std::string name;
    std::string type;
    std::string value;
    bool isModified = false;
};

/**
 * AI Debugger - visualizes behavior trees and navigation
 */
class AIDebugger : public EditorWindow {
public:
    AIDebugger();
    
    void draw() override;
    
    /**
     * Set target entity for debugging
     */
    void setTarget(Entity entity);
    
    /**
     * Set target behavior tree
     */
    void setBehaviorTree(BehaviorTreeAsset* tree);
    
    /**
     * Update debug visualization
     */
    void update(float deltaTime);
    
    /**
     * Enable/disable world visualization
     */
    void setWorldVisualizationEnabled(bool enabled) { showWorldVis_ = enabled; }
    
private:
    void drawBehaviorTreeView();
    void drawBlackboardView();
    void drawNavigationView();
    void drawAIStateView();
    
    void drawTreeNode(BTDebugNode& node, int depth);
    void updateBTVisualization();
    void buildDebugTree(BTNode* node, uint32_t parentId);
    
    // Draw world overlays
    void drawNavigationDebug();
    void drawSensorDebug();
    void drawGoalDebug();
    
    Entity targetEntity_;
    BehaviorTreeAsset* behaviorTree_ = nullptr;
    Blackboard* blackboard_ = nullptr;
    
    // BT visualization
    std::unordered_map<uint32_t, BTDebugNode> debugNodes_;
    uint32_t rootNodeId_ = 0;
    uint32_t nextDebugId_ = 1;
    
    // Blackboard display
    std::vector<BlackboardVariable> blackboardVars_;
    std::string bbSearchFilter_;
    
    // Navigation display
    bool showNavMesh_ = true;
    bool showCurrentPath_ = true;
    bool showCrowdAgents_ = false;
    
    // Visualization options
    bool showWorldVis_ = true;
    bool showSensors_ = true;
    bool showGoals_ = true;
    bool pauseOnCondition_ = false;
    std::string pauseCondition_;
    
    // History
    std::vector<std::pair<float, std::string>> executionHistory_;
    int maxHistorySize_ = 100;
};

// ============================================================================
// COMBAT DESIGNER
// ============================================================================

/**
 * Hitbox editor visualization
 */
struct HitboxVisual {
    uint32_t id;
    std::string name;
    HitboxType type;
    glm::vec3 localPosition;
    glm::vec3 halfExtents;
    glm::quat rotation;
    glm::vec4 color;
    
    bool isSelected = false;
    bool isVisible = true;
    
    // Frame data
    int startFrame = 0;
    int endFrame = 10;
    bool isActive = false;
};

/**
 * Combo node visualization
 */
struct ComboNode {
    uint32_t id;
    std::string attackName;
    std::string animationClip;
    glm::vec2 position;
    
    // Timing
    int hitFrame = 5;
    int recoveryFrames = 10;
    int cancelWindowStart = 8;
    int cancelWindowEnd = 15;
    
    // Properties
    float damage = 10.0f;
    float knockback = 5.0f;
    glm::vec3 knockbackDirection = glm::vec3(1, 0.3f, 0);
    
    bool isSelected = false;
};

/**
 * Combat Designer - hitbox and combo editor
 */
class CombatDesigner : public EditorWindow {
public:
    CombatDesigner();
    
    void draw() override;
    
    /**
     * Load combat data
     */
    void loadCombatData(const std::string& path);
    
    /**
     * Save combat data
     */
    void saveCombatData(const std::string& path);
    
    /**
     * Set preview skeleton
     */
    void setPreviewSkeleton(const std::string& skeletonPath);
    
    /**
     * Add hitbox
     */
    HitboxVisual* addHitbox(const std::string& name, HitboxType type);
    
    /**
     * Add combo node
     */
    ComboNode* addComboNode(const std::string& attackName);
    
private:
    void drawToolbar();
    void drawHitboxPanel();
    void drawComboGraphPanel();
    void drawTimelinePanel();
    void drawPreviewPanel();
    void drawPropertyPanel();
    
    void drawHitboxGizmos();
    void drawComboGraph();
    void drawTimeline();
    
    void updatePreview(float deltaTime);
    
    // Mode
    enum class EditorMode {
        Hitbox,
        Combo,
        Timeline
    };
    EditorMode mode_ = EditorMode::Hitbox;
    
    // Hitboxes
    std::vector<HitboxVisual> hitboxes_;
    uint32_t selectedHitboxId_ = 0;
    uint32_t nextHitboxId_ = 1;
    
    // Gizmo state
    enum class GizmoMode { Translate, Rotate, Scale };
    GizmoMode gizmoMode_ = GizmoMode::Translate;
    bool isGizmoDragging_ = false;
    glm::vec3 gizmoDragStart_;
    
    // Combo graph
    std::vector<ComboNode> comboNodes_;
    std::vector<std::pair<uint32_t, uint32_t>> comboLinks_;
    uint32_t selectedComboNodeId_ = 0;
    uint32_t nextComboNodeId_ = 1;
    
    // Timeline
    int currentFrame_ = 0;
    int totalFrames_ = 60;
    float frameRate_ = 30.0f;
    bool isPlaying_ = false;
    float playbackTime_ = 0.0f;
    
    // Preview
    std::string previewSkeletonPath_;
    std::string previewAnimationPath_;
    
    // File
    std::string currentFilePath_;
    bool isDirty_ = false;
};

// ============================================================================
// PROPERTY EDITOR
// ============================================================================

/**
 * Property types
 */
enum class PropertyType {
    Bool,
    Int,
    Float,
    String,
    Vec2,
    Vec3,
    Vec4,
    Color,
    Enum,
    Asset,
    Object
};

/**
 * Property definition
 */
struct PropertyDef {
    std::string name;
    std::string displayName;
    PropertyType type;
    
    // Value access
    std::function<std::string()> getter;
    std::function<void(const std::string&)> setter;
    
    // Constraints
    float minValue = 0.0f;
    float maxValue = 100.0f;
    std::vector<std::string> enumOptions;
    std::string assetFilter;
    
    // Display
    std::string category;
    std::string tooltip;
    bool isReadOnly = false;
    bool isAdvanced = false;
};

/**
 * Generic property editor panel
 */
class PropertyEditor : public EditorWindow {
public:
    PropertyEditor();
    
    void draw() override;
    
    /**
     * Set properties to display
     */
    void setProperties(const std::vector<PropertyDef>& properties);
    
    /**
     * Clear properties
     */
    void clear();
    
    /**
     * Add single property
     */
    void addProperty(const PropertyDef& property);
    
    /**
     * Show advanced properties
     */
    void setShowAdvanced(bool show) { showAdvanced_ = show; }
    
private:
    void drawProperty(PropertyDef& prop);
    void drawCategory(const std::string& name, std::vector<PropertyDef*>& props);
    
    std::vector<PropertyDef> properties_;
    std::string searchFilter_;
    bool showAdvanced_ = false;
    
    // Categorized properties
    std::unordered_map<std::string, std::vector<PropertyDef*>> categories_;
    std::unordered_set<std::string> collapsedCategories_;
};

// ============================================================================
// EDITOR WINDOW BASE
// ============================================================================

/**
 * Base class for editor windows
 */
class EditorWindow {
public:
    EditorWindow(const std::string& name) : name_(name) {}
    virtual ~EditorWindow() = default;
    
    virtual void draw() = 0;
    
    const std::string& getName() const { return name_; }
    bool isOpen() const { return isOpen_; }
    void setOpen(bool open) { isOpen_ = open; }
    
protected:
    std::string name_;
    bool isOpen_ = true;
};

// ============================================================================
// EDITOR MANAGER
// ============================================================================

/**
 * Manages all editor windows
 */
class EditorManager {
public:
    static EditorManager& getInstance();
    
    /**
     * Initialize editor
     */
    void init();
    
    /**
     * Shutdown editor
     */
    void shutdown();
    
    /**
     * Draw all editor windows
     */
    void draw();
    
    /**
     * Register window
     */
    template<typename T>
    T* registerWindow() {
        auto window = std::make_unique<T>();
        T* ptr = window.get();
        windows_[window->getName()] = std::move(window);
        return ptr;
    }
    
    /**
     * Get window
     */
    template<typename T>
    T* getWindow(const std::string& name) {
        auto it = windows_.find(name);
        if (it != windows_.end()) {
            return dynamic_cast<T*>(it->second.get());
        }
        return nullptr;
    }
    
    /**
     * Toggle window visibility
     */
    void toggleWindow(const std::string& name);
    
    // Specific editors
    AnimationBlueprintEditor* getAnimationEditor() { return animEditor_; }
    AIDebugger* getAIDebugger() { return aiDebugger_; }
    CombatDesigner* getCombatDesigner() { return combatDesigner_; }
    PropertyEditor* getPropertyEditor() { return propertyEditor_; }
    
private:
    EditorManager() = default;
    
    std::unordered_map<std::string, std::unique_ptr<EditorWindow>> windows_;
    
    // Quick access to common editors
    AnimationBlueprintEditor* animEditor_ = nullptr;
    AIDebugger* aiDebugger_ = nullptr;
    CombatDesigner* combatDesigner_ = nullptr;
    PropertyEditor* propertyEditor_ = nullptr;
    
    bool showDemoWindow_ = false;
};

// ============================================================================
// DEBUG DRAW HELPERS
// ============================================================================

/**
 * Debug drawing utilities
 */
class DebugDraw {
public:
    static void drawLine(const glm::vec3& start, const glm::vec3& end, 
                         const glm::vec4& color, float thickness = 1.0f);
    
    static void drawBox(const glm::vec3& center, const glm::vec3& halfExtents,
                        const glm::quat& rotation, const glm::vec4& color, bool filled = false);
    
    static void drawSphere(const glm::vec3& center, float radius, 
                           const glm::vec4& color, bool filled = false);
    
    static void drawCapsule(const glm::vec3& p1, const glm::vec3& p2, float radius,
                            const glm::vec4& color, bool filled = false);
    
    static void drawArrow(const glm::vec3& start, const glm::vec3& end,
                          const glm::vec4& color, float headSize = 0.2f);
    
    static void drawText(const glm::vec3& worldPos, const std::string& text,
                         const glm::vec4& color);
    
    static void drawGrid(const glm::vec3& center, float size, int divisions,
                         const glm::vec4& color);
    
    static void drawFrustum(const glm::mat4& viewProj, const glm::vec4& color);
    
    static void drawPath(const std::vector<glm::vec3>& points, const glm::vec4& color);
    
    /**
     * Submit all debug draws
     */
    static void flush();
    
private:
    struct DebugLine {
        glm::vec3 start, end;
        glm::vec4 color;
        float thickness;
    };
    
    static std::vector<DebugLine> lines_;
};

} // namespace Editor
} // namespace Sanic
