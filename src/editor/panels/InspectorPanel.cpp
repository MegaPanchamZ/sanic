/**
 * InspectorPanel.cpp
 * 
 * Implementation of property inspector panel.
 */

#include "InspectorPanel.h"
#include "../Editor.h"
#include "../core/Selection.h"
#include "../core/UndoSystem.h"
#include <glm/gtc/type_ptr.hpp>
#include <imgui_internal.h>

namespace Sanic::Editor {

void InspectorPanel::initialize(Editor* editor) {
    EditorWindow::initialize(editor);
    editor_ = editor;
}

void InspectorPanel::update(float deltaTime) {
}

void InspectorPanel::draw() {
    if (!beginPanel()) {
        endPanel();
        return;
    }
    
    if (!editor_) {
        ImGui::TextDisabled("No editor");
        endPanel();
        return;
    }
    
    auto& selection = editor_->getSelection();
    
    if (!selection.hasSelection()) {
        ImGui::TextDisabled("No entity selected");
        endPanel();
        return;
    }
    
    if (selection.getSelectionCount() > 1) {
        drawMultiEntityInspector();
    } else {
        Entity entity = selection.getFirstSelected();
        drawEntityInspector(entity);
    }
    
    endPanel();
}

void InspectorPanel::drawEntityInspector(Entity entity) {
    Sanic::World* world = editor_->getWorld();
    if (!world || !world->isValid(entity)) {
        ImGui::TextDisabled("Invalid entity");
        return;
    }
    
    // Entity header
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
    
    // Active toggle and name
    bool active = true;
    if (world->hasComponent<Active>(entity)) {
        active = world->getComponent<Active>(entity).active;
    }
    
    if (ImGui::Checkbox("##Active", &active)) {
        if (world->hasComponent<Active>(entity)) {
            world->getComponent<Active>(entity).active = active;
        } else {
            world->addComponent<Active>(entity, {active});
        }
    }
    
    ImGui::SameLine();
    
    // Name
    if (world->hasComponent<Name>(entity)) {
        Name& name = world->getComponent<Name>(entity);
        drawNameComponent(entity, name);
    } else {
        char nameBuffer[256];
        snprintf(nameBuffer, sizeof(nameBuffer), "Entity %u", entity);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##EntityName", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_ReadOnly);
    }
    
    ImGui::PopStyleVar();
    
    ImGui::Separator();
    
    // Transform (always present typically)
    if (world->hasComponent<Transform>(entity)) {
        Transform& transform = world->getComponent<Transform>(entity);
        drawTransformComponent(entity, transform);
    }
    
    // Mesh Renderer
    if (world->hasComponent<MeshRenderer>(entity)) {
        MeshRenderer& renderer = world->getComponent<MeshRenderer>(entity);
        drawMeshRendererComponent(entity, renderer);
    }
    
    // Light
    if (world->hasComponent<Light>(entity)) {
        Light& light = world->getComponent<Light>(entity);
        drawLightComponent(entity, light);
    }
    
    // Camera
    if (world->hasComponent<Camera>(entity)) {
        Camera& camera = world->getComponent<Camera>(entity);
        drawCameraComponent(entity, camera);
    }
    
    // Custom editors
    for (auto& [typeId, editor] : editors_) {
        // Would check if entity has component of this type
        // editor->draw(entity, *world, editor_->getUndoSystem());
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Add Component button
    drawAddComponentButton(entity);
}

void InspectorPanel::drawMultiEntityInspector() {
    ImGui::TextDisabled("%zu entities selected", editor_->getSelection().getSelectionCount());
    ImGui::Separator();
    
    // TODO: Show common components and allow batch editing
    ImGui::TextDisabled("Multi-entity editing not yet implemented");
}

void InspectorPanel::drawAddComponentButton(Entity entity) {
    float buttonWidth = ImGui::GetContentRegionAvail().x;
    
    if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0))) {
        ImGui::OpenPopup("AddComponentPopup");
        addComponentFilter_[0] = '\0';
    }
    
    if (ImGui::BeginPopup("AddComponentPopup")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Filter", "Search...", addComponentFilter_, sizeof(addComponentFilter_));
        
        ImGui::Separator();
        
        Sanic::World* world = editor_->getWorld();
        
        // List available components
        auto addComponentItem = [&](const char* name, auto addFunc) {
            if (addComponentFilter_[0] != '\0') {
                std::string nameLower = name;
                std::string filterLower = addComponentFilter_;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
                if (nameLower.find(filterLower) == std::string::npos) return;
            }
            
            if (ImGui::MenuItem(name)) {
                addFunc();
                ImGui::CloseCurrentPopup();
            }
        };
        
        addComponentItem("Mesh Renderer", [&]() {
            if (!world->hasComponent<MeshRenderer>(entity)) {
                world->addComponent<MeshRenderer>(entity);
            }
        });
        
        addComponentItem("Light", [&]() {
            if (!world->hasComponent<Light>(entity)) {
                world->addComponent<Light>(entity);
            }
        });
        
        addComponentItem("Camera", [&]() {
            if (!world->hasComponent<Camera>(entity)) {
                world->addComponent<Camera>(entity);
            }
        });
        
        addComponentItem("Rigid Body", [&]() {
            if (!world->hasComponent<RigidBody>(entity)) {
                world->addComponent<RigidBody>(entity);
            }
        });
        
        addComponentItem("Collider", [&]() {
            if (!world->hasComponent<Collider>(entity)) {
                world->addComponent<Collider>(entity);
            }
        });
        
        addComponentItem("Audio Source", [&]() {
            if (!world->hasComponent<AudioSource>(entity)) {
                world->addComponent<AudioSource>(entity);
            }
        });
        
        ImGui::EndPopup();
    }
}

