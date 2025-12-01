/**
 * DialogueSystem.h
 * 
 * Dialogue and Conversation System
 * 
 * Features:
 * - Branching dialogue trees
 * - Conditional responses based on game state
 * - Localization support
 * - Rich text formatting
 * - Voice acting integration
 * - Dialogue events and callbacks
 * 
 * Reference:
 *   Engine/Plugins/Runtime/Dialogue/
 */

#pragma once

#include "ECS.h"
#include "AudioSystem.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <variant>
#include <optional>

namespace Sanic {

// Forward declarations
class DialogueGraph;
class DialogueNode;
class DialogueContext;

// ============================================================================
// DIALOGUE TYPES
// ============================================================================

/**
 * Unique identifier for dialogue nodes
 */
using DialogueNodeID = uint64_t;
constexpr DialogueNodeID INVALID_DIALOGUE_NODE = 0;

/**
 * Dialogue line with localization support
 */
struct LocalizedString {
    std::string defaultText;
    std::unordered_map<std::string, std::string> translations;
    
    /**
     * Get text in specified locale
     */
    const std::string& get(const std::string& locale = "en") const {
        auto it = translations.find(locale);
        if (it != translations.end()) {
            return it->second;
        }
        return defaultText;
    }
    
    /**
     * Set text for locale
     */
    void set(const std::string& locale, const std::string& text) {
        translations[locale] = text;
    }
};

/**
 * Speaker information
 */
struct DialogueSpeaker {
    std::string id;                  // Unique identifier
    LocalizedString displayName;      // Displayed name
    std::string portraitAsset;        // Portrait image path
    glm::vec4 textColor = glm::vec4(1.0f);  // Text color for this speaker
    
    // Voice settings
    std::string voiceBank;            // Voice asset collection
    float pitch = 1.0f;
    float speed = 1.0f;
};

/**
 * A single dialogue line
 */
struct DialogueLine {
    LocalizedString text;             // The actual text
    std::string speakerId;            // Who says this
    
    // Timing
    float displayDuration = 0.0f;     // 0 = auto-calculate from text
    float typewriterSpeed = 30.0f;    // Characters per second
    
    // Audio
    std::string voiceClip;            // Voice audio asset
    std::string soundEffect;          // Optional sound effect
    
    // Rich text tags
    // Supports: <color=#FF0000>, <b>, <i>, <shake>, <wave>
    bool useRichText = true;
    
    // Animation
    std::string speakerAnimation;     // Animation to play on speaker
    std::string listenerAnimation;    // Animation to play on listener
    
    // Camera
    bool useCameraShot = false;
    std::string cameraShotName;
};

/**
 * A player response option
 */
struct DialogueResponse {
    DialogueNodeID id = 0;
    LocalizedString text;             // Response text
    DialogueNodeID nextNodeId = INVALID_DIALOGUE_NODE;
    
    // Conditions for showing this response
    std::vector<std::function<bool(const DialogueContext&)>> conditions;
    
    // Visual hints
    enum class Mood {
        Neutral,
        Friendly,
        Aggressive,
        Sarcastic,
        Romantic,
        Lie
    } mood = Mood::Neutral;
    
    // Requirements display (e.g., "[Charisma 10]")
    std::string requirementText;
    bool requirementMet = true;
    
    /**
     * Check if all conditions are met
     */
    bool canShow(const DialogueContext& context) const;
};

// ============================================================================
// DIALOGUE CONDITIONS
// ============================================================================

/**
 * Variable types for dialogue conditions
 */
using DialogueVariable = std::variant<bool, int, float, std::string>;

/**
 * Condition operators
 */
enum class DialogueOperator {
    Equal,
    NotEqual,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    Contains,      // For strings
    HasFlag,       // For flags/bits
    QuestComplete,
    QuestActive,
    ItemOwned
};

/**
 * A condition for dialogue branching
 */
struct DialogueCondition {
    std::string variableName;
    DialogueOperator op = DialogueOperator::Equal;
    DialogueVariable value;
    
    /**
     * Evaluate condition against context
     */
    bool evaluate(const DialogueContext& context) const;
};

// ============================================================================
// DIALOGUE ACTIONS
// ============================================================================

/**
 * Action types that can be triggered during dialogue
 */
enum class DialogueActionType {
    SetVariable,       // Set a dialogue variable
    GiveItem,          // Give item to player
    TakeItem,          // Remove item from player
    GiveQuest,         // Start a quest
    CompleteQuest,     // Complete quest objective
    AddReputation,     // Change faction reputation
    PlayAnimation,     // Trigger animation
    PlaySound,         // Play sound effect
    StartBattle,       // Initiate combat
    Teleport,          // Move player
    Custom             // Custom callback
};

/**
 * An action to execute during dialogue
 */
struct DialogueAction {
    DialogueActionType type = DialogueActionType::SetVariable;
    
