/**
 * HierarchyPanel.cpp
 * 
 * Implementation of scene hierarchy panel.
 */

#include "HierarchyPanel.h"
#include "../Editor.h"
#include "../core/Selection.h"
#include "../core/UndoSystem.h"
#include <algorithm>

namespace Sanic::Editor {

void HierarchyPanel::initialize(Editor* editor) {
    EditorWindow::initialize(editor);
    editor_ = editor;
}

void HierarchyPanel::update(float deltaTime) {
}

void HierarchyPanel::draw() {
    if (!beginPanel()) {
        endPanel();
        return;
    }
    
    Sanic::World* world = editor_ ? editor_->getWorld() : nullptr;
    
    // Toolbar
    if (ImGui::Button("+")) {
        ImGui::OpenPopup("CreateEntityPopup");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Create Entity");
    }
    
    // Create entity popup
    if (ImGui::BeginPopup("CreateEntityPopup")) {
        if (ImGui::MenuItem("Empty Entity")) {
            createEntity("Empty");
        }
        if (ImGui::BeginMenu("3D Object")) {
            if (ImGui::MenuItem("Cube")) createPrimitive("Cube");
            if (ImGui::MenuItem("Sphere")) createPrimitive("Sphere");
            if (ImGui::MenuItem("Plane")) createPrimitive("Plane");
            if (ImGui::MenuItem("Cylinder")) createPrimitive("Cylinder");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Light")) {
            if (ImGui::MenuItem("Directional Light")) createEntity("Directional Light");
            if (ImGui::MenuItem("Point Light")) createEntity("Point Light");
            if (ImGui::MenuItem("Spot Light")) createEntity("Spot Light");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Camera")) {
            createEntity("Camera");
        }
        ImGui::EndPopup();
    }
    
    ImGui::SameLine();
    
    // Search box
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Search", "Search...", searchBuffer_, sizeof(searchBuffer_));
    
    ImGui::Separator();
    
    // Entity list
    if (world) {
        auto rootEntities = getRootEntities(*world);
        
        for (Entity entity : rootEntities) {
            if (matchesFilter(entity, *world)) {
                drawEntityNode(entity, *world);
            }
        }
        
        // Right-click on empty space
        if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Create Empty")) {
                createEntity();
            }
            ImGui::EndPopup();
        }
        
        // Drop target for root level
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY")) {
                Entity droppedEntity = *static_cast<Entity*>(payload->Data);
                if (world->hasComponent<Transform>(droppedEntity)) {
                    Transform& transform = world->getComponent<Transform>(droppedEntity);
                    if (transform.parent != INVALID_ENTITY) {
                        // Remove from current parent
                        if (world->hasComponent<Transform>(transform.parent)) {
                            auto& parentTransform = world->getComponent<Transform>(transform.parent);
                            auto& children = parentTransform.children;
                            children.erase(std::remove(children.begin(), children.end(), droppedEntity), children.end());
                        }
                        transform.parent = INVALID_ENTITY;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    } else {
        ImGui::TextDisabled("No world loaded");
    }
    
    endPanel();
}

void HierarchyPanel::drawEntityNode(Entity entity, Sanic::World& world) {
    auto& selection = editor_->getSelection();
    bool isSelected = selection.isSelected(entity);
    
    std::string displayName = getEntityDisplayName(entity, world);
    
    // Check if entity has children
    bool hasChildren = false;
    if (world.hasComponent<Transform>(entity)) {
        hasChildren = !world.getComponent<Transform>(entity).children.empty();
    }
    
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    
    // Check if renaming this entity
    bool isRenaming = (renamingEntity_ == entity);
    
    if (isRenaming) {
        // Draw rename input
        ImGui::SetNextItemWidth(-1);
        
        if (!renameFocused_) {
            ImGui::SetKeyboardFocusHere();
            renameFocused_ = true;
        }
        
        if (ImGui::InputText("##Rename", renameBuffer_, sizeof(renameBuffer_), 
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            // Apply rename
            if (world.hasComponent<Name>(entity)) {
                world.getComponent<Name>(entity).name = renameBuffer_;
            }
            renamingEntity_ = INVALID_ENTITY;
            renameFocused_ = false;
        }
        
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            renamingEntity_ = INVALID_ENTITY;
            renameFocused_ = false;
        }
        
        if (!ImGui::IsItemActive() && renameFocused_) {
            renamingEntity_ = INVALID_ENTITY;
            renameFocused_ = false;
        }
    } else {
        bool nodeOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(entity)), 
                                           flags, "%s", displayName.c_str());
        
        // Selection on click
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            if (ImGui::GetIO().KeyCtrl) {
                selection.toggleSelection(entity);
            } else {
                selection.select(entity);
            }
        }
        
        // Double-click to rename
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            renamingEntity_ = entity;
            strncpy(renameBuffer_, displayName.c_str(), sizeof(renameBuffer_) - 1);
            renameFocused_ = false;
        }
        
        // Context menu
        if (ImGui::BeginPopupContextItem()) {
            handleContextMenu(entity, world);
            ImGui::EndPopup();
        }
        
        // Drag source
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("ENTITY", &entity, sizeof(Entity));
            ImGui::Text("%s", displayName.c_str());
            ImGui::EndDragDropSource();
        }
        
        // Drop target
        handleDragDrop(entity, world);
        
        // Draw children
        if (nodeOpen) {
            if (world.hasComponent<Transform>(entity)) {
                const auto& children = world.getComponent<Transform>(entity).children;
                for (Entity child : children) {
                    if (matchesFilter(child, world)) {
                        drawEntityNode(child, world);
                    }
                }
            }
            ImGui::TreePop();
        }
    }
}

