/**
 * ScriptingInspector.h
 * 
 * Editor Inspector UI for C# Script Properties.
 * Provides [SerializeField] attribute exposure and visual debugging.
 * 
 * Inspired by Unity's Inspector and Unreal's Details Panel.
 */

#pragma once

#include "ScriptingSystem.h"
#include "Reflection.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <variant>
#include <unordered_map>

namespace Sanic {

// Forward declarations
class Entity;
class World;

// ============================================================================
// SERIALIZED FIELD METADATA
// ============================================================================

/**
 * Field visibility in inspector
 */
enum class EFieldVisibility : uint8_t {
    Hidden = 0,           // Not shown
    ReadOnly = 1,         // Shown but not editable
    Editable = 2,         // Fully editable
    Debug = 3             // Only shown in debug mode
};

/**
 * Field category for grouping
 */
enum class EFieldCategory : uint8_t {
    Default = 0,
    Transform = 1,
    Rendering = 2,
    Physics = 3,
    Audio = 4,
    Gameplay = 5,
    AI = 6,
    Network = 7,
    Custom = 8
};

/**
 * Widget type for custom rendering
 */
enum class EWidgetType : uint8_t {
    Auto = 0,             // Automatic based on type
    Slider = 1,           // Slider for numeric values
    ColorPicker = 2,      // Color picker for vector3/4
    AssetPicker = 3,      // Asset browser reference
    ObjectPicker = 4,     // Scene object reference
    Curve = 5,            // Animation curve editor
    Gradient = 6,         // Gradient editor
    MultiLine = 7,        // Multiline text
    Password = 8,         // Hidden text
    Dropdown = 9,         // Enum dropdown
    Toggle = 10,          // Boolean toggle
    Button = 11,          // Button that calls method
    ProgressBar = 12,     // Read-only progress
    MinMaxSlider = 13     // Range slider
};

/**
 * Serialized field info from C# script
 */
struct FSerializedField {
    std::string name;
    std::string displayName;          // [Header("Display Name")]
    std::string tooltip;              // [Tooltip("...")]
    std::string category;             // [Category("...")]
    
    EFieldVisibility visibility = EFieldVisibility::Editable;
    EWidgetType widgetType = EWidgetType::Auto;
    
    // Type info
    std::string typeName;             // C# type name
    bool isArray = false;
    bool isReference = false;         // Reference to another object
    
    // Constraints
    bool hasRange = false;
    float rangeMin = 0.0f;
    float rangeMax = 1.0f;
    
    // Current value (polymorphic)
    using ValueType = std::variant<
        std::monostate,               // null/none
        bool,                         // Boolean
        int32_t,                      // Int32
        int64_t,                      // Int64
        float,                        // Single
        double,                       // Double
        std::string,                  // String
        glm::vec2,                    // Vector2
        glm::vec3,                    // Vector3
        glm::vec4,                    // Vector4/Color
        glm::quat,                    // Quaternion
        uint64_t,                     // Object reference (entity ID)
        std::vector<uint8_t>          // Array/complex data
    >;
    ValueType value;
    
    // Callback when value changes
    std::function<void(const ValueType&)> onValueChanged;
};

/**
 * Script component metadata
 */
struct FScriptComponentInfo {
    std::string className;
    std::string displayName;
    std::string description;
    std::string iconPath;
    EFieldCategory defaultCategory = EFieldCategory::Gameplay;
    
    std::vector<FSerializedField> fields;
    std::vector<std::string> methodButtons;  // Methods with [Button] attribute
    
    bool allowMultiple = false;
    bool requireComponent = false;           // Has [RequireComponent] attribute
    std::vector<std::string> requiredComponents;
};

// ============================================================================
// INSPECTOR INTERFACE
// ============================================================================

/**
 * Base class for inspector widgets
 */
class IInspectorWidget {
public:
    virtual ~IInspectorWidget() = default;
    
    virtual void render() = 0;
    virtual void update() {}
    
    // Value binding
    virtual void bindField(FSerializedField* field) { boundField_ = field; }
    FSerializedField* getBoundField() const { return boundField_; }
    
protected:
    FSerializedField* boundField_ = nullptr;
};

/**
 * Widget factory for creating appropriate UI widgets
 */
class InspectorWidgetFactory {
public:
    using WidgetCreator = std::function<std::unique_ptr<IInspectorWidget>()>;
    
    static InspectorWidgetFactory& get();
    
