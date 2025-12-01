/**
 * BehaviorTree.h
 * 
 * AI Behavior Tree System
 * 
 * Features:
 * - Composite nodes (Selector, Sequence, Parallel)
 * - Decorator nodes (Inverter, Repeater, Succeeder)
 * - Leaf nodes (Actions, Conditions)
 * - Blackboard for shared data
 * - Tree serialization/deserialization
 * - Visual debugging support
 * 
 * Reference:
 *   Engine/Source/Runtime/AIModule/
 *   Engine/Plugins/AI/BehaviorTree/
 */

#pragma once

#include "ECS.h"
#include "NavigationSystem.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <variant>
#include <any>

namespace Sanic {

// Forward declarations
class AIController;
class BehaviorTree;
class BTNode;

// ============================================================================
// BEHAVIOR TREE STATUS
// ============================================================================

/**
 * Result of a behavior tree node execution
 */
enum class BTStatus {
    Success,    // Node completed successfully
    Failure,    // Node failed
    Running     // Node is still running
};

// ============================================================================
// BLACKBOARD
// ============================================================================

/**
 * Shared data storage for behavior tree
 */
class Blackboard {
public:
    using Value = std::variant<
        bool,
        int,
        float,
        std::string,
        glm::vec3,
        glm::quat,
        Entity
    >;
    
    /**
     * Set a value
     */
    template<typename T>
    void set(const std::string& key, const T& value) {
        data_[key] = value;
    }
    
    /**
     * Get a value
     */
    template<typename T>
    T get(const std::string& key, const T& defaultValue = T{}) const {
        auto it = data_.find(key);
        if (it == data_.end()) return defaultValue;
        
        if (auto* val = std::get_if<T>(&it->second)) {
            return *val;
        }
        return defaultValue;
    }
    
    /**
     * Check if key exists
     */
    bool has(const std::string& key) const {
        return data_.find(key) != data_.end();
    }
    
    /**
     * Remove a key
     */
    void remove(const std::string& key) {
        data_.erase(key);
    }
    
    /**
     * Clear all data
     */
    void clear() {
        data_.clear();
    }
    
    /**
     * Get raw value (for inspection)
     */
    const Value* getRaw(const std::string& key) const {
        auto it = data_.find(key);
        return it != data_.end() ? &it->second : nullptr;
    }
    
    /**
     * Get all keys
     */
    std::vector<std::string> getKeys() const {
        std::vector<std::string> keys;
        for (const auto& [k, v] : data_) {
            keys.push_back(k);
        }
        return keys;
    }
    
private:
    std::unordered_map<std::string, Value> data_;
};

// ============================================================================
// BEHAVIOR TREE NODE BASE
// ============================================================================

/**
 * Base class for all behavior tree nodes
 */
class BTNode {
public:
    BTNode(const std::string& name = "") : name_(name) {}
    virtual ~BTNode() = default;
    
    /**
     * Execute the node
     */
    virtual BTStatus execute(AIController* ai, Blackboard& bb) = 0;
    
    /**
     * Called when node is first entered
     */
    virtual void onEnter(AIController* ai, Blackboard& bb) {}
    
    /**
     * Called when node is exited (success, failure, or abort)
     */
    virtual void onExit(AIController* ai, Blackboard& bb) {}
    
    /**
     * Abort running node
     */
    virtual void abort(AIController* ai, Blackboard& bb) {}
    
    /**
     * Get node name
     */
    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }
    
    /**
     * Get node type name (for serialization/debugging)
     */
    virtual const char* getTypeName() const = 0;
    
    /**
     * Is this a composite node
     */
    virtual bool isComposite() const { return false; }
    
    /**
     * Get children (for composite nodes)
     */
    virtual std::vector<BTNode*> getChildren() { return {}; }
    
protected:
    std::string name_;
};

// ============================================================================
// COMPOSITE NODES
// ============================================================================

/**
 * Base class for composite nodes (have children)
 */
