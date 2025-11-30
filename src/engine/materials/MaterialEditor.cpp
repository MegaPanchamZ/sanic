/**
 * MaterialEditor.cpp
 * 
 * Implementation of the visual material editor.
 */

#include "MaterialEditor.h"
#include "MaterialNodes/CommonNodes.h"

// ImGui and imnodes
#include <imgui.h>
#include <imnodes.h>

#include <algorithm>
#include <sstream>

namespace Sanic {

// Pin IDs are encoded as: (nodeId << 16) | (isInput << 15) | pinIndex
static uint32_t encodePinId(uint64_t nodeId, bool isInput, uint32_t pinIndex) {
    return static_cast<uint32_t>((nodeId & 0x7FFF) << 16) | 
           (isInput ? 0x8000 : 0) | 
           (pinIndex & 0x7FFF);
}

static void decodePinId(uint32_t pinId, uint64_t& nodeId, bool& isInput, uint32_t& pinIndex) {
    nodeId = (pinId >> 16) & 0x7FFF;
    isInput = (pinId & 0x8000) != 0;
    pinIndex = pinId & 0x7FFF;
}

// Link IDs are just the connection ID
static uint32_t encodeLinkId(uint64_t connectionId) {
    return static_cast<uint32_t>(connectionId);
}

MaterialEditor::MaterialEditor() {
    m_Graph = std::make_unique<MaterialGraph>();
}

MaterialEditor::~MaterialEditor() {
    shutdown();
}

void MaterialEditor::initialize(VulkanContext* context) {
    m_VulkanContext = context;
    
    // Create imnodes context
    m_ImNodesContext = ImNodes::CreateContext();
    ImNodes::SetCurrentContext(static_cast<ImNodesContext*>(m_ImNodesContext));
    
    // Setup style
    setupNodeStyle();
}

void MaterialEditor::shutdown() {
    if (m_ImNodesContext) {
        ImNodes::DestroyContext(static_cast<ImNodesContext*>(m_ImNodesContext));
        m_ImNodesContext = nullptr;
    }
}

// ============================================================================
// Graph Management
// ============================================================================

void MaterialEditor::newMaterial() {
    m_Graph = std::make_unique<MaterialGraph>();
    m_CurrentFilePath.clear();
    m_IsModified = false;
    m_UndoStack.clear();
    m_RedoStack.clear();
    m_Selection.clear();
    
    if (m_AutoCompile) {
        compile();
    }
}

bool MaterialEditor::loadMaterial(const std::string& path) {
    auto graph = MaterialGraph::loadFromFile(path);
    if (!graph) {
        return false;
    }
    
    m_Graph = std::move(graph);
    m_CurrentFilePath = path;
    m_IsModified = false;
    m_UndoStack.clear();
    m_RedoStack.clear();
    m_Selection.clear();
    
    if (m_AutoCompile) {
        compile();
    }
    
    return true;
}

bool MaterialEditor::saveMaterial(const std::string& path) {
    if (!m_Graph) return false;
    
    if (m_Graph->saveToFile(path)) {
        m_CurrentFilePath = path;
        m_IsModified = false;
        return true;
    }
    
    return false;
}

void MaterialEditor::setGraph(std::unique_ptr<MaterialGraph> graph) {
    m_Graph = std::move(graph);
    m_Selection.clear();
    m_IsModified = true;
    
    if (m_AutoCompile) {
        compile();
    }
    
    if (m_OnModified) {
        m_OnModified();
    }
}

// ============================================================================
// Rendering
// ============================================================================

void MaterialEditor::render() {
    if (!m_Graph) return;
    
    ImNodes::SetCurrentContext(static_cast<ImNodesContext*>(m_ImNodesContext));
    
    // Main editor window layout
    ImGui::Begin("Material Editor", nullptr, ImGuiWindowFlags_MenuBar);
    
    // Menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) newMaterial();
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                // TODO: File dialog
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                if (!m_CurrentFilePath.empty()) {
                    saveMaterial(m_CurrentFilePath);
                }
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                // TODO: File dialog
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo())) undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo())) redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X")) { copySelection(); deleteSelection(); }
            if (ImGui::MenuItem("Copy", "Ctrl+C")) copySelection();
            if (ImGui::MenuItem("Paste", "Ctrl+V")) pasteClipboard();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicateSelection();
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A")) selectAll();
            if (ImGui::MenuItem("Delete", "Del")) deleteSelection();
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Frame All", "F")) frameAll();
            if (ImGui::MenuItem("Frame Selection", "Shift+F")) frameSelection();
            ImGui::Separator();
            ImGui::MenuItem("Show Minimap", nullptr, &m_ShowMinimap);
            ImGui::MenuItem("Show Debug Info", nullptr, &m_ShowDebugInfo);
            ImGui::MenuItem("Show Generated Code", nullptr, &m_ShowGeneratedCode);
            ImGui::EndMenu();
        }
        
        ImGui::EndMenuBar();
    }
    
    // Toolbar
    renderToolbar();
    
    // Main layout: left panel (palette), center (node graph), right panel (properties)
    float leftPanelWidth = 200.0f;
    float rightPanelWidth = 300.0f;
    float availableWidth = ImGui::GetContentRegionAvail().x;
    float graphWidth = availableWidth - leftPanelWidth - rightPanelWidth - 16.0f;
    
    // Left panel - Node palette
    ImGui::BeginChild("NodePalette", ImVec2(leftPanelWidth, 0), true);
    renderNodePalette();
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    // Center - Node graph
    ImGui::BeginChild("NodeGraph", ImVec2(graphWidth, 0), true, 
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    renderNodeGraph();
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    // Right panel - Properties
    ImGui::BeginChild("Properties", ImVec2(rightPanelWidth, 0), true);
    renderPropertiesPanel();
    ImGui::EndChild();
    
    ImGui::End();
    
    // Preview window (separate)
    if (ImGui::Begin("Material Preview")) {
        renderPreview();
    }
    ImGui::End();
    
    // Generated code window
    if (m_ShowGeneratedCode) {
        if (ImGui::Begin("Generated GLSL", &m_ShowGeneratedCode)) {
            if (m_CompiledMaterial.success) {
                if (ImGui::BeginTabBar("ShaderCode")) {
                    if (ImGui::BeginTabItem("Vertex Shader")) {
                        ImGui::TextWrapped("%s", m_CompiledMaterial.vertexShaderSource.c_str());
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Fragment Shader")) {
                        ImGui::TextWrapped("%s", m_CompiledMaterial.fragmentShaderSource.c_str());
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            } else {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Compilation failed:");
                ImGui::TextWrapped("%s", m_CompiledMaterial.errorMessage.c_str());
            }
        }
        ImGui::End();
    }
    
    // Handle keyboard shortcuts
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::GetIO().KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z)) undo();
            if (ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
            if (ImGui::IsKeyPressed(ImGuiKey_C)) copySelection();
            if (ImGui::IsKeyPressed(ImGuiKey_V)) pasteClipboard();
            if (ImGui::IsKeyPressed(ImGuiKey_D)) duplicateSelection();
            if (ImGui::IsKeyPressed(ImGuiKey_A)) selectAll();
            if (ImGui::IsKeyPressed(ImGuiKey_N)) newMaterial();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) deleteSelection();
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            if (ImGui::GetIO().KeyShift) frameSelection();
            else frameAll();
        }
    }
}

