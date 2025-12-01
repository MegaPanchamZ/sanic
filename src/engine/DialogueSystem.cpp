/**
 * DialogueSystem.cpp
 * 
 * Dialogue and Conversation System Implementation
 */

#include "DialogueSystem.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace Sanic {

// ============================================================================
// DIALOGUE CONDITION IMPLEMENTATION
// ============================================================================

bool DialogueCondition::evaluate(const DialogueContext& context) const {
    auto it = context.variables.find(variableName);
    
    // Handle special operators
    if (op == DialogueOperator::HasFlag) {
        return context.hasFlag(variableName);
    }
    if (op == DialogueOperator::QuestComplete) {
        return context.isQuestComplete && context.isQuestComplete(variableName);
    }
    if (op == DialogueOperator::QuestActive) {
        return context.isQuestActive && context.isQuestActive(variableName);
    }
    
    if (it == context.variables.end()) {
        return false;
    }
    
    const DialogueVariable& var = it->second;
    
    // Type-specific comparisons
    if (std::holds_alternative<bool>(var) && std::holds_alternative<bool>(value)) {
        bool v = std::get<bool>(var);
        bool c = std::get<bool>(value);
        switch (op) {
            case DialogueOperator::Equal: return v == c;
            case DialogueOperator::NotEqual: return v != c;
            default: return false;
        }
    }
    
    if (std::holds_alternative<int>(var) && std::holds_alternative<int>(value)) {
        int v = std::get<int>(var);
        int c = std::get<int>(value);
        switch (op) {
            case DialogueOperator::Equal: return v == c;
            case DialogueOperator::NotEqual: return v != c;
            case DialogueOperator::Greater: return v > c;
            case DialogueOperator::GreaterEqual: return v >= c;
            case DialogueOperator::Less: return v < c;
            case DialogueOperator::LessEqual: return v <= c;
            default: return false;
        }
    }
    
    if (std::holds_alternative<float>(var) && std::holds_alternative<float>(value)) {
        float v = std::get<float>(var);
        float c = std::get<float>(value);
        switch (op) {
            case DialogueOperator::Equal: return std::abs(v - c) < 0.0001f;
            case DialogueOperator::NotEqual: return std::abs(v - c) >= 0.0001f;
            case DialogueOperator::Greater: return v > c;
            case DialogueOperator::GreaterEqual: return v >= c;
            case DialogueOperator::Less: return v < c;
            case DialogueOperator::LessEqual: return v <= c;
            default: return false;
        }
    }
    
    if (std::holds_alternative<std::string>(var) && std::holds_alternative<std::string>(value)) {
        const std::string& v = std::get<std::string>(var);
        const std::string& c = std::get<std::string>(value);
        switch (op) {
            case DialogueOperator::Equal: return v == c;
            case DialogueOperator::NotEqual: return v != c;
            case DialogueOperator::Contains: return v.find(c) != std::string::npos;
            default: return false;
        }
    }
    
    return false;
}

// ============================================================================
// DIALOGUE ACTION IMPLEMENTATION
// ============================================================================

void DialogueAction::execute(DialogueContext& context) const {
    switch (type) {
        case DialogueActionType::SetVariable:
            if (!stringParam2.empty()) {
                context.setVariable(stringParam1, stringParam2);
            } else if (floatParam != 0.0f) {
                context.setVariable(stringParam1, floatParam);
            } else {
                context.setVariable(stringParam1, intParam);
            }
            break;
            
        case DialogueActionType::GiveItem:
            if (context.giveItem) {
                context.giveItem(stringParam1, intParam > 0 ? intParam : 1);
            }
            break;
            
        case DialogueActionType::TakeItem:
            if (context.takeItem) {
                context.takeItem(stringParam1, intParam > 0 ? intParam : 1);
            }
            break;
            
        case DialogueActionType::GiveQuest:
            if (context.startQuest) {
                context.startQuest(stringParam1);
            }
            break;
            
        case DialogueActionType::CompleteQuest:
            if (context.completeObjective) {
                context.completeObjective(stringParam1, stringParam2);
            }
            break;
            
        case DialogueActionType::AddReputation:
            // Would integrate with faction system
            break;
            
        case DialogueActionType::PlayAnimation:
            // Would trigger animation on speaker/listener
            break;
            
        case DialogueActionType::PlaySound:
            // Would play sound effect
            break;
            
        case DialogueActionType::StartBattle:
            // Would initiate combat
            break;
            
        case DialogueActionType::Teleport:
            // Would move player
            break;
            
        case DialogueActionType::Custom:
            if (customAction) {
                customAction(context);
            }
            break;
    }
}

