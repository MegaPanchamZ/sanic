/**
 * AsyncPhysics.cpp
 * 
 * Implementation of async physics simulation.
 */

#include "AsyncPhysics.h"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <iostream>
#include <cstdarg>
#include <chrono>

// Jolt callbacks
static void TraceImpl(const char *inFMT, ...) {
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);
    // Could log to console
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, uint32_t inLine) {
    std::cerr << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage ? inMessage : "") << std::endl;
    return true;
}
#endif

// Layer definitions
namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer DEBRIS = 2;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 3;
    
    static constexpr JPH::BroadPhaseLayer BP_NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer BP_MOVING(1);
    static constexpr uint32_t NUM_BP_LAYERS = 2;
}

// Broad phase layer interface
class AsyncPhysics::BPLayerInterface : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterface() {
        objectToBroadPhase_[Layers::NON_MOVING] = Layers::BP_NON_MOVING;
        objectToBroadPhase_[Layers::MOVING] = Layers::BP_MOVING;
        objectToBroadPhase_[Layers::DEBRIS] = Layers::BP_MOVING;
    }
    
    virtual uint32_t GetNumBroadPhaseLayers() const override {
        return Layers::NUM_BP_LAYERS;
    }
    
    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        return objectToBroadPhase_[inLayer];
    }
    
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
            case (JPH::BroadPhaseLayer::Type)Layers::BP_NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)Layers::BP_MOVING: return "MOVING";
            default: return "INVALID";
        }
    }
#endif
    
private:
    JPH::BroadPhaseLayer objectToBroadPhase_[Layers::NUM_LAYERS];
};

class AsyncPhysics::ObjectVsBroadPhaseLayerFilter : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case Layers::NON_MOVING:
                return inLayer2 == Layers::BP_MOVING;
            case Layers::MOVING:
            case Layers::DEBRIS:
                return true;
            default:
                return false;
        }
    }
};

class AsyncPhysics::ObjectLayerPairFilter : public JPH::ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        switch (inObject1) {
            case Layers::NON_MOVING:
                return inObject2 == Layers::MOVING || inObject2 == Layers::DEBRIS;
            case Layers::MOVING:
                return true;
            case Layers::DEBRIS:
                return inObject2 != Layers::DEBRIS;  // Debris doesn't collide with debris
            default:
                return false;
        }
    }
};

class AsyncPhysics::ContactListener : public JPH::ContactListener {
public:
    ContactListener(AsyncPhysics* owner) : owner_(owner) {}
    
    virtual void OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2,
                                const JPH::ContactManifold &inManifold,
                                JPH::ContactSettings &ioSettings) override {
        // Could queue collision events here
    }
    
private:
    AsyncPhysics* owner_;
};

AsyncPhysics::AsyncPhysics() = default;

AsyncPhysics::~AsyncPhysics() {
    shutdown();
}

bool AsyncPhysics::initialize(const AsyncPhysicsConfig& config) {
    if (initialized_) return true;
    
    config_ = config;
    
    // Initialize Jolt
    JPH::RegisterDefaultAllocator();
    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)
    
    if (!JPH::Factory::sInstance) {
        JPH::Factory::sInstance = new JPH::Factory();
    }
    
    JPH::RegisterTypes();
    
    // Create allocators
    tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(config_.tempAllocatorSize);
    
    uint32_t numThreads = config_.numPhysicsThreads;
    if (numThreads == 0) {
        numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    }
    
    if (config_.useJobSystem) {
        jobSystem_ = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers,
            static_cast<int>(numThreads)
        );
    }
    
    // Create layer interfaces
    bpLayerInterface_ = std::make_unique<BPLayerInterface>();
    broadPhaseFilter_ = std::make_unique<ObjectVsBroadPhaseLayerFilter>();
    layerPairFilter_ = std::make_unique<ObjectLayerPairFilter>();
    contactListener_ = std::make_unique<ContactListener>(this);
    
    // Create physics system
    physicsSystem_ = std::make_unique<JPH::PhysicsSystem>();
    physicsSystem_->Init(
        config_.maxBodies,
        0,  // num body mutexes (0 = default)
        config_.maxBodyPairs,
        config_.maxContactConstraints,
        *bpLayerInterface_,
        *broadPhaseFilter_,
        *layerPairFilter_
    );
    
    physicsSystem_->SetContactListener(contactListener_.get());
    
    // Set physics settings
    JPH::PhysicsSettings settings;
    settings.mNumVelocitySteps = 10;
    settings.mNumPositionSteps = 2;
    physicsSystem_->SetPhysicsSettings(settings);
    
    initialized_ = true;
    return true;
}