void MaterialEditor::renderToolbar() {
    ImGui::BeginGroup();
    
    if (ImGui::Button("Compile")) {
        compile();
    }
    ImGui::SameLine();
    
    ImGui::Checkbox("Auto", &m_AutoCompile);
    ImGui::SameLine();
    
    // Compilation status
    if (m_CompiledMaterial.success) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "OK");
    } else if (!m_CompiledMaterial.errorMessage.empty()) {
        ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "Error");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", m_CompiledMaterial.errorMessage.c_str());
        }
    }
    
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200);
    
    // Material name
    char nameBuf[256];
    strncpy(nameBuf, m_Graph->name.c_str(), sizeof(nameBuf) - 1);
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("##MaterialName", nameBuf, sizeof(nameBuf))) {
        m_Graph->name = nameBuf;
        m_IsModified = true;
    }
    
    ImGui::EndGroup();
    ImGui::Separator();
}

void MaterialEditor::renderNodeGraph() {
    ImNodes::BeginNodeEditor();
    
    // Render all nodes
    for (const auto& [nodeId, node] : m_Graph->getNodes()) {
        renderNode(node.get());
    }
    
    // Render all connections
    for (const auto& [connId, conn] : m_Graph->getConnections()) {
        uint32_t startPin = encodePinId(conn.sourceNodeId, false, conn.sourcePin);
        uint32_t endPin = encodePinId(conn.targetNodeId, true, conn.targetPin);
        
        ImNodes::Link(encodeLinkId(conn.id), startPin, endPin);
    }
    
    // Minimap
    if (m_ShowMinimap) {
        ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);
    }
    
    ImNodes::EndNodeEditor();
    
    // Handle new connections
    handleNewConnection();
    
    // Handle deleted connections
    handleDeletedConnection();
    
    // Context menu
    if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("NodeGraphContext");
    }
    renderContextMenu();
    
    // Update selection
    int numSelectedNodes = ImNodes::NumSelectedNodes();
    if (numSelectedNodes > 0) {
        std::vector<int> selectedIds(numSelectedNodes);
        ImNodes::GetSelectedNodes(selectedIds.data());
        
        m_Selection.selectedNodes.clear();
        for (int id : selectedIds) {
            m_Selection.selectedNodes.push_back(static_cast<uint64_t>(id));
        }
    } else {
        m_Selection.selectedNodes.clear();
    }
    
    int numSelectedLinks = ImNodes::NumSelectedLinks();
    if (numSelectedLinks > 0) {
        std::vector<int> selectedIds(numSelectedLinks);
        ImNodes::GetSelectedLinks(selectedIds.data());
        
        m_Selection.selectedConnections.clear();
        for (int id : selectedIds) {
            m_Selection.selectedConnections.push_back(static_cast<uint64_t>(id));
        }
    } else {
        m_Selection.selectedConnections.clear();
    }
    
    // Node creation popup
    renderNodeCreationPopup();
}

