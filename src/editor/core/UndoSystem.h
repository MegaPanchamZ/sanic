/**
 * UndoSystem.h
 * 
 * Undo/redo system for editor operations.
 * 
 * Features:
 * - Undoable action base class
 * - Undo/redo stacks
 * - Action merging for continuous operations
 * - Batch operations (compound actions)
 */

#pragma once

#include "../../engine/ECS.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace Sanic::Editor {

// Forward declaration
class UndoSystem;

// Base class for undoable actions
class UndoableAction {
public:
    virtual ~UndoableAction() = default;
    
    virtual void execute() = 0;     // Do/Redo
    virtual void undo() = 0;        // Undo
    
    virtual std::string getDescription() const = 0;
    
    // Can this action be merged with another of same type?
    virtual bool canMerge(const UndoableAction* other) const { return false; }
    virtual void merge(const UndoableAction* other) {}
    
    // Time of action (for merge window)
    float getTimestamp() const { return timestamp_; }
    void setTimestamp(float time) { timestamp_ = time; }
    
protected:
    float timestamp_ = 0.0f;
};

// Transform change action
class TransformAction : public UndoableAction {
public:
    TransformAction(Sanic::World* world, Entity entity, 
                   const Transform& oldTransform,
                   const Transform& newTransform);
    
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    
    bool canMerge(const UndoableAction* other) const override;
    void merge(const UndoableAction* other) override;
    
private:
    Sanic::World* world_;
    Entity entity_;
    Transform oldTransform_;
    Transform newTransform_;
};

// Entity creation action
class CreateEntityAction : public UndoableAction {
public:
    CreateEntityAction(Sanic::World* world, const std::string& name = "New Entity");
    
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    
    Entity getCreatedEntity() const { return entity_; }
    
private:
    Sanic::World* world_;
    std::string name_;
    Entity entity_ = INVALID_ENTITY;
    std::vector<uint8_t> serializedData_;  // For redo after undo
    bool firstExecute_ = true;
};

// Entity deletion action
class DeleteEntityAction : public UndoableAction {
public:
    DeleteEntityAction(Sanic::World* world, Entity entity);
    
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    
private:
    void serializeEntity();
    void deserializeEntity();
    
    Sanic::World* world_;
    Entity entity_;
    std::string entityName_;
    std::vector<uint8_t> serializedData_;
};

// Property modification action (generic)
class PropertyAction : public UndoableAction {
public:
    using ApplyFunc = std::function<void()>;
    using UndoFunc = std::function<void()>;
    
    PropertyAction(const std::string& description, ApplyFunc apply, UndoFunc undo);
    
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    
private:
    std::string description_;
    ApplyFunc applyFunc_;
    UndoFunc undoFunc_;
};

// Compound action (multiple actions as one)
class CompoundAction : public UndoableAction {
public:
    CompoundAction(const std::string& description);
    
    void addAction(std::unique_ptr<UndoableAction> action);
    bool isEmpty() const { return actions_.empty(); }
    
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    
private:
    std::string description_;
    std::vector<std::unique_ptr<UndoableAction>> actions_;
};

// Undo/Redo stack manager
class UndoSystem {
public:
    UndoSystem(size_t maxHistorySize = 100);
    
    // Execute an action and add to history
    void execute(std::unique_ptr<UndoableAction> action);
    
    // Record an action without executing (already done)
    void record(std::unique_ptr<UndoableAction> action);
    
    // Undo/Redo
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    void undo();
    void redo();
    
    // Get descriptions for UI
    std::string getUndoDescription() const;
    std::string getRedoDescription() const;
    std::vector<std::string> getUndoHistory(size_t maxItems = 10) const;
    std::vector<std::string> getRedoHistory(size_t maxItems = 10) const;
    
    // Clear all history
    void clear();
    
    // Mark clean (e.g., after save)
    void markClean();
    bool isDirty() const;
    
    // Begin/end batch (merges actions)
    void beginBatch(const std::string& description);
    void endBatch();
    bool isBatching() const { return currentBatch_ != nullptr; }
    
    // Set merge window (time in seconds for action merging)
    void setMergeWindow(float seconds) { mergeWindow_ = seconds; }
    
private:
    void trimHistory();
    bool tryMerge(UndoableAction* action);
    
    std::vector<std::unique_ptr<UndoableAction>> undoStack_;
    std::vector<std::unique_ptr<UndoableAction>> redoStack_;
    
    size_t maxHistorySize_;
    size_t cleanIndex_ = 0;  // Index when last saved
    
    std::unique_ptr<CompoundAction> currentBatch_;
    float mergeWindow_ = 0.3f;  // 300ms window for merging
    float currentTime_ = 0.0f;
};

} // namespace Sanic::Editor