void InspectorPanel::drawTransformComponent(Entity entity, Transform& transform) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_AllowOverlap;
    
    bool open = ImGui::TreeNodeEx("Transform", flags);
    
    // No remove button for transform
    
    if (open) {
        ImGui::PushID("Transform");
        
        // Cache transform when editing starts
        if (ImGui::IsWindowFocused() && !transformEditing_) {
            cachedTransform_ = transform;
            transformEditing_ = true;
        }
        
        bool changed = false;
        
        // Position
        changed |= drawVector3("Position", transform.position);
        
        // Rotation (as euler angles)
        glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(transform.rotation));
        if (drawVector3("Rotation", eulerAngles)) {
            transform.rotation = glm::quat(glm::radians(eulerAngles));
            changed = true;
        }
        
        // Scale
        changed |= drawVector3("Scale", transform.scale, 1.0f);
        
        // Record undo when editing ends
        if (changed && !ImGui::IsAnyItemActive() && transformEditing_) {
            if (cachedTransform_.position != transform.position ||
                cachedTransform_.rotation != transform.rotation ||
                cachedTransform_.scale != transform.scale) {
                editor_->getUndoSystem().record(
                    std::make_unique<TransformAction>(editor_->getWorld(), entity, cachedTransform_, transform)
                );
            }
            transformEditing_ = false;
        }
        
        ImGui::PopID();
        ImGui::TreePop();
    }
}

void InspectorPanel::drawNameComponent(Entity entity, Name& name) {
    static char nameBuffer[256];
    strncpy(nameBuffer, name.name.c_str(), sizeof(nameBuffer) - 1);
    
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer))) {
        name.name = nameBuffer;
    }
}

void InspectorPanel::drawMeshRendererComponent(Entity entity, MeshRenderer& renderer) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_AllowOverlap;
    
    bool open = ImGui::TreeNodeEx("Mesh Renderer", flags);
    
    // Remove button
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 20);
    if (ImGui::SmallButton("X##RemoveMeshRenderer")) {
        editor_->getWorld()->removeComponent<MeshRenderer>(entity);
        if (open) ImGui::TreePop();
        return;
    }
    
    if (open) {
        ImGui::PushID("MeshRenderer");
        
        ImGui::Text("Mesh ID: %u", renderer.meshId);
        ImGui::Text("Material ID: %u", renderer.materialId);
        
        ImGui::Checkbox("Cast Shadows", &renderer.castShadows);
        ImGui::Checkbox("Receive Shadows", &renderer.receiveShadows);
        
        ImGui::PopID();
        ImGui::TreePop();
    }
}

void InspectorPanel::drawLightComponent(Entity entity, Light& light) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_AllowOverlap;
    
    bool open = ImGui::TreeNodeEx("Light", flags);
    
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 20);
    if (ImGui::SmallButton("X##RemoveLight")) {
        editor_->getWorld()->removeComponent<Light>(entity);
        if (open) ImGui::TreePop();
        return;
    }
    
    if (open) {
        ImGui::PushID("Light");
        
        // Type
        const char* lightTypes[] = { "Directional", "Point", "Spot" };
        int currentType = static_cast<int>(light.type);
        if (ImGui::Combo("Type", &currentType, lightTypes, 3)) {
            light.type = static_cast<Light::Type>(currentType);
        }
        
        drawColor3("Color", light.color);
        drawFloat("Intensity", light.intensity, 0.1f, 0.0f, 100.0f);
        
        if (light.type == Light::Type::Point || light.type == Light::Type::Spot) {
            drawFloat("Range", light.range, 0.5f, 0.0f, 1000.0f);
        }
        
        if (light.type == Light::Type::Spot) {
            drawFloat("Inner Angle", light.innerAngle, 1.0f, 0.0f, light.outerAngle);
            drawFloat("Outer Angle", light.outerAngle, 1.0f, light.innerAngle, 180.0f);
        }
        
        ImGui::Checkbox("Cast Shadows", &light.castShadows);
        
        ImGui::PopID();
        ImGui::TreePop();
    }
}