void MaterialEditor::renderNode(MaterialNode* node) {
    const uint64_t nodeId = node->id;
    
    // Set node position
    ImNodes::SetNodeGridSpacePos(static_cast<int>(nodeId), 
                                  ImVec2(node->position.x, node->position.y));
    
    // Begin node
    ImNodes::BeginNode(static_cast<int>(nodeId));
    
    // Title bar
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(node->getName().c_str());
    ImNodes::EndNodeTitleBar();
    
    // Input pins
    const auto& inputs = node->getInputPins();
    for (uint32_t i = 0; i < inputs.size(); i++) {
        renderNodePin(inputs[i], true, nodeId, i);
    }
    
    // Output pins
    const auto& outputs = node->getOutputPins();
    for (uint32_t i = 0; i < outputs.size(); i++) {
        renderNodePin(outputs[i], false, nodeId, i);
    }
    
    // Custom node content (for previews, etc.)
    if (node->supportsPreview()) {
        ImGui::Dummy(ImVec2(100, 100)); // Placeholder for preview
    }
    
    ImNodes::EndNode();
    
    // Update node position from editor
    ImVec2 pos = ImNodes::GetNodeGridSpacePos(static_cast<int>(nodeId));
    if (pos.x != node->position.x || pos.y != node->position.y) {
        node->position = glm::vec2(pos.x, pos.y);
        m_IsModified = true;
    }
}

void MaterialEditor::renderNodePin(const MaterialPin& pin, bool isInput, uint64_t nodeId, uint32_t pinIndex) {
    uint32_t pinId = encodePinId(nodeId, isInput, pinIndex);
    glm::vec4 color = getPinColor(pin.type);
    
    ImNodes::PushColorStyle(ImNodesCol_Pin, IM_COL32(
        static_cast<int>(color.r * 255),
        static_cast<int>(color.g * 255),
        static_cast<int>(color.b * 255),
        static_cast<int>(color.a * 255)));
    
    if (isInput) {
        ImNodes::BeginInputAttribute(pinId);
        ImGui::TextUnformatted(pin.name.c_str());
        ImNodes::EndInputAttribute();
    } else {
        ImNodes::BeginOutputAttribute(pinId);
        ImGui::Indent(40);
        ImGui::TextUnformatted(pin.name.c_str());
        ImNodes::EndOutputAttribute();
    }
    
    ImNodes::PopColorStyle();
}

glm::vec4 MaterialEditor::getPinColor(MaterialValueType type) const {
    switch (type) {
        case MaterialValueType::Float:
            return glm::vec4(0.5f, 0.8f, 0.5f, 1.0f);  // Green
        case MaterialValueType::Float2:
            return glm::vec4(0.5f, 0.7f, 0.9f, 1.0f);  // Light blue
        case MaterialValueType::Float3:
            return glm::vec4(0.9f, 0.8f, 0.3f, 1.0f);  // Yellow
        case MaterialValueType::Float4:
            return glm::vec4(0.9f, 0.5f, 0.5f, 1.0f);  // Red
        case MaterialValueType::Texture2D:
            return glm::vec4(0.8f, 0.4f, 0.8f, 1.0f);  // Purple
        case MaterialValueType::TextureCube:
            return glm::vec4(0.6f, 0.3f, 0.7f, 1.0f);  // Dark purple
        case MaterialValueType::Bool:
            return glm::vec4(0.9f, 0.2f, 0.2f, 1.0f);  // Bright red
        case MaterialValueType::Int:
            return glm::vec4(0.3f, 0.9f, 0.9f, 1.0f);  // Cyan
        default:
            return glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);  // Gray
    }
}

