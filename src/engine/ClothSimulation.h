/**
 * ClothSimulation.h
 * 
 * GPU-Based Cloth Simulation using Verlet Integration
 * 
 * Features:
 * - Position-based dynamics (Verlet integration)
 * - Distance constraints for structural integrity
 * - Bending constraints for realistic deformation
 * - Self-collision detection
 * - Collision with character capsules/spheres
 * - Wind forces
 * - GPU compute for performance
 * 
 * Reference:
 *   Engine/Source/Runtime/ClothingSystemRuntimeNv/
 *   Engine/Source/Runtime/ClothingSystemRuntimeCommon/
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

namespace Sanic {

class VulkanContext;

// ============================================================================
// CLOTH DATA STRUCTURES
// ============================================================================

/**
 * A single particle in the cloth simulation
 */
struct ClothParticle {
    glm::vec3 position;       // Current position
    float invMass;            // Inverse mass (0 = pinned/fixed)
    
    glm::vec3 prevPosition;   // Previous position for Verlet
    float padding1;
    
    glm::vec3 velocity;       // Current velocity (for visualization)
    float padding2;
    
    glm::vec3 normal;         // Surface normal (updated each frame)
    float padding3;
};

/**
 * Distance constraint between two particles
 */
struct ClothConstraint {
    uint32_t particleA;       // First particle index
    uint32_t particleB;       // Second particle index
    float restLength;         // Rest distance
    float stiffness;          // Constraint stiffness (0-1)
};

/**
 * Bending constraint (keeps 4 particles coplanar)
 */
struct ClothBendConstraint {
    uint32_t particles[4];    // Four particles forming a hinge
    float restAngle;          // Rest dihedral angle
    float stiffness;          // Bending stiffness
};

/**
 * Collision sphere (for body collision)
 */
struct ClothCollisionSphere {
    glm::vec3 center;
    float radius;
};

/**
 * Collision capsule (for limbs)
 */
struct ClothCollisionCapsule {
    glm::vec3 pointA;
    float radius;
    glm::vec3 pointB;
    float padding;
};

// ============================================================================
// CLOTH CONFIGURATION
// ============================================================================

/**
 * Configuration for a cloth simulation instance
 */
struct ClothConfig {
    // Physics settings
    float gravity = 9.81f;
    float damping = 0.02f;              // Velocity damping (0-1)
    float drag = 0.1f;                  // Air drag coefficient
    
    // Constraint solver
    uint32_t solverIterations = 4;      // More = stiffer cloth
    float stretchStiffness = 1.0f;      // Distance constraint stiffness
    float bendStiffness = 0.5f;         // Bending constraint stiffness
    float compressionStiffness = 1.0f;  // Resistance to compression
    
    // Collision
    float collisionMargin = 0.01f;      // Collision offset
    float friction = 0.3f;              // Collision friction
    bool enableSelfCollision = false;   // Expensive!
    float selfCollisionDistance = 0.05f;
    
    // Wind
    glm::vec3 windDirection = glm::vec3(1, 0, 0);
    float windStrength = 0.0f;
    float windTurbulence = 0.0f;        // Random variation
    
    // Quality
    bool useGPU = true;                 // Use GPU compute
    float maxTimeStep = 1.0f / 60.0f;   // Maximum substep
    uint32_t maxSubsteps = 4;           // Maximum substeps per frame
};

// ============================================================================
// CLOTH MESH
// ============================================================================

/**
 * Represents the cloth mesh data
 */
class ClothMesh {
public:
    ClothMesh() = default;
    
    /**
     * Create a rectangular cloth mesh
     * @param width Width in world units
     * @param height Height in world units
     * @param resX Resolution in X
     * @param resY Resolution in Y
     */
    static std::unique_ptr<ClothMesh> createRectangle(
        float width, float height,
        uint32_t resX, uint32_t resY
    );
    
    /**
     * Create cloth from an existing mesh
     * @param vertices Vertex positions
     * @param indices Triangle indices
     */
    static std::unique_ptr<ClothMesh> fromMesh(
        const std::vector<glm::vec3>& vertices,
        const std::vector<uint32_t>& indices
    );
    
    // Data access
    std::vector<ClothParticle>& getParticles() { return particles_; }
    const std::vector<ClothParticle>& getParticles() const { return particles_; }
    
    std::vector<ClothConstraint>& getConstraints() { return constraints_; }
    const std::vector<ClothConstraint>& getConstraints() const { return constraints_; }
    
    std::vector<ClothBendConstraint>& getBendConstraints() { return bendConstraints_; }
    const std::vector<ClothBendConstraint>& getBendConstraints() const { return bendConstraints_; }
    
