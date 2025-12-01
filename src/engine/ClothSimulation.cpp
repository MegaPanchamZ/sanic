/**
 * ClothSimulation.cpp
 * 
 * Implementation of GPU and CPU cloth simulation
 */

#include "ClothSimulation.h"
#include "VulkanContext.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace Sanic {

// ============================================================================
// CLOTH MESH IMPLEMENTATION
// ============================================================================

std::unique_ptr<ClothMesh> ClothMesh::createRectangle(
    float width, float height,
    uint32_t resX, uint32_t resY
) {
    auto mesh = std::make_unique<ClothMesh>();
    
    float stepX = width / (resX - 1);
    float stepY = height / (resY - 1);
    
    // Create particles in a grid
    mesh->particles_.resize(resX * resY);
    
    for (uint32_t y = 0; y < resY; ++y) {
        for (uint32_t x = 0; x < resX; ++x) {
            uint32_t idx = y * resX + x;
            ClothParticle& p = mesh->particles_[idx];
            
            p.position = glm::vec3(x * stepX - width * 0.5f, 0.0f, y * stepY - height * 0.5f);
            p.prevPosition = p.position;
            p.velocity = glm::vec3(0);
            p.invMass = 1.0f;  // All particles have same mass
            p.normal = glm::vec3(0, 1, 0);
        }
    }
    
    // Create triangle indices
    for (uint32_t y = 0; y < resY - 1; ++y) {
        for (uint32_t x = 0; x < resX - 1; ++x) {
            uint32_t topLeft = y * resX + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (y + 1) * resX + x;
            uint32_t bottomRight = bottomLeft + 1;
            
            // First triangle
            mesh->indices_.push_back(topLeft);
            mesh->indices_.push_back(bottomLeft);
            mesh->indices_.push_back(topRight);
            
            // Second triangle
            mesh->indices_.push_back(topRight);
            mesh->indices_.push_back(bottomLeft);
            mesh->indices_.push_back(bottomRight);
        }
    }
    
    // Generate constraints
    mesh->generateConstraints(1.0f);
    mesh->generateBendingConstraints(0.5f);
    
    return mesh;
}

std::unique_ptr<ClothMesh> ClothMesh::fromMesh(
    const std::vector<glm::vec3>& vertices,
    const std::vector<uint32_t>& indices
) {
    auto mesh = std::make_unique<ClothMesh>();
    
    // Create particles from vertices
    mesh->particles_.resize(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        ClothParticle& p = mesh->particles_[i];
        p.position = vertices[i];
        p.prevPosition = vertices[i];
        p.velocity = glm::vec3(0);
        p.invMass = 1.0f;
        p.normal = glm::vec3(0, 1, 0);
    }
    
    mesh->indices_ = indices;
    
    // Generate constraints from edges
    mesh->generateConstraints(1.0f);
    mesh->generateBendingConstraints(0.5f);
    
    return mesh;
}

void ClothMesh::pinParticle(uint32_t index) {
    if (index < particles_.size()) {
        particles_[index].invMass = 0.0f;
    }
}

void ClothMesh::unpinParticle(uint32_t index, float mass) {
    if (index < particles_.size() && mass > 0.0f) {
        particles_[index].invMass = 1.0f / mass;
    }
}

void ClothMesh::pinRow(uint32_t row, uint32_t rowWidth) {
    for (uint32_t x = 0; x < rowWidth; ++x) {
        pinParticle(row * rowWidth + x);
    }
}

void ClothMesh::generateConstraints(float stiffness) {
    constraints_.clear();
    existingConstraints_.clear();
    
    // Create constraints from triangle edges
    for (size_t i = 0; i < indices_.size(); i += 3) {
        uint32_t a = indices_[i];
        uint32_t b = indices_[i + 1];
        uint32_t c = indices_[i + 2];
        
        addConstraintIfNew(a, b, stiffness);
        addConstraintIfNew(b, c, stiffness);
        addConstraintIfNew(c, a, stiffness);
    }
}

