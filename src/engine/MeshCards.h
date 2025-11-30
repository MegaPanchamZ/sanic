/**
 * MeshCards.h
 * 
 * Lumen-style mesh card system for surface cache.
 * Generates 6-axis oriented bounding box (OBB) cards for each mesh.
 * 
 * Key features:
 * - Axis-aligned card generation with OBB representation
 * - Hierarchical mip-level allocation (8x8 to 2048x2048)
 * - Virtual texture page management
 * - Card-mesh relationship tracking
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <bitset>

class VulkanContext;

// Card facing direction (6 axis-aligned directions)
enum class CardDirection : uint8_t {
    PositiveX = 0,
    NegativeX = 1,
    PositiveY = 2,
    NegativeY = 3,
    PositiveZ = 4,
    NegativeZ = 5,
    Count = 6
};

// Oriented Bounding Box representation
struct OBB {
    glm::vec3 center;
    glm::vec3 extents;      // Half-extents along each axis
    glm::mat3 orientation;  // Rotation matrix
    
    OBB() : center(0.0f), extents(0.0f), orientation(1.0f) {}
    
    float getSurfaceArea() const {
        return 2.0f * (extents.x * extents.y + extents.y * extents.z + extents.z * extents.x);
    }
};

// Single mesh card - represents one face of the mesh's OBB
struct LumenCard {
    // OBB representations
    OBB localOBB;           // In mesh local space
    OBB worldOBB;           // Transformed to world space
    OBB meshCardsOBB;       // In mesh cards space (for atlas UV mapping)
    
    // Atlas allocation
    struct AtlasAllocation {
        uint16_t offsetX;
        uint16_t offsetY;
        uint16_t sizeX;
        uint16_t sizeY;
        uint8_t mipLevel;
        bool valid;
    } atlasAlloc;
    
    // Card properties
    CardDirection direction;
    uint32_t meshCardsIndex;    // Parent mesh cards
    uint32_t cardIndex;         // Index within parent (0-5)
    float initialAspectRatio;   // Cannot change during reallocation
    float priority;             // For update scheduling
    
    // State
    bool needsCapture;
    bool isVisible;
    uint32_t lastCaptureFrame;
    uint32_t lastAccessFrame;
    
    // GPU buffer index
    uint32_t gpuIndex;
};

// GPU-side card data structure (matches shader)
struct alignas(16) GPULumenCard {
    // OBB data (center + extents)
    glm::vec4 worldCenter;      // xyz = center, w = surfaceArea
    glm::vec4 worldExtents;     // xyz = half-extents, w = priority
    
    // Orientation quaternion
    glm::vec4 orientation;      // xyzw quaternion
    
    // Atlas info
    glm::ivec4 atlasRect;       // x,y = offset, z,w = size
    
    // Additional data
    glm::vec4 normalDirection;  // xyz = facing normal, w = mipLevel
    glm::ivec4 indices;         // x = meshCardsIdx, y = cardIdx, z = flags, w = reserved
};

// Mesh cards - groups all cards for a single mesh/primitive
struct LumenMeshCards {
    // Transform
    glm::mat4 localToWorld;
    glm::mat4 worldToLocal;
    
    // Card range
    uint32_t firstCardIndex;
    uint32_t cardCount;
    
    // Direction lookup - bitmask for each direction
    std::bitset<6> directionMask;
    
    // Properties
    uint32_t meshId;
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    
    // Flags
    bool hasDistanceField;
    bool affectsIndirectLighting;
    bool affectsSkyLighting;
    bool isVisible;
    bool needsUpdate;
    
    // LOD
    float screenSize;           // Current screen size for LOD
    uint32_t currentLOD;
};

// GPU-side mesh cards data
struct alignas(16) GPUMeshCardsData {
    glm::mat4 localToWorld;
    glm::vec4 boundsMin;        // w = firstCardIndex
    glm::vec4 boundsMax;        // w = cardCount
    glm::ivec4 flags;           // x = directionMask, y = meshId, z = LOD, w = reserved
};

// Surface cache page for virtual texturing
struct SurfaceCachePage {
    uint32_t physicalX;
    uint32_t physicalY;
    uint32_t virtualX;
    uint32_t virtualY;
    uint32_t cardIndex;
    uint8_t mipLevel;
    bool resident;
    uint32_t lastAccessFrame;
};

// Configuration
struct MeshCardsConfig {
    // Resolution
    uint32_t minCardResolution = 8;
    uint32_t maxCardResolution = 2048;
    uint32_t physicalPageSize = 128;
    
    // Allocation
    uint32_t maxCards = 16384;
    uint32_t maxMeshCards = 4096;
    uint32_t maxPages = 8192;
    
    // Quality
    float minCardSurfaceArea = 100.0f;  // 10cm² minimum (100 cm² = 0.01 m²)
    float cardTargetScreenSize = 0.15f; // Target 15% screen coverage per card
    
    // Update budget
    float captureTimeBudgetMs = 2.0f;
    uint32_t maxCardsPerFrame = 64;
};

class MeshCards {
public:
    MeshCards() = default;
    ~MeshCards();
    
    bool initialize(VulkanContext* context, const MeshCardsConfig& config = {});
    void cleanup();
    
    /**
     * Register mesh and generate cards from pre-built data
     * @param meshId Unique mesh identifier
     * @param boundsMin Local-space AABB min
     * @param boundsMax Local-space AABB max
     * @param transform World transform
     * @param cardOBBs Optional pre-computed card OBBs (from asset cook)
     * @return Mesh cards index
     */
    uint32_t registerMesh(uint32_t meshId,
                          const glm::vec3& boundsMin,
                          const glm::vec3& boundsMax,
                          const glm::mat4& transform,
                          const std::vector<OBB>* cardOBBs = nullptr);
    
    /**
     * Unregister mesh and free all its cards
     */
    void unregisterMesh(uint32_t meshCardsIndex);
    
    /**
     * Update mesh transform (invalidates cards)
     */
    void updateTransform(uint32_t meshCardsIndex, const glm::mat4& transform);
    
    /**
     * Build GPU buffers after registration changes
     */
    void buildGPUBuffers(VkCommandBuffer cmd);
    
    /**
     * Get cards that need capture this frame
     */
    std::vector<uint32_t> getCardsToCapture(uint32_t maxCards, const glm::vec3& cameraPos);
    
    /**
     * Mark card as captured
     */
    void markCaptured(uint32_t cardIndex, uint32_t frame);
    
    /**
     * Cull cards against frustum
     */
    void cullCards(const glm::mat4& viewProj, const glm::vec3& cameraPos);
    
    // Accessors
    VkBuffer getCardBuffer() const { return cardBuffer_; }
    VkBuffer getMeshCardsBuffer() const { return meshCardsBuffer_; }
    VkDeviceAddress getCardBufferAddress() const { return cardBufferAddr_; }
    
    const LumenCard& getCard(uint32_t index) const { return cards_[index]; }
    const LumenMeshCards& getMeshCards(uint32_t index) const { return meshCards_[index]; }
    
    uint32_t getCardCount() const { return static_cast<uint32_t>(cards_.size()); }
    uint32_t getMeshCardsCount() const { return static_cast<uint32_t>(meshCards_.size()); }
    
    struct Stats {
        uint32_t totalCards;
        uint32_t visibleCards;
        uint32_t pendingCaptures;
        uint32_t totalMeshCards;
        uint32_t residentPages;
        float atlasUtilization;
    };
    Stats getStats() const;
    