    const std::vector<uint32_t>& getIndices() const { return indices_; }
    
    /**
     * Pin a particle (set inverse mass to 0)
     */
    void pinParticle(uint32_t index);
    
    /**
     * Unpin a particle
     */
    void unpinParticle(uint32_t index, float mass = 1.0f);
    
    /**
     * Pin all particles in a row (for hanging cloth)
     */
    void pinRow(uint32_t row, uint32_t rowWidth);
    
    /**
     * Generate distance constraints from triangle mesh
     */
    void generateConstraints(float stiffness = 1.0f);
    
    /**
     * Generate bending constraints
     */
    void generateBendingConstraints(float stiffness = 0.5f);
    
private:
    std::vector<ClothParticle> particles_;
    std::vector<ClothConstraint> constraints_;
    std::vector<ClothBendConstraint> bendConstraints_;
    std::vector<uint32_t> indices_;  // Triangle indices for rendering
    
    // Helper for constraint generation
    void addConstraintIfNew(uint32_t a, uint32_t b, float stiffness);
    std::unordered_map<uint64_t, bool> existingConstraints_;
};

// ============================================================================
// GPU CLOTH SIMULATOR
// ============================================================================

/**
 * GPU-based cloth simulation using compute shaders
 */
class GPUClothSimulator {
public:
    GPUClothSimulator(VulkanContext& context);
    ~GPUClothSimulator();
    
    /**
     * Initialize the simulator
     */
    bool initialize();
    
    /**
     * Shutdown and cleanup
     */
    void shutdown();
    
    /**
     * Create a new cloth instance
     * @param mesh The cloth mesh
     * @param config Simulation configuration
     * @return Handle to the cloth instance
     */
    uint32_t createCloth(std::unique_ptr<ClothMesh> mesh, const ClothConfig& config = {});
    
    /**
     * Destroy a cloth instance
     */
    void destroyCloth(uint32_t handle);
    
    /**
     * Update simulation
     * @param cmd Command buffer
     * @param deltaTime Frame delta time
     */
    void simulate(VkCommandBuffer cmd, float deltaTime);
    
    /**
     * Get particle buffer for a cloth (for rendering)
     */
    VkBuffer getParticleBuffer(uint32_t handle) const;
    
    /**
     * Get index buffer for a cloth
     */
    VkBuffer getIndexBuffer(uint32_t handle) const;
    
    /**
     * Get particle count
     */
    uint32_t getParticleCount(uint32_t handle) const;
    
    /**
     * Get triangle count
     */
    uint32_t getTriangleCount(uint32_t handle) const;
    
    /**
     * Update collision shapes for a cloth
     */
    void setCollisionSpheres(uint32_t handle, const std::vector<ClothCollisionSphere>& spheres);
    void setCollisionCapsules(uint32_t handle, const std::vector<ClothCollisionCapsule>& capsules);
    
    /**
     * Update wind for a cloth
     */
    void setWind(uint32_t handle, const glm::vec3& direction, float strength, float turbulence = 0.0f);
    
    /**
     * Reset cloth to initial state
     */
    void resetCloth(uint32_t handle);
    
    /**
     * Update configuration
     */
    void setConfig(uint32_t handle, const ClothConfig& config);
    
    /**
     * Apply external force to all particles
     */
    void applyForce(uint32_t handle, const glm::vec3& force);
    
    /**
     * Apply impulse at a point
     */
    void applyImpulse(uint32_t handle, const glm::vec3& position, const glm::vec3& impulse, float radius);
    
private:
    VulkanContext& context_;
    
    // Compute pipelines
    VkPipeline integratePipeline_ = VK_NULL_HANDLE;
    VkPipeline constraintPipeline_ = VK_NULL_HANDLE;
    VkPipeline collisionPipeline_ = VK_NULL_HANDLE;
    VkPipeline normalsPipeline_ = VK_NULL_HANDLE;
    
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    
    // Per-cloth instance data
    struct ClothInstance {
        std::unique_ptr<ClothMesh> mesh;
        ClothConfig config;
        
        // GPU buffers
        VkBuffer particleBuffer = VK_NULL_HANDLE;
        VkDeviceMemory particleMemory = VK_NULL_HANDLE;
        
        VkBuffer constraintBuffer = VK_NULL_HANDLE;
        VkDeviceMemory constraintMemory = VK_NULL_HANDLE;
        
        VkBuffer bendConstraintBuffer = VK_NULL_HANDLE;
        VkDeviceMemory bendConstraintMemory = VK_NULL_HANDLE;
        
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        
        VkBuffer collisionBuffer = VK_NULL_HANDLE;
        VkDeviceMemory collisionMemory = VK_NULL_HANDLE;
        
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        
        // Collision data
        std::vector<ClothCollisionSphere> spheres;
        std::vector<ClothCollisionCapsule> capsules;
        
