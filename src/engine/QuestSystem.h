/**
 * QuestSystem.h
 * 
 * Quest and Objective Tracking System
 * 
 * Features:
 * - Quest objectives with multiple types
 * - Quest chains and prerequisites
 * - Rewards (items, XP, reputation)
 * - Quest tracking UI integration
 * - Save/Load quest state
 * - Quest markers on map
 * 
 * Reference:
 *   Engine/Plugins/Runtime/Quest/
 */

#pragma once

#include "ECS.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace Sanic {

// Forward declarations
class QuestManager;

// ============================================================================
// QUEST TYPES
// ============================================================================

/**
 * Unique identifier for quests
 */
using QuestID = std::string;
using ObjectiveID = std::string;

/**
 * Quest state
 */
enum class QuestState {
    Unavailable,    // Prerequisites not met
    Available,      // Can be accepted
    Active,         // Currently in progress
    Completed,      // All objectives done
    TurnedIn,       // Rewards claimed
    Failed          // Quest failed
};

/**
 * Objective state
 */
enum class ObjectiveState {
    Inactive,       // Not yet active
    Active,         // In progress
    Completed,      // Done
    Failed          // Failed
};

/**
 * Types of objectives
 */
enum class ObjectiveType {
    Kill,           // Kill N enemies of type X
    Collect,        // Collect N items of type X
    Talk,           // Talk to NPC
    GoTo,           // Go to location
    Escort,         // Escort NPC to location
    Defend,         // Defend location for time
    Interact,       // Interact with object
    Discover,       // Discover location
    Craft,          // Craft item
    Custom          // Custom condition
};

// ============================================================================
// QUEST REWARD
// ============================================================================

/**
 * Reward given upon quest completion
 */
struct QuestReward {
    // Experience
    int experience = 0;
    
    // Currency
    int gold = 0;
    
    // Items (item ID -> count)
    std::unordered_map<std::string, int> items;
    
    // Reputation changes (faction ID -> change)
    std::unordered_map<std::string, int> reputation;
    
    // Unlocks
    std::vector<std::string> unlockedAbilities;
    std::vector<std::string> unlockedRecipes;
    std::vector<QuestID> unlockedQuests;
    
    // Custom reward callback
    std::function<void()> customReward;
};

// ============================================================================
// QUEST OBJECTIVE
// ============================================================================

/**
 * A single objective within a quest
 */
struct QuestObjective {
    ObjectiveID id;
    ObjectiveType type = ObjectiveType::Custom;
    ObjectiveState state = ObjectiveState::Inactive;
    
    // Description
    std::string title;
    std::string description;
    
    // Target (meaning depends on type)
    std::string targetId;      // NPC ID, item ID, location ID, etc.
    std::string targetName;    // Display name
    
    // Progress
    int requiredCount = 1;
    int currentCount = 0;
    
    // Location hint
    bool hasLocation = false;
    glm::vec3 location = glm::vec3(0);
    float locationRadius = 5.0f;
    std::string locationName;
    
    // Optional objectives
    bool isOptional = false;
    bool isHidden = false;     // Not shown until conditions met
    
    // Ordering
    int order = 0;             // Display order
    std::vector<ObjectiveID> prerequisites;  // Must complete these first
    
    // Timing
    float timeLimit = 0.0f;    // 0 = no limit
    float elapsedTime = 0.0f;
    
    // Custom condition
    std::function<bool()> customCondition;
    
    /**
     * Check if objective is complete
     */
    bool isComplete() const {
        return state == ObjectiveState::Completed;
    }
    
    /**
     * Get progress as fraction
     */
    float getProgress() const {
        if (requiredCount <= 0) return state == ObjectiveState::Completed ? 1.0f : 0.0f;
        return static_cast<float>(currentCount) / static_cast<float>(requiredCount);
    }
    
    /**
     * Get progress text
     */
    std::string getProgressText() const {
        if (requiredCount <= 1) return "";
        return std::to_string(currentCount) + "/" + std::to_string(requiredCount);
    }
};

// ============================================================================
// QUEST
// ============================================================================

/**
 * A complete quest
 */
class Quest {
public:
    Quest(const QuestID& id = "");
    
    // Identity
    QuestID id;
    std::string title;
    std::string description;
    std::string category;      // Main, Side, Daily, etc.
    
    // State
    QuestState state = QuestState::Unavailable;
    
    // Objectives
    std::vector<QuestObjective> objectives;
    
    // Rewards
    QuestReward reward;
    
    // Prerequisites
    std::vector<QuestID> prerequisiteQuests;
    int requiredLevel = 0;
    std::unordered_map<std::string, int> requiredReputation;
    std::function<bool()> customPrerequisite;
    
    // Quest giver/turn-in
    std::string questGiverNpcId;
    std::string turnInNpcId;    // Empty = same as quest giver
    
    // Quest chain
    QuestID previousQuest;      // Part of a chain
    QuestID nextQuest;          // Unlocked after completion
    
    // Repeatable
    bool isRepeatable = false;
    float repeatCooldown = 0.0f;
    float lastCompletionTime = -1000.0f;
    