    // Parameters (usage depends on type)
    std::string stringParam1;
    std::string stringParam2;
    int intParam = 0;
    float floatParam = 0.0f;
    
    // Custom action callback
    std::function<void(DialogueContext&)> customAction;
    
    /**
     * Execute the action
     */
    void execute(DialogueContext& context) const;
};

// ============================================================================
// DIALOGUE NODE
// ============================================================================

/**
 * Types of dialogue nodes
 */
enum class DialogueNodeType {
    Entry,           // Entry point
    Line,            // NPC speaks a line
    PlayerChoice,    // Player chooses a response
    Branch,          // Conditional branch
    Action,          // Execute actions
    Random,          // Random selection
    Exit             // End dialogue
};

/**
 * A node in the dialogue graph
 */
class DialogueNode {
public:
    DialogueNode(DialogueNodeType type = DialogueNodeType::Line);
    
    // Node identity
    DialogueNodeID id = 0;
    DialogueNodeType type = DialogueNodeType::Line;
    std::string name;  // For editor display
    
    // Content (depends on type)
    std::vector<DialogueLine> lines;           // For Line type
    std::vector<DialogueResponse> responses;   // For PlayerChoice type
    std::vector<DialogueCondition> conditions; // For Branch type
    std::vector<DialogueAction> actions;       // Actions to execute
    
    // Connections
    DialogueNodeID defaultNextNode = INVALID_DIALOGUE_NODE;
    std::vector<std::pair<DialogueCondition, DialogueNodeID>> conditionalBranches;
    
    // Editor position (for visual editor)
    glm::vec2 editorPosition = glm::vec2(0);
    
    /**
     * Get next node based on context
     */
    DialogueNodeID getNextNode(const DialogueContext& context) const;
    
    /**
     * Get available responses (filtered by conditions)
     */
    std::vector<DialogueResponse> getAvailableResponses(const DialogueContext& context) const;
};

// ============================================================================
// DIALOGUE GRAPH
// ============================================================================

/**
 * A complete dialogue tree/graph
 */
class DialogueGraph {
public:
    DialogueGraph(const std::string& name = "");
    
    /**
     * Add a node to the graph
     */
    DialogueNode& addNode(DialogueNodeType type);
    
    /**
     * Get node by ID
     */
    DialogueNode* getNode(DialogueNodeID id);
    const DialogueNode* getNode(DialogueNodeID id) const;
    
    /**
     * Remove node
     */
    void removeNode(DialogueNodeID id);
    
    /**
     * Set entry point
     */
    void setEntryNode(DialogueNodeID id) { entryNodeId_ = id; }
    DialogueNodeID getEntryNode() const { return entryNodeId_; }
    
    /**
     * Get all nodes
     */
    const std::unordered_map<DialogueNodeID, DialogueNode>& getNodes() const {
        return nodes_;
    }
    
    /**
     * Add speaker definition
     */
    void addSpeaker(const DialogueSpeaker& speaker) {
        speakers_[speaker.id] = speaker;
    }
    
    const DialogueSpeaker* getSpeaker(const std::string& id) const {
        auto it = speakers_.find(id);
        return it != speakers_.end() ? &it->second : nullptr;
    }
    
    // Graph metadata
    std::string name;
    std::string description;
    std::vector<std::string> tags;
    
    /**
     * Save to file
     */
    bool saveToFile(const std::string& path) const;
    
    /**
     * Load from file
     */
    static std::unique_ptr<DialogueGraph> loadFromFile(const std::string& path);
    
private:
    std::unordered_map<DialogueNodeID, DialogueNode> nodes_;
    std::unordered_map<std::string, DialogueSpeaker> speakers_;
    DialogueNodeID entryNodeId_ = INVALID_DIALOGUE_NODE;
    DialogueNodeID nextNodeId_ = 1;
};

// ============================================================================
// DIALOGUE CONTEXT
// ============================================================================

/**
 * Runtime context for dialogue execution
 */
class DialogueContext {
public:
    DialogueContext(World* world = nullptr);
    
    // Participants
    Entity playerEntity = INVALID_ENTITY;
    Entity npcEntity = INVALID_ENTITY;
    
    // World reference
    World* world = nullptr;
    
