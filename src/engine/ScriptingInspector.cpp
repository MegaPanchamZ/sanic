/**
 * ScriptingInspector.cpp
 * 
 * Implementation of script inspector and visual debugging.
 */

#include "ScriptingInspector.h"
#include "ECS.h"
#include <sstream>
#include <algorithm>
#include <cmath>

namespace Sanic {

// ============================================================================
// WIDGET FACTORY
// ============================================================================

InspectorWidgetFactory& InspectorWidgetFactory::get() {
    static InspectorWidgetFactory instance;
    return instance;
}

void InspectorWidgetFactory::registerWidget(EWidgetType type, WidgetCreator creator) {
    widgetCreators_[type] = std::move(creator);
}

void InspectorWidgetFactory::registerWidget(const std::string& typeName, WidgetCreator creator) {
    typeCreators_[typeName] = std::move(creator);
}

std::unique_ptr<IInspectorWidget> InspectorWidgetFactory::createWidget(const FSerializedField& field) {
    // First check for type-specific widget
    auto typeIt = typeCreators_.find(field.typeName);
    if (typeIt != typeCreators_.end()) {
        return typeIt->second();
    }
    
    // Then check for widget type override
    if (field.widgetType != EWidgetType::Auto) {
        auto widgetIt = widgetCreators_.find(field.widgetType);
        if (widgetIt != widgetCreators_.end()) {
            return widgetIt->second();
        }
    }
    
    // Default widget based on type name
    if (field.typeName == "System.Boolean" || field.typeName == "bool") {
        return std::make_unique<BoolWidget>();
    }
    else if (field.typeName == "System.Int32" || field.typeName == "int") {
        auto widget = std::make_unique<IntWidget>();
        if (field.hasRange) {
            widget->setRange((int)field.rangeMin, (int)field.rangeMax);
        }
        return widget;
    }
    else if (field.typeName == "System.Single" || field.typeName == "float") {
        auto widget = std::make_unique<FloatWidget>();
        if (field.hasRange) {
            widget->setRange(field.rangeMin, field.rangeMax);
        }
        return widget;
    }
    else if (field.typeName == "System.Double" || field.typeName == "double") {
        return std::make_unique<FloatWidget>();
    }
    else if (field.typeName == "System.String" || field.typeName == "string") {
        return std::make_unique<StringWidget>();
    }
    else if (field.typeName == "UnityEngine.Vector2" || field.typeName == "Vector2") {
        return std::make_unique<Vector2Widget>();
    }
    else if (field.typeName == "UnityEngine.Vector3" || field.typeName == "Vector3") {
        return std::make_unique<Vector3Widget>();
    }
    else if (field.typeName == "UnityEngine.Vector4" || field.typeName == "Vector4") {
        return std::make_unique<Vector4Widget>();
    }
    else if (field.typeName == "UnityEngine.Color" || field.typeName == "Color") {
        auto widget = std::make_unique<ColorPickerWidget>();
        return widget;
    }
    else if (field.typeName == "UnityEngine.Quaternion" || field.typeName == "Quaternion") {
        return std::make_unique<QuaternionWidget>();
    }
    else if (field.isArray) {
        auto widget = std::make_unique<ArrayWidget>();
        // Extract element type from array type name
        size_t bracketPos = field.typeName.find('[');
        if (bracketPos != std::string::npos) {
            widget->setElementType(field.typeName.substr(0, bracketPos));
        }
        return widget;
    }
    else if (field.isReference) {
        return std::make_unique<ObjectReferenceWidget>();
    }
    
    // Fallback to string widget
    return std::make_unique<StringWidget>();
}

// ============================================================================
// SCRIPT FIELD EXTRACTOR
// ============================================================================

ScriptFieldExtractor::ScriptFieldExtractor(ScriptingSystem& scriptingSystem)
    : scriptingSystem_(scriptingSystem) {
    initializeReflectionMethods();
}

bool ScriptFieldExtractor::initializeReflectionMethods() {
    // TODO: Get method handles from C# reflection helper
    // These would call into C# code that uses System.Reflection to examine types
    return true;
}

FScriptComponentInfo ScriptFieldExtractor::extractComponentInfo(const std::string& className) {
    // Check cache
    auto it = cachedInfo_.find(className);
    if (it != cachedInfo_.end()) {
        return it->second;
    }
    
    FScriptComponentInfo info;
    info.className = className;
    info.displayName = className;
    
    // Extract namespace-free name
    size_t dotPos = className.rfind('.');
    if (dotPos != std::string::npos) {
        info.displayName = className.substr(dotPos + 1);
    }
    
    // TODO: Call into C# to get field info via reflection
    // This would invoke managed code that does:
    // 
    // Type type = Type.GetType(className);
    // foreach (var field in type.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance)) {
    //     if (field.GetCustomAttribute<SerializeFieldAttribute>() != null || field.IsPublic) {
    //         // Extract field info
    //     }
    // }
    
    // For now, add some placeholder fields based on common patterns
    if (className.find("Player") != std::string::npos) {
        FSerializedField healthField;
        healthField.name = "health";
        healthField.displayName = "Health";
        healthField.typeName = "float";
        healthField.hasRange = true;
        healthField.rangeMin = 0.0f;
        healthField.rangeMax = 100.0f;
        healthField.value = 100.0f;
        info.fields.push_back(healthField);
        
        FSerializedField speedField;
        speedField.name = "moveSpeed";
        speedField.displayName = "Move Speed";
        speedField.typeName = "float";
        speedField.hasRange = true;
        speedField.rangeMin = 0.0f;
        speedField.rangeMax = 20.0f;
        speedField.value = 5.0f;
        info.fields.push_back(speedField);
    }
    
    cachedInfo_[className] = info;
    return info;
}

void ScriptFieldExtractor::readFieldValues(ScriptInstance* instance, 
                                            std::vector<FSerializedField>& fields) {
    if (!instance) return;
    
    // TODO: Call into C# to get field values
    // This would use reflection to read current field values from the managed object
    //
    // foreach (var field in fields) {
    //     field.value = fieldInfo.GetValue(instance.managedObject);
    // }
}

void ScriptFieldExtractor::writeFieldValues(ScriptInstance* instance,
                                             const std::vector<FSerializedField>& fields) {
    if (!instance) return;
    
    // TODO: Call into C# to set field values
    // foreach (var field in fields) {
    //     fieldInfo.SetValue(instance.managedObject, field.value);
    // }
}

bool ScriptFieldExtractor::isSerializable(const std::string& className, const std::string& fieldName) {
    auto info = extractComponentInfo(className);
    for (const auto& field : info.fields) {
        if (field.name == fieldName) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> ScriptFieldExtractor::getSerializableClasses() {
    // TODO: Get list from ScriptingSystem's loaded assemblies
    return { "PlayerController", "EnemyAI", "Interactable", "HealthPickup" };
}

EWidgetType ScriptFieldExtractor::determineWidgetType(const std::string& typeName,
                                                       const FSerializedField& field) {
    if (field.widgetType != EWidgetType::Auto) {
        return field.widgetType;
    }
    
    if (typeName == "Color" || typeName.find("Color") != std::string::npos) {
        return EWidgetType::ColorPicker;
    }
    if (typeName.find("Curve") != std::string::npos) {
        return EWidgetType::Curve;
    }
    if (typeName.find("Gradient") != std::string::npos) {
        return EWidgetType::Gradient;
    }
    if (field.hasRange) {
        return EWidgetType::Slider;
    }
    
    return EWidgetType::Auto;
}

// ============================================================================
// INSPECTOR PANEL
// ============================================================================

InspectorPanel::InspectorPanel(World& world, ScriptingSystem& scriptingSystem)
    : world_(world)
    , scriptingSystem_(scriptingSystem)
    , fieldExtractor_(scriptingSystem) {
}

InspectorPanel::~InspectorPanel() = default;

void InspectorPanel::setTarget(Entity entity) {
    targetEntity_ = entity;
    hasTarget_ = true;
    refreshComponents();
}

void InspectorPanel::clearTarget() {
    hasTarget_ = false;
    componentPanels_.clear();
}

void InspectorPanel::refreshComponents() {
    componentPanels_.clear();
    
    if (!hasTarget_) return;
    
    // Get script components on this entity
    // TODO: Query ECS for Script components
    // For now, simulate finding scripts
    
    std::vector<std::string> scripts = { "PlayerController" };  // Placeholder
    
    for (const auto& scriptName : scripts) {
        ComponentPanel panel;
        panel.info = fieldExtractor_.extractComponentInfo(scriptName);
        
        // Create widgets for each field
        for (auto& field : panel.info.fields) {
            auto widget = InspectorWidgetFactory::get().createWidget(field);
            widget->bindField(&field);
            panel.widgets.push_back(std::move(widget));
        }
        
        componentPanels_.push_back(std::move(panel));
    }
}

void InspectorPanel::render() {
    if (!hasTarget_) {
        // Render "No selection" message
        return;
    }
    
    // Render entity name/ID header
    // ImGui::Text("Entity: %u", targetEntity_);
    
    // Render each component
    for (auto& panel : componentPanels_) {
        renderComponent(panel);
    }
}

void InspectorPanel::renderComponent(ComponentPanel& panel) {
    // Component header
    renderHeader(panel.info.displayName, panel.expanded);
    
    if (!panel.expanded) return;
    
    // Render each field
    for (size_t i = 0; i < panel.info.fields.size(); ++i) {
        auto& field = panel.info.fields[i];
        IInspectorWidget* widget = panel.widgets[i].get();
        
        if (debugMode_ || field.visibility != EFieldVisibility::Debug) {
            renderField(field, widget);
        }
    }
    
    // Render method buttons
    for (const auto& methodName : panel.info.methodButtons) {
        // if (ImGui::Button(methodName.c_str())) {
        //     if (onMethodCalled) {
        //         onMethodCalled(targetEntity_, panel.info.className, methodName);
        //     }
        // }
    }
}

void InspectorPanel::renderField(FSerializedField& field, IInspectorWidget* widget) {
    if (field.visibility == EFieldVisibility::Hidden) return;
    
    bool enabled = !readOnly_ && field.visibility == EFieldVisibility::Editable;
    
    // Draw label with tooltip
    // ImGui::Text("%s", field.displayName.c_str());
    // if (!field.tooltip.empty() && ImGui::IsItemHovered()) {
    //     ImGui::SetTooltip("%s", field.tooltip.c_str());
    // }
    
    // Draw widget
    // ImGui::BeginDisabled(!enabled);
    widget->render();
    // ImGui::EndDisabled();
    
    // Handle value changes
    // if (valueChanged && onFieldChanged) {
    //     onFieldChanged(targetEntity_, field.name, field.value);
    // }
}

void InspectorPanel::renderHeader(const std::string& name, bool& expanded) {
    // ImGui::SetNextItemOpen(expanded, ImGuiCond_Once);
    // expanded = ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
}

void InspectorPanel::applyChanges() {
    // Write all modified fields back to script instances
    for (auto& panel : componentPanels_) {
        // Get script instance for this entity
        // ScriptInstance* instance = scriptingSystem_.getInstance(targetEntity_, panel.info.className);
        // fieldExtractor_.writeFieldValues(instance, panel.info.fields);
    }
}

// ============================================================================
// BUILT-IN WIDGETS
// ============================================================================

void BoolWidget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<bool>(&boundField_->value)) {
        tempValue_ = *val;
    }
    
    // if (ImGui::Checkbox("##bool", &tempValue_)) {
    //     boundField_->value = tempValue_;
    //     if (boundField_->onValueChanged) {
    //         boundField_->onValueChanged(tempValue_);
    //     }
    // }
}

void IntWidget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<int32_t>(&boundField_->value)) {
        tempValue_ = *val;
    }
    
    if (hasRange_) {
        // ImGui::SliderInt("##int", &tempValue_, min_, max_);
    } else {
        // ImGui::DragInt("##int", &tempValue_);
    }
    
    // if (valueChanged) {
    //     boundField_->value = tempValue_;
    // }
}

