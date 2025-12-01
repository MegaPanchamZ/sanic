/**
 * SaveSystem.cpp
 * 
 * Save and Load System Implementation
 */

#include "SaveSystem.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <zlib.h>

namespace Sanic {

namespace fs = std::filesystem;

// ============================================================================
// SAVE VERSION IMPLEMENTATION
// ============================================================================

SaveVersion SaveVersion::fromString(const std::string& str) {
    SaveVersion version;
    std::sscanf(str.c_str(), "%d.%d.%d", &version.major, &version.minor, &version.patch);
    return version;
}

// ============================================================================
// SAVE METADATA IMPLEMENTATION
// ============================================================================

std::string SaveMetadata::getFormattedTime() const {
    auto time = std::chrono::system_clock::to_time_t(saveTime);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string SaveMetadata::getFormattedPlayTime() const {
    int hours = static_cast<int>(playTime / 3600);
    int minutes = static_cast<int>((playTime - hours * 3600) / 60);
    int seconds = static_cast<int>(playTime) % 60;
    
    std::stringstream ss;
    if (hours > 0) {
        ss << hours << "h ";
    }
    ss << minutes << "m " << seconds << "s";
    return ss.str();
}

// ============================================================================
// SERIALIZATION CONTEXT IMPLEMENTATION
// ============================================================================

uint64_t SerializationContext::getPersistentId(Entity entity) {
    auto it = entityToId.find(entity);
    if (it != entityToId.end()) {
        return it->second;
    }
    
    uint64_t id = nextPersistentId_++;
    entityToId[entity] = id;
    idToEntity[id] = entity;
    return id;
}

Entity SerializationContext::getEntity(uint64_t persistentId) {
    auto it = idToEntity.find(persistentId);
    return it != idToEntity.end() ? it->second : INVALID_ENTITY;
}

// ============================================================================
// CHECKPOINT MANAGER IMPLEMENTATION
// ============================================================================

void CheckpointManager::registerCheckpoint(const Checkpoint& checkpoint) {
    checkpoints_[checkpoint.id] = checkpoint;
}

void CheckpointManager::activateCheckpoint(const std::string& id) {
    auto it = checkpoints_.find(id);
    if (it == checkpoints_.end()) return;
    
    it->second.isActivated = true;
    it->second.activationTime = 0.0f;  // Would get current game time
    currentCheckpointId_ = id;
}

const Checkpoint* CheckpointManager::getCurrentCheckpoint() const {
    if (currentCheckpointId_.empty()) return nullptr;
    
    auto it = checkpoints_.find(currentCheckpointId_);
    return it != checkpoints_.end() ? &it->second : nullptr;
}

std::vector<Checkpoint> CheckpointManager::getAllCheckpoints() const {
    std::vector<Checkpoint> result;
    for (const auto& [id, checkpoint] : checkpoints_) {
        result.push_back(checkpoint);
    }
    return result;
}

std::vector<const Checkpoint*> CheckpointManager::getActivatedCheckpoints() const {
    std::vector<const Checkpoint*> result;
    for (const auto& [id, checkpoint] : checkpoints_) {
        if (checkpoint.isActivated) {
            result.push_back(&checkpoint);
        }
    }
    return result;
}

bool CheckpointManager::respawnAtCheckpoint(World& world, Entity player) {
    const Checkpoint* checkpoint = getCurrentCheckpoint();
    if (!checkpoint) return false;
    
    auto* transform = world.getComponent<Transform>(player);
    if (!transform) return false;
    
    transform->position = checkpoint->respawnPosition;
    transform->rotation = checkpoint->respawnRotation;
    
    // Could also restore state snapshot
    
    return true;
}

void CheckpointManager::clearActivations() {
    for (auto& [id, checkpoint] : checkpoints_) {
        checkpoint.isActivated = false;
    }
    currentCheckpointId_.clear();
}

std::string CheckpointManager::serialize() const {
    using json = nlohmann::json;
    
    json doc;
    doc["currentCheckpoint"] = currentCheckpointId_;
    doc["checkpoints"] = json::array();
    
    for (const auto& [id, checkpoint] : checkpoints_) {
        json c;
        c["id"] = checkpoint.id;
        c["activated"] = checkpoint.isActivated;
        c["activationTime"] = checkpoint.activationTime;
        doc["checkpoints"].push_back(c);
    }
    
    return doc.dump();
}

bool CheckpointManager::deserialize(const std::string& data) {
    using json = nlohmann::json;
    
    try {
        json doc = json::parse(data);
        
        currentCheckpointId_ = doc.value("currentCheckpoint", "");
        
        if (doc.contains("checkpoints")) {
            for (const auto& c : doc["checkpoints"]) {
                std::string id = c.value("id", "");
                auto it = checkpoints_.find(id);
                if (it != checkpoints_.end()) {
                    it->second.isActivated = c.value("activated", false);
                    it->second.activationTime = c.value("activationTime", 0.0f);
                }
            }
        }
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// ============================================================================
// SAVE SYSTEM IMPLEMENTATION
// ============================================================================

SaveSystem::SaveSystem() {
}

SaveSystem::~SaveSystem() {
    shutdown();
}

void SaveSystem::init(const std::string& saveDirectory) {
    saveDirectory_ = saveDirectory;
    
    // Create directory if it doesn't exist
    fs::create_directories(saveDirectory_);
    
    // Scan existing saves
    for (int i = 0; i < maxSaveSlots_; ++i) {
        std::string path = getSlotFilePath(i);
        if (fs::exists(path)) {
            // Would load metadata here
        }
    }
}

void SaveSystem::shutdown() {
    // Cleanup
}

void SaveSystem::registerSectionHandler(const SaveSectionHandler& handler) {
    sectionHandlers_.push_back(handler);
    
    // Sort by priority
    std::sort(sectionHandlers_.begin(), sectionHandlers_.end(),
              [](const SaveSectionHandler& a, const SaveSectionHandler& b) {
                  return a.priority < b.priority;
              });
}

void SaveSystem::registerSerializable(const std::string& typeId,
                                       std::function<ISerializable*()> factory) {
    serializableFactories_[typeId] = factory;
}

bool SaveSystem::saveGame(SaveSlotID slot, const std::string& saveName) {
    if (onSaveStarted_) onSaveStarted_(slot, true);
    
    std::string filePath = getSlotFilePath(slot);
    SaveMetadata metadata = createMetadata(slot, saveName);
    
    bool success = saveToFile(filePath, metadata);
    
    if (success) {
        slotCache_[slot] = metadata;
    }
    
    if (onSaveCompleted_) onSaveCompleted_(slot, success);
    
    return success;
}

bool SaveSystem::quickSave() {
    return saveGame(QUICK_SAVE_SLOT, "Quick Save");
}

bool SaveSystem::autoSave() {
    return saveGame(AUTO_SAVE_SLOT, "Auto Save");
}

bool SaveSystem::saveToFile(const std::string& filePath, const SaveMetadata& metadata) {
    using json = nlohmann::json;
    
    SerializationContext context;
    context.saveVersion = currentVersion_;
    
    // Serialize game state
    std::string gameData = serializeGameState(context);
    
    // Build save file
    json doc;
    doc["version"] = currentVersion_.toString();
    doc["metadata"] = {
        {"slotId", metadata.slotId},
        {"saveName", metadata.saveName},
        {"characterName", metadata.characterName},
        {"saveTime", std::chrono::system_clock::to_time_t(metadata.saveTime)},
        {"playTime", metadata.playTime},
        {"playerLevel", metadata.playerLevel},
        {"currentArea", metadata.currentArea},
        {"currentQuest", metadata.currentQuest},
        {"completionPercent", metadata.completionPercent}
    };
    doc["gameData"] = gameData;
    doc["checkpoints"] = checkpointManager_.serialize();
    
    std::string jsonStr = doc.dump();
    
    // Calculate checksum
    uint32_t checksum = calculateChecksum(jsonStr);
    
    // Compress
    auto compressed = compressData(jsonStr);
    
    // Write header + compressed data
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Magic number
    const char magic[] = "SANIC";
    file.write(magic, 5);
    
    // Version
    file.write(reinterpret_cast<const char*>(&currentVersion_.major), sizeof(int));
    file.write(reinterpret_cast<const char*>(&currentVersion_.minor), sizeof(int));
    file.write(reinterpret_cast<const char*>(&currentVersion_.patch), sizeof(int));
    
    // Checksum
    file.write(reinterpret_cast<const char*>(&checksum), sizeof(uint32_t));
    
    // Uncompressed size
    size_t uncompressedSize = jsonStr.size();
    file.write(reinterpret_cast<const char*>(&uncompressedSize), sizeof(size_t));
    
    // Compressed data
    file.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
    
    return true;
}

bool SaveSystem::loadGame(SaveSlotID slot) {
    if (onLoadStarted_) onLoadStarted_(slot, true);
    
    std::string filePath = getSlotFilePath(slot);
    bool success = loadFromFile(filePath);
    
    if (onLoadCompleted_) onLoadCompleted_(slot, success);
    
    return success;
}

bool SaveSystem::quickLoad() {
    return loadGame(QUICK_SAVE_SLOT);
}

bool SaveSystem::loadFromFile(const std::string& filePath) {
    using json = nlohmann::json;
    
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Read magic number
    char magic[6] = {0};
    file.read(magic, 5);
    if (std::string(magic) != "SANIC") return false;
    
    // Read version
    SaveVersion fileVersion;
    file.read(reinterpret_cast<char*>(&fileVersion.major), sizeof(int));
    file.read(reinterpret_cast<char*>(&fileVersion.minor), sizeof(int));
    file.read(reinterpret_cast<char*>(&fileVersion.patch), sizeof(int));
    
    if (!currentVersion_.isCompatible(fileVersion)) {
        return false;  // Incompatible save
    }
    
    // Read checksum
    uint32_t storedChecksum;
    file.read(reinterpret_cast<char*>(&storedChecksum), sizeof(uint32_t));
    
    // Read uncompressed size
    size_t uncompressedSize;
    file.read(reinterpret_cast<char*>(&uncompressedSize), sizeof(size_t));
    
    // Read compressed data
    std::vector<uint8_t> compressed(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
    
    // Decompress
    std::string jsonStr = decompressData(compressed);
    if (jsonStr.empty()) return false;
    
    // Verify checksum
    uint32_t checksum = calculateChecksum(jsonStr);
    if (checksum != storedChecksum) return false;
    
    // Parse JSON
    json doc;
    try {
        doc = json::parse(jsonStr);
    } catch (const std::exception&) {
        return false;
    }
    
    // Migrate if needed
    if (fileVersion.minor != currentVersion_.minor || 
        fileVersion.patch != currentVersion_.patch) {
        std::string gameData = doc["gameData"].get<std::string>();
        if (!migrateData(gameData, fileVersion, currentVersion_)) {
            return false;
        }
        doc["gameData"] = gameData;
    }
    
    // Deserialize
    SerializationContext context;
    context.loadVersion = fileVersion;
    context.saveVersion = currentVersion_;
    
    std::string gameData = doc["gameData"].get<std::string>();
    if (!deserializeGameState(gameData, context)) {
        return false;
    }
    
    // Load checkpoints
    if (doc.contains("checkpoints")) {
        checkpointManager_.deserialize(doc["checkpoints"].get<std::string>());
    }
    
    return true;
}

std::optional<SaveMetadata> SaveSystem::getSlotMetadata(SaveSlotID slot) const {
    auto it = slotCache_.find(slot);
    if (it != slotCache_.end()) {
        return it->second;
    }
    
    std::string path = getSlotFilePath(slot);
    if (!fs::exists(path)) return std::nullopt;
    
    // Would read just the header/metadata
    // For now, return empty metadata
    SaveMetadata metadata;
    metadata.slotId = slot;
    metadata.filePath = path;
    
    return metadata;
}

std::vector<SaveMetadata> SaveSystem::getAllSaveSlots() const {
    std::vector<SaveMetadata> result;
    
    for (int i = 0; i < maxSaveSlots_; ++i) {
        auto metadata = getSlotMetadata(i);
        if (metadata) {
            result.push_back(*metadata);
        }
    }
    
    // Also check special slots
    auto quickSave = getSlotMetadata(QUICK_SAVE_SLOT);
    if (quickSave) result.push_back(*quickSave);
    
    auto autoSave = getSlotMetadata(AUTO_SAVE_SLOT);
    if (autoSave) result.push_back(*autoSave);
    
    return result;
}

int SaveSystem::getUsedSlotCount() const {
    int count = 0;
    for (int i = 0; i < maxSaveSlots_; ++i) {
        if (slotExists(i)) count++;
    }
    return count;
}

bool SaveSystem::deleteSaveSlot(SaveSlotID slot) {
    std::string path = getSlotFilePath(slot);
    
    if (fs::exists(path)) {
        fs::remove(path);
        slotCache_.erase(slot);
        return true;
    }
    
    return false;
}

bool SaveSystem::copySaveSlot(SaveSlotID source, SaveSlotID destination) {
    std::string srcPath = getSlotFilePath(source);
    std::string dstPath = getSlotFilePath(destination);
    
    if (!fs::exists(srcPath)) return false;
    
    try {
        fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing);
        
        // Update cache
        auto metadata = getSlotMetadata(source);
        if (metadata) {
            metadata->slotId = destination;
            metadata->filePath = dstPath;
            slotCache_[destination] = *metadata;
        }
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool SaveSystem::slotExists(SaveSlotID slot) const {
    return fs::exists(getSlotFilePath(slot));
}

void SaveSystem::update(float deltaTime) {
    if (!autoSaveEnabled_) return;
    
    autoSaveTimer_ += deltaTime;
    
    if (autoSaveTimer_ >= autoSaveInterval_) {
        autoSaveTimer_ = 0.0f;
        autoSave();
    }
}

bool SaveSystem::uploadToCloud(SaveSlotID slot) {
    if (!cloudSaveEnabled_) return false;
    
    // Would integrate with Steam, Epic, etc.
    return false;
}

bool SaveSystem::downloadFromCloud(SaveSlotID slot) {
    if (!cloudSaveEnabled_) return false;
    
    // Would integrate with platform SDK
    return false;
}

bool SaveSystem::syncWithCloud() {
    if (!cloudSaveEnabled_) return false;
    
    // Would compare local and cloud saves
    return false;
}

void SaveSystem::registerMigration(const SaveVersion& from, const SaveVersion& to,
                                    std::function<std::string(const std::string&)> migrator) {
    migrations_.emplace_back(from, to, migrator);
}

std::string SaveSystem::getSlotFilePath(SaveSlotID slot) const {
    std::string filename;
    
    switch (slot) {
        case AUTO_SAVE_SLOT:
            filename = "autosave.sav";
            break;
        case QUICK_SAVE_SLOT:
            filename = "quicksave.sav";
            break;
        default:
            filename = "save_" + std::to_string(slot) + ".sav";
            break;
    }
    
    return (fs::path(saveDirectory_) / filename).string();
}

uint32_t SaveSystem::calculateChecksum(const std::string& data) {
    // Simple CRC32
    uint32_t crc = 0xFFFFFFFF;
    
    for (char c : data) {
        crc ^= static_cast<uint8_t>(c);
        for (int i = 0; i < 8; ++i) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

std::vector<uint8_t> SaveSystem::compressData(const std::string& data) {
    // Simple compression using zlib (would need to link zlib)
    // For now, just return uncompressed
    return std::vector<uint8_t>(data.begin(), data.end());
}

std::string SaveSystem::decompressData(const std::vector<uint8_t>& compressed) {
    // Would use zlib
    return std::string(compressed.begin(), compressed.end());
}

std::string SaveSystem::serializeGameState(SerializationContext& context) {
    using json = nlohmann::json;
    
    json doc;
    
    // Serialize each section
    for (const auto& handler : sectionHandlers_) {
        if (handler.serialize) {
            doc[handler.name] = handler.serialize(context);
        }
    }
    
    // Serialize saveable entities
    if (world_) {
        json entities = json::array();
        
        world_->query<SaveableComponent>([&](Entity entity, SaveableComponent& saveable) {
            if (!saveable.shouldSave) return;
            
            json e;
            e["persistentId"] = saveable.persistentId;
            
            if (saveable.saveTransform) {
                auto* transform = world_->getComponent<Transform>(entity);
                if (transform) {
                    e["transform"] = {
                        {"pos", {transform->position.x, transform->position.y, transform->position.z}},
                        {"rot", {transform->rotation.x, transform->rotation.y, 
                                transform->rotation.z, transform->rotation.w}},
                        {"scale", {transform->scale.x, transform->scale.y, transform->scale.z}}
                    };
                }
            }
            
            if (saveable.saveHealth) {
                auto* health = world_->getComponent<Health>(entity);
                if (health) {
                    e["health"] = {
                        {"current", health->current},
                        {"max", health->max}
                    };
                }
            }
            
            if (saveable.saveCustomData && saveable.customSerialize) {
                e["custom"] = saveable.customSerialize();
            }
            
            entities.push_back(e);
        });
        
        doc["entities"] = entities;
    }
    
    return doc.dump();
}

bool SaveSystem::deserializeGameState(const std::string& data, SerializationContext& context) {
    using json = nlohmann::json;
    
    json doc;
    try {
        doc = json::parse(data);
    } catch (const std::exception&) {
        return false;
    }
    
    // Deserialize each section
    for (const auto& handler : sectionHandlers_) {
        if (handler.deserialize && doc.contains(handler.name)) {
            if (!handler.deserialize(doc[handler.name].get<std::string>(), context)) {
                return false;
            }
        }
    }
    
    // Deserialize entities
    if (world_ && doc.contains("entities")) {
        for (const auto& e : doc["entities"]) {
            std::string persistentId = e.value("persistentId", "");
            
            // Find entity with this persistent ID
            Entity entity = INVALID_ENTITY;
            world_->query<SaveableComponent>([&](Entity ent, SaveableComponent& saveable) {
                if (saveable.persistentId == persistentId) {
                    entity = ent;
                }
            });
            
            if (entity == INVALID_ENTITY) continue;
            
            auto* saveable = world_->getComponent<SaveableComponent>(entity);
            if (!saveable) continue;
            
            if (saveable->saveTransform && e.contains("transform")) {
                auto* transform = world_->getComponent<Transform>(entity);
                if (transform) {
                    auto& t = e["transform"];
                    transform->position = glm::vec3(t["pos"][0], t["pos"][1], t["pos"][2]);
                    transform->rotation = glm::quat(t["rot"][3], t["rot"][0], t["rot"][1], t["rot"][2]);
                    transform->scale = glm::vec3(t["scale"][0], t["scale"][1], t["scale"][2]);
                }
            }
            
            if (saveable->saveHealth && e.contains("health")) {
                auto* health = world_->getComponent<Health>(entity);
                if (health) {
                    health->current = e["health"]["current"];
                    health->max = e["health"]["max"];
                }
            }
            
            if (saveable->saveCustomData && saveable->customDeserialize && e.contains("custom")) {
                saveable->customDeserialize(e["custom"].get<std::string>());
            }
        }
    }
    
    return true;
}

bool SaveSystem::migrateData(std::string& data, const SaveVersion& from, const SaveVersion& to) {
    // Find migration path
    for (const auto& [migFrom, migTo, migrator] : migrations_) {
        if (migFrom.major == from.major && migFrom.minor == from.minor &&
            migTo.major == to.major && migTo.minor == to.minor) {
            data = migrator(data);
            return true;
        }
    }
    
    // No migration needed or path not found
    return true;
}

SaveMetadata SaveSystem::createMetadata(SaveSlotID slot, const std::string& saveName) {
    SaveMetadata metadata;
    metadata.slotId = slot;
    metadata.saveName = saveName.empty() ? "Save " + std::to_string(slot) : saveName;
    metadata.saveTime = std::chrono::system_clock::now();
    metadata.filePath = getSlotFilePath(slot);
    metadata.version = currentVersion_;
    
    captureSnapshot(metadata);
    
    return metadata;
}

void SaveSystem::captureSnapshot(SaveMetadata& metadata) {
    // Would capture current game state
    // metadata.playerLevel = ...
    // metadata.currentArea = ...
    // etc.
}

// ============================================================================
// PLAYER SAVE DATA IMPLEMENTATION
// ============================================================================

std::string PlayerSaveData::serialize() const {
    using json = nlohmann::json;
    
    json doc;
    doc["playerName"] = playerName;
    doc["characterClass"] = characterClass;
    doc["level"] = level;
    doc["experience"] = experience;
    doc["playTime"] = playTime;
    doc["position"] = { position.x, position.y, position.z };
    doc["rotation"] = { rotation.x, rotation.y, rotation.z, rotation.w };
    doc["currentArea"] = currentArea;
    doc["maxHealth"] = maxHealth;
    doc["currentHealth"] = currentHealth;
    doc["maxMana"] = maxMana;
    doc["currentMana"] = currentMana;
    doc["gold"] = gold;
    doc["attributes"] = attributes;
    doc["unlockedSkills"] = unlockedSkills;
    doc["skillLevels"] = skillLevels;
    doc["achievements"] = achievements;
    doc["statistics"] = statistics;
    
    return doc.dump();
}

bool PlayerSaveData::deserialize(const std::string& data) {
    using json = nlohmann::json;
    
    try {
        json doc = json::parse(data);
        
        playerName = doc.value("playerName", "");
        characterClass = doc.value("characterClass", "");
        level = doc.value("level", 1);
        experience = doc.value("experience", 0);
        playTime = doc.value("playTime", 0.0f);
        
        if (doc.contains("position")) {
            position = glm::vec3(doc["position"][0], doc["position"][1], doc["position"][2]);
        }
        if (doc.contains("rotation")) {
            rotation = glm::quat(doc["rotation"][3], doc["rotation"][0], 
                                doc["rotation"][1], doc["rotation"][2]);
        }
        
        currentArea = doc.value("currentArea", "");
        maxHealth = doc.value("maxHealth", 100);
        currentHealth = doc.value("currentHealth", 100);
        maxMana = doc.value("maxMana", 50);
        currentMana = doc.value("currentMana", 50);
        gold = doc.value("gold", 0);
        
        if (doc.contains("attributes")) {
            attributes = doc["attributes"].get<std::unordered_map<std::string, int>>();
        }
        if (doc.contains("unlockedSkills")) {
            unlockedSkills = doc["unlockedSkills"].get<std::vector<std::string>>();
        }
        if (doc.contains("skillLevels")) {
            skillLevels = doc["skillLevels"].get<std::unordered_map<std::string, int>>();
        }
        if (doc.contains("achievements")) {
            achievements = doc["achievements"].get<std::vector<std::string>>();
        }
        if (doc.contains("statistics")) {
            statistics = doc["statistics"].get<std::unordered_map<std::string, int>>();
        }
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// ============================================================================
// WORLD SAVE DATA IMPLEMENTATION
// ============================================================================

std::string WorldSaveData::serialize() const {
    using json = nlohmann::json;
    
    json doc;
    doc["gameTime"] = gameTime;
    doc["dayCount"] = dayCount;
    doc["weatherState"] = weatherState;
    doc["destroyedPersistentIds"] = destroyedPersistentIds;
    doc["spawnedEntityData"] = spawnedEntityData;
    doc["objectStates"] = objectStates;
    doc["unlockedAreas"] = unlockedAreas;
    doc["flags"] = flags;
    doc["counters"] = counters;
    doc["strings"] = strings;
    
    return doc.dump();
}

bool WorldSaveData::deserialize(const std::string& data) {
    using json = nlohmann::json;
    
    try {
        json doc = json::parse(data);
        
        gameTime = doc.value("gameTime", 0.0f);
        dayCount = doc.value("dayCount", 1);
        weatherState = doc.value("weatherState", "");
        
        if (doc.contains("destroyedPersistentIds")) {
            destroyedPersistentIds = doc["destroyedPersistentIds"].get<std::vector<std::string>>();
        }
        if (doc.contains("spawnedEntityData")) {
            spawnedEntityData = doc["spawnedEntityData"].get<std::vector<std::string>>();
        }
        if (doc.contains("objectStates")) {
            objectStates = doc["objectStates"].get<std::unordered_map<std::string, std::string>>();
        }
        if (doc.contains("unlockedAreas")) {
            unlockedAreas = doc["unlockedAreas"].get<std::vector<std::string>>();
        }
        if (doc.contains("flags")) {
            flags = doc["flags"].get<std::unordered_map<std::string, bool>>();
        }
        if (doc.contains("counters")) {
            counters = doc["counters"].get<std::unordered_map<std::string, int>>();
        }
        if (doc.contains("strings")) {
            strings = doc["strings"].get<std::unordered_map<std::string, std::string>>();
        }
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace Sanic