    // Dialogue variables (can be saved/loaded)
    std::unordered_map<std::string, DialogueVariable> variables;
    
    // Flags (persistent across dialogues)
    std::unordered_set<std::string> flags;
    
    // Current locale
    std::string locale = "en";
    
    // Quest integration
    std::function<bool(const std::string&)> isQuestActive;
    std::function<bool(const std::string&)> isQuestComplete;
    std::function<void(const std::string&)> startQuest;
    std::function<void(const std::string&, const std::string&)> completeObjective;
    
    // Inventory integration
    std::function<bool(const std::string&, int)> hasItem;
    std::function<void(const std::string&, int)> giveItem;
    std::function<void(const std::string&, int)> takeItem;
    
    // Variable access
    template<typename T>
    T getVariable(const std::string& name, const T& defaultValue = T{}) const {
        auto it = variables.find(name);
        if (it == variables.end()) return defaultValue;
        
        if (auto* val = std::get_if<T>(&it->second)) {
            return *val;
        }
        return defaultValue;
    }
    
    template<typename T>
    void setVariable(const std::string& name, const T& value) {
        variables[name] = value;
    }
    
    // Flag operations
    bool hasFlag(const std::string& flag) const {
        return flags.find(flag) != flags.end();
    }
    
    void setFlag(const std::string& flag) {
        flags.insert(flag);
    }
    
    void clearFlag(const std::string& flag) {
        flags.erase(flag);
    }
};

// ============================================================================
// DIALOGUE EVENTS
// ============================================================================

/**
 * Events fired during dialogue
 */
struct DialogueEvent {
    enum class Type {
        Started,          // Dialogue began
        NodeEntered,      // Entered a new node
        LineDisplayed,    // A line is being displayed
        LineCompleted,    // Line finished displaying
        ChoicePresented,  // Player choices shown
        ChoiceMade,       // Player selected a choice
        ActionExecuted,   // An action was triggered
        Ended             // Dialogue finished
    } type;
    
    DialogueNodeID nodeId = INVALID_DIALOGUE_NODE;
    int lineIndex = 0;
    int choiceIndex = -1;
    const DialogueLine* line = nullptr;
    const DialogueResponse* choice = nullptr;
};

// ============================================================================
// DIALOGUE PLAYER
// ============================================================================

/**
 * Plays a dialogue graph
 */
class DialoguePlayer {
public:
    using EventCallback = std::function<void(const DialogueEvent&)>;
    
    DialoguePlayer();
    
    /**
     * Start a dialogue
     */
    void startDialogue(std::shared_ptr<DialogueGraph> graph, DialogueContext& context);
    
    /**
     * Stop current dialogue
     */
    void stopDialogue();
    
    /**
     * Advance to next line/node
     */
    void advance();
    
    /**
     * Select a response choice
     */
    void selectChoice(int choiceIndex);
    
    /**
     * Update (for typewriter effect, etc.)
     */
    void update(float deltaTime);
    
    /**
     * Is dialogue active?
     */
    bool isActive() const { return isActive_; }
    
    /**
     * Is waiting for player input?
     */
    bool isWaitingForChoice() const { return waitingForChoice_; }
    bool isWaitingForAdvance() const { return waitingForAdvance_; }
    
    /**
     * Get current displayed text
     */
    const std::string& getCurrentDisplayText() const { return displayText_; }
    
    /**
     * Get current speaker
     */
    const DialogueSpeaker* getCurrentSpeaker() const { return currentSpeaker_; }
    
    /**
     * Get current choices (if waiting for choice)
     */
    const std::vector<DialogueResponse>& getCurrentChoices() const { return currentChoices_; }
    
    /**
     * Register event callback
     */
    void setEventCallback(EventCallback callback) { eventCallback_ = callback; }
    
    /**
     * Skip typewriter effect
     */
    void skipTypewriter();
    
private:
    void enterNode(DialogueNodeID nodeId);
    void displayLine(int lineIndex);
    void executeActions(const std::vector<DialogueAction>& actions);
    void fireEvent(DialogueEvent::Type type);
    
    std::shared_ptr<DialogueGraph> graph_;
    DialogueContext* context_ = nullptr;
    
    DialogueNodeID currentNodeId_ = INVALID_DIALOGUE_NODE;
    int currentLineIndex_ = 0;
    
    bool isActive_ = false;
    bool waitingForChoice_ = false;
    bool waitingForAdvance_ = false;
    bool typewriterActive_ = false;
    