    void registerWidget(EWidgetType type, WidgetCreator creator);
    void registerWidget(const std::string& typeName, WidgetCreator creator);
    
    std::unique_ptr<IInspectorWidget> createWidget(const FSerializedField& field);
    
private:
    InspectorWidgetFactory() = default;
    std::unordered_map<EWidgetType, WidgetCreator> widgetCreators_;
    std::unordered_map<std::string, WidgetCreator> typeCreators_;
};

// ============================================================================
// SCRIPT FIELD EXTRACTOR
// ============================================================================

/**
 * Extracts serialized fields from C# scripts via reflection
 */
class ScriptFieldExtractor {
public:
    ScriptFieldExtractor(ScriptingSystem& scriptingSystem);
    
    /**
     * Extract all serializable fields from a script class
     */
    FScriptComponentInfo extractComponentInfo(const std::string& className);
    
    /**
     * Get current field values from a script instance
     */
    void readFieldValues(ScriptInstance* instance, std::vector<FSerializedField>& fields);
    
    /**
     * Set field values on a script instance
     */
    void writeFieldValues(ScriptInstance* instance, const std::vector<FSerializedField>& fields);
    
    /**
     * Check if a field has [SerializeField] attribute
     */
    bool isSerializable(const std::string& className, const std::string& fieldName);
    
    /**
     * Get all serializable script classes
     */
    std::vector<std::string> getSerializableClasses();
    
private:
    ScriptingSystem& scriptingSystem_;
    
    // Cached metadata
    std::unordered_map<std::string, FScriptComponentInfo> cachedInfo_;
    
    // Managed method handles for reflection
    void* getFieldsMethod_ = nullptr;
    void* getAttributeMethod_ = nullptr;
    void* getValueMethod_ = nullptr;
    void* setValueMethod_ = nullptr;
    
    bool initializeReflectionMethods();
    EWidgetType determineWidgetType(const std::string& typeName, const FSerializedField& field);
};

// ============================================================================
// INSPECTOR PANEL
// ============================================================================

/**
 * Main inspector panel for editing entity components
 */
class InspectorPanel {
public:
    InspectorPanel(World& world, ScriptingSystem& scriptingSystem);
    ~InspectorPanel();
    
    /**
     * Set the entity to inspect
     */
    void setTarget(Entity entity);
    void clearTarget();
    
    /**
     * Render the inspector UI
     */
    void render();
    
    /**
     * Apply pending changes
     */
    void applyChanges();
    
    /**
     * Configuration
     */
    void setDebugMode(bool debug) { debugMode_ = debug; }
    bool isDebugMode() const { return debugMode_; }
    
    void setReadOnly(bool readOnly) { readOnly_ = readOnly; }
    bool isReadOnly() const { return readOnly_; }
    
    // Events
    std::function<void(Entity, const std::string&, const FSerializedField::ValueType&)> onFieldChanged;
    std::function<void(Entity, const std::string&, const std::string&)> onMethodCalled;
    
private:
    World& world_;
    ScriptingSystem& scriptingSystem_;
    ScriptFieldExtractor fieldExtractor_;
    
    Entity targetEntity_;
    bool hasTarget_ = false;
    bool debugMode_ = false;
    bool readOnly_ = false;
    
    // Cached component info
    struct ComponentPanel {
        FScriptComponentInfo info;
        std::vector<std::unique_ptr<IInspectorWidget>> widgets;
        bool expanded = true;
    };
    std::vector<ComponentPanel> componentPanels_;
    
    void refreshComponents();
    void renderComponent(ComponentPanel& panel);
    void renderField(FSerializedField& field, IInspectorWidget* widget);
    void renderHeader(const std::string& name, bool& expanded);
};

// ============================================================================
// BUILT-IN WIDGETS
// ============================================================================

class BoolWidget : public IInspectorWidget {
public:
    void render() override;
    
private:
    bool tempValue_ = false;
};

class IntWidget : public IInspectorWidget {
public:
    void render() override;
    
    void setRange(int min, int max) { min_ = min; max_ = max; hasRange_ = true; }
    
private:
    int tempValue_ = 0;
    int min_ = 0, max_ = 100;
    bool hasRange_ = false;
};

class FloatWidget : public IInspectorWidget {
public:
    void render() override;
    
    void setRange(float min, float max) { min_ = min; max_ = max; hasRange_ = true; }
    void setDragSpeed(float speed) { dragSpeed_ = speed; }
    
private:
    float tempValue_ = 0.0f;
    float min_ = 0.0f, max_ = 1.0f;
    float dragSpeed_ = 0.01f;
    bool hasRange_ = false;
};

class StringWidget : public IInspectorWidget {
public:
    void render() override;
    