void AsyncPhysics::shutdown() {
    stopSimulation();
    
    if (physicsSystem_) {
        // Remove all bodies
        JPH::BodyInterface& bi = physicsSystem_->GetBodyInterface();
        for (auto& obj : objects_) {
            if (!obj.bodyId.IsInvalid()) {
                bi.RemoveBody(obj.bodyId);
                bi.DestroyBody(obj.bodyId);
            }
        }
        objects_.clear();
    }
    
    physicsSystem_.reset();
    contactListener_.reset();
    layerPairFilter_.reset();
    broadPhaseFilter_.reset();
    bpLayerInterface_.reset();
    jobSystem_.reset();
    tempAllocator_.reset();
    
    if (JPH::Factory::sInstance) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
    
    initialized_ = false;
}

void AsyncPhysics::startSimulation() {
    if (running_) return;
    
    running_ = true;
    paused_ = false;
    physicsThread_ = std::thread(&AsyncPhysics::physicsThreadFunc, this);
}

void AsyncPhysics::stopSimulation() {
    if (!running_) return;
    
    {
        std::lock_guard<std::mutex> lock(physicsMutex_);
        running_ = false;
    }
    physicsCV_.notify_all();
    
    if (physicsThread_.joinable()) {
        physicsThread_.join();
    }
}

void AsyncPhysics::physicsThreadFunc() {
    auto lastTime = std::chrono::high_resolution_clock::now();
    
    while (running_) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        if (!paused_) {
            // Process commands
            processCommands();
            
            // Step physics
            auto stepStart = std::chrono::high_resolution_clock::now();
            stepPhysics(deltaTime);
            auto stepEnd = std::chrono::high_resolution_clock::now();
            
            lastStepTime_ = std::chrono::duration<float, std::milli>(stepEnd - stepStart).count();
            
            // Swap buffers after step
            swapBuffers();
            
            // Fire callbacks
            if (config_.enableAsyncCallbacks) {
                fireCallbacks();
            }
        }
        
        // Sleep to hit target rate
        float targetDelta = config_.fixedDeltaTime;
        auto elapsed = std::chrono::high_resolution_clock::now() - lastTime;
        float elapsedSeconds = std::chrono::duration<float>(elapsed).count();
        
        if (elapsedSeconds < targetDelta) {
            std::this_thread::sleep_for(
                std::chrono::duration<float>(targetDelta - elapsedSeconds)
            );
        }
    }
}

void AsyncPhysics::stepPhysics(float deltaTime) {
    if (!physicsSystem_) return;
    
    // Fixed timestep with accumulator
    accumulator_ += deltaTime;
    
    substepsThisFrame_ = 0;
    while (accumulator_ >= config_.fixedDeltaTime && substepsThisFrame_ < config_.maxSubSteps) {
        physicsSystem_->Update(
            config_.fixedDeltaTime,
            1,  // collision steps
            tempAllocator_.get(),
            jobSystem_.get()
        );
        
        accumulator_ -= config_.fixedDeltaTime;
        substepsThisFrame_++;
    }
    
    // Clamp accumulator to prevent spiral of death
    if (accumulator_ > config_.fixedDeltaTime * 2.0f) {
        accumulator_ = config_.fixedDeltaTime * 2.0f;
    }
    
    // Calculate interpolation alpha
    interpolationAlpha_ = accumulator_ / config_.fixedDeltaTime;
}

void AsyncPhysics::swapBuffers() {
    std::lock_guard<std::mutex> lock(objectsMutex_);
    
    JPH::BodyInterface& bi = physicsSystem_->GetBodyInterface();
    
    for (auto& obj : objects_) {
        if (!obj.isActive || obj.bodyId.IsInvalid()) continue;
        
        // Move current to previous
        obj.prev = obj.curr;
        
        // Read new current from physics
        JPH::RVec3 pos = bi.GetCenterOfMassPosition(obj.bodyId);
        JPH::Quat rot = bi.GetRotation(obj.bodyId);
        JPH::Vec3 vel = bi.GetLinearVelocity(obj.bodyId);
        JPH::Vec3 angVel = bi.GetAngularVelocity(obj.bodyId);
        
        obj.curr.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
        obj.curr.rotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
        obj.curr.velocity = glm::vec3(vel.GetX(), vel.GetY(), vel.GetZ());
        obj.curr.angularVelocity = glm::vec3(angVel.GetX(), angVel.GetY(), angVel.GetZ());
        
        obj.needsSync = true;
    }
    
    // Swap read/write buffer indices
    uint32_t temp = readBuffer_;
    readBuffer_ = writeBuffer_.load();
    writeBuffer_ = temp;
}

