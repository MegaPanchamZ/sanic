/**
 * MaterialEditor.h
 * 
 * ImGui-based visual node graph editor for materials.
 * Uses imnodes library for node graph rendering and interaction.
 */

#pragma once

#include "MaterialGraph.h"
#include "MaterialCompiler.h"
#include <functional>
#include <memory>
#include <string>
#include <optional>

// Forward declare imnodes types
namespace ImNodes {
    struct EditorContext;
}

namespace Sanic {

// Forward declarations
class VulkanContext;

/**
 * Material preview settings
 */
struct MaterialPreviewSettings {
    enum class PreviewShape {
        Sphere,
        Cube,
        Plane,
        Cylinder,
        Custom
    };
    
    PreviewShape shape = PreviewShape::Sphere;
    float rotationSpeed = 0.5f;
    bool autoRotate = true;
    glm::vec3 lightDirection = glm::normalize(glm::vec3(1.0f, 1.0f, 0.5f));
    float exposure = 1.0f;
    std::string customMeshPath;
};

/**
 * Node search/creation popup state
 */
struct NodeCreationPopup {
    bool isOpen = false;
    glm::vec2 createPosition;
    std::string searchQuery;
    std::vector<std::string> filteredNodes;
    int selectedIndex = 0;
    
    // If connecting from an existing pin
    bool fromPin = false;
    uint64_t fromNodeId = 0;
    uint32_t fromPinIndex = 0;
    bool fromIsOutput = false;
};

/**
 * Editor selection state
 */
struct EditorSelection {
    std::vector<uint64_t> selectedNodes;
    std::vector<uint64_t> selectedConnections;
    
    void clear() {
        selectedNodes.clear();
        selectedConnections.clear();
    }
    
    bool hasSelection() const {
        return !selectedNodes.empty() || !selectedConnections.empty();
    }
};

/**
 * Clipboard for copy/paste
 */
struct MaterialClipboard {
    std::vector<nlohmann::json> nodes;
    std::vector<nlohmann::json> connections;
    glm::vec2 centerOffset;
    bool hasContent = false;
};

/**
 * Undo/redo action
 */
struct MaterialEditorAction {
    enum class Type {
        AddNode,
        RemoveNode,
        AddConnection,
        RemoveConnection,
        MoveNodes,
        ModifyNode,
        MultipleActions  // For grouping
    };
    
    Type type;
    nlohmann::json data;  // State data for undo/redo
};

/**
 * Material editor - visual node graph editor
 */
class MaterialEditor {
public:
    MaterialEditor();
    ~MaterialEditor();
    
    // Lifecycle
    void initialize(VulkanContext* context);
    void shutdown();
    
    // --------------------------------------------------------------------------
    // Graph Management
    // --------------------------------------------------------------------------
    
    /**
     * Create a new empty material
     */
    void newMaterial();
    
    /**
     * Load a material from file
     */
    bool loadMaterial(const std::string& path);
    
    /**
     * Save current material to file
     */
    bool saveMaterial(const std::string& path);
    
    /**
     * Get current material graph
     */
    MaterialGraph* getGraph() { return m_Graph.get(); }
    const MaterialGraph* getGraph() const { return m_Graph.get(); }
    
    /**
     * Set material graph (takes ownership)
     */
    void setGraph(std::unique_ptr<MaterialGraph> graph);
    
    // --------------------------------------------------------------------------
    // Rendering
    // --------------------------------------------------------------------------
    
    /**
     * Render the material editor UI
     * Call this within an ImGui frame
     */
    void render();
    
    /**
     * Render just the node graph area
     */
    void renderNodeGraph();
    
    /**
     * Render the properties panel
     */
    void renderPropertiesPanel();
    
    /**
     * Render the node palette/library
     */
    void renderNodePalette();
    
    /**
     * Render the material preview
     */
    void renderPreview();
    
    /**
     * Render the toolbar
     */
    void renderToolbar();
    
    // --------------------------------------------------------------------------
    // Compilation
    // --------------------------------------------------------------------------
    
    /**
     * Compile the current material
     */
    bool compile();
    
