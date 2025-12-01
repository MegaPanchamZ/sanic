/**
 * BehaviorTree.cpp
 * 
 * AI Behavior Tree System Implementation
 */

#include "BehaviorTree.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>

namespace Sanic {

// ============================================================================
// COMMON AI ACTIONS IMPLEMENTATION
// ============================================================================

BTStatus BTMoveTo::execute(AIController* ai, Blackboard& bb) {
    if (!ai || !bb.has(targetKey_)) {
        return BTStatus::Failure;
    }
    
    glm::vec3 target = bb.get<glm::vec3>(targetKey_);
    float distance = ai->getDistanceTo(target);
    
    if (distance <= acceptanceRadius_) {
        ai->stopMovement();
        return BTStatus::Success;
    }
    
    ai->moveTo(target);
    return BTStatus::Running;
}

BTStatus BTMoveToEntity::execute(AIController* ai, Blackboard& bb) {
    if (!ai || !bb.has(entityKey_)) {
        return BTStatus::Failure;
    }
    
    Entity target = bb.get<Entity>(entityKey_);
    if (target == INVALID_ENTITY) {
        return BTStatus::Failure;
    }
    
    float distance = ai->getDistanceTo(target);
    
    if (distance <= acceptanceRadius_) {
        ai->stopMovement();
        return BTStatus::Success;
    }
    
    // Get target location and move
    World* world = ai->getWorld();
    if (!world) return BTStatus::Failure;
    
    auto* transform = world->getComponent<Transform>(target);
    if (!transform) return BTStatus::Failure;
    
    ai->moveTo(transform->position);
    return BTStatus::Running;
}

BTStatus BTIsInRange::execute(AIController* ai, Blackboard& bb) {
    if (!ai || !bb.has(targetKey_)) {
        return BTStatus::Failure;
    }
    
    Entity target = bb.get<Entity>(targetKey_);
    if (target == INVALID_ENTITY) {
        return BTStatus::Failure;
    }
    
    float distance = ai->getDistanceTo(target);
    return distance <= range_ ? BTStatus::Success : BTStatus::Failure;
}

BTStatus BTAttack::execute(AIController* ai, Blackboard& bb) {
    if (!ai) return BTStatus::Failure;
    
    ai->performAttack();
    return BTStatus::Success;
}

BTStatus BTLookAt::execute(AIController* ai, Blackboard& bb) {
    if (!ai || !bb.has(targetKey_)) {
        return BTStatus::Failure;
    }
    
    Entity target = bb.get<Entity>(targetKey_);
    if (target == INVALID_ENTITY) {
        return BTStatus::Failure;
    }
    
    ai->lookAt(target);
    return BTStatus::Success;
}

// ============================================================================
// BEHAVIOR TREE IMPLEMENTATION
// ============================================================================

BehaviorTree::BehaviorTree(const std::string& name)
    : name_(name)
{
}

BTStatus BehaviorTree::execute(AIController* ai, Blackboard& bb) {
    if (!root_) {
        return BTStatus::Failure;
    }
    
    // If starting fresh or last execution completed
    if (lastStatus_ != BTStatus::Running) {
        root_->onEnter(ai, bb);
    }
    
    lastStatus_ = root_->execute(ai, bb);
    
    if (lastStatus_ != BTStatus::Running) {
        root_->onExit(ai, bb);
    }
    
    return lastStatus_;
}

void BehaviorTree::reset() {
    lastStatus_ = BTStatus::Success;
}

bool BehaviorTree::saveToFile(const std::string& path) const {
    using json = nlohmann::json;
    
    std::function<json(const BTNode*)> serializeNode = [&](const BTNode* node) -> json {
        if (!node) return nullptr;
        
        json j;
        j["type"] = node->getTypeName();
        j["name"] = node->getName();
        
        // Serialize children for composite/decorator nodes
        auto children = const_cast<BTNode*>(node)->getChildren();
        if (!children.empty()) {
            j["children"] = json::array();
            for (auto* child : children) {
                j["children"].push_back(serializeNode(child));
            }
        }
        
        return j;
    };
    
    json doc;
    doc["name"] = name_;
    doc["root"] = serializeNode(root_.get());
    
    std::ofstream file(path);
    if (!file.is_open()) return false;
    
    file << doc.dump(2);
    return true;
}

std::unique_ptr<BehaviorTree> BehaviorTree::loadFromFile(const std::string& path) {
    using json = nlohmann::json;
    
    std::ifstream file(path);
    if (!file.is_open()) return nullptr;
    
    json doc;
    try {
        file >> doc;
    } catch (const std::exception&) {
        return nullptr;
    }
    
    auto tree = std::make_unique<BehaviorTree>(doc.value("name", "Loaded Tree"));
    
    // Node factory function
    std::function<std::unique_ptr<BTNode>(const json&)> deserializeNode;
    deserializeNode = [&](const json& j) -> std::unique_ptr<BTNode> {
        if (j.is_null()) return nullptr;
        
        std::string type = j.value("type", "");
        std::string name = j.value("name", "");
        
        std::unique_ptr<BTNode> node;
        
        // Create node based on type
        if (type == "Selector") {
            auto selector = std::make_unique<BTSelector>(name);
            if (j.contains("children")) {
                for (const auto& child : j["children"]) {
                    selector->addChild(deserializeNode(child));
                }
            }
            node = std::move(selector);
        }
        else if (type == "Sequence") {
            auto sequence = std::make_unique<BTSequence>(name);
            if (j.contains("children")) {
                for (const auto& child : j["children"]) {
                    sequence->addChild(deserializeNode(child));
                }
            }
            node = std::move(sequence);
        }
        else if (type == "Parallel") {
            auto parallel = std::make_unique<BTParallel>();
            parallel->setName(name);
            if (j.contains("children")) {
                for (const auto& child : j["children"]) {
                    parallel->addChild(deserializeNode(child));
                }
            }
            node = std::move(parallel);
        }
        else if (type == "Inverter") {
            auto inverter = std::make_unique<BTInverter>();
            if (j.contains("children") && !j["children"].empty()) {
                inverter->setChild(deserializeNode(j["children"][0]));
            }
            node = std::move(inverter);
        }
        else if (type == "Succeeder") {
            auto succeeder = std::make_unique<BTSucceeder>();
            if (j.contains("children") && !j["children"].empty()) {
                succeeder->setChild(deserializeNode(j["children"][0]));
            }
            node = std::move(succeeder);
        }
        else if (type == "Repeater") {
            int count = j.value("repeatCount", -1);
            auto repeater = std::make_unique<BTRepeater>(count);
            if (j.contains("children") && !j["children"].empty()) {
                repeater->setChild(deserializeNode(j["children"][0]));
            }
            node = std::move(repeater);
        }
        else if (type == "Wait") {
            float time = j.value("waitTime", 1.0f);
            node = std::make_unique<BTWait>(time, name);
        }
        else if (type == "MoveTo") {
            std::string targetKey = j.value("targetKey", "MoveTarget");
            float radius = j.value("acceptanceRadius", 0.5f);
            node = std::make_unique<BTMoveTo>(targetKey, radius, name);
        }
        else if (type == "MoveToEntity") {
            std::string entityKey = j.value("entityKey", "TargetEntity");
            float radius = j.value("acceptanceRadius", 2.0f);
            node = std::make_unique<BTMoveToEntity>(entityKey, radius, name);
        }
        else if (type == "IsInRange") {
            std::string targetKey = j.value("targetKey", "TargetEntity");
            float range = j.value("range", 5.0f);
            node = std::make_unique<BTIsInRange>(targetKey, range, name);
        }
        else if (type == "Attack") {
            node = std::make_unique<BTAttack>(name);
        }
        else if (type == "LookAt") {
            std::string targetKey = j.value("targetKey", "TargetEntity");
            node = std::make_unique<BTLookAt>(targetKey, name);
        }
        
        return node;
    };
    
    if (doc.contains("root")) {
        tree->setRoot(deserializeNode(doc["root"]));
    }
    
    return tree;
}

// ============================================================================
// AI CONTROLLER IMPLEMENTATION
// ============================================================================

void AIController::update(float deltaTime) {
    if (!behaviorTree_) return;
    
    // Update time in blackboard
    float currentTime = blackboard_.get<float>("_time", 0.0f);
    blackboard_.set("_time", currentTime + deltaTime);
    blackboard_.set("_deltaTime", deltaTime);
    
    // Execute behavior tree
    behaviorTree_->execute(this, blackboard_);
}

void AIController::moveTo(const glm::vec3& destination) {
    if (!world_ || entity_ == INVALID_ENTITY) return;
    
    // Store destination for navigation system
    blackboard_.set("_navDestination", destination);
    blackboard_.set("_isMoving", true);
    
    // If we have a navigation component, update it
    // This would integrate with NavigationSystem
}

void AIController::stopMovement() {
    blackboard_.set("_isMoving", false);
}

bool AIController::hasReachedDestination() const {
    if (!blackboard_.has("_navDestination")) return true;
    
    glm::vec3 dest = blackboard_.get<glm::vec3>("_navDestination");
    glm::vec3 current = getLocation();
    
    return glm::distance(current, dest) < 0.5f;
}

glm::vec3 AIController::getLocation() const {
    if (!world_ || entity_ == INVALID_ENTITY) {
        return glm::vec3(0);
    }
    
    auto* transform = world_->getComponent<Transform>(entity_);
    if (!transform) return glm::vec3(0);
    
    return transform->position;
}

bool AIController::hasLineOfSightTo(Entity target) const {
    if (!world_ || entity_ == INVALID_ENTITY || target == INVALID_ENTITY) {
        return false;
    }
    
    auto* myTransform = world_->getComponent<Transform>(entity_);
    auto* targetTransform = world_->getComponent<Transform>(target);
    
    if (!myTransform || !targetTransform) return false;
    
    // TODO: Perform raycast using physics system
    // For now, just return true if in range
    float distance = glm::distance(myTransform->position, targetTransform->position);
    return distance < 50.0f;
}

float AIController::getDistanceTo(Entity target) const {
    if (!world_ || entity_ == INVALID_ENTITY || target == INVALID_ENTITY) {
        return std::numeric_limits<float>::max();
    }
    
    auto* myTransform = world_->getComponent<Transform>(entity_);
    auto* targetTransform = world_->getComponent<Transform>(target);
    
    if (!myTransform || !targetTransform) {
        return std::numeric_limits<float>::max();
    }
    
    return glm::distance(myTransform->position, targetTransform->position);
}

float AIController::getDistanceTo(const glm::vec3& location) const {
    return glm::distance(getLocation(), location);
}

void AIController::performAttack() {
    // Trigger attack animation and combat system
    if (!world_ || entity_ == INVALID_ENTITY) return;
    
    // TODO: Integrate with CombatSystem
}

bool AIController::isInAttackRange() const {
    Entity target = blackboard_.get<Entity>("TargetEntity");
    if (target == INVALID_ENTITY) return false;
    
    float attackRange = blackboard_.get<float>("AttackRange", 2.0f);
    return getDistanceTo(target) <= attackRange;
}

void AIController::lookAt(const glm::vec3& target) {
    if (!world_ || entity_ == INVALID_ENTITY) return;
    
    auto* transform = world_->getComponent<Transform>(entity_);
    if (!transform) return;
    
    glm::vec3 direction = glm::normalize(target - transform->position);
    
    // Calculate rotation to face direction
    float yaw = atan2(direction.x, direction.z);
    transform->rotation = glm::quat(glm::vec3(0, yaw, 0));
}

void AIController::lookAt(Entity target) {
    if (!world_ || target == INVALID_ENTITY) return;
    
    auto* targetTransform = world_->getComponent<Transform>(target);
    if (!targetTransform) return;
    
    lookAt(targetTransform->position);
}

// ============================================================================
// AI SYSTEM IMPLEMENTATION
// ============================================================================

AISystem::AISystem() {
}

void AISystem::init(World& world) {
    // Register AI component if not already
}

void AISystem::update(World& world, float deltaTime) {
    // Query all entities with AI components
    world.query<AIComponent, Transform>([&](Entity entity, AIComponent& ai, Transform& transform) {
        if (!ai.active) return;
        
        // Update perception
        updatePerception(world, entity, ai);
        
        // Update behavior tree through controller
        if (ai.controller) {
            ai.controller->setEntity(entity);
            ai.controller->setWorld(&world);
            
            if (ai.behaviorTree && !ai.controller->getBehaviorTree()) {
                ai.controller->setBehaviorTree(ai.behaviorTree);
            }
            
            // Update blackboard with entity info
            Blackboard& bb = ai.controller->getBlackboard();
            bb.set("SelfEntity", entity);
            bb.set("Position", transform.position);
            
            if (ai.targetEntity != INVALID_ENTITY) {
                bb.set("TargetEntity", ai.targetEntity);
            }
            
            ai.controller->update(deltaTime);
        }
    });
}

void AISystem::updatePerception(World& world, Entity entity, AIComponent& ai) {
    auto* myTransform = world.getComponent<Transform>(entity);
    if (!myTransform) return;
    
    // Find potential targets (entities with Health component that are not self)
    Entity nearestEnemy = INVALID_ENTITY;
    float nearestDistance = ai.sightRange;
    
    world.query<Transform, Health>([&](Entity other, Transform& otherTransform, Health& health) {
        if (other == entity) return;  // Skip self
        if (health.current <= 0) return;  // Skip dead entities
        
        float distance = glm::distance(myTransform->position, otherTransform->position);
        
        if (distance > ai.sightRange) return;  // Out of sight range
        
        // Check sight cone
        glm::vec3 toTarget = glm::normalize(otherTransform->position - myTransform->position);
        glm::vec3 forward = myTransform->rotation * glm::vec3(0, 0, 1);
        
        float dot = glm::dot(forward, toTarget);
        float angle = glm::degrees(acos(dot));
        
        if (angle > ai.sightAngle * 0.5f) return;  // Outside field of view
        
        // TODO: Raycast for line of sight check
        
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestEnemy = other;
        }
    });
    