void ClothMesh::addConstraintIfNew(uint32_t a, uint32_t b, float stiffness) {
    if (a > b) std::swap(a, b);
    
    uint64_t key = (uint64_t(a) << 32) | uint64_t(b);
    
    if (existingConstraints_.find(key) != existingConstraints_.end()) {
        return;
    }
    
    existingConstraints_[key] = true;
    
    ClothConstraint c;
    c.particleA = a;
    c.particleB = b;
    c.restLength = glm::length(particles_[b].position - particles_[a].position);
    c.stiffness = stiffness;
    
    constraints_.push_back(c);
}

void ClothMesh::generateBendingConstraints(float stiffness) {
    bendConstraints_.clear();
    
    // Find adjacent triangles and create bending constraints
    std::unordered_map<uint64_t, std::vector<uint32_t>> edgeToTriangles;
    
    // Map edges to triangles
    for (size_t i = 0; i < indices_.size(); i += 3) {
        uint32_t triIdx = static_cast<uint32_t>(i / 3);
        
        uint32_t v[3] = { indices_[i], indices_[i + 1], indices_[i + 2] };
        
        for (int e = 0; e < 3; ++e) {
            uint32_t a = v[e];
            uint32_t b = v[(e + 1) % 3];
            if (a > b) std::swap(a, b);
            
            uint64_t key = (uint64_t(a) << 32) | uint64_t(b);
            edgeToTriangles[key].push_back(triIdx);
        }
    }
    
    // Create bending constraints for shared edges
    for (const auto& [edge, triangles] : edgeToTriangles) {
        if (triangles.size() != 2) continue;
        
        uint32_t t1 = triangles[0];
        uint32_t t2 = triangles[1];
        
        // Find the four vertices
        uint32_t edgeA = uint32_t(edge >> 32);
        uint32_t edgeB = uint32_t(edge & 0xFFFFFFFF);
        
        // Find opposite vertices
        uint32_t oppA = UINT32_MAX, oppB = UINT32_MAX;
        
        for (int i = 0; i < 3; ++i) {
            uint32_t v = indices_[t1 * 3 + i];
            if (v != edgeA && v != edgeB) oppA = v;
            
            v = indices_[t2 * 3 + i];
            if (v != edgeA && v != edgeB) oppB = v;
        }
        
        if (oppA == UINT32_MAX || oppB == UINT32_MAX) continue;
        
        ClothBendConstraint bc;
        bc.particles[0] = edgeA;
        bc.particles[1] = edgeB;
        bc.particles[2] = oppA;
        bc.particles[3] = oppB;
        bc.stiffness = stiffness;
        
        // Calculate rest angle
        glm::vec3 p0 = particles_[edgeA].position;
        glm::vec3 p1 = particles_[edgeB].position;
        glm::vec3 p2 = particles_[oppA].position;
        glm::vec3 p3 = particles_[oppB].position;
        
        glm::vec3 n1 = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        glm::vec3 n2 = glm::normalize(glm::cross(p3 - p0, p1 - p0));
        
        bc.restAngle = std::acos(glm::clamp(glm::dot(n1, n2), -1.0f, 1.0f));
        
        bendConstraints_.push_back(bc);
    }
}

// ============================================================================
// GPU CLOTH SIMULATOR IMPLEMENTATION
// ============================================================================

GPUClothSimulator::GPUClothSimulator(VulkanContext& context) 
    : context_(context) {
}

GPUClothSimulator::~GPUClothSimulator() {
    shutdown();
}

bool GPUClothSimulator::initialize() {
    if (initialized_) return true;
    
    createDescriptorLayout();
    createPipelines();
    
    initialized_ = true;
    return true;
}