void FloatWidget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<float>(&boundField_->value)) {
        tempValue_ = *val;
    }
    
    if (hasRange_) {
        // ImGui::SliderFloat("##float", &tempValue_, min_, max_);
    } else {
        // ImGui::DragFloat("##float", &tempValue_, dragSpeed_);
    }
    
    // if (valueChanged) {
    //     boundField_->value = tempValue_;
    // }
}

void StringWidget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<std::string>(&boundField_->value)) {
        tempValue_ = *val;
    }
    
    tempValue_.resize(maxLength_);
    
    if (multiLine_) {
        // ImGui::InputTextMultiline("##string", tempValue_.data(), maxLength_);
    } else {
        // ImGui::InputText("##string", tempValue_.data(), maxLength_);
    }
    
    // if (valueChanged) {
    //     boundField_->value = std::string(tempValue_.c_str());
    // }
}

void Vector2Widget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<glm::vec2>(&boundField_->value)) {
        tempValue_ = *val;
    }
    
    // ImGui::DragFloat2("##vec2", &tempValue_.x, 0.1f);
    
    // if (valueChanged) {
    //     boundField_->value = tempValue_;
    // }
}

void Vector3Widget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<glm::vec3>(&boundField_->value)) {
        tempValue_ = *val;
    }
    
    if (colorMode_) {
        // ImGui::ColorEdit3("##color3", &tempValue_.x);
    } else {
        // ImGui::DragFloat3("##vec3", &tempValue_.x, 0.1f);
    }
    
    // if (valueChanged) {
    //     boundField_->value = tempValue_;
    // }
}