void AsyncPhysics::update(float deltaTime) {
    // Compute interpolation on game thread
    if (config_.enableInterpolation) {
        computeInterpolation(interpolationAlpha_);
    }
}

void AsyncPhysics::computeInterpolation(float alpha) {
    std::lock_guard<std::mutex> lock(objectsMutex_);
    
    for (auto& obj : objects_) {
        if (!obj.isActive || !obj.needsSync) continue;
        
        // Interpolate between prev and curr
        obj.interpolated.position = glm::mix(obj.prev.position, obj.curr.position, alpha);
        obj.interpolated.rotation = glm::slerp(obj.prev.rotation, obj.curr.rotation, alpha);
        
        // Build matrix
        glm::mat4 T = glm::translate(glm::mat4(1.0f), obj.interpolated.position);
        glm::mat4 R = glm::mat4_cast(obj.interpolated.rotation);
        obj.interpolated.matrix = T * R;
    }
}

void AsyncPhysics::processCommands() {
    std::lock_guard<std::mutex> lock(commandMutex_);
    
    while (!commandQueue_.empty()) {
        auto& cmd = commandQueue_.front();
        cmd(*physicsSystem_);
        commandQueue_.pop();
    }
}

void AsyncPhysics::fireCallbacks() {
    if (!transformCallback_) return;
    
    std::lock_guard<std::mutex> lock(objectsMutex_);
    
    for (auto& obj : objects_) {
        if (!obj.isActive || !obj.needsSync) continue;
        transformCallback_(obj.gameObjectId, obj.curr);
        obj.needsSync = false;
    }
}

uint32_t AsyncPhysics::registerObject(JPH::BodyID bodyId, uint32_t gameObjectId) {
    std::lock_guard<std::mutex> lock(objectsMutex_);
    
    uint32_t index;
    if (!freeIndices_.empty()) {
        index = freeIndices_.back();
        freeIndices_.pop_back();
    } else {
        index = static_cast<uint32_t>(objects_.size());
        objects_.emplace_back();
    }
    
    PhysicsObjectState& obj = objects_[index];
    obj.bodyId = bodyId;
    obj.gameObjectId = gameObjectId;
    obj.isActive = true;
    obj.needsSync = true;
    obj.isDynamic = physicsSystem_->GetBodyInterface().GetMotionType(bodyId) == JPH::EMotionType::Dynamic;
    
    // Initialize transforms from current body state
    JPH::BodyInterface& bi = physicsSystem_->GetBodyInterface();
    JPH::RVec3 pos = bi.GetCenterOfMassPosition(bodyId);
    JPH::Quat rot = bi.GetRotation(bodyId);
    
    obj.curr.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
    obj.curr.rotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
    obj.curr.velocity = glm::vec3(0.0f);
    obj.curr.angularVelocity = glm::vec3(0.0f);
    obj.prev = obj.curr;
    
    return index;
}

void AsyncPhysics::unregisterObject(uint32_t objectIndex) {
    std::lock_guard<std::mutex> lock(objectsMutex_);
    
    if (objectIndex >= objects_.size()) return;
    
    objects_[objectIndex].isActive = false;
    objects_[objectIndex].bodyId = JPH::BodyID();
    freeIndices_.push_back(objectIndex);
}

InterpolatedTransform AsyncPhysics::getInterpolatedTransform(uint32_t objectIndex) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(objectsMutex_));
    
    if (objectIndex >= objects_.size() || !objects_[objectIndex].isActive) {
        return InterpolatedTransform{};
    }
    
    return objects_[objectIndex].interpolated;
}

void AsyncPhysics::getInterpolatedTransforms(std::vector<InterpolatedTransform>& outTransforms) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(objectsMutex_));
    
    outTransforms.clear();
    outTransforms.reserve(objects_.size());
    
    for (const auto& obj : objects_) {
        if (obj.isActive) {
            outTransforms.push_back(obj.interpolated);
        }
    }
}

void AsyncPhysics::queueCommand(std::function<void(JPH::PhysicsSystem&)> command) {
    std::lock_guard<std::mutex> lock(commandMutex_);
    commandQueue_.push(std::move(command));
}

JPH::BodyInterface& AsyncPhysics::getBodyInterface() {
    return physicsSystem_->GetBodyInterface();
}

AsyncPhysics::Stats AsyncPhysics::getStats() const {
    Stats stats;
    stats.lastStepTime = lastStepTime_;
    stats.substepsThisFrame = substepsThisFrame_;
    stats.activeBodyCount = physicsSystem_ ? physicsSystem_->GetNumActiveBodies(JPH::EBodyType::RigidBody) : 0;
    stats.interpolationAlpha = interpolationAlpha_;
    stats.isSimulating = running_;
    return stats;
}