void MaterialEditor::handleNewConnection() {
    int startPinId, endPinId;
    if (ImNodes::IsLinkCreated(&startPinId, &endPinId)) {
        uint64_t startNodeId, endNodeId;
        bool startIsInput, endIsInput;
        uint32_t startPinIndex, endPinIndex;
        
        decodePinId(startPinId, startNodeId, startIsInput, startPinIndex);
        decodePinId(endPinId, endNodeId, endIsInput, endPinIndex);
        
        // Ensure we're connecting output to input
        if (startIsInput) {
            std::swap(startNodeId, endNodeId);
            std::swap(startPinIndex, endPinIndex);
        }
        
        // Create connection
        uint64_t connId = m_Graph->connect(startNodeId, startPinIndex, endNodeId, endPinIndex);
        if (connId != 0) {
            m_IsModified = true;
            m_Graph->markDirty();
            
            if (m_AutoCompile) {
                compile();
            }
            
            if (m_OnModified) {
                m_OnModified();
            }
        }
    }
}

void MaterialEditor::handleDeletedConnection() {
    int linkId;
    if (ImNodes::IsLinkDestroyed(&linkId)) {
        m_Graph->disconnect(static_cast<uint64_t>(linkId));
        m_IsModified = true;
        m_Graph->markDirty();
        
        if (m_AutoCompile) {
            compile();
        }
        
        if (m_OnModified) {
            m_OnModified();
        }
    }
}

void MaterialEditor::renderPropertiesPanel() {
    ImGui::Text("Properties");
    ImGui::Separator();
    
    if (m_Selection.selectedNodes.size() == 1) {
        MaterialNode* node = m_Graph->getNode(m_Selection.selectedNodes[0]);
        if (node) {
            renderNodeProperties(node);
        }
    } else if (m_Selection.selectedNodes.empty()) {
        renderMaterialProperties();
    } else {
        ImGui::Text("%zu nodes selected", m_Selection.selectedNodes.size());
    }
}

void MaterialEditor::renderNodeProperties(MaterialNode* node) {
    ImGui::Text("Node: %s", node->getName().c_str());
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "%s", node->getCategory().c_str());
    ImGui::Separator();
    
    if (!node->getDescription().empty()) {
        ImGui::TextWrapped("%s", node->getDescription().c_str());
        ImGui::Separator();
    }
    
    // Node-specific properties (based on node type)
    // This would be expanded with property editors for each node type
    
    // Example for ScalarNode
    if (ScalarNode* scalar = dynamic_cast<ScalarNode*>(node)) {
        if (ImGui::DragFloat("Value", &scalar->value, 0.01f)) {
            m_IsModified = true;
            m_Graph->markDirty();
        }
    }
    
    // Example for ColorNode
    if (ColorNode* color = dynamic_cast<ColorNode*>(node)) {
        float col[3] = { color->color.r, color->color.g, color->color.b };
        if (ImGui::ColorEdit3("Color", col)) {
            color->color = glm::vec3(col[0], col[1], col[2]);
            m_IsModified = true;
            m_Graph->markDirty();
        }
        if (ImGui::DragFloat("Alpha", &color->alpha, 0.01f, 0.0f, 1.0f)) {
            m_IsModified = true;
            m_Graph->markDirty();
        }
    }
    
    // Example for VectorNode
    if (VectorNode* vec = dynamic_cast<VectorNode*>(node)) {
        float v[4] = { vec->value.x, vec->value.y, vec->value.z, vec->value.w };
        if (ImGui::DragFloat4("Value", v, 0.01f)) {
            vec->value = glm::vec4(v[0], v[1], v[2], v[3]);
            m_IsModified = true;
            m_Graph->markDirty();
        }
    }
    
    // Example for TextureSampleNode
    if (TextureSampleNode* tex = dynamic_cast<TextureSampleNode*>(node)) {
        char pathBuf[512];
        strncpy(pathBuf, tex->texturePath.c_str(), sizeof(pathBuf) - 1);
        if (ImGui::InputText("Texture", pathBuf, sizeof(pathBuf))) {
            tex->texturePath = pathBuf;
            m_IsModified = true;
            m_Graph->markDirty();
        }
        
        int slot = static_cast<int>(tex->textureSlot);
        if (ImGui::InputInt("Slot", &slot)) {
            tex->textureSlot = static_cast<uint32_t>(std::max(0, slot));
            m_IsModified = true;
            m_Graph->markDirty();
        }
        
        if (ImGui::Checkbox("sRGB", &tex->useSRGB)) {
            m_IsModified = true;
            m_Graph->markDirty();
        }
    }
}