void Vector4Widget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<glm::vec4>(&boundField_->value)) {
        tempValue_ = *val;
    }
    
    if (colorMode_) {
        // ImGui::ColorEdit4("##color4", &tempValue_.x);
    } else {
        // ImGui::DragFloat4("##vec4", &tempValue_.x, 0.1f);
    }
    
    // if (valueChanged) {
    //     boundField_->value = tempValue_;
    // }
}

void QuaternionWidget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<glm::quat>(&boundField_->value)) {
        eulerAngles_ = glm::degrees(glm::eulerAngles(*val));
    }
    
    // ImGui::DragFloat3("Rotation", &eulerAngles_.x, 1.0f);
    
    // if (valueChanged) {
    //     glm::quat q = glm::quat(glm::radians(eulerAngles_));
    //     boundField_->value = q;
    // }
}

void EnumWidget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<int32_t>(&boundField_->value)) {
        selectedIndex_ = *val;
    }
    
    // Build combo preview
    const char* preview = selectedIndex_ >= 0 && selectedIndex_ < (int)options_.size() 
                         ? options_[selectedIndex_].c_str() : "";
    
    // if (ImGui::BeginCombo("##enum", preview)) {
    //     for (int i = 0; i < options_.size(); ++i) {
    //         bool selected = (i == selectedIndex_);
    //         if (ImGui::Selectable(options_[i].c_str(), selected)) {
    //             selectedIndex_ = i;
    //             boundField_->value = selectedIndex_;
    //         }
    //     }
    //     ImGui::EndCombo();
    // }
}