    // Update target
    if (nearestEnemy != INVALID_ENTITY) {
        ai.targetEntity = nearestEnemy;
        
        auto* targetTransform = world.getComponent<Transform>(nearestEnemy);
        if (targetTransform) {
            ai.lastKnownTargetPosition = targetTransform->position;
        }
        
        // Update alert level
        if (nearestDistance < 5.0f) {
            ai.alertLevel = AIComponent::AlertLevel::Combat;
        } else if (nearestDistance < 10.0f) {
            ai.alertLevel = AIComponent::AlertLevel::Alert;
        } else {
            ai.alertLevel = AIComponent::AlertLevel::Suspicious;
        }
    } else if (ai.alertLevel != AIComponent::AlertLevel::Idle) {
        // Gradually reduce alert level when no target
        // This would use a timer in practice
    }
}

void AISystem::shutdown(World& world) {
    // Cleanup
}

// ============================================================================
// BEHAVIOR TREE BUILDER
// ============================================================================

/**
 * Helper class for building behavior trees fluently
 */
class BehaviorTreeBuilder {
public:
    BehaviorTreeBuilder(const std::string& name = "Tree")
        : tree_(std::make_unique<BehaviorTree>(name)) {}
    
    /**
     * Start a selector node
     */
    BehaviorTreeBuilder& selector(const std::string& name = "Selector") {
        pushComposite(std::make_unique<BTSelector>(name));
        return *this;
    }
    