void MaterialEditor::renderMaterialProperties() {
    ImGui::Text("Material Properties");
    ImGui::Separator();
    
    // Material name
    char nameBuf[256];
    strncpy(nameBuf, m_Graph->name.c_str(), sizeof(nameBuf) - 1);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        m_Graph->name = nameBuf;
        m_IsModified = true;
    }
    
    // Blend mode
    const char* blendModes[] = { "Opaque", "Masked", "Translucent", "Additive", "Modulate" };
    int blendMode = static_cast<int>(m_Graph->blendMode);
    if (ImGui::Combo("Blend Mode", &blendMode, blendModes, IM_ARRAYSIZE(blendModes))) {
        m_Graph->blendMode = static_cast<MaterialBlendMode>(blendMode);
        m_IsModified = true;
        m_Graph->markDirty();
    }
    
    // Shading model
    const char* shadingModels[] = { "Unlit", "Default Lit", "Subsurface", "Clear Coat", 
                                     "Cloth", "Eye", "Hair", "Thin Translucent" };
    int shadingModel = static_cast<int>(m_Graph->shadingModel);
    if (ImGui::Combo("Shading Model", &shadingModel, shadingModels, IM_ARRAYSIZE(shadingModels))) {
        m_Graph->shadingModel = static_cast<MaterialShadingModel>(shadingModel);
        m_IsModified = true;
        m_Graph->markDirty();
    }
    
    // Two-sided
    if (ImGui::Checkbox("Two Sided", &m_Graph->twoSided)) {
        m_IsModified = true;
        m_Graph->markDirty();
    }
    
    // Wireframe
    if (ImGui::Checkbox("Wireframe", &m_Graph->wireframe)) {
        m_IsModified = true;
        m_Graph->markDirty();
    }
    
    ImGui::Separator();
    
    // Validation status
    auto diagnostics = m_Graph->validate();
    if (diagnostics.empty()) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1), "Material is valid");
    } else {
        for (const auto& diag : diagnostics) {
            ImVec4 color;
            switch (diag.severity) {
                case MaterialGraphDiagnostic::Severity::Error:
                    color = ImVec4(0.9f, 0.2f, 0.2f, 1);
                    break;
                case MaterialGraphDiagnostic::Severity::Warning:
                    color = ImVec4(0.9f, 0.7f, 0.2f, 1);
                    break;
                default:
                    color = ImVec4(0.5f, 0.5f, 0.8f, 1);
            }
            ImGui::TextColored(color, "%s", diag.message.c_str());
        }
    }
}

void MaterialEditor::renderNodePalette() {
    ImGui::Text("Node Palette");
    ImGui::Separator();
    
    // Search
    static char searchBuf[256] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##Search", searchBuf, sizeof(searchBuf));
    
    std::string searchStr = searchBuf;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
    
    // Get registered node types
    auto nodeTypes = MaterialNodeFactory::instance().getRegisteredTypes();
    
    // Group by category
    std::unordered_map<std::string, std::vector<std::string>> categories;
    for (const auto& type : nodeTypes) {
        auto node = MaterialNodeFactory::instance().create(type);
        if (node) {
            std::string nodeName = node->getName();
            std::string category = node->getCategory();
            
            // Filter by search
            if (!searchStr.empty()) {
                std::string lowerName = nodeName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                if (lowerName.find(searchStr) == std::string::npos) {
                    continue;
                }
            }
            
            categories[category].push_back(type);
        }
    }
    
    // Render categories
    for (const auto& [category, types] : categories) {
        if (ImGui::TreeNodeEx(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& type : types) {
                auto node = MaterialNodeFactory::instance().create(type);
                if (node) {
                    bool isSelected = false;
                    if (ImGui::Selectable(node->getName().c_str(), isSelected)) {
                        // Create node in center of view
                        glm::vec2 pos(200.0f, 200.0f); // TODO: Get actual view center
                        MaterialNode* newNode = m_Graph->createNode(type);
                        if (newNode) {
                            newNode->position = pos;
                            m_IsModified = true;
                            
                            if (m_AutoCompile) {
                                compile();
                            }
                        }
                    }
                    
                    // Tooltip
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", node->getDescription().c_str());
                    }
                }
            }
            ImGui::TreePop();
        }
    }
}

void MaterialEditor::renderPreview() {
    ImGui::Text("Preview");
    ImGui::Separator();
    
    // Preview shape selection
    const char* shapes[] = { "Sphere", "Cube", "Plane", "Cylinder", "Custom" };
    int shapeIndex = static_cast<int>(m_PreviewSettings.shape);
    if (ImGui::Combo("Shape", &shapeIndex, shapes, IM_ARRAYSIZE(shapes))) {
        m_PreviewSettings.shape = static_cast<MaterialPreviewSettings::PreviewShape>(shapeIndex);
    }
    
    // Auto-rotate
    ImGui::Checkbox("Auto Rotate", &m_PreviewSettings.autoRotate);
    if (m_PreviewSettings.autoRotate) {
        ImGui::SameLine();
        ImGui::DragFloat("Speed", &m_PreviewSettings.rotationSpeed, 0.01f, 0.0f, 2.0f);
    }
    
    // Exposure
    ImGui::DragFloat("Exposure", &m_PreviewSettings.exposure, 0.01f, 0.1f, 5.0f);
    
    ImGui::Separator();
    
    // Preview render area
    ImVec2 available = ImGui::GetContentRegionAvail();
    float size = std::min(available.x, available.y);
    
    // TODO: Actual preview rendering
    ImGui::Dummy(ImVec2(size, size));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetItemRectMin();
    drawList->AddRectFilled(p, ImVec2(p.x + size, p.y + size), IM_COL32(40, 40, 40, 255));
    drawList->AddText(ImVec2(p.x + size/2 - 50, p.y + size/2), 
                      IM_COL32(100, 100, 100, 255), "Preview Area");
}