    // Time limit for entire quest
    float timeLimit = 0.0f;
    float startTime = 0.0f;
    
    // Importance
    int priority = 0;          // Higher = more important
    bool isTracked = false;    // Show on HUD
    
    // UI
    std::string iconPath;
    glm::vec4 color = glm::vec4(1.0f);
    
    // Callbacks
    std::function<void()> onAccepted;
    std::function<void()> onCompleted;
    std::function<void()> onFailed;
    std::function<void()> onAbandoned;
    
    /**
     * Get objective by ID
     */
    QuestObjective* getObjective(const ObjectiveID& objId);
    const QuestObjective* getObjective(const ObjectiveID& objId) const;
    
    /**
     * Get all active objectives
     */
    std::vector<QuestObjective*> getActiveObjectives();
    
    /**
     * Check if all required objectives are complete
     */
    bool areRequiredObjectivesComplete() const;
    
    /**
     * Get overall progress
     */
    float getProgress() const;
    
    /**
     * Check prerequisites
     */
    bool checkPrerequisites(const QuestManager& manager) const;
};

// ============================================================================
// QUEST EVENTS
// ============================================================================

/**
 * Events that can affect quest progress
 */
struct QuestEvent {
    enum class Type {
        EnemyKilled,
        ItemCollected,
        ItemUsed,
        NpcTalkedTo,
        LocationReached,
        ObjectInteracted,
        LocationDiscovered,
        ItemCrafted,
        Custom
    } type;
    
    std::string targetId;      // What was affected
    int count = 1;             // How many
    glm::vec3 location;        // Where it happened
    Entity sourceEntity = INVALID_ENTITY;
    
    // Additional data
    std::unordered_map<std::string, std::string> metadata;
};

// ============================================================================
// QUEST MANAGER
// ============================================================================

/**
 * Manages all quests in the game
 */
class QuestManager {
public:
    using QuestCallback = std::function<void(const Quest&)>;
    using ObjectiveCallback = std::function<void(const Quest&, const QuestObjective&)>;
    
    QuestManager();
    
    /**
     * Register a quest definition
     */
    void registerQuest(std::unique_ptr<Quest> quest);
    
    /**
     * Get quest by ID
     */
    Quest* getQuest(const QuestID& id);
    const Quest* getQuest(const QuestID& id) const;
    
    /**
     * Accept a quest
     */
    bool acceptQuest(const QuestID& id);
    
    /**
     * Abandon a quest
     */
    bool abandonQuest(const QuestID& id);
    
    /**
     * Complete a quest (turn in)
     */
    bool completeQuest(const QuestID& id);
    
    /**
     * Fail a quest
     */
    bool failQuest(const QuestID& id);
    
    /**
     * Process a quest event
     */
    void processEvent(const QuestEvent& event);
    
    /**
     * Update objective progress directly
     */
    void updateObjective(const QuestID& questId, const ObjectiveID& objectiveId, int progress);
    
    /**
     * Complete objective directly
     */
    void completeObjective(const QuestID& questId, const ObjectiveID& objectiveId);
    
    /**
     * Update (for time-based objectives)
     */
    void update(float deltaTime);
    
    /**
     * Get quests by state
     */
    std::vector<Quest*> getQuestsByState(QuestState state);
    std::vector<const Quest*> getQuestsByState(QuestState state) const;
    
    /**
     * Get active quests
     */
    std::vector<Quest*> getActiveQuests();
    
    /**
     * Get available quests
     */
    std::vector<Quest*> getAvailableQuests();
    
    /**
     * Get quests for an NPC (available or ready to turn in)
     */
    std::vector<Quest*> getQuestsForNpc(const std::string& npcId);
    
    /**
     * Check if quest is complete
     */
    bool isQuestComplete(const QuestID& id) const;
    
    /**
     * Check if quest is active
     */
    bool isQuestActive(const QuestID& id) const;
    
    /**
     * Check if quest was ever completed
     */
    bool wasQuestCompleted(const QuestID& id) const;
    
    /**
     * Set tracked quest
     */
    void setTrackedQuest(const QuestID& id);
    
    /**
     * Get tracked quest
     */
    Quest* getTrackedQuest();
    
    // Callbacks
    void setOnQuestAccepted(QuestCallback callback) { onQuestAccepted_ = callback; }
    void setOnQuestCompleted(QuestCallback callback) { onQuestCompleted_ = callback; }
    void setOnQuestFailed(QuestCallback callback) { onQuestFailed_ = callback; }
    void setOnObjectiveCompleted(ObjectiveCallback callback) { onObjectiveCompleted_ = callback; }
    void setOnObjectiveProgress(ObjectiveCallback callback) { onObjectiveProgress_ = callback; }
    
    /**
     * Save state to JSON
     */
    std::string saveState() const;
    
    /**
     * Load state from JSON
     */
    void loadState(const std::string& json);
    
    /**
     * Get player level (for prerequisites)
     */
    void setPlayerLevel(int level) { playerLevel_ = level; }
    int getPlayerLevel() const { return playerLevel_; }
    
