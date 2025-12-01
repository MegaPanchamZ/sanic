/**
 * SaveSystem.h
 * 
 * Save and Load System
 * 
 * Features:
 * - Game state serialization
 * - Save slots with metadata
 * - Auto-save functionality
 * - Checkpoint system
 * - Cloud save integration hooks
 * - Save file versioning and migration
 * 
 * Reference:
 *   Engine/Source/Runtime/SaveGame/
 */

#pragma once

#include "ECS.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <optional>

namespace Sanic {

// Forward declarations
class World;
class SaveSystem;

// ============================================================================
// SAVE TYPES
// ============================================================================

/**
 * Save slot identifier
 */
using SaveSlotID = int;
constexpr SaveSlotID AUTO_SAVE_SLOT = -1;
constexpr SaveSlotID QUICK_SAVE_SLOT = -2;

/**
 * Save file version
 */
struct SaveVersion {
    int major = 1;
    int minor = 0;
    int patch = 0;
    
    bool isCompatible(const SaveVersion& other) const {
        return major == other.major;  // Minor/patch are backwards compatible
    }
    
    std::string toString() const {
        return std::to_string(major) + "." + 
               std::to_string(minor) + "." + 
               std::to_string(patch);
    }
    
    static SaveVersion fromString(const std::string& str);
};

/**
 * Metadata for a save slot
 */
struct SaveMetadata {
    SaveSlotID slotId = 0;
    std::string saveName;
    std::string characterName;
    
    // Timestamps
    std::chrono::system_clock::time_point saveTime;
    float playTime = 0.0f;  // Total play time in seconds
    
    // Game state snapshot
    int playerLevel = 1;
    std::string currentArea;
    std::string currentQuest;
    float completionPercent = 0.0f;
    
    // Screenshot/thumbnail
    std::string thumbnailPath;
    std::vector<uint8_t> thumbnailData;
    
    // Save file info
    std::string filePath;
    size_t fileSize = 0;
    SaveVersion version;
    
    // Checksum for integrity
    uint32_t checksum = 0;
    
    /**
     * Get formatted save time
     */
    std::string getFormattedTime() const;
    
    /**
     * Get formatted play time
     */
    std::string getFormattedPlayTime() const;
};

// ============================================================================
// SERIALIZATION
// ============================================================================

/**
 * Interface for serializable objects
 */
class ISerializable {
public:
    virtual ~ISerializable() = default;
    
    /**
     * Serialize to JSON string
     */
    virtual std::string serialize() const = 0;
    
    /**
     * Deserialize from JSON string
     */
    virtual bool deserialize(const std::string& data) = 0;
    
    /**
     * Get type identifier
     */
    virtual const char* getTypeId() const = 0;
};

/**
 * Serialization context for save/load operations
 */
class SerializationContext {
public:
    SerializationContext() = default;
    
    // Entity ID mapping (runtime ID <-> persistent ID)
    std::unordered_map<Entity, uint64_t> entityToId;
    std::unordered_map<uint64_t, Entity> idToEntity;
    
    // Asset path mapping
    std::unordered_map<std::string, std::string> assetPaths;
    
    // Version info
    SaveVersion saveVersion;
    SaveVersion loadVersion;
    
    /**
     * Map runtime entity to persistent ID
     */
    uint64_t getPersistentId(Entity entity);
    
    /**
     * Map persistent ID to runtime entity
     */
    Entity getEntity(uint64_t persistentId);
    
private:
    uint64_t nextPersistentId_ = 1;
};

// ============================================================================
// SAVE DATA SECTIONS
// ============================================================================

/**
 * Section types in a save file
 */
enum class SaveSection {
    Header,
    World,
    Player,
    Quests,
    Inventory,
    Dialogue,
    Settings,
    Custom
};

/**
 * Handler for a save section
 */
struct SaveSectionHandler {
    SaveSection section;
    std::string name;
    
    std::function<std::string(const SerializationContext&)> serialize;
    std::function<bool(const std::string&, SerializationContext&)> deserialize;
    
    int priority = 0;  // Lower = saved/loaded first
};

// ============================================================================
// CHECKPOINT
// ============================================================================

/**
 * A checkpoint in the game
 */
struct Checkpoint {
    std::string id;
    std::string name;
    glm::vec3 respawnPosition;
    glm::quat respawnRotation;
    std::string areaId;
    
    // State snapshot at checkpoint
    std::string stateSnapshot;
    
    // Activation
    bool isActivated = false;
    float activationTime = 0.0f;
    
    // Visual/audio
    std::string iconPath;
    std::string activationSound;
};

/**
 * Checkpoint manager
 */
class CheckpointManager {
public:
    /**
     * Register a checkpoint
     */
    void registerCheckpoint(const Checkpoint& checkpoint);
    
    /**
     * Activate a checkpoint
     */
    void activateCheckpoint(const std::string& id);
    
    /**
     * Get current (last activated) checkpoint
     */
    const Checkpoint* getCurrentCheckpoint() const;
    
    /**
     * Get all checkpoints
     */
    std::vector<Checkpoint> getAllCheckpoints() const;
    