void MaterialEditor::renderContextMenu() {
    if (ImGui::BeginPopup("NodeGraphContext")) {
        if (ImGui::BeginMenu("Add Node")) {
            auto nodeTypes = MaterialNodeFactory::instance().getRegisteredTypes();
            
            // Group by category
            std::unordered_map<std::string, std::vector<std::string>> categories;
            for (const auto& type : nodeTypes) {
                auto node = MaterialNodeFactory::instance().create(type);
                if (node) {
                    categories[node->getCategory()].push_back(type);
                }
            }
            
            for (const auto& [category, types] : categories) {
                if (ImGui::BeginMenu(category.c_str())) {
                    for (const auto& type : types) {
                        auto node = MaterialNodeFactory::instance().create(type);
                        if (node && ImGui::MenuItem(node->getName().c_str())) {
                            ImVec2 mousePos = ImGui::GetMousePosOnOpeningCurrentPopup();
                            MaterialNode* newNode = m_Graph->createNode(type);
                            if (newNode) {
                                newNode->position = glm::vec2(mousePos.x, mousePos.y);
                                m_IsModified = true;
                                
                                if (m_AutoCompile) {
                                    compile();
                                }
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
            }
            
            ImGui::EndMenu();
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, m_Selection.hasSelection())) {
            copySelection();
            deleteSelection();
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, m_Selection.hasSelection())) {
            copySelection();
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, m_Clipboard.hasContent)) {
            pasteClipboard();
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, m_Selection.hasSelection())) {
            duplicateSelection();
        }
        if (ImGui::MenuItem("Delete", "Del", false, m_Selection.hasSelection())) {
            deleteSelection();
        }
        
        ImGui::EndPopup();
    }
}

void MaterialEditor::openNodeCreationPopup(glm::vec2 position) {
    m_NodePopup.isOpen = true;
    m_NodePopup.createPosition = position;
    m_NodePopup.searchQuery.clear();
    m_NodePopup.selectedIndex = 0;
    updateNodeSearchFilter();
}

void MaterialEditor::closeNodeCreationPopup() {
    m_NodePopup.isOpen = false;
    m_NodePopup.fromPin = false;
}

void MaterialEditor::updateNodeSearchFilter() {
    m_NodePopup.filteredNodes.clear();
    
    auto nodeTypes = MaterialNodeFactory::instance().getRegisteredTypes();
    
    std::string searchLower = m_NodePopup.searchQuery;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
    
    for (const auto& type : nodeTypes) {
        auto node = MaterialNodeFactory::instance().create(type);
        if (node) {
            std::string nameLower = node->getName();
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            
            if (searchLower.empty() || nameLower.find(searchLower) != std::string::npos) {
                m_NodePopup.filteredNodes.push_back(type);
            }
        }
    }
}

void MaterialEditor::renderNodeCreationPopup() {
    if (!m_NodePopup.isOpen) return;
    
    ImGui::SetNextWindowPos(ImVec2(m_NodePopup.createPosition.x, m_NodePopup.createPosition.y));
    ImGui::SetNextWindowSize(ImVec2(200, 300));
    
    if (ImGui::Begin("##NodeCreate", &m_NodePopup.isOpen, 
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
        
        // Search input
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        
        char searchBuf[256];
        strncpy(searchBuf, m_NodePopup.searchQuery.c_str(), sizeof(searchBuf) - 1);
        if (ImGui::InputText("##Search", searchBuf, sizeof(searchBuf))) {
            m_NodePopup.searchQuery = searchBuf;
            updateNodeSearchFilter();
            m_NodePopup.selectedIndex = 0;
        }
        
        ImGui::Separator();
        
        // Node list
        for (size_t i = 0; i < m_NodePopup.filteredNodes.size(); i++) {
            auto node = MaterialNodeFactory::instance().create(m_NodePopup.filteredNodes[i]);
            if (node) {
                bool isSelected = (i == m_NodePopup.selectedIndex);
                if (ImGui::Selectable(node->getName().c_str(), isSelected)) {
                    MaterialNode* newNode = m_Graph->createNode(m_NodePopup.filteredNodes[i]);
                    if (newNode) {
                        newNode->position = m_NodePopup.createPosition;
                        m_IsModified = true;
                        
                        // Auto-connect if creating from pin
                        if (m_NodePopup.fromPin) {
                            // TODO: Auto-connect logic
                        }
                        
                        if (m_AutoCompile) {
                            compile();
                        }
                    }
                    closeNodeCreationPopup();
                }
            }
        }
        
        // Keyboard navigation
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && m_NodePopup.selectedIndex > 0) {
            m_NodePopup.selectedIndex--;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && 
            m_NodePopup.selectedIndex < m_NodePopup.filteredNodes.size() - 1) {
            m_NodePopup.selectedIndex++;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !m_NodePopup.filteredNodes.empty()) {
            MaterialNode* newNode = m_Graph->createNode(m_NodePopup.filteredNodes[m_NodePopup.selectedIndex]);
            if (newNode) {
                newNode->position = m_NodePopup.createPosition;
                m_IsModified = true;
                
                if (m_AutoCompile) {
                    compile();
                }
            }
            closeNodeCreationPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            closeNodeCreationPopup();
        }
    }
    ImGui::End();
}