void GPUClothSimulator::shutdown() {
    // Destroy all cloth instances
    for (auto& [handle, instance] : clothInstances_) {
        destroyBuffers(instance);
    }
    clothInstances_.clear();
    
    // Destroy pipelines
    if (integratePipeline_) {
        vkDestroyPipeline(context_.device, integratePipeline_, nullptr);
        integratePipeline_ = VK_NULL_HANDLE;
    }
    if (constraintPipeline_) {
        vkDestroyPipeline(context_.device, constraintPipeline_, nullptr);
        constraintPipeline_ = VK_NULL_HANDLE;
    }
    if (collisionPipeline_) {
        vkDestroyPipeline(context_.device, collisionPipeline_, nullptr);
        collisionPipeline_ = VK_NULL_HANDLE;
    }
    if (normalsPipeline_) {
        vkDestroyPipeline(context_.device, normalsPipeline_, nullptr);
        normalsPipeline_ = VK_NULL_HANDLE;
    }
    
    if (pipelineLayout_) {
        vkDestroyPipelineLayout(context_.device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorLayout_) {
        vkDestroyDescriptorSetLayout(context_.device, descriptorLayout_, nullptr);
        descriptorLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_) {
        vkDestroyDescriptorPool(context_.device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    
    initialized_ = false;
}

uint32_t GPUClothSimulator::createCloth(std::unique_ptr<ClothMesh> mesh, const ClothConfig& config) {
    if (!initialized_) return 0;
    
    uint32_t handle = nextHandle_++;
    
    ClothInstance instance;
    instance.mesh = std::move(mesh);
    instance.config = config;
    
    createBuffers(instance);
    
    clothInstances_[handle] = std::move(instance);
    
    return handle;
}

void GPUClothSimulator::destroyCloth(uint32_t handle) {
    auto it = clothInstances_.find(handle);
    if (it != clothInstances_.end()) {
        destroyBuffers(it->second);
        clothInstances_.erase(it);
    }
}

void GPUClothSimulator::simulate(VkCommandBuffer cmd, float deltaTime) {
    for (auto& [handle, instance] : clothInstances_) {
        instance.accumulatedTime += deltaTime;
        
        float maxStep = instance.config.maxTimeStep;
        uint32_t substeps = 0;
        
        while (instance.accumulatedTime >= maxStep && substeps < instance.config.maxSubsteps) {
            // Integration step (Verlet)
            dispatchIntegrate(cmd, instance, maxStep);
            
            // Memory barrier between stages
            VkMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &barrier, 0, nullptr, 0, nullptr);
            
            // Constraint solving (multiple iterations)
            for (uint32_t i = 0; i < instance.config.solverIterations; ++i) {
                dispatchConstraints(cmd, instance);
                
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &barrier, 0, nullptr, 0, nullptr);
            }
            
            // Collision handling
            dispatchCollision(cmd, instance);
            
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &barrier, 0, nullptr, 0, nullptr);
            
            instance.accumulatedTime -= maxStep;
            substeps++;
        }
        
        // Update normals for rendering
        dispatchNormals(cmd, instance);
        
        // Clear external force
        instance.externalForce = glm::vec3(0);
    }
}

VkBuffer GPUClothSimulator::getParticleBuffer(uint32_t handle) const {
    auto it = clothInstances_.find(handle);
    return it != clothInstances_.end() ? it->second.particleBuffer : VK_NULL_HANDLE;
}

VkBuffer GPUClothSimulator::getIndexBuffer(uint32_t handle) const {
    auto it = clothInstances_.find(handle);
    return it != clothInstances_.end() ? it->second.indexBuffer : VK_NULL_HANDLE;
}

uint32_t GPUClothSimulator::getParticleCount(uint32_t handle) const {
    auto it = clothInstances_.find(handle);
    return it != clothInstances_.end() ? static_cast<uint32_t>(it->second.mesh->getParticles().size()) : 0;
}