class BTComposite : public BTNode {
public:
    BTComposite(const std::string& name = "") : BTNode(name) {}
    
    void addChild(std::unique_ptr<BTNode> child) {
        children_.push_back(std::move(child));
    }
    
    bool isComposite() const override { return true; }
    
    std::vector<BTNode*> getChildren() override {
        std::vector<BTNode*> result;
        for (auto& child : children_) {
            result.push_back(child.get());
        }
        return result;
    }
    
protected:
    std::vector<std::unique_ptr<BTNode>> children_;
    int currentChild_ = 0;
};

/**
 * Selector: Tries children until one succeeds
 * Returns Success if any child succeeds
 * Returns Failure if all children fail
 */
class BTSelector : public BTComposite {
public:
    BTSelector(const std::string& name = "Selector") : BTComposite(name) {}
    
    const char* getTypeName() const override { return "Selector"; }
    
    void onEnter(AIController* ai, Blackboard& bb) override {
        currentChild_ = 0;
    }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        while (currentChild_ < static_cast<int>(children_.size())) {
            BTStatus status = children_[currentChild_]->execute(ai, bb);
            
            if (status == BTStatus::Running) {
                return BTStatus::Running;
            }
            
            if (status == BTStatus::Success) {
                return BTStatus::Success;
            }
            
            // Failure - try next child
            currentChild_++;
        }
        
        return BTStatus::Failure;
    }
};

/**
 * Sequence: Runs all children in order
 * Returns Failure if any child fails
 * Returns Success if all children succeed
 */
class BTSequence : public BTComposite {
public:
    BTSequence(const std::string& name = "Sequence") : BTComposite(name) {}
    
    const char* getTypeName() const override { return "Sequence"; }
    
    void onEnter(AIController* ai, Blackboard& bb) override {
        currentChild_ = 0;
    }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        while (currentChild_ < static_cast<int>(children_.size())) {
            BTStatus status = children_[currentChild_]->execute(ai, bb);
            
            if (status == BTStatus::Running) {
                return BTStatus::Running;
            }
            
            if (status == BTStatus::Failure) {
                return BTStatus::Failure;
            }
            
            // Success - continue to next child
            currentChild_++;
        }
        
        return BTStatus::Success;
    }
};

/**
 * Parallel: Runs all children simultaneously
 * Policy determines success/failure conditions
 */
class BTParallel : public BTComposite {
public:
    enum class Policy {
        RequireOne,     // Succeed if one succeeds
        RequireAll      // Succeed only if all succeed
    };
    
    BTParallel(Policy successPolicy = Policy::RequireAll,
               Policy failurePolicy = Policy::RequireOne,
               const std::string& name = "Parallel")
        : BTComposite(name)
        , successPolicy_(successPolicy)
        , failurePolicy_(failurePolicy) {}
    
    const char* getTypeName() const override { return "Parallel"; }
    
    void onEnter(AIController* ai, Blackboard& bb) override {
        childStatus_.resize(children_.size(), BTStatus::Running);
    }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        int successCount = 0;
        int failureCount = 0;
        int runningCount = 0;
        
        for (size_t i = 0; i < children_.size(); ++i) {
            if (childStatus_[i] == BTStatus::Running) {
                childStatus_[i] = children_[i]->execute(ai, bb);
            }
            
            switch (childStatus_[i]) {
                case BTStatus::Success: successCount++; break;
                case BTStatus::Failure: failureCount++; break;
                case BTStatus::Running: runningCount++; break;
            }
        }
        
        // Check failure policy
        if (failurePolicy_ == Policy::RequireOne && failureCount > 0) {
            return BTStatus::Failure;
        }
        if (failurePolicy_ == Policy::RequireAll && 
            failureCount == static_cast<int>(children_.size())) {
            return BTStatus::Failure;
        }
        
        // Check success policy
        if (successPolicy_ == Policy::RequireOne && successCount > 0) {
            return BTStatus::Success;
        }
        if (successPolicy_ == Policy::RequireAll && 
            successCount == static_cast<int>(children_.size())) {
            return BTStatus::Success;
        }
        
