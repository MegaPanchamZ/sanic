/**
 * AbilitySystem.cpp
 * 
 * Implementation of the ability system.
 */

#include "AbilitySystem.h"
#include "KineticCharacterController.h"
#include "SplineComponent.h"
#include "ECS.h"
#include <algorithm>

namespace Sanic {

// ============================================================================
// BASE ABILITY
// ============================================================================

Ability::Ability(const std::string& name)
    : name_(name)
{
    tags_.ownedTags.push_back("Ability");
}

bool Ability::canActivate(const AbilityContext& context) const {
    // Check state
    if (state_ != AbilityState::Ready) {
        return false;
    }
    
    // Check resource cost (if we have access to component, would check here)
    
    return true;
}

void Ability::activate(const AbilityContext& context) {
    if (!canActivate(context)) {
        return;
    }
    
    cachedContext_ = context;
    state_ = AbilityState::Active;
    activeTime_ = 0.0f;
    
    onActivate(context);
    
    if (activateCallback_) {
        activateCallback_(this);
    }
}

void Ability::tick(float deltaTime, const AbilityContext& context) {
    if (state_ != AbilityState::Active) {
        return;
    }
    
    activeTime_ += deltaTime;
    cachedContext_ = context;
    cachedContext_.deltaTime = deltaTime;
    
    onTick(deltaTime, context);
    
    // Auto-end if duration exceeded
    if (duration_ > 0.0f && activeTime_ >= duration_) {
        deactivate();
    }
}

void Ability::deactivate() {
    if (state_ != AbilityState::Active) {
        return;
    }
    
    onDeactivate();
    
    state_ = AbilityState::Cooldown;
    cooldownRemaining_ = cooldown_;
    
    if (deactivateCallback_) {
        deactivateCallback_(this);
    }
}

void Ability::cancel() {
    if (state_ != AbilityState::Active) {
        return;
    }
    
    onCancel();
    deactivate();
}

void Ability::forceEnd() {
    state_ = AbilityState::Ready;
    activeTime_ = 0.0f;
    cooldownRemaining_ = 0.0f;
}

void Ability::updateCooldown(float deltaTime) {
    if (state_ == AbilityState::Cooldown) {
        cooldownRemaining_ -= deltaTime;
        if (cooldownRemaining_ <= 0.0f) {
            cooldownRemaining_ = 0.0f;
            state_ = AbilityState::Ready;
        }
    }
}

// ============================================================================
// BOOST ABILITY
// ============================================================================

BoostAbility::BoostAbility()
    : Ability("Boost")
{
    cooldown_ = 2.0f;
    duration_ = 0.0f;  // Manual end
    
    tags_.ownedTags.push_back("Movement");
    tags_.ownedTags.push_back("Boost");
}

void BoostAbility::onActivate(const AbilityContext& context) {
    timer_ = boostDuration;
    
    if (context.controller) {
        // Apply velocity burst in facing direction
        glm::vec3 forward = context.controller->getForward();
        context.controller->applyImpulse(forward * boostForce);
        
        // Grant invincibility
        if (grantsInvincibility) {
            context.controller->setInvincible(boostDuration);
        }
        
        // Mark as boosting
        context.controller->getState().isBoosting = true;
    }
    
    // TODO: Spawn VFX, play sound
}

void BoostAbility::onTick(float deltaTime, const AbilityContext& context) {
    timer_ -= deltaTime;
    
    if (timer_ <= 0.0f) {
        deactivate();
    }
}

void BoostAbility::onDeactivate() {
    if (cachedContext_.controller) {
        cachedContext_.controller->getState().isBoosting = false;
    }
}

// ============================================================================
// SUPER JUMP ABILITY
// ============================================================================

SuperJumpAbility::SuperJumpAbility()
    : Ability("SuperJump")
{
    cooldown_ = 0.5f;
    duration_ = 0.0f;
    
    tags_.ownedTags.push_back("Movement");
    tags_.ownedTags.push_back("Jump");
}

void SuperJumpAbility::startCharge(const AbilityContext& context) {
    if (!canActivate(context)) return;
    
    if (requiresGround && context.controller) {
        if (!context.controller->isGrounded()) {
            return;
        }
    }
    
    isCharging_ = true;
    currentCharge_ = 0.0f;
    
    // Start charge animation
    // TODO: Play charge animation
}

void SuperJumpAbility::releaseCharge(const AbilityContext& context) {
    if (!isCharging_) return;
    
    isCharging_ = false;
    
    // Perform the jump
    if (context.controller) {
        float jumpForce = glm::mix(minJumpForce, maxJumpForce, currentCharge_);
        context.controller->superJump(jumpForce);
    }
    
    currentCharge_ = 0.0f;
    
    // Start cooldown
    state_ = AbilityState::Cooldown;
    cooldownRemaining_ = cooldown_;
    
    // TODO: Play launch animation and sound
}

void SuperJumpAbility::cancelCharge() {
    if (isCharging_) {
        isCharging_ = false;
        currentCharge_ = 0.0f;
    }
}

void SuperJumpAbility::onActivate(const AbilityContext& context) {
    startCharge(context);
}

void SuperJumpAbility::onTick(float deltaTime, const AbilityContext& context) {
    if (isCharging_) {
        currentCharge_ += deltaTime / chargeTime;
        currentCharge_ = glm::min(currentCharge_, 1.0f);
        
        // TODO: Update charge visual feedback
    }
}

void SuperJumpAbility::onDeactivate() {
    isCharging_ = false;
    currentCharge_ = 0.0f;
}

void SuperJumpAbility::onCancel() {
    cancelCharge();
}

// ============================================================================
// ZIPLINE ATTACH ABILITY
// ============================================================================

ZiplineAttachAbility::ZiplineAttachAbility()
    : Ability("ZiplineAttach")
{
    cooldown_ = 0.5f;
    duration_ = 0.0f;  // Lasts until player exits
    
    tags_.ownedTags.push_back("Movement");
    tags_.ownedTags.push_back("Spline");
}

void ZiplineAttachAbility::onActivate(const AbilityContext& context) {
    SplineComponent* nearestSpline = findNearestSpline(context);
    
    if (nearestSpline) {
        attachedSpline_ = nearestSpline;
        
        // Lock controller to spline
        if (context.controller) {
            float startDist = nearestSpline->findClosestDistance(context.controller->getPosition());
            context.controller->lockToSpline(nearestSpline, startDist);
        }
        
        // TODO: Play attach animation and sound
    } else {
        // No zipline found, cancel
        forceEnd();
    }
}

void ZiplineAttachAbility::onTick(float deltaTime, const AbilityContext& context) {
    // Check if still on spline
    if (context.controller && !context.controller->isLockedToSpline()) {
        deactivate();
    }
}

void ZiplineAttachAbility::onDeactivate() {
    attachedSpline_ = nullptr;
    
    if (cachedContext_.controller) {
        cachedContext_.controller->unlockFromSpline();
    }
}

SplineComponent* ZiplineAttachAbility::findNearestSpline(const AbilityContext& context) {
    if (!context.controller || !context.world) {
        return nullptr;
    }
    
    glm::vec3 position = context.controller->getPosition();
    
    // TODO: Query world for splines with the correct tag
    // This would involve querying the ECS for SplineComponent entities
    // For now, return nullptr - actual implementation would search the world
    
    return nullptr;
}

// ============================================================================
// DASH ABILITY
// ============================================================================

DashAbility::DashAbility()
    : Ability("Dash")
{
    cooldown_ = 1.0f;
    duration_ = 0.0f;
    
    tags_.ownedTags.push_back("Movement");
    tags_.ownedTags.push_back("Dash");
    tags_.blockedByTags.push_back("Stunned");
}

void DashAbility::onActivate(const AbilityContext& context) {
    if (!context.controller) {
        forceEnd();
        return;
    }
    
    // Check air dash limit
    if (!context.controller->isGrounded()) {
        if (!canDashInAir || airDashCount_ >= maxAirDashes) {
            forceEnd();
            return;
        }
        airDashCount_++;
    } else {
        airDashCount_ = 0;  // Reset on ground
    }
    
    timer_ = dashDuration;
    startPosition_ = context.controller->getPosition();
    
    // Determine dash direction
    if (glm::length(context.aimDirection) > 0.001f) {
        dashDirection_ = glm::normalize(context.aimDirection);
    } else {
        dashDirection_ = context.controller->getForward();
    }
    
    // Calculate dash velocity
    float dashSpeed = dashDistance / dashDuration;
    context.controller->setVelocity(dashDirection_ * dashSpeed);
    
    // Grant invincibility
    if (grantsInvincibility) {
        context.controller->setInvincible(dashDuration);
    }
    
    // TODO: Play dash VFX and sound
}

void DashAbility::onTick(float deltaTime, const AbilityContext& context) {
    timer_ -= deltaTime;
    
    if (timer_ <= 0.0f) {
        deactivate();
    }
}

void DashAbility::onDeactivate() {
    // Reduce velocity after dash
    if (cachedContext_.controller) {
        glm::vec3 velocity = cachedContext_.controller->getVelocity();
        cachedContext_.controller->setVelocity(velocity * 0.5f);
    }
}

// ============================================================================
// GROUND POUND ABILITY
// ============================================================================

GroundPoundAbility::GroundPoundAbility()
    : Ability("GroundPound")
{
    cooldown_ = 1.5f;
    duration_ = 0.0f;
    
    tags_.ownedTags.push_back("Movement");
    tags_.ownedTags.push_back("Attack");
    tags_.ownedTags.push_back("GroundPound");
}

void GroundPoundAbility::onActivate(const AbilityContext& context) {
    if (!context.controller) {
        forceEnd();
        return;
    }
    
    // Must be airborne
    if (requiresAirborne && context.controller->isGrounded()) {
        forceEnd();
        return;
    }
    
    hasLanded_ = false;
    
    // Set downward velocity
    glm::vec3 down = -context.controller->getUp();
    context.controller->setVelocity(down * descendSpeed);
    
    // TODO: Play pound animation
}

void GroundPoundAbility::onTick(float deltaTime, const AbilityContext& context) {
    if (!context.controller) {
        deactivate();
        return;
    }
    
    // Maintain downward velocity
    glm::vec3 down = -context.controller->getUp();
    context.controller->setVelocity(down * descendSpeed);
    
    // Check if landed
    if (context.controller->isGrounded() && !hasLanded_) {
        hasLanded_ = true;
        
        // Impact!
        glm::vec3 impactPos = context.controller->getPosition();
        
        // TODO: Spawn impact VFX
        // TODO: Apply damage/force to nearby objects
        // TODO: Camera shake
        
        // Query for nearby destructible objects and apply force
        // This would involve querying the physics system
        
        deactivate();
    }
}

void GroundPoundAbility::onDeactivate() {
    hasLanded_ = false;
}

// ============================================================================
// ABILITY COMPONENT
// ============================================================================

AbilityComponent::AbilityComponent() = default;

uint32_t AbilityComponent::addAbility(std::unique_ptr<Ability> ability) {
    uint32_t index = static_cast<uint32_t>(abilities_.size());
    nameToIndex_[ability->getName()] = index;
    abilities_.push_back(std::move(ability));
    return index;
}

void AbilityComponent::removeAbility(uint32_t index) {
    if (index >= abilities_.size()) return;
    
    // Remove from name map
    const std::string& name = abilities_[index]->getName();
    nameToIndex_.erase(name);
    
    // Remove ability
    abilities_.erase(abilities_.begin() + index);
    
    // Update indices in map
    nameToIndex_.clear();
    for (size_t i = 0; i < abilities_.size(); i++) {
        nameToIndex_[abilities_[i]->getName()] = static_cast<uint32_t>(i);
    }
}

Ability* AbilityComponent::getAbility(uint32_t index) {
    if (index >= abilities_.size()) return nullptr;
    return abilities_[index].get();
}

const Ability* AbilityComponent::getAbility(uint32_t index) const {
    if (index >= abilities_.size()) return nullptr;
    return abilities_[index].get();
}

Ability* AbilityComponent::getAbilityByName(const std::string& name) {
    auto it = nameToIndex_.find(name);
    if (it == nameToIndex_.end()) return nullptr;
    return getAbility(it->second);
}

bool AbilityComponent::tryActivate(uint32_t index, const AbilityContext& context) {
    Ability* ability = getAbility(index);
    if (!ability) return false;
    
    // Check if blocked by active abilities
    for (const auto& blockTag : ability->getTags().blockedByTags) {
        if (hasBlockingTag(blockTag)) {
            return false;
        }
    }
    
    // Check resource cost
    if (ability->getResourceCost() > currentResource_) {
        return false;
    }
    
    if (!ability->canActivate(context)) {
        return false;
    }
    
    // Cancel abilities with cancel tags
    for (const auto& cancelTag : ability->getTags().cancelTags) {
        cancelAbilitiesWithTag(cancelTag);
    }
    
    // Consume resource
    consumeResource(ability->getResourceCost());
    
    // Activate
    ability->activate(context);
    
    return true;
}

bool AbilityComponent::tryActivateByName(const std::string& name, const AbilityContext& context) {
    auto it = nameToIndex_.find(name);
    if (it == nameToIndex_.end()) return false;
    return tryActivate(it->second, context);
}

void AbilityComponent::cancelAbility(uint32_t index) {
    Ability* ability = getAbility(index);
    if (ability && ability->isActive()) {
        ability->cancel();
    }
}

void AbilityComponent::cancelAllAbilities() {
    for (auto& ability : abilities_) {
        if (ability->isActive()) {
            ability->cancel();
        }
    }
}

void AbilityComponent::cancelAbilitiesWithTag(const std::string& tag) {
    for (auto& ability : abilities_) {
        if (ability->isActive()) {
            const auto& tags = ability->getTags().ownedTags;
            if (std::find(tags.begin(), tags.end(), tag) != tags.end()) {
                ability->cancel();
            }
        }
    }
}

void AbilityComponent::update(float deltaTime, const AbilityContext& context) {
    for (auto& ability : abilities_) {
        // Update cooldowns
        ability->updateCooldown(deltaTime);
        
        // Tick active abilities
        if (ability->isActive()) {
            ability->tick(deltaTime, context);
        }
    }
}

bool AbilityComponent::hasBlockingTag(const std::string& tag) const {
    for (const auto& ability : abilities_) {
        if (ability->isActive()) {
            const auto& blockTags = ability->getTags().blockTags;
            if (std::find(blockTags.begin(), blockTags.end(), tag) != blockTags.end()) {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> AbilityComponent::getActiveAbilityTags() const {
    std::vector<std::string> result;
    for (const auto& ability : abilities_) {
        if (ability->isActive()) {
            const auto& tags = ability->getTags().ownedTags;
            result.insert(result.end(), tags.begin(), tags.end());
        }
    }
    return result;
}

bool AbilityComponent::consumeResource(float amount) {
    if (amount > currentResource_) {
        return false;
    }
    currentResource_ -= amount;
    return true;
}

} // namespace Sanic