uint32_t GPUClothSimulator::getTriangleCount(uint32_t handle) const {
    auto it = clothInstances_.find(handle);
    return it != clothInstances_.end() ? static_cast<uint32_t>(it->second.mesh->getIndices().size() / 3) : 0;
}

void GPUClothSimulator::setCollisionSpheres(uint32_t handle, const std::vector<ClothCollisionSphere>& spheres) {
    auto it = clothInstances_.find(handle);
    if (it != clothInstances_.end()) {
        it->second.spheres = spheres;
        // TODO: Update collision buffer
    }
}

void GPUClothSimulator::setCollisionCapsules(uint32_t handle, const std::vector<ClothCollisionCapsule>& capsules) {
    auto it = clothInstances_.find(handle);
    if (it != clothInstances_.end()) {
        it->second.capsules = capsules;
        // TODO: Update collision buffer
    }
}

void GPUClothSimulator::setWind(uint32_t handle, const glm::vec3& direction, float strength, float turbulence) {
    auto it = clothInstances_.find(handle);
    if (it != clothInstances_.end()) {
        it->second.config.windDirection = glm::normalize(direction);
        it->second.config.windStrength = strength;
        it->second.config.windTurbulence = turbulence;
    }
}

void GPUClothSimulator::resetCloth(uint32_t handle) {
    auto it = clothInstances_.find(handle);
    if (it != clothInstances_.end()) {
        // Reset particles to initial state
        auto& particles = it->second.mesh->getParticles();
        for (auto& p : particles) {
            p.velocity = glm::vec3(0);
            p.prevPosition = p.position;
        }
        updateBuffers(it->second);
    }
}

void GPUClothSimulator::applyForce(uint32_t handle, const glm::vec3& force) {
    auto it = clothInstances_.find(handle);
    if (it != clothInstances_.end()) {
        it->second.externalForce += force;
    }
}

void GPUClothSimulator::applyImpulse(uint32_t handle, const glm::vec3& position, const glm::vec3& impulse, float radius) {
    auto it = clothInstances_.find(handle);
    if (it == clothInstances_.end()) return;
    
    auto& particles = it->second.mesh->getParticles();
    
    for (auto& p : particles) {
        if (p.invMass <= 0.0f) continue;
        
        float dist = glm::length(p.position - position);
        if (dist < radius) {
            float factor = 1.0f - (dist / radius);
            p.velocity += impulse * factor * p.invMass;
        }
    }
    
    updateBuffers(it->second);
}

void GPUClothSimulator::createPipelines() {
    // Pipeline creation would load shaders from:
    // - cloth_integrate.comp
    // - cloth_constraints.comp
    // - cloth_collision.comp
    // - cloth_normals.comp
    
    // For now, just mark as created
    // Actual implementation would use ShaderManager to load and compile shaders
}

void GPUClothSimulator::createDescriptorLayout() {
    // Create descriptor set layout for cloth buffers
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        // Particles buffer (read-write)
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        // Constraints buffer (read-only)
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        // Collision buffer (read-only)
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    vkCreateDescriptorSetLayout(context_.device, &layoutInfo, nullptr, &descriptorLayout_);
}

void GPUClothSimulator::createBuffers(ClothInstance& instance) {
    // Buffer creation implementation
    // Would create GPU buffers for particles, constraints, etc.
}

void GPUClothSimulator::updateBuffers(ClothInstance& instance) {
    // Update GPU buffers from CPU data
}

void GPUClothSimulator::destroyBuffers(ClothInstance& instance) {
    if (instance.particleBuffer) {
        vkDestroyBuffer(context_.device, instance.particleBuffer, nullptr);
        vkFreeMemory(context_.device, instance.particleMemory, nullptr);
    }
    if (instance.constraintBuffer) {
        vkDestroyBuffer(context_.device, instance.constraintBuffer, nullptr);
        vkFreeMemory(context_.device, instance.constraintMemory, nullptr);
    }
    if (instance.indexBuffer) {
        vkDestroyBuffer(context_.device, instance.indexBuffer, nullptr);
        vkFreeMemory(context_.device, instance.indexMemory, nullptr);
    }
    if (instance.collisionBuffer) {
        vkDestroyBuffer(context_.device, instance.collisionBuffer, nullptr);
        vkFreeMemory(context_.device, instance.collisionMemory, nullptr);
    }
}

