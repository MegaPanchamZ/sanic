/**
 * EditorTools.cpp
 * 
 * Implementation of Editor Extensions and Debug Tools
 */

#include "EditorTools.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace Sanic {
namespace Editor {

// ============================================================================
// NODE GRAPH EDITOR
// ============================================================================

NodeGraphEditor::NodeGraphEditor(const std::string& name) : name_(name) {}

void NodeGraphEditor::draw() {
    ImGui::Begin(name_.c_str());
    
    handleInput();
    
    // Draw background grid
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    
    // Grid
    float gridSize = 32.0f * viewZoom_;
    ImU32 gridColor = IM_COL32(50, 50, 50, 200);
    for (float x = fmodf(viewOffset_.x, gridSize); x < canvasSize.x; x += gridSize) {
        drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y),
                          ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y), gridColor);
    }
    for (float y = fmodf(viewOffset_.y, gridSize); y < canvasSize.y; y += gridSize) {
        drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y),
                          ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y), gridColor);
    }
    
    // Draw connections
    drawConnections();
    
    // Draw nodes
    for (auto& [id, node] : nodes_) {
        drawNode(node.get());
    }
    
    // Draw connection in progress
    if (isConnecting_) {
        auto sourceIt = nodes_.find(connectSourceNode_);
        if (sourceIt != nodes_.end()) {
            auto& sourceNode = sourceIt->second;
            if (connectSourceSlot_ < sourceNode->outputs.size()) {
                ImVec2 startPos = ImVec2(
                    canvasPos.x + viewOffset_.x + sourceNode->position.x + sourceNode->size.x,
                    canvasPos.y + viewOffset_.y + sourceNode->position.y + 30 + connectSourceSlot_ * 20
                );
                ImVec2 endPos = ImVec2(connectEndPos_.x, connectEndPos_.y);
                drawList->AddBezierCubic(startPos, 
                                          ImVec2(startPos.x + 50, startPos.y),
                                          ImVec2(endPos.x - 50, endPos.y),
                                          endPos, IM_COL32(200, 200, 100, 255), 2.0f);
            }
        }
    }
    
    // Context menu
    if (ImGui::BeginPopupContextWindow("NodeContextMenu")) {
        drawContextMenu();
        ImGui::EndPopup();
    }
    
    ImGui::End();
}

void NodeGraphEditor::addNode(std::shared_ptr<VisualNode> node) {
    node->id = nextNodeId_++;
    nodes_[node->id] = node;
    if (onNodeCreated_) onNodeCreated_(node.get());
}

void NodeGraphEditor::removeNode(uint32_t nodeId) {
    // Remove connections
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
            [nodeId](const NodeConnection& c) {
                return c.sourceNodeId == nodeId || c.targetNodeId == nodeId;
            }),
        connections_.end()
    );
    
    if (onNodeDeleted_) onNodeDeleted_(nodeId);
    nodes_.erase(nodeId);
    selectedNodes_.erase(nodeId);
}

std::vector<VisualNode*> NodeGraphEditor::getSelectedNodes() {
    std::vector<VisualNode*> result;
    for (uint32_t id : selectedNodes_) {
        auto it = nodes_.find(id);
        if (it != nodes_.end()) {
            result.push_back(it->second.get());
        }
    }
    return result;
}

void NodeGraphEditor::clearSelection() {
    for (uint32_t id : selectedNodes_) {
        auto it = nodes_.find(id);
        if (it != nodes_.end()) {
            it->second->isSelected = false;
        }
    }
    selectedNodes_.clear();
}

bool NodeGraphEditor::connect(uint32_t sourceNode, uint32_t sourceSlot,
                               uint32_t targetNode, uint32_t targetSlot) {
    auto srcIt = nodes_.find(sourceNode);
    auto tgtIt = nodes_.find(targetNode);
    if (srcIt == nodes_.end() || tgtIt == nodes_.end()) return false;
    
    if (sourceSlot >= srcIt->second->outputs.size()) return false;
    if (targetSlot >= tgtIt->second->inputs.size()) return false;
    
    NodeConnection conn;
    conn.sourceNodeId = sourceNode;
    conn.sourceSlotIndex = sourceSlot;
    conn.targetNodeId = targetNode;
    conn.targetSlotIndex = targetSlot;
    connections_.push_back(conn);
    
    srcIt->second->outputs[sourceSlot].isConnected = true;
    tgtIt->second->inputs[targetSlot].isConnected = true;
    
    if (onConnected_) onConnected_(conn);
    return true;
}

void NodeGraphEditor::disconnect(uint32_t sourceNode, uint32_t sourceSlot,
                                  uint32_t targetNode, uint32_t targetSlot) {
    auto it = std::find_if(connections_.begin(), connections_.end(),
        [=](const NodeConnection& c) {
            return c.sourceNodeId == sourceNode && c.sourceSlotIndex == sourceSlot &&
                   c.targetNodeId == targetNode && c.targetSlotIndex == targetSlot;
        });
    
    if (it != connections_.end()) {
        if (onDisconnected_) onDisconnected_(*it);
        connections_.erase(it);
    }
}

void NodeGraphEditor::drawNode(VisualNode* node) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    
    ImVec2 nodePos = ImVec2(
        canvasPos.x + viewOffset_.x + node->position.x,
        canvasPos.y + viewOffset_.y + node->position.y
    );
    ImVec2 nodeSize = ImVec2(node->size.x * viewZoom_, node->size.y * viewZoom_);
    
    // Node background
    ImU32 bgColor = node->isSelected ? IM_COL32(80, 80, 100, 255) : 
                    node->isHovered ? IM_COL32(60, 60, 70, 255) :
                    IM_COL32(50, 50, 55, 255);
    drawList->AddRectFilled(nodePos, ImVec2(nodePos.x + nodeSize.x, nodePos.y + nodeSize.y),
                            bgColor, 4.0f);
    
    // Header
    ImU32 headerColor = IM_COL32(
        static_cast<int>(node->color.r * 255),
        static_cast<int>(node->color.g * 255),
        static_cast<int>(node->color.b * 255), 255);
    drawList->AddRectFilled(nodePos, ImVec2(nodePos.x + nodeSize.x, nodePos.y + 24),
                            headerColor, 4.0f, ImDrawFlags_RoundCornersTop);
    
    // Title
    drawList->AddText(ImVec2(nodePos.x + 5, nodePos.y + 4), IM_COL32(255, 255, 255, 255),
                      node->name.c_str());
    
    // Border
    ImU32 borderColor = node->isSelected ? IM_COL32(255, 200, 100, 255) : IM_COL32(100, 100, 100, 255);
    drawList->AddRect(nodePos, ImVec2(nodePos.x + nodeSize.x, nodePos.y + nodeSize.y),
                      borderColor, 4.0f, 0, 2.0f);
    
    // Input slots
    float slotY = nodePos.y + 30;
    for (size_t i = 0; i < node->inputs.size(); ++i) {
        ImVec2 slotPos = ImVec2(nodePos.x, slotY);
        drawList->AddCircleFilled(slotPos, 5.0f, 
            node->inputs[i].isConnected ? IM_COL32(100, 200, 100, 255) : IM_COL32(150, 150, 150, 255));
        drawList->AddText(ImVec2(slotPos.x + 10, slotY - 7), IM_COL32(200, 200, 200, 255),
                          node->inputs[i].name.c_str());
        slotY += 20;
    }
    
    // Output slots
    slotY = nodePos.y + 30;
    for (size_t i = 0; i < node->outputs.size(); ++i) {
        ImVec2 slotPos = ImVec2(nodePos.x + nodeSize.x, slotY);
        drawList->AddCircleFilled(slotPos, 5.0f,
            node->outputs[i].isConnected ? IM_COL32(100, 200, 100, 255) : IM_COL32(150, 150, 150, 255));
        
        const char* name = node->outputs[i].name.c_str();
        ImVec2 textSize = ImGui::CalcTextSize(name);
        drawList->AddText(ImVec2(slotPos.x - textSize.x - 10, slotY - 7), 
                          IM_COL32(200, 200, 200, 255), name);
        slotY += 20;
    }
}

