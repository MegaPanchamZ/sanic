/**
 * AsyncPhysics.h
 * 
 * Asynchronous physics simulation running on dedicated thread.
 * Implements fixed timestep with visual interpolation.
 * 
 * Key features:
 * - Dedicated physics thread with job system
 * - Fixed timestep simulation (default 60Hz)
 * - Visual interpolation for smooth rendering
 * - Double-buffered transform output
 * - Async callbacks for gameplay logic
 */

#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <functional>
#include <queue>

// Transform state for interpolation
struct PhysicsTransform {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 velocity;
    glm::vec3 angularVelocity;
};

// Interpolated transform for rendering
struct InterpolatedTransform {
    glm::mat4 matrix;
    glm::vec3 position;
    glm::quat rotation;
};

// Physics object state
struct PhysicsObjectState {
    JPH::BodyID bodyId;
    uint32_t gameObjectId;
    
    // Double-buffered transforms
    PhysicsTransform prev;
    PhysicsTransform curr;
    
    // Interpolated for rendering
    InterpolatedTransform interpolated;
    
    // Flags
    bool isDynamic;
    bool needsSync;
    bool isActive;
};

// Async callback for physics events
using PhysicsCallback = std::function<void(uint32_t objectId, const PhysicsTransform& transform)>;

// Physics configuration
struct AsyncPhysicsConfig {
    // Timestep
    float fixedDeltaTime = 1.0f / 60.0f;     // 60Hz physics
    uint32_t maxSubSteps = 4;                 // Max substeps per frame
    
    // Threading
    uint32_t numPhysicsThreads = 0;           // 0 = auto (hardware_concurrency - 1)
    bool useJobSystem = true;
    
    // Memory
    uint32_t tempAllocatorSize = 16 * 1024 * 1024;  // 16 MB
    uint32_t maxBodies = 65536;
    uint32_t maxBodyPairs = 65536;
    uint32_t maxContactConstraints = 10240;
    
    // Interpolation
    bool enableInterpolation = true;
    float interpolationSmoothing = 0.9f;
    
    // Callbacks
    bool enableAsyncCallbacks = true;
};

class AsyncPhysics {
public:
    AsyncPhysics();
    ~AsyncPhysics();
    
    // Non-copyable
    AsyncPhysics(const AsyncPhysics&) = delete;
    AsyncPhysics& operator=(const AsyncPhysics&) = delete;
    
    /**
     * Initialize physics system
     */
    bool initialize(const AsyncPhysicsConfig& config = {});
    
    /**
     * Shutdown and cleanup
     */
    void shutdown();
    
    /**
     * Start async physics thread
     */
    void startSimulation();
    
    /**
     * Stop async physics thread
     */
    void stopSimulation();
    
    /**
     * Update from game thread - handles timing and interpolation
     * @param deltaTime Real time since last frame
     */
    void update(float deltaTime);
    
    /**
     * Register a physics object
     * @param bodyId Jolt body ID
     * @param gameObjectId Game-side object ID
     * @return Physics object index
     */
    uint32_t registerObject(JPH::BodyID bodyId, uint32_t gameObjectId);
    
    /**
     * Unregister a physics object
     */
    void unregisterObject(uint32_t objectIndex);
    
    /**
     * Get interpolated transform for rendering
     */
    InterpolatedTransform getInterpolatedTransform(uint32_t objectIndex) const;
    
    /**
     * Get all interpolated transforms for batch rendering
     */
    void getInterpolatedTransforms(std::vector<InterpolatedTransform>& outTransforms) const;
    
    /**
     * Queue a physics command (thread-safe)
     */
    void queueCommand(std::function<void(JPH::PhysicsSystem&)> command);
    
    /**
     * Set callback for transform updates
     */
    void setTransformCallback(PhysicsCallback callback) { transformCallback_ = callback; }
    
    /**
     * Set callback for collision events
     */
    void setCollisionCallback(PhysicsCallback callback) { collisionCallback_ = callback; }
    
    // Direct access (use carefully - not thread-safe!)
    JPH::PhysicsSystem& getPhysicsSystem() { return *physicsSystem_; }
    JPH::BodyInterface& getBodyInterface();
    
    // Stats
    struct Stats {
        float lastStepTime;
        uint32_t substepsThisFrame;
        uint32_t activeBodyCount;
        float interpolationAlpha;
        bool isSimulating;
    };
    Stats getStats() const;
    
private:
    // Physics thread main loop
    void physicsThreadFunc();
    
    // Simulation step
    void stepPhysics(float deltaTime);
    
    // Swap transform buffers
    void swapBuffers();
    
    // Update interpolation
    void computeInterpolation(float alpha);
    
    // Process queued commands
    void processCommands();
    
    // Fire callbacks
    void fireCallbacks();
    
    AsyncPhysicsConfig config_;
    bool initialized_ = false;
    
    // Jolt systems
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;
    
    // Layer interfaces
    class BPLayerInterface;
    class ObjectVsBroadPhaseLayerFilter;
    class ObjectLayerPairFilter;
    class ContactListener;
    
    std::unique_ptr<BPLayerInterface> bpLayerInterface_;
    std::unique_ptr<ObjectVsBroadPhaseLayerFilter> broadPhaseFilter_;
    std::unique_ptr<ObjectLayerPairFilter> layerPairFilter_;
    std::unique_ptr<ContactListener> contactListener_;
    
    // Physics thread
    std::thread physicsThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::condition_variable physicsCV_;
    std::mutex physicsMutex_;
    
    // Timing
    float accumulator_ = 0.0f;
    float interpolationAlpha_ = 0.0f;
    float lastStepTime_ = 0.0f;
    uint32_t substepsThisFrame_ = 0;
    
    // Object storage
    std::vector<PhysicsObjectState> objects_;
    std::mutex objectsMutex_;
    std::vector<uint32_t> freeIndices_;
    
    // Command queue
    std::queue<std::function<void(JPH::PhysicsSystem&)>> commandQueue_;
    std::mutex commandMutex_;
    
    // Callbacks
    PhysicsCallback transformCallback_;
    PhysicsCallback collisionCallback_;
    
    // Double buffer control
    std::atomic<uint32_t> readBuffer_{0};
    std::atomic<uint32_t> writeBuffer_{1};
};