void GPUClothSimulator::dispatchIntegrate(VkCommandBuffer cmd, ClothInstance& instance, float dt) {
    // Would bind integrate pipeline and dispatch compute
    // Push constants would include: dt, gravity, damping, wind params
}

void GPUClothSimulator::dispatchConstraints(VkCommandBuffer cmd, ClothInstance& instance) {
    // Would bind constraint pipeline and dispatch for each constraint
}

void GPUClothSimulator::dispatchCollision(VkCommandBuffer cmd, ClothInstance& instance) {
    // Would bind collision pipeline and dispatch
}

void GPUClothSimulator::dispatchNormals(VkCommandBuffer cmd, ClothInstance& instance) {
    // Would bind normals pipeline and dispatch
}

// ============================================================================
// CPU CLOTH SIMULATOR IMPLEMENTATION
// ============================================================================

uint32_t CPUClothSimulator::createCloth(std::unique_ptr<ClothMesh> mesh, const ClothConfig& config) {
    uint32_t handle = nextHandle_++;
    
    ClothInstance instance;
    instance.mesh = std::move(mesh);
    instance.config = config;
    
    instances_[handle] = std::move(instance);
    
    return handle;
}

void CPUClothSimulator::destroyCloth(uint32_t handle) {
    instances_.erase(handle);
}

void CPUClothSimulator::simulate(float deltaTime) {
    for (auto& [handle, instance] : instances_) {
        instance.accumulatedTime += deltaTime;
        
        float maxStep = instance.config.maxTimeStep;
        uint32_t substeps = 0;
        
        while (instance.accumulatedTime >= maxStep && substeps < instance.config.maxSubsteps) {
            // Apply wind
            applyWind(instance, maxStep);
            
            // Verlet integration
            integrateParticles(instance, maxStep);
            
            // Constraint solving
            for (uint32_t i = 0; i < instance.config.solverIterations; ++i) {
                solveConstraints(instance);
            }
            
            // Collision handling
            handleCollisions(instance);
            
            instance.accumulatedTime -= maxStep;
            substeps++;
        }
        
        // Update normals
        updateNormals(instance);
    }
}

const std::vector<ClothParticle>* CPUClothSimulator::getParticles(uint32_t handle) const {
    auto it = instances_.find(handle);
    return it != instances_.end() ? &it->second.mesh->getParticles() : nullptr;
}

void CPUClothSimulator::setCollisionSpheres(uint32_t handle, const std::vector<ClothCollisionSphere>& spheres) {
    auto it = instances_.find(handle);
    if (it != instances_.end()) {
        it->second.spheres = spheres;
    }
}

void CPUClothSimulator::setCollisionCapsules(uint32_t handle, const std::vector<ClothCollisionCapsule>& capsules) {
    auto it = instances_.find(handle);
    if (it != instances_.end()) {
        it->second.capsules = capsules;
    }
}

void CPUClothSimulator::setWind(uint32_t handle, const glm::vec3& direction, float strength, float turbulence) {
    auto it = instances_.find(handle);
    if (it != instances_.end()) {
        it->second.config.windDirection = glm::normalize(direction);
        it->second.config.windStrength = strength;
        it->second.config.windTurbulence = turbulence;
    }
}

