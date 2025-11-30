/**
 * HierarchyPanel.h
 * 
 * Scene hierarchy panel showing entity tree.
 * 
 * Features:
 * - Tree view of entities with parent/child relationships
 * - Drag-drop for reparenting
 * - Context menu for entity operations
 * - Search/filter
 * - Rename inline
 */

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
    void drawEntityNode(Entity entity, Sanic::World& world);
    void handleDragDrop(Entity entity, Sanic::World& world);
    void handleContextMenu(Entity entity, Sanic::World& world);
    
    void createEntity(const char* name = "New Entity");
    void createPrimitive(const char* type);
    void duplicateEntity(Entity entity);
    void deleteEntity(Entity entity);
    
    // Get all root entities (entities without parents)
    std::vector<Entity> getRootEntities(Sanic::World& world);
    
    // Get entity display name
    std::string getEntityDisplayName(Entity entity, Sanic::World& world);
    
    // Check if entity matches search filter
    bool matchesFilter(Entity entity, Sanic::World& world);
    
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
    bool renameFocused_ = false;
    
    // Context menu
    Entity contextMenuEntity_ = INVALID_ENTITY;
};

} // namespace Sanic::Editor