    std::string fullText_;
    std::string displayText_;
    float typewriterProgress_ = 0.0f;
    float typewriterSpeed_ = 30.0f;
    
    const DialogueSpeaker* currentSpeaker_ = nullptr;
    std::vector<DialogueResponse> currentChoices_;
    
    EventCallback eventCallback_;
};

// ============================================================================
// DIALOGUE COMPONENT
// ============================================================================

/**
 * Component for entities that can participate in dialogue
 */
struct DialogueComponent {
    std::vector<std::shared_ptr<DialogueGraph>> dialogues;
    
    // Which dialogue to use (can be changed based on game state)
    int activeDialogueIndex = 0;
    
    // Speaker info for this entity
    DialogueSpeaker speaker;
    
    // Interaction settings
    float interactionRadius = 2.0f;
    bool canInitiateDialogue = true;
    
    // Bark (one-liner) settings
    std::vector<LocalizedString> barks;
    float barkCooldown = 30.0f;
    float lastBarkTime = -1000.0f;
    
    /**
     * Get active dialogue
     */
    std::shared_ptr<DialogueGraph> getActiveDialogue() const {
        if (activeDialogueIndex >= 0 && 
            activeDialogueIndex < static_cast<int>(dialogues.size())) {
            return dialogues[activeDialogueIndex];
        }
        return nullptr;
    }
};

// ============================================================================
// DIALOGUE SYSTEM
// ============================================================================

/**
 * System that manages dialogue interactions
 */
class DialogueSystem : public System {
public:
    DialogueSystem();
    
    void init(World& world) override;
    void update(World& world, float deltaTime) override;
    void shutdown(World& world) override;
    
    /**
     * Start dialogue with an NPC
     */
    bool startDialogue(Entity player, Entity npc);
    
    /**
     * Get the dialogue player
     */
    DialoguePlayer& getPlayer() { return player_; }
    const DialoguePlayer& getPlayer() const { return player_; }
    
    /**
     * Get current context
     */
    DialogueContext& getContext() { return context_; }
    
    /**
     * Set the current locale
     */
    void setLocale(const std::string& locale) { context_.locale = locale; }
    
    /**
     * Find nearby interactable NPCs
     */
    std::vector<Entity> findNearbyDialogueEntities(Entity player, float maxDistance = 3.0f);
    
    /**
     * Trigger a bark (one-liner)
     */
    void triggerBark(Entity entity);
    
private:
    DialoguePlayer player_;
    DialogueContext context_;
    
    // UI callbacks
    std::function<void(const std::string&, const DialogueSpeaker*)> showLineCallback_;
    std::function<void(const std::vector<DialogueResponse>&)> showChoicesCallback_;
    std::function<void()> hideDialogueCallback_;
};

// ============================================================================
// DIALOGUE BUILDER
// ============================================================================

/**
 * Helper for building dialogues programmatically
 */
class DialogueBuilder {
public:
    DialogueBuilder(const std::string& name = "Dialogue");
    
    /**
     * Add a speaker
     */
    DialogueBuilder& speaker(const std::string& id, const std::string& displayName);
    
    /**
     * Start a line node
     */
    DialogueBuilder& line(const std::string& speakerId, const std::string& text);
    
    /**
     * Add another line to current node
     */
    DialogueBuilder& then(const std::string& speakerId, const std::string& text);
    
    /**
     * Start a choice node
     */
    DialogueBuilder& choice();
    
    /**
     * Add a response option
     */
    DialogueBuilder& option(const std::string& text);
    
    /**
     * Add condition to current option
     */
    DialogueBuilder& when(const std::string& variable, DialogueOperator op, 
                          const DialogueVariable& value);
    
    /**
     * Set action for current option
     */
    DialogueBuilder& action(DialogueActionType type, const std::string& param1 = "",
                            const std::string& param2 = "", int intParam = 0);
    
    /**
     * Go to a labeled node
     */
    DialogueBuilder& goTo(const std::string& label);
    
    /**
     * Label current node
     */
    DialogueBuilder& label(const std::string& name);
    
    /**
     * End dialogue
     */
    DialogueBuilder& endDialogue();
    
    /**
     * Build the graph
     */
    std::unique_ptr<DialogueGraph> build();
    
private:
    std::unique_ptr<DialogueGraph> graph_;
    DialogueNode* currentNode_ = nullptr;
    DialogueResponse* currentResponse_ = nullptr;
    std::unordered_map<std::string, DialogueNodeID> labels_;
    std::vector<std::pair<DialogueNodeID, std::string>> pendingGotos_;
};

} // namespace Sanic