void ObjectReferenceWidget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<uint64_t>(&boundField_->value)) {
        targetId_ = *val;
    }
    
    // Display object name or "None"
    std::string displayText = targetId_ == 0 ? "None" : "Entity " + std::to_string(targetId_);
    
    // ImGui::InputText("##objref", displayText.data(), displayText.size(), ImGuiInputTextFlags_ReadOnly);
    
    // Drag-drop target
    // if (ImGui::BeginDragDropTarget()) {
    //     if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY")) {
    //         targetId_ = *(uint64_t*)payload->Data;
    //         boundField_->value = targetId_;
    //     }
    //     ImGui::EndDragDropTarget();
    // }
    
    // Object picker button
    // ImGui::SameLine();
    // if (ImGui::Button("...")) {
    //     // Open object picker dialog
    // }
}

void AssetReferenceWidget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<std::string>(&boundField_->value)) {
        assetPath_ = *val;
    }
    
    // Display asset path
    // ImGui::InputText("##assetref", assetPath_.data(), assetPath_.size(), ImGuiInputTextFlags_ReadOnly);
    
    // Drag-drop target for assets
    // if (ImGui::BeginDragDropTarget()) {
    //     if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
    //         assetPath_ = std::string((const char*)payload->Data);
    //         boundField_->value = assetPath_;
    //     }
    //     ImGui::EndDragDropTarget();
    // }
    
    // Asset browser button
    // ImGui::SameLine();
    // if (ImGui::Button("...")) {
    //     // Open asset browser
    // }
}

