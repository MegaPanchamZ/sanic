/**
 * InventorySystem.h
 * 
 * Inventory and Item Management System
 * 
 * Features:
 * - Item definitions with properties
 * - Inventory containers with slots
 * - Equipment system with stat modifiers
 * - Item crafting and recipes
 * - Item stacking and splitting
 * - Drag and drop support
 * 
 * Reference:
 *   Engine/Plugins/Runtime/Inventory/
 */

#pragma once

#include "ECS.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <optional>

namespace Sanic {

// Forward declarations
class InventoryManager;
class ItemDatabase;

// ============================================================================
// ITEM TYPES
// ============================================================================

/**
 * Unique identifier for items
 */
using ItemID = std::string;
using InstanceID = uint64_t;
constexpr InstanceID INVALID_INSTANCE = 0;

/**
 * Item categories
 */
enum class ItemCategory {
    Weapon,
    Armor,
    Consumable,
    Material,
    Quest,
    Key,
    Currency,
    Misc
};

/**
 * Item rarity
 */
enum class ItemRarity {
    Common,
    Uncommon,
    Rare,
    Epic,
    Legendary,
    Unique
};

/**
 * Equipment slots
 */
enum class EquipmentSlot {
    None,
    Head,
    Chest,
    Hands,
    Legs,
    Feet,
    MainHand,
    OffHand,
    TwoHand,
    Ring1,
    Ring2,
    Amulet,
    Back,
    Trinket1,
    Trinket2
};

/**
 * Weapon types
 */
enum class WeaponType {
    None,
    Sword,
    Axe,
    Mace,
    Dagger,
    Spear,
    Staff,
    Bow,
    Crossbow,
    Shield
};

/**
 * Armor types
 */
enum class ArmorType {
    None,
    Cloth,
    Leather,
    Mail,
    Plate
};

// ============================================================================
// STAT MODIFIERS
// ============================================================================

/**
 * Types of stats that can be modified
 */
enum class StatType {
    // Primary stats
    Strength,
    Dexterity,
    Intelligence,
    Vitality,
    
    // Derived stats
    MaxHealth,
    MaxMana,
    MaxStamina,
    HealthRegen,
    ManaRegen,
    StaminaRegen,
    
    // Combat stats
    AttackPower,
    SpellPower,
    Armor,
    MagicResist,
    CritChance,
    CritDamage,
    AttackSpeed,
    CastSpeed,
    
    // Movement
    MoveSpeed,
    
    // Special
    ExperienceBonus,
    GoldFind,
    MagicFind
};

/**
 * How the stat modifier is applied
 */
enum class ModifierType {
    Flat,           // +10 Strength
    Percent,        // +10% Strength
    PercentFinal    // Applied after all other modifiers
};

/**
 * A single stat modifier
 */
struct StatModifier {
    StatType stat;
    ModifierType type = ModifierType::Flat;
    float value = 0.0f;
    
    // Optional condition (e.g., "while health above 50%")
    std::function<bool()> condition;
};

// ============================================================================
// ITEM DEFINITION
// ============================================================================

/**
 * Base definition of an item type (template)
 */
struct ItemDefinition {
    ItemID id;
    std::string name;
    std::string description;
    std::string iconPath;
    std::string modelPath;
    
    ItemCategory category = ItemCategory::Misc;
    ItemRarity rarity = ItemRarity::Common;
    
    // Stacking
    bool isStackable = true;
    int maxStackSize = 99;
    
    // Value
    int buyPrice = 0;
    int sellPrice = 0;
    
    // Weight (for encumbrance systems)
    float weight = 0.0f;
    
    // Equipment
    bool isEquippable = false;
    EquipmentSlot equipSlot = EquipmentSlot::None;
    WeaponType weaponType = WeaponType::None;
    ArmorType armorType = ArmorType::None;
    int requiredLevel = 0;
    
