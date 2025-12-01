/**
 * AbilitySystem.h
 * 
 * A structured system for character abilities:
 * - Boost ability
 * - Super jump
 * - Zipline attach
 * - Combat abilities (slash, ranged)
 * 
 * Based on UE5's Gameplay Ability System (GAS) concepts.
 */

#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace Sanic {

// Forward declarations
class KineticCharacterController;
class SplineComponent;
class World;
struct Entity;

// ============================================================================
// ABILITY STATE
// ============================================================================

enum class AbilityState {
    Ready,       // Can be activated
    Active,      // Currently running
    Cooldown,    // Waiting for cooldown
    Disabled     // Temporarily disabled
};

// ============================================================================
// ABILITY TAGS
// ============================================================================

struct AbilityTags {
    std::vector<std::string> ownedTags;       // Tags this ability has
    std::vector<std::string> blockedByTags;   // Can't activate if owner has these
    std::vector<std::string> blockTags;       // Blocks abilities with these tags while active
    std::vector<std::string> cancelTags;      // Cancels abilities with these tags on activation
};

// ============================================================================
// ABILITY CONTEXT
// ============================================================================

struct AbilityContext {
    KineticCharacterController* controller = nullptr;
    World* world = nullptr;
    Entity* ownerEntity = nullptr;
    
    // Input
    glm::vec3 aimDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 targetLocation = glm::vec3(0.0f);
    Entity* targetEntity = nullptr;
    
    // Delta time
    float deltaTime = 0.0f;
};

// ============================================================================
// BASE ABILITY CLASS
// ============================================================================

class Ability {
public:
    Ability(const std::string& name = "Ability");
    virtual ~Ability() = default;
    
    // ========== LIFECYCLE ==========
    
    /**
     * Check if ability can be activated
     */
    virtual bool canActivate(const AbilityContext& context) const;
    
    /**
     * Activate the ability
     */
    void activate(const AbilityContext& context);
    
    /**
     * Update active ability
     */
    void tick(float deltaTime, const AbilityContext& context);
    
    /**
     * End the ability (called internally or by cancel)
     */
    void deactivate();
    
    /**
     * Cancel the ability prematurely
     */
    virtual void cancel();
    
    /**
     * Force end without normal deactivation logic
     */
    void forceEnd();
    
    // ========== STATE ==========
    
    AbilityState getState() const { return state_; }
    bool isActive() const { return state_ == AbilityState::Active; }
    bool isReady() const { return state_ == AbilityState::Ready; }
    bool isOnCooldown() const { return state_ == AbilityState::Cooldown; }
    
    float getCooldownRemaining() const { return cooldownRemaining_; }
    float getCooldownPercent() const { return cooldown_ > 0 ? cooldownRemaining_ / cooldown_ : 0.0f; }
    
    float getActiveTime() const { return activeTime_; }
    
    // ========== PROPERTIES ==========
    
    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }
    
    float getCooldown() const { return cooldown_; }
    void setCooldown(float cooldown) { cooldown_ = cooldown; }
    
    float getResourceCost() const { return resourceCost_; }
    void setResourceCost(float cost) { resourceCost_ = cost; }
    
    const AbilityTags& getTags() const { return tags_; }
    AbilityTags& getTags() { return tags_; }
    
    // ========== CALLBACKS ==========
    
    using ActivateCallback = std::function<void(Ability*)>;
    using DeactivateCallback = std::function<void(Ability*)>;
    
    void setActivateCallback(ActivateCallback callback) { activateCallback_ = callback; }
    void setDeactivateCallback(DeactivateCallback callback) { deactivateCallback_ = callback; }
    
    // ========== INTERNAL UPDATE ==========
    
    void updateCooldown(float deltaTime);
    
protected:
    // Override these in derived classes
    virtual void onActivate(const AbilityContext& context) = 0;
    virtual void onTick(float deltaTime, const AbilityContext& context) {}
    virtual void onDeactivate() {}
    virtual void onCancel() {}
    
    // State
    AbilityState state_ = AbilityState::Ready;
    float cooldownRemaining_ = 0.0f;
    float activeTime_ = 0.0f;
    
    // Properties
    std::string name_;
    float cooldown_ = 1.0f;
    float resourceCost_ = 0.0f;
    float duration_ = 0.0f;  // 0 = instant/manual end
    
    // Tags
    AbilityTags tags_;
    
    // Callbacks
    ActivateCallback activateCallback_;
    DeactivateCallback deactivateCallback_;
    
    // Context cache (for tick/deactivate)
    AbilityContext cachedContext_;
};

// ============================================================================
// BOOST ABILITY
// ============================================================================

class BoostAbility : public Ability {
public:
    BoostAbility();
    
    float boostForce = 500.0f;
    float boostDuration = 0.5f;
    bool grantsInvincibility = true;
    
protected:
    void onActivate(const AbilityContext& context) override;
    void onTick(float deltaTime, const AbilityContext& context) override;
    void onDeactivate() override;
    
private:
    float timer_ = 0.0f;
};

// ============================================================================
// SUPER JUMP ABILITY
// ============================================================================