void ArrayWidget::render() {
    if (!boundField_) return;
    
    // Array header with size
    size_t arraySize = elementWidgets_.size();
    
    // if (ImGui::TreeNode(("##array_" + boundField_->name).c_str(), "%s [%zu]", 
    //                     boundField_->displayName.c_str(), arraySize)) {
    //     
    //     // Size control
    //     int newSize = (int)arraySize;
    //     if (ImGui::InputInt("Size", &newSize)) {
    //         if (newSize >= 0) {
    //             elementWidgets_.resize(newSize);
    //             // Create new widgets for added elements
    //         }
    //     }
    //     
    //     // Render each element
    //     for (size_t i = 0; i < elementWidgets_.size(); ++i) {
    //         ImGui::PushID((int)i);
    //         ImGui::Text("[%zu]", i);
    //         ImGui::SameLine();
    //         elementWidgets_[i]->render();
    //         ImGui::PopID();
    //     }
    //     
    //     ImGui::TreePop();
    // }
}

void ButtonWidget::render() {
    // if (ImGui::Button(label_.c_str())) {
    //     if (callback_) {
    //         callback_();
    //     }
    // }
}

void SliderWidget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<float>(&boundField_->value)) {
        value_ = *val;
    }
    
    if (isInteger_) {
        int intValue = (int)value_;
        // if (ImGui::SliderInt("##slider", &intValue, (int)min_, (int)max_)) {
        //     boundField_->value = (float)intValue;
        // }
    } else {
        // if (ImGui::SliderFloat("##slider", &value_, min_, max_)) {
        //     boundField_->value = value_;
        // }
    }
}

void ColorPickerWidget::render() {
    if (!boundField_) return;
    
    if (auto* val = std::get_if<glm::vec4>(&boundField_->value)) {
        color_ = *val;
    } else if (auto* val3 = std::get_if<glm::vec3>(&boundField_->value)) {
        color_ = glm::vec4(*val3, 1.0f);
    }
    
    int flags = 0;
    if (hdrMode_) {
        // flags |= ImGuiColorEditFlags_HDR;
    }
    if (!hasAlpha_) {
        // flags |= ImGuiColorEditFlags_NoAlpha;
    }
    
    // if (ImGui::ColorEdit4("##color", &color_.x, flags)) {
    //     if (hasAlpha_) {
    //         boundField_->value = color_;
    //     } else {
    //         boundField_->value = glm::vec3(color_);
    //     }
    // }
}