    // Base stats (for equipment)
    std::vector<StatModifier> statModifiers;
    
    // Weapon specific
    float minDamage = 0.0f;
    float maxDamage = 0.0f;
    float attackSpeed = 1.0f;
    float range = 1.0f;
    
    // Armor specific
    int armorValue = 0;
    
    // Consumable
    bool isConsumable = false;
    std::function<void(Entity user)> useEffect;
    float cooldown = 0.0f;
    
    // Quest item
    bool isQuestItem = false;
    bool isDestroyable = true;
    bool isTradeable = true;
    bool isDroppable = true;
    
    // Crafting
    bool isCraftingMaterial = false;
    
    // Tags for filtering
    std::vector<std::string> tags;
    
    /**
     * Get rarity color
     */
    glm::vec4 getRarityColor() const {
        switch (rarity) {
            case ItemRarity::Common: return glm::vec4(1, 1, 1, 1);
            case ItemRarity::Uncommon: return glm::vec4(0.12f, 0.8f, 0.12f, 1);
            case ItemRarity::Rare: return glm::vec4(0.12f, 0.56f, 1, 1);
            case ItemRarity::Epic: return glm::vec4(0.64f, 0.21f, 0.93f, 1);
            case ItemRarity::Legendary: return glm::vec4(1, 0.5f, 0, 1);
            case ItemRarity::Unique: return glm::vec4(0.9f, 0.8f, 0.5f, 1);
            default: return glm::vec4(1, 1, 1, 1);
        }
    }
};

// ============================================================================
// ITEM INSTANCE
// ============================================================================

/**
 * An actual item in the game (instance of a definition)
 */
struct ItemInstance {
    InstanceID instanceId = INVALID_INSTANCE;
    ItemID itemId;
    int stackCount = 1;
    
    // Instance-specific modifications
    std::vector<StatModifier> bonusModifiers;
    std::string customName;  // Empty = use definition name
    
    // Durability (for equipment)
    float durability = 100.0f;
    float maxDurability = 100.0f;
    
    // Level (for scaling items)
    int itemLevel = 1;
    
    // Sockets/gems
    std::vector<ItemID> socketedGems;
    int maxSockets = 0;
    
    // Enchantments
    std::vector<std::string> enchantments;
    
    // Bound status
    bool isBound = false;
    Entity boundToEntity = INVALID_ENTITY;
    
    // Metadata
    std::unordered_map<std::string, std::string> metadata;
    
    /**
     * Check if item can stack with another
     */
    bool canStackWith(const ItemInstance& other) const {
        return itemId == other.itemId && 
               bonusModifiers.empty() && other.bonusModifiers.empty() &&
               customName.empty() && other.customName.empty() &&
               socketedGems.empty() && other.socketedGems.empty() &&
               enchantments.empty() && other.enchantments.empty();
    }
};

// ============================================================================
// INVENTORY SLOT
// ============================================================================

/**
 * A single slot in an inventory
 */
struct InventorySlot {
    int slotIndex = -1;
    std::optional<ItemInstance> item;
    
    // Slot restrictions
    ItemCategory allowedCategory = ItemCategory::Misc;  // Misc = any
    bool isLocked = false;
    
    bool isEmpty() const { return !item.has_value(); }
    bool hasItem() const { return item.has_value(); }
    
    const ItemInstance* getItem() const {
        return item.has_value() ? &item.value() : nullptr;
    }
    
    ItemInstance* getItem() {
        return item.has_value() ? &item.value() : nullptr;
    }
};

// ============================================================================
// INVENTORY CONTAINER
// ============================================================================

/**
 * A container holding inventory slots
 */
class InventoryContainer {
public:
    InventoryContainer(int slotCount = 20);
    
    /**
     * Get slot by index
     */
    InventorySlot* getSlot(int index);
    const InventorySlot* getSlot(int index) const;
    
