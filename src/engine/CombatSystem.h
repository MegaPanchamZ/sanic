/**
 * CombatSystem.h
 * 
 * Melee Combat System with Hitboxes, Damage, and Combos
 * 
 * Features:
 * - Hitbox volumes (sphere, capsule, box) attached to bones
 * - Damage events with knockback and hitstun
 * - Combo system with input buffering
 * - Animation notifies for hitbox activation
 * - Hit effects and audio
 * 
 * Reference:
 *   Engine/Plugins/Runtime/GameplayAbilities/
 *   Engine/Source/Runtime/Engine/Private/Animation/AnimNotify.cpp
 */

#pragma once

#include "ECS.h"
#include "Animation.h"
#include "PhysicsSystem.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <queue>

namespace Sanic {

class VulkanContext;
class PhysicsSystem;

// ============================================================================
// HITBOX SYSTEM
// ============================================================================

/**
 * Shape type for hitbox volumes
 */
enum class HitboxShape {
    Sphere,
    Capsule,
    Box
};

/**
 * A single hitbox volume attached to a bone
 */
struct HitboxVolume {
    std::string name;                  // Identifier for this hitbox
    HitboxShape shape = HitboxShape::Sphere;
    
    // Transform relative to attached bone
    glm::vec3 offset = glm::vec3(0);
    glm::quat rotation = glm::quat(1, 0, 0, 0);
    glm::vec3 size = glm::vec3(0.1f);  // Radius for sphere, half-extents for box
    float radius = 0.1f;               // For sphere/capsule
    float height = 0.5f;               // For capsule
    
    // Attachment
    std::string attachBoneName;
    int attachBoneIndex = -1;          // Cached bone index
    
    // State
    bool active = false;               // Is hitbox currently checking for hits
    
    // Combat properties
    float damageMultiplier = 1.0f;     // Multiplied with base attack damage
    bool blockable = true;             // Can be blocked
    bool parryable = true;             // Can be parried
    
    // Hit sound/effect
    std::string hitSoundCue;
    std::string hitEffectName;
};

/**
 * Result of a hit detection
 */
struct HitResult {
    Entity hitEntity;                  // Entity that was hit
    Entity attackerEntity;             // Entity that performed the attack
    
    glm::vec3 hitPoint;                // World position of hit
    glm::vec3 hitNormal;               // Normal at hit point
    
    std::string hitboxName;            // Which hitbox caused the hit
    std::string hurtboxName;           // Which hurtbox was hit (if applicable)
    
    float damageDealt = 0.0f;          // Actual damage after modifiers
    bool wasCritical = false;          // Was this a critical hit
    bool wasBlocked = false;           // Was the hit blocked
    bool wasParried = false;           // Was the hit parried
};

/**
 * Manages hitbox volumes for an entity
 */
class HitboxManager {
public:
    HitboxManager() = default;
    
    /**
     * Add a hitbox
     */
    void addHitbox(const HitboxVolume& hitbox);
    
    /**
     * Remove a hitbox by name
     */
    void removeHitbox(const std::string& name);
    
    /**
     * Get a hitbox by name
     */
    HitboxVolume* getHitbox(const std::string& name);
    const HitboxVolume* getHitbox(const std::string& name) const;
    
    /**
     * Get all hitboxes
     */
    const std::vector<HitboxVolume>& getHitboxes() const { return hitboxes_; }
    
    /**
     * Activate a hitbox by name
     */
    void activateHitbox(const std::string& name);
    
    /**
     * Deactivate a hitbox
     */
    void deactivateHitbox(const std::string& name);
    
    /**
     * Deactivate all hitboxes
     */
    void deactivateAll();
    
    /**
     * Update hitbox transforms from skeleton
     */
    void updateTransforms(const Skeleton& skeleton, const glm::mat4& worldTransform);
    
    /**
     * Get world transform for a hitbox
     */
    glm::mat4 getHitboxWorldTransform(const std::string& name) const;
    
    /**
     * Cache bone indices from skeleton
     */
    void cacheBoneIndices(const Skeleton& skeleton);
    
private:
    std::vector<HitboxVolume> hitboxes_;
    std::unordered_map<std::string, size_t> nameToIndex_;
    std::unordered_map<std::string, glm::mat4> worldTransforms_;
};

// ============================================================================
// DAMAGE SYSTEM
// ============================================================================

/**
 * Types of damage
 */
enum class DamageType {
    Physical,
    Fire,
    Ice,
    Lightning,
    Poison,
    Pure  // Ignores resistance
};

/**
 * Damage event data
 */
struct DamageEvent {
    Entity source;                     // Who caused the damage
    Entity target;                     // Who receives the damage
    
    float baseDamage = 0.0f;           // Base damage amount
    DamageType type = DamageType::Physical;
    
    // Modifiers
    float critMultiplier = 1.0f;       // Applied if critical hit
    bool isCritical = false;
    bool canCrit = true;
    
    // Effects
    glm::vec3 knockback = glm::vec3(0);  // Knockback direction and force
    float hitStunDuration = 0.0f;        // Duration of hitstun
    