    /**
     * Start a sequence node
     */
    BehaviorTreeBuilder& sequence(const std::string& name = "Sequence") {
        pushComposite(std::make_unique<BTSequence>(name));
        return *this;
    }
    
    /**
     * Start a parallel node
     */
    BehaviorTreeBuilder& parallel(BTParallel::Policy successPolicy = BTParallel::Policy::RequireAll,
                                   BTParallel::Policy failurePolicy = BTParallel::Policy::RequireOne,
                                   const std::string& name = "Parallel") {
        pushComposite(std::make_unique<BTParallel>(successPolicy, failurePolicy, name));
        return *this;
    }
    
    /**
     * Add inverter decorator
     */
    BehaviorTreeBuilder& inverter() {
        pushDecorator(std::make_unique<BTInverter>());
        return *this;
    }
    
    /**
     * Add repeater decorator
     */
    BehaviorTreeBuilder& repeater(int count = -1) {
        pushDecorator(std::make_unique<BTRepeater>(count));
        return *this;
    }
    
    /**
     * Add cooldown decorator
     */
    BehaviorTreeBuilder& cooldown(float time) {
        pushDecorator(std::make_unique<BTCooldown>(time));
        return *this;
    }
    
    /**
     * Add condition decorator
     */
    BehaviorTreeBuilder& condition(BTConditionDecorator::ConditionFunc func,
                                    const std::string& name = "Condition") {
        pushDecorator(std::make_unique<BTConditionDecorator>(func, nullptr, name));
        return *this;
    }
    