// ============================================================================
// Compilation
// ============================================================================

bool MaterialEditor::compile() {
    if (!m_Graph) return false;
    
    m_CompiledMaterial = m_Compiler.compile(*m_Graph);
    m_Graph->clearDirty();
    
    if (m_OnCompiled) {
        m_OnCompiled(m_CompiledMaterial);
    }
    
    return m_CompiledMaterial.success;
}

bool MaterialEditor::needsRecompile() const {
    return m_Graph && m_Graph->isDirty();
}

// ============================================================================
// Edit Operations
// ============================================================================

void MaterialEditor::deleteSelection() {
    // Delete connections first
    for (uint64_t connId : m_Selection.selectedConnections) {
        m_Graph->disconnect(connId);
    }
    
    // Delete nodes
    for (uint64_t nodeId : m_Selection.selectedNodes) {
        m_Graph->removeNode(nodeId);
    }
    
    m_Selection.clear();
    m_IsModified = true;
    m_Graph->markDirty();
    
    if (m_AutoCompile) {
        compile();
    }
    
    if (m_OnModified) {
        m_OnModified();
    }
}

void MaterialEditor::copySelection() {
    m_Clipboard.nodes.clear();
    m_Clipboard.connections.clear();
    
    // Serialize selected nodes
    for (uint64_t nodeId : m_Selection.selectedNodes) {
        MaterialNode* node = m_Graph->getNode(nodeId);
        if (node) {
            nlohmann::json nodeJson = MaterialSerializer::serializeNode(node);
            nodeJson["originalId"] = nodeId;
            m_Clipboard.nodes.push_back(nodeJson);
        }
    }
    
    // Serialize connections between selected nodes
    for (const auto& [connId, conn] : m_Graph->getConnections()) {
        bool sourceSelected = std::find(m_Selection.selectedNodes.begin(), 
                                         m_Selection.selectedNodes.end(),
                                         conn.sourceNodeId) != m_Selection.selectedNodes.end();
        bool targetSelected = std::find(m_Selection.selectedNodes.begin(),
                                         m_Selection.selectedNodes.end(),
                                         conn.targetNodeId) != m_Selection.selectedNodes.end();
        
        if (sourceSelected && targetSelected) {
            nlohmann::json connJson;
            connJson["sourceNodeId"] = conn.sourceNodeId;
            connJson["sourcePin"] = conn.sourcePin;
            connJson["targetNodeId"] = conn.targetNodeId;
            connJson["targetPin"] = conn.targetPin;
            m_Clipboard.connections.push_back(connJson);
        }
    }
    
    m_Clipboard.hasContent = !m_Clipboard.nodes.empty();
}

void MaterialEditor::pasteClipboard() {
    if (!m_Clipboard.hasContent) return;
    
    std::unordered_map<uint64_t, uint64_t> idMapping;
    
    // Create nodes
    for (const auto& nodeJson : m_Clipboard.nodes) {
        auto node = MaterialSerializer::deserializeNode(nodeJson);
        if (node) {
            uint64_t originalId = nodeJson["originalId"];
            node->position += glm::vec2(50.0f, 50.0f); // Offset
            
            MaterialNode* newNode = m_Graph->addNode(std::move(node));
            if (newNode) {
                idMapping[originalId] = newNode->id;
            }
        }
    }
    
    // Create connections
    for (const auto& connJson : m_Clipboard.connections) {
        uint64_t oldSourceId = connJson["sourceNodeId"];
        uint64_t oldTargetId = connJson["targetNodeId"];
        
        if (idMapping.count(oldSourceId) && idMapping.count(oldTargetId)) {
            m_Graph->connect(idMapping[oldSourceId], connJson["sourcePin"],
                            idMapping[oldTargetId], connJson["targetPin"]);
        }
    }
    
    m_IsModified = true;
    m_Graph->markDirty();
    
    if (m_AutoCompile) {
        compile();
    }
    
    if (m_OnModified) {
        m_OnModified();
    }
}

