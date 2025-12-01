/**
 * InventorySystem.cpp
 * 
 * Inventory and Item Management System Implementation
 */

#include "InventorySystem.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <random>
#include <fstream>

namespace Sanic {

// ============================================================================
// INVENTORY CONTAINER IMPLEMENTATION
// ============================================================================

InventoryContainer::InventoryContainer(int slotCount) {
    resize(slotCount);
}

InventorySlot* InventoryContainer::getSlot(int index) {
    if (index < 0 || index >= static_cast<int>(slots_.size())) {
        return nullptr;
    }
    return &slots_[index];
}

const InventorySlot* InventoryContainer::getSlot(int index) const {
    if (index < 0 || index >= static_cast<int>(slots_.size())) {
        return nullptr;
    }
    return &slots_[index];
}

void InventoryContainer::resize(int newSize) {
    int oldSize = static_cast<int>(slots_.size());
    slots_.resize(newSize);
    
    for (int i = oldSize; i < newSize; ++i) {
        slots_[i].slotIndex = i;
    }
}

bool InventoryContainer::addItem(const ItemInstance& item, const ItemDatabase& db) {
    const ItemDefinition* def = db.getDefinition(item.itemId);
    if (!def) return false;
    
    int remaining = item.stackCount;
    
    // If stackable, try to add to existing stacks first
    if (def->isStackable) {
        for (auto& slot : slots_) {
            if (slot.isLocked) continue;
            if (!slot.hasItem()) continue;
            
            ItemInstance* existing = slot.getItem();
            if (existing->itemId == item.itemId && existing->canStackWith(item)) {
                int spaceInStack = def->maxStackSize - existing->stackCount;
                int toAdd = std::min(remaining, spaceInStack);
                
                existing->stackCount += toAdd;
                remaining -= toAdd;
                
                if (remaining <= 0) return true;
            }
        }
    }
    
    // Add to empty slots
    while (remaining > 0) {
        int emptySlot = findEmptySlot();
        if (emptySlot < 0) return remaining < item.stackCount;  // Partial success
        
        int toAdd = def->isStackable ? std::min(remaining, def->maxStackSize) : 1;
        
        ItemInstance newItem = item;
        newItem.stackCount = toAdd;
        slots_[emptySlot].item = newItem;
        
        remaining -= toAdd;
    }
    
    return true;
}

bool InventoryContainer::addItemToSlot(int slotIndex, const ItemInstance& item, 
                                        const ItemDatabase& db) {
    InventorySlot* slot = getSlot(slotIndex);
    if (!slot || slot->isLocked) return false;
    
    if (slot->isEmpty()) {
        slot->item = item;
        return true;
    }
    
    // Try to stack
    const ItemDefinition* def = db.getDefinition(item.itemId);
    if (!def || !def->isStackable) return false;
    
    ItemInstance* existing = slot->getItem();
    if (!existing->canStackWith(item)) return false;
    
    int spaceInStack = def->maxStackSize - existing->stackCount;
    if (spaceInStack <= 0) return false;
    
    int toAdd = std::min(item.stackCount, spaceInStack);
    existing->stackCount += toAdd;
    
    return toAdd == item.stackCount;
}

std::optional<ItemInstance> InventoryContainer::removeItem(int slotIndex, int count) {
    InventorySlot* slot = getSlot(slotIndex);
    if (!slot || slot->isEmpty()) return std::nullopt;
    
    ItemInstance* item = slot->getItem();
    
    if (count < 0 || count >= item->stackCount) {
        // Remove entire stack
        ItemInstance result = *item;
        slot->item.reset();
        return result;
    }
    
    // Remove partial stack
    ItemInstance result = *item;
    result.stackCount = count;
    item->stackCount -= count;
    
    return result;
}

bool InventoryContainer::removeItemById(const ItemID& itemId, int count, 
                                         const ItemDatabase& db) {
    int remaining = count;
    
    for (auto& slot : slots_) {
        if (!slot.hasItem()) continue;
        
        ItemInstance* item = slot.getItem();
        if (item->itemId != itemId) continue;
        
        if (item->stackCount <= remaining) {
            remaining -= item->stackCount;
            slot.item.reset();
        } else {
            item->stackCount -= remaining;
            remaining = 0;
        }
        
        if (remaining <= 0) return true;
    }
    
    return remaining <= 0;
}

bool InventoryContainer::moveItem(int fromSlot, int toSlot, const ItemDatabase& db) {
    if (fromSlot == toSlot) return true;
    
    InventorySlot* from = getSlot(fromSlot);
    InventorySlot* to = getSlot(toSlot);
    
    if (!from || !to) return false;
    if (from->isEmpty()) return false;
    if (from->isLocked || to->isLocked) return false;
    
    if (to->isEmpty()) {
        to->item = std::move(from->item);
        from->item.reset();
        return true;
    }
    
    // Try to stack
    const ItemDefinition* def = db.getDefinition(from->getItem()->itemId);
    if (def && def->isStackable) {
        ItemInstance* fromItem = from->getItem();
        ItemInstance* toItem = to->getItem();
        
        if (fromItem->canStackWith(*toItem)) {
            int spaceInStack = def->maxStackSize - toItem->stackCount;
            int toMove = std::min(fromItem->stackCount, spaceInStack);
            
            toItem->stackCount += toMove;
            fromItem->stackCount -= toMove;
            
            if (fromItem->stackCount <= 0) {
                from->item.reset();
            }
            
            return true;
        }
    }
    
    // Swap
    return swapItems(fromSlot, toSlot);
}

bool InventoryContainer::swapItems(int slot1, int slot2) {
    InventorySlot* s1 = getSlot(slot1);
    InventorySlot* s2 = getSlot(slot2);
    
    if (!s1 || !s2) return false;
    if (s1->isLocked || s2->isLocked) return false;
    
    std::swap(s1->item, s2->item);
    return true;
}

bool InventoryContainer::splitStack(int slotIndex, int splitCount, int targetSlot,
                                     const ItemDatabase& db) {
    InventorySlot* from = getSlot(slotIndex);
    InventorySlot* to = getSlot(targetSlot);
    
    if (!from || !to) return false;
    if (from->isEmpty()) return false;
    if (!to->isEmpty()) return false;
    if (from->isLocked || to->isLocked) return false;
    
    ItemInstance* item = from->getItem();
    if (splitCount <= 0 || splitCount >= item->stackCount) return false;
    
    ItemInstance newStack = *item;
    newStack.stackCount = splitCount;
    item->stackCount -= splitCount;
    
    to->item = newStack;
    return true;
}

bool InventoryContainer::hasItem(const ItemID& itemId, int count) const {
    return countItem(itemId) >= count;
}

int InventoryContainer::countItem(const ItemID& itemId) const {
    int total = 0;
    for (const auto& slot : slots_) {
        if (slot.hasItem() && slot.getItem()->itemId == itemId) {
            total += slot.getItem()->stackCount;
        }
    }
    return total;
}

int InventoryContainer::findItem(const ItemID& itemId) const {
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].hasItem() && slots_[i].getItem()->itemId == itemId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<int> InventoryContainer::findAllItems(const ItemID& itemId) const {
    std::vector<int> result;
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].hasItem() && slots_[i].getItem()->itemId == itemId) {
            result.push_back(static_cast<int>(i));
        }
    }
    return result;
}

