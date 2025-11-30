/**
 * InspectorPanel.h
 * 
 * Property inspector panel for editing selected entities.
 * 
 * Features:
 * - Component editors
 * - Property widgets
 * - Add/remove components
 * - Multi-entity editing
 */

#pragma once

#include "../EditorWindow.h"
#include "../../engine/ECS.h"
#include <functional>
#include <unordered_map>
#include <typeindex>
#include <memory>

namespace Sanic::Editor {

class UndoSystem;

// Component editor interface
class IComponentEditor {
public:
    virtual ~IComponentEditor() = default;
    virtual void draw(Entity entity, Sanic::World& world, UndoSystem& undo) = 0;
    virtual const char* getComponentName() const = 0;
    virtual const char* getIcon() const { return nullptr; }
    virtual bool canRemove() const { return true; }
};

class InspectorPanel : public EditorWindow {
public:
    void initialize(Editor* editor) override;
    void update(float deltaTime) override;
    void draw() override;
    const char* getName() const override { return "Inspector"; }
    
    // Register custom component editors
    template<typename T>
    void registerEditor(std::unique_ptr<IComponentEditor> editor) {
        ComponentTypeId id = ComponentRegistry::getInstance().getTypeId<T>();
        editors_[id] = std::move(editor);
    }
    
private:
    void drawEntityInspector(Entity entity);
    void drawMultiEntityInspector();
    void drawAddComponentButton(Entity entity);
    void drawTransformComponent(Entity entity, Transform& transform);
    void drawNameComponent(Entity entity, Name& name);
    void drawMeshRendererComponent(Entity entity, MeshRenderer& renderer);
    void drawLightComponent(Entity entity, Light& light);
    void drawCameraComponent(Entity entity, Camera& camera);
    
    bool drawVector3(const char* label, glm::vec3& value, float resetValue = 0.0f);
    bool drawFloat(const char* label, float& value, float speed = 0.1f, float min = 0.0f, float max = 0.0f);
    bool drawColor3(const char* label, glm::vec3& color);
    bool drawColor4(const char* label, glm::vec4& color);
    
    Editor* editor_ = nullptr;
    std::unordered_map<ComponentTypeId, std::unique_ptr<IComponentEditor>> editors_;
    
    // Add component popup state
    bool showAddComponentPopup_ = false;
    char addComponentFilter_[128] = "";
    
    // Cached transform for undo
    Transform cachedTransform_;
    bool transformEditing_ = false;
};

} // namespace Sanic::Editor