        return runningCount > 0 ? BTStatus::Running : BTStatus::Failure;
    }
    
private:
    Policy successPolicy_;
    Policy failurePolicy_;
    std::vector<BTStatus> childStatus_;
};

/**
 * Random Selector: Tries children in random order
 */
class BTRandomSelector : public BTComposite {
public:
    BTRandomSelector(const std::string& name = "RandomSelector") : BTComposite(name) {}
    
    const char* getTypeName() const override { return "RandomSelector"; }
    
    void onEnter(AIController* ai, Blackboard& bb) override {
        // Shuffle order
        order_.resize(children_.size());
        for (size_t i = 0; i < children_.size(); ++i) {
            order_[i] = static_cast<int>(i);
        }
        
        for (size_t i = children_.size() - 1; i > 0; --i) {
            size_t j = rand() % (i + 1);
            std::swap(order_[i], order_[j]);
        }
        
        currentChild_ = 0;
    }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        while (currentChild_ < static_cast<int>(children_.size())) {
            BTStatus status = children_[order_[currentChild_]]->execute(ai, bb);
            
            if (status != BTStatus::Failure) {
                return status;
            }
            
            currentChild_++;
        }
        
        return BTStatus::Failure;
    }
    
private:
    std::vector<int> order_;
};

// ============================================================================
// DECORATOR NODES
// ============================================================================

/**
 * Base class for decorator nodes (single child)
 */
class BTDecorator : public BTNode {
public:
    BTDecorator(std::unique_ptr<BTNode> child = nullptr, const std::string& name = "")
        : BTNode(name), child_(std::move(child)) {}
    
    void setChild(std::unique_ptr<BTNode> child) {
        child_ = std::move(child);
    }
    
    std::vector<BTNode*> getChildren() override {
        return child_ ? std::vector<BTNode*>{ child_.get() } : std::vector<BTNode*>{};
    }
    
protected:
    std::unique_ptr<BTNode> child_;
};

/**
 * Inverter: Inverts child result
 */
class BTInverter : public BTDecorator {
public:
    BTInverter(std::unique_ptr<BTNode> child = nullptr)
        : BTDecorator(std::move(child), "Inverter") {}
    
    const char* getTypeName() const override { return "Inverter"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        if (!child_) return BTStatus::Failure;
        
        BTStatus status = child_->execute(ai, bb);
        
        switch (status) {
            case BTStatus::Success: return BTStatus::Failure;
            case BTStatus::Failure: return BTStatus::Success;
            default: return status;
        }
    }
};

/**
 * Succeeder: Always returns success
 */
class BTSucceeder : public BTDecorator {
public:
    BTSucceeder(std::unique_ptr<BTNode> child = nullptr)
        : BTDecorator(std::move(child), "Succeeder") {}
    
    const char* getTypeName() const override { return "Succeeder"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        if (child_) child_->execute(ai, bb);
        return BTStatus::Success;
    }
};

/**
 * Failer: Always returns failure
 */
class BTFailer : public BTDecorator {
public:
    BTFailer(std::unique_ptr<BTNode> child = nullptr)
        : BTDecorator(std::move(child), "Failer") {}
    
    const char* getTypeName() const override { return "Failer"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        if (child_) child_->execute(ai, bb);
        return BTStatus::Failure;
    }
};

/**
 * Repeater: Repeats child N times
 */
class BTRepeater : public BTDecorator {
public:
    BTRepeater(int repeatCount = -1, std::unique_ptr<BTNode> child = nullptr)
        : BTDecorator(std::move(child), "Repeater")
        , repeatCount_(repeatCount) {}
    
    const char* getTypeName() const override { return "Repeater"; }
    
    void onEnter(AIController* ai, Blackboard& bb) override {
        currentCount_ = 0;
    }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        if (!child_) return BTStatus::Failure;
        
        BTStatus status = child_->execute(ai, bb);
        