void CurveWidget::render() {
    // Custom curve editor
    // Would render a curve with draggable control points
    
    // ImVec2 canvasSize(200, 100);
    // ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    // ImDrawList* drawList = ImGui::GetWindowDrawList();
    // 
    // // Draw background
    // drawList->AddRectFilled(canvasPos, 
    //     ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
    //     IM_COL32(30, 30, 30, 255));
    // 
    // // Draw curve
    // if (points_.size() >= 2) {
    //     for (size_t i = 0; i < points_.size() - 1; ++i) {
    //         // Draw bezier curve between points
    //     }
    // }
    // 
    // // Draw points
    // for (size_t i = 0; i < points_.size(); ++i) {
    //     // Draw draggable point
    // }
}

void GradientWidget::render() {
    // Custom gradient editor
    // Would render a gradient bar with color stops
    
    // ImVec2 barSize(200, 20);
    // ImVec2 barPos = ImGui::GetCursorScreenPos();
    // ImDrawList* drawList = ImGui::GetWindowDrawList();
    // 
    // // Draw gradient
    // for (size_t i = 0; i < keys_.size() - 1; ++i) {
    //     // Draw gradient segment
    // }
    // 
    // // Draw color stop markers
    // for (size_t i = 0; i < keys_.size(); ++i) {
    //     // Draw draggable marker
    // }
}

// ============================================================================
// SCRIPT DEBUGGER
// ============================================================================

ScriptDebugger::ScriptDebugger(World& world) : world_(world) {
}

void ScriptDebugger::drawDebug(ScriptInstance* instance) {
    if (!enabled_ || !instance) return;
    
    // Check for registered debug callback
    // auto it = debugDrawCallbacks_.find(instance->getClassName());
    // if (it != debugDrawCallbacks_.end()) {
    //     it->second(instance->getEntity());
    // }
}

void ScriptDebugger::registerDebugDraw(const std::string& scriptName,
                                        std::function<void(Entity)> drawCallback) {
    debugDrawCallbacks_[scriptName] = std::move(drawCallback);
}

void ScriptDebugger::drawGizmo(Entity entity, const glm::vec3& position, float size) {
    // Draw 3-axis gizmo
    drawLine(position, position + glm::vec3(size, 0, 0), glm::vec3(1, 0, 0));
    drawLine(position, position + glm::vec3(0, size, 0), glm::vec3(0, 1, 0));
    drawLine(position, position + glm::vec3(0, 0, size), glm::vec3(0, 0, 1));
}

void ScriptDebugger::drawLine(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color) {
    lines_.push_back({ start, end, color });
}

void ScriptDebugger::drawWireSphere(const glm::vec3& center, float radius, const glm::vec3& color) {
    const int segments = 32;
    const float step = 2.0f * 3.14159f / segments;
    
    // XY circle
    for (int i = 0; i < segments; ++i) {
        float a1 = i * step;
        float a2 = (i + 1) * step;
        glm::vec3 p1 = center + glm::vec3(std::cos(a1) * radius, std::sin(a1) * radius, 0);
        glm::vec3 p2 = center + glm::vec3(std::cos(a2) * radius, std::sin(a2) * radius, 0);
        drawLine(p1, p2, color);
    }
    
    // XZ circle
    for (int i = 0; i < segments; ++i) {
        float a1 = i * step;
        float a2 = (i + 1) * step;
        glm::vec3 p1 = center + glm::vec3(std::cos(a1) * radius, 0, std::sin(a1) * radius);
        glm::vec3 p2 = center + glm::vec3(std::cos(a2) * radius, 0, std::sin(a2) * radius);
        drawLine(p1, p2, color);
    }
    
    // YZ circle
    for (int i = 0; i < segments; ++i) {
        float a1 = i * step;
        float a2 = (i + 1) * step;
        glm::vec3 p1 = center + glm::vec3(0, std::cos(a1) * radius, std::sin(a1) * radius);
        glm::vec3 p2 = center + glm::vec3(0, std::cos(a2) * radius, std::sin(a2) * radius);
        drawLine(p1, p2, color);
    }
}