    /**
     * Get number of slots
     */
    int getSlotCount() const { return static_cast<int>(slots_.size()); }
    
    /**
     * Resize container
     */
    void resize(int newSize);
    
    /**
     * Add item to inventory (finds suitable slot)
     */
    bool addItem(const ItemInstance& item, const ItemDatabase& db);
    
    /**
     * Add item to specific slot
     */
    bool addItemToSlot(int slotIndex, const ItemInstance& item, const ItemDatabase& db);
    
    /**
     * Remove item from slot
     */
    std::optional<ItemInstance> removeItem(int slotIndex, int count = -1);
    
    /**
     * Remove item by ID
     */
    bool removeItemById(const ItemID& itemId, int count = 1, const ItemDatabase& db = ItemDatabase());
    
    /**
     * Move item between slots
     */
    bool moveItem(int fromSlot, int toSlot, const ItemDatabase& db);
    
    /**
     * Swap items between slots
     */
    bool swapItems(int slot1, int slot2);
    
    /**
     * Split stack
     */
    bool splitStack(int slotIndex, int splitCount, int targetSlot, const ItemDatabase& db);
    
    /**
     * Check if inventory contains item
     */
    bool hasItem(const ItemID& itemId, int count = 1) const;
    
    /**
     * Count items of a type
     */
    int countItem(const ItemID& itemId) const;
    
    /**
     * Find first slot with item
     */
    int findItem(const ItemID& itemId) const;
    
    /**
     * Find all slots with item
     */
    std::vector<int> findAllItems(const ItemID& itemId) const;
    
    /**
     * Find first empty slot
     */
    int findEmptySlot() const;
    
    /**
     * Get number of empty slots
     */
    int getEmptySlotCount() const;
    
    /**
     * Check if inventory is full
     */
    bool isFull() const { return getEmptySlotCount() == 0; }
    
    /**
     * Sort inventory
     */
    void sort(const ItemDatabase& db);
    
    /**
     * Clear all items
     */
    void clear();
    
    // Container properties
    std::string name = "Inventory";
    float maxWeight = 0.0f;  // 0 = unlimited
    
private:
    std::vector<InventorySlot> slots_;
};

// ============================================================================
// EQUIPMENT LOADOUT
// ============================================================================

/**
 * Currently equipped items
 */
class EquipmentLoadout {
public:
    EquipmentLoadout();
    
    /**
     * Get item in slot
     */
    const ItemInstance* getEquipped(EquipmentSlot slot) const;
    ItemInstance* getEquipped(EquipmentSlot slot);
    
    /**
     * Equip an item
     */
    bool equip(const ItemInstance& item, const ItemDatabase& db);
    
    /**
     * Unequip from slot
     */
    std::optional<ItemInstance> unequip(EquipmentSlot slot);
    
    /**
     * Check if slot is occupied
     */
    bool isSlotOccupied(EquipmentSlot slot) const;
    
    /**
     * Get all stat modifiers from equipment
     */
    std::vector<StatModifier> getAllModifiers() const;
    
    /**
     * Calculate total stat value
     */
    float getTotalStat(StatType stat) const;
    
    /**
     * Get total armor
     */
    int getTotalArmor() const;
    
    /**
     * Get weapon damage range
     */
    std::pair<float, float> getWeaponDamage() const;
    
    /**
     * Get all equipped items
     */
    std::vector<std::pair<EquipmentSlot, const ItemInstance*>> getAllEquipped() const;
    
private:
    std::unordered_map<EquipmentSlot, ItemInstance> equipped_;
};

// ============================================================================
// ITEM DATABASE
// ============================================================================

/**
 * Database of all item definitions
 */
class ItemDatabase {
public:
    static ItemDatabase& getInstance() {
        static ItemDatabase instance;
        return instance;
    }
    
    /**
     * Register an item definition
     */
    void registerItem(const ItemDefinition& def);
    