void CPUClothSimulator::integrateParticles(ClothInstance& instance, float dt) {
    auto& particles = instance.mesh->getParticles();
    const auto& config = instance.config;
    
    glm::vec3 gravity = glm::vec3(0, -config.gravity, 0);
    float damping = 1.0f - config.damping;
    
    for (auto& p : particles) {
        if (p.invMass <= 0.0f) continue;  // Pinned particle
        
        // Verlet integration
        glm::vec3 velocity = (p.position - p.prevPosition) * damping;
        glm::vec3 acceleration = gravity * p.invMass;
        
        p.prevPosition = p.position;
        p.position = p.position + velocity + acceleration * dt * dt;
        p.velocity = velocity / dt;  // Store for visualization
    }
}

void CPUClothSimulator::solveConstraints(ClothInstance& instance) {
    auto& particles = instance.mesh->getParticles();
    const auto& constraints = instance.mesh->getConstraints();
    const auto& config = instance.config;
    
    // Solve distance constraints
    for (const auto& c : constraints) {
        ClothParticle& p1 = particles[c.particleA];
        ClothParticle& p2 = particles[c.particleB];
        
        glm::vec3 delta = p2.position - p1.position;
        float dist = glm::length(delta);
        
        if (dist < 0.0001f) continue;
        
        float diff = (dist - c.restLength) / dist;
        
        float w1 = p1.invMass / (p1.invMass + p2.invMass);
        float w2 = p2.invMass / (p1.invMass + p2.invMass);
        
        float stiffness = c.stiffness * config.stretchStiffness;
        
        if (p1.invMass > 0.0f) {
            p1.position += delta * diff * w1 * stiffness;
        }
        if (p2.invMass > 0.0f) {
            p2.position -= delta * diff * w2 * stiffness;
        }
    }
    
    // Solve bending constraints
    const auto& bendConstraints = instance.mesh->getBendConstraints();
    
    for (const auto& bc : bendConstraints) {
        ClothParticle& p0 = particles[bc.particles[0]];
        ClothParticle& p1 = particles[bc.particles[1]];
        ClothParticle& p2 = particles[bc.particles[2]];
        ClothParticle& p3 = particles[bc.particles[3]];
        
        // Calculate current angle
        glm::vec3 n1 = glm::cross(p1.position - p0.position, p2.position - p0.position);
        glm::vec3 n2 = glm::cross(p3.position - p0.position, p1.position - p0.position);
        
        float len1 = glm::length(n1);
        float len2 = glm::length(n2);
        
        if (len1 < 0.0001f || len2 < 0.0001f) continue;
        
        n1 /= len1;
        n2 /= len2;
        
        float currentAngle = std::acos(glm::clamp(glm::dot(n1, n2), -1.0f, 1.0f));
        float diff = (currentAngle - bc.restAngle) * bc.stiffness * config.bendStiffness;
        
        // Apply correction (simplified)
        glm::vec3 correction = n1 * diff * 0.1f;
        
        if (p2.invMass > 0.0f) p2.position -= correction * p2.invMass;
        if (p3.invMass > 0.0f) p3.position += correction * p3.invMass;
    }
}