        if (status == BTStatus::Running) {
            return BTStatus::Running;
        }
        
        currentCount_++;
        
        if (repeatCount_ < 0 || currentCount_ < repeatCount_) {
            child_->onExit(ai, bb);
            child_->onEnter(ai, bb);
            return BTStatus::Running;
        }
        
        return status;
    }
    
private:
    int repeatCount_;  // -1 = infinite
    int currentCount_ = 0;
};

/**
 * RepeatUntilFail: Repeats until child fails
 */
class BTRepeatUntilFail : public BTDecorator {
public:
    BTRepeatUntilFail(std::unique_ptr<BTNode> child = nullptr)
        : BTDecorator(std::move(child), "RepeatUntilFail") {}
    
    const char* getTypeName() const override { return "RepeatUntilFail"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        if (!child_) return BTStatus::Failure;
        
        BTStatus status = child_->execute(ai, bb);
        
        if (status == BTStatus::Running) {
            return BTStatus::Running;
        }
        
        if (status == BTStatus::Failure) {
            return BTStatus::Success;
        }
        
        // Success - repeat
        child_->onExit(ai, bb);
        child_->onEnter(ai, bb);
        return BTStatus::Running;
    }
};

/**
 * Cooldown: Prevents re-execution for a time
 */
class BTCooldown : public BTDecorator {
public:
    BTCooldown(float cooldownTime, std::unique_ptr<BTNode> child = nullptr)
        : BTDecorator(std::move(child), "Cooldown")
        , cooldownTime_(cooldownTime) {}
    
    const char* getTypeName() const override { return "Cooldown"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        if (!child_) return BTStatus::Failure;
        
        float currentTime = bb.get<float>("_time", 0.0f);
        
        if (currentTime < lastExecutionTime_ + cooldownTime_) {
            return BTStatus::Failure;
        }
        
        BTStatus status = child_->execute(ai, bb);
        
        if (status != BTStatus::Running) {
            lastExecutionTime_ = currentTime;
        }
        
        return status;
    }
    
private:
    float cooldownTime_;
    float lastExecutionTime_ = -1000.0f;
};

/**
 * Condition: Only runs child if condition is true
 */
class BTConditionDecorator : public BTDecorator {
public:
    using ConditionFunc = std::function<bool(AIController*, Blackboard&)>;
    
    BTConditionDecorator(ConditionFunc condition, 
                         std::unique_ptr<BTNode> child = nullptr,
                         const std::string& name = "Condition")
        : BTDecorator(std::move(child), name)
        , condition_(condition) {}
    
    const char* getTypeName() const override { return "Condition"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        if (!condition_ || !condition_(ai, bb)) {
            return BTStatus::Failure;
        }
        
        if (!child_) return BTStatus::Success;
        return child_->execute(ai, bb);
    }
    
private:
    ConditionFunc condition_;
};

// ============================================================================
// LEAF NODES
// ============================================================================

/**
 * Action: Executes a function
 */
class BTAction : public BTNode {
public:
    using ActionFunc = std::function<BTStatus(AIController*, Blackboard&)>;
    
    BTAction(ActionFunc action, const std::string& name = "Action")
        : BTNode(name), action_(action) {}
    
    const char* getTypeName() const override { return "Action"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        return action_ ? action_(ai, bb) : BTStatus::Failure;
    }
    
private:
    ActionFunc action_;
};

/**
 * Condition: Returns success/failure based on condition
 */
class BTCondition : public BTNode {
public:
    using ConditionFunc = std::function<bool(AIController*, Blackboard&)>;
    
    BTCondition(ConditionFunc condition, const std::string& name = "Condition")
        : BTNode(name), condition_(condition) {}
    
    const char* getTypeName() const override { return "Condition"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        return (condition_ && condition_(ai, bb)) ? BTStatus::Success : BTStatus::Failure;
    }
    
private:
    ConditionFunc condition_;
};

/**
 * Wait: Waits for a specified time
 */