int InventoryContainer::findEmptySlot() const {
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].isEmpty() && !slots_[i].isLocked) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int InventoryContainer::getEmptySlotCount() const {
    int count = 0;
    for (const auto& slot : slots_) {
        if (slot.isEmpty() && !slot.isLocked) {
            count++;
        }
    }
    return count;
}

void InventoryContainer::sort(const ItemDatabase& db) {
    // Collect all items
    std::vector<ItemInstance> items;
    for (auto& slot : slots_) {
        if (slot.hasItem()) {
            items.push_back(*slot.getItem());
            slot.item.reset();
        }
    }
    
    // Sort by category, rarity, name
    std::sort(items.begin(), items.end(), [&db](const ItemInstance& a, const ItemInstance& b) {
        const ItemDefinition* defA = db.getDefinition(a.itemId);
        const ItemDefinition* defB = db.getDefinition(b.itemId);
        
        if (!defA || !defB) return false;
        
        if (defA->category != defB->category) {
            return static_cast<int>(defA->category) < static_cast<int>(defB->category);
        }
        
        if (defA->rarity != defB->rarity) {
            return static_cast<int>(defA->rarity) > static_cast<int>(defB->rarity);
        }
        
        return defA->name < defB->name;
    });
    
    // Put back
    for (const auto& item : items) {
        addItem(item, db);
    }
}