    // Context
    glm::vec3 hitPoint = glm::vec3(0);
    glm::vec3 hitNormal = glm::vec3(0);
    std::string hitboxName;
};

/**
 * Damage modifiers applied by equipment, buffs, etc.
 */
struct DamageModifier {
    std::string id;                    // Unique identifier
    int priority = 0;                  // Order of application (higher = later)
    
    // Flat modifiers (applied first)
    float flatDamageBonus = 0.0f;
    float flatDamageReduction = 0.0f;
    
    // Percentage modifiers
    float damageMultiplier = 1.0f;
    float damageResistance = 0.0f;     // 0-1, percentage reduction
    
    // Type-specific
    std::unordered_map<DamageType, float> typeResistance;
    std::unordered_map<DamageType, float> typeMultiplier;
    
    // Conditional
    std::function<bool(const DamageEvent&)> condition;
};

/**
 * Health component with damage handling
 */
struct HealthComponent {
    float currentHealth = 100.0f;
    float maxHealth = 100.0f;
    
    // Shields/barriers (absorbed first)
    float currentShield = 0.0f;
    float maxShield = 0.0f;
    
    // State
    bool invulnerable = false;
    float invulnerabilityTimer = 0.0f;
    
    // Death
    bool isDead() const { return currentHealth <= 0.0f; }
    float healthPercent() const { return maxHealth > 0 ? currentHealth / maxHealth : 0.0f; }
    
    // Modifiers
    std::vector<DamageModifier> modifiers;
};

/**
 * Processes damage events
 */
class DamageProcessor {
public:
    using DamageCallback = std::function<void(const DamageEvent&, float finalDamage)>;
    using DeathCallback = std::function<void(Entity, const DamageEvent&)>;
    
    /**
     * Process a damage event
     * @return Final damage dealt after all modifiers
     */
    float processDamage(World& world, DamageEvent& event);
    
    /**
     * Register callback for when damage is dealt
     */
    void onDamageDealt(DamageCallback callback) { damageCallbacks_.push_back(callback); }
    
    /**
     * Register callback for death
     */
    void onDeath(DeathCallback callback) { deathCallbacks_.push_back(callback); }
    
private:
    std::vector<DamageCallback> damageCallbacks_;
    std::vector<DeathCallback> deathCallbacks_;
    
    float applyModifiers(const HealthComponent& health, DamageEvent& event);
};

// ============================================================================
// COMBO SYSTEM
// ============================================================================

/**
 * A single attack in a combo chain
 */
struct ComboAttack {
    std::string name;                  // Identifier
    std::string animationName;         // Animation to play
    
    float baseDamage = 10.0f;
    float knockbackForce = 5.0f;
    glm::vec3 knockbackDirection = glm::vec3(0, 0, 1);
    float hitStunDuration = 0.2f;
    
    // Timing
    float startupFrames = 5;           // Frames before hitbox active
    float activeFrames = 10;           // Frames hitbox is active
    float recoveryFrames = 15;         // Frames after hitbox inactive
    
    float windowStart = 0.0f;          // When can chain to next attack (0-1)
    float windowEnd = 1.0f;            // Window end time
    
    // Which hitbox to activate
    std::string hitboxName;
    
    // Cancel options
    std::vector<std::string> cancelInto;  // What attacks can cancel this one
    bool canCancelOnHit = true;
    bool canCancelOnBlock = true;
    bool canCancelOnWhiff = false;
};

/**
 * State of the combo system
 */
enum class ComboState {
    Idle,
    Startup,
    Active,
    Recovery,
    CancelWindow
};

/**
 * Controls combo attack sequences
 */
class ComboController {
public:
    ComboController() = default;
    
    /**
     * Register a combo chain
     */
    void registerComboChain(const std::string& name, const std::vector<ComboAttack>& attacks);
    
    /**
     * Get registered attacks
     */
    const std::vector<ComboAttack>& getComboChain(const std::string& name) const;
    
    /**
     * Handle attack input
     * @return Name of attack to play, or empty if no attack
     */
    std::string onAttackInput();
    
    /**
     * Update combo state
     */
    void update(float deltaTime);
    
    /**
     * Called when attack animation reaches certain points
     */
    void onAnimationEvent(const std::string& eventName);
    
    /**
     * Called when a hit connects
     */
    void onHitConnected();
    
    /**
     * Called when attack is blocked
     */
    void onHitBlocked();
    
    /**
     * Reset combo to idle
     */
    void reset();
    
    // State queries
    ComboState getState() const { return state_; }
    int getCurrentComboIndex() const { return currentComboIndex_; }
    const ComboAttack* getCurrentAttack() const;
    float getComboTimer() const { return comboTimer_; }
    
    // Input buffering
    void setBufferWindow(float seconds) { inputBufferWindow_ = seconds; }
    
private:
    std::unordered_map<std::string, std::vector<ComboAttack>> comboChains_;
    std::string currentChain_;
    int currentComboIndex_ = 0;
    