    /**
     * Get activated checkpoints
     */
    std::vector<const Checkpoint*> getActivatedCheckpoints() const;
    
    /**
     * Respawn at current checkpoint
     */
    bool respawnAtCheckpoint(World& world, Entity player);
    
    /**
     * Clear all checkpoint activations
     */
    void clearActivations();
    
    /**
     * Serialize
     */
    std::string serialize() const;
    
    /**
     * Deserialize
     */
    bool deserialize(const std::string& data);
    
private:
    std::unordered_map<std::string, Checkpoint> checkpoints_;
    std::string currentCheckpointId_;
};

// ============================================================================
// SAVE SYSTEM
// ============================================================================

/**
 * Main save/load system
 */
class SaveSystem {
public:
    SaveSystem();
    ~SaveSystem();
    
    /**
     * Initialize the save system
     */
    void init(const std::string& saveDirectory);
    
    /**
     * Shutdown
     */
    void shutdown();
    
    /**
     * Register a section handler
     */
    void registerSectionHandler(const SaveSectionHandler& handler);
    
    /**
     * Register a custom serializable
     */
    void registerSerializable(const std::string& typeId, 
                               std::function<ISerializable*()> factory);
    
    // ================== SAVE OPERATIONS ==================
    
    /**
     * Save game to slot
     */
    bool saveGame(SaveSlotID slot, const std::string& saveName = "");
    
    /**
     * Quick save
     */
    bool quickSave();
    
    /**
     * Auto save
     */
    bool autoSave();
    
    /**
     * Save to file directly
     */
    bool saveToFile(const std::string& filePath, const SaveMetadata& metadata);
    
    // ================== LOAD OPERATIONS ==================
    
    /**
     * Load game from slot
     */
    bool loadGame(SaveSlotID slot);
    
    /**
     * Quick load
     */
    bool quickLoad();
    
    /**
     * Load from file
     */
    bool loadFromFile(const std::string& filePath);
    
    // ================== SLOT MANAGEMENT ==================
    
    /**
     * Get save metadata for slot
     */
    std::optional<SaveMetadata> getSlotMetadata(SaveSlotID slot) const;
    
    /**
     * Get all save slots
     */
    std::vector<SaveMetadata> getAllSaveSlots() const;
    
    /**
     * Get number of used slots
     */
    int getUsedSlotCount() const;
    
    /**
     * Delete save slot
     */
    bool deleteSaveSlot(SaveSlotID slot);
    
    /**
     * Copy save slot
     */
    bool copySaveSlot(SaveSlotID source, SaveSlotID destination);
    
    /**
     * Check if slot exists
     */
    bool slotExists(SaveSlotID slot) const;
    
    /**
     * Get maximum save slots
     */
    int getMaxSaveSlots() const { return maxSaveSlots_; }
    
    /**
     * Set maximum save slots
     */
    void setMaxSaveSlots(int max) { maxSaveSlots_ = max; }
    
    // ================== AUTO-SAVE ==================
    
    /**
     * Enable/disable auto-save
     */
    void setAutoSaveEnabled(bool enabled) { autoSaveEnabled_ = enabled; }
    bool isAutoSaveEnabled() const { return autoSaveEnabled_; }
    
    /**
     * Set auto-save interval (seconds)
     */
    void setAutoSaveInterval(float seconds) { autoSaveInterval_ = seconds; }
    float getAutoSaveInterval() const { return autoSaveInterval_; }
    
    /**
     * Update (for auto-save timer)
     */
    void update(float deltaTime);
    
    // ================== CHECKPOINTS ==================
    
    /**
     * Get checkpoint manager
     */
    CheckpointManager& getCheckpointManager() { return checkpointManager_; }
    const CheckpointManager& getCheckpointManager() const { return checkpointManager_; }
    
    // ================== CALLBACKS ==================
    
    using SaveCallback = std::function<void(SaveSlotID, bool success)>;
    using LoadCallback = std::function<void(SaveSlotID, bool success)>;
    
    void setOnSaveStarted(SaveCallback callback) { onSaveStarted_ = callback; }
    void setOnSaveCompleted(SaveCallback callback) { onSaveCompleted_ = callback; }
    void setOnLoadStarted(LoadCallback callback) { onLoadStarted_ = callback; }
    void setOnLoadCompleted(LoadCallback callback) { onLoadCompleted_ = callback; }
    
    // ================== CLOUD SAVE ==================
    
    /**
     * Cloud save integration (hooks for platform SDKs)
     */
    void setCloudSaveEnabled(bool enabled) { cloudSaveEnabled_ = enabled; }
    bool isCloudSaveEnabled() const { return cloudSaveEnabled_; }
    
    /**
     * Upload save to cloud
     */
    bool uploadToCloud(SaveSlotID slot);
    
    /**
     * Download save from cloud
     */
    bool downloadFromCloud(SaveSlotID slot);
    
    /**
     * Sync with cloud
     */
    bool syncWithCloud();
    
    // ================== VERSIONING ==================
    
    /**
     * Get current save version
     */
    SaveVersion getCurrentVersion() const { return currentVersion_; }
    