void InventoryContainer::clear() {
    for (auto& slot : slots_) {
        slot.item.reset();
    }
}

// ============================================================================
// EQUIPMENT LOADOUT IMPLEMENTATION
// ============================================================================

EquipmentLoadout::EquipmentLoadout() {
}

const ItemInstance* EquipmentLoadout::getEquipped(EquipmentSlot slot) const {
    auto it = equipped_.find(slot);
    return it != equipped_.end() ? &it->second : nullptr;
}

ItemInstance* EquipmentLoadout::getEquipped(EquipmentSlot slot) {
    auto it = equipped_.find(slot);
    return it != equipped_.end() ? &it->second : nullptr;
}

bool EquipmentLoadout::equip(const ItemInstance& item, const ItemDatabase& db) {
    const ItemDefinition* def = db.getDefinition(item.itemId);
    if (!def || !def->isEquippable) return false;
    
    EquipmentSlot slot = def->equipSlot;
    if (slot == EquipmentSlot::None) return false;
    
    // Handle two-handed weapons
    if (slot == EquipmentSlot::TwoHand) {
        equipped_.erase(EquipmentSlot::MainHand);
        equipped_.erase(EquipmentSlot::OffHand);
        slot = EquipmentSlot::MainHand;
    }
    
    equipped_[slot] = item;
    return true;
}

std::optional<ItemInstance> EquipmentLoadout::unequip(EquipmentSlot slot) {
    auto it = equipped_.find(slot);
    if (it == equipped_.end()) return std::nullopt;
    
    ItemInstance item = it->second;
    equipped_.erase(it);
    return item;
}

bool EquipmentLoadout::isSlotOccupied(EquipmentSlot slot) const {
    return equipped_.find(slot) != equipped_.end();
}

std::vector<StatModifier> EquipmentLoadout::getAllModifiers() const {
    std::vector<StatModifier> result;
    
    for (const auto& [slot, item] : equipped_) {
        const ItemDefinition* def = ItemDatabase::getInstance().getDefinition(item.itemId);
        if (def) {
            result.insert(result.end(), 
                         def->statModifiers.begin(), 
                         def->statModifiers.end());
        }
        
        result.insert(result.end(),
                     item.bonusModifiers.begin(),
                     item.bonusModifiers.end());
    }
    
    return result;
}

float EquipmentLoadout::getTotalStat(StatType stat) const {
    float flat = 0.0f;
    float percent = 0.0f;
    float percentFinal = 0.0f;
    
    for (const auto& mod : getAllModifiers()) {
        if (mod.stat != stat) continue;
        if (mod.condition && !mod.condition()) continue;
        
        switch (mod.type) {
            case ModifierType::Flat: flat += mod.value; break;
            case ModifierType::Percent: percent += mod.value; break;
            case ModifierType::PercentFinal: percentFinal += mod.value; break;
        }
    }
    
    float result = flat * (1.0f + percent / 100.0f);
    result *= (1.0f + percentFinal / 100.0f);
    
    return result;
}

int EquipmentLoadout::getTotalArmor() const {
    int total = 0;
    
    for (const auto& [slot, item] : equipped_) {
        const ItemDefinition* def = ItemDatabase::getInstance().getDefinition(item.itemId);
        if (def) {
            total += def->armorValue;
        }
    }
    
    return total;
}

std::pair<float, float> EquipmentLoadout::getWeaponDamage() const {
    const ItemInstance* weapon = getEquipped(EquipmentSlot::MainHand);
    if (!weapon) return { 0.0f, 0.0f };
    
    const ItemDefinition* def = ItemDatabase::getInstance().getDefinition(weapon->itemId);
    if (!def) return { 0.0f, 0.0f };
    
    return { def->minDamage, def->maxDamage };
}

std::vector<std::pair<EquipmentSlot, const ItemInstance*>> EquipmentLoadout::getAllEquipped() const {
    std::vector<std::pair<EquipmentSlot, const ItemInstance*>> result;
    for (const auto& [slot, item] : equipped_) {
        result.push_back({ slot, &item });
    }
    return result;
}

// ============================================================================
// ITEM DATABASE IMPLEMENTATION
// ============================================================================

void ItemDatabase::registerItem(const ItemDefinition& def) {
    items_[def.id] = def;
}

