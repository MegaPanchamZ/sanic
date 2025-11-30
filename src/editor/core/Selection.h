/**
 * Selection.h
 * 
 * Entity selection manager for the editor.
 * 
 * Features:
 * - Single and multi-select
 * - Box selection
 * - Selection callbacks
 * - Selection center/bounds calculation
 */

#pragma once

#include "../../engine/ECS.h"
#include <vector>
#include <functional>
#include <unordered_set>
#include <glm/glm.hpp>

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
                     const glm::mat4& viewProj, const glm::vec2& viewportSize, bool additive = false);
    
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
    
    // Get selection as vector (for ordered iteration)
    std::vector<Entity> getSelectionVector() const;
    
private:
    void notifyChanged();
    
    std::unordered_set<Entity> selected_;
    std::vector<Entity> selectionOrder_;  // Maintains selection order
    Entity focused_ = INVALID_ENTITY;
    std::vector<SelectionChangedCallback> callbacks_;
};

} // namespace Sanic::Editor