    /**
     * Add action leaf
     */
    BehaviorTreeBuilder& action(BTAction::ActionFunc func, const std::string& name = "Action") {
        addLeaf(std::make_unique<BTAction>(func, name));
        return *this;
    }
    
    /**
     * Add wait leaf
     */
    BehaviorTreeBuilder& wait(float time) {
        addLeaf(std::make_unique<BTWait>(time));
        return *this;
    }
    
    /**
     * Add move to leaf
     */
    BehaviorTreeBuilder& moveTo(const std::string& targetKey = "MoveTarget",
                                 float acceptanceRadius = 0.5f) {
        addLeaf(std::make_unique<BTMoveTo>(targetKey, acceptanceRadius));
        return *this;
    }
    
    /**
     * Add move to entity leaf
     */
    BehaviorTreeBuilder& moveToEntity(const std::string& entityKey = "TargetEntity",
                                       float acceptanceRadius = 2.0f) {
        addLeaf(std::make_unique<BTMoveToEntity>(entityKey, acceptanceRadius));
        return *this;
    }
    
    /**
     * Add is in range condition
     */
    BehaviorTreeBuilder& isInRange(const std::string& targetKey = "TargetEntity",
                                    float range = 5.0f) {
        addLeaf(std::make_unique<BTIsInRange>(targetKey, range));
        return *this;
    }
    