const ItemDefinition* ItemDatabase::getDefinition(const ItemID& id) const {
    auto it = items_.find(id);
    return it != items_.end() ? &it->second : nullptr;
}

ItemInstance ItemDatabase::createInstance(const ItemID& id, int count) {
    ItemInstance instance;
    instance.instanceId = nextInstanceId_++;
    instance.itemId = id;
    instance.stackCount = count;
    
    const ItemDefinition* def = getDefinition(id);
    if (def && def->isEquippable) {
        instance.durability = 100.0f;
        instance.maxDurability = 100.0f;
    }
    
    return instance;
}

std::vector<const ItemDefinition*> ItemDatabase::getItemsByCategory(ItemCategory category) const {
    std::vector<const ItemDefinition*> result;
    for (const auto& [id, def] : items_) {
        if (def.category == category) {
            result.push_back(&def);
        }
    }
    return result;
}

std::vector<const ItemDefinition*> ItemDatabase::getItemsByTag(const std::string& tag) const {
    std::vector<const ItemDefinition*> result;
    for (const auto& [id, def] : items_) {
        for (const auto& t : def.tags) {
            if (t == tag) {
                result.push_back(&def);
                break;
            }
        }
    }
    return result;
}

std::vector<const ItemDefinition*> ItemDatabase::searchItems(const std::string& query) const {
    std::vector<const ItemDefinition*> result;
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    
    for (const auto& [id, def] : items_) {
        std::string lowerName = def.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        
        if (lowerName.find(lowerQuery) != std::string::npos) {
            result.push_back(&def);
        }
    }
    return result;
}

bool ItemDatabase::loadFromFile(const std::string& path) {
    using json = nlohmann::json;
    
    std::ifstream file(path);
    if (!file.is_open()) return false;
    
    json doc;
    try {
        file >> doc;
    } catch (const std::exception&) {
        return false;
    }
    
    for (const auto& item : doc["items"]) {
        ItemDefinition def;
        def.id = item.value("id", "");
        def.name = item.value("name", "");
        def.description = item.value("description", "");
        def.iconPath = item.value("icon", "");
        def.modelPath = item.value("model", "");
        def.category = static_cast<ItemCategory>(item.value("category", 0));
        def.rarity = static_cast<ItemRarity>(item.value("rarity", 0));
        def.isStackable = item.value("stackable", true);
        def.maxStackSize = item.value("maxStack", 99);
        def.buyPrice = item.value("buyPrice", 0);
        def.sellPrice = item.value("sellPrice", 0);
        def.weight = item.value("weight", 0.0f);
        def.isEquippable = item.value("equippable", false);
        def.equipSlot = static_cast<EquipmentSlot>(item.value("equipSlot", 0));
        def.requiredLevel = item.value("requiredLevel", 0);
        def.minDamage = item.value("minDamage", 0.0f);
        def.maxDamage = item.value("maxDamage", 0.0f);
        def.armorValue = item.value("armor", 0);
        def.isConsumable = item.value("consumable", false);
        def.isQuestItem = item.value("questItem", false);
        
        if (item.contains("tags")) {
            def.tags = item["tags"].get<std::vector<std::string>>();
        }
        
        registerItem(def);
    }
    
    return true;
}

bool ItemDatabase::saveToFile(const std::string& path) const {
    using json = nlohmann::json;
    
    json doc;
    doc["items"] = json::array();
    
    for (const auto& [id, def] : items_) {
        json item;
        item["id"] = def.id;
        item["name"] = def.name;
        item["description"] = def.description;
        item["icon"] = def.iconPath;
        item["model"] = def.modelPath;
        item["category"] = static_cast<int>(def.category);
        item["rarity"] = static_cast<int>(def.rarity);
        item["stackable"] = def.isStackable;
        item["maxStack"] = def.maxStackSize;
        item["buyPrice"] = def.buyPrice;
        item["sellPrice"] = def.sellPrice;
        item["weight"] = def.weight;
        item["equippable"] = def.isEquippable;
        item["equipSlot"] = static_cast<int>(def.equipSlot);
        item["requiredLevel"] = def.requiredLevel;
        item["minDamage"] = def.minDamage;
        item["maxDamage"] = def.maxDamage;
        item["armor"] = def.armorValue;
        item["consumable"] = def.isConsumable;
        item["questItem"] = def.isQuestItem;
        item["tags"] = def.tags;
        
        doc["items"].push_back(item);
    }
    
    std::ofstream file(path);
    if (!file.is_open()) return false;
    
    file << doc.dump(2);
    return true;
}