void NodeGraphEditor::drawConnections() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    
    for (const auto& conn : connections_) {
        auto srcIt = nodes_.find(conn.sourceNodeId);
        auto tgtIt = nodes_.find(conn.targetNodeId);
        if (srcIt == nodes_.end() || tgtIt == nodes_.end()) continue;
        
        auto& srcNode = srcIt->second;
        auto& tgtNode = tgtIt->second;
        
        ImVec2 startPos = ImVec2(
            canvasPos.x + viewOffset_.x + srcNode->position.x + srcNode->size.x,
            canvasPos.y + viewOffset_.y + srcNode->position.y + 30 + conn.sourceSlotIndex * 20
        );
        ImVec2 endPos = ImVec2(
            canvasPos.x + viewOffset_.x + tgtNode->position.x,
            canvasPos.y + viewOffset_.y + tgtNode->position.y + 30 + conn.targetSlotIndex * 20
        );
        
        float dist = std::abs(endPos.x - startPos.x) * 0.5f;
        drawList->AddBezierCubic(startPos,
                                  ImVec2(startPos.x + dist, startPos.y),
                                  ImVec2(endPos.x - dist, endPos.y),
                                  endPos, IM_COL32(200, 200, 200, 255), 2.0f);
    }
}

void NodeGraphEditor::drawContextMenu() {
    if (ImGui::MenuItem("Delete Selected")) {
        auto selected = getSelectedNodes();
        for (auto* node : selected) {
            removeNode(node->id);
        }
    }
}

void NodeGraphEditor::handleInput() {
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    
    // Pan with middle mouse
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        viewOffset_.x += delta.x;
        viewOffset_.y += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }
    
    // Zoom with scroll
    if (ImGui::IsWindowHovered()) {
        float scroll = ImGui::GetIO().MouseWheel;
        if (scroll != 0.0f) {
            viewZoom_ = glm::clamp(viewZoom_ + scroll * 0.1f, 0.25f, 2.0f);
        }
    }
    
    // Node selection and dragging
    ImVec2 mousePos = ImGui::GetMousePos();
    
    for (auto& [id, node] : nodes_) {
        ImVec2 nodePos = ImVec2(
            canvasPos.x + viewOffset_.x + node->position.x,
            canvasPos.y + viewOffset_.y + node->position.y
        );
        ImVec2 nodeEnd = ImVec2(nodePos.x + node->size.x, nodePos.y + node->size.y);
        
        node->isHovered = mousePos.x >= nodePos.x && mousePos.x <= nodeEnd.x &&
                          mousePos.y >= nodePos.y && mousePos.y <= nodeEnd.y;
        
        if (node->isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (!ImGui::GetIO().KeyCtrl) {
                clearSelection();
            }
            node->isSelected = true;
            selectedNodes_.insert(id);
            dragNodeId_ = id;
            isDragging_ = true;
        }
    }
    
    // Drag selected nodes
    if (isDragging_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        for (uint32_t id : selectedNodes_) {
            auto it = nodes_.find(id);
            if (it != nodes_.end()) {
                it->second->position.x += delta.x;
                it->second->position.y += delta.y;
            }
        }
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }
    
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        isDragging_ = false;
        isConnecting_ = false;
    }
}

// ============================================================================
// ANIMATION BLUEPRINT EDITOR
// ============================================================================

void AnimStateNode::drawContent() {
    ImGui::Text("Clip: %s", animationClip.c_str());
    ImGui::Text("Speed: %.2f", playbackSpeed);
}

void AnimTransitionNode::drawContent() {
    ImGui::Text("Duration: %.2fs", transitionDuration);
    ImGui::Text("Condition: %s", conditionExpression.c_str());
}

AnimationBlueprintEditor::AnimationBlueprintEditor() 
    : NodeGraphEditor("Animation Blueprint") {
}

void AnimationBlueprintEditor::draw() {
    ImGui::Begin("Animation Blueprint Editor", nullptr, ImGuiWindowFlags_MenuBar);
    
    // Menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) { nodes_.clear(); connections_.clear(); isDirty_ = true; }
            if (ImGui::MenuItem("Open...")) { /* File dialog */ }
            if (ImGui::MenuItem("Save", "Ctrl+S")) { saveBlueprint(currentFilePath_); }
            if (ImGui::MenuItem("Save As...")) { /* File dialog */ }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del")) {
                auto selected = getSelectedNodes();
                for (auto* node : selected) removeNode(node->id);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    
    // Layout
    float leftPanelWidth = 200;
    float rightPanelWidth = 250;
    float bottomPanelHeight = 150;
    
    // Left panel - State list
    ImGui::BeginChild("StateList", ImVec2(leftPanelWidth, -bottomPanelHeight), true);
    drawStateList();
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    // Center - Node graph
    ImGui::BeginChild("NodeGraph", ImVec2(-rightPanelWidth, -bottomPanelHeight), true);
    NodeGraphEditor::draw();
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    // Right panel - Properties
    ImGui::BeginChild("Properties", ImVec2(rightPanelWidth, -bottomPanelHeight), true);
    drawPropertyPanel();
    ImGui::EndChild();
    
    // Bottom panel - Preview
    ImGui::BeginChild("Preview", ImVec2(0, bottomPanelHeight), true);
    drawPreviewPanel();
    ImGui::EndChild();
    
    ImGui::End();
}

void AnimationBlueprintEditor::loadBlueprint(const std::string& path) {
    // Load from JSON file
    currentFilePath_ = path;
    isDirty_ = false;
}

void AnimationBlueprintEditor::saveBlueprint(const std::string& path) {
    if (path.empty()) return;
    // Save to JSON file
    currentFilePath_ = path;
    isDirty_ = false;
}

AnimStateNode* AnimationBlueprintEditor::createState(const std::string& name, 
                                                      const glm::vec2& position) {
    auto node = std::make_shared<AnimStateNode>();
    node->name = name;
    node->type = "State";
    node->position = position;
    node->color = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
    
    node->inputs.push_back({"In", "Transition", true});
    node->outputs.push_back({"Out", "Transition", false});
    
    addNode(node);
    isDirty_ = true;
    return node.get();
}

AnimTransitionNode* AnimationBlueprintEditor::createTransition(uint32_t fromState, 
                                                                uint32_t toState) {
    auto node = std::make_shared<AnimTransitionNode>();
    node->name = "Transition";
    node->type = "Transition";
    node->color = glm::vec4(0.5f, 0.3f, 0.2f, 1.0f);
    
    // Position between states
    auto fromIt = nodes_.find(fromState);
    auto toIt = nodes_.find(toState);
    if (fromIt != nodes_.end() && toIt != nodes_.end()) {
        node->position = (fromIt->second->position + toIt->second->position) * 0.5f;
    }
    
    node->inputs.push_back({"From", "State", true});
    node->outputs.push_back({"To", "State", false});
    
    addNode(node);
    
    // Create connections
    connect(fromState, 0, node->id, 0);
    connect(node->id, 0, toState, 0);
    
    isDirty_ = true;
    return node.get();
}

