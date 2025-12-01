/**
 * CombatSystem.cpp
 * 
 * Implementation of melee combat system
 */

#include "CombatSystem.h"
#include <algorithm>
#include <cmath>

namespace Sanic {

// ============================================================================
// HITBOX MANAGER IMPLEMENTATION
// ============================================================================

void HitboxManager::addHitbox(const HitboxVolume& hitbox) {
    nameToIndex_[hitbox.name] = hitboxes_.size();
    hitboxes_.push_back(hitbox);
}

void HitboxManager::removeHitbox(const std::string& name) {
    auto it = nameToIndex_.find(name);
    if (it == nameToIndex_.end()) return;
    
    size_t idx = it->second;
    
    // Swap with last and pop
    if (idx < hitboxes_.size() - 1) {
        std::swap(hitboxes_[idx], hitboxes_.back());
        nameToIndex_[hitboxes_[idx].name] = idx;
    }
    
    hitboxes_.pop_back();
    nameToIndex_.erase(it);
}

HitboxVolume* HitboxManager::getHitbox(const std::string& name) {
    auto it = nameToIndex_.find(name);
    return it != nameToIndex_.end() ? &hitboxes_[it->second] : nullptr;
}

const HitboxVolume* HitboxManager::getHitbox(const std::string& name) const {
    auto it = nameToIndex_.find(name);
    return it != nameToIndex_.end() ? &hitboxes_[it->second] : nullptr;
}

void HitboxManager::activateHitbox(const std::string& name) {
    if (auto* hb = getHitbox(name)) {
        hb->active = true;
    }
}

void HitboxManager::deactivateHitbox(const std::string& name) {
    if (auto* hb = getHitbox(name)) {
        hb->active = false;
    }
}

void HitboxManager::deactivateAll() {
    for (auto& hb : hitboxes_) {
        hb.active = false;
    }
}

void HitboxManager::updateTransforms(const Skeleton& skeleton, const glm::mat4& worldTransform) {
    for (auto& hb : hitboxes_) {
        if (hb.attachBoneIndex < 0) continue;
        
        glm::mat4 boneWorld = worldTransform * skeleton.bones[hb.attachBoneIndex].globalTransform;
        glm::mat4 localOffset = glm::translate(glm::mat4(1.0f), hb.offset) * 
                                glm::mat4_cast(hb.rotation);
        
        worldTransforms_[hb.name] = boneWorld * localOffset;
    }
}

glm::mat4 HitboxManager::getHitboxWorldTransform(const std::string& name) const {
    auto it = worldTransforms_.find(name);
    return it != worldTransforms_.end() ? it->second : glm::mat4(1.0f);
}

void HitboxManager::cacheBoneIndices(const Skeleton& skeleton) {
    for (auto& hb : hitboxes_) {
        hb.attachBoneIndex = skeleton.findBone(hb.attachBoneName);
    }
}

// ============================================================================
// DAMAGE PROCESSOR IMPLEMENTATION
// ============================================================================

float DamageProcessor::processDamage(World& world, DamageEvent& event) {
    auto* health = world.tryGetComponent<HealthComponent>(event.target);
    if (!health) return 0.0f;
    
    if (health->invulnerable || health->isDead()) return 0.0f;
    
    // Calculate final damage
    float finalDamage = applyModifiers(*health, event);
    
    // Apply to shield first
    if (health->currentShield > 0.0f) {
        float shieldDamage = std::min(health->currentShield, finalDamage);
        health->currentShield -= shieldDamage;
        finalDamage -= shieldDamage;
    }
    
    // Apply remaining damage to health
    health->currentHealth = std::max(0.0f, health->currentHealth - finalDamage);
    
    // Notify callbacks
    for (auto& callback : damageCallbacks_) {
        callback(event, finalDamage);
    }
    
    // Check for death
    if (health->isDead()) {
        for (auto& callback : deathCallbacks_) {
            callback(event.target, event);
        }
    }
    
    return finalDamage;
}

float DamageProcessor::applyModifiers(const HealthComponent& health, DamageEvent& event) {
    float damage = event.baseDamage;
    
    // Sort modifiers by priority
    std::vector<const DamageModifier*> sortedModifiers;
    for (const auto& mod : health.modifiers) {
        if (!mod.condition || mod.condition(event)) {
            sortedModifiers.push_back(&mod);
        }
    }
    
    std::sort(sortedModifiers.begin(), sortedModifiers.end(),
        [](const DamageModifier* a, const DamageModifier* b) {
            return a->priority < b->priority;
        });
    
    // Apply modifiers
    for (const auto* mod : sortedModifiers) {
        // Flat bonuses/reductions
        damage += mod->flatDamageBonus;
        damage -= mod->flatDamageReduction;
        
        // Percentage multipliers
        damage *= mod->damageMultiplier;
        
        // Type-specific resistance
        auto typeResIt = mod->typeResistance.find(event.type);
        if (typeResIt != mod->typeResistance.end()) {
            damage *= (1.0f - typeResIt->second);
        }
        
        // Type-specific multiplier
        auto typeMultIt = mod->typeMultiplier.find(event.type);
        if (typeMultIt != mod->typeMultiplier.end()) {
            damage *= typeMultIt->second;
        }
        
        // General resistance
        damage *= (1.0f - mod->damageResistance);
    }
    
    // Apply critical hit
    if (event.isCritical) {
        damage *= event.critMultiplier;
    }
    
    return std::max(0.0f, damage);
}

// ============================================================================
// COMBO CONTROLLER IMPLEMENTATION
// ============================================================================

void ComboController::registerComboChain(const std::string& name, const std::vector<ComboAttack>& attacks) {
    comboChains_[name] = attacks;
    
    if (currentChain_.empty()) {
        currentChain_ = name;
    }
}

const std::vector<ComboAttack>& ComboController::getComboChain(const std::string& name) const {
    static const std::vector<ComboAttack> empty;
    auto it = comboChains_.find(name);
    return it != comboChains_.end() ? it->second : empty;
}

std::string ComboController::onAttackInput() {
    // Buffer the input
    BufferedInput input;
    input.timestamp = comboTimer_;
    inputBuffer_.push(input);
    
    // If idle, start first attack
    if (state_ == ComboState::Idle) {
        const auto& chain = getComboChain(currentChain_);
        if (!chain.empty()) {
            currentComboIndex_ = 0;
            transitionToState(ComboState::Startup);
            return chain[0].animationName;
        }
    }
    
    // If in cancel window, try to advance
    if (state_ == ComboState::CancelWindow || state_ == ComboState::Recovery) {
        const auto* attack = getCurrentAttack();
        if (attack) {
            // Check if can cancel
            bool canCancel = (hitConnected_ && attack->canCancelOnHit) ||
                            (hitBlocked_ && attack->canCancelOnBlock) ||
                            (!hitConnected_ && !hitBlocked_ && attack->canCancelOnWhiff);
            
            if (canCancel) {
                advanceCombo();
                const auto* nextAttack = getCurrentAttack();
                if (nextAttack) {
                    return nextAttack->animationName;
                }
            }
        }
    }
    
    return "";
}

void ComboController::update(float deltaTime) {
    comboTimer_ += deltaTime;
    stateTimer_ += deltaTime;
    
    // Clear old buffered inputs
    clearOldInputs(comboTimer_);
    
    const auto* attack = getCurrentAttack();
    if (!attack) {
        if (state_ != ComboState::Idle) {
            reset();
        }
        return;
    }
    
    // 60 FPS assumed for frame data
    float frameTime = 1.0f / 60.0f;
    float startupTime = attack->startupFrames * frameTime;
    float activeTime = attack->activeFrames * frameTime;
    float recoveryTime = attack->recoveryFrames * frameTime;
    
    switch (state_) {
        case ComboState::Startup:
            if (stateTimer_ >= startupTime) {
                transitionToState(ComboState::Active);
            }
            break;
            
        case ComboState::Active:
            if (stateTimer_ >= activeTime) {
                transitionToState(ComboState::Recovery);
            }
            break;
            
        case ComboState::Recovery: {
            float totalTime = startupTime + activeTime + recoveryTime;
            float progress = (stateTimer_ + startupTime + activeTime) / totalTime;
            
            if (progress >= attack->windowStart && progress <= attack->windowEnd) {
                transitionToState(ComboState::CancelWindow);
            } else if (stateTimer_ >= recoveryTime) {
                reset();
            }
            break;
        }
            
        case ComboState::CancelWindow: {
            float totalTime = startupTime + activeTime + recoveryTime;
            float progress = (comboTimer_ - (startupTime)) / totalTime;
            
            // Check for buffered input
            if (auto* buffered = peekBuffer()) {
                // Try to advance combo
                advanceCombo();
                inputBuffer_.pop();
                
                const auto* nextAttack = getCurrentAttack();
                if (nextAttack) {
                    transitionToState(ComboState::Startup);
                    return;
                }
            }
            
            if (progress > attack->windowEnd) {
                reset();
            }
            break;
        }
            
        default:
            break;
    }
}

void ComboController::onAnimationEvent(const std::string& eventName) {
    // Handle animation notifies
    // e.g., "HitboxActive", "HitboxInactive", "CanCancel"
}

void ComboController::onHitConnected() {
    hitConnected_ = true;
}

void ComboController::onHitBlocked() {
    hitBlocked_ = true;
}

void ComboController::reset() {
    state_ = ComboState::Idle;
    currentComboIndex_ = 0;
    comboTimer_ = 0.0f;
    stateTimer_ = 0.0f;
    hitConnected_ = false;
    hitBlocked_ = false;
    
    // Clear input buffer
    while (!inputBuffer_.empty()) {
        inputBuffer_.pop();
    }
}

const ComboAttack* ComboController::getCurrentAttack() const {
    const auto& chain = comboChains_.find(currentChain_);
    if (chain == comboChains_.end()) return nullptr;
    
    if (currentComboIndex_ >= 0 && currentComboIndex_ < static_cast<int>(chain->second.size())) {
        return &chain->second[currentComboIndex_];
    }
    
    return nullptr;
}

void ComboController::advanceCombo() {
    const auto& chain = getComboChain(currentChain_);
    
    currentComboIndex_++;
    
    if (currentComboIndex_ >= static_cast<int>(chain.size())) {
        currentComboIndex_ = 0;  // Loop back to first attack
    }
    
    stateTimer_ = 0.0f;
    hitConnected_ = false;
    hitBlocked_ = false;
}

void ComboController::transitionToState(ComboState newState) {
    state_ = newState;
    stateTimer_ = 0.0f;
}

ComboController::BufferedInput* ComboController::peekBuffer() {
    if (inputBuffer_.empty()) return nullptr;
    return &inputBuffer_.front();
}

void ComboController::clearOldInputs(float currentTime) {
    while (!inputBuffer_.empty()) {
        if (currentTime - inputBuffer_.front().timestamp > inputBufferWindow_) {
            inputBuffer_.pop();
        } else {
            break;
        }
    }
}

// ============================================================================
// COMBAT SYSTEM IMPLEMENTATION
// ============================================================================

CombatSystem::CombatSystem() {
    requireComponent<MeleeCombatComponent>();
    requireComponent<Transform>();
}

void CombatSystem::init(World& world) {
    // Register damage callbacks
    damageProcessor_.onDamageDealt([this](const DamageEvent& event, float damage) {
        // Spawn hit effect
        spawnHitEffect(event.hitPoint, "default_hit");
        
        // Play hit sound
        playHitSound(event.hitPoint, "hit_flesh");
    });
    
    damageProcessor_.onDeath([](Entity entity, const DamageEvent& event) {
        // Handle death - could emit event, play animation, etc.
    });
}

void CombatSystem::update(World& world, float deltaTime) {
    updateCombos(world, deltaTime);
    
    // Process hitboxes for each entity with combat component
    for (auto& [entity, transform, combat] : world.query<Transform, MeleeCombatComponent>()) {
        // Skip if no active hitboxes
        bool hasActiveHitbox = false;
        for (const auto& hb : combat.hitboxManager.getHitboxes()) {
            if (hb.active) {
                hasActiveHitbox = true;
                break;
            }
        }
        
        if (!hasActiveHitbox) continue;
        
        processHitboxes(world, entity, combat);
    }
}

void CombatSystem::shutdown(World& world) {
    // Cleanup
}

void CombatSystem::handleAttackInput(World& world, Entity entity) {
    auto* combat = world.tryGetComponent<MeleeCombatComponent>(entity);
    if (!combat) return;
    
    std::string animName = combat->comboController.onAttackInput();
    
    if (!animName.empty()) {
        // Play attack animation
        // This would trigger animation system
        combat->isAttacking = true;
        combat->clearHitTracking();
    }
}

void CombatSystem::processHitboxes(World& world, Entity entity, MeleeCombatComponent& combat) {
    if (!physics_) return;
    
    const auto& transform = world.getComponent<Transform>(entity);
    
    for (const auto& hitbox : combat.hitboxManager.getHitboxes()) {
        if (!hitbox.active) continue;
        
        glm::mat4 hitboxWorld = combat.hitboxManager.getHitboxWorldTransform(hitbox.name);
        glm::vec3 hitboxPos = glm::vec3(hitboxWorld[3]);
        
        // Query physics for overlaps
        // This is a placeholder - actual implementation would use Jolt physics
        std::vector<Entity> potentialHits;
        
        // For each potential hit, check if it has a hurtbox and isn't already hit
        for (Entity target : potentialHits) {
            if (target == entity) continue;  // Don't hit self
            if (combat.hasHitEntity(target)) continue;  // Already hit this swing
            
            auto* targetHealth = world.tryGetComponent<HealthComponent>(target);
            if (!targetHealth) continue;
            
            // Check for blocking
            auto* targetBlock = world.tryGetComponent<BlockComponent>(target);
            bool wasBlocked = false;
            bool wasParried = false;
            
            if (targetBlock && targetBlock->isBlocking && hitbox.blockable) {
                // Check block angle
                auto* targetTransform = world.tryGetComponent<Transform>(target);
                if (targetTransform) {
                    glm::vec3 toAttacker = glm::normalize(transform.position - targetTransform->position);
                    float angle = glm::degrees(std::acos(glm::dot(toAttacker, targetBlock->blockDirection)));
                    
                    if (angle <= targetBlock->blockAngle * 0.5f) {
                        wasBlocked = true;
                        
                        // Check for parry
                        if (targetBlock->isParrying && hitbox.parryable) {
                            wasParried = true;
                        }
                    }
                }
            }
            
            // Create damage event
            DamageEvent dmgEvent;
            dmgEvent.source = entity;
            dmgEvent.target = target;
            dmgEvent.baseDamage = combat.baseDamage * hitbox.damageMultiplier;
            dmgEvent.type = DamageType::Physical;
            dmgEvent.hitPoint = hitboxPos;  // Simplified - should be contact point
            dmgEvent.hitboxName = hitbox.name;
            
            // Check for critical hit
            dmgEvent.canCrit = true;
            float critRoll = static_cast<float>(rand()) / RAND_MAX;
            dmgEvent.isCritical = critRoll < combat.criticalChance;
            dmgEvent.critMultiplier = combat.criticalMultiplier;
            
            // Get current attack for knockback
            const ComboAttack* currentAttack = combat.comboController.getCurrentAttack();
            if (currentAttack) {
                dmgEvent.knockback = transform.forward() * currentAttack->knockbackForce;
                dmgEvent.hitStunDuration = currentAttack->hitStunDuration;
            }
            
            // Process the hit
            float finalDamage = 0.0f;
            
            if (wasParried) {
                // Parry - no damage, attacker gets staggered
                combat.comboController.onHitBlocked();
            } else if (wasBlocked) {
                // Blocked - reduced damage
                dmgEvent.baseDamage *= (1.0f - targetBlock->damageReduction);
                dmgEvent.knockback *= (1.0f - targetBlock->knockbackReduction);
                
                finalDamage = damageProcessor_.processDamage(world, dmgEvent);
                combat.comboController.onHitBlocked();
                
                // Consume block stamina
                targetBlock->blockStamina -= targetBlock->staminaCostPerBlock;
                if (targetBlock->blockStamina <= 0.0f) {
                    targetBlock->guardBroken = true;
                    targetBlock->guardBreakTimer = targetBlock->guardBreakRecovery;
                }
            } else {
                // Clean hit
                finalDamage = damageProcessor_.processDamage(world, dmgEvent);
                combat.comboController.onHitConnected();
            }
            
            // Mark as hit
            combat.markEntityHit(target);
            
            // Create hit result
            HitResult result;
            result.hitEntity = target;
            result.attackerEntity = entity;
            result.hitPoint = dmgEvent.hitPoint;
            result.hitboxName = hitbox.name;
            result.damageDealt = finalDamage;
            result.wasCritical = dmgEvent.isCritical;
            result.wasBlocked = wasBlocked;
            result.wasParried = wasParried;
            
            // Notify callbacks
            for (auto& callback : combat.onHitCallbacks) {
                callback(result);
            }
            
            // Spawn effects
            spawnHitEffect(result.hitPoint, hitbox.hitEffectName);
            playHitSound(result.hitPoint, hitbox.hitSoundCue);
        }
    }
}

void CombatSystem::updateCombos(World& world, float deltaTime) {
    for (auto& [entity, combat] : world.query<MeleeCombatComponent>()) {
        combat.comboController.update(deltaTime);
        
        // Update hitbox activation based on combo state
        ComboState state = combat.comboController.getState();
        const ComboAttack* attack = combat.comboController.getCurrentAttack();
        
        if (attack) {
            if (state == ComboState::Active) {
                combat.hitboxManager.activateHitbox(attack->hitboxName);
            } else {
                combat.hitboxManager.deactivateHitbox(attack->hitboxName);
            }
        }
        
        // Reset attack state when combo ends
        if (state == ComboState::Idle) {
            combat.isAttacking = false;
            combat.hitboxManager.deactivateAll();
        }
    }
}

void CombatSystem::handleAnimationNotifies(World& world, Entity entity,
                                            MeleeCombatComponent& combat,
                                            const std::string& notify) {
    if (notify.find("HitboxActive_") == 0) {
        std::string hitboxName = notify.substr(13);
        combat.hitboxManager.activateHitbox(hitboxName);
        combat.clearHitTracking();
    } else if (notify.find("HitboxInactive_") == 0) {
        std::string hitboxName = notify.substr(15);
        combat.hitboxManager.deactivateHitbox(hitboxName);
    } else if (notify == "HitboxInactiveAll") {
        combat.hitboxManager.deactivateAll();
    }
    
    combat.comboController.onAnimationEvent(notify);
}

void CombatSystem::spawnHitEffect(const glm::vec3& position, const std::string& effectName) {
    // Would spawn particle effect at position
    // Integration with particle system
}

void CombatSystem::playHitSound(const glm::vec3& position, const std::string& soundCue) {
    // Would play positional audio
    // Integration with audio system
}

} // namespace Sanic