// ============================================================================
// CRAFTING SYSTEM IMPLEMENTATION
// ============================================================================

void CraftingSystem::registerRecipe(const CraftingRecipe& recipe) {
    recipes_[recipe.recipeId] = recipe;
}

const CraftingRecipe* CraftingSystem::getRecipe(const std::string& recipeId) const {
    auto it = recipes_.find(recipeId);
    return it != recipes_.end() ? &it->second : nullptr;
}

std::vector<const CraftingRecipe*> CraftingSystem::getRecipesForItem(const ItemID& itemId) const {
    std::vector<const CraftingRecipe*> result;
    for (const auto& [id, recipe] : recipes_) {
        if (recipe.resultItemId == itemId) {
            result.push_back(&recipe);
        }
    }
    return result;
}

std::vector<const CraftingRecipe*> CraftingSystem::getRecipesUsingIngredient(const ItemID& itemId) const {
    std::vector<const CraftingRecipe*> result;
    for (const auto& [id, recipe] : recipes_) {
        if (recipe.ingredients.find(itemId) != recipe.ingredients.end()) {
            result.push_back(&recipe);
        }
    }
    return result;
}

bool CraftingSystem::canCraft(const std::string& recipeId, const InventoryContainer& inventory,
                               int playerLevel) const {
    const CraftingRecipe* recipe = getRecipe(recipeId);
    if (!recipe) return false;
    
    if (playerLevel < recipe->requiredLevel) return false;
    
    for (const auto& [itemId, count] : recipe->ingredients) {
        if (!inventory.hasItem(itemId, count)) {
            return false;
        }
    }
    
    return true;
}

bool CraftingSystem::craft(const std::string& recipeId, InventoryContainer& inventory) {
    const CraftingRecipe* recipe = getRecipe(recipeId);
    if (!recipe) return false;
    
    if (!canCraft(recipeId, inventory, 0)) return false;
    
    // Check success chance
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    
    if (dis(gen) > recipe->successChance) {
        // Failed - still consume some ingredients
        return false;
    }
    
    // Remove ingredients
    ItemDatabase& db = ItemDatabase::getInstance();
    for (const auto& [itemId, count] : recipe->ingredients) {
        inventory.removeItemById(itemId, count, db);
    }
    
    // Add result
    ItemInstance result = db.createInstance(recipe->resultItemId, recipe->resultCount);
    inventory.addItem(result, db);
    
    return true;
}

std::vector<const CraftingRecipe*> CraftingSystem::getAvailableRecipes(
    const InventoryContainer& inventory, int playerLevel) const {
    
    std::vector<const CraftingRecipe*> result;
    for (const auto& [id, recipe] : recipes_) {
        if (canCraft(id, inventory, playerLevel)) {
            result.push_back(&recipe);
        }
    }
    return result;
}

// ============================================================================
// INVENTORY COMPONENT IMPLEMENTATION
// ============================================================================

void InventoryComponent::recalculateWeight(const ItemDatabase& db) {
    currentWeight = 0.0f;
    
    for (int i = 0; i < inventory.getSlotCount(); ++i) {
        const InventorySlot* slot = inventory.getSlot(i);
        if (slot && slot->hasItem()) {
            const ItemInstance* item = slot->getItem();
            const ItemDefinition* def = db.getDefinition(item->itemId);
            if (def) {
                currentWeight += def->weight * item->stackCount;
            }
        }
    }
    
    isOverEncumbered = maxCarryWeight > 0 && currentWeight > maxCarryWeight;
}

bool InventoryComponent::canCarry(float additionalWeight) const {
    if (maxCarryWeight <= 0) return true;
    return currentWeight + additionalWeight <= maxCarryWeight;
}

// ============================================================================
// LOOT TABLE IMPLEMENTATION
// ============================================================================

LootTable::LootTable(const std::string& id)
    : id(id)
{
}

void LootTable::addEntry(const LootEntry& entry) {
    entries_.push_back(entry);
}