void AnimationBlueprintEditor::drawToolbar() {
    if (ImGui::Button("Add State")) {
        createState("New State", glm::vec2(100, 100) - viewOffset_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Blend Space")) {
        auto state = createState("Blend Space", glm::vec2(100, 100) - viewOffset_);
        state->isBlendSpace = true;
    }
}

void AnimationBlueprintEditor::drawStateList() {
    ImGui::Text("States");
    ImGui::Separator();
    
    ImGui::InputText("Search", &searchFilter_[0], searchFilter_.capacity());
    
    for (auto& [id, node] : nodes_) {
        if (node->type == "State") {
            bool selected = selectedNodes_.count(id) > 0;
            if (ImGui::Selectable(node->name.c_str(), selected)) {
                clearSelection();
                node->isSelected = true;
                selectedNodes_.insert(id);
            }
        }
    }
    
    ImGui::Separator();
    if (ImGui::Button("+ Add State")) {
        createState("New State", glm::vec2(200, 200));
    }
}

void AnimationBlueprintEditor::drawPreviewPanel() {
    ImGui::Text("Preview");
    ImGui::Separator();
    
    if (ImGui::Button(isPreviewPlaying_ ? "Pause" : "Play")) {
        isPreviewPlaying_ = !isPreviewPlaying_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        previewTime_ = 0.0f;
    }
    
    ImGui::SliderFloat("Time", &previewTime_, 0.0f, 1.0f);
    
    // Preview viewport would render skeleton here
    ImVec2 previewSize = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("PreviewViewport", previewSize, true);
    ImGui::Text("Skeleton Preview");
    ImGui::EndChild();
}

void AnimationBlueprintEditor::drawPropertyPanel() {
    ImGui::Text("Properties");
    ImGui::Separator();
    
    auto selected = getSelectedNodes();
    if (selected.empty()) {
        ImGui::TextDisabled("No selection");
        return;
    }
    
    auto* node = selected[0];
    
    char nameBuf[256];
    strncpy(nameBuf, node->name.c_str(), sizeof(nameBuf));
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        node->name = nameBuf;
        isDirty_ = true;
    }
    
    if (auto* state = dynamic_cast<AnimStateNode*>(node)) {
        char clipBuf[256];
        strncpy(clipBuf, state->animationClip.c_str(), sizeof(clipBuf));
        if (ImGui::InputText("Animation", clipBuf, sizeof(clipBuf))) {
            state->animationClip = clipBuf;
            isDirty_ = true;
        }
        
        if (ImGui::DragFloat("Speed", &state->playbackSpeed, 0.01f, 0.0f, 5.0f)) {
            isDirty_ = true;
        }
        
        if (ImGui::Checkbox("Looping", &state->looping)) {
            isDirty_ = true;
        }
    }
    
    if (auto* trans = dynamic_cast<AnimTransitionNode*>(node)) {
        if (ImGui::DragFloat("Duration", &trans->transitionDuration, 0.01f, 0.0f, 2.0f)) {
            isDirty_ = true;
        }
        
        char condBuf[512];
        strncpy(condBuf, trans->conditionExpression.c_str(), sizeof(condBuf));
        if (ImGui::InputText("Condition", condBuf, sizeof(condBuf))) {
            trans->conditionExpression = condBuf;
            isDirty_ = true;
        }
    }
}

void AnimationBlueprintEditor::drawContextMenu() {
    if (ImGui::MenuItem("Add State")) {
        ImVec2 mousePos = ImGui::GetMousePos();
        createState("New State", glm::vec2(mousePos.x, mousePos.y) - viewOffset_);
    }
    if (ImGui::MenuItem("Add Blend Space 1D")) {
        auto* state = createState("Blend Space 1D", glm::vec2(200, 200));
        state->isBlendSpace = true;
    }
    ImGui::Separator();
    NodeGraphEditor::drawContextMenu();
}

// ============================================================================
// AI DEBUGGER
// ============================================================================

AIDebugger::AIDebugger() : EditorWindow("AI Debugger") {}

void AIDebugger::draw() {
    ImGui::Begin("AI Debugger", &isOpen_);
    
    // Toolbar
    if (ImGui::Button("Pause")) { pauseOnCondition_ = true; }
    ImGui::SameLine();
    if (ImGui::Button("Step")) { /* Single step */ }
    ImGui::SameLine();
    if (ImGui::Button("Continue")) { pauseOnCondition_ = false; }
    
    ImGui::Separator();
    
    // Tabs
    if (ImGui::BeginTabBar("AIDebugTabs")) {
        if (ImGui::BeginTabItem("Behavior Tree")) {
            drawBehaviorTreeView();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Blackboard")) {
            drawBlackboardView();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Navigation")) {
            drawNavigationView();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("AI State")) {
            drawAIStateView();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    
    ImGui::End();
}

void AIDebugger::setTarget(Entity entity) {
    targetEntity_ = entity;
    updateBTVisualization();
}

void AIDebugger::setBehaviorTree(BehaviorTreeAsset* tree) {
    behaviorTree_ = tree;
    updateBTVisualization();
}

void AIDebugger::update(float deltaTime) {
    // Update execution history
    // Track node status changes
}

void AIDebugger::drawBehaviorTreeView() {
    ImGui::BeginChild("BTView", ImVec2(0, 0), true);
    
    if (debugNodes_.empty()) {
        ImGui::TextDisabled("No behavior tree loaded");
    } else {
        auto rootIt = debugNodes_.find(rootNodeId_);
        if (rootIt != debugNodes_.end()) {
            drawTreeNode(rootIt->second, 0);
        }
    }
    
    ImGui::EndChild();
}

void AIDebugger::drawTreeNode(BTDebugNode& node, int depth) {
    // Indent
    ImGui::Indent(depth * 20.0f);
    
    // Status color
    ImVec4 statusColor;
    switch (node.lastStatus) {
        case BTNodeStatus::Success: statusColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); break;
        case BTNodeStatus::Failure: statusColor = ImVec4(0.8f, 0.2f, 0.2f, 1.0f); break;
        case BTNodeStatus::Running: statusColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f); break;
        default: statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break;
    }
    
    ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
    
    // Node header
    bool open = true;
    if (!node.childIds.empty()) {
        open = ImGui::TreeNodeEx(node.name.c_str(), 
            node.isExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        node.isExpanded = open;
    } else {
        ImGui::BulletText("%s", node.name.c_str());
    }
    
    ImGui::PopStyleColor();
    
    // Tooltip with details
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Type: %s", node.type.c_str());
        ImGui::Text("Executions: %d", node.executionCount);
        ImGui::Text("Last run: %.2fs ago", node.lastExecutionTime);
        ImGui::EndTooltip();
    }
    
    // Children
    if (open && !node.childIds.empty()) {
        for (uint32_t childId : node.childIds) {
            auto it = debugNodes_.find(childId);
            if (it != debugNodes_.end()) {
                drawTreeNode(it->second, depth + 1);
            }
        }
        if (!node.childIds.empty()) {
            ImGui::TreePop();
        }
    }
    
    ImGui::Unindent(depth * 20.0f);
}