    ComboState state_ = ComboState::Idle;
    float comboTimer_ = 0.0f;
    float stateTimer_ = 0.0f;
    
    // Input buffer
    float inputBufferWindow_ = 0.2f;
    struct BufferedInput {
        float timestamp;
    };
    std::queue<BufferedInput> inputBuffer_;
    
    // Hit tracking
    bool hitConnected_ = false;
    bool hitBlocked_ = false;
    
    void advanceCombo();
    void transitionToState(ComboState newState);
    BufferedInput* peekBuffer();
    void clearOldInputs(float currentTime);
};

// ============================================================================
// MELEE COMBAT COMPONENT
// ============================================================================

/**
 * Component for entities that can engage in melee combat
 */
struct MeleeCombatComponent {
    // Hitbox management
    HitboxManager hitboxManager;
    
    // Combo system
    ComboController comboController;
    
    // Combat stats
    float baseDamage = 10.0f;
    float attackSpeed = 1.0f;          // Animation speed multiplier
    float criticalChance = 0.1f;       // 0-1
    float criticalMultiplier = 2.0f;
    
    // Hit tracking (prevent multi-hit per swing)
    std::unordered_set<uint32_t> hitEntities;
    
    // State
    bool isAttacking = false;
    bool canBeInterrupted = true;
    
    // Callbacks
    using HitCallback = std::function<void(const HitResult&)>;
    std::vector<HitCallback> onHitCallbacks;
    
    void clearHitTracking() { hitEntities.clear(); }
    
    bool hasHitEntity(Entity e) const { 
        return hitEntities.find(static_cast<uint32_t>(e)) != hitEntities.end(); 
    }
    
    void markEntityHit(Entity e) { 
        hitEntities.insert(static_cast<uint32_t>(e)); 
    }
};

// ============================================================================
// COMBAT SYSTEM
// ============================================================================

/**
 * System that handles melee combat logic
 */
class CombatSystem : public System {
public:
    CombatSystem();
    
    void init(World& world) override;
    void update(World& world, float deltaTime) override;
    void shutdown(World& world) override;
    
    /**
     * Set physics system for collision queries
     */
    void setPhysicsSystem(PhysicsSystem* physics) { physics_ = physics; }
    
    /**
     * Spawn hit effect at position
     */
    void spawnHitEffect(const glm::vec3& position, const std::string& effectName);
    
    /**
     * Play hit sound
     */
    void playHitSound(const glm::vec3& position, const std::string& soundCue);
    
    /**
     * Process attack input for an entity
     */
    void handleAttackInput(World& world, Entity entity);
    
private:
    PhysicsSystem* physics_ = nullptr;
    DamageProcessor damageProcessor_;
    
    /**
     * Check hitbox overlaps and process hits
     */
    void processHitboxes(World& world, Entity entity, MeleeCombatComponent& combat);
    
    /**
     * Update combo controllers
     */
    void updateCombos(World& world, float deltaTime);
    
    /**
     * Activate/deactivate hitboxes based on animation events
     */
    void handleAnimationNotifies(World& world, Entity entity, 
                                  MeleeCombatComponent& combat, 
                                  const std::string& notify);
};

// ============================================================================
// HURTBOX COMPONENT
// ============================================================================

/**
 * Defines regions that can receive damage
 */
struct HurtboxComponent {
    struct Hurtbox {
        std::string name;
        HitboxShape shape = HitboxShape::Capsule;
        glm::vec3 offset = glm::vec3(0);
        glm::vec3 size = glm::vec3(0.3f);
        float radius = 0.3f;
        float height = 1.0f;
        std::string attachBone;
        
        // Damage modifiers for this region
        float damageMultiplier = 1.0f;  // e.g., headshot = 2x
        bool critical = false;           // Hits here are always critical
    };
    
    std::vector<Hurtbox> hurtboxes;
    
    // Quick collision shape for broad phase
    HitboxShape broadphaseShape = HitboxShape::Capsule;
    float broadphaseRadius = 0.5f;
    float broadphaseHeight = 2.0f;
};

// ============================================================================
// BLOCKING AND PARRYING
// ============================================================================

/**
 * Component for entities that can block attacks
 */
struct BlockComponent {
    bool isBlocking = false;
    float blockStamina = 100.0f;
    float maxBlockStamina = 100.0f;
    
    // Block properties
    float damageReduction = 0.8f;      // How much damage is reduced
    float staminaCostPerBlock = 10.0f; // Stamina cost per blocked hit
    float knockbackReduction = 0.5f;   // How much knockback is reduced
    
    // Parry window
    bool isParrying = false;
    float parryWindow = 0.15f;         // Seconds of parry opportunity
    float parryTimer = 0.0f;
    
    // Guard break
    bool guardBroken = false;
    float guardBreakRecovery = 2.0f;   // Seconds to recover from guard break
    float guardBreakTimer = 0.0f;
    
    // Block direction (for directional blocking)
    glm::vec3 blockDirection = glm::vec3(0, 0, 1);
    float blockAngle = 90.0f;          // Degrees - attacks within this angle can be blocked
};

} // namespace Sanic