    void setMultiLine(bool multiLine) { multiLine_ = multiLine; }
    void setMaxLength(size_t maxLen) { maxLength_ = maxLen; }
    
private:
    std::string tempValue_;
    bool multiLine_ = false;
    size_t maxLength_ = 256;
};

class Vector2Widget : public IInspectorWidget {
public:
    void render() override;
    
private:
    glm::vec2 tempValue_{0.0f};
};

class Vector3Widget : public IInspectorWidget {
public:
    void render() override;
    
    void setColorMode(bool isColor) { colorMode_ = isColor; }
    
private:
    glm::vec3 tempValue_{0.0f};
    bool colorMode_ = false;
};

class Vector4Widget : public IInspectorWidget {
public:
    void render() override;
    
    void setColorMode(bool isColor) { colorMode_ = isColor; }
    
private:
    glm::vec4 tempValue_{0.0f};
    bool colorMode_ = false;
};

class QuaternionWidget : public IInspectorWidget {
public:
    void render() override;
    
private:
    glm::vec3 eulerAngles_{0.0f};
};

class EnumWidget : public IInspectorWidget {
public:
    void render() override;
    
    void setOptions(const std::vector<std::string>& options) { options_ = options; }
    
private:
    int selectedIndex_ = 0;
    std::vector<std::string> options_;
};

class ObjectReferenceWidget : public IInspectorWidget {
public:
    void render() override;
    
    void setExpectedType(const std::string& type) { expectedType_ = type; }
    void setWorld(World* world) { world_ = world; }
    
private:
    uint64_t targetId_ = 0;
    std::string expectedType_;
    World* world_ = nullptr;
};

class AssetReferenceWidget : public IInspectorWidget {
public:
    void render() override;
    
    void setAssetType(const std::string& type) { assetType_ = type; }
    void setAssetPath(const std::string& path) { assetPath_ = path; }
    
private:
    std::string assetPath_;
    std::string assetType_;
};

class ArrayWidget : public IInspectorWidget {
public:
    void render() override;
    
    void setElementType(const std::string& type) { elementType_ = type; }
    
private:
    std::string elementType_;
    std::vector<std::unique_ptr<IInspectorWidget>> elementWidgets_;
    bool expanded_ = true;
};

class ButtonWidget : public IInspectorWidget {
public:
    void render() override;
    
    void setLabel(const std::string& label) { label_ = label; }
    void setCallback(std::function<void()> callback) { callback_ = callback; }
    
private:
    std::string label_;
    std::function<void()> callback_;
};

class SliderWidget : public IInspectorWidget {
public:
    void render() override;
    
    void setRange(float min, float max) { min_ = min; max_ = max; }
    void setInteger(bool isInt) { isInteger_ = isInt; }
    
private:
    float value_ = 0.0f;
    float min_ = 0.0f;
    float max_ = 1.0f;
    bool isInteger_ = false;
};

class ColorPickerWidget : public IInspectorWidget {
public:
    void render() override;
    
    void setHDR(bool hdr) { hdrMode_ = hdr; }
    void setAlpha(bool alpha) { hasAlpha_ = alpha; }
    
private:
    glm::vec4 color_{1.0f};
    bool hdrMode_ = false;
    bool hasAlpha_ = true;
};

class CurveWidget : public IInspectorWidget {
public:
    void render() override;
    
    struct CurvePoint {
        float time;
        float value;
        float inTangent;
        float outTangent;
    };
    
    void setCurve(const std::vector<CurvePoint>& points) { points_ = points; }
    const std::vector<CurvePoint>& getCurve() const { return points_; }
    
private:
    std::vector<CurvePoint> points_;
    int selectedPoint_ = -1;
};

class GradientWidget : public IInspectorWidget {
public:
    void render() override;
    
    struct GradientKey {
        float time;
        glm::vec4 color;
    };
    
    void setGradient(const std::vector<GradientKey>& keys) { keys_ = keys; }
    const std::vector<GradientKey>& getGradient() const { return keys_; }
    
private:
    std::vector<GradientKey> keys_;
    int selectedKey_ = -1;
};

// ============================================================================
// VISUAL DEBUGGING
// ============================================================================

/**
 * Visual debugging support for scripts
 */
class ScriptDebugger {
public:
    ScriptDebugger(World& world);
    