void AIDebugger::updateBTVisualization() {
    debugNodes_.clear();
    nextDebugId_ = 1;
    
    if (behaviorTree_ && behaviorTree_->root) {
        buildDebugTree(behaviorTree_->root.get(), 0);
    }
}

void AIDebugger::buildDebugTree(BTNode* node, uint32_t parentId) {
    BTDebugNode debugNode;
    debugNode.id = nextDebugId_++;
    debugNode.name = node->name;
    debugNode.type = "Node";  // Would use RTTI or virtual method
    debugNode.parentId = parentId;
    
    if (parentId == 0) {
        rootNodeId_ = debugNode.id;
    } else {
        debugNodes_[parentId].childIds.push_back(debugNode.id);
    }
    
    uint32_t currentId = debugNode.id;
    debugNodes_[currentId] = debugNode;
    
    // Recursively add children
    for (auto& child : node->children) {
        buildDebugTree(child.get(), currentId);
    }
}

void AIDebugger::drawBlackboardView() {
    ImGui::InputText("Filter", &bbSearchFilter_[0], bbSearchFilter_.capacity());
    ImGui::Separator();
    
    ImGui::BeginChild("BBVars", ImVec2(0, 0), true);
    
    if (blackboard_) {
        ImGui::Columns(3, "BBColumns");
        ImGui::Text("Name"); ImGui::NextColumn();
        ImGui::Text("Type"); ImGui::NextColumn();
        ImGui::Text("Value"); ImGui::NextColumn();
        ImGui::Separator();
        
        for (const auto& var : blackboardVars_) {
            if (!bbSearchFilter_.empty() && 
                var.name.find(bbSearchFilter_) == std::string::npos) continue;
            
            if (var.isModified) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
            }
            
            ImGui::Text("%s", var.name.c_str()); ImGui::NextColumn();
            ImGui::Text("%s", var.type.c_str()); ImGui::NextColumn();
            ImGui::Text("%s", var.value.c_str()); ImGui::NextColumn();
            
            if (var.isModified) {
                ImGui::PopStyleColor();
            }
        }
        
        ImGui::Columns(1);
    } else {
        ImGui::TextDisabled("No blackboard data");
    }
    
    ImGui::EndChild();
}

void AIDebugger::drawNavigationView() {
    ImGui::Checkbox("Show NavMesh", &showNavMesh_);
    ImGui::Checkbox("Show Current Path", &showCurrentPath_);
    ImGui::Checkbox("Show Crowd Agents", &showCrowdAgents_);
    
    ImGui::Separator();
    
    // Navigation stats
    ImGui::Text("Path Status: Valid");
    ImGui::Text("Path Length: 15.3m");
    ImGui::Text("Waypoints: 5");
    ImGui::Text("Current Waypoint: 2");
}

void AIDebugger::drawAIStateView() {
    ImGui::Text("Current State: Patrol");
    ImGui::Text("Target: Player");
    ImGui::Text("Alert Level: 0.3");
    
    ImGui::Separator();
    ImGui::Text("Execution History");
    
    ImGui::BeginChild("History", ImVec2(0, 0), true);
    for (const auto& [time, entry] : executionHistory_) {
        ImGui::Text("[%.2f] %s", time, entry.c_str());
    }
    ImGui::EndChild();
}

// ============================================================================
// COMBAT DESIGNER
// ============================================================================

CombatDesigner::CombatDesigner() : EditorWindow("Combat Designer") {}

void CombatDesigner::draw() {
    ImGui::Begin("Combat Designer", &isOpen_, ImGuiWindowFlags_MenuBar);
    
    // Menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) { hitboxes_.clear(); comboNodes_.clear(); }
            if (ImGui::MenuItem("Open...")) {}
            if (ImGui::MenuItem("Save")) { saveCombatData(currentFilePath_); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Hitbox Mode", nullptr, mode_ == EditorMode::Hitbox))
                mode_ = EditorMode::Hitbox;
            if (ImGui::MenuItem("Combo Mode", nullptr, mode_ == EditorMode::Combo))
                mode_ = EditorMode::Combo;
            if (ImGui::MenuItem("Timeline Mode", nullptr, mode_ == EditorMode::Timeline))
                mode_ = EditorMode::Timeline;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    
    drawToolbar();
    ImGui::Separator();
    
    // Main content based on mode
    float panelWidth = 250.0f;
    
    ImGui::BeginChild("LeftPanel", ImVec2(panelWidth, -150), true);
    switch (mode_) {
        case EditorMode::Hitbox: drawHitboxPanel(); break;
        case EditorMode::Combo: drawComboGraphPanel(); break;
        case EditorMode::Timeline: break;
    }
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    // Preview/main viewport
    ImGui::BeginChild("MainView", ImVec2(-panelWidth, -150), true);
    drawPreviewPanel();
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    // Property panel
    ImGui::BeginChild("RightPanel", ImVec2(panelWidth, -150), true);
    drawPropertyPanel();
    ImGui::EndChild();
    
    // Timeline
    ImGui::BeginChild("Timeline", ImVec2(0, 150), true);
    drawTimelinePanel();
    ImGui::EndChild();
    
    ImGui::End();
}

void CombatDesigner::loadCombatData(const std::string& path) {
    currentFilePath_ = path;
    isDirty_ = false;
}

void CombatDesigner::saveCombatData(const std::string& path) {
    if (path.empty()) return;
    currentFilePath_ = path;
    isDirty_ = false;
}

HitboxVisual* CombatDesigner::addHitbox(const std::string& name, HitboxType type) {
    HitboxVisual hb;
    hb.id = nextHitboxId_++;
    hb.name = name;
    hb.type = type;
    hb.halfExtents = glm::vec3(0.3f);
    hb.color = (type == HitboxType::Damage) ? 
               glm::vec4(1, 0, 0, 0.5f) : glm::vec4(0, 1, 0, 0.5f);
    
    hitboxes_.push_back(hb);
    isDirty_ = true;
    return &hitboxes_.back();
}

ComboNode* CombatDesigner::addComboNode(const std::string& attackName) {
    ComboNode node;
    node.id = nextComboNodeId_++;
    node.attackName = attackName;
    node.position = glm::vec2(100 + comboNodes_.size() * 150, 100);
    
    comboNodes_.push_back(node);
    isDirty_ = true;
    return &comboNodes_.back();
}

void CombatDesigner::drawToolbar() {
    // Mode buttons
    if (ImGui::RadioButton("Hitbox", mode_ == EditorMode::Hitbox)) 
        mode_ = EditorMode::Hitbox;
    ImGui::SameLine();
    if (ImGui::RadioButton("Combo", mode_ == EditorMode::Combo)) 
        mode_ = EditorMode::Combo;
    ImGui::SameLine();
    if (ImGui::RadioButton("Timeline", mode_ == EditorMode::Timeline)) 
        mode_ = EditorMode::Timeline;
    
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();
    
    // Playback controls
    if (ImGui::Button(isPlaying_ ? "||" : ">")) isPlaying_ = !isPlaying_;
    ImGui::SameLine();
    if (ImGui::Button("|<")) { currentFrame_ = 0; playbackTime_ = 0; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderInt("Frame", &currentFrame_, 0, totalFrames_);
}

void CombatDesigner::drawHitboxPanel() {
    ImGui::Text("Hitboxes");
    ImGui::Separator();
    
    for (auto& hb : hitboxes_) {
        bool selected = hb.id == selectedHitboxId_;
        if (ImGui::Selectable(hb.name.c_str(), selected)) {
            selectedHitboxId_ = hb.id;
        }
    }
    
    ImGui::Separator();
    if (ImGui::Button("+ Damage Box")) {
        addHitbox("DamageBox", HitboxType::Damage);
    }
    if (ImGui::Button("+ Hurt Box")) {
        addHitbox("HurtBox", HitboxType::Hurt);
    }
}

void CombatDesigner::drawComboGraphPanel() {
    ImGui::Text("Combo Nodes");
    ImGui::Separator();
    
    for (auto& node : comboNodes_) {
        bool selected = node.id == selectedComboNodeId_;
        if (ImGui::Selectable(node.attackName.c_str(), selected)) {
            selectedComboNodeId_ = node.id;
        }
    }
    
    ImGui::Separator();
    if (ImGui::Button("+ Attack")) {
        addComboNode("Attack");
    }
}

void CombatDesigner::drawTimelinePanel() {
    ImGui::Text("Timeline - Frame %d / %d", currentFrame_, totalFrames_);
    
    // Draw timeline ruler
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    
    float frameWidth = size.x / totalFrames_;
    
    // Background
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + 30), IM_COL32(40, 40, 40, 255));
    
    // Frame markers
    for (int i = 0; i <= totalFrames_; i += 5) {
        float x = pos.x + i * frameWidth;
        drawList->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + 15), IM_COL32(100, 100, 100, 255));
        if (i % 10 == 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", i);
            drawList->AddText(ImVec2(x + 2, pos.y + 15), IM_COL32(150, 150, 150, 255), buf);
        }
    }
    
    // Current frame indicator
    float curX = pos.x + currentFrame_ * frameWidth;
    drawList->AddLine(ImVec2(curX, pos.y), ImVec2(curX, pos.y + size.y), IM_COL32(255, 100, 100, 255), 2.0f);
    
    // Hitbox tracks
    float trackY = pos.y + 35;
    float trackHeight = 20;
    
    for (const auto& hb : hitboxes_) {
        // Track background
        drawList->AddRectFilled(
            ImVec2(pos.x, trackY),
            ImVec2(pos.x + size.x, trackY + trackHeight),
            IM_COL32(50, 50, 50, 255)
        );
        
        // Active range
        float startX = pos.x + hb.startFrame * frameWidth;
        float endX = pos.x + hb.endFrame * frameWidth;
        ImU32 color = (hb.type == HitboxType::Damage) ? 
                      IM_COL32(200, 80, 80, 200) : IM_COL32(80, 200, 80, 200);
        drawList->AddRectFilled(
            ImVec2(startX, trackY + 2),
            ImVec2(endX, trackY + trackHeight - 2),
            color, 3.0f
        );
        
        // Label
        drawList->AddText(ImVec2(startX + 3, trackY + 2), IM_COL32(255, 255, 255, 255), hb.name.c_str());
        
        trackY += trackHeight + 2;
    }
}