// ============================================================================
// DIALOGUE RESPONSE IMPLEMENTATION
// ============================================================================

bool DialogueResponse::canShow(const DialogueContext& context) const {
    for (const auto& condition : conditions) {
        if (!condition(context)) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// DIALOGUE NODE IMPLEMENTATION
// ============================================================================

DialogueNode::DialogueNode(DialogueNodeType type)
    : type(type)
{
}

DialogueNodeID DialogueNode::getNextNode(const DialogueContext& context) const {
    // Check conditional branches first
    for (const auto& [condition, nodeId] : conditionalBranches) {
        if (condition.evaluate(context)) {
            return nodeId;
        }
    }
    
    return defaultNextNode;
}

std::vector<DialogueResponse> DialogueNode::getAvailableResponses(const DialogueContext& context) const {
    std::vector<DialogueResponse> available;
    
    for (const auto& response : responses) {
        if (response.canShow(context)) {
            available.push_back(response);
        }
    }
    
    return available;
}

// ============================================================================
// DIALOGUE GRAPH IMPLEMENTATION
// ============================================================================

DialogueGraph::DialogueGraph(const std::string& name)
    : name(name)
{
}

DialogueNode& DialogueGraph::addNode(DialogueNodeType type) {
    DialogueNodeID id = nextNodeId_++;
    nodes_[id] = DialogueNode(type);
    nodes_[id].id = id;
    return nodes_[id];
}

DialogueNode* DialogueGraph::getNode(DialogueNodeID id) {
    auto it = nodes_.find(id);
    return it != nodes_.end() ? &it->second : nullptr;
}

const DialogueNode* DialogueGraph::getNode(DialogueNodeID id) const {
    auto it = nodes_.find(id);
    return it != nodes_.end() ? &it->second : nullptr;
}

void DialogueGraph::removeNode(DialogueNodeID id) {
    nodes_.erase(id);
    
    // Update references
    for (auto& [nodeId, node] : nodes_) {
        if (node.defaultNextNode == id) {
            node.defaultNextNode = INVALID_DIALOGUE_NODE;
        }
        
        node.conditionalBranches.erase(
            std::remove_if(node.conditionalBranches.begin(), node.conditionalBranches.end(),
                [id](const auto& pair) { return pair.second == id; }),
            node.conditionalBranches.end()
        );
        
        for (auto& response : node.responses) {
            if (response.nextNodeId == id) {
                response.nextNodeId = INVALID_DIALOGUE_NODE;
            }
        }
    }
    
    if (entryNodeId_ == id) {
        entryNodeId_ = INVALID_DIALOGUE_NODE;
    }
}

bool DialogueGraph::saveToFile(const std::string& path) const {
    using json = nlohmann::json;
    
    json doc;
    doc["name"] = name;
    doc["description"] = description;
    doc["tags"] = tags;
    doc["entryNode"] = entryNodeId_;
    
    // Serialize speakers
    doc["speakers"] = json::array();
    for (const auto& [id, speaker] : speakers_) {
        json s;
        s["id"] = speaker.id;
        s["displayName"] = speaker.displayName.defaultText;
        s["portrait"] = speaker.portraitAsset;
        s["textColor"] = { speaker.textColor.r, speaker.textColor.g, 
                           speaker.textColor.b, speaker.textColor.a };
        doc["speakers"].push_back(s);
    }
    
    // Serialize nodes
    doc["nodes"] = json::array();
    for (const auto& [id, node] : nodes_) {
        json n;
        n["id"] = node.id;
        n["type"] = static_cast<int>(node.type);
        n["name"] = node.name;
        n["defaultNext"] = node.defaultNextNode;
        n["position"] = { node.editorPosition.x, node.editorPosition.y };
        
        // Lines
        n["lines"] = json::array();
        for (const auto& line : node.lines) {
            json l;
            l["text"] = line.text.defaultText;
            l["speaker"] = line.speakerId;
            l["duration"] = line.displayDuration;
            l["typewriterSpeed"] = line.typewriterSpeed;
            l["voiceClip"] = line.voiceClip;
            n["lines"].push_back(l);
        }
        
        // Responses
        n["responses"] = json::array();
        for (const auto& response : node.responses) {
            json r;
            r["id"] = response.id;
            r["text"] = response.text.defaultText;
            r["nextNode"] = response.nextNodeId;
            r["mood"] = static_cast<int>(response.mood);
            r["requirement"] = response.requirementText;
            n["responses"].push_back(r);
        }
        
        // Actions
        n["actions"] = json::array();
        for (const auto& action : node.actions) {
            json a;
            a["type"] = static_cast<int>(action.type);
            a["param1"] = action.stringParam1;
            a["param2"] = action.stringParam2;
            a["intParam"] = action.intParam;
            a["floatParam"] = action.floatParam;
            n["actions"].push_back(a);
        }
        
        doc["nodes"].push_back(n);
    }
    
    std::ofstream file(path);
    if (!file.is_open()) return false;
    
    file << doc.dump(2);
    return true;
}

std::unique_ptr<DialogueGraph> DialogueGraph::loadFromFile(const std::string& path) {
    using json = nlohmann::json;
    
    std::ifstream file(path);
    if (!file.is_open()) return nullptr;
    
    json doc;
    try {
        file >> doc;
    } catch (const std::exception&) {
        return nullptr;
    }
    
    auto graph = std::make_unique<DialogueGraph>(doc.value("name", ""));
    graph->description = doc.value("description", "");
    graph->tags = doc.value("tags", std::vector<std::string>{});
    graph->entryNodeId_ = doc.value("entryNode", 0ULL);
    
    // Load speakers
    if (doc.contains("speakers")) {
        for (const auto& s : doc["speakers"]) {
            DialogueSpeaker speaker;
            speaker.id = s.value("id", "");
            speaker.displayName.defaultText = s.value("displayName", "");
            speaker.portraitAsset = s.value("portrait", "");
            if (s.contains("textColor") && s["textColor"].size() == 4) {
                speaker.textColor = glm::vec4(
                    s["textColor"][0], s["textColor"][1],
                    s["textColor"][2], s["textColor"][3]
                );
            }
            graph->addSpeaker(speaker);
        }
    }
    
    // Load nodes
    if (doc.contains("nodes")) {
        for (const auto& n : doc["nodes"]) {
            DialogueNodeType type = static_cast<DialogueNodeType>(n.value("type", 0));
            DialogueNode& node = graph->addNode(type);
            
            // Override generated ID with saved ID
            DialogueNodeID savedId = n.value("id", node.id);
            if (savedId != node.id) {
                graph->nodes_.erase(node.id);
                node.id = savedId;
                graph->nodes_[savedId] = std::move(node);
                graph->nextNodeId_ = std::max(graph->nextNodeId_, savedId + 1);
            }
            
            DialogueNode& loadedNode = graph->nodes_[savedId];
            loadedNode.name = n.value("name", "");
            loadedNode.defaultNextNode = n.value("defaultNext", 0ULL);
            
            if (n.contains("position") && n["position"].size() == 2) {
                loadedNode.editorPosition = glm::vec2(n["position"][0], n["position"][1]);
            }
            
            // Lines
            if (n.contains("lines")) {
                for (const auto& l : n["lines"]) {
                    DialogueLine line;
                    line.text.defaultText = l.value("text", "");
                    line.speakerId = l.value("speaker", "");
                    line.displayDuration = l.value("duration", 0.0f);
                    line.typewriterSpeed = l.value("typewriterSpeed", 30.0f);
                    line.voiceClip = l.value("voiceClip", "");
                    loadedNode.lines.push_back(line);
                }
            }
            
            // Responses
            if (n.contains("responses")) {
                for (const auto& r : n["responses"]) {
                    DialogueResponse response;
                    response.id = r.value("id", 0ULL);
                    response.text.defaultText = r.value("text", "");
                    response.nextNodeId = r.value("nextNode", 0ULL);
                    response.mood = static_cast<DialogueResponse::Mood>(r.value("mood", 0));
                    response.requirementText = r.value("requirement", "");
                    loadedNode.responses.push_back(response);
                }
            }
            
            // Actions
            if (n.contains("actions")) {
                for (const auto& a : n["actions"]) {
                    DialogueAction action;
                    action.type = static_cast<DialogueActionType>(a.value("type", 0));
                    action.stringParam1 = a.value("param1", "");
                    action.stringParam2 = a.value("param2", "");
                    action.intParam = a.value("intParam", 0);
                    action.floatParam = a.value("floatParam", 0.0f);
                    loadedNode.actions.push_back(action);
                }
            }
        }
    }
    
    return graph;
}

// ============================================================================
// DIALOGUE CONTEXT IMPLEMENTATION
// ============================================================================

DialogueContext::DialogueContext(World* world)
    : world(world)
{
}

// ============================================================================
// DIALOGUE PLAYER IMPLEMENTATION
// ============================================================================

DialoguePlayer::DialoguePlayer() {
}

void DialoguePlayer::startDialogue(std::shared_ptr<DialogueGraph> graph, DialogueContext& context) {
    graph_ = graph;
    context_ = &context;
    isActive_ = true;
    
    fireEvent(DialogueEvent::Type::Started);
    
    enterNode(graph_->getEntryNode());
}

void DialoguePlayer::stopDialogue() {
    if (!isActive_) return;
    
    fireEvent(DialogueEvent::Type::Ended);
    
    graph_ = nullptr;
    context_ = nullptr;
    isActive_ = false;
    waitingForChoice_ = false;
    waitingForAdvance_ = false;
    currentChoices_.clear();
    currentSpeaker_ = nullptr;
}

void DialoguePlayer::enterNode(DialogueNodeID nodeId) {
    if (!graph_ || nodeId == INVALID_DIALOGUE_NODE) {
        stopDialogue();
        return;
    }
    
    DialogueNode* node = graph_->getNode(nodeId);
    if (!node) {
        stopDialogue();
        return;
    }
    
    currentNodeId_ = nodeId;
    currentLineIndex_ = 0;
    
    fireEvent(DialogueEvent::Type::NodeEntered);
    
    // Execute entry actions
    executeActions(node->actions);
    
    switch (node->type) {
        case DialogueNodeType::Entry:
        case DialogueNodeType::Action:
            // Proceed to next node
            enterNode(node->getNextNode(*context_));
            break;
            
        case DialogueNodeType::Line:
            if (!node->lines.empty()) {
                displayLine(0);
            } else {
                enterNode(node->getNextNode(*context_));
            }
            break;
            
        case DialogueNodeType::PlayerChoice:
            currentChoices_ = node->getAvailableResponses(*context_);
            if (currentChoices_.empty()) {
                // No valid choices, proceed to default
                enterNode(node->defaultNextNode);
            } else {
                waitingForChoice_ = true;
                fireEvent(DialogueEvent::Type::ChoicePresented);
            }
            break;
            
        case DialogueNodeType::Branch:
            enterNode(node->getNextNode(*context_));
            break;
            
        case DialogueNodeType::Random:
            if (!node->lines.empty()) {
                int index = rand() % static_cast<int>(node->lines.size());
                displayLine(index);
            }
            break;
            
        case DialogueNodeType::Exit:
            stopDialogue();
            break;
    }
}

void DialoguePlayer::displayLine(int lineIndex) {
    DialogueNode* node = graph_->getNode(currentNodeId_);
    if (!node || lineIndex >= static_cast<int>(node->lines.size())) {
        enterNode(node->getNextNode(*context_));
        return;
    }
    
    currentLineIndex_ = lineIndex;
    const DialogueLine& line = node->lines[lineIndex];
    
    fullText_ = line.text.get(context_->locale);
    displayText_.clear();
    typewriterProgress_ = 0.0f;
    typewriterSpeed_ = line.typewriterSpeed;
    typewriterActive_ = typewriterSpeed_ > 0.0f;
    
    currentSpeaker_ = graph_->getSpeaker(line.speakerId);
    
    waitingForAdvance_ = false;
    
    fireEvent(DialogueEvent::Type::LineDisplayed);
}

void DialoguePlayer::advance() {
    if (!isActive_ || waitingForChoice_) return;
    
    if (typewriterActive_) {
        skipTypewriter();
        return;
    }
    
    if (!waitingForAdvance_) return;
    
    DialogueNode* node = graph_->getNode(currentNodeId_);
    if (!node) {
        stopDialogue();
        return;
    }
    
    // Check if there are more lines in this node
    if (currentLineIndex_ + 1 < static_cast<int>(node->lines.size())) {
        displayLine(currentLineIndex_ + 1);
    } else {
        // Move to next node
        enterNode(node->getNextNode(*context_));
    }
}

void DialoguePlayer::selectChoice(int choiceIndex) {
    if (!isActive_ || !waitingForChoice_) return;
    if (choiceIndex < 0 || choiceIndex >= static_cast<int>(currentChoices_.size())) return;
    
    const DialogueResponse& choice = currentChoices_[choiceIndex];
    
    waitingForChoice_ = false;
    
    fireEvent(DialogueEvent::Type::ChoiceMade);
    
    enterNode(choice.nextNodeId);
}

void DialoguePlayer::update(float deltaTime) {
    if (!isActive_) return;
    
    // Update typewriter effect
    if (typewriterActive_) {
        typewriterProgress_ += typewriterSpeed_ * deltaTime;
        
        int charsToShow = static_cast<int>(typewriterProgress_);
        if (charsToShow >= static_cast<int>(fullText_.length())) {
            displayText_ = fullText_;
            typewriterActive_ = false;
            waitingForAdvance_ = true;
            fireEvent(DialogueEvent::Type::LineCompleted);
        } else {
            displayText_ = fullText_.substr(0, charsToShow);
        }
    }
}

void DialoguePlayer::skipTypewriter() {
    if (!typewriterActive_) return;
    
    displayText_ = fullText_;
    typewriterActive_ = false;
    waitingForAdvance_ = true;
    fireEvent(DialogueEvent::Type::LineCompleted);
}

void DialoguePlayer::executeActions(const std::vector<DialogueAction>& actions) {
    if (!context_) return;
    
    for (const auto& action : actions) {
        action.execute(*context_);
        fireEvent(DialogueEvent::Type::ActionExecuted);
    }
}

void DialoguePlayer::fireEvent(DialogueEvent::Type type) {
    if (!eventCallback_) return;
    
    DialogueEvent event;
    event.type = type;
    event.nodeId = currentNodeId_;
    event.lineIndex = currentLineIndex_;
    
    if (type == DialogueEvent::Type::ChoiceMade && !currentChoices_.empty()) {
        // Would need to track which choice was made
    }
    
    DialogueNode* node = graph_ ? graph_->getNode(currentNodeId_) : nullptr;
    if (node && currentLineIndex_ < static_cast<int>(node->lines.size())) {
        event.line = &node->lines[currentLineIndex_];
    }
    
    eventCallback_(event);
}

// ============================================================================
// DIALOGUE SYSTEM IMPLEMENTATION
// ============================================================================

DialogueSystem::DialogueSystem() {
}

void DialogueSystem::init(World& world) {
    context_.world = &world;
}

void DialogueSystem::update(World& world, float deltaTime) {
    // Update the dialogue player
    player_.update(deltaTime);
}

void DialogueSystem::shutdown(World& world) {
    player_.stopDialogue();
}

bool DialogueSystem::startDialogue(Entity player, Entity npc) {
    auto* dialogueComp = context_.world->getComponent<DialogueComponent>(npc);
    if (!dialogueComp || !dialogueComp->canInitiateDialogue) {
        return false;
    }
    
    auto graph = dialogueComp->getActiveDialogue();
    if (!graph) {
        return false;
    }
    
    context_.playerEntity = player;
    context_.npcEntity = npc;
    
    player_.startDialogue(graph, context_);
    
    return true;
}

std::vector<Entity> DialogueSystem::findNearbyDialogueEntities(Entity player, float maxDistance) {
    std::vector<Entity> result;
    
    auto* playerTransform = context_.world->getComponent<Transform>(player);
    if (!playerTransform) return result;
    
    context_.world->query<DialogueComponent, Transform>([&](Entity entity, 
                                                             DialogueComponent& dialogue,
                                                             Transform& transform) {
        if (entity == player) return;
        if (!dialogue.canInitiateDialogue) return;
        
        float distance = glm::distance(playerTransform->position, transform.position);
        if (distance <= std::min(maxDistance, dialogue.interactionRadius)) {
            result.push_back(entity);
        }
    });
    
    return result;
}

void DialogueSystem::triggerBark(Entity entity) {
    auto* dialogueComp = context_.world->getComponent<DialogueComponent>(entity);
    if (!dialogueComp || dialogueComp->barks.empty()) return;
    
    // Check cooldown
    // Would need access to game time
    // if (currentTime - dialogueComp->lastBarkTime < dialogueComp->barkCooldown) return;
    
    // Pick random bark
    int index = rand() % static_cast<int>(dialogueComp->barks.size());
    const LocalizedString& bark = dialogueComp->barks[index];
    
    // Would display as floating text or play audio
    // dialogueComp->lastBarkTime = currentTime;
}

// ============================================================================
// DIALOGUE BUILDER IMPLEMENTATION
// ============================================================================

DialogueBuilder::DialogueBuilder(const std::string& name)
    : graph_(std::make_unique<DialogueGraph>(name))
{
}

DialogueBuilder& DialogueBuilder::speaker(const std::string& id, const std::string& displayName) {
    DialogueSpeaker s;
    s.id = id;
    s.displayName.defaultText = displayName;
    graph_->addSpeaker(s);
    return *this;
}

DialogueBuilder& DialogueBuilder::line(const std::string& speakerId, const std::string& text) {
    DialogueNode& node = graph_->addNode(DialogueNodeType::Line);
    
    // Link previous node to this one
    if (currentNode_) {
        currentNode_->defaultNextNode = node.id;
    } else {
        graph_->setEntryNode(node.id);
    }
    
    currentNode_ = &node;
    
    DialogueLine l;
    l.speakerId = speakerId;
    l.text.defaultText = text;
    currentNode_->lines.push_back(l);
    
    return *this;
}

DialogueBuilder& DialogueBuilder::then(const std::string& speakerId, const std::string& text) {
    if (!currentNode_) {
        return line(speakerId, text);
    }
    
    DialogueLine l;
    l.speakerId = speakerId;
    l.text.defaultText = text;
    currentNode_->lines.push_back(l);
    
    return *this;
}

DialogueBuilder& DialogueBuilder::choice() {
    DialogueNode& node = graph_->addNode(DialogueNodeType::PlayerChoice);
    
    if (currentNode_) {
        currentNode_->defaultNextNode = node.id;
    } else {
        graph_->setEntryNode(node.id);
    }
    
    currentNode_ = &node;
    currentResponse_ = nullptr;
    
    return *this;
}

DialogueBuilder& DialogueBuilder::option(const std::string& text) {
    if (!currentNode_ || currentNode_->type != DialogueNodeType::PlayerChoice) {
        return *this;
    }
    
    DialogueResponse response;
    response.text.defaultText = text;
    currentNode_->responses.push_back(response);
    currentResponse_ = &currentNode_->responses.back();
    
    return *this;
}

DialogueBuilder& DialogueBuilder::when(const std::string& variable, DialogueOperator op,
                                        const DialogueVariable& value) {
    if (!currentResponse_) return *this;
    
    currentResponse_->conditions.push_back([variable, op, value](const DialogueContext& ctx) {
        DialogueCondition cond;
        cond.variableName = variable;
        cond.op = op;
        cond.value = value;
        return cond.evaluate(ctx);
    });
    
    return *this;
}

DialogueBuilder& DialogueBuilder::action(DialogueActionType type, const std::string& param1,
                                          const std::string& param2, int intParam) {
    if (!currentNode_) return *this;
    
    DialogueAction a;
    a.type = type;
    a.stringParam1 = param1;
    a.stringParam2 = param2;
    a.intParam = intParam;
    currentNode_->actions.push_back(a);
    
    return *this;
}

DialogueBuilder& DialogueBuilder::goTo(const std::string& labelName) {
    if (!currentNode_ && !currentResponse_) return *this;
    
    auto it = labels_.find(labelName);
    if (it != labels_.end()) {
        if (currentResponse_) {
            currentResponse_->nextNodeId = it->second;
        } else if (currentNode_) {
            currentNode_->defaultNextNode = it->second;
        }
    } else {
        // Defer resolution
        if (currentResponse_) {
            pendingGotos_.push_back({ currentResponse_->id, labelName });
        } else if (currentNode_) {
            pendingGotos_.push_back({ currentNode_->id, labelName });
        }
    }
    
    return *this;
}

DialogueBuilder& DialogueBuilder::label(const std::string& name) {
    if (currentNode_) {
        labels_[name] = currentNode_->id;
    }
    return *this;
}

DialogueBuilder& DialogueBuilder::endDialogue() {
    DialogueNode& node = graph_->addNode(DialogueNodeType::Exit);
    
    if (currentNode_) {
        currentNode_->defaultNextNode = node.id;
    }
    
    currentNode_ = &node;
    return *this;
}

std::unique_ptr<DialogueGraph> DialogueBuilder::build() {
    // Resolve pending gotos
    for (const auto& [nodeId, labelName] : pendingGotos_) {
        auto it = labels_.find(labelName);
        if (it != labels_.end()) {
            auto* node = graph_->getNode(nodeId);
            if (node) {
                node->defaultNextNode = it->second;
            }
        }
    }
    
    return std::move(graph_);
}

// ============================================================================
// EXAMPLE DIALOGUE
// ============================================================================

inline std::unique_ptr<DialogueGraph> createExampleDialogue() {
    DialogueBuilder builder("Shopkeeper Greeting");
    
    return builder
        .speaker("shopkeeper", "Old Tom")
        .speaker("player", "Player")
        
        .line("shopkeeper", "Welcome to my humble shop, traveler!")
        .then("shopkeeper", "What can I do for you today?")
        
        .choice()
            .option("What do you have for sale?")
                .goTo("shop")
            .option("I'm looking for information.")
                .goTo("info")
            .option("Nothing, just browsing.")
                .goTo("goodbye")
            .option("[Intimidate] Hand over your gold!")
                .when("charisma", DialogueOperator::GreaterEqual, 15)
                .goTo("intimidate")
        
        .label("shop")
        .line("shopkeeper", "Take a look at my wares!")
        .action(DialogueActionType::Custom) // Would open shop UI
        .endDialogue()
        
        .label("info")
        .line("shopkeeper", "Information, eh? What do you want to know?")
        .choice()
            .option("Tell me about this town.")
            .option("Have you seen anything suspicious?")
            .option("Never mind.")
                .goTo("goodbye")
        
        .label("goodbye")
        .line("shopkeeper", "Come back anytime!")
        .endDialogue()
        
        .label("intimidate")
        .line("shopkeeper", "W-what?! Guards! GUARDS!")
        .action(DialogueActionType::SetVariable, "shopkeeper_hostile", "", 1)
        .endDialogue()
        
        .build();
}

} // namespace Sanic