std::vector<ItemInstance> LootTable::generateLoot(int playerLevel, float magicFind) const {
    std::vector<ItemInstance> result;
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    
    // Guaranteed drops
    for (int i = 0; i < guaranteedDrops; ++i) {
        auto item = rollOnce(playerLevel);
        if (item) {
            result.push_back(*item);
        }
    }
    
    // Random drops
    for (const auto& entry : entries_) {
        if (result.size() >= static_cast<size_t>(maxDrops)) break;
        
        // Level filter
        if (entry.minLevel > 0 && playerLevel < entry.minLevel) continue;
        if (entry.maxLevel > 0 && playerLevel > entry.maxLevel) continue;
        
        // Drop chance (modified by magic find for rare items)
        float chance = entry.dropChance;
        const ItemDefinition* def = ItemDatabase::getInstance().getDefinition(entry.itemId);
        if (def && static_cast<int>(def->rarity) >= static_cast<int>(ItemRarity::Rare)) {
            chance *= magicFind;
        }
        
        if (dis(gen) <= chance) {
            std::uniform_int_distribution<int> countDis(entry.minCount, entry.maxCount);
            int count = countDis(gen);
            
            ItemInstance item = ItemDatabase::getInstance().createInstance(entry.itemId, count);
            result.push_back(item);
        }
    }
    
    return result;
}

std::optional<ItemInstance> LootTable::rollOnce(int playerLevel) const {
    if (entries_.empty()) return std::nullopt;
    
    // Weighted random selection
    float totalWeight = 0.0f;
    for (const auto& entry : entries_) {
        if (entry.minLevel > 0 && playerLevel < entry.minLevel) continue;
        if (entry.maxLevel > 0 && playerLevel > entry.maxLevel) continue;
        totalWeight += entry.weight;
    }
    
    if (totalWeight <= 0.0f) return std::nullopt;
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, totalWeight);
    
    float roll = dis(gen);
    float cumulative = 0.0f;
    
    for (const auto& entry : entries_) {
        if (entry.minLevel > 0 && playerLevel < entry.minLevel) continue;
        if (entry.maxLevel > 0 && playerLevel > entry.maxLevel) continue;
        
        cumulative += entry.weight;
        if (roll <= cumulative) {
            std::uniform_int_distribution<int> countDis(entry.minCount, entry.maxCount);
            int count = countDis(gen);
            
            return ItemDatabase::getInstance().createInstance(entry.itemId, count);
        }
    }
    
    return std::nullopt;
}

// ============================================================================
// INVENTORY SYSTEM IMPLEMENTATION
// ============================================================================

InventorySystem::InventorySystem() {
}

void InventorySystem::init(World& world) {
    world_ = &world;
}

void InventorySystem::update(World& world, float deltaTime) {
    // Update world items (bobbing, rotation, despawn)
    world.query<WorldItemComponent, Transform>([&](Entity entity, 
                                                    WorldItemComponent& worldItem,
                                                    Transform& transform) {
        worldItem.spawnTime += deltaTime;
        
        // Despawn check
        if (worldItem.canDespawn && worldItem.spawnTime >= worldItem.despawnTime) {
            world.destroyEntity(entity);
            return;
        }
        
        // Visual effects
        if (worldItem.bobbing) {
            float bob = sin(worldItem.spawnTime * 2.0f) * 0.1f;
            transform.position.y += bob * deltaTime;
        }
        
        if (worldItem.rotating) {
            float rotation = worldItem.rotationSpeed * deltaTime;
            transform.rotation = glm::rotate(transform.rotation, 
                                             glm::radians(rotation),
                                             glm::vec3(0, 1, 0));
        }
    });
}

void InventorySystem::shutdown(World& world) {
    world_ = nullptr;
}

bool InventorySystem::transferItem(Entity from, Entity to, int slotIndex, int count) {
    if (!world_) return false;
    
    auto* fromInv = world_->getComponent<InventoryComponent>(from);
    auto* toInv = world_->getComponent<InventoryComponent>(to);
    
    if (!fromInv || !toInv) return false;
    
    auto item = fromInv->inventory.removeItem(slotIndex, count);
    if (!item) return false;
    
    if (!toInv->inventory.addItem(*item, getDatabase())) {
        // Return item on failure
        fromInv->inventory.addItem(*item, getDatabase());
        return false;
    }
    
    return true;
}

