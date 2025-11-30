/**
 * Selection.cpp
 * 
 * Implementation of entity selection manager.
 */

#include "Selection.h"
#include <algorithm>
#include <limits>

namespace Sanic::Editor {

void Selection::select(Entity entity) {
    if (entity == INVALID_ENTITY) {
        clearSelection();
        return;
    }
    
    bool changed = !selected_.empty() || selected_.find(entity) == selected_.end();
    
    selected_.clear();
    selectionOrder_.clear();
    
    selected_.insert(entity);
    selectionOrder_.push_back(entity);
    focused_ = entity;
    
    if (changed) {
        notifyChanged();
    }
}

void Selection::addToSelection(Entity entity) {
    if (entity == INVALID_ENTITY) return;
    
    if (selected_.insert(entity).second) {
        selectionOrder_.push_back(entity);
        focused_ = entity;
        notifyChanged();
    }
}

void Selection::removeFromSelection(Entity entity) {
    if (selected_.erase(entity) > 0) {
        selectionOrder_.erase(
            std::remove(selectionOrder_.begin(), selectionOrder_.end(), entity),
            selectionOrder_.end()
        );
        
        if (focused_ == entity) {
            focused_ = selectionOrder_.empty() ? INVALID_ENTITY : selectionOrder_.back();
        }
        
        notifyChanged();
    }
}

void Selection::toggleSelection(Entity entity) {
    if (isSelected(entity)) {
        removeFromSelection(entity);
    } else {
        addToSelection(entity);
    }
}

void Selection::selectAll(World& world) {
    selected_.clear();
    selectionOrder_.clear();
    
    // Get all entities with transforms (visible entities)
    auto entities = world.getEntitiesWithSignature(ComponentSignature{});
    
    for (Entity e : entities) {
        if (world.hasComponent<Transform>(e)) {
            selected_.insert(e);
            selectionOrder_.push_back(e);
        }
    }
    
    focused_ = selectionOrder_.empty() ? INVALID_ENTITY : selectionOrder_.back();
    notifyChanged();
}

void Selection::clearSelection() {
    if (!selected_.empty()) {
        selected_.clear();
        selectionOrder_.clear();
        focused_ = INVALID_ENTITY;
        notifyChanged();
    }
}

void Selection::selectInRect(World& world, const glm::vec2& min, const glm::vec2& max,
                              const glm::mat4& viewProj, const glm::vec2& viewportSize, bool additive) {
    if (!additive) {
        selected_.clear();
        selectionOrder_.clear();
    }
    
    // Project all entity positions to screen space and check if inside rect
    auto query = world.query<Transform>();
    
    for (auto [entity, transform] : query) {
        // Project position to screen space
        glm::vec4 clipPos = viewProj * glm::vec4(transform.position, 1.0f);
        
        if (clipPos.w <= 0.0f) continue;  // Behind camera
        
        glm::vec3 ndcPos = glm::vec3(clipPos) / clipPos.w;
        
        // Convert to screen space
        glm::vec2 screenPos;
        screenPos.x = (ndcPos.x * 0.5f + 0.5f) * viewportSize.x;
        screenPos.y = (1.0f - (ndcPos.y * 0.5f + 0.5f)) * viewportSize.y;  // Flip Y
        
        // Check if inside selection rectangle
        if (screenPos.x >= min.x && screenPos.x <= max.x &&
            screenPos.y >= min.y && screenPos.y <= max.y) {
            if (selected_.insert(entity).second) {
                selectionOrder_.push_back(entity);
            }
        }
    }
    
    focused_ = selectionOrder_.empty() ? INVALID_ENTITY : selectionOrder_.back();
    notifyChanged();
}

bool Selection::isSelected(Entity entity) const {
    return selected_.find(entity) != selected_.end();
}

Entity Selection::getFirstSelected() const {
    return selectionOrder_.empty() ? INVALID_ENTITY : selectionOrder_.front();
}

Entity Selection::getLastSelected() const {
    return selectionOrder_.empty() ? INVALID_ENTITY : selectionOrder_.back();
}

void Selection::setFocused(Entity entity) {
    if (entity == INVALID_ENTITY || isSelected(entity)) {
        focused_ = entity;
    }
}

void Selection::onSelectionChanged(SelectionChangedCallback callback) {
    callbacks_.push_back(std::move(callback));
}

glm::vec3 Selection::getSelectionCenter(World& world) const {
    if (selected_.empty()) return glm::vec3(0);
    
    glm::vec3 center(0);
    int count = 0;
    
    for (Entity e : selected_) {
        if (world.hasComponent<Transform>(e)) {
            center += world.getComponent<Transform>(e).position;
            ++count;
        }
    }
    
    return count > 0 ? center / static_cast<float>(count) : glm::vec3(0);
}

glm::vec3 Selection::getSelectionBoundsMin(World& world) const {
    glm::vec3 minBounds(std::numeric_limits<float>::max());
    
    for (Entity e : selected_) {
        if (world.hasComponent<Transform>(e)) {
            const auto& pos = world.getComponent<Transform>(e).position;
            minBounds = glm::min(minBounds, pos);
        }
    }
    
    return minBounds;
}

glm::vec3 Selection::getSelectionBoundsMax(World& world) const {
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
    
    for (Entity e : selected_) {
        if (world.hasComponent<Transform>(e)) {
            const auto& pos = world.getComponent<Transform>(e).position;
            maxBounds = glm::max(maxBounds, pos);
        }
    }
    
    return maxBounds;
}

std::vector<Entity> Selection::getSelectionVector() const {
    return selectionOrder_;
}

void Selection::notifyChanged() {
    for (auto& callback : callbacks_) {
        callback();
    }
}

} // namespace Sanic::Editor
