/**
 * ECS.cpp
 * 
 * Implementation of the Entity Component System.
 */

#include "ECS.h"
#include <algorithm>
#include <iostream>

namespace Sanic {

// ============================================================================
// WORLD IMPLEMENTATION
// ============================================================================

World::~World() {
    // Shutdown all systems
    for (auto& system : systems_) {
        system->shutdown(*this);
    }
    systems_.clear();
}

Entity World::createEntity() {
    Entity entity;
    
    if (!availableEntities_.empty()) {
        entity = availableEntities_.front();
        availableEntities_.pop();
    } else {
        entity = nextEntityId_++;
    }
    
    livingEntities_.insert(entity);
    signatures_[entity] = ComponentSignature{};
    
    return entity;
}

Entity World::createEntity(const std::string& name) {
    Entity entity = createEntity();
    addComponent<Name>(entity, {name, ""});
    addComponent<Transform>(entity);
    addComponent<Active>(entity);
    return entity;
}

void World::destroyEntity(Entity entity) {
    if (livingEntities_.find(entity) == livingEntities_.end()) {
        return;
    }
    
    // Remove from parent if has transform
    if (hasComponent<Transform>(entity)) {
        Transform& transform = getComponent<Transform>(entity);
        if (transform.parent != INVALID_ENTITY && isValid(transform.parent)) {
            Transform& parentTransform = getComponent<Transform>(transform.parent);
            auto& children = parentTransform.children;
            children.erase(std::remove(children.begin(), children.end(), entity), children.end());
        }
        
        // Destroy children
        for (Entity child : transform.children) {
            destroyEntity(child);
        }
    }
    
    // Notify all component arrays
    for (auto& [typeId, array] : componentArrays_) {
        array->entityDestroyed(entity);
    }
    
    signatures_.erase(entity);
    livingEntities_.erase(entity);
    availableEntities_.push(entity);
}

bool World::isValid(Entity entity) const {
    return livingEntities_.find(entity) != livingEntities_.end();
}

void World::update(float deltaTime) {
    // Process events
    eventBus_.processEvents();
    
    // Update all systems
    for (auto& system : systems_) {
        system->update(*this, deltaTime);
    }
    
    // Process pending destruction
    for (Entity entity : pendingDestruction_) {
        destroyEntity(entity);
    }
    pendingDestruction_.clear();
}

void World::fixedUpdate(float fixedDeltaTime) {
    for (auto& system : systems_) {
        system->fixedUpdate(*this, fixedDeltaTime);
    }
}

void World::lateUpdate(float deltaTime) {
    for (auto& system : systems_) {
        system->lateUpdate(*this, deltaTime);
    }
}

std::vector<Entity> World::getEntitiesWithSignature(ComponentSignature signature) const {
    std::vector<Entity> result;
    
    for (Entity entity : livingEntities_) {
        auto it = signatures_.find(entity);
        if (it != signatures_.end()) {
            // Check if entity has all required components
            if ((it->second & signature) == signature) {
                result.push_back(entity);
            }
        }
    }
    
    return result;
}

Entity World::findEntity(const std::string& name) const {
    ComponentTypeId nameTypeId = ComponentRegistry::getInstance().getTypeId<Name>();
    auto it = componentArrays_.find(nameTypeId);
    if (it == componentArrays_.end()) return INVALID_ENTITY;
    
    auto* nameArray = static_cast<ComponentArray<Name>*>(it->second.get());
    for (size_t i = 0; i < nameArray->size(); ++i) {
        if ((*nameArray)[i].name == name) {
            return nameArray->getEntity(i);
        }
    }
    
    return INVALID_ENTITY;
}

std::vector<Entity> World::findEntitiesWithTag(const std::string& tag) const {
    std::vector<Entity> result;
    
    ComponentTypeId nameTypeId = ComponentRegistry::getInstance().getTypeId<Name>();
    auto it = componentArrays_.find(nameTypeId);
    if (it == componentArrays_.end()) return result;
    
    auto* nameArray = static_cast<ComponentArray<Name>*>(it->second.get());
    for (size_t i = 0; i < nameArray->size(); ++i) {
        if ((*nameArray)[i].tag == tag) {
            result.push_back(nameArray->getEntity(i));
        }
    }
    
    return result;
}

Entity World::instantiate(Entity prefab) {
    if (!isValid(prefab)) return INVALID_ENTITY;
    
    Entity instance = createEntity();
    
    // Copy all components from prefab
    ComponentSignature prefabSig = getSignature(prefab);
    for (auto& [typeId, array] : componentArrays_) {
        if (prefabSig.test(typeId)) {
            array->copyComponent(prefab, instance);
            signatures_[instance].set(typeId);
        }
    }
    
    return instance;
}

Entity World::instantiate(Entity prefab, const glm::vec3& position, const glm::quat& rotation) {
    Entity instance = instantiate(prefab);
    if (instance != INVALID_ENTITY && hasComponent<Transform>(instance)) {
        Transform& transform = getComponent<Transform>(instance);
        transform.position = position;
        transform.rotation = rotation;
    }
    return instance;
}

void World::clear() {
    // Destroy all entities
    std::vector<Entity> toDestroy(livingEntities_.begin(), livingEntities_.end());
    for (Entity entity : toDestroy) {
        destroyEntity(entity);
    }
    
    // Reset entity counter
    while (!availableEntities_.empty()) {
        availableEntities_.pop();
    }
    nextEntityId_ = 0;
}

// ============================================================================
// TRANSFORM SYSTEM
// ============================================================================

void TransformSystem::update(World& world, float deltaTime) {
    updateWorldMatrices(world);
}

void TransformSystem::updateWorldMatrices(World& world) {
    worldMatrices_.clear();
    
    // Get all entities with transforms
    auto query = world.query<Transform>();
    
    // First pass: find root entities (no parent)
    std::vector<Entity> roots;
    for (auto [entity, transform] : query) {
        if (transform.parent == INVALID_ENTITY) {
            roots.push_back(entity);
        }
    }
    
    // Recursive update function
    std::function<void(Entity, const glm::mat4&)> updateEntity = [&](Entity entity, const glm::mat4& parentMatrix) {
        Transform& transform = world.getComponent<Transform>(entity);
        glm::mat4 worldMatrix = parentMatrix * transform.getLocalMatrix();
        worldMatrices_[entity] = worldMatrix;
        
        for (Entity child : transform.children) {
            if (world.isValid(child)) {
                updateEntity(child, worldMatrix);
            }
        }
    };
    
    // Update from roots
    for (Entity root : roots) {
        updateEntity(root, glm::mat4(1.0f));
    }
}

glm::mat4 TransformSystem::getWorldMatrix(World& world, Entity entity) {
    auto it = worldMatrices_.find(entity);
    if (it != worldMatrices_.end()) {
        return it->second;
    }
    
    if (world.hasComponent<Transform>(entity)) {
        return world.getComponent<Transform>(entity).getLocalMatrix();
    }
    
    return glm::mat4(1.0f);
}

void TransformSystem::setParent(World& world, Entity child, Entity parent) {
    if (!world.hasComponent<Transform>(child)) return;
    
    Transform& childTransform = world.getComponent<Transform>(child);
    
    // Remove from old parent
    if (childTransform.parent != INVALID_ENTITY && world.isValid(childTransform.parent)) {
        Transform& oldParent = world.getComponent<Transform>(childTransform.parent);
        auto& children = oldParent.children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }
    
    // Set new parent
    childTransform.parent = parent;
    
    if (parent != INVALID_ENTITY && world.hasComponent<Transform>(parent)) {
        Transform& parentTransform = world.getComponent<Transform>(parent);
        parentTransform.children.push_back(child);
    }
}

void TransformSystem::removeFromParent(World& world, Entity child) {
    setParent(world, child, INVALID_ENTITY);
}

// ============================================================================
// MOVEMENT SYSTEM
// ============================================================================

void MovementSystem::update(World& world, float deltaTime) {
    for (auto [entity, transform, velocity] : world.query<Transform, Velocity>()) {
        // Apply linear velocity
        transform.position += velocity.linear * deltaTime;
        
        // Apply angular velocity (as euler angles per second)
        if (glm::length(velocity.angular) > 0.0001f) {
            glm::quat rotDelta = glm::quat(velocity.angular * deltaTime);
            transform.rotation = rotDelta * transform.rotation;
            transform.rotation = glm::normalize(transform.rotation);
        }
    }
}

} // namespace Sanic