    /**
     * Get/set reputation
     */
    void setReputation(const std::string& factionId, int value) { reputation_[factionId] = value; }
    int getReputation(const std::string& factionId) const {
        auto it = reputation_.find(factionId);
        return it != reputation_.end() ? it->second : 0;
    }
    
private:
    void checkQuestAvailability();
    void updateQuestState(Quest& quest);
    void activateNextObjectives(Quest& quest);
    void giveRewards(const Quest& quest);
    
    std::unordered_map<QuestID, std::unique_ptr<Quest>> quests_;
    std::unordered_set<QuestID> completedQuests_;  // History
    QuestID trackedQuestId_;
    
    int playerLevel_ = 1;
    std::unordered_map<std::string, int> reputation_;
    
    QuestCallback onQuestAccepted_;
    QuestCallback onQuestCompleted_;
    QuestCallback onQuestFailed_;
    ObjectiveCallback onObjectiveCompleted_;
    ObjectiveCallback onObjectiveProgress_;
};

// ============================================================================
// QUEST SYSTEM (ECS)
// ============================================================================

/**
 * Component for entities that give/receive quests
 */
struct QuestGiverComponent {
    std::vector<QuestID> offeredQuests;
    std::vector<QuestID> turnInQuests;  // Quests that can be turned in here
    
    // Visual indicators
    bool showQuestMarker = true;
    enum class MarkerType {
        None,
        Available,      // Yellow !
        InProgress,     // Gray ?
        ReadyToTurnIn   // Yellow ?
    };
    
    /**
     * Get current marker type
     */
    MarkerType getMarkerType(const QuestManager& manager) const;
};

/**
 * Component for quest target entities
 */
struct QuestTargetComponent {
    std::string targetId;      // Matches quest objective targetId
    ObjectiveType targetType = ObjectiveType::Kill;
};

/**
 * Quest system
 */
class QuestSystem : public System {
public:
    QuestSystem();
    
    void init(World& world) override;
    void update(World& world, float deltaTime) override;
    void shutdown(World& world) override;
    
    /**
     * Get the quest manager
     */
    QuestManager& getManager() { return manager_; }
    const QuestManager& getManager() const { return manager_; }
    
    /**
     * Notify of entity death (for kill objectives)
     */
    void onEntityKilled(Entity entity, Entity killer);
    
    /**
     * Notify of item pickup (for collect objectives)
     */
    void onItemCollected(const std::string& itemId, int count);
    
    /**
     * Notify of NPC interaction (for talk objectives)
     */
    void onNpcInteraction(const std::string& npcId);
    
    /**
     * Notify of location reached (for goto objectives)
     */
    void onLocationReached(const std::string& locationId, const glm::vec3& position);
    
private:
    QuestManager manager_;
};

// ============================================================================
// QUEST BUILDER
// ============================================================================

/**
 * Fluent builder for creating quests
 */
class QuestBuilder {
public:
    QuestBuilder(const QuestID& id);
    
    QuestBuilder& title(const std::string& t);
    QuestBuilder& description(const std::string& d);
    QuestBuilder& category(const std::string& c);
    
    QuestBuilder& questGiver(const std::string& npcId);
    QuestBuilder& turnIn(const std::string& npcId);
    
    QuestBuilder& prerequisite(const QuestID& questId);
    QuestBuilder& requireLevel(int level);
    QuestBuilder& requireReputation(const std::string& faction, int value);
    
    QuestBuilder& rewardXP(int xp);
    QuestBuilder& rewardGold(int gold);
    QuestBuilder& rewardItem(const std::string& itemId, int count = 1);
    QuestBuilder& rewardReputation(const std::string& faction, int value);
    
    // Objectives
    QuestBuilder& killObjective(const ObjectiveID& id, const std::string& enemyType,
                                 int count, const std::string& description);
    QuestBuilder& collectObjective(const ObjectiveID& id, const std::string& itemId,
                                    int count, const std::string& description);
    QuestBuilder& talkObjective(const ObjectiveID& id, const std::string& npcId,
                                 const std::string& description);
    QuestBuilder& gotoObjective(const ObjectiveID& id, const glm::vec3& location,
                                 float radius, const std::string& description);
    QuestBuilder& interactObjective(const ObjectiveID& id, const std::string& objectId,
                                     const std::string& description);
    
    QuestBuilder& optionalObjective();
    QuestBuilder& hiddenObjective();
    QuestBuilder& objectivePrerequisite(const ObjectiveID& prereq);
    QuestBuilder& objectiveTimeLimit(float seconds);
    
    QuestBuilder& repeatable(float cooldown = 0.0f);
    QuestBuilder& timeLimit(float seconds);
    QuestBuilder& priority(int p);
    
    QuestBuilder& onAccept(std::function<void()> callback);
    QuestBuilder& onComplete(std::function<void()> callback);
    QuestBuilder& onFail(std::function<void()> callback);
    
    std::unique_ptr<Quest> build();
    
private:
    std::unique_ptr<Quest> quest_;
    QuestObjective* currentObjective_ = nullptr;
};

} // namespace Sanic