void CombatDesigner::drawPreviewPanel() {
    ImGui::Text("3D Preview");
    
    // Would render skeleton with hitbox gizmos
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("3DView", size, true);
    
    ImGui::TextDisabled("Skeleton preview with hitboxes");
    
    // Gizmo controls
    if (ImGui::RadioButton("Translate", gizmoMode_ == GizmoMode::Translate))
        gizmoMode_ = GizmoMode::Translate;
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", gizmoMode_ == GizmoMode::Rotate))
        gizmoMode_ = GizmoMode::Rotate;
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", gizmoMode_ == GizmoMode::Scale))
        gizmoMode_ = GizmoMode::Scale;
    
    ImGui::EndChild();
}

void CombatDesigner::drawPropertyPanel() {
    ImGui::Text("Properties");
    ImGui::Separator();
    
    if (mode_ == EditorMode::Hitbox && selectedHitboxId_ > 0) {
        for (auto& hb : hitboxes_) {
            if (hb.id != selectedHitboxId_) continue;
            
            char nameBuf[128];
            strncpy(nameBuf, hb.name.c_str(), sizeof(nameBuf));
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                hb.name = nameBuf;
                isDirty_ = true;
            }
            
            ImGui::DragFloat3("Position", &hb.localPosition.x, 0.01f);
            ImGui::DragFloat3("Size", &hb.halfExtents.x, 0.01f, 0.01f, 10.0f);
            
            ImGui::Separator();
            ImGui::Text("Frame Range");
            ImGui::DragInt("Start", &hb.startFrame, 1, 0, totalFrames_);
            ImGui::DragInt("End", &hb.endFrame, 1, 0, totalFrames_);
            
            break;
        }
    } else if (mode_ == EditorMode::Combo && selectedComboNodeId_ > 0) {
        for (auto& node : comboNodes_) {
            if (node.id != selectedComboNodeId_) continue;
            
            char nameBuf[128];
            strncpy(nameBuf, node.attackName.c_str(), sizeof(nameBuf));
            if (ImGui::InputText("Attack Name", nameBuf, sizeof(nameBuf))) {
                node.attackName = nameBuf;
                isDirty_ = true;
            }
            
            ImGui::DragFloat("Damage", &node.damage, 0.5f, 0.0f, 100.0f);
            ImGui::DragFloat("Knockback", &node.knockback, 0.1f, 0.0f, 50.0f);
            ImGui::DragFloat3("KB Direction", &node.knockbackDirection.x, 0.01f);
            
            ImGui::Separator();
            ImGui::Text("Timing (frames)");
            ImGui::DragInt("Hit Frame", &node.hitFrame, 1, 0, 60);
            ImGui::DragInt("Recovery", &node.recoveryFrames, 1, 0, 60);
            ImGui::DragInt("Cancel Start", &node.cancelWindowStart, 1, 0, 60);
            ImGui::DragInt("Cancel End", &node.cancelWindowEnd, 1, 0, 60);
            
            break;
        }
    } else {
        ImGui::TextDisabled("Select an item");
    }
}

void CombatDesigner::updatePreview(float deltaTime) {
    if (isPlaying_) {
        playbackTime_ += deltaTime;
        currentFrame_ = static_cast<int>(playbackTime_ * frameRate_) % totalFrames_;
    }
}

// ============================================================================
// PROPERTY EDITOR
// ============================================================================

PropertyEditor::PropertyEditor() : EditorWindow("Properties") {}