class BTWait : public BTNode {
public:
    BTWait(float waitTime, const std::string& name = "Wait")
        : BTNode(name), waitTime_(waitTime) {}
    
    const char* getTypeName() const override { return "Wait"; }
    
    void onEnter(AIController* ai, Blackboard& bb) override {
        startTime_ = bb.get<float>("_time", 0.0f);
    }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        float currentTime = bb.get<float>("_time", 0.0f);
        
        if (currentTime - startTime_ >= waitTime_) {
            return BTStatus::Success;
        }
        
        return BTStatus::Running;
    }
    
private:
    float waitTime_;
    float startTime_ = 0.0f;
};

/**
 * WaitRandom: Waits for a random time in range
 */
class BTWaitRandom : public BTNode {
public:
    BTWaitRandom(float minTime, float maxTime, const std::string& name = "WaitRandom")
        : BTNode(name), minTime_(minTime), maxTime_(maxTime) {}
    
    const char* getTypeName() const override { return "WaitRandom"; }
    
    void onEnter(AIController* ai, Blackboard& bb) override {
        startTime_ = bb.get<float>("_time", 0.0f);
        float t = static_cast<float>(rand()) / RAND_MAX;
        waitTime_ = minTime_ + t * (maxTime_ - minTime_);
    }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override {
        float currentTime = bb.get<float>("_time", 0.0f);
        
        if (currentTime - startTime_ >= waitTime_) {
            return BTStatus::Success;
        }
        
        return BTStatus::Running;
    }
    
private:
    float minTime_;
    float maxTime_;
    float waitTime_ = 0.0f;
    float startTime_ = 0.0f;
};

// ============================================================================
// COMMON AI ACTIONS
// ============================================================================

/**
 * Move to location stored in blackboard
 */
class BTMoveTo : public BTNode {
public:
    BTMoveTo(const std::string& targetKey = "MoveTarget",
             float acceptanceRadius = 0.5f,
             const std::string& name = "MoveTo")
        : BTNode(name)
        , targetKey_(targetKey)
        , acceptanceRadius_(acceptanceRadius) {}
    
    const char* getTypeName() const override { return "MoveTo"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override;
    
private:
    std::string targetKey_;
    float acceptanceRadius_;
};

/**
 * Move to target entity
 */
class BTMoveToEntity : public BTNode {
public:
    BTMoveToEntity(const std::string& entityKey = "TargetEntity",
                   float acceptanceRadius = 2.0f,
                   const std::string& name = "MoveToEntity")
        : BTNode(name)
        , entityKey_(entityKey)
        , acceptanceRadius_(acceptanceRadius) {}
    
    const char* getTypeName() const override { return "MoveToEntity"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override;
    
private:
    std::string entityKey_;
    float acceptanceRadius_;
};

/**
 * Check if target is in range
 */
class BTIsInRange : public BTNode {
public:
    BTIsInRange(const std::string& targetKey = "TargetEntity",
                float range = 5.0f,
                const std::string& name = "IsInRange")
        : BTNode(name)
        , targetKey_(targetKey)
        , range_(range) {}
    
    const char* getTypeName() const override { return "IsInRange"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override;
    
private:
    std::string targetKey_;
    float range_;
};

/**
 * Perform an attack
 */
class BTAttack : public BTNode {
public:
    BTAttack(const std::string& name = "Attack") : BTNode(name) {}
    
    const char* getTypeName() const override { return "Attack"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override;
};

/**
 * Look at target
 */
class BTLookAt : public BTNode {
public:
    BTLookAt(const std::string& targetKey = "TargetEntity",
             const std::string& name = "LookAt")
        : BTNode(name)
        , targetKey_(targetKey) {}
    
    const char* getTypeName() const override { return "LookAt"; }
    
    BTStatus execute(AIController* ai, Blackboard& bb) override;
    
private:
    std::string targetKey_;
};

// ============================================================================
// BEHAVIOR TREE
// ============================================================================

/**
 * The behavior tree itself
 */
class BehaviorTree {
public:
    BehaviorTree(const std::string& name = "BehaviorTree");
    ~BehaviorTree() = default;
    