void MaterialEditor::duplicateSelection() {
    copySelection();
    pasteClipboard();
}

void MaterialEditor::selectAll() {
    m_Selection.selectedNodes.clear();
    for (const auto& [nodeId, node] : m_Graph->getNodes()) {
        m_Selection.selectedNodes.push_back(nodeId);
    }
}

void MaterialEditor::frameAll() {
    // TODO: Calculate bounds and adjust view
}

void MaterialEditor::frameSelection() {
    // TODO: Calculate selection bounds and adjust view
}

// ============================================================================
// Undo/Redo
// ============================================================================

void MaterialEditor::pushUndoAction(MaterialEditorAction action) {
    m_UndoStack.push_back(action);
    if (m_UndoStack.size() > MAX_UNDO_STACK) {
        m_UndoStack.erase(m_UndoStack.begin());
    }
    m_RedoStack.clear();
}

void MaterialEditor::undo() {
    if (m_UndoStack.empty()) return;
    
    MaterialEditorAction action = m_UndoStack.back();
    m_UndoStack.pop_back();
    
    applyAction(action, true);
    m_RedoStack.push_back(action);
}

void MaterialEditor::redo() {
    if (m_RedoStack.empty()) return;
    
    MaterialEditorAction action = m_RedoStack.back();
    m_RedoStack.pop_back();
    
    applyAction(action, false);
    m_UndoStack.push_back(action);
}

bool MaterialEditor::canUndo() const {
    return !m_UndoStack.empty();
}

bool MaterialEditor::canRedo() const {
    return !m_RedoStack.empty();
}

void MaterialEditor::applyAction(const MaterialEditorAction& action, bool isUndo) {
    // TODO: Implement undo/redo action application
    m_IsModified = true;
    m_Graph->markDirty();
    
    if (m_AutoCompile) {
        compile();
    }
}

// ============================================================================
// Style
// ============================================================================

void MaterialEditor::setupNodeStyle() {
    ImNodesStyle& style = ImNodes::GetStyle();
    
    // Colors
    style.Colors[ImNodesCol_NodeBackground] = IM_COL32(50, 50, 50, 255);
    style.Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(60, 60, 60, 255);
    style.Colors[ImNodesCol_NodeBackgroundSelected] = IM_COL32(70, 70, 70, 255);
    style.Colors[ImNodesCol_NodeOutline] = IM_COL32(100, 100, 100, 255);
    
    style.Colors[ImNodesCol_TitleBar] = IM_COL32(80, 80, 80, 255);
    style.Colors[ImNodesCol_TitleBarHovered] = IM_COL32(100, 100, 100, 255);
    style.Colors[ImNodesCol_TitleBarSelected] = IM_COL32(110, 110, 110, 255);
    
    style.Colors[ImNodesCol_Link] = IM_COL32(200, 200, 200, 255);
    style.Colors[ImNodesCol_LinkHovered] = IM_COL32(255, 255, 255, 255);
    style.Colors[ImNodesCol_LinkSelected] = IM_COL32(255, 200, 100, 255);
    
    style.Colors[ImNodesCol_Pin] = IM_COL32(150, 150, 150, 255);
    style.Colors[ImNodesCol_PinHovered] = IM_COL32(255, 255, 255, 255);
    
    style.Colors[ImNodesCol_GridBackground] = IM_COL32(35, 35, 35, 255);
    style.Colors[ImNodesCol_GridLine] = IM_COL32(50, 50, 50, 255);
    
    // Sizes
    style.NodeCornerRounding = 4.0f;
    style.NodePadding = ImVec2(8.0f, 8.0f);
    style.NodeBorderThickness = 1.0f;
    
    style.LinkThickness = 3.0f;
    style.LinkLineSegmentsPerLength = 0.1f;
    style.LinkHoverDistance = 10.0f;
    
    style.PinCircleRadius = 4.0f;
    style.PinQuadSideLength = 7.0f;
    style.PinTriangleSideLength = 9.0f;
    style.PinLineThickness = 1.0f;
    style.PinHoverRadius = 10.0f;
    style.PinOffset = 0.0f;
    
    // Grid
    style.GridSpacing = 24.0f;
    
    // Minimap
    style.MiniMapPadding = ImVec2(8.0f, 8.0f);
    style.MiniMapOffset = ImVec2(4.0f, 4.0f);
}

} // namespace Sanic