void PropertyEditor::draw() {
    ImGui::Begin("Properties", &isOpen_);
    
    ImGui::InputText("Search", &searchFilter_[0], searchFilter_.capacity());
    ImGui::Checkbox("Show Advanced", &showAdvanced_);
    ImGui::Separator();
    
    // Group by category
    for (auto& [category, props] : categories_) {
        bool collapsed = collapsedCategories_.count(category) > 0;
        if (ImGui::CollapsingHeader(category.c_str(), collapsed ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) {
            collapsedCategories_.erase(category);
            for (auto* prop : props) {
                if (prop->isAdvanced && !showAdvanced_) continue;
                if (!searchFilter_.empty() && 
                    prop->displayName.find(searchFilter_) == std::string::npos) continue;
                drawProperty(*prop);
            }
        } else {
            collapsedCategories_.insert(category);
        }
    }
    
    ImGui::End();
}

void PropertyEditor::setProperties(const std::vector<PropertyDef>& properties) {
    properties_ = properties;
    categories_.clear();
    
    for (auto& prop : properties_) {
        categories_[prop.category].push_back(&prop);
    }
}

void PropertyEditor::clear() {
    properties_.clear();
    categories_.clear();
}

void PropertyEditor::addProperty(const PropertyDef& property) {
    properties_.push_back(property);
    categories_[property.category].push_back(&properties_.back());
}

void PropertyEditor::drawProperty(PropertyDef& prop) {
    if (prop.isReadOnly) {
        ImGui::BeginDisabled();
    }
    
    std::string value = prop.getter();
    
    switch (prop.type) {
        case PropertyType::Bool: {
            bool b = (value == "true");
            if (ImGui::Checkbox(prop.displayName.c_str(), &b)) {
                prop.setter(b ? "true" : "false");
            }
            break;
        }
        case PropertyType::Int: {
            int i = std::stoi(value);
            if (ImGui::DragInt(prop.displayName.c_str(), &i, 1, 
                               static_cast<int>(prop.minValue), 
                               static_cast<int>(prop.maxValue))) {
                prop.setter(std::to_string(i));
            }
            break;
        }
        case PropertyType::Float: {
            float f = std::stof(value);
            if (ImGui::DragFloat(prop.displayName.c_str(), &f, 0.01f, 
                                  prop.minValue, prop.maxValue)) {
                prop.setter(std::to_string(f));
            }
            break;
        }
        case PropertyType::String: {
            char buf[256];
            strncpy(buf, value.c_str(), sizeof(buf));
            if (ImGui::InputText(prop.displayName.c_str(), buf, sizeof(buf))) {
                prop.setter(buf);
            }
            break;
        }
        case PropertyType::Color: {
            // Parse "r,g,b,a" format
            float col[4] = {1, 1, 1, 1};
            sscanf(value.c_str(), "%f,%f,%f,%f", &col[0], &col[1], &col[2], &col[3]);
            if (ImGui::ColorEdit4(prop.displayName.c_str(), col)) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%f,%f,%f,%f", col[0], col[1], col[2], col[3]);
                prop.setter(buf);
            }
            break;
        }
        case PropertyType::Enum: {
            int current = 0;
            for (size_t i = 0; i < prop.enumOptions.size(); ++i) {
                if (prop.enumOptions[i] == value) {
                    current = static_cast<int>(i);
                    break;
                }
            }
            if (ImGui::BeginCombo(prop.displayName.c_str(), 
                                   current < prop.enumOptions.size() ? 
                                   prop.enumOptions[current].c_str() : "")) {
                for (size_t i = 0; i < prop.enumOptions.size(); ++i) {
                    bool selected = (current == static_cast<int>(i));
                    if (ImGui::Selectable(prop.enumOptions[i].c_str(), selected)) {
                        prop.setter(prop.enumOptions[i]);
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        default:
            ImGui::Text("%s: %s", prop.displayName.c_str(), value.c_str());
            break;
    }
    
    if (!prop.tooltip.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", prop.tooltip.c_str());
    }
    
    if (prop.isReadOnly) {
        ImGui::EndDisabled();
    }
}

// ============================================================================
// EDITOR MANAGER
// ============================================================================

EditorManager& EditorManager::getInstance() {
    static EditorManager instance;
    return instance;
}

void EditorManager::init() {
    animEditor_ = registerWindow<AnimationBlueprintEditor>();
    aiDebugger_ = registerWindow<AIDebugger>();
    combatDesigner_ = registerWindow<CombatDesigner>();
    propertyEditor_ = registerWindow<PropertyEditor>();
}

void EditorManager::shutdown() {
    windows_.clear();
}

void EditorManager::draw() {
    // Main menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Windows")) {
            for (auto& [name, window] : windows_) {
                bool open = window->isOpen();
                if (ImGui::MenuItem(name.c_str(), nullptr, &open)) {
                    window->setOpen(open);
                }
            }
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &showDemoWindow_);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    
    // Draw windows
    for (auto& [name, window] : windows_) {
        if (window->isOpen()) {
            window->draw();
        }
    }
    
    if (showDemoWindow_) {
        ImGui::ShowDemoWindow(&showDemoWindow_);
    }
}

void EditorManager::toggleWindow(const std::string& name) {
    auto it = windows_.find(name);
    if (it != windows_.end()) {
        it->second->setOpen(!it->second->isOpen());
    }
}

// ============================================================================
// SPLINE EDITOR TOOL
// ============================================================================

SplineEditorTool::SplineEditorTool() = default;

SplineEditorTool::~SplineEditorTool() = default;

void SplineEditorTool::setSpline(SplineComponent* spline) {
    selectedSpline_ = spline;
    selectedPoint_ = -1;
    pointHandles_.clear();
    
    if (spline) {
        updatePointHandles();
    }
}

void SplineEditorTool::update(const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                               const glm::vec2& viewportSize) {
    viewMatrix_ = viewMatrix;
    projMatrix_ = projMatrix;
    viewportSize_ = viewportSize;
    
    // Extract camera position from inverse view matrix
    glm::mat4 invView = glm::inverse(viewMatrix);
    cameraPosition_ = glm::vec3(invView[3]);
    cameraForward_ = -glm::vec3(invView[2]);
    
    if (selectedSpline_) {
        updatePointHandles();
    }
}

void SplineEditorTool::handleInput(const glm::vec2& mousePos, bool mouseDown,
                                    const glm::vec2& mouseDelta, bool shiftHeld, bool ctrlHeld) {
    if (!selectedSpline_) return;
    
    // Check for point selection on mouse down
    if (mouseDown && !wasMouseDown_) {
        int pickedPoint = pickPoint(mousePos);
        if (pickedPoint >= 0) {
            if (ctrlHeld) {
                // Multi-select (toggle)
                if (selectedPoint_ == pickedPoint) {
                    selectedPoint_ = -1;
                } else {
                    selectedPoint_ = pickedPoint;
                }
            } else {
                selectedPoint_ = pickedPoint;
            }
            isGizmoDragging_ = true;
            gizmoDragStart_ = selectedSpline_->getControlPoint(selectedPoint_).position;
            gizmoDragOffset_ = glm::vec3(0);
        } else if (!ctrlHeld) {
            // Clicked on empty space - deselect
            selectedPoint_ = -1;
        }
    }
    
    // Handle dragging
    if (isGizmoDragging_ && mouseDown && selectedPoint_ >= 0) {
        glm::vec3 newPos = handleGizmoInput(gizmoDragStart_ + gizmoDragOffset_);
        gizmoDragOffset_ = newPos - gizmoDragStart_;
        
        // Update spline point
        auto& point = selectedSpline_->getControlPoint(selectedPoint_);
        point.position = newPos;
        selectedSpline_->rebuildDistanceTable();
        notifySplineChanged();
    }
    
    // End dragging
    if (!mouseDown && wasMouseDown_) {
        isGizmoDragging_ = false;
    }
    
    lastMousePos_ = mousePos;
    wasMouseDown_ = mouseDown;
}

void SplineEditorTool::draw() {
    if (!selectedSpline_) return;
    
    drawSplineCurve();
    drawControlPoints();
    
    if (config_.showTangentHandles) {
        drawTangentHandles();
    }
    
    // Draw gizmo for selected point
    if (selectedPoint_ >= 0 && selectedPoint_ < static_cast<int>(selectedSpline_->getPointCount())) {
        glm::vec3 pos = selectedSpline_->getControlPoint(selectedPoint_).position;
        drawTranslateGizmo(pos, true);
    }
}

bool SplineEditorTool::drawTranslateGizmo(const glm::vec3& worldPos, bool isSelected) {
    if (!isSelected) return false;
    
    float gizmoSize = glm::length(worldPos - cameraPosition_) * 0.1f;
    
    // X axis (red)
    drawGizmoArrow(worldPos, glm::vec3(1, 0, 0), gizmoSize, 
                   glm::vec4(1, 0.2f, 0.2f, 1), 0);
    
    // Y axis (green)
    drawGizmoArrow(worldPos, glm::vec3(0, 1, 0), gizmoSize,
                   glm::vec4(0.2f, 1, 0.2f, 1), 1);
    
    // Z axis (blue)
    drawGizmoArrow(worldPos, glm::vec3(0, 0, 1), gizmoSize,
                   glm::vec4(0.2f, 0.2f, 1, 1), 2);
    
    return activeAxis_ >= 0;
}

glm::vec3 SplineEditorTool::handleGizmoInput(const glm::vec3& worldPos) {
    if (!isGizmoDragging_) return worldPos;
    
    glm::vec3 rayOrigin, rayDir;
    screenToWorldRay(lastMousePos_, rayDir);
    rayOrigin = cameraPosition_;
    
    glm::vec3 result = worldPos;
    
    // Project mouse movement onto active axis or plane
    if (activeAxis_ >= 0) {
        glm::vec3 axisDir(0);
        axisDir[activeAxis_] = 1.0f;
        
        // Find closest point on axis to ray
        glm::vec3 w0 = rayOrigin - worldPos;
        float a = glm::dot(axisDir, axisDir);
        float b = glm::dot(axisDir, rayDir);
        float c = glm::dot(rayDir, rayDir);
        float d = glm::dot(axisDir, w0);
        float e = glm::dot(rayDir, w0);
        
        float denom = a * c - b * b;
        if (std::abs(denom) > 0.0001f) {
            float t = (b * e - c * d) / denom;
            result = worldPos + axisDir * t;
        }
    } else {
        // Free movement on camera-facing plane
        glm::vec3 planeNormal = cameraForward_;
        float t;
        if (rayPlaneIntersect(rayOrigin, rayDir, worldPos, planeNormal, t)) {
            result = rayOrigin + rayDir * t;
        }
    }
    
    return applySnapping(result);
}

void SplineEditorTool::selectPoint(int index) {
    if (selectedSpline_ && index >= 0 && 
        index < static_cast<int>(selectedSpline_->getPointCount())) {
        selectedPoint_ = index;
    } else {
        selectedPoint_ = -1;
    }
}

void SplineEditorTool::addPoint(const glm::vec3& position) {
    if (!selectedSpline_) return;
    
    SplinePoint point;
    point.position = position;
    point.rotation = glm::quat(1, 0, 0, 0);
    point.scale = glm::vec3(1);
    
    selectedSpline_->addControlPoint(point);
    selectedSpline_->rebuildDistanceTable();
    
    selectedPoint_ = static_cast<int>(selectedSpline_->getPointCount()) - 1;
    updatePointHandles();
    notifySplineChanged();
}

void SplineEditorTool::insertPointAfterSelected(const glm::vec3& position) {
    if (!selectedSpline_ || selectedPoint_ < 0) {
        addPoint(position);
        return;
    }
    
    SplinePoint point;
    point.position = position;
    point.rotation = glm::quat(1, 0, 0, 0);
    point.scale = glm::vec3(1);
    
    selectedSpline_->insertControlPoint(selectedPoint_ + 1, point);
    selectedSpline_->rebuildDistanceTable();
    
    selectedPoint_ = selectedPoint_ + 1;
    updatePointHandles();
    notifySplineChanged();
}

void SplineEditorTool::deleteSelectedPoint() {
    if (!selectedSpline_ || selectedPoint_ < 0) return;
    if (selectedSpline_->getPointCount() <= 2) return;  // Keep at least 2 points
    
    selectedSpline_->removeControlPoint(selectedPoint_);
    selectedSpline_->rebuildDistanceTable();
    
    if (selectedPoint_ >= static_cast<int>(selectedSpline_->getPointCount())) {
        selectedPoint_ = static_cast<int>(selectedSpline_->getPointCount()) - 1;
    }
    
    updatePointHandles();
    notifySplineChanged();
}

void SplineEditorTool::duplicateSelectedPoint() {
    if (!selectedSpline_ || selectedPoint_ < 0) return;
    
    const auto& srcPoint = selectedSpline_->getControlPoint(selectedPoint_);
    glm::vec3 offset = glm::vec3(10, 0, 0);  // Offset duplicated point
    
    insertPointAfterSelected(srcPoint.position + offset);
}

void SplineEditorTool::updatePointHandles() {
    if (!selectedSpline_) return;
    
    size_t pointCount = selectedSpline_->getPointCount();
    pointHandles_.resize(pointCount);
    
    for (size_t i = 0; i < pointCount; ++i) {
        const auto& point = selectedSpline_->getControlPoint(static_cast<int>(i));
        
        pointHandles_[i].pointIndex = static_cast<int>(i);
        pointHandles_[i].worldPosition = point.position;
        pointHandles_[i].screenPosition = worldToScreen(point.position);
        pointHandles_[i].isSelected = (static_cast<int>(i) == selectedPoint_);
        pointHandles_[i].isHovered = false;
    }
}

glm::vec2 SplineEditorTool::worldToScreen(const glm::vec3& worldPos) {
    glm::vec4 clipSpace = projMatrix_ * viewMatrix_ * glm::vec4(worldPos, 1.0f);
    
    if (clipSpace.w <= 0.0f) {
        return glm::vec2(-1000, -1000);  // Behind camera
    }
    
    glm::vec3 ndc = glm::vec3(clipSpace) / clipSpace.w;
    
    return glm::vec2(
        (ndc.x + 1.0f) * 0.5f * viewportSize_.x,
        (1.0f - ndc.y) * 0.5f * viewportSize_.y  // Y is flipped
    );
}

glm::vec3 SplineEditorTool::screenToWorldRay(const glm::vec2& screenPos, glm::vec3& rayDir) {
    // Convert screen to NDC
    float ndcX = (screenPos.x / viewportSize_.x) * 2.0f - 1.0f;
    float ndcY = 1.0f - (screenPos.y / viewportSize_.y) * 2.0f;
    
    // Unproject
    glm::mat4 invProjView = glm::inverse(projMatrix_ * viewMatrix_);
    
    glm::vec4 nearPoint = invProjView * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPoint = invProjView * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    
    rayDir = glm::normalize(glm::vec3(farPoint) - glm::vec3(nearPoint));
    return glm::vec3(nearPoint);
}

bool SplineEditorTool::rayPlaneIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                          const glm::vec3& planePoint, const glm::vec3& planeNormal,
                                          float& t) {
    float denom = glm::dot(planeNormal, rayDir);
    if (std::abs(denom) < 0.0001f) return false;
    
    t = glm::dot(planePoint - rayOrigin, planeNormal) / denom;
    return t >= 0;
}

int SplineEditorTool::pickPoint(const glm::vec2& screenPos, float threshold) {
    int closestPoint = -1;
    float closestDist = threshold;
    
    for (const auto& handle : pointHandles_) {
        float dist = glm::length(screenPos - handle.screenPosition);
        if (dist < closestDist) {
            closestDist = dist;
            closestPoint = handle.pointIndex;
        }
    }
    
    return closestPoint;
}

void SplineEditorTool::drawSplineCurve() {
    if (!selectedSpline_ || selectedSpline_->getPointCount() < 2) return;
    
    std::vector<glm::vec3> curvePoints;
    int segments = config_.curveSegments * (static_cast<int>(selectedSpline_->getPointCount()) - 1);
    
    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        curvePoints.push_back(selectedSpline_->evaluatePosition(t));
    }
    
    // Draw curve
    glm::vec4 color = (selectedPoint_ >= 0) ? config_.selectedSplineColor : config_.splineColor;
    DebugDraw::drawPath(curvePoints, color);
    
    // Draw distance markers if enabled
    if (config_.showDistanceMarkers) {
        float totalLength = selectedSpline_->getTotalLength();
        for (float dist = 0; dist < totalLength; dist += config_.distanceMarkerInterval) {
            float t = selectedSpline_->distanceToParameter(dist);
            glm::vec3 pos = selectedSpline_->evaluatePosition(t);
            DebugDraw::drawSphere(pos, 0.5f, glm::vec4(1, 1, 0, 0.5f), false);
        }
    }
}

void SplineEditorTool::drawControlPoints() {
    for (const auto& handle : pointHandles_) {
        glm::vec4 color = config_.pointColor;
        float size = config_.pointHandleSize;
        
        if (handle.isSelected) {
            color = config_.selectedPointColor;
            size *= config_.handleHoverGrow;
        } else if (handle.isHovered) {
            color = config_.hoveredPointColor;
            size *= config_.handleHoverGrow;
        }
        
        // Draw point as sphere in 3D
        DebugDraw::drawSphere(handle.worldPosition, size * 0.1f, color, true);
    }
}

void SplineEditorTool::drawTangentHandles() {
    if (!selectedSpline_ || selectedPoint_ < 0) return;
    
    // For Bezier splines, draw tangent handles
    const auto& point = selectedSpline_->getControlPoint(selectedPoint_);
    
    // Calculate tangent at this point
    int numPoints = static_cast<int>(selectedSpline_->getPointCount());
    if (numPoints < 2) return;
    
    float t = static_cast<float>(selectedPoint_) / static_cast<float>(numPoints - 1);
    glm::vec3 tangent = selectedSpline_->evaluateTangent(t);
    
    float handleLength = 20.0f;
    glm::vec3 tangentIn = point.position - tangent * handleLength;
    glm::vec3 tangentOut = point.position + tangent * handleLength;
    
    // Draw tangent lines
    DebugDraw::drawLine(tangentIn, point.position, config_.tangentLineColor);
    DebugDraw::drawLine(point.position, tangentOut, config_.tangentLineColor);
    
    // Draw tangent handles
    DebugDraw::drawSphere(tangentIn, config_.tangentHandleSize * 0.05f, config_.tangentHandleColor, true);
    DebugDraw::drawSphere(tangentOut, config_.tangentHandleSize * 0.05f, config_.tangentHandleColor, true);
}

void SplineEditorTool::drawGizmoArrow(const glm::vec3& origin, const glm::vec3& direction,
                                       float length, const glm::vec4& color, int axis) {
    glm::vec3 end = origin + direction * length;
    
    // Highlight if this is the active axis
    glm::vec4 drawColor = color;
    if (activeAxis_ == axis) {
        drawColor = glm::vec4(1, 1, 0, 1);  // Yellow when active
    }
    
    DebugDraw::drawArrow(origin, end, drawColor, length * 0.15f);
}

glm::vec3 SplineEditorTool::applySnapping(const glm::vec3& position) {
    if (!config_.enableSnapping) return position;
    
    glm::vec3 snapped = position;
    
    snapped.x = std::round(snapped.x / config_.snapGridSize) * config_.snapGridSize;
    snapped.y = std::round(snapped.y / config_.snapGridSize) * config_.snapGridSize;
    snapped.z = std::round(snapped.z / config_.snapGridSize) * config_.snapGridSize;
    
    return snapped;
}

void SplineEditorTool::notifySplineChanged() {
    for (auto& callback : changeCallbacks_) {
        callback(selectedSpline_);
    }
}

// ============================================================================
// DEBUG DRAW
// ============================================================================

std::vector<DebugDraw::DebugLine> DebugDraw::lines_;

void DebugDraw::drawLine(const glm::vec3& start, const glm::vec3& end,
                          const glm::vec4& color, float thickness) {
    lines_.push_back({start, end, color, thickness});
}

void DebugDraw::drawBox(const glm::vec3& center, const glm::vec3& halfExtents,
                         const glm::quat& rotation, const glm::vec4& color, bool filled) {
    // 8 corners
    glm::vec3 corners[8] = {
        center + rotation * glm::vec3(-halfExtents.x, -halfExtents.y, -halfExtents.z),
        center + rotation * glm::vec3( halfExtents.x, -halfExtents.y, -halfExtents.z),
        center + rotation * glm::vec3( halfExtents.x,  halfExtents.y, -halfExtents.z),
        center + rotation * glm::vec3(-halfExtents.x,  halfExtents.y, -halfExtents.z),
        center + rotation * glm::vec3(-halfExtents.x, -halfExtents.y,  halfExtents.z),
        center + rotation * glm::vec3( halfExtents.x, -halfExtents.y,  halfExtents.z),
        center + rotation * glm::vec3( halfExtents.x,  halfExtents.y,  halfExtents.z),
        center + rotation * glm::vec3(-halfExtents.x,  halfExtents.y,  halfExtents.z),
    };
    
    // 12 edges
    int edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7}
    };
    
    for (auto& e : edges) {
        drawLine(corners[e[0]], corners[e[1]], color);
    }
}