    /**
     * Add attack leaf
     */
    BehaviorTreeBuilder& attack() {
        addLeaf(std::make_unique<BTAttack>());
        return *this;
    }
    
    /**
     * Add look at leaf
     */
    BehaviorTreeBuilder& lookAt(const std::string& targetKey = "TargetEntity") {
        addLeaf(std::make_unique<BTLookAt>(targetKey));
        return *this;
    }
    
    /**
     * End current composite/decorator
     */
    BehaviorTreeBuilder& end() {
        if (!nodeStack_.empty()) {
            auto node = std::move(nodeStack_.back());
            nodeStack_.pop_back();
            
            if (nodeStack_.empty()) {
                tree_->setRoot(std::move(node));
            } else {
                addToParent(std::move(node));
            }
        }
        return *this;
    }
    
    /**
     * Build and return the tree
     */
    std::unique_ptr<BehaviorTree> build() {
        // End any remaining open nodes
        while (!nodeStack_.empty()) {
            end();
        }
        return std::move(tree_);
    }
    
private:
    void pushComposite(std::unique_ptr<BTComposite> composite) {
        nodeStack_.push_back(std::move(composite));
    }
    
    void pushDecorator(std::unique_ptr<BTDecorator> decorator) {
        decoratorStack_.push_back(std::move(decorator));
    }
    