    /**
     * Set current save version
     */
    void setCurrentVersion(const SaveVersion& version) { currentVersion_ = version; }
    
    /**
     * Register migration handler
     */
    void registerMigration(const SaveVersion& from, const SaveVersion& to,
                           std::function<std::string(const std::string&)> migrator);
    
    // ================== UTILITIES ==================
    
    /**
     * Get save directory
     */
    const std::string& getSaveDirectory() const { return saveDirectory_; }
    
    /**
     * Get file path for slot
     */
    std::string getSlotFilePath(SaveSlotID slot) const;
    
    /**
     * Calculate checksum
     */
    static uint32_t calculateChecksum(const std::string& data);
    
    /**
     * Compress save data
     */
    static std::vector<uint8_t> compressData(const std::string& data);
    
    /**
     * Decompress save data
     */
    static std::string decompressData(const std::vector<uint8_t>& compressed);
    
    /**
     * Set world reference
     */
    void setWorld(World* world) { world_ = world; }
    
private:
    std::string serializeGameState(SerializationContext& context);
    bool deserializeGameState(const std::string& data, SerializationContext& context);
    bool migrateData(std::string& data, const SaveVersion& from, const SaveVersion& to);
    SaveMetadata createMetadata(SaveSlotID slot, const std::string& saveName);
    void captureSnapshot(SaveMetadata& metadata);
    
    std::string saveDirectory_;
    World* world_ = nullptr;
    
    std::vector<SaveSectionHandler> sectionHandlers_;
    std::unordered_map<std::string, std::function<ISerializable*()>> serializableFactories_;
    
    CheckpointManager checkpointManager_;
    
    // Save slots
    int maxSaveSlots_ = 10;
    mutable std::unordered_map<SaveSlotID, SaveMetadata> slotCache_;
    
    // Auto-save
    bool autoSaveEnabled_ = true;
    float autoSaveInterval_ = 300.0f;  // 5 minutes
    float autoSaveTimer_ = 0.0f;
    
    // Cloud save
    bool cloudSaveEnabled_ = false;
    
    // Versioning
    SaveVersion currentVersion_ = { 1, 0, 0 };
    std::vector<std::tuple<SaveVersion, SaveVersion, 
                          std::function<std::string(const std::string&)>>> migrations_;
    
    // Callbacks
    SaveCallback onSaveStarted_;
    SaveCallback onSaveCompleted_;
    LoadCallback onLoadStarted_;
    LoadCallback onLoadCompleted_;
};

// ============================================================================
// SAVE GAME COMPONENT
// ============================================================================

/**
 * Component marking an entity to be saved
 */
struct SaveableComponent {
    bool shouldSave = true;
    std::string persistentId;  // Unique ID for save/load
    
    // What to save
    bool saveTransform = true;
    bool saveHealth = true;
    bool saveInventory = true;
    bool saveAI = true;
    bool saveCustomData = true;
    
    // Custom serialization
    std::function<std::string()> customSerialize;
    std::function<void(const std::string&)> customDeserialize;
};

// ============================================================================
// PLAYER DATA
// ============================================================================

/**
 * Persistent player data (separate from entity)
 */
struct PlayerSaveData : public ISerializable {
    // Identity
    std::string playerName;
    std::string characterClass;
    
    // Progress
    int level = 1;
    int experience = 0;
    float playTime = 0.0f;
    
    // Position
    glm::vec3 position = glm::vec3(0);
    glm::quat rotation = glm::quat(1, 0, 0, 0);
    std::string currentArea;
    
    // Stats
    int maxHealth = 100;
    int currentHealth = 100;
    int maxMana = 50;
    int currentMana = 50;
    
    // Attributes
    std::unordered_map<std::string, int> attributes;
    
    // Skills
    std::vector<std::string> unlockedSkills;
    std::unordered_map<std::string, int> skillLevels;
    
    // Currency
    int gold = 0;
    
    // Achievements
    std::vector<std::string> achievements;
    
    // Statistics
    std::unordered_map<std::string, int> statistics;
    
    // ISerializable interface
    std::string serialize() const override;
    bool deserialize(const std::string& data) override;
    const char* getTypeId() const override { return "PlayerSaveData"; }
};

// ============================================================================
// WORLD SAVE DATA
// ============================================================================

/**
 * Persistent world state
 */
struct WorldSaveData : public ISerializable {
    // World state
    float gameTime = 0.0f;
    int dayCount = 1;
    std::string weatherState;
    
    // Destroyed/spawned entities
    std::vector<std::string> destroyedPersistentIds;
    std::vector<std::string> spawnedEntityData;
    
    // Modified objects
    std::unordered_map<std::string, std::string> objectStates;
    
    // Unlocked areas
    std::vector<std::string> unlockedAreas;
    
    // Global flags
    std::unordered_map<std::string, bool> flags;
    std::unordered_map<std::string, int> counters;
    std::unordered_map<std::string, std::string> strings;
    
    // ISerializable interface
    std::string serialize() const override;
    bool deserialize(const std::string& data) override;
    const char* getTypeId() const override { return "WorldSaveData"; }
};

} // namespace Sanic