class SuperJumpAbility : public Ability {
public:
    SuperJumpAbility();
    
    float minJumpForce = 100.0f;
    float maxJumpForce = 500.0f;
    float chargeTime = 1.0f;
    bool requiresGround = true;
    
    float getCurrentCharge() const { return currentCharge_; }
    bool isCharging() const { return isCharging_; }
    
    void startCharge(const AbilityContext& context);
    void releaseCharge(const AbilityContext& context);
    void cancelCharge();
    
protected:
    void onActivate(const AbilityContext& context) override;
    void onTick(float deltaTime, const AbilityContext& context) override;
    void onDeactivate() override;
    void onCancel() override;
    
private:
    float currentCharge_ = 0.0f;
    bool isCharging_ = false;
};

// ============================================================================
// ZIPLINE ATTACH ABILITY
// ============================================================================

class ZiplineAttachAbility : public Ability {
public:
    ZiplineAttachAbility();
    
    float detectionRadius = 5.0f;
    std::string splineTag = "Zipline";
    
    SplineComponent* getAttachedSpline() const { return attachedSpline_; }
    
protected:
    void onActivate(const AbilityContext& context) override;
    void onTick(float deltaTime, const AbilityContext& context) override;
    void onDeactivate() override;
    
private:
    SplineComponent* findNearestSpline(const AbilityContext& context);
    SplineComponent* attachedSpline_ = nullptr;
};

// ============================================================================
// DASH ABILITY
// ============================================================================

class DashAbility : public Ability {
public:
    DashAbility();
    
    float dashDistance = 10.0f;
    float dashDuration = 0.2f;
    bool grantsInvincibility = true;
    bool canDashInAir = true;
    int maxAirDashes = 1;
    
protected:
    void onActivate(const AbilityContext& context) override;
    void onTick(float deltaTime, const AbilityContext& context) override;
    void onDeactivate() override;
    
private:
    glm::vec3 dashDirection_;
    glm::vec3 startPosition_;
    float timer_ = 0.0f;
    int airDashCount_ = 0;
};

// ============================================================================
// GROUND POUND ABILITY
// ============================================================================

class GroundPoundAbility : public Ability {
public:
    GroundPoundAbility();
    
    float descendSpeed = 50.0f;
    float impactRadius = 5.0f;
    float impactForce = 100.0f;
    bool requiresAirborne = true;
    
protected:
    void onActivate(const AbilityContext& context) override;
    void onTick(float deltaTime, const AbilityContext& context) override;
    void onDeactivate() override;
    
private:
    bool hasLanded_ = false;
};

// ============================================================================
// ABILITY COMPONENT
// ============================================================================

class AbilityComponent {
public:
    AbilityComponent();
    ~AbilityComponent() = default;
    
    // ========== ABILITY MANAGEMENT ==========
    
    /**
     * Add an ability
     * @return Ability index
     */
    uint32_t addAbility(std::unique_ptr<Ability> ability);
    
    /**
     * Remove an ability by index
     */
    void removeAbility(uint32_t index);
    
    /**
     * Get ability by index
     */
    Ability* getAbility(uint32_t index);
    const Ability* getAbility(uint32_t index) const;
    
    /**
     * Get ability by name
     */
    Ability* getAbilityByName(const std::string& name);
    
    /**
     * Get all abilities
     */
    const std::vector<std::unique_ptr<Ability>>& getAbilities() const { return abilities_; }
    
    // ========== ACTIVATION ==========
    
    /**
     * Try to activate an ability by index
     */
    bool tryActivate(uint32_t index, const AbilityContext& context);
    
    /**
     * Try to activate an ability by name
     */
    bool tryActivateByName(const std::string& name, const AbilityContext& context);
    
    /**
     * Cancel an ability by index
     */
    void cancelAbility(uint32_t index);
    
    /**
     * Cancel all active abilities
     */
    void cancelAllAbilities();
    
    /**
     * Cancel abilities with specific tags
     */
    void cancelAbilitiesWithTag(const std::string& tag);
    
    // ========== UPDATE ==========
    
    /**
     * Update all abilities (cooldowns, active abilities)
     */
    void update(float deltaTime, const AbilityContext& context);
    
    // ========== TAGS ==========
    
    /**
     * Check if any active ability has a blocking tag
     */
    bool hasBlockingTag(const std::string& tag) const;
    
    /**
     * Get all active ability tags
     */
    std::vector<std::string> getActiveAbilityTags() const;
    
    // ========== RESOURCES ==========
    
    float getResource() const { return currentResource_; }
    float getMaxResource() const { return maxResource_; }
    void setResource(float value) { currentResource_ = glm::clamp(value, 0.0f, maxResource_); }
    void setMaxResource(float max) { maxResource_ = max; currentResource_ = glm::min(currentResource_, max); }
    void addResource(float amount) { setResource(currentResource_ + amount); }
    bool consumeResource(float amount);
    
private:
    std::vector<std::unique_ptr<Ability>> abilities_;
    std::unordered_map<std::string, uint32_t> nameToIndex_;
    
    float currentResource_ = 100.0f;
    float maxResource_ = 100.0f;
};

} // namespace Sanic