    void addLeaf(std::unique_ptr<BTNode> leaf) {
        // Apply any pending decorators
        auto node = std::move(leaf);
        while (!decoratorStack_.empty()) {
            auto decorator = std::move(decoratorStack_.back());
            decoratorStack_.pop_back();
            decorator->setChild(std::move(node));
            node = std::move(decorator);
        }
        
        if (nodeStack_.empty()) {
            tree_->setRoot(std::move(node));
        } else {
            addToParent(std::move(node));
        }
    }
    
    void addToParent(std::unique_ptr<BTNode> node) {
        auto& parent = nodeStack_.back();
        if (auto* composite = dynamic_cast<BTComposite*>(parent.get())) {
            composite->addChild(std::move(node));
        }
    }
    
    std::unique_ptr<BehaviorTree> tree_;
    std::vector<std::unique_ptr<BTNode>> nodeStack_;
    std::vector<std::unique_ptr<BTDecorator>> decoratorStack_;
};

// ============================================================================
// EXAMPLE BEHAVIOR TREES
// ============================================================================

/**
 * Create a simple patrol behavior tree
 */
inline std::unique_ptr<BehaviorTree> createPatrolBehavior() {
    BehaviorTreeBuilder builder("Patrol");
    
    return builder
        .selector("Root")
            // Combat behavior
            .sequence("Combat")
                .action([](AIController* ai, Blackboard& bb) {
                    // Check if we have a target
                    return bb.has("TargetEntity") ? BTStatus::Success : BTStatus::Failure;
                }, "HasTarget")
                .selector("Attack or Chase")
                    .sequence("Attack")
                        .isInRange("TargetEntity", 2.0f)
                        .lookAt("TargetEntity")
                        .attack()
                    .end()
                    .sequence("Chase")
                        .moveToEntity("TargetEntity", 1.5f)
                    .end()
                .end()
            .end()
            // Patrol behavior
            .sequence("Patrol")
                .action([](AIController* ai, Blackboard& bb) {
                    // Get next patrol point
                    int index = bb.get<int>("PatrolIndex", 0);
                    // Would get patrol points from component/blackboard
                    return BTStatus::Success;
                }, "GetPatrolPoint")
                .moveTo("PatrolTarget", 0.5f)
                .wait(2.0f)
                .action([](AIController* ai, Blackboard& bb) {
                    // Increment patrol index
                    int index = bb.get<int>("PatrolIndex", 0);
                    bb.set("PatrolIndex", index + 1);
                    return BTStatus::Success;
                }, "NextPatrolPoint")
            .end()
        .end()
        .build();
}

/**
 * Create a simple guard behavior tree
 */
inline std::unique_ptr<BehaviorTree> createGuardBehavior() {
    BehaviorTreeBuilder builder("Guard");
    
    return builder
        .selector("Root")
            // Alert behavior - investigate suspicious activity
            .sequence("Investigate")
                .condition([](AIController* ai, Blackboard& bb) {
                    return bb.has("SuspiciousLocation");
                }, "HasSuspiciousLocation")
                .moveTo("SuspiciousLocation", 1.0f)
                .wait(3.0f)
                .action([](AIController* ai, Blackboard& bb) {
                    bb.remove("SuspiciousLocation");
                    return BTStatus::Success;
                }, "ClearSuspicion")
            .end()
            // Combat
            .sequence("Combat")
                .condition([](AIController* ai, Blackboard& bb) {
                    return bb.has("TargetEntity");
                }, "HasTarget")
                .parallel()
                    .lookAt("TargetEntity")
                    .selector()
                        .sequence("Melee")
                            .isInRange("TargetEntity", 2.0f)
                            .attack()
                        .end()
                        .moveToEntity("TargetEntity", 1.5f)
                    .end()
                .end()
            .end()
            // Idle - look around
            .sequence("Idle")
                .wait(3.0f)
                .action([](AIController* ai, Blackboard& bb) {
                    // Random look direction
                    return BTStatus::Success;
                }, "LookAround")
            .end()
        .end()
        .build();
}

} // namespace Sanic
