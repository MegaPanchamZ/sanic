/**
 * QuestSystem.cpp
 * 
 * Quest and Objective Tracking System Implementation
 */

#include "QuestSystem.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <sstream>

namespace Sanic {

// ============================================================================
// QUEST IMPLEMENTATION
// ============================================================================

Quest::Quest(const QuestID& id)
    : id(id)
{
}

QuestObjective* Quest::getObjective(const ObjectiveID& objId) {
    for (auto& obj : objectives) {
        if (obj.id == objId) return &obj;
    }
    return nullptr;
}

const QuestObjective* Quest::getObjective(const ObjectiveID& objId) const {
    for (const auto& obj : objectives) {
        if (obj.id == objId) return &obj;
    }
    return nullptr;
}

std::vector<QuestObjective*> Quest::getActiveObjectives() {
    std::vector<QuestObjective*> result;
    for (auto& obj : objectives) {
        if (obj.state == ObjectiveState::Active) {
            result.push_back(&obj);
        }
    }
    return result;
}

bool Quest::areRequiredObjectivesComplete() const {
    for (const auto& obj : objectives) {
        if (!obj.isOptional && obj.state != ObjectiveState::Completed) {
            return false;
        }
    }
    return true;
}

float Quest::getProgress() const {
    if (objectives.empty()) return 0.0f;
    
    int completed = 0;
    int required = 0;
    
    for (const auto& obj : objectives) {
        if (!obj.isOptional) {
            required++;
            if (obj.state == ObjectiveState::Completed) {
                completed++;
            }
        }
    }
    
    if (required == 0) return 1.0f;
    return static_cast<float>(completed) / static_cast<float>(required);
}

bool Quest::checkPrerequisites(const QuestManager& manager) const {
    // Check level
    if (manager.getPlayerLevel() < requiredLevel) {
        return false;
    }
    
    // Check prerequisite quests
    for (const auto& prereqId : prerequisiteQuests) {
        if (!manager.wasQuestCompleted(prereqId)) {
            return false;
        }
    }
    
    // Check reputation
    for (const auto& [factionId, required] : requiredReputation) {
        if (manager.getReputation(factionId) < required) {
            return false;
        }
    }
    
    // Check custom prerequisite
    if (customPrerequisite && !customPrerequisite()) {
        return false;
    }
    
    return true;
}

// ============================================================================
// QUEST MANAGER IMPLEMENTATION
// ============================================================================

QuestManager::QuestManager() {
}

void QuestManager::registerQuest(std::unique_ptr<Quest> quest) {
    QuestID id = quest->id;
    quests_[id] = std::move(quest);
    checkQuestAvailability();
}

Quest* QuestManager::getQuest(const QuestID& id) {
    auto it = quests_.find(id);
    return it != quests_.end() ? it->second.get() : nullptr;
}

const Quest* QuestManager::getQuest(const QuestID& id) const {
    auto it = quests_.find(id);
    return it != quests_.end() ? it->second.get() : nullptr;
}

bool QuestManager::acceptQuest(const QuestID& id) {
    Quest* quest = getQuest(id);
    if (!quest) return false;
    
    if (quest->state != QuestState::Available) return false;
    if (!quest->checkPrerequisites(*this)) return false;
    
    quest->state = QuestState::Active;
    quest->startTime = 0.0f;  // Would get current game time
    
    // Activate initial objectives
    activateNextObjectives(*quest);
    
    // Fire callback
    if (quest->onAccepted) quest->onAccepted();
    if (onQuestAccepted_) onQuestAccepted_(*quest);
    
    // Auto-track if first quest
    if (trackedQuestId_.empty()) {
        trackedQuestId_ = id;
        quest->isTracked = true;
    }
    
    return true;
}

bool QuestManager::abandonQuest(const QuestID& id) {
    Quest* quest = getQuest(id);
    if (!quest) return false;
    
    if (quest->state != QuestState::Active) return false;
    
    quest->state = QuestState::Available;
    
    // Reset objectives
    for (auto& obj : quest->objectives) {
        obj.state = ObjectiveState::Inactive;
        obj.currentCount = 0;
        obj.elapsedTime = 0.0f;
    }
    
    if (quest->onAbandoned) quest->onAbandoned();
    
    if (trackedQuestId_ == id) {
        trackedQuestId_.clear();
        quest->isTracked = false;
    }
    
    return true;
}

bool QuestManager::completeQuest(const QuestID& id) {
    Quest* quest = getQuest(id);
    if (!quest) return false;
    
    if (quest->state != QuestState::Completed) {
        // Check if can be completed
        if (quest->state != QuestState::Active) return false;
        if (!quest->areRequiredObjectivesComplete()) return false;
        
        quest->state = QuestState::Completed;
    }
    
    // Give rewards
    giveRewards(*quest);
    
    quest->state = QuestState::TurnedIn;
    quest->lastCompletionTime = 0.0f;  // Would get current game time
    completedQuests_.insert(id);
    
    // Fire callback
    if (quest->onCompleted) quest->onCompleted();
    if (onQuestCompleted_) onQuestCompleted_(*quest);
    
    // Unlock next quest in chain
    if (!quest->nextQuest.empty()) {
        Quest* next = getQuest(quest->nextQuest);
        if (next) {
            next->state = QuestState::Available;
        }
    }
    
    // Update availability
    checkQuestAvailability();
    
    // Update tracking
    if (trackedQuestId_ == id) {
        trackedQuestId_.clear();
        quest->isTracked = false;
        
        // Auto-track another active quest
        auto active = getActiveQuests();
        if (!active.empty()) {
            setTrackedQuest(active[0]->id);
        }
    }
    
    return true;
}

bool QuestManager::failQuest(const QuestID& id) {
    Quest* quest = getQuest(id);
    if (!quest) return false;
    
    if (quest->state != QuestState::Active) return false;
    
    quest->state = QuestState::Failed;
    
    if (quest->onFailed) quest->onFailed();
    if (onQuestFailed_) onQuestFailed_(*quest);
    
    return true;
}

void QuestManager::processEvent(const QuestEvent& event) {
    for (auto& [id, quest] : quests_) {
        if (quest->state != QuestState::Active) continue;
        
        for (auto& obj : quest->objectives) {
            if (obj.state != ObjectiveState::Active) continue;
            
            bool matches = false;
            
            switch (event.type) {
                case QuestEvent::Type::EnemyKilled:
                    matches = (obj.type == ObjectiveType::Kill && 
                              obj.targetId == event.targetId);
                    break;
                    
                case QuestEvent::Type::ItemCollected:
                    matches = (obj.type == ObjectiveType::Collect && 
                              obj.targetId == event.targetId);
                    break;
                    
                case QuestEvent::Type::NpcTalkedTo:
                    matches = (obj.type == ObjectiveType::Talk && 
                              obj.targetId == event.targetId);
                    break;
                    
                case QuestEvent::Type::LocationReached:
                    if (obj.type == ObjectiveType::GoTo || 
                        obj.type == ObjectiveType::Discover) {
                        if (obj.hasLocation) {
                            float dist = glm::distance(event.location, obj.location);
                            matches = dist <= obj.locationRadius;
                        } else {
                            matches = obj.targetId == event.targetId;
                        }
                    }
                    break;
                    
                case QuestEvent::Type::ObjectInteracted:
                    matches = (obj.type == ObjectiveType::Interact && 
                              obj.targetId == event.targetId);
                    break;
                    
                case QuestEvent::Type::ItemCrafted:
                    matches = (obj.type == ObjectiveType::Craft && 
                              obj.targetId == event.targetId);
                    break;
                    
                case QuestEvent::Type::Custom:
                    if (obj.type == ObjectiveType::Custom && obj.customCondition) {
                        matches = obj.customCondition();
                    }
                    break;
                    
                default:
                    break;
            }
            
            if (matches) {
                obj.currentCount += event.count;
                
                if (onObjectiveProgress_) {
                    onObjectiveProgress_(*quest, obj);
                }
                
                if (obj.currentCount >= obj.requiredCount) {
                    obj.state = ObjectiveState::Completed;
                    
                    if (onObjectiveCompleted_) {
                        onObjectiveCompleted_(*quest, obj);
                    }
                    
                    // Activate next objectives
                    activateNextObjectives(*quest);
                    
                    // Check quest completion
                    updateQuestState(*quest);
                }
            }
        }
    }
}

void QuestManager::updateObjective(const QuestID& questId, const ObjectiveID& objectiveId, 
                                    int progress) {
    Quest* quest = getQuest(questId);
    if (!quest || quest->state != QuestState::Active) return;
    
    QuestObjective* obj = quest->getObjective(objectiveId);
    if (!obj || obj->state != ObjectiveState::Active) return;
    
    obj->currentCount = progress;
    
    if (onObjectiveProgress_) {
        onObjectiveProgress_(*quest, *obj);
    }
    
    if (obj->currentCount >= obj->requiredCount) {
        obj->state = ObjectiveState::Completed;
        
        if (onObjectiveCompleted_) {
            onObjectiveCompleted_(*quest, *obj);
        }
        
        activateNextObjectives(*quest);
        updateQuestState(*quest);
    }
}

void QuestManager::completeObjective(const QuestID& questId, const ObjectiveID& objectiveId) {
    Quest* quest = getQuest(questId);
    if (!quest || quest->state != QuestState::Active) return;
    
    QuestObjective* obj = quest->getObjective(objectiveId);
    if (!obj) return;
    
    obj->currentCount = obj->requiredCount;
    obj->state = ObjectiveState::Completed;
    
    if (onObjectiveCompleted_) {
        onObjectiveCompleted_(*quest, *obj);
    }
    
    activateNextObjectives(*quest);
    updateQuestState(*quest);
}

void QuestManager::update(float deltaTime) {
    for (auto& [id, quest] : quests_) {
        if (quest->state != QuestState::Active) continue;
        
        // Update quest time limit
        if (quest->timeLimit > 0.0f) {
            quest->startTime += deltaTime;
            if (quest->startTime >= quest->timeLimit) {
                failQuest(id);
                continue;
            }
        }
        
        // Update objective time limits
        for (auto& obj : quest->objectives) {
            if (obj.state != ObjectiveState::Active) continue;
            
            if (obj.timeLimit > 0.0f) {
                obj.elapsedTime += deltaTime;
                if (obj.elapsedTime >= obj.timeLimit) {
                    obj.state = ObjectiveState::Failed;
                    
                    // Check if this fails the quest
                    if (!obj.isOptional) {
                        failQuest(id);
                    }
                }
            }
        }
    }
}

std::vector<Quest*> QuestManager::getQuestsByState(QuestState state) {
    std::vector<Quest*> result;
    for (auto& [id, quest] : quests_) {
        if (quest->state == state) {
            result.push_back(quest.get());
        }
    }
    return result;
}

std::vector<const Quest*> QuestManager::getQuestsByState(QuestState state) const {
    std::vector<const Quest*> result;
    for (const auto& [id, quest] : quests_) {
        if (quest->state == state) {
            result.push_back(quest.get());
        }
    }
    return result;
}

std::vector<Quest*> QuestManager::getActiveQuests() {
    return getQuestsByState(QuestState::Active);
}

std::vector<Quest*> QuestManager::getAvailableQuests() {
    return getQuestsByState(QuestState::Available);
}

std::vector<Quest*> QuestManager::getQuestsForNpc(const std::string& npcId) {
    std::vector<Quest*> result;
    
    for (auto& [id, quest] : quests_) {
        bool isGiver = quest->questGiverNpcId == npcId;
        bool isTurnIn = quest->turnInNpcId.empty() ? 
            (quest->questGiverNpcId == npcId) : 
            (quest->turnInNpcId == npcId);
        
        if (quest->state == QuestState::Available && isGiver) {
            result.push_back(quest.get());
        } else if ((quest->state == QuestState::Completed || 
                    quest->areRequiredObjectivesComplete()) && isTurnIn) {
            result.push_back(quest.get());
        }
    }
    
    return result;
}

bool QuestManager::isQuestComplete(const QuestID& id) const {
    const Quest* quest = getQuest(id);
    return quest && (quest->state == QuestState::Completed || 
                     quest->state == QuestState::TurnedIn);
}

bool QuestManager::isQuestActive(const QuestID& id) const {
    const Quest* quest = getQuest(id);
    return quest && quest->state == QuestState::Active;
}

bool QuestManager::wasQuestCompleted(const QuestID& id) const {
    return completedQuests_.find(id) != completedQuests_.end();
}

void QuestManager::setTrackedQuest(const QuestID& id) {
    // Untrack previous
    if (!trackedQuestId_.empty()) {
        Quest* prev = getQuest(trackedQuestId_);
        if (prev) prev->isTracked = false;
    }
    
    trackedQuestId_ = id;
    
    Quest* quest = getQuest(id);
    if (quest) {
        quest->isTracked = true;
    }
}

Quest* QuestManager::getTrackedQuest() {
    return getQuest(trackedQuestId_);
}

std::string QuestManager::saveState() const {
    using json = nlohmann::json;
    
    json doc;
    doc["playerLevel"] = playerLevel_;
    doc["trackedQuest"] = trackedQuestId_;
    doc["completedQuests"] = json::array();
    
    for (const auto& id : completedQuests_) {
        doc["completedQuests"].push_back(id);
    }
    
    doc["reputation"] = json::object();
    for (const auto& [faction, value] : reputation_) {
        doc["reputation"][faction] = value;
    }
    
    doc["quests"] = json::array();
    for (const auto& [id, quest] : quests_) {
        json q;
        q["id"] = quest->id;
        q["state"] = static_cast<int>(quest->state);
        q["startTime"] = quest->startTime;
        q["lastCompletion"] = quest->lastCompletionTime;
        
        q["objectives"] = json::array();
        for (const auto& obj : quest->objectives) {
            json o;
            o["id"] = obj.id;
            o["state"] = static_cast<int>(obj.state);
            o["currentCount"] = obj.currentCount;
            o["elapsedTime"] = obj.elapsedTime;
            q["objectives"].push_back(o);
        }
        
        doc["quests"].push_back(q);
    }
    
    return doc.dump(2);
}

void QuestManager::loadState(const std::string& jsonStr) {
    using json = nlohmann::json;
    
    try {
        json doc = json::parse(jsonStr);
        
        playerLevel_ = doc.value("playerLevel", 1);
        trackedQuestId_ = doc.value("trackedQuest", "");
        
        completedQuests_.clear();
        if (doc.contains("completedQuests")) {
            for (const auto& id : doc["completedQuests"]) {
                completedQuests_.insert(id.get<std::string>());
            }
        }
        
        reputation_.clear();
        if (doc.contains("reputation")) {
            for (auto& [key, value] : doc["reputation"].items()) {
                reputation_[key] = value.get<int>();
            }
        }
        
        if (doc.contains("quests")) {
            for (const auto& q : doc["quests"]) {
                std::string id = q.value("id", "");
                Quest* quest = getQuest(id);
                if (!quest) continue;
                
                quest->state = static_cast<QuestState>(q.value("state", 0));
                quest->startTime = q.value("startTime", 0.0f);
                quest->lastCompletionTime = q.value("lastCompletion", -1000.0f);
                quest->isTracked = (id == trackedQuestId_);
                
                if (q.contains("objectives")) {
                    for (const auto& o : q["objectives"]) {
                        std::string objId = o.value("id", "");
                        QuestObjective* obj = quest->getObjective(objId);
                        if (!obj) continue;
                        
                        obj->state = static_cast<ObjectiveState>(o.value("state", 0));
                        obj->currentCount = o.value("currentCount", 0);
                        obj->elapsedTime = o.value("elapsedTime", 0.0f);
                    }
                }
            }
        }
        
    } catch (const std::exception&) {
        // Handle parse error
    }
}

void QuestManager::checkQuestAvailability() {
    for (auto& [id, quest] : quests_) {
        if (quest->state == QuestState::Unavailable) {
            if (quest->checkPrerequisites(*this)) {
                quest->state = QuestState::Available;
            }
        }
    }
}

void QuestManager::updateQuestState(Quest& quest) {
    if (quest.state != QuestState::Active) return;
    
    if (quest.areRequiredObjectivesComplete()) {
        quest.state = QuestState::Completed;
    }
}

void QuestManager::activateNextObjectives(Quest& quest) {
    for (auto& obj : quest.objectives) {
        if (obj.state != ObjectiveState::Inactive) continue;
        
        // Check prerequisites
        bool allPrereqsMet = true;
        for (const auto& prereqId : obj.prerequisites) {
            const QuestObjective* prereq = quest.getObjective(prereqId);
            if (!prereq || prereq->state != ObjectiveState::Completed) {
                allPrereqsMet = false;
                break;
            }
        }
        
        if (allPrereqsMet) {
            obj.state = ObjectiveState::Active;
        }
    }
}

void QuestManager::giveRewards(const Quest& quest) {
    const QuestReward& reward = quest.reward;
    
    // Would integrate with player inventory/stats
    // For now, just log
    
    if (reward.customReward) {
        reward.customReward();
    }
}

// ============================================================================
// QUEST GIVER COMPONENT IMPLEMENTATION
// ============================================================================

QuestGiverComponent::MarkerType QuestGiverComponent::getMarkerType(
    const QuestManager& manager) const {
    
    // Check for quests ready to turn in
    for (const auto& questId : turnInQuests) {
        const Quest* quest = manager.getQuest(questId);
        if (quest && (quest->state == QuestState::Completed ||
            (quest->state == QuestState::Active && quest->areRequiredObjectivesComplete()))) {
            return MarkerType::ReadyToTurnIn;
        }
    }
    
    // Check for available quests
    for (const auto& questId : offeredQuests) {
        const Quest* quest = manager.getQuest(questId);
        if (quest && quest->state == QuestState::Available) {
            return MarkerType::Available;
        }
    }
    
    // Check for in-progress quests
    for (const auto& questId : offeredQuests) {
        const Quest* quest = manager.getQuest(questId);
        if (quest && quest->state == QuestState::Active) {
            return MarkerType::InProgress;
        }
    }
    
    return MarkerType::None;
}

// ============================================================================
// QUEST SYSTEM IMPLEMENTATION
// ============================================================================

QuestSystem::QuestSystem() {
}

void QuestSystem::init(World& world) {
}

void QuestSystem::update(World& world, float deltaTime) {
    manager_.update(deltaTime);
}

void QuestSystem::shutdown(World& world) {
}

void QuestSystem::onEntityKilled(Entity entity, Entity killer) {
    auto* target = manager_.getQuest("")->objectives[0].targetId.c_str();
    // Would get entity type from component
    
    QuestEvent event;
    event.type = QuestEvent::Type::EnemyKilled;
    // event.targetId = entityType;
    event.count = 1;
    event.sourceEntity = killer;
    
    manager_.processEvent(event);
}

void QuestSystem::onItemCollected(const std::string& itemId, int count) {
    QuestEvent event;
    event.type = QuestEvent::Type::ItemCollected;
    event.targetId = itemId;
    event.count = count;
    
    manager_.processEvent(event);
}

void QuestSystem::onNpcInteraction(const std::string& npcId) {
    QuestEvent event;
    event.type = QuestEvent::Type::NpcTalkedTo;
    event.targetId = npcId;
    event.count = 1;
    
    manager_.processEvent(event);
}

void QuestSystem::onLocationReached(const std::string& locationId, const glm::vec3& position) {
    QuestEvent event;
    event.type = QuestEvent::Type::LocationReached;
    event.targetId = locationId;
    event.location = position;
    
    manager_.processEvent(event);
}

// ============================================================================
// QUEST BUILDER IMPLEMENTATION
// ============================================================================

QuestBuilder::QuestBuilder(const QuestID& id)
    : quest_(std::make_unique<Quest>(id))
{
}

QuestBuilder& QuestBuilder::title(const std::string& t) {
    quest_->title = t;
    return *this;
}

QuestBuilder& QuestBuilder::description(const std::string& d) {
    quest_->description = d;
    return *this;
}

QuestBuilder& QuestBuilder::category(const std::string& c) {
    quest_->category = c;
    return *this;
}

QuestBuilder& QuestBuilder::questGiver(const std::string& npcId) {
    quest_->questGiverNpcId = npcId;
    return *this;
}

QuestBuilder& QuestBuilder::turnIn(const std::string& npcId) {
    quest_->turnInNpcId = npcId;
    return *this;
}

QuestBuilder& QuestBuilder::prerequisite(const QuestID& questId) {
    quest_->prerequisiteQuests.push_back(questId);
    return *this;
}

QuestBuilder& QuestBuilder::requireLevel(int level) {
    quest_->requiredLevel = level;
    return *this;
}

QuestBuilder& QuestBuilder::requireReputation(const std::string& faction, int value) {
    quest_->requiredReputation[faction] = value;
    return *this;
}

QuestBuilder& QuestBuilder::rewardXP(int xp) {
    quest_->reward.experience = xp;
    return *this;
}

QuestBuilder& QuestBuilder::rewardGold(int gold) {
    quest_->reward.gold = gold;
    return *this;
}

QuestBuilder& QuestBuilder::rewardItem(const std::string& itemId, int count) {
    quest_->reward.items[itemId] = count;
    return *this;
}

QuestBuilder& QuestBuilder::rewardReputation(const std::string& faction, int value) {
    quest_->reward.reputation[faction] = value;
    return *this;
}

QuestBuilder& QuestBuilder::killObjective(const ObjectiveID& id, const std::string& enemyType,
                                           int count, const std::string& description) {
    QuestObjective obj;
    obj.id = id;
    obj.type = ObjectiveType::Kill;
    obj.targetId = enemyType;
    obj.requiredCount = count;
    obj.description = description;
    obj.order = static_cast<int>(quest_->objectives.size());
    
    quest_->objectives.push_back(obj);
    currentObjective_ = &quest_->objectives.back();
    
    return *this;
}

QuestBuilder& QuestBuilder::collectObjective(const ObjectiveID& id, const std::string& itemId,
                                              int count, const std::string& description) {
    QuestObjective obj;
    obj.id = id;
    obj.type = ObjectiveType::Collect;
    obj.targetId = itemId;
    obj.requiredCount = count;
    obj.description = description;
    obj.order = static_cast<int>(quest_->objectives.size());
    
    quest_->objectives.push_back(obj);
    currentObjective_ = &quest_->objectives.back();
    
    return *this;
}

QuestBuilder& QuestBuilder::talkObjective(const ObjectiveID& id, const std::string& npcId,
                                           const std::string& description) {
    QuestObjective obj;
    obj.id = id;
    obj.type = ObjectiveType::Talk;
    obj.targetId = npcId;
    obj.requiredCount = 1;
    obj.description = description;
    obj.order = static_cast<int>(quest_->objectives.size());
    
    quest_->objectives.push_back(obj);
    currentObjective_ = &quest_->objectives.back();
    
    return *this;
}

QuestBuilder& QuestBuilder::gotoObjective(const ObjectiveID& id, const glm::vec3& location,
                                           float radius, const std::string& description) {
    QuestObjective obj;
    obj.id = id;
    obj.type = ObjectiveType::GoTo;
    obj.hasLocation = true;
    obj.location = location;
    obj.locationRadius = radius;
    obj.requiredCount = 1;
    obj.description = description;
    obj.order = static_cast<int>(quest_->objectives.size());
    
    quest_->objectives.push_back(obj);
    currentObjective_ = &quest_->objectives.back();
    
    return *this;
}

QuestBuilder& QuestBuilder::interactObjective(const ObjectiveID& id, const std::string& objectId,
                                               const std::string& description) {
    QuestObjective obj;
    obj.id = id;
    obj.type = ObjectiveType::Interact;
    obj.targetId = objectId;
    obj.requiredCount = 1;
    obj.description = description;
    obj.order = static_cast<int>(quest_->objectives.size());
    
    quest_->objectives.push_back(obj);
    currentObjective_ = &quest_->objectives.back();
    
    return *this;
}

QuestBuilder& QuestBuilder::optionalObjective() {
    if (currentObjective_) {
        currentObjective_->isOptional = true;
    }
    return *this;
}

QuestBuilder& QuestBuilder::hiddenObjective() {
    if (currentObjective_) {
        currentObjective_->isHidden = true;
    }
    return *this;
}

QuestBuilder& QuestBuilder::objectivePrerequisite(const ObjectiveID& prereq) {
    if (currentObjective_) {
        currentObjective_->prerequisites.push_back(prereq);
    }
    return *this;
}

QuestBuilder& QuestBuilder::objectiveTimeLimit(float seconds) {
    if (currentObjective_) {
        currentObjective_->timeLimit = seconds;
    }
    return *this;
}

QuestBuilder& QuestBuilder::repeatable(float cooldown) {
    quest_->isRepeatable = true;
    quest_->repeatCooldown = cooldown;
    return *this;
}

QuestBuilder& QuestBuilder::timeLimit(float seconds) {
    quest_->timeLimit = seconds;
    return *this;
}

QuestBuilder& QuestBuilder::priority(int p) {
    quest_->priority = p;
    return *this;
}

QuestBuilder& QuestBuilder::onAccept(std::function<void()> callback) {
    quest_->onAccepted = callback;
    return *this;
}

QuestBuilder& QuestBuilder::onComplete(std::function<void()> callback) {
    quest_->onCompleted = callback;
    return *this;
}

QuestBuilder& QuestBuilder::onFail(std::function<void()> callback) {
    quest_->onFailed = callback;
    return *this;
}

std::unique_ptr<Quest> QuestBuilder::build() {
    // Activate first objectives (those with no prerequisites)
    for (auto& obj : quest_->objectives) {
        if (obj.prerequisites.empty()) {
            obj.state = ObjectiveState::Active;
        }
    }
    
    return std::move(quest_);
}

// ============================================================================
// EXAMPLE QUEST
// ============================================================================

inline std::unique_ptr<Quest> createExampleQuest() {
    QuestBuilder builder("main_01_rats");
    
    return builder
        .title("A Rat Problem")
        .description("The innkeeper has asked you to clear the cellar of giant rats.")
        .category("Main")
        .questGiver("innkeeper_tom")
        
        .rewardXP(100)
        .rewardGold(50)
        .rewardItem("health_potion", 2)
        
        .talkObjective("talk_innkeeper", "innkeeper_tom", 
                       "Talk to the innkeeper about the rat problem")
        
        .gotoObjective("enter_cellar", glm::vec3(10, -5, 20), 3.0f,
                       "Enter the cellar")
        .objectivePrerequisite("talk_innkeeper")
        
        .killObjective("kill_rats", "giant_rat", 5,
                       "Kill the giant rats (0/5)")
        .objectivePrerequisite("enter_cellar")
        
        .killObjective("kill_boss", "rat_king", 1,
                       "Defeat the Rat King")
        .objectivePrerequisite("kill_rats")
        
        .collectObjective("loot_key", "cellar_key", 1,
                          "Retrieve the stolen key from the Rat King")
        .objectivePrerequisite("kill_boss")
        
        .talkObjective("return_innkeeper", "innkeeper_tom",
                       "Return to the innkeeper")
        .objectivePrerequisite("loot_key")
        
        .onComplete([]() {
            // Could trigger cutscene, unlock shop, etc.
        })
        
        .build();
}

} // namespace Sanic