        // Simulation state
        float accumulatedTime = 0.0f;
        glm::vec3 externalForce = glm::vec3(0);
    };
    
    std::unordered_map<uint32_t, ClothInstance> clothInstances_;
    uint32_t nextHandle_ = 1;
    
    bool initialized_ = false;
    
    // Pipeline creation
    void createPipelines();
    void createDescriptorLayout();
    
    // Buffer management
    void createBuffers(ClothInstance& instance);
    void updateBuffers(ClothInstance& instance);
    void destroyBuffers(ClothInstance& instance);
    
    // Simulation steps
    void dispatchIntegrate(VkCommandBuffer cmd, ClothInstance& instance, float dt);
    void dispatchConstraints(VkCommandBuffer cmd, ClothInstance& instance);
    void dispatchCollision(VkCommandBuffer cmd, ClothInstance& instance);
    void dispatchNormals(VkCommandBuffer cmd, ClothInstance& instance);
};

// ============================================================================
// CPU CLOTH SIMULATOR (Fallback)
// ============================================================================

/**
 * CPU-based cloth simulation (fallback when GPU not available)
 */
class CPUClothSimulator {
public:
    CPUClothSimulator() = default;
    
    /**
     * Create a cloth simulation
     */
    uint32_t createCloth(std::unique_ptr<ClothMesh> mesh, const ClothConfig& config = {});
    
    /**
     * Destroy cloth
     */
    void destroyCloth(uint32_t handle);
    
    /**
     * Simulate one frame
     */
    void simulate(float deltaTime);
    
    /**
     * Get particles for a cloth
     */
    const std::vector<ClothParticle>* getParticles(uint32_t handle) const;
    
    /**
     * Set collision shapes
     */
    void setCollisionSpheres(uint32_t handle, const std::vector<ClothCollisionSphere>& spheres);
    void setCollisionCapsules(uint32_t handle, const std::vector<ClothCollisionCapsule>& capsules);
    
    /**
     * Set wind
     */
    void setWind(uint32_t handle, const glm::vec3& direction, float strength, float turbulence = 0.0f);
    
private:
    struct ClothInstance {
        std::unique_ptr<ClothMesh> mesh;
        ClothConfig config;
        std::vector<ClothCollisionSphere> spheres;
        std::vector<ClothCollisionCapsule> capsules;
        float accumulatedTime = 0.0f;
    };
    
    std::unordered_map<uint32_t, ClothInstance> instances_;
    uint32_t nextHandle_ = 1;
    
    // Simulation steps
    void integrateParticles(ClothInstance& instance, float dt);
    void solveConstraints(ClothInstance& instance);
    void handleCollisions(ClothInstance& instance);
    void updateNormals(ClothInstance& instance);
    void applyWind(ClothInstance& instance, float dt);
};

// ============================================================================
// CLOTH COMPONENT (ECS)
// ============================================================================

/**
 * Component for attaching cloth to an entity
 */
struct ClothComponent {
    uint32_t clothHandle = 0;           // Handle to cloth simulation
    bool useGPU = true;                 // Use GPU simulation
    
    // Attachment
    std::string attachBoneName;          // Bone to attach to (optional)
    glm::vec3 attachOffset = glm::vec3(0);
    
    // Configuration (copied to simulator)
    ClothConfig config;
    
    // Collision binding
    std::vector<std::string> collisionBones;  // Bones that collide with cloth
    float collisionRadius = 0.1f;
};

// ============================================================================
// CLOTH SKINNING
// ============================================================================

/**
 * Allows cloth to follow a skinned mesh partially
 */
class ClothSkinning {
public:
    struct SkinningWeight {
        uint32_t boneIndex;
        float weight;
    };
    
    /**
     * Set skinning data for cloth particles
     * Skinned particles will blend between simulation and skeleton position
     */
    void setSkinningWeights(
        uint32_t clothHandle,
        const std::vector<std::vector<SkinningWeight>>& weights
    );
    
    /**
     * Apply skinning blend
     * @param particles Current simulated particles
     * @param boneTransforms Current bone transforms
     * @param restPose Rest pose of cloth in bone space
     */
    void applySkinning(
        std::vector<ClothParticle>& particles,
        const std::vector<glm::mat4>& boneTransforms,
        const std::vector<glm::vec3>& restPose
    );
    
private:
    std::unordered_map<uint32_t, std::vector<std::vector<SkinningWeight>>> skinningData_;
};

} // namespace Sanic