void ScriptDebugger::drawWireBox(const glm::vec3& center, const glm::vec3& extents, const glm::vec3& color) {
    glm::vec3 min = center - extents;
    glm::vec3 max = center + extents;
    
    // Bottom face
    drawLine(glm::vec3(min.x, min.y, min.z), glm::vec3(max.x, min.y, min.z), color);
    drawLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, min.y, max.z), color);
    drawLine(glm::vec3(max.x, min.y, max.z), glm::vec3(min.x, min.y, max.z), color);
    drawLine(glm::vec3(min.x, min.y, max.z), glm::vec3(min.x, min.y, min.z), color);
    
    // Top face
    drawLine(glm::vec3(min.x, max.y, min.z), glm::vec3(max.x, max.y, min.z), color);
    drawLine(glm::vec3(max.x, max.y, min.z), glm::vec3(max.x, max.y, max.z), color);
    drawLine(glm::vec3(max.x, max.y, max.z), glm::vec3(min.x, max.y, max.z), color);
    drawLine(glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, max.y, min.z), color);
    
    // Vertical edges
    drawLine(glm::vec3(min.x, min.y, min.z), glm::vec3(min.x, max.y, min.z), color);
    drawLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, max.y, min.z), color);
    drawLine(glm::vec3(max.x, min.y, max.z), glm::vec3(max.x, max.y, max.z), color);
    drawLine(glm::vec3(min.x, min.y, max.z), glm::vec3(min.x, max.y, max.z), color);
}

void ScriptDebugger::drawWireCapsule(const glm::vec3& start, const glm::vec3& end, 
                                      float radius, const glm::vec3& color) {
    // Draw cylinder body
    glm::vec3 dir = glm::normalize(end - start);
    glm::vec3 up = glm::abs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(up, dir));
    up = glm::cross(dir, right);
    
    const int segments = 16;
    const float step = 2.0f * 3.14159f / segments;
    
    for (int i = 0; i < segments; ++i) {
        float a = i * step;
        glm::vec3 offset = right * std::cos(a) * radius + up * std::sin(a) * radius;
        drawLine(start + offset, end + offset, color);
    }
    
    // Draw end caps (simplified as circles)
    for (int i = 0; i < segments; ++i) {
        float a1 = i * step;
        float a2 = (i + 1) * step;
        glm::vec3 o1 = right * std::cos(a1) * radius + up * std::sin(a1) * radius;
        glm::vec3 o2 = right * std::cos(a2) * radius + up * std::sin(a2) * radius;
        drawLine(start + o1, start + o2, color);
        drawLine(end + o1, end + o2, color);
    }
}

void ScriptDebugger::drawArrow(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color) {
    drawLine(start, end, color);
    
    glm::vec3 dir = glm::normalize(end - start);
    float length = glm::length(end - start);
    float headLength = length * 0.2f;
    
    glm::vec3 up = glm::abs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(up, dir));
    up = glm::cross(dir, right);
    
    glm::vec3 headBase = end - dir * headLength;
    float headRadius = headLength * 0.3f;
    
    drawLine(end, headBase + right * headRadius, color);
    drawLine(end, headBase - right * headRadius, color);
    drawLine(end, headBase + up * headRadius, color);
    drawLine(end, headBase - up * headRadius, color);
}

void ScriptDebugger::drawText(const glm::vec3& position, const std::string& text, const glm::vec3& color) {
    texts_.push_back({ position, text, color });
}

void ScriptDebugger::drawRay(const glm::vec3& origin, const glm::vec3& direction, 
                              float length, const glm::vec3& color, bool hit) {
    glm::vec3 end = origin + glm::normalize(direction) * length;
    drawLine(origin, end, hit ? glm::vec3(1, 0, 0) : color);
    
    if (hit) {
        drawWireSphere(end, 0.1f, glm::vec3(1, 0, 0));
    }
}