Entity InventorySystem::dropItem(World& world, Entity owner, int slotIndex, int count) {
    auto* inv = world.getComponent<InventoryComponent>(owner);
    auto* transform = world.getComponent<Transform>(owner);
    
    if (!inv || !transform) return INVALID_ENTITY;
    
    auto item = inv->inventory.removeItem(slotIndex, count);
    if (!item) return INVALID_ENTITY;
    
    // Create world item entity
    Entity itemEntity = world.createEntity();
    
    Transform& itemTransform = world.addComponent<Transform>(itemEntity);
    itemTransform.position = transform->position + glm::vec3(0, 0.5f, 1.0f);
    itemTransform.scale = glm::vec3(0.5f);
    
    WorldItemComponent& worldItem = world.addComponent<WorldItemComponent>(itemEntity);
    worldItem.item = *item;
    worldItem.owner = owner;
    
    if (onItemDropped_) {
        onItemDropped_(owner, *item);
    }
    
    return itemEntity;
}

bool InventorySystem::pickUpItem(World& world, Entity picker, Entity itemEntity) {
    auto* inv = world.getComponent<InventoryComponent>(picker);
    auto* worldItem = world.getComponent<WorldItemComponent>(itemEntity);
    
    if (!inv || !worldItem) return false;
    
    // Check ownership
    if (worldItem->owner != INVALID_ENTITY && 
        worldItem->owner != picker &&
        worldItem->spawnTime < worldItem->ownershipTime) {
        return false;  // Belongs to someone else still
    }
    
    // Auto-pickup delay
    if (worldItem->autoPickup && worldItem->spawnTime < worldItem->autoPickupDelay) {
        return false;
    }
    
    if (!inv->inventory.addItem(worldItem->item, getDatabase())) {
        return false;  // Inventory full
    }
    
    if (onItemPickedUp_) {
        onItemPickedUp_(picker, worldItem->item);
    }
    
    world.destroyEntity(itemEntity);
    return true;
}

bool InventorySystem::useItem(World& world, Entity user, int slotIndex) {
    auto* inv = world.getComponent<InventoryComponent>(user);
    if (!inv) return false;
    
    InventorySlot* slot = inv->inventory.getSlot(slotIndex);
    if (!slot || !slot->hasItem()) return false;
    
    ItemInstance* item = slot->getItem();
    const ItemDefinition* def = getDatabase().getDefinition(item->itemId);
    
    if (!def || !def->isConsumable) return false;
    
    // Use the item
    if (def->useEffect) {
        def->useEffect(user);
    }
    
    if (onItemUsed_) {
        onItemUsed_(user, *item);
    }
    
    // Consume
    item->stackCount--;
    if (item->stackCount <= 0) {
        slot->item.reset();
    }
    
    return true;
}

bool InventorySystem::equipItem(Entity entity, int inventorySlot) {
    if (!world_) return false;
    
    auto* inv = world_->getComponent<InventoryComponent>(entity);
    if (!inv) return false;
    
    InventorySlot* slot = inv->inventory.getSlot(inventorySlot);
    if (!slot || !slot->hasItem()) return false;
    
    const ItemInstance& item = *slot->getItem();
    const ItemDefinition* def = getDatabase().getDefinition(item.itemId);
    
    if (!def || !def->isEquippable) return false;
    
    // Unequip existing item
    auto unequipped = inv->equipment.unequip(def->equipSlot);
    
    // Equip new item
    if (!inv->equipment.equip(item, getDatabase())) {
        // Put back unequipped item
        if (unequipped) {
            inv->equipment.equip(*unequipped, getDatabase());
        }
        return false;
    }
    
    // Remove from inventory
    slot->item.reset();
    
    // Put unequipped item in inventory
    if (unequipped) {
        inv->inventory.addItem(*unequipped, getDatabase());
    }
    
    if (onItemEquipped_) {
        onItemEquipped_(entity, item);
    }
    
    return true;
}

bool InventorySystem::unequipItem(Entity entity, EquipmentSlot slot) {
    if (!world_) return false;
    
    auto* inv = world_->getComponent<InventoryComponent>(entity);
    if (!inv) return false;
    
    auto item = inv->equipment.unequip(slot);
    if (!item) return false;
    
    if (!inv->inventory.addItem(*item, getDatabase())) {
        // Put back if inventory is full
        inv->equipment.equip(*item, getDatabase());
        return false;
    }
    
    if (onItemUnequipped_) {
        onItemUnequipped_(entity, *item);
    }
    
    return true;
}

} // namespace Sanic