void HierarchyPanel::handleDragDrop(Entity entity, Sanic::World& world) {
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY")) {
            Entity droppedEntity = *static_cast<Entity*>(payload->Data);
            
            if (droppedEntity != entity) {
                // Reparent
                if (world.hasComponent<Transform>(droppedEntity)) {
                    Transform& droppedTransform = world.getComponent<Transform>(droppedEntity);
                    
                    // Remove from old parent
                    if (droppedTransform.parent != INVALID_ENTITY && world.hasComponent<Transform>(droppedTransform.parent)) {
                        auto& oldParentChildren = world.getComponent<Transform>(droppedTransform.parent).children;
                        oldParentChildren.erase(std::remove(oldParentChildren.begin(), oldParentChildren.end(), droppedEntity), oldParentChildren.end());
                    }
                    
                    // Add to new parent
                    droppedTransform.parent = entity;
                    if (world.hasComponent<Transform>(entity)) {
                        world.getComponent<Transform>(entity).children.push_back(droppedEntity);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void HierarchyPanel::handleContextMenu(Entity entity, Sanic::World& world) {
    if (ImGui::MenuItem("Rename", "F2")) {
        renamingEntity_ = entity;
        strncpy(renameBuffer_, getEntityDisplayName(entity, world).c_str(), sizeof(renameBuffer_) - 1);
        renameFocused_ = false;
    }
    
    ImGui::Separator();
    
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
        duplicateEntity(entity);
    }
    
    if (ImGui::MenuItem("Delete", "Delete")) {
        deleteEntity(entity);
    }
    
    ImGui::Separator();
    
    if (ImGui::MenuItem("Create Empty Child")) {
        Entity child = world.createEntity("Empty");
        world.addComponent<Transform>(child);
        
        if (world.hasComponent<Transform>(entity)) {
            world.getComponent<Transform>(child).parent = entity;
            world.getComponent<Transform>(entity).children.push_back(child);
        }
        
        editor_->getSelection().select(child);
    }
    
    ImGui::Separator();
    
    if (ImGui::MenuItem("Focus", "F")) {
        // Would trigger viewport focus
    }
}

void HierarchyPanel::createEntity(const char* name) {
    Sanic::World* world = editor_->getWorld();
    if (!world) return;
    
    Entity entity = world->createEntity(name);
    world->addComponent<Transform>(entity);
    
    editor_->getSelection().select(entity);
}

void HierarchyPanel::createPrimitive(const char* type) {
    Sanic::World* world = editor_->getWorld();
    if (!world) return;
    
    Entity entity = world->createEntity(type);
    world->addComponent<Transform>(entity);
    
    // Add mesh renderer with appropriate mesh
    MeshRenderer renderer;
    // renderer.meshId would be set based on type
    world->addComponent<MeshRenderer>(entity, renderer);
    
    editor_->getSelection().select(entity);
}

void HierarchyPanel::duplicateEntity(Entity entity) {
    Sanic::World* world = editor_->getWorld();
    if (!world) return;
    
    Entity duplicate = world->instantiate(entity);
    editor_->getSelection().select(duplicate);
}

void HierarchyPanel::deleteEntity(Entity entity) {
    Sanic::World* world = editor_->getWorld();
    if (!world) return;
    
    // Record undo
    editor_->getUndoSystem().record(
        std::make_unique<DeleteEntityAction>(world, entity)
    );
    
    editor_->getSelection().removeFromSelection(entity);
    world->destroyEntity(entity);
}

std::vector<Entity> HierarchyPanel::getRootEntities(Sanic::World& world) {
    std::vector<Entity> roots;
    
    auto query = world.query<Transform>();
    for (auto [entity, transform] : query) {
        if (transform.parent == INVALID_ENTITY) {
            roots.push_back(entity);
        }
    }
    
    return roots;
}

std::string HierarchyPanel::getEntityDisplayName(Entity entity, Sanic::World& world) {
    if (world.hasComponent<Name>(entity)) {
        const std::string& name = world.getComponent<Name>(entity).name;
        if (!name.empty()) return name;
    }
    return "Entity " + std::to_string(entity);
}

bool HierarchyPanel::matchesFilter(Entity entity, Sanic::World& world) {
    if (searchBuffer_[0] == '\0') return true;
    
    std::string displayName = getEntityDisplayName(entity, world);
    std::string filter(searchBuffer_);
    
    // Case-insensitive search
    std::transform(displayName.begin(), displayName.end(), displayName.begin(), ::tolower);
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    
    return displayName.find(filter) != std::string::npos;
}

} // namespace Sanic::Editor