void ScriptDebugger::drawPath(const std::vector<glm::vec3>& points, const glm::vec3& color, bool closed) {
    if (points.size() < 2) return;
    
    for (size_t i = 0; i < points.size() - 1; ++i) {
        drawLine(points[i], points[i + 1], color);
    }
    
    if (closed && points.size() >= 3) {
        drawLine(points.back(), points.front(), color);
    }
}

void ScriptDebugger::renderDebugDraws() {
    if (!enabled_) return;
    
    // TODO: Submit lines to debug renderer
    // This would use a simple line shader to render all debug geometry
    
    // For lines:
    // debugLineRenderer->setLineWidth(lineWidth_);
    // debugLineRenderer->setDepthTest(depthTest_);
    // for (const auto& line : lines_) {
    //     debugLineRenderer->drawLine(line.start, line.end, line.color);
    // }
    
    // For text:
    // for (const auto& text : texts_) {
    //     debugTextRenderer->drawText(text.position, text.text, text.color);
    // }
}

void ScriptDebugger::clearDebugDraws() {
    lines_.clear();
    texts_.clear();
}

// ============================================================================
// PROPERTY DRAWER REGISTRY
// ============================================================================

PropertyDrawerRegistry& PropertyDrawerRegistry::get() {
    static PropertyDrawerRegistry instance;
    return instance;
}

void PropertyDrawerRegistry::registerDrawer(std::unique_ptr<IPropertyDrawer> drawer) {
    std::string typeName = drawer->getTypeName();
    drawers_[typeName] = std::move(drawer);
}

IPropertyDrawer* PropertyDrawerRegistry::findDrawer(const std::string& typeName) {
    auto it = drawers_.find(typeName);
    return it != drawers_.end() ? it->second.get() : nullptr;
}

// ============================================================================
// UNDO/REDO
// ============================================================================

InspectorUndoCommand::InspectorUndoCommand(Entity entity, const std::string& fieldPath,
                                           FSerializedField::ValueType oldValue,
                                           FSerializedField::ValueType newValue)
    : entity_(entity)
    , fieldPath_(fieldPath)
    , oldValue_(oldValue)
    , newValue_(newValue) {
    description_ = "Modify " + fieldPath;
}

void InspectorUndoCommand::execute() {
    // TODO: Set value on entity's script field
}

void InspectorUndoCommand::undo() {
    // TODO: Restore old value
}

void InspectorUndoStack::push(std::unique_ptr<InspectorUndoCommand> command) {
    if (inGroup_) {
        groupCommands_.push_back(std::move(command));
    } else {
        command->execute();
        undoStack_.push_back(std::move(command));
        redoStack_.clear();
    }
}

void InspectorUndoStack::undo() {
    if (undoStack_.empty()) return;
    
    auto command = std::move(undoStack_.back());
    undoStack_.pop_back();
    
    command->undo();
    redoStack_.push_back(std::move(command));
}

void InspectorUndoStack::redo() {
    if (redoStack_.empty()) return;
    
    auto command = std::move(redoStack_.back());
    redoStack_.pop_back();
    
    command->execute();
    undoStack_.push_back(std::move(command));
}

void InspectorUndoStack::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

void InspectorUndoStack::beginGroup(const std::string& name) {
    inGroup_ = true;
    groupName_ = name;
    groupCommands_.clear();
}

void InspectorUndoStack::endGroup() {
    if (!inGroup_) return;
    
    // Execute all group commands
    for (auto& cmd : groupCommands_) {
        cmd->execute();
        undoStack_.push_back(std::move(cmd));
    }
    
    groupCommands_.clear();
    inGroup_ = false;
    redoStack_.clear();
}

} // namespace Sanic
