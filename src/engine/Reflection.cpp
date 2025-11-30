/**
 * Reflection.cpp
 * 
 * Implementation of built-in type registrations and reflection utilities.
 */

#include "Reflection.h"
#include "ECS.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace Sanic {

// ============================================================================
// BUILT-IN COMPONENT REFLECTIONS
// ============================================================================

void RegisterBuiltInReflections() {
    auto& registry = TypeRegistry::getInstance();
    
    // Transform component
    StructBuilder("Transform")
        .TypeInfo<Transform>()
        .DisplayName("Transform")
        .Tooltip("Position, rotation, and scale in 3D space")
        .Category("Core")
        .AddProperty(
            PropertyBuilder("position", EPropertyType::Vec3, 
                            offsetof(Transform, position), sizeof(glm::vec3))
                .DisplayName("Position")
                .Tooltip("World position in units")
                .Category("Transform")
                .Units("m")
                .TypeInfo<glm::vec3>()
        )
        .AddProperty(
            PropertyBuilder("rotation", EPropertyType::Quat,
                            offsetof(Transform, rotation), sizeof(glm::quat))
                .DisplayName("Rotation")
                .Tooltip("World rotation as quaternion")
                .Category("Transform")
                .CustomWidget("QuaternionEditor")
                .TypeInfo<glm::quat>()
        )
        .AddProperty(
            PropertyBuilder("scale", EPropertyType::Vec3,
                            offsetof(Transform, scale), sizeof(glm::vec3))
                .DisplayName("Scale")
                .Tooltip("Local scale multiplier")
                .Category("Transform")
                .ClampMin(0.001)
                .TypeInfo<glm::vec3>()
        )
        .AddProperty(
            PropertyBuilder("parent", EPropertyType::Entity,
                            offsetof(Transform, parent), sizeof(Entity))
                .DisplayName("Parent")
                .Tooltip("Parent entity in hierarchy")
                .Category("Hierarchy")
                .Flags(EPropertyFlags::EditAnywhere | EPropertyFlags::EntityRef)
                .TypeInfo<Entity>()
        )
        .registerStruct();
    
    // Name component
    StructBuilder("Name")
        .TypeInfo<Name>()
        .DisplayName("Name")
        .Category("Core")
        .AddProperty(
            PropertyBuilder("name", EPropertyType::String,
                            offsetof(Name, name), sizeof(std::string))
                .DisplayName("Name")
                .Tooltip("Entity display name")
                .Category("Identity")
                .TypeInfo<std::string>()
        )
        .AddProperty(
            PropertyBuilder("tag", EPropertyType::String,
                            offsetof(Name, tag), sizeof(std::string))
                .DisplayName("Tag")
                .Tooltip("Entity tag for grouping")
                .Category("Identity")
                .TypeInfo<std::string>()
        )
        .registerStruct();
    
    // Active component
    StructBuilder("Active")
        .TypeInfo<Active>()
        .DisplayName("Active State")
        .Category("Core")
        .AddProperty(
            PropertyBuilder("active", EPropertyType::Bool,
                            offsetof(Active, active), sizeof(bool))
                .DisplayName("Active")
                .Tooltip("Is this entity active in the scene?")
                .TypeInfo<bool>()
        )
        .AddProperty(
            PropertyBuilder("visibleInEditor", EPropertyType::Bool,
                            offsetof(Active, visibleInEditor), sizeof(bool))
                .DisplayName("Visible in Editor")
                .Tooltip("Is this entity visible in the editor hierarchy?")
                .TypeInfo<bool>()
        )
        .registerStruct();
    
    // Velocity component
    StructBuilder("Velocity")
        .TypeInfo<Velocity>()
        .DisplayName("Velocity")
        .Category("Physics")
        .AddProperty(
            PropertyBuilder("linear", EPropertyType::Vec3,
                            offsetof(Velocity, linear), sizeof(glm::vec3))
                .DisplayName("Linear Velocity")
                .Tooltip("Linear velocity in units per second")
                .Units("m/s")
                .TypeInfo<glm::vec3>()
        )
        .AddProperty(
            PropertyBuilder("angular", EPropertyType::Vec3,
                            offsetof(Velocity, angular), sizeof(glm::vec3))
                .DisplayName("Angular Velocity")
                .Tooltip("Angular velocity in radians per second")
                .Units("rad/s")
                .TypeInfo<glm::vec3>()
        )
        .registerStruct();
    
    // Health component
    StructBuilder("Health")
        .TypeInfo<Health>()
        .DisplayName("Health")
        .Category("Gameplay")
        .AddProperty(
            PropertyBuilder("current", EPropertyType::Float,
                            offsetof(Health, current), sizeof(float))
                .DisplayName("Current Health")
                .Tooltip("Current health points")
                .ClampMin(0)
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("max", EPropertyType::Float,
                            offsetof(Health, max), sizeof(float))
                .DisplayName("Max Health")
                .Tooltip("Maximum health points")
                .ClampMin(1)
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("invulnerable", EPropertyType::Bool,
                            offsetof(Health, invulnerable), sizeof(bool))
                .DisplayName("Invulnerable")
                .Tooltip("Cannot take damage")
                .TypeInfo<bool>()
        )
        .registerStruct();
    
    // Collider component
    registry.registerEnum("Collider::Type", {
        {"Box", 0},
        {"Sphere", 1},
        {"Capsule", 2},
        {"Mesh", 3}
    });
    
    StructBuilder("Collider")
        .TypeInfo<Collider>()
        .DisplayName("Collider")
        .Category("Physics")
        .AddProperty(
            PropertyBuilder("type", EPropertyType::Enum,
                            offsetof(Collider, type), sizeof(Collider::Type))
                .DisplayName("Collider Type")
                .EnumValues({{"Box", 0}, {"Sphere", 1}, {"Capsule", 2}, {"Mesh", 3}})
        )
        .AddProperty(
            PropertyBuilder("center", EPropertyType::Vec3,
                            offsetof(Collider, center), sizeof(glm::vec3))
                .DisplayName("Center")
                .Tooltip("Offset from entity origin")
                .Units("m")
                .TypeInfo<glm::vec3>()
        )
        .AddProperty(
            PropertyBuilder("size", EPropertyType::Vec3,
                            offsetof(Collider, size), sizeof(glm::vec3))
                .DisplayName("Size")
                .Tooltip("Box dimensions")
                .EditCondition("type == 0")
                .ClampMin(0.001)
                .Units("m")
                .TypeInfo<glm::vec3>()
        )
        .AddProperty(
            PropertyBuilder("radius", EPropertyType::Float,
                            offsetof(Collider, radius), sizeof(float))
                .DisplayName("Radius")
                .Tooltip("Sphere/Capsule radius")
                .EditCondition("type == 1 || type == 2")
                .ClampMin(0.001)
                .Units("m")
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("height", EPropertyType::Float,
                            offsetof(Collider, height), sizeof(float))
                .DisplayName("Height")
                .Tooltip("Capsule height")
                .EditCondition("type == 2")
                .ClampMin(0.001)
                .Units("m")
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("isTrigger", EPropertyType::Bool,
                            offsetof(Collider, isTrigger), sizeof(bool))
                .DisplayName("Is Trigger")
                .Tooltip("Generate trigger events instead of collision")
                .TypeInfo<bool>()
        )
        .AddProperty(
            PropertyBuilder("layer", EPropertyType::UInt32,
                            offsetof(Collider, layer), sizeof(uint32_t))
                .DisplayName("Collision Layer")
                .Tooltip("Layer for collision filtering")
                .ClampMin(0)
                .ClampMax(31)
                .CustomWidget("LayerMask")
                .TypeInfo<uint32_t>()
        )
        .AddProperty(
            PropertyBuilder("mask", EPropertyType::UInt32,
                            offsetof(Collider, mask), sizeof(uint32_t))
                .DisplayName("Collision Mask")
                .Tooltip("Which layers this collides with")
                .CustomWidget("LayerMask")
                .TypeInfo<uint32_t>()
        )
        .registerStruct();
    
    // RigidBody component
    registry.registerEnum("RigidBody::Type", {
        {"Static", 0},
        {"Kinematic", 1},
        {"Dynamic", 2}
    });
    
    StructBuilder("RigidBody")
        .TypeInfo<RigidBody>()
        .DisplayName("Rigidbody")
        .Category("Physics")
        .AddProperty(
            PropertyBuilder("type", EPropertyType::Enum,
                            offsetof(RigidBody, type), sizeof(RigidBody::Type))
                .DisplayName("Body Type")
                .EnumValues({{"Static", 0}, {"Kinematic", 1}, {"Dynamic", 2}})
        )
        .AddProperty(
            PropertyBuilder("mass", EPropertyType::Float,
                            offsetof(RigidBody, mass), sizeof(float))
                .DisplayName("Mass")
                .Tooltip("Mass in kilograms")
                .EditCondition("type == 2")
                .ClampMin(0.001)
                .Units("kg")
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("drag", EPropertyType::Float,
                            offsetof(RigidBody, drag), sizeof(float))
                .DisplayName("Drag")
                .Tooltip("Linear damping")
                .EditCondition("type == 2")
                .ClampMin(0)
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("angularDrag", EPropertyType::Float,
                            offsetof(RigidBody, angularDrag), sizeof(float))
                .DisplayName("Angular Drag")
                .Tooltip("Angular damping")
                .EditCondition("type == 2")
                .ClampMin(0)
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("useGravity", EPropertyType::Bool,
                            offsetof(RigidBody, useGravity), sizeof(bool))
                .DisplayName("Use Gravity")
                .EditCondition("type == 2")
                .TypeInfo<bool>()
        )
        .AddProperty(
            PropertyBuilder("isKinematic", EPropertyType::Bool,
                            offsetof(RigidBody, isKinematic), sizeof(bool))
                .DisplayName("Is Kinematic")
                .EditCondition("type == 2")
                .TypeInfo<bool>()
        )
        .AddProperty(
            PropertyBuilder("velocity", EPropertyType::Vec3,
                            offsetof(RigidBody, velocity), sizeof(glm::vec3))
                .DisplayName("Velocity")
                .Tooltip("Current linear velocity")
                .Flags(EPropertyFlags::VisibleAnywhere | EPropertyFlags::Transient)
                .Units("m/s")
                .TypeInfo<glm::vec3>()
        )
        .AddProperty(
            PropertyBuilder("angularVelocity", EPropertyType::Vec3,
                            offsetof(RigidBody, angularVelocity), sizeof(glm::vec3))
                .DisplayName("Angular Velocity")
                .Tooltip("Current angular velocity")
                .Flags(EPropertyFlags::VisibleAnywhere | EPropertyFlags::Transient)
                .Units("rad/s")
                .TypeInfo<glm::vec3>()
        )
        .registerStruct();
    
    // MeshRenderer component
    StructBuilder("MeshRenderer")
        .TypeInfo<MeshRenderer>()
        .DisplayName("Mesh Renderer")
        .Category("Rendering")
        .AddProperty(
            PropertyBuilder("meshId", EPropertyType::UInt32,
                            offsetof(MeshRenderer, meshId), sizeof(uint32_t))
                .DisplayName("Mesh")
                .Tooltip("Mesh asset reference")
                .Flags(EPropertyFlags::EditAnywhere | EPropertyFlags::AssetRef)
                .AllowedClasses("Mesh")
                .CustomWidget("AssetPicker")
        )
        .AddProperty(
            PropertyBuilder("materialId", EPropertyType::UInt32,
                            offsetof(MeshRenderer, materialId), sizeof(uint32_t))
                .DisplayName("Material")
                .Tooltip("Material asset reference")
                .Flags(EPropertyFlags::EditAnywhere | EPropertyFlags::AssetRef)
                .AllowedClasses("Material")
                .CustomWidget("AssetPicker")
        )
        .AddProperty(
            PropertyBuilder("castShadows", EPropertyType::Bool,
                            offsetof(MeshRenderer, castShadows), sizeof(bool))
                .DisplayName("Cast Shadows")
                .TypeInfo<bool>()
        )
        .AddProperty(
            PropertyBuilder("receiveShadows", EPropertyType::Bool,
                            offsetof(MeshRenderer, receiveShadows), sizeof(bool))
                .DisplayName("Receive Shadows")
                .TypeInfo<bool>()
        )
        .AddProperty(
            PropertyBuilder("layer", EPropertyType::UInt32,
                            offsetof(MeshRenderer, layer), sizeof(uint32_t))
                .DisplayName("Render Layer")
                .Tooltip("Render layer for camera culling")
                .CustomWidget("LayerMask")
                .TypeInfo<uint32_t>()
        )
        .registerStruct();
    
    // Light component
    registry.registerEnum("Light::Type", {
        {"Directional", 0},
        {"Point", 1},
        {"Spot", 2}
    });
    
    StructBuilder("Light")
        .TypeInfo<Light>()
        .DisplayName("Light")
        .Category("Lighting")
        .AddProperty(
            PropertyBuilder("type", EPropertyType::Enum,
                            offsetof(Light, type), sizeof(Light::Type))
                .DisplayName("Light Type")
                .EnumValues({{"Directional", 0}, {"Point", 1}, {"Spot", 2}})
        )
        .AddProperty(
            PropertyBuilder("color", EPropertyType::Vec3,
                            offsetof(Light, color), sizeof(glm::vec3))
                .DisplayName("Color")
                .Tooltip("Light color (linear RGB)")
                .CustomWidget("ColorPicker", {{"hdr", "true"}})
                .TypeInfo<glm::vec3>()
        )
        .AddProperty(
            PropertyBuilder("intensity", EPropertyType::Float,
                            offsetof(Light, intensity), sizeof(float))
                .DisplayName("Intensity")
                .Tooltip("Light brightness")
                .ClampMin(0)
                .UIRange(0, 100)
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("range", EPropertyType::Float,
                            offsetof(Light, range), sizeof(float))
                .DisplayName("Range")
                .Tooltip("Light attenuation distance")
                .EditCondition("type != 0")
                .ClampMin(0.1)
                .Units("m")
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("innerAngle", EPropertyType::Float,
                            offsetof(Light, innerAngle), sizeof(float))
                .DisplayName("Inner Angle")
                .Tooltip("Spotlight inner cone angle")
                .EditCondition("type == 2")
                .ClampMin(0)
                .ClampMax(180)
                .Units("°")
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("outerAngle", EPropertyType::Float,
                            offsetof(Light, outerAngle), sizeof(float))
                .DisplayName("Outer Angle")
                .Tooltip("Spotlight outer cone angle")
                .EditCondition("type == 2")
                .ClampMin(0)
                .ClampMax(180)
                .Units("°")
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("castShadows", EPropertyType::Bool,
                            offsetof(Light, castShadows), sizeof(bool))
                .DisplayName("Cast Shadows")
                .TypeInfo<bool>()
        )
        .registerStruct();
    
    // Camera component
    StructBuilder("Camera")
        .TypeInfo<Camera>()
        .DisplayName("Camera")
        .Category("Rendering")
        .AddProperty(
            PropertyBuilder("fov", EPropertyType::Float,
                            offsetof(Camera, fov), sizeof(float))
                .DisplayName("Field of View")
                .Tooltip("Vertical field of view in degrees")
                .EditCondition("isOrthographic == false")
                .ClampMin(1)
                .ClampMax(179)
                .Units("°")
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("nearPlane", EPropertyType::Float,
                            offsetof(Camera, nearPlane), sizeof(float))
                .DisplayName("Near Plane")
                .Tooltip("Near clipping plane distance")
                .ClampMin(0.001)
                .Units("m")
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("farPlane", EPropertyType::Float,
                            offsetof(Camera, farPlane), sizeof(float))
                .DisplayName("Far Plane")
                .Tooltip("Far clipping plane distance")
                .ClampMin(0.1)
                .Units("m")
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("isOrthographic", EPropertyType::Bool,
                            offsetof(Camera, isOrthographic), sizeof(bool))
                .DisplayName("Orthographic")
                .Tooltip("Use orthographic projection")
                .TypeInfo<bool>()
        )
        .AddProperty(
            PropertyBuilder("orthoSize", EPropertyType::Float,
                            offsetof(Camera, orthoSize), sizeof(float))
                .DisplayName("Orthographic Size")
                .Tooltip("Half-height of the orthographic view")
                .EditCondition("isOrthographic == true")
                .ClampMin(0.001)
                .Units("m")
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("priority", EPropertyType::Int32,
                            offsetof(Camera, priority), sizeof(int))
                .DisplayName("Priority")
                .Tooltip("Higher priority cameras render on top")
                .TypeInfo<int>()
        )
        .registerStruct();
    
    // AudioSource component
    StructBuilder("AudioSource")
        .TypeInfo<AudioSource>()
        .DisplayName("Audio Source")
        .Category("Audio")
        .AddProperty(
            PropertyBuilder("clipPath", EPropertyType::String,
                            offsetof(AudioSource, clipPath), sizeof(std::string))
                .DisplayName("Audio Clip")
                .Tooltip("Path to audio clip asset")
                .Flags(EPropertyFlags::EditAnywhere | EPropertyFlags::AssetRef)
                .AllowedClasses("AudioClip")
                .CustomWidget("AssetPicker")
        )
        .AddProperty(
            PropertyBuilder("volume", EPropertyType::Float,
                            offsetof(AudioSource, volume), sizeof(float))
                .DisplayName("Volume")
                .ClampMin(0)
                .ClampMax(1)
                .UIRange(0, 1)
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("pitch", EPropertyType::Float,
                            offsetof(AudioSource, pitch), sizeof(float))
                .DisplayName("Pitch")
                .ClampMin(0.1)
                .ClampMax(3)
                .UIRange(0.5, 2)
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("minDistance", EPropertyType::Float,
                            offsetof(AudioSource, minDistance), sizeof(float))
                .DisplayName("Min Distance")
                .Tooltip("Distance at which attenuation starts")
                .EditCondition("is3D == true")
                .ClampMin(0.1)
                .Units("m")
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("maxDistance", EPropertyType::Float,
                            offsetof(AudioSource, maxDistance), sizeof(float))
                .DisplayName("Max Distance")
                .Tooltip("Distance at which sound is inaudible")
                .EditCondition("is3D == true")
                .ClampMin(1)
                .Units("m")
                .TypeInfo<float>()
        )
        .AddProperty(
            PropertyBuilder("loop", EPropertyType::Bool,
                            offsetof(AudioSource, loop), sizeof(bool))
                .DisplayName("Loop")
                .TypeInfo<bool>()
        )
        .AddProperty(
            PropertyBuilder("playOnStart", EPropertyType::Bool,
                            offsetof(AudioSource, playOnStart), sizeof(bool))
                .DisplayName("Play On Start")
                .TypeInfo<bool>()
        )
        .AddProperty(
            PropertyBuilder("is3D", EPropertyType::Bool,
                            offsetof(AudioSource, is3D), sizeof(bool))
                .DisplayName("3D Sound")
                .Tooltip("Enable spatial audio")
                .TypeInfo<bool>()
        )
        .registerStruct();
    
    // Script component
    StructBuilder("Script")
        .TypeInfo<Script>()
        .DisplayName("Script")
        .Category("Scripting")
        .AddProperty(
            PropertyBuilder("scriptPath", EPropertyType::String,
                            offsetof(Script, scriptPath), sizeof(std::string))
                .DisplayName("Script")
                .Tooltip("C# script class to run")
                .Flags(EPropertyFlags::EditAnywhere | EPropertyFlags::AssetRef)
                .AllowedClasses("MonoScript")
                .CustomWidget("ScriptPicker")
        )
        .registerStruct();
    
    // Animator component
    StructBuilder("Animator")
        .TypeInfo<Animator>()
        .DisplayName("Animator")
        .Category("Animation")
        .AddProperty(
            PropertyBuilder("controllerPath", EPropertyType::String,
                            offsetof(Animator, controllerPath), sizeof(std::string))
                .DisplayName("Controller")
                .Tooltip("Animation controller asset")
                .Flags(EPropertyFlags::EditAnywhere | EPropertyFlags::AssetRef)
                .AllowedClasses("AnimatorController")
                .CustomWidget("AssetPicker")
        )
        .registerStruct();
    
    // ParticleEmitter component
    StructBuilder("ParticleEmitter")
        .TypeInfo<ParticleEmitter>()
        .DisplayName("Particle Emitter")
        .Category("Effects")
        .AddProperty(
            PropertyBuilder("effectPath", EPropertyType::String,
                            offsetof(ParticleEmitter, effectPath), sizeof(std::string))
                .DisplayName("Effect")
                .Tooltip("Particle effect asset")
                .Flags(EPropertyFlags::EditAnywhere | EPropertyFlags::AssetRef)
                .AllowedClasses("ParticleEffect")
                .CustomWidget("AssetPicker")
        )
        .AddProperty(
            PropertyBuilder("playOnStart", EPropertyType::Bool,
                            offsetof(ParticleEmitter, playOnStart), sizeof(bool))
                .DisplayName("Play On Start")
                .TypeInfo<bool>()
        )
        .AddProperty(
            PropertyBuilder("loop", EPropertyType::Bool,
                            offsetof(ParticleEmitter, loop), sizeof(bool))
                .DisplayName("Loop")
                .TypeInfo<bool>()
        )
        .registerStruct();
}

// Initialize reflection system on startup
namespace {
    struct ReflectionInitializer {
        ReflectionInitializer() {
            RegisterBuiltInReflections();
        }
    };
    static ReflectionInitializer s_reflectionInit;
}

} // namespace Sanic
