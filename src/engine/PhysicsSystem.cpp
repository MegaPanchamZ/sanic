#include "PhysicsSystem.h"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <iostream>
#include <cstdarg>
#include <algorithm>

// Callback for traces
static void TraceImpl(const char *inFMT, ...) {
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);
    std::cout << buffer << std::endl;
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, uint32_t inLine) {
    std::cout << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage ? inMessage : "") << std::endl;
    return true;
};
#endif

// Layer Interface Implementations
namespace Layers {
    static constexpr JPH::BroadPhaseLayer BP_NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer BP_MOVING(1);
    static constexpr uint32_t NUM_BROAD_PHASE_LAYERS = 2;
};

class PhysicsSystem::BPLayerInterfaceImpl : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        objectToBroadPhase[Layers::NON_MOVING] = Layers::BP_NON_MOVING;
        objectToBroadPhase[Layers::MOVING] = Layers::BP_MOVING;
    }

    virtual uint32_t GetNumBroadPhaseLayers() const override {
        return Layers::NUM_BROAD_PHASE_LAYERS;
    }

    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return objectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
            case (JPH::BroadPhaseLayer::Type)Layers::BP_NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)Layers::BP_MOVING: return "MOVING";
            default: JPH_ASSERT(false); return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer objectToBroadPhase[Layers::NUM_LAYERS];
};

class PhysicsSystem::ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case Layers::NON_MOVING:
                return inLayer2 == Layers::BP_MOVING;
            case Layers::MOVING:
                return true;
            default:
                JPH_ASSERT(false);
                return false;
        }
    }
};

class PhysicsSystem::ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        switch (inObject1) {
            case Layers::NON_MOVING:
                return inObject2 == Layers::MOVING;
            case Layers::MOVING:
                return true;
            default:
                JPH_ASSERT(false);
                return false;
        }
    }
};

PhysicsSystem::PhysicsSystem() {
    // Register allocation hook
    JPH::RegisterDefaultAllocator();

    // Install callbacks
    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

    // Create a factory
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all Jolt physics types
    JPH::RegisterTypes();

    // Init temp allocator
    tempAllocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);

    // Init job system - use single thread for debugging
    unsigned int numThreads = 1;  // Single-threaded for debugging
    std::cout << "Physics Job System: " << numThreads << " thread(s)" << std::endl;
    jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);

    // Create layer interfaces
    bpLayerInterface = std::make_unique<BPLayerInterfaceImpl>();
    objectVsBroadPhaseLayerFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
    objectLayerPairFilter = std::make_unique<ObjectLayerPairFilterImpl>();

    // Init physics system
    const uint32_t cMaxBodies = 1024;
    const uint32_t cNumBodyMutexes = 0;
    const uint32_t cMaxBodyPairs = 1024;
    const uint32_t cMaxContactConstraints = 1024;

    physicsSystem.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, 
                       *bpLayerInterface, *objectVsBroadPhaseLayerFilter, *objectLayerPairFilter);
}

PhysicsSystem::~PhysicsSystem() {
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    
    delete jobSystem;
    delete tempAllocator;
}

void PhysicsSystem::update(float deltaTime) {
    // Skip update with very small delta time (can cause numerical issues)
    // Minimum is 1/240 seconds (4.16ms) to avoid instability
    const float minDeltaTime = 1.0f / 240.0f;
    if (deltaTime < minDeltaTime) {
        return;  // Skip this update, accumulate time for next frame
    }
    
    // Clamp delta time to avoid physics explosion with large time steps
    const float maxDeltaTime = 1.0f / 30.0f;  // Max ~33ms per step
    deltaTime = std::min(deltaTime, maxDeltaTime);
    
    const int cCollisionSteps = 1;
    try {
        physicsSystem.Update(deltaTime, cCollisionSteps, tempAllocator, jobSystem);
    } catch (const std::exception& e) {
        std::cerr << "Physics Update Exception: " << e.what() << std::endl;
        throw;
    } catch (...) {
        std::cerr << "Physics Update Unknown Exception!" << std::endl;
        throw;
    }
}

void PhysicsSystem::updateGameObjects(std::vector<GameObject>& gameObjects) {
    JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
    
    for (auto& obj : gameObjects) {
        if (!obj.bodyID.IsInvalid()) {
            // Only update dynamic bodies - static and kinematic bodies don't move from physics
            JPH::EMotionType motionType = bodyInterface.GetMotionType(obj.bodyID);
            if (motionType != JPH::EMotionType::Dynamic) {
                continue;  // Skip static and kinematic bodies
            }
            
            JPH::RVec3 position = bodyInterface.GetCenterOfMassPosition(obj.bodyID);
            JPH::Quat rotation = bodyInterface.GetRotation(obj.bodyID);
            
            // Update GameObject transform
            // Assuming glm::mat4 transform
            // We need to convert JPH types to GLM
            glm::vec3 pos(position.GetX(), position.GetY(), position.GetZ());
            glm::quat rot(rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ());
            
            glm::mat4 model = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
            // Scale? We don't have scale in Jolt body usually, or it's baked in shape.
            // We should preserve scale from original transform if possible, or assume 1.
            // For now, overwrite transform.
            obj.transform = model;
        }
    }
}

JPH::BodyInterface& PhysicsSystem::getBodyInterface() {
    return physicsSystem.GetBodyInterface();
}

const JPH::BodyInterface& PhysicsSystem::getBodyInterface() const {
    return physicsSystem.GetBodyInterface();
}
