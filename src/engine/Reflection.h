/**
 * Reflection.h
 * 
 * Compile-time and runtime reflection system for the Sanic Engine.
 * Inspired by Unreal Engine's UPROPERTY/UCLASS macros.
 * 
 * Features:
 * - SPROPERTY macro for automatic serialization and editor exposure
 * - Type registration system with metadata
 * - Property enumeration for components
 * - Editor metadata for UI customization
 * - Supports primitive types, vectors, quaternions, entity refs, strings, arrays
 * 
 * Usage:
 *   struct PlayerStats {
 *       SPROPERTY(EditAnywhere, Category = "Combat", DisplayName = "Max Health")
 *       float maxHealth = 100.0f;
 *       
 *       SPROPERTY(EditAnywhere, Category = "Combat", ClampMin = 0, ClampMax = 1)
 *       float armor = 0.5f;
 *       
 *       SPROPERTY(VisibleAnywhere, Category = "Runtime")
 *       float currentHealth = 100.0f;
 *       
 *       SANIC_GENERATED_BODY()
 *   };
 *   SANIC_REGISTER_STRUCT(PlayerStats);
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <typeindex>
#include <any>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Sanic {

// ============================================================================
// PROPERTY FLAGS
// ============================================================================

enum class EPropertyFlags : uint32_t {
    None = 0,
    
    // Editor visibility
    EditAnywhere = 1 << 0,         // Editable in editor (instances + defaults)
    EditDefaultsOnly = 1 << 1,     // Only editable on archetypes/prefabs
    EditInstanceOnly = 1 << 2,     // Only editable on instances
    VisibleAnywhere = 1 << 3,      // Read-only in editor
    VisibleDefaultsOnly = 1 << 4,  // Read-only on defaults
    VisibleInstanceOnly = 1 << 5,  // Read-only on instances
    
    // Serialization
    Transient = 1 << 6,            // Don't serialize (runtime only)
    SaveGame = 1 << 7,             // Include in save game data
    Config = 1 << 8,               // Saved to config file
    
    // Scripting exposure
    ScriptReadWrite = 1 << 9,      // Accessible from C# scripts (read + write)
    ScriptReadOnly = 1 << 10,      // Accessible from C# scripts (read only)
    
    // Networking
    Replicated = 1 << 11,          // Replicated to clients
    ReplicatedUsing = 1 << 12,     // Replicated with notification callback
    
    // Special behaviors
    ExposeOnSpawn = 1 << 13,       // Shown when spawning from prefab
    AssetRef = 1 << 14,            // Asset reference picker in editor
    EntityRef = 1 << 15,           // Entity reference picker in editor
    InlineEditCondition = 1 << 16, // Edit condition shown inline
    
    // Advanced
    NoClear = 1 << 17,             // Hide "clear" button on object refs
    NoExport = 1 << 18,            // Exclude from text export
    Interp = 1 << 19,              // Interpolatable for cinematic curves
    NonPIEDuplicate = 1 << 20,     // Don't duplicate for PIE
    
    // Computed flags
    Editable = EditAnywhere | EditDefaultsOnly | EditInstanceOnly,
    Visible = VisibleAnywhere | VisibleDefaultsOnly | VisibleInstanceOnly,
    Serializable = ~(Transient),
    ScriptAccessible = ScriptReadWrite | ScriptReadOnly
};

inline EPropertyFlags operator|(EPropertyFlags a, EPropertyFlags b) {
    return static_cast<EPropertyFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline EPropertyFlags operator&(EPropertyFlags a, EPropertyFlags b) {
    return static_cast<EPropertyFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasFlag(EPropertyFlags flags, EPropertyFlags check) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(check)) != 0;
}

// ============================================================================
// PROPERTY TYPES
// ============================================================================

enum class EPropertyType : uint8_t {
    Unknown = 0,
    Bool,
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Float,
    Double,
    String,
    Vec2,
    Vec3,
    Vec4,
    Quat,
    Mat4,
    Color,
    Entity,         // Entity reference
    Asset,          // Asset path
    Struct,         // Nested struct
    Array,          // Dynamic array
    Map,            // Key-value map
    Enum,           // Enumeration
    Object,         // UObject-like pointer
    SoftObject      // Soft reference (path-based)
};

// ============================================================================
// PROPERTY METADATA
// ============================================================================

struct PropertyMeta {
    std::string displayName;           // Custom display name
    std::string tooltip;               // Tooltip text
    std::string category;              // Category for grouping
    
    // Numeric constraints
    std::optional<double> clampMin;    // Minimum value
    std::optional<double> clampMax;    // Maximum value
    std::optional<double> uiMin;       // UI slider minimum
    std::optional<double> uiMax;       // UI slider maximum
    std::optional<double> sliderExponent; // Non-linear slider
    double delta = 0.1;                // Step value for spinbox
    std::string units;                 // Display units (e.g., "m/s", "degrees")
    
    // Conditional visibility
    std::string editCondition;         // Property name for edit condition
    bool editConditionHides = false;   // Hide instead of disable when condition fails
    
    // Array behavior
    std::string arrayTitleProperty;    // Property name to use as array element title
    bool noElementDuplicate = false;   // Disable array element duplication
    int32_t maxArraySize = -1;         // Maximum array size (-1 = unlimited)
    
    // Object/Asset filters
    std::string allowedClasses;        // Comma-separated allowed class names
    std::string disallowedClasses;     // Comma-separated disallowed class names
    std::string assetBundles;          // Asset bundle filter
    
    // UI customization
    bool inlineEditCondition = false;  // Show condition inline
    bool showOnlyInnerProperties = false; // Hide struct header, show children
    bool allowPrivateAccess = false;   // Allow access to private members
    
    // Custom widget
    std::string customWidget;          // Custom widget type name
    std::unordered_map<std::string, std::string> widgetParams; // Widget parameters
};

// ============================================================================
// PROPERTY DESCRIPTOR
// ============================================================================

struct PropertyDescriptor {
    std::string name;                  // Property name (variable name)
    EPropertyType type;                // Property type enum
    EPropertyFlags flags;              // Property flags
    PropertyMeta meta;                 // Metadata for editor/serialization
    
    size_t offset;                     // Byte offset within struct
    size_t size;                       // Size in bytes
    std::type_index typeInfo = std::type_index(typeid(void)); // C++ type info
    
    // For arrays
    EPropertyType elementType = EPropertyType::Unknown;
    const PropertyDescriptor* elementDescriptor = nullptr;
    
    // For structs
    std::string structTypeName;
    
    // For enums
    std::vector<std::pair<std::string, int64_t>> enumValues;
    
    // Accessors
    std::function<std::any(const void*)> getter;
    std::function<void(void*, const std::any&)> setter;
    
    // Serialization
    std::function<void(const void*, std::ostream&)> serialize;
    std::function<void(void*, std::istream&)> deserialize;
    
    // Validation
    std::function<bool(const std::any&)> validator;
    std::function<std::string(const std::any&)> validationMessage;
    
    // Helper methods
    bool isEditable() const { return HasFlag(flags, EPropertyFlags::Editable); }
    bool isVisible() const { return HasFlag(flags, EPropertyFlags::Visible) || isEditable(); }
    bool isSerializable() const { return !HasFlag(flags, EPropertyFlags::Transient); }
    bool isScriptAccessible() const { return HasFlag(flags, EPropertyFlags::ScriptAccessible); }
};

// ============================================================================
// STRUCT DESCRIPTOR
// ============================================================================

struct StructDescriptor {
    std::string name;                              // Struct name
    std::string displayName;                       // Display name for editor
    std::string tooltip;                           // Tooltip description
    std::string category;                          // Category for grouping
    
    size_t size;                                   // Total struct size
    size_t alignment;                              // Struct alignment
    std::type_index typeInfo;                      // C++ type info
    
    std::vector<PropertyDescriptor> properties;    // All properties
    std::unordered_map<std::string, size_t> propertyMap; // Name to index
    
    // Parent struct (for inheritance)
    std::string parentName;
    const StructDescriptor* parent = nullptr;
    
    // Factory
    std::function<void*()> factory;                // Create default instance
    std::function<void(void*)> destructor;         // Destroy instance
    std::function<void*(const void*)> copier;      // Deep copy
    
    // Helpers
    const PropertyDescriptor* findProperty(const std::string& name) const {
        auto it = propertyMap.find(name);
        if (it != propertyMap.end()) {
            return &properties[it->second];
        }
        return parent ? parent->findProperty(name) : nullptr;
    }
    
    // Get all properties including inherited
    std::vector<const PropertyDescriptor*> getAllProperties() const {
        std::vector<const PropertyDescriptor*> result;
        if (parent) {
            auto parentProps = parent->getAllProperties();
            result.insert(result.end(), parentProps.begin(), parentProps.end());
        }
        for (const auto& prop : properties) {
            result.push_back(&prop);
        }
        return result;
    }
};

// ============================================================================
// TYPE REGISTRY
// ============================================================================

class TypeRegistry {
public:
    static TypeRegistry& getInstance() {
        static TypeRegistry instance;
        return instance;
    }
    
    // Register a struct type
    void registerStruct(const StructDescriptor& descriptor) {
        structs_[descriptor.name] = descriptor;
        typeIndexToName_[descriptor.typeInfo] = descriptor.name;
    }
    
    // Get struct descriptor by name
    const StructDescriptor* getStruct(const std::string& name) const {
        auto it = structs_.find(name);
        return it != structs_.end() ? &it->second : nullptr;
    }
    
    // Get struct descriptor by type_index
    const StructDescriptor* getStruct(std::type_index typeInfo) const {
        auto it = typeIndexToName_.find(typeInfo);
        if (it != typeIndexToName_.end()) {
            return getStruct(it->second);
        }
        return nullptr;
    }
    
    // Get struct descriptor by type
    template<typename T>
    const StructDescriptor* getStruct() const {
        return getStruct(std::type_index(typeid(T)));
    }
    
    // Get all registered structs
    std::vector<std::string> getRegisteredStructs() const {
        std::vector<std::string> result;
        for (const auto& [name, desc] : structs_) {
            result.push_back(name);
        }
        return result;
    }
    
    // Register an enum
    void registerEnum(const std::string& name, 
                      const std::vector<std::pair<std::string, int64_t>>& values) {
        enums_[name] = values;
    }
    
    const std::vector<std::pair<std::string, int64_t>>* getEnum(const std::string& name) const {
        auto it = enums_.find(name);
        return it != enums_.end() ? &it->second : nullptr;
    }
    
private:
    TypeRegistry() = default;
    
    std::unordered_map<std::string, StructDescriptor> structs_;
    std::unordered_map<std::type_index, std::string> typeIndexToName_;
    std::unordered_map<std::string, std::vector<std::pair<std::string, int64_t>>> enums_;
};

// ============================================================================
// PROPERTY BUILDER - Fluent API for building descriptors
// ============================================================================

class PropertyBuilder {
public:
    PropertyBuilder(const std::string& name, EPropertyType type, size_t offset, size_t size)
        : desc_{.name = name, .type = type, .flags = EPropertyFlags::EditAnywhere,
                .offset = offset, .size = size} {}
    
    PropertyBuilder& Flags(EPropertyFlags flags) { 
        desc_.flags = flags; 
        return *this; 
    }
    
    PropertyBuilder& DisplayName(const std::string& name) { 
        desc_.meta.displayName = name; 
        return *this; 
    }
    
    PropertyBuilder& Tooltip(const std::string& tip) { 
        desc_.meta.tooltip = tip; 
        return *this; 
    }
    
    PropertyBuilder& Category(const std::string& cat) { 
        desc_.meta.category = cat; 
        return *this; 
    }
    
    PropertyBuilder& ClampMin(double min) { 
        desc_.meta.clampMin = min; 
        return *this; 
    }
    
    PropertyBuilder& ClampMax(double max) { 
        desc_.meta.clampMax = max; 
        return *this; 
    }
    
    PropertyBuilder& UIRange(double min, double max) { 
        desc_.meta.uiMin = min;
        desc_.meta.uiMax = max;
        return *this; 
    }
    
    PropertyBuilder& Units(const std::string& units) { 
        desc_.meta.units = units; 
        return *this; 
    }
    
    PropertyBuilder& EditCondition(const std::string& condition, bool hides = false) {
        desc_.meta.editCondition = condition;
        desc_.meta.editConditionHides = hides;
        return *this;
    }
    
    PropertyBuilder& AllowedClasses(const std::string& classes) {
        desc_.meta.allowedClasses = classes;
        return *this;
    }
    
    PropertyBuilder& CustomWidget(const std::string& widget, 
                                   const std::unordered_map<std::string, std::string>& params = {}) {
        desc_.meta.customWidget = widget;
        desc_.meta.widgetParams = params;
        return *this;
    }
    
    template<typename T>
    PropertyBuilder& TypeInfo() {
        desc_.typeInfo = std::type_index(typeid(T));
        return *this;
    }
    
    PropertyBuilder& StructType(const std::string& structName) {
        desc_.structTypeName = structName;
        return *this;
    }
    
    PropertyBuilder& EnumValues(const std::vector<std::pair<std::string, int64_t>>& values) {
        desc_.enumValues = values;
        return *this;
    }
    
    PropertyBuilder& ArrayElement(EPropertyType elementType) {
        desc_.elementType = elementType;
        return *this;
    }
    
    template<typename C, typename T>
    PropertyBuilder& Accessors(T C::* member) {
        desc_.getter = [member](const void* obj) -> std::any {
            return static_cast<const C*>(obj)->*member;
        };
        desc_.setter = [member](void* obj, const std::any& value) {
            static_cast<C*>(obj)->*member = std::any_cast<T>(value);
        };
        return *this;
    }
    
    PropertyBuilder& Validator(std::function<bool(const std::any&)> func, 
                                const std::string& message = "") {
        desc_.validator = func;
        if (!message.empty()) {
            desc_.validationMessage = [message](const std::any&) { return message; };
        }
        return *this;
    }
    
    PropertyDescriptor build() { return desc_; }
    
private:
    PropertyDescriptor desc_;
};

// ============================================================================
// STRUCT BUILDER
// ============================================================================

class StructBuilder {
public:
    explicit StructBuilder(const std::string& name) {
        desc_.name = name;
        desc_.typeInfo = std::type_index(typeid(void));
    }
    
    StructBuilder& DisplayName(const std::string& name) {
        desc_.displayName = name;
        return *this;
    }
    
    StructBuilder& Tooltip(const std::string& tip) {
        desc_.tooltip = tip;
        return *this;
    }
    
    StructBuilder& Category(const std::string& cat) {
        desc_.category = cat;
        return *this;
    }
    
    StructBuilder& Parent(const std::string& parentName) {
        desc_.parentName = parentName;
        return *this;
    }
    
    template<typename T>
    StructBuilder& TypeInfo() {
        desc_.typeInfo = std::type_index(typeid(T));
        desc_.size = sizeof(T);
        desc_.alignment = alignof(T);
        desc_.factory = []() -> void* { return new T(); };
        desc_.destructor = [](void* ptr) { delete static_cast<T*>(ptr); };
        desc_.copier = [](const void* src) -> void* { return new T(*static_cast<const T*>(src)); };
        return *this;
    }
    
    StructBuilder& AddProperty(const PropertyDescriptor& prop) {
        desc_.propertyMap[prop.name] = desc_.properties.size();
        desc_.properties.push_back(prop);
        return *this;
    }
    
    StructBuilder& AddProperty(PropertyBuilder&& builder) {
        return AddProperty(builder.build());
    }
    
    StructDescriptor build() {
        // Resolve parent
        if (!desc_.parentName.empty()) {
            desc_.parent = TypeRegistry::getInstance().getStruct(desc_.parentName);
        }
        return desc_;
    }
    
    void registerStruct() {
        TypeRegistry::getInstance().registerStruct(build());
    }
    
private:
    StructDescriptor desc_;
};

// ============================================================================
// MACROS FOR PROPERTY REGISTRATION
// ============================================================================

// Helper to get member offset
#define SANIC_OFFSET_OF(Type, Member) offsetof(Type, Member)

// Helper to get member size
#define SANIC_SIZEOF(Type, Member) sizeof(((Type*)nullptr)->Member)

// Determine property type from C++ type
template<typename T> constexpr EPropertyType GetPropertyType() { return EPropertyType::Unknown; }
template<> constexpr EPropertyType GetPropertyType<bool>() { return EPropertyType::Bool; }
template<> constexpr EPropertyType GetPropertyType<int8_t>() { return EPropertyType::Int8; }
template<> constexpr EPropertyType GetPropertyType<int16_t>() { return EPropertyType::Int16; }
template<> constexpr EPropertyType GetPropertyType<int32_t>() { return EPropertyType::Int32; }
template<> constexpr EPropertyType GetPropertyType<int64_t>() { return EPropertyType::Int64; }
template<> constexpr EPropertyType GetPropertyType<uint8_t>() { return EPropertyType::UInt8; }
template<> constexpr EPropertyType GetPropertyType<uint16_t>() { return EPropertyType::UInt16; }
template<> constexpr EPropertyType GetPropertyType<uint32_t>() { return EPropertyType::UInt32; }
template<> constexpr EPropertyType GetPropertyType<uint64_t>() { return EPropertyType::UInt64; }
template<> constexpr EPropertyType GetPropertyType<float>() { return EPropertyType::Float; }
template<> constexpr EPropertyType GetPropertyType<double>() { return EPropertyType::Double; }
template<> constexpr EPropertyType GetPropertyType<std::string>() { return EPropertyType::String; }
template<> constexpr EPropertyType GetPropertyType<glm::vec2>() { return EPropertyType::Vec2; }
template<> constexpr EPropertyType GetPropertyType<glm::vec3>() { return EPropertyType::Vec3; }
template<> constexpr EPropertyType GetPropertyType<glm::vec4>() { return EPropertyType::Vec4; }
template<> constexpr EPropertyType GetPropertyType<glm::quat>() { return EPropertyType::Quat; }
template<> constexpr EPropertyType GetPropertyType<glm::mat4>() { return EPropertyType::Mat4; }

// Create property builder with automatic type detection
#define SANIC_PROPERTY(Type, Name) \
    PropertyBuilder(#Name, GetPropertyType<decltype(((Type*)nullptr)->Name)>(), \
                    SANIC_OFFSET_OF(Type, Name), SANIC_SIZEOF(Type, Name)) \
        .TypeInfo<decltype(((Type*)nullptr)->Name)>() \
        .Accessors(&Type::Name)

// Register a struct with the type registry
#define SANIC_REGISTER_STRUCT(StructType) \
    namespace { \
        struct StructType##_Registrar { \
            StructType##_Registrar() { \
                StructType::RegisterReflection(); \
            } \
        }; \
        static StructType##_Registrar s_##StructType##_registrar; \
    }

// Generated body macro - declares static RegisterReflection method
#define SANIC_GENERATED_BODY() \
    public: \
        static void RegisterReflection(); \
        static const Sanic::StructDescriptor* GetStaticStruct() { \
            return Sanic::TypeRegistry::getInstance().getStruct(typeid(std::remove_pointer_t<decltype(this)>)); \
        } \
    private:

// ============================================================================
// PROPERTY CHANGE NOTIFICATION
// ============================================================================

class IPropertyChangeListener {
public:
    virtual ~IPropertyChangeListener() = default;
    
    // Called before property changes
    virtual void onPropertyChanging(void* object, const PropertyDescriptor& property, 
                                     const std::any& oldValue, const std::any& newValue) {}
    
    // Called after property changes
    virtual void onPropertyChanged(void* object, const PropertyDescriptor& property,
                                    const std::any& oldValue, const std::any& newValue) {}
};

class PropertyNotifier {
public:
    static PropertyNotifier& getInstance() {
        static PropertyNotifier instance;
        return instance;
    }
    
    void addListener(IPropertyChangeListener* listener) {
        listeners_.push_back(listener);
    }
    
    void removeListener(IPropertyChangeListener* listener) {
        listeners_.erase(
            std::remove(listeners_.begin(), listeners_.end(), listener),
            listeners_.end()
        );
    }
    
    void notifyChanging(void* object, const PropertyDescriptor& property,
                        const std::any& oldValue, const std::any& newValue) {
        for (auto* listener : listeners_) {
            listener->onPropertyChanging(object, property, oldValue, newValue);
        }
    }
    
    void notifyChanged(void* object, const PropertyDescriptor& property,
                       const std::any& oldValue, const std::any& newValue) {
        for (auto* listener : listeners_) {
            listener->onPropertyChanged(object, property, oldValue, newValue);
        }
    }
    
private:
    std::vector<IPropertyChangeListener*> listeners_;
};

// ============================================================================
// PROPERTY ACCESS UTILITIES
// ============================================================================

class PropertyAccess {
public:
    // Get property value as typed
    template<typename T>
    static T getValue(const void* object, const PropertyDescriptor& prop) {
        if (prop.getter) {
            return std::any_cast<T>(prop.getter(object));
        }
        // Direct memory access fallback
        return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(object) + prop.offset);
    }
    
    // Set property value
    template<typename T>
    static void setValue(void* object, const PropertyDescriptor& prop, const T& value) {
        std::any oldValue;
        if (prop.getter) {
            oldValue = prop.getter(object);
        }
        
        PropertyNotifier::getInstance().notifyChanging(object, prop, oldValue, value);
        
        if (prop.setter) {
            prop.setter(object, value);
        } else {
            // Direct memory access fallback
            *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + prop.offset) = value;
        }
        
        PropertyNotifier::getInstance().notifyChanged(object, prop, oldValue, value);
    }
    
    // Get property value as any
    static std::any getValueAny(const void* object, const PropertyDescriptor& prop) {
        if (prop.getter) {
            return prop.getter(object);
        }
        
        // Type-based fallback
        switch (prop.type) {
            case EPropertyType::Bool:
                return getValue<bool>(object, prop);
            case EPropertyType::Int32:
                return getValue<int32_t>(object, prop);
            case EPropertyType::Float:
                return getValue<float>(object, prop);
            case EPropertyType::Double:
                return getValue<double>(object, prop);
            case EPropertyType::String:
                return getValue<std::string>(object, prop);
            case EPropertyType::Vec3:
                return getValue<glm::vec3>(object, prop);
            case EPropertyType::Quat:
                return getValue<glm::quat>(object, prop);
            default:
                return {};
        }
    }
    
    // Validate a value before setting
    static bool validate(const PropertyDescriptor& prop, const std::any& value, std::string& error) {
        if (prop.validator) {
            if (!prop.validator(value)) {
                if (prop.validationMessage) {
                    error = prop.validationMessage(value);
                } else {
                    error = "Validation failed for property: " + prop.name;
                }
                return false;
            }
        }
        
        // Numeric range validation
        if (prop.meta.clampMin || prop.meta.clampMax) {
            double numValue = 0.0;
            try {
                if (prop.type == EPropertyType::Float) {
                    numValue = std::any_cast<float>(value);
                } else if (prop.type == EPropertyType::Double) {
                    numValue = std::any_cast<double>(value);
                } else if (prop.type == EPropertyType::Int32) {
                    numValue = std::any_cast<int32_t>(value);
                }
                
                if (prop.meta.clampMin && numValue < *prop.meta.clampMin) {
                    error = prop.name + " must be >= " + std::to_string(*prop.meta.clampMin);
                    return false;
                }
                if (prop.meta.clampMax && numValue > *prop.meta.clampMax) {
                    error = prop.name + " must be <= " + std::to_string(*prop.meta.clampMax);
                    return false;
                }
            } catch (...) {}
        }
        
        return true;
    }
    
    // Clamp value to property constraints
    template<typename T>
    static T clampValue(const PropertyDescriptor& prop, const T& value) {
        if constexpr (std::is_arithmetic_v<T>) {
            T result = value;
            if (prop.meta.clampMin) {
                result = std::max(result, static_cast<T>(*prop.meta.clampMin));
            }
            if (prop.meta.clampMax) {
                result = std::min(result, static_cast<T>(*prop.meta.clampMax));
            }
            return result;
        }
        return value;
    }
};

} // namespace Sanic
