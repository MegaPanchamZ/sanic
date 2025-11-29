#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <memory>
#include <vector>
#include "GameObject.h"

// Layer constants
namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};

class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();

    void update(float deltaTime);
    void updateGameObjects(std::vector<GameObject>& gameObjects);
    
    JPH::BodyInterface& getBodyInterface();
    const JPH::BodyInterface& getBodyInterface() const;
    
    JPH::PhysicsSystem& getPhysicsSystem() { return physicsSystem; }

private:
    // Jolt Physics objects
    JPH::TempAllocatorImpl* tempAllocator = nullptr;
    JPH::JobSystemThreadPool* jobSystem = nullptr;
    JPH::PhysicsSystem physicsSystem;
    
    // Layer interfaces
    class BPLayerInterfaceImpl;
    class ObjectVsBroadPhaseLayerFilterImpl;
    class ObjectLayerPairFilterImpl;
    
    std::unique_ptr<BPLayerInterfaceImpl> bpLayerInterface;
    std::unique_ptr<ObjectVsBroadPhaseLayerFilterImpl> objectVsBroadPhaseLayerFilter;
    std::unique_ptr<ObjectLayerPairFilterImpl> objectLayerPairFilter;
};
