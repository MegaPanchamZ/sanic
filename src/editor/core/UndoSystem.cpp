/**
 * UndoSystem.cpp
 * 
 * Implementation of undo/redo system.
 */

#include "UndoSystem.h"
#include <algorithm>
#include <sstream>

namespace Sanic::Editor {

// ============================================================================
// TransformAction
// ============================================================================

TransformAction::TransformAction(Sanic::World* world, Entity entity,
                                  const Transform& oldTransform,
                                  const Transform& newTransform)
    : world_(world), entity_(entity)
    , oldTransform_(oldTransform), newTransform_(newTransform) {
}

void TransformAction::execute() {
    if (world_ && world_->hasComponent<Transform>(entity_)) {
        world_->getComponent<Transform>(entity_) = newTransform_;
    }
}

void TransformAction::undo() {
    if (world_ && world_->hasComponent<Transform>(entity_)) {
        world_->getComponent<Transform>(entity_) = oldTransform_;
    }
}

std::string TransformAction::getDescription() const {
    return "Transform";
}

bool TransformAction::canMerge(const UndoableAction* other) const {
    auto* otherTransform = dynamic_cast<const TransformAction*>(other);
    if (!otherTransform) return false;
    
    // Same entity and within time window
    if (otherTransform->entity_ != entity_) return false;
    
    float timeDiff = otherTransform->getTimestamp() - getTimestamp();
    return timeDiff >= 0.0f && timeDiff < 0.3f;  // 300ms window
}

void TransformAction::merge(const UndoableAction* other) {
    auto* otherTransform = dynamic_cast<const TransformAction*>(other);
    if (otherTransform) {
        newTransform_ = otherTransform->newTransform_;
        // Keep our timestamp as the batch start
    }
}

// ============================================================================
// CreateEntityAction
// ============================================================================

CreateEntityAction::CreateEntityAction(Sanic::World* world, const std::string& name)
    : world_(world), name_(name) {
}

void CreateEntityAction::execute() {
    if (!world_) return;
    
    if (firstExecute_) {
        entity_ = world_->createEntity(name_);
        world_->addComponent<Transform>(entity_);
        firstExecute_ = false;
    } else {
        // Re-create entity with same ID if possible (simplified)
        entity_ = world_->createEntity(name_);
        world_->addComponent<Transform>(entity_);
    }
}

void CreateEntityAction::undo() {
    if (world_ && entity_ != INVALID_ENTITY) {
        world_->destroyEntity(entity_);
    }
}

std::string CreateEntityAction::getDescription() const {
    return "Create Entity '" + name_ + "'";
}

// ============================================================================
// DeleteEntityAction
// ============================================================================

DeleteEntityAction::DeleteEntityAction(Sanic::World* world, Entity entity)
    : world_(world), entity_(entity) {
    if (world_ && world_->hasComponent<Name>(entity_)) {
        entityName_ = world_->getComponent<Name>(entity_).name;
    }
    serializeEntity();
}

void DeleteEntityAction::execute() {
    if (world_ && entity_ != INVALID_ENTITY) {
        serializeEntity();  // Save state before delete
        world_->destroyEntity(entity_);
    }
}

void DeleteEntityAction::undo() {
    deserializeEntity();
}

std::string DeleteEntityAction::getDescription() const {
    if (!entityName_.empty()) {
        return "Delete '" + entityName_ + "'";
    }
    return "Delete Entity";
}

void DeleteEntityAction::serializeEntity() {
    // Simplified serialization - in a full implementation you'd serialize all components
    // For now, we just store basic info
    serializedData_.clear();
    
    if (!world_ || entity_ == INVALID_ENTITY) return;
    
    // Store transform if present
    if (world_->hasComponent<Transform>(entity_)) {
        const auto& t = world_->getComponent<Transform>(entity_);
        // Would serialize transform data here
    }
}

void DeleteEntityAction::deserializeEntity() {
    // Simplified deserialization
    if (!world_) return;
    
    entity_ = world_->createEntity(entityName_);
    world_->addComponent<Transform>(entity_);
}

// ============================================================================
// PropertyAction
// ============================================================================

PropertyAction::PropertyAction(const std::string& description, ApplyFunc apply, UndoFunc undo)
    : description_(description), applyFunc_(std::move(apply)), undoFunc_(std::move(undo)) {
}

void PropertyAction::execute() {
    if (applyFunc_) applyFunc_();
}

void PropertyAction::undo() {
    if (undoFunc_) undoFunc_();
}

std::string PropertyAction::getDescription() const {
    return description_;
}

// ============================================================================
// CompoundAction
// ============================================================================

CompoundAction::CompoundAction(const std::string& description)
    : description_(description) {
}

void CompoundAction::addAction(std::unique_ptr<UndoableAction> action) {
    actions_.push_back(std::move(action));
}

void CompoundAction::execute() {
    for (auto& action : actions_) {
        action->execute();
    }
}

void CompoundAction::undo() {
    // Undo in reverse order
    for (auto it = actions_.rbegin(); it != actions_.rend(); ++it) {
        (*it)->undo();
    }
}

std::string CompoundAction::getDescription() const {
    return description_;
}

// ============================================================================
// UndoSystem
// ============================================================================

UndoSystem::UndoSystem(size_t maxHistorySize)
    : maxHistorySize_(maxHistorySize) {
}

void UndoSystem::execute(std::unique_ptr<UndoableAction> action) {
    if (!action) return;
    
    action->setTimestamp(currentTime_);
    action->execute();
    
    record(std::move(action));
}

void UndoSystem::record(std::unique_ptr<UndoableAction> action) {
    if (!action) return;
    
    action->setTimestamp(currentTime_);
    
    if (currentBatch_) {
        currentBatch_->addAction(std::move(action));
        return;
    }
    
    // Try to merge with previous action
    if (!undoStack_.empty() && tryMerge(action.get())) {
        return;
    }
    
    // Clear redo stack when new action is added
    redoStack_.clear();
    
    undoStack_.push_back(std::move(action));
    trimHistory();
}

bool UndoSystem::tryMerge(UndoableAction* action) {
    if (undoStack_.empty()) return false;
    
    auto& lastAction = undoStack_.back();
    if (lastAction->canMerge(action)) {
        lastAction->merge(action);
        return true;
    }
    
    return false;
}

void UndoSystem::undo() {
    if (undoStack_.empty()) return;
    
    auto action = std::move(undoStack_.back());
    undoStack_.pop_back();
    
    action->undo();
    
    redoStack_.push_back(std::move(action));
}

void UndoSystem::redo() {
    if (redoStack_.empty()) return;
    
    auto action = std::move(redoStack_.back());
    redoStack_.pop_back();
    
    action->execute();
    
    undoStack_.push_back(std::move(action));
}

std::string UndoSystem::getUndoDescription() const {
    if (undoStack_.empty()) return "";
    return undoStack_.back()->getDescription();
}

std::string UndoSystem::getRedoDescription() const {
    if (redoStack_.empty()) return "";
    return redoStack_.back()->getDescription();
}

std::vector<std::string> UndoSystem::getUndoHistory(size_t maxItems) const {
    std::vector<std::string> history;
    size_t count = std::min(maxItems, undoStack_.size());
    
    for (size_t i = 0; i < count; ++i) {
        size_t idx = undoStack_.size() - 1 - i;
        history.push_back(undoStack_[idx]->getDescription());
    }
    
    return history;
}

std::vector<std::string> UndoSystem::getRedoHistory(size_t maxItems) const {
    std::vector<std::string> history;
    size_t count = std::min(maxItems, redoStack_.size());
    
    for (size_t i = 0; i < count; ++i) {
        size_t idx = redoStack_.size() - 1 - i;
        history.push_back(redoStack_[idx]->getDescription());
    }
    
    return history;
}

void UndoSystem::clear() {
    undoStack_.clear();
    redoStack_.clear();
    cleanIndex_ = 0;
}

void UndoSystem::markClean() {
    cleanIndex_ = undoStack_.size();
}

bool UndoSystem::isDirty() const {
    return undoStack_.size() != cleanIndex_;
}

void UndoSystem::beginBatch(const std::string& description) {
    if (currentBatch_) {
        // Nested batches not supported, end current one
        endBatch();
    }
    currentBatch_ = std::make_unique<CompoundAction>(description);
}

void UndoSystem::endBatch() {
    if (!currentBatch_) return;
    
    if (!currentBatch_->isEmpty()) {
        redoStack_.clear();
        undoStack_.push_back(std::move(currentBatch_));
        trimHistory();
    }
    
    currentBatch_.reset();
}

void UndoSystem::trimHistory() {
    while (undoStack_.size() > maxHistorySize_) {
        undoStack_.erase(undoStack_.begin());
        if (cleanIndex_ > 0) cleanIndex_--;
    }
}

} // namespace Sanic::Editor