void CPUClothSimulator::handleCollisions(ClothInstance& instance) {
    auto& particles = instance.mesh->getParticles();
    const auto& config = instance.config;
    
    // Sphere collisions
    for (const auto& sphere : instance.spheres) {
        for (auto& p : particles) {
            if (p.invMass <= 0.0f) continue;
            
            glm::vec3 toParticle = p.position - sphere.center;
            float dist = glm::length(toParticle);
            float minDist = sphere.radius + config.collisionMargin;
            
            if (dist < minDist && dist > 0.0001f) {
                glm::vec3 normal = toParticle / dist;
                p.position = sphere.center + normal * minDist;
                
                // Apply friction
                glm::vec3 velocity = p.position - p.prevPosition;
                glm::vec3 normalVel = glm::dot(velocity, normal) * normal;
                glm::vec3 tangentVel = velocity - normalVel;
                p.prevPosition = p.position - tangentVel * (1.0f - config.friction);
            }
        }
    }
    
    // Capsule collisions
    for (const auto& capsule : instance.capsules) {
        glm::vec3 ab = capsule.pointB - capsule.pointA;
        float abLen = glm::length(ab);
        if (abLen < 0.0001f) continue;
        
        glm::vec3 abNorm = ab / abLen;
        
        for (auto& p : particles) {
            if (p.invMass <= 0.0f) continue;
            
            glm::vec3 ap = p.position - capsule.pointA;
            float t = glm::clamp(glm::dot(ap, abNorm) / abLen, 0.0f, 1.0f);
            glm::vec3 closest = capsule.pointA + ab * t;
            
            glm::vec3 toParticle = p.position - closest;
            float dist = glm::length(toParticle);
            float minDist = capsule.radius + config.collisionMargin;
            
            if (dist < minDist && dist > 0.0001f) {
                glm::vec3 normal = toParticle / dist;
                p.position = closest + normal * minDist;
                
                // Apply friction
                glm::vec3 velocity = p.position - p.prevPosition;
                glm::vec3 normalVel = glm::dot(velocity, normal) * normal;
                glm::vec3 tangentVel = velocity - normalVel;
                p.prevPosition = p.position - tangentVel * (1.0f - config.friction);
            }
        }
    }
}

void CPUClothSimulator::updateNormals(ClothInstance& instance) {
    auto& particles = instance.mesh->getParticles();
    const auto& indices = instance.mesh->getIndices();
    
    // Reset normals
    for (auto& p : particles) {
        p.normal = glm::vec3(0);
    }
    
    // Accumulate face normals
    for (size_t i = 0; i < indices.size(); i += 3) {
        ClothParticle& p0 = particles[indices[i]];
        ClothParticle& p1 = particles[indices[i + 1]];
        ClothParticle& p2 = particles[indices[i + 2]];
        
        glm::vec3 e1 = p1.position - p0.position;
        glm::vec3 e2 = p2.position - p0.position;
        glm::vec3 normal = glm::cross(e1, e2);
        
        p0.normal += normal;
        p1.normal += normal;
        p2.normal += normal;
    }
    
    // Normalize
    for (auto& p : particles) {
        float len = glm::length(p.normal);
        if (len > 0.0001f) {
            p.normal /= len;
        } else {
            p.normal = glm::vec3(0, 1, 0);
        }
    }
}

void CPUClothSimulator::applyWind(ClothInstance& instance, float dt) {
    if (instance.config.windStrength <= 0.0f) return;
    
    auto& particles = instance.mesh->getParticles();
    const auto& config = instance.config;
    
    for (auto& p : particles) {
        if (p.invMass <= 0.0f) continue;
        
        // Wind force based on normal
        float exposure = std::max(0.0f, glm::dot(p.normal, config.windDirection));
        
        // Add turbulence
        float turbulence = 0.0f;
        if (config.windTurbulence > 0.0f) {
            // Simple noise based on position
            turbulence = std::sin(p.position.x * 3.0f + p.position.z * 2.0f) * config.windTurbulence;
        }
        
        glm::vec3 windForce = config.windDirection * (config.windStrength + turbulence) * exposure;
        
        // Apply as velocity change
        p.position += windForce * dt * dt * p.invMass;
    }
}

// ============================================================================
// CLOTH SKINNING IMPLEMENTATION
// ============================================================================

void ClothSkinning::setSkinningWeights(
    uint32_t clothHandle,
    const std::vector<std::vector<SkinningWeight>>& weights
) {
    skinningData_[clothHandle] = weights;
}

void ClothSkinning::applySkinning(
    std::vector<ClothParticle>& particles,
    const std::vector<glm::mat4>& boneTransforms,
    const std::vector<glm::vec3>& restPose
) {
    // Would blend simulated positions with skinned positions
    // Based on weights set via setSkinningWeights
}

} // namespace Sanic