void InspectorPanel::drawCameraComponent(Entity entity, Camera& camera) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_AllowOverlap;
    
    bool open = ImGui::TreeNodeEx("Camera", flags);
    
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 20);
    if (ImGui::SmallButton("X##RemoveCamera")) {
        editor_->getWorld()->removeComponent<Camera>(entity);
        if (open) ImGui::TreePop();
        return;
    }
    
    if (open) {
        ImGui::PushID("Camera");
        
        ImGui::Checkbox("Orthographic", &camera.isOrthographic);
        
        if (camera.isOrthographic) {
            drawFloat("Size", camera.orthoSize, 0.1f, 0.1f, 100.0f);
        } else {
            drawFloat("FOV", camera.fov, 1.0f, 1.0f, 179.0f);
        }
        
        drawFloat("Near", camera.nearPlane, 0.01f, 0.001f, camera.farPlane);
        drawFloat("Far", camera.farPlane, 1.0f, camera.nearPlane, 100000.0f);
        
        int priority = camera.priority;
        if (ImGui::DragInt("Priority", &priority, 1, -100, 100)) {
            camera.priority = priority;
        }
        
        ImGui::PopID();
        ImGui::TreePop();
    }
}

bool InspectorPanel::drawVector3(const char* label, glm::vec3& value, float resetValue) {
    bool changed = false;
    
    ImGui::PushID(label);
    
    ImGui::Columns(2);
    ImGui::SetColumnWidth(0, 100.0f);
    ImGui::Text("%s", label);
    ImGui::NextColumn();
    
    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    
    float lineHeight = ImGui::GetFontSize() + GImGui->Style.FramePadding.y * 2.0f;
    ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };
    
    // X
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.1f, 0.15f, 1.0f));
    if (ImGui::Button("X", buttonSize)) {
        value.x = resetValue;
        changed = true;
    }
    ImGui::PopStyleColor(3);
    
    ImGui::SameLine();
    if (ImGui::DragFloat("##X", &value.x, 0.1f)) changed = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    
    // Y
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
    if (ImGui::Button("Y", buttonSize)) {
        value.y = resetValue;
        changed = true;
    }
    ImGui::PopStyleColor(3);
    
    ImGui::SameLine();
    if (ImGui::DragFloat("##Y", &value.y, 0.1f)) changed = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    
    // Z
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.25f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.35f, 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.25f, 0.8f, 1.0f));
    if (ImGui::Button("Z", buttonSize)) {
        value.z = resetValue;
        changed = true;
    }
    ImGui::PopStyleColor(3);
    
    ImGui::SameLine();
    if (ImGui::DragFloat("##Z", &value.z, 0.1f)) changed = true;
    ImGui::PopItemWidth();
    
    ImGui::PopStyleVar();
    ImGui::Columns(1);
    
    ImGui::PopID();
    
    return changed;
}

bool InspectorPanel::drawFloat(const char* label, float& value, float speed, float min, float max) {
    ImGui::PushID(label);
    
    ImGui::Columns(2);
    ImGui::SetColumnWidth(0, 100.0f);
    ImGui::Text("%s", label);
    ImGui::NextColumn();
    
    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::DragFloat("##value", &value, speed, min, max);
    
    ImGui::Columns(1);
    ImGui::PopID();
    
    return changed;
}

bool InspectorPanel::drawColor3(const char* label, glm::vec3& color) {
    ImGui::PushID(label);
    
    ImGui::Columns(2);
    ImGui::SetColumnWidth(0, 100.0f);
    ImGui::Text("%s", label);
    ImGui::NextColumn();
    
    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::ColorEdit3("##color", glm::value_ptr(color));
    
    ImGui::Columns(1);
    ImGui::PopID();
    
    return changed;
}

bool InspectorPanel::drawColor4(const char* label, glm::vec4& color) {
    ImGui::PushID(label);
    
    ImGui::Columns(2);
    ImGui::SetColumnWidth(0, 100.0f);
    ImGui::Text("%s", label);
    ImGui::NextColumn();
    
    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::ColorEdit4("##color", glm::value_ptr(color));
    
    ImGui::Columns(1);
    ImGui::PopID();
    
    return changed;
}

} // namespace Sanic::Editor