void DebugDraw::drawSphere(const glm::vec3& center, float radius,
                            const glm::vec4& color, bool filled) {
    const int segments = 16;
    for (int i = 0; i < segments; ++i) {
        float a1 = (float)i / segments * glm::two_pi<float>();
        float a2 = (float)(i + 1) / segments * glm::two_pi<float>();
        
        // XY circle
        drawLine(center + glm::vec3(cos(a1), sin(a1), 0) * radius,
                 center + glm::vec3(cos(a2), sin(a2), 0) * radius, color);
        // XZ circle
        drawLine(center + glm::vec3(cos(a1), 0, sin(a1)) * radius,
                 center + glm::vec3(cos(a2), 0, sin(a2)) * radius, color);
        // YZ circle
        drawLine(center + glm::vec3(0, cos(a1), sin(a1)) * radius,
                 center + glm::vec3(0, cos(a2), sin(a2)) * radius, color);
    }
}

void DebugDraw::drawArrow(const glm::vec3& start, const glm::vec3& end,
                           const glm::vec4& color, float headSize) {
    drawLine(start, end, color);
    
    glm::vec3 dir = glm::normalize(end - start);
    glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
    glm::vec3 up = glm::cross(right, dir);
    
    glm::vec3 headBase = end - dir * headSize;
    drawLine(end, headBase + right * headSize * 0.5f, color);
    drawLine(end, headBase - right * headSize * 0.5f, color);
    drawLine(end, headBase + up * headSize * 0.5f, color);
    drawLine(end, headBase - up * headSize * 0.5f, color);
}

void DebugDraw::drawPath(const std::vector<glm::vec3>& points, const glm::vec4& color) {
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        drawLine(points[i], points[i + 1], color);
    }
}

void DebugDraw::flush() {
    // Would submit to renderer
    lines_.clear();
}

} // namespace Editor
} // namespace Sanic