    /**
     * Get item definition
     */
    const ItemDefinition* getDefinition(const ItemID& id) const;
    
    /**
     * Create item instance
     */
    ItemInstance createInstance(const ItemID& id, int count = 1);
    
    /**
     * Get all items in category
     */
    std::vector<const ItemDefinition*> getItemsByCategory(ItemCategory category) const;
    
    /**
     * Get all items with tag
     */
    std::vector<const ItemDefinition*> getItemsByTag(const std::string& tag) const;
    
    /**
     * Search items by name
     */
    std::vector<const ItemDefinition*> searchItems(const std::string& query) const;
    
    /**
     * Load from file
     */
    bool loadFromFile(const std::string& path);
    
    /**
     * Save to file
     */
    bool saveToFile(const std::string& path) const;
    
private:
    ItemDatabase() = default;
    
    std::unordered_map<ItemID, ItemDefinition> items_;
    InstanceID nextInstanceId_ = 1;
};

// ============================================================================
// CRAFTING RECIPE
// ============================================================================

/**
 * Recipe for crafting items
 */
struct CraftingRecipe {
    std::string recipeId;
    std::string name;
    
    // Required items (itemId -> count)
    std::unordered_map<ItemID, int> ingredients;
    
    // Result
    ItemID resultItemId;
    int resultCount = 1;
    
    // Requirements
    int requiredLevel = 0;
    std::string requiredProfession;
    int requiredProfessionLevel = 0;
    
    // Crafting station
    std::string requiredStation;  // Empty = can craft anywhere
    
    // Crafting time
    float craftTime = 0.0f;
    
    // XP reward
    int craftingXP = 0;
    
    // Success chance
    float successChance = 1.0f;
    
    // Tags
    std::vector<std::string> tags;
};

/**
 * Crafting system
 */
class CraftingSystem {
public:
    static CraftingSystem& getInstance() {
        static CraftingSystem instance;
        return instance;
    }
    
    /**
     * Register a recipe
     */
    void registerRecipe(const CraftingRecipe& recipe);
    
    /**
     * Get recipe by ID
     */
    const CraftingRecipe* getRecipe(const std::string& recipeId) const;
    
    /**
     * Get recipes for an item
     */
    std::vector<const CraftingRecipe*> getRecipesForItem(const ItemID& itemId) const;
    
    /**
     * Get recipes using ingredient
     */
    std::vector<const CraftingRecipe*> getRecipesUsingIngredient(const ItemID& itemId) const;
    
    /**
     * Check if can craft
     */
    bool canCraft(const std::string& recipeId, const InventoryContainer& inventory,
                  int playerLevel = 0) const;
    
    /**
     * Craft item
     */
    bool craft(const std::string& recipeId, InventoryContainer& inventory);
    
    /**
     * Get available recipes for inventory
     */
    std::vector<const CraftingRecipe*> getAvailableRecipes(
        const InventoryContainer& inventory, int playerLevel = 0) const;
    
private:
    CraftingSystem() = default;
    
    std::unordered_map<std::string, CraftingRecipe> recipes_;
};

// ============================================================================
// INVENTORY COMPONENT
// ============================================================================

/**
 * Component for entities with inventory
 */
struct InventoryComponent {
    InventoryContainer inventory;
    EquipmentLoadout equipment;
    
    // Currency
    int gold = 0;
    
    // Weight system
    float currentWeight = 0.0f;
    float maxCarryWeight = 100.0f;
    bool isOverEncumbered = false;
    
    /**
     * Recalculate current weight
     */
    void recalculateWeight(const ItemDatabase& db);
    