    /**
     * Draw debug visualization for a script instance
     */
    void drawDebug(ScriptInstance* instance);
    
    /**
     * Register debug draw callback from script
     */
    void registerDebugDraw(const std::string& scriptName, 
                           std::function<void(Entity)> drawCallback);
    
    /**
     * Built-in debug visualizations
     */
    void drawGizmo(Entity entity, const glm::vec3& position, float size = 1.0f);
    void drawLine(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color);
    void drawWireSphere(const glm::vec3& center, float radius, const glm::vec3& color);
    void drawWireBox(const glm::vec3& center, const glm::vec3& extents, const glm::vec3& color);
    void drawWireCapsule(const glm::vec3& start, const glm::vec3& end, float radius, const glm::vec3& color);
    void drawArrow(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color);
    void drawText(const glm::vec3& position, const std::string& text, const glm::vec3& color);
    
    /**
     * Raycast visualization
     */
    void drawRay(const glm::vec3& origin, const glm::vec3& direction, float length, 
                 const glm::vec3& color, bool hit = false);
    
    /**
     * Path visualization
     */
    void drawPath(const std::vector<glm::vec3>& points, const glm::vec3& color, bool closed = false);
    
    /**
     * Render all queued debug draws
     */
    void renderDebugDraws();
    void clearDebugDraws();
    
    // Settings
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
    void setLineWidth(float width) { lineWidth_ = width; }
    void setDepthTest(bool test) { depthTest_ = test; }
    
private:
    World& world_;
    bool enabled_ = true;
    float lineWidth_ = 1.0f;
    bool depthTest_ = true;
    
    struct DebugLine {
        glm::vec3 start, end, color;
    };
    struct DebugText {
        glm::vec3 position;
        std::string text;
        glm::vec3 color;
    };
    
    std::vector<DebugLine> lines_;
    std::vector<DebugText> texts_;
    
    std::unordered_map<std::string, std::function<void(Entity)>> debugDrawCallbacks_;
};

// ============================================================================
// PROPERTY DRAWER REGISTRATION
// ============================================================================

/**
 * Custom property drawer for specific types
 */
class IPropertyDrawer {
public:
    virtual ~IPropertyDrawer() = default;
    
    virtual const char* getTypeName() const = 0;
    virtual void drawProperty(FSerializedField& field) = 0;
    virtual float getPropertyHeight(const FSerializedField& field) { return 20.0f; }
};

class PropertyDrawerRegistry {
public:
    static PropertyDrawerRegistry& get();
    
    void registerDrawer(std::unique_ptr<IPropertyDrawer> drawer);
    IPropertyDrawer* findDrawer(const std::string& typeName);
    
private:
    PropertyDrawerRegistry() = default;
    std::unordered_map<std::string, std::unique_ptr<IPropertyDrawer>> drawers_;
};

/**
 * Macro for easy property drawer registration
 */
#define REGISTER_PROPERTY_DRAWER(TypeName, DrawerClass) \
    static struct DrawerClass##Registrar { \
        DrawerClass##Registrar() { \
            PropertyDrawerRegistry::get().registerDrawer( \
                std::make_unique<DrawerClass>() \
            ); \
        } \
    } s_##DrawerClass##Registrar

// ============================================================================
// UNDO/REDO SUPPORT
// ============================================================================

/**
 * Undo command for inspector field changes
 */
class InspectorUndoCommand {
public:
    InspectorUndoCommand(Entity entity, const std::string& fieldPath,
                         FSerializedField::ValueType oldValue,
                         FSerializedField::ValueType newValue);
    
    void execute();
    void undo();
    
    const std::string& getDescription() const { return description_; }
    
private:
    Entity entity_;
    std::string fieldPath_;
    FSerializedField::ValueType oldValue_;
    FSerializedField::ValueType newValue_;
    std::string description_;
};

class InspectorUndoStack {
public:
    void push(std::unique_ptr<InspectorUndoCommand> command);
    void undo();
    void redo();
    
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    
    void clear();
    
    // Group multiple changes
    void beginGroup(const std::string& name);
    void endGroup();
    
private:
    std::vector<std::unique_ptr<InspectorUndoCommand>> undoStack_;
    std::vector<std::unique_ptr<InspectorUndoCommand>> redoStack_;
    
    bool inGroup_ = false;
    std::vector<std::unique_ptr<InspectorUndoCommand>> groupCommands_;
    std::string groupName_;
};

} // namespace Sanic