private:
    // Card generation
    void generateCardsFromBounds(uint32_t meshCardsIndex,
                                 const glm::vec3& boundsMin,
                                 const glm::vec3& boundsMax);
    
    void generateCardsFromOBBs(uint32_t meshCardsIndex,
                               const std::vector<OBB>& obbs);
    
    // Atlas allocation
    bool allocateCardInAtlas(LumenCard& card);
    void freeCardFromAtlas(LumenCard& card);
    
    // Resolution selection
    uint32_t selectCardResolution(const LumenCard& card, const glm::vec3& cameraPos);
    
    // Priority calculation
    void updateCardPriorities(const glm::vec3& cameraPos);
    
    VulkanContext* context_ = nullptr;
    MeshCardsConfig config_;
    bool initialized_ = false;
    
    // Card storage
    std::vector<LumenCard> cards_;
    std::vector<LumenMeshCards> meshCards_;
    
    // Lookup maps
    std::unordered_map<uint32_t, uint32_t> meshIdToMeshCards_;
    
    // Free lists
    std::vector<uint32_t> freeCardSlots_;
    std::vector<uint32_t> freeMeshCardsSlots_;
    
    // GPU buffers
    VkBuffer cardBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory cardMemory_ = VK_NULL_HANDLE;
    VkDeviceAddress cardBufferAddr_ = 0;
    
    VkBuffer meshCardsBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory meshCardsMemory_ = VK_NULL_HANDLE;
    
    // Staging buffer for uploads
    VkBuffer stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    void* stagingMapped_ = nullptr;
    
    // Page table for virtual texturing
    std::vector<SurfaceCachePage> pages_;
    VkBuffer pageTableBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory pageTableMemory_ = VK_NULL_HANDLE;
    
    // Update tracking
    std::vector<uint32_t> dirtyCards_;
    bool buffersNeedRebuild_ = false;
    uint32_t currentFrame_ = 0;
};