    /**
     * Set the root node
     */
    void setRoot(std::unique_ptr<BTNode> root) {
        root_ = std::move(root);
    }
    
    /**
     * Get the root node
     */
    BTNode* getRoot() { return root_.get(); }
    const BTNode* getRoot() const { return root_.get(); }
    
    /**
     * Execute the tree
     */
    BTStatus execute(AIController* ai, Blackboard& bb);
    
    /**
     * Reset the tree (for re-execution from scratch)
     */
    void reset();
    
    /**
     * Get tree name
     */
    const std::string& getName() const { return name_; }
    
    /**
     * Save to file
     */
    bool saveToFile(const std::string& path) const;
    
    /**
     * Load from file
     */
    static std::unique_ptr<BehaviorTree> loadFromFile(const std::string& path);
    
private:
    std::string name_;
    std::unique_ptr<BTNode> root_;
    BTStatus lastStatus_ = BTStatus::Success;
};

// ============================================================================
// AI CONTROLLER
// ============================================================================

/**
 * AI controller that uses behavior trees
 */
class AIController {
public:
    AIController() = default;
    
    /**
     * Set the entity this controller controls
     */
    void setEntity(Entity entity) { entity_ = entity; }
    Entity getEntity() const { return entity_; }
    
    /**
     * Set the world reference
     */
    void setWorld(World* world) { world_ = world; }
    World* getWorld() { return world_; }
    
    /**
     * Set behavior tree
     */
    void setBehaviorTree(std::shared_ptr<BehaviorTree> tree) {
        behaviorTree_ = tree;
    }
    
    /**
     * Get behavior tree
     */
    BehaviorTree* getBehaviorTree() { return behaviorTree_.get(); }
    
    /**
     * Get blackboard
     */
    Blackboard& getBlackboard() { return blackboard_; }
    const Blackboard& getBlackboard() const { return blackboard_; }
    
    /**
     * Update the AI
     */
    void update(float deltaTime);
    
    // Movement interface
    void moveTo(const glm::vec3& destination);
    void stopMovement();
    bool hasReachedDestination() const;
    glm::vec3 getLocation() const;
    
    // Perception interface
    bool hasLineOfSightTo(Entity target) const;
    float getDistanceTo(Entity target) const;
    float getDistanceTo(const glm::vec3& location) const;
    
    // Combat interface
    void performAttack();
    bool isInAttackRange() const;
    
    // Rotation
    void lookAt(const glm::vec3& target);
    void lookAt(Entity target);
    
private:
    Entity entity_ = INVALID_ENTITY;
    World* world_ = nullptr;
    std::shared_ptr<BehaviorTree> behaviorTree_;
    Blackboard blackboard_;
};

// ============================================================================
// AI COMPONENT
// ============================================================================

/**
 * Component for AI-controlled entities
 */
struct AIComponent {
    std::shared_ptr<AIController> controller;
    std::shared_ptr<BehaviorTree> behaviorTree;
    
    // Perception
    float sightRange = 20.0f;
    float sightAngle = 120.0f;  // Degrees
    float hearingRange = 10.0f;
    
    // State
    bool active = true;
    Entity targetEntity = INVALID_ENTITY;
    glm::vec3 lastKnownTargetPosition = glm::vec3(0);
    
    // Alert level
    enum class AlertLevel {
        Idle,
        Suspicious,
        Alert,
        Combat
    } alertLevel = AlertLevel::Idle;
};

// ============================================================================
// AI SYSTEM
// ============================================================================

/**
 * System that updates AI controllers
 */
class AISystem : public System {
public:
    AISystem();
    
    void init(World& world) override;
    void update(World& world, float deltaTime) override;
    void shutdown(World& world) override;
    
private:
    void updatePerception(World& world, Entity entity, AIComponent& ai);
};

} // namespace Sanic