    /**
     * Get last compiled result
     */
    const CompiledMaterial& getCompiledMaterial() const { return m_CompiledMaterial; }
    
    /**
     * Check if material needs recompilation
     */
    bool needsRecompile() const;
    
    /**
     * Enable/disable auto-recompile on changes
     */
    void setAutoCompile(bool enable) { m_AutoCompile = enable; }
    
    // --------------------------------------------------------------------------
    // Edit Operations
    // --------------------------------------------------------------------------
    
    /**
     * Delete selected nodes and connections
     */
    void deleteSelection();
    
    /**
     * Copy selection to clipboard
     */
    void copySelection();
    
    /**
     * Paste from clipboard
     */
    void pasteClipboard();
    
    /**
     * Duplicate selection
     */
    void duplicateSelection();
    
    /**
     * Select all nodes
     */
    void selectAll();
    
    /**
     * Frame all nodes in view
     */
    void frameAll();
    
    /**
     * Frame selection in view
     */
    void frameSelection();
    
    // --------------------------------------------------------------------------
    // Undo/Redo
    // --------------------------------------------------------------------------
    
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    
    // --------------------------------------------------------------------------
    // Callbacks
    // --------------------------------------------------------------------------
    
    using CompileCallback = std::function<void(const CompiledMaterial&)>;
    using ModifiedCallback = std::function<void()>;
    
    void setOnCompiled(CompileCallback callback) { m_OnCompiled = callback; }
    void setOnModified(ModifiedCallback callback) { m_OnModified = callback; }
    
    // --------------------------------------------------------------------------
    // State
    // --------------------------------------------------------------------------
    
    bool isModified() const { return m_IsModified; }
    void clearModified() { m_IsModified = false; }
    
    const std::string& getCurrentFilePath() const { return m_CurrentFilePath; }
    
    MaterialPreviewSettings& getPreviewSettings() { return m_PreviewSettings; }
    
private:
    // ImNodes integration
    void* m_ImNodesContext = nullptr;  // ImNodes::EditorContext*
    
    // Current material
    std::unique_ptr<MaterialGraph> m_Graph;
    std::string m_CurrentFilePath;
    bool m_IsModified = false;
    
    // Compiled result
    CompiledMaterial m_CompiledMaterial;
    bool m_AutoCompile = true;
    
    // Compiler
    MaterialCompiler m_Compiler;
    
    // Selection
    EditorSelection m_Selection;
    
    // Clipboard
    MaterialClipboard m_Clipboard;
    
    // Node creation popup
    NodeCreationPopup m_NodePopup;
    void openNodeCreationPopup(glm::vec2 position);
    void closeNodeCreationPopup();
    void updateNodeSearchFilter();
    void renderNodeCreationPopup();
    
    // Undo/redo
    std::vector<MaterialEditorAction> m_UndoStack;
    std::vector<MaterialEditorAction> m_RedoStack;
    static const size_t MAX_UNDO_STACK = 100;
    
    void pushUndoAction(MaterialEditorAction action);
    void applyAction(const MaterialEditorAction& action, bool isUndo);
    
    // Node rendering helpers
    void renderNode(MaterialNode* node);
    void renderNodePin(const MaterialPin& pin, bool isInput, uint64_t nodeId, uint32_t pinIndex);
    glm::vec4 getPinColor(MaterialValueType type) const;
    
    // Connection handling
    void handleNewConnection();
    void handleDeletedConnection();
    
    // Context menu
    void renderContextMenu();
    
    // Properties panel
    void renderNodeProperties(MaterialNode* node);
    void renderMaterialProperties();
    
    // Preview
    MaterialPreviewSettings m_PreviewSettings;
    VulkanContext* m_VulkanContext = nullptr;
    // TODO: Preview render target, mesh, etc.
    
    // Minimap
    bool m_ShowMinimap = true;
    
    // Debug
    bool m_ShowDebugInfo = false;
    bool m_ShowGeneratedCode = false;
    
    // Callbacks
    CompileCallback m_OnCompiled;
    ModifiedCallback m_OnModified;
    
    // Style
    void setupNodeStyle();
};

} // namespace Sanic