    /**
     * Check if can carry additional weight
     */
    bool canCarry(float additionalWeight) const;
};

// ============================================================================
// LOOT TABLE
// ============================================================================

/**
 * Entry in a loot table
 */
struct LootEntry {
    ItemID itemId;
    float dropChance = 1.0f;     // 0.0 - 1.0
    int minCount = 1;
    int maxCount = 1;
    int minLevel = 0;            // Minimum level to drop
    int maxLevel = 0;            // 0 = no max level
    float weight = 1.0f;         // For weighted random selection
};

/**
 * Loot table for generating drops
 */
class LootTable {
public:
    LootTable(const std::string& id = "");
    
    /**
     * Add entry
     */
    void addEntry(const LootEntry& entry);
    
    /**
     * Generate loot
     */
    std::vector<ItemInstance> generateLoot(int playerLevel = 1, 
                                            float magicFind = 1.0f) const;
    
    /**
     * Roll once from table
     */
    std::optional<ItemInstance> rollOnce(int playerLevel = 1) const;
    
    std::string id;
    int guaranteedDrops = 0;     // Guaranteed number of items to drop
    int maxDrops = 5;            // Maximum items from one roll
    
private:
    std::vector<LootEntry> entries_;
};

// ============================================================================
// INVENTORY SYSTEM
// ============================================================================

/**
 * System managing inventories
 */
class InventorySystem : public System {
public:
    InventorySystem();
    
    void init(World& world) override;
    void update(World& world, float deltaTime) override;
    void shutdown(World& world) override;
    
    /**
     * Get item database
     */
    ItemDatabase& getDatabase() { return ItemDatabase::getInstance(); }
    
    /**
     * Get crafting system
     */
    CraftingSystem& getCrafting() { return CraftingSystem::getInstance(); }
    
    /**
     * Transfer item between entities
     */
    bool transferItem(Entity from, Entity to, int slotIndex, int count = -1);
    
    /**
     * Drop item from entity
     */
    Entity dropItem(World& world, Entity owner, int slotIndex, int count = -1);
    
    /**
     * Pick up item entity
     */
    bool pickUpItem(World& world, Entity picker, Entity itemEntity);
    
    /**
     * Use item
     */
    bool useItem(World& world, Entity user, int slotIndex);
    
    /**
     * Equip item
     */
    bool equipItem(Entity entity, int inventorySlot);
    
    /**
     * Unequip item
     */
    bool unequipItem(Entity entity, EquipmentSlot slot);
    
    // Callbacks
    using ItemCallback = std::function<void(Entity, const ItemInstance&)>;
    void setOnItemPickedUp(ItemCallback callback) { onItemPickedUp_ = callback; }
    void setOnItemDropped(ItemCallback callback) { onItemDropped_ = callback; }
    void setOnItemUsed(ItemCallback callback) { onItemUsed_ = callback; }
    void setOnItemEquipped(ItemCallback callback) { onItemEquipped_ = callback; }
    void setOnItemUnequipped(ItemCallback callback) { onItemUnequipped_ = callback; }
    
private:
    World* world_ = nullptr;
    
    ItemCallback onItemPickedUp_;
    ItemCallback onItemDropped_;
    ItemCallback onItemUsed_;
    ItemCallback onItemEquipped_;
    ItemCallback onItemUnequipped_;
};

// ============================================================================
// WORLD ITEM COMPONENT
// ============================================================================

/**
 * Component for items in the world (dropped/spawned)
 */
struct WorldItemComponent {
    ItemInstance item;
    
    // Pickup settings
    float pickupRadius = 1.5f;
    bool autoPickup = false;
    float autoPickupDelay = 0.5f;
    float spawnTime = 0.0f;
    
    // Despawn
    bool canDespawn = true;
    float despawnTime = 300.0f;  // 5 minutes
    
    // Physics
    bool hasGravity = true;
    bool hasCollision = true;
    
    // Visual
    bool bobbing = true;
    bool rotating = true;
    float rotationSpeed = 45.0f;
    
    // Owner (for loot rights)
    Entity owner = INVALID_ENTITY;
    float ownershipTime = 60.0f;  // Others can pick up after this
};

} // namespace Sanic
